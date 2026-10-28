// Authentication callbacks must not accept lookalike origins or path prefixes.
#import "../hle_web_ui.m"
#include <assert.h>
int main(void) { @autoreleasepool {
    NSURL *cb=[NSURL URLWithString:@"https://login.live.com/oauth20_desktop.srf"];
    assert(httpsURL(cb)); assert(!httpsURL([NSURL URLWithString:@"http://login.live.com/"]));
    assert(!httpsURL([NSURL URLWithString:@"https://user:password@login.live.com/"]));
    assert(matchesCallback([NSURL URLWithString:@"https://login.live.com/oauth20_desktop.srf?code=test#fragment"],cb));
    assert(!matchesCallback([NSURL URLWithString:@"https://login.live.com.attacker.invalid/oauth20_desktop.srf"],cb));
    assert(!matchesCallback([NSURL URLWithString:@"https://login.live.com/oauth20_desktop.srf.attacker"],cb));
    assert(!matchesCallback([NSURL URLWithString:@"https://login.live.com:444/oauth20_desktop.srf"],cb));
    assert(!matchesCallback([NSURL URLWithString:@"http://login.live.com/oauth20_desktop.srf"],cb));
    assert(!matchesCallback([NSURL URLWithString:@"https://user@login.live.com/oauth20_desktop.srf"],cb));
    puts("Web applet HTTPS and callback origin/path matching: OK");
} }
