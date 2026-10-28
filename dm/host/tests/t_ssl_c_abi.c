// Exercise the actual Result/out-byte C ABI used by the game's libcurl.
#include "../hle_net.c"
#include <assert.h>
#include <arpa/inet.h>
#include <time.h>
uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000000000+t.tv_nsec; }
void dm_guest_sleep_ns(int64_t n) { struct timespec t={n/1000000000,n%1000000000}; nanosleep(&t,NULL); }
void dm_thread_native_wait(int entering) { (void)entering; }
void dm_bigstack_enter(void) { abort(); } // Implementations are called directly.
int main(int argc,char **argv) {
    assert(argc==4); unsigned port=(unsigned)atoi(argv[1]);
    unsetenv("DM_NETWORK"); uint64_t ctx=0,cn=0,id=0;
    id=99; assert(!h_cssl_CtxId(&ctx,&id) && !id);
    id=99; assert(!h_cssl_Id(&cn,&id) && !id);
    assert(h_cssl_CtxId(NULL,&id) && h_cssl_CtxId(&ctx,NULL));
    assert(h_cssl_Id(NULL,&id) && h_cssl_Id(&cn,NULL));
    assert(!h_ssl_CtxCreate(&ctx,1)); assert(!h_cssl_CtxId(&ctx,&id) && id);
    assert(h_cssl_Create(&cn,&ctx)); assert(!h_cssl_CtxDestroy(&ctx));
    setenv("DM_NETWORK","1",1); assert(!h_ssl_CtxCreate(&ctx,1));
    FILE *f=fopen(argv[2],"rb"); assert(f); fseek(f,0,SEEK_END); long size=ftell(f); rewind(f);
    char *pem=malloc((size_t)size); assert(pem && fread(pem,1,(size_t)size,f)==(size_t)size); fclose(f);
    uint64_t ca=0; assert(!h_ssl_CtxImportServer(&ctx,&ca,pem,(unsigned)size,1)); free(pem);
    int fd=dm_net_socket(2,1,6); assert(fd>=0);
    struct sockaddr_in address={.sin_len=sizeof address,.sin_family=2,.sin_port=htons((uint16_t)port)};
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK); assert(!dm_net_connect(fd,&address,sizeof address));
    assert(!h_cssl_Create(&cn,&ctx)); assert(!h_cssl_Id(&cn,&id) && id);
    assert(!h_cssl_Socket(&cn,fd)); assert(!h_cssl_Host(&cn,"localhost",10));
    assert(!h_cssl_Verify(&cn,7)); assert(!h_cssl_Mode(&cn,2));
    assert(!h_cssl_Option(&cn,1,1)); assert(!h_cssl_Option(&cn,3,1));
    const unsigned char alpn[]={8,'h','t','t','p','/','1','.','1'};
    assert(!h_cssl_SetAlpn(&cn,alpn,sizeof alpn));
    unsigned char certificates[16384+16]; memset(certificates,0xa5,sizeof certificates);
    unsigned cert_size=0,cert_count=0; Result r; uint64_t deadline=now_ns()+10000000000ull;
    while ((r=h_cssl_Handshake(&cn,&cert_size,&cert_count))==DM_TLS_WOULD_BLOCK) {
        assert(now_ns()<deadline); dm_guest_sleep_ns(1000000);
    }
    assert(!r && !cert_size && !cert_count);
    assert(!h_cssl_Handshake(&cn,NULL,NULL));
    assert(!h_cssl_CertBuffer(&cn,(char *)certificates,16384));
    assert(!h_cssl_Handshake(&cn,&cert_size,&cert_count));
    assert(!r && cert_size>16 && cert_size<=16384 && cert_count>0);
    for (unsigned i=16384;i<sizeof certificates;i++) assert(certificates[i]==0xa5);
    struct { uint32_t size,pad; const unsigned char *data; } detail={0};
    assert(!h_cssl_CertDetail(&cn,&detail,NULL,0));
    assert(detail.size && detail.data>=certificates && detail.data+detail.size<=certificates+cert_size && detail.data[0]==0x30);
    f=fopen(argv[3],"rb"); assert(f); fseek(f,0,SEEK_END); long der_size=ftell(f); rewind(f);
    unsigned char *der=malloc((size_t)der_size); assert(der && fread(der,1,(size_t)der_size,f)==(size_t)der_size); fclose(f);
    assert(detail.size==(unsigned)der_size && !memcmp(detail.data,der,(size_t)der_size)); free(der);
    char protocol[32]={0}; unsigned length=0; int state=0;
    assert(!h_cssl_GetAlpn(&cn,&state,(unsigned char *)protocol,&length,sizeof protocol));
    assert(state==1 && length==8 && !memcmp(protocol,"http/1.1",8));
    int got=99; char response[4096];
    assert(h_cssl_Read(&cn,response,&got,1)==DM_TLS_WOULD_BLOCK && got==0);
    assert(!h_cssl_Mode(&cn,1));
    const char request[]="GET / HTTP/1.0\r\nHost: localhost\r\n\r\n";
    assert(!h_cssl_Write(&cn,request,&got,sizeof request-1) && got==sizeof request-1);
    assert(!h_cssl_Peek(&cn,response,&got,4) && got==4 && !memcmp(response,"HTTP",4));
    assert(!h_cssl_Pending(&cn,&got) && got>=4);
    int used=0;
    do { assert(!h_cssl_Read(&cn,response+used,&got,sizeof response-1-(unsigned)used)); used+=got; assert(used<sizeof response-1); } while (got);
    response[used]=0; assert(strstr(response,"200 OK") && strstr(response,"native-tls-ok"));
    assert(!h_cssl_Destroy(&cn) && !cn); assert(!h_cssl_CtxDestroy(&ctx) && !ctx);
    id=99; assert(!h_cssl_CtxId(&ctx,&id) && !id);
    id=99; assert(!h_cssl_Id(&cn,&id) && !id);
    assert(dm_net_native_fd(fd)>=0); assert(!dm_net_close(fd));
    puts("SSL C ABI: Result/out-byte I/O, WOULD_BLOCK, real certificate chain, ALPN and HTTPS passed");
}
