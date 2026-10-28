// Malformed applet buffers must not overrun guest memory or return partial URLs.
#define DM_WEB_TEST 1
#include "../hle_web.c"
#include <assert.h>
static void entry(unsigned char *buf, unsigned *pos, uint16_t type, const char *value, uint16_t len) {
    *pos=(unsigned)put(buf,*pos,type,value,len);
}
int main(void) {
    unsigned char arg[8192]={0},rv[8192]; dm_web_args out;
    uint16_t count=2; uint32_t shim=5; memcpy(arg,&count,2); memcpy(arg+4,&shim,4);
    unsigned pos=8; entry(arg,&pos,1,"https://login.live.com/",sizeof("https://login.live.com/")); entry(arg,&pos,3,"https://example.com/cb",23);
    assert(!dm_web_parse(&out,arg)); assert(!strcmp(out.initial,"https://login.live.com/"));
    assert(!strcmp(out.callback,"https://example.com/cb"));
    arg[4]=4; assert(dm_web_parse(&out,arg)); arg[4]=5;
    arg[2]=255; assert(!dm_web_parse(&out,arg));
    arg[10]=255; arg[11]=255; assert(dm_web_parse(&out,arg));
    memset(arg,0,sizeof(arg)); count=1; memcpy(arg,&count,2); memcpy(arg+4,&shim,4); pos=8;
    entry(arg,&pos,1,"abcd",4); assert(dm_web_parse(&out,arg));
    dm_web_result(rv,3,"https://example.com/cb?code=opaque");
    assert(u16(rv)==3 && u32(rv+4)==5 && u16(rv+8)==1 && u32(rv+16)==3);
    assert(u16(rv+20)==2 && !strcmp((char *)rv+28,"https://example.com/cb?code=opaque"));
    dm_web_result(rv,0,NULL); assert(u16(rv)==1 && u32(rv+16)==0 && !rv[20]);
    puts("Web applet TLV parsing and return ABI: OK");
}
