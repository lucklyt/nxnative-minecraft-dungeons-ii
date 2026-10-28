// Native WebKit login. Default TLS validation and isolated web storage; no URL,
// cookie, password, authorization code or token is written to the host log.
#import <TargetConditionals.h>
#import <WebKit/WebKit.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#import <Cocoa/Cocoa.h>
#endif
#include "net_web.h"

static BOOL httpsURL(NSURL *u) { return u && [u.scheme.lowercaseString isEqual:@"https"] && u.host.length && !u.user.length && !u.password.length; }
static NSNumber *urlPort(NSURL *u) { return u.port ?: ([u.scheme.lowercaseString isEqual:@"https"] ? @443 : nil); }
// Match the registered callback's origin and exact path, never a string prefix
// such as example.com.attacker.invalid. OAuth query/fragment remain opaque.
static BOOL matchesCallback(NSURL *url, NSURL *cb) {
    if (!url || !cb || !cb.scheme.length || url.user.length || url.password.length) return NO;
    NSString *up=url.path.length ? url.path : @"/", *cp=cb.path.length ? cb.path : @"/";
    BOOL port=(!urlPort(url) && !urlPort(cb)) || [urlPort(url) isEqual:urlPort(cb)];
    return [url.scheme.lowercaseString isEqual:cb.scheme.lowercaseString] &&
        [(url.host ?: @"").lowercaseString isEqual:(cb.host ?: @"").lowercaseString] && port && [up isEqual:cp] &&
        (!cb.query.length || [url.query isEqual:cb.query]);
}

@interface DMWebLogin : NSObject <WKNavigationDelegate, WKUIDelegate
#if !TARGET_OS_IPHONE
, NSWindowDelegate
#endif
>
@property WKWebView *web;
@property NSURL *callback;
@property dispatch_semaphore_t done;
@property unsigned reason;
@property NSString *lastURL;
@property BOOL finished;
#if TARGET_OS_IPHONE
@property UINavigationController *navigation;
#else
@property NSWindow *window;
#endif
- (void)finish:(unsigned)reason url:(NSString *)url;
- (BOOL)open:(NSURL *)url;
@end
@implementation DMWebLogin
- (void)finish:(unsigned)reason url:(NSString *)url {
    if (self.finished) return;
    self.finished=YES; self.reason=reason; self.lastURL=url;
    [self.web stopLoading]; self.web.navigationDelegate=nil; self.web.UIDelegate=nil;
#if TARGET_OS_IPHONE
    [self.navigation dismissViewControllerAnimated:YES completion:^{ dispatch_semaphore_signal(self.done); }];
#else
    self.window.delegate=nil; [self.window close]; dispatch_semaphore_signal(self.done);
#endif
}
- (void)cancel:(id)sender { (void)sender; [self finish:0 url:nil]; }
- (BOOL)open:(NSURL *)url {
    WKWebViewConfiguration *cfg=[WKWebViewConfiguration new];
    cfg.websiteDataStore=WKWebsiteDataStore.nonPersistentDataStore;
    self.web=[[WKWebView alloc] initWithFrame:CGRectZero configuration:cfg];
    self.web.navigationDelegate=self; self.web.UIDelegate=self;
#if TARGET_OS_IPHONE
    UIViewController *root=nil;
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes)
        if ([scene isKindOfClass:UIWindowScene.class] && scene.activationState==UISceneActivationStateForegroundActive)
            for (UIWindow *w in ((UIWindowScene *)scene).windows) if (w.isKeyWindow) root=w.rootViewController;
    if (!root || root.presentedViewController) return NO;
    UIViewController *vc=[UIViewController new]; vc.view=self.web; vc.title=@"Sign in";
    vc.navigationItem.leftBarButtonItem=[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemCancel target:self action:@selector(cancel:)];
    self.navigation=[[UINavigationController alloc] initWithRootViewController:vc];
    self.navigation.modalPresentationStyle=UIModalPresentationOverFullScreen;
    [root presentViewController:self.navigation animated:YES completion:nil];
#else
    if (!NSApp || !NSApp.windows.count) return NO;
    self.window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,900,680) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
    self.window.releasedWhenClosed=NO; self.window.title=@"Sign in"; self.window.delegate=self;
    self.web.frame=self.window.contentView.bounds; self.web.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
    [self.window.contentView addSubview:self.web]; [self.window center]; [self.window makeKeyAndOrderFront:nil];
#endif
    [self.web loadRequest:[NSURLRequest requestWithURL:url]]; return YES;
}
#if !TARGET_OS_IPHONE
- (BOOL)windowShouldClose:(NSWindow *)sender { (void)sender; [self finish:0 url:nil]; return NO; }
#endif
- (void)webView:(WKWebView *)web decidePolicyForNavigationAction:(WKNavigationAction *)action decisionHandler:(void (^)(WKNavigationActionPolicy))handler {
    (void)web; NSURL *url=action.request.URL;
    if (action.targetFrame.mainFrame && matchesCallback(url,self.callback)) {
        handler(WKNavigationActionPolicyCancel);
        if (url.absoluteString.length < DM_WEB_URL_SIZE && [url.absoluteString lengthOfBytesUsingEncoding:NSUTF8StringEncoding]<DM_WEB_URL_SIZE)
            [self finish:3 url:url.absoluteString];
        else [self finish:7 url:nil];
        return;
    }
    handler(httpsURL(url) ? WKNavigationActionPolicyAllow : WKNavigationActionPolicyCancel);
}
- (void)webView:(WKWebView *)web didFailProvisionalNavigation:(WKNavigation *)navigation withError:(NSError *)error {
    (void)web; (void)navigation; if (error.code!=NSURLErrorCancelled) [self finish:7 url:nil];
}
- (void)webView:(WKWebView *)web didFailNavigation:(WKNavigation *)navigation withError:(NSError *)error {
    (void)web; (void)navigation; if (error.code!=NSURLErrorCancelled) [self finish:7 url:nil];
}
- (void)webViewWebContentProcessDidTerminate:(WKWebView *)web { (void)web; [self finish:7 url:nil]; }
- (WKWebView *)webView:(WKWebView *)web createWebViewWithConfiguration:(WKWebViewConfiguration *)configuration forNavigationAction:(WKNavigationAction *)action windowFeatures:(WKWindowFeatures *)features {
    (void)configuration; (void)features;
    if (!action.targetFrame && httpsURL(action.request.URL)) [web loadRequest:action.request];
    return nil;
}
// WebKit applies its normal trust evaluation. Never install a server-trust bypass.
@end
int dm_web_present(const dm_web_args *arg,unsigned *reason,char *url,size_t cap) {
    if (!arg || !reason || !url || !cap || NSThread.isMainThread) return -1;
    @autoreleasepool {
        NSURL *initial=[NSURL URLWithString:@(arg->initial)];
        NSURL *callback=arg->callback[0] ? [NSURL URLWithString:@(arg->callback)] : nil;
        if (!httpsURL(initial) || (arg->callback[0] && (!callback.scheme.length || callback.user.length || callback.password.length))) return -1;
        DMWebLogin *login=[DMWebLogin new]; login.callback=callback;
        login.done=dispatch_semaphore_create(0); login.reason=7;
        dispatch_async(dispatch_get_main_queue(), ^{
            if (![login open:initial]) { login.finished=YES; dispatch_semaphore_signal(login.done); }
        });
        dispatch_semaphore_wait(login.done,DISPATCH_TIME_FOREVER);
        *reason=login.reason; url[0]=0;
        const char *result=login.lastURL.UTF8String;
        if (result) { if (strlen(result)>=cap) return -1; memcpy(url,result,strlen(result)+1); }
        return login.reason==7 ? -1 : 0;
    }
}
