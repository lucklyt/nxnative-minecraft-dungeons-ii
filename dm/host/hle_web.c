// A real web applet for explicit online mode; the SDK's argument setters and
// return-value accessors continue to run natively against the original TLV ABI.
#include "hle.h"
#include "net_transport.h"
#include "net_web.h"
static unsigned u16(const unsigned char *p) { uint16_t n; memcpy(&n,p,2); return n; }
static unsigned u32(const unsigned char *p) { uint32_t n; memcpy(&n,p,4); return n; }
int dm_web_parse(dm_web_args *out, const void *arg) {
    if (!out || !arg) return -1;
    memset(out,0,sizeof(*out));
    const unsigned char *p=arg; unsigned count=u16(p); size_t pos=8;
    if (u32(p+4)!=5 || count>(DM_WEB_ARG_SIZE-8)/8) return -1;
    unsigned seen=0;
    for (unsigned i=0;i<count;i++) {
        if (pos>DM_WEB_ARG_SIZE-8) return -1;
        unsigned type=u16(p+pos), len=u16(p+pos+2); pos+=8;
        if (len>DM_WEB_ARG_SIZE-pos) return -1;
        char *dst=NULL; size_t cap=0; unsigned bit=0;
        if (type==1) { dst=out->initial; cap=sizeof(out->initial); bit=1; }
        if (type==3) { dst=out->callback; cap=sizeof(out->callback); bit=2; }
        if (dst) {
            if ((seen&bit) || !len || !memchr(p+pos,0,len)) return -1;
            size_t n=strnlen((const char *)p+pos,len);
            if (n>=cap) return -1;
            memcpy(dst,p+pos,n); seen|=bit;
        }
        pos+=len;
    }
    return (seen&1) && out->initial[0] ? 0 : -1;
}
static size_t put(unsigned char *p,size_t pos,uint16_t type,const void *data,uint16_t len) {
    uint32_t zero=0; memcpy(p+pos,&type,2); memcpy(p+pos+2,&len,2); memcpy(p+pos+4,&zero,4);
    memcpy(p+pos+8,data,len); return pos+8+len;
}
void dm_web_result(void *out,unsigned reason,const char *url) {
    unsigned char *p=out; memset(p,0,DM_WEB_RETURN_SIZE);
    uint16_t count=1; uint32_t r=reason; size_t pos=put(p,8,1,&r,4);
    if (url && *url) {
        size_t n=strnlen(url,DM_WEB_URL_SIZE);
        if (n<DM_WEB_URL_SIZE) {
            pos=put(p,pos,2,url,(uint16_t)(n+1)); uint64_t len=n+1;
            put(p,pos,3,&len,8); count=3;
        }
    }
    memcpy(p,&count,2); uint32_t shim=5; memcpy(p+4,&shim,4);
}
#ifndef DM_WEB_TEST
HLE_FORCE_BIG("_ZN2nn3web11ShowWebPageEPNS0_18WebPageReturnValueERKNS0_14ShowWebPageArgE", "native HTTPS web applet in explicit online mode", Result, h_web_Page, void *rv,const void *arg) {
    if (!rv) return RES_FAIL;
    dm_web_result(rv,7,NULL);
    if (!dm_net_enabled()) return RES_FAIL;
    dm_web_args args; if (dm_web_parse(&args,arg)) { LOG("web applet: invalid arguments"); return RES_FAIL; }
    char url[DM_WEB_URL_SIZE]={0}; unsigned reason=7;
    LOG("web applet: opening native HTTPS view (URL redacted)");
    dm_thread_native_wait(1); int result=dm_web_present(&args,&reason,url,sizeof(url)); dm_thread_native_wait(0);
    dm_web_result(rv,reason,url);
    memset(url,0,sizeof(url)); memset(&args,0,sizeof(args));
    LOG("web applet: closed, reason=%u",reason);
    return result ? RES_FAIL : RES_OK;
}
#endif
