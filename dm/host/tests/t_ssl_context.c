// A network library must be able to own its SSL configuration while offline.
// Creating that configuration must never make connections or authentication succeed.
#include "../hle_net.c"
#include <assert.h>

uint64_t now_ns(void) { return 1; }
static int64_t slept_ns;
void dm_guest_sleep_ns(int64_t n) { slept_ns=n; }
void dm_thread_native_wait(int entering) { (void)entering; }
void dm_bigstack_enter(void) { abort(); } // This unit calls implementations directly, never registered entry stubs.

int main(void) {
    unsetenv("DM_NETWORK");
    assert(dm_sk_poll(NULL,0,33)==0 && slept_ns==33000000);
    assert(dm_sk_poll(NULL,0,-1)==0 && slept_ns==100000000);
    struct { int32_t fd; int16_t events,revents; } idle={-1,1,7};
    assert(dm_sk_poll(&idle,1,0)==0 && !idle.revents);
    idle.fd=32; assert(dm_sk_poll(&idle,1,0)==-1 && dm_net_errno()==50);
    int64_t tv[2]={0,50000}; uint64_t empty[16]={0};
    assert(dm_sk_select(1024,empty,NULL,NULL,tv)==0 && slept_ns==50000000);
    empty[0]=1; assert(dm_sk_select(1,empty,NULL,NULL,tv)==-1 && dm_net_errno()==50);
    uint64_t a = 0, b = 0;
    assert(h_ssl_CtxCreate(NULL, 1) != RES_OK);
    assert(h_ssl_CtxCreate(&a, 0) != RES_OK && a == 0);
    assert(h_ssl_CtxCreate(&a, 0x05000001) == RES_OK && a != 0);
    uint64_t old = a;
    assert(h_ssl_CtxCreate(&a, 1) != RES_OK && a == old);
    assert(h_ssl_CtxCreate(&b, 1) == RES_OK && b != a && b != 0);
    assert(h_nifm_Available() == 0);
    assert(h_sk_Socket(2, 1, 0) == -1);
    assert(h_ssl_CnCreate(&b, &a) != RES_OK);
    assert(h_ssl_CnHandshake(&b) != RES_OK);
    assert(h_ssl_CtxDestroy(&a) == RES_OK && a == 0);
    assert(h_ssl_CtxDestroy(&a) != RES_OK);
    assert(h_ssl_CtxCreate(&a, 1) == RES_OK && a != old);
    assert(h_ssl_CtxDestroy(&a) == RES_OK);
    assert(h_ssl_CtxDestroy(&b) == RES_OK && b == 0);
    assert(g_ssl_contexts == NULL);
    puts("ssl context lifecycle and offline transport: passed");
    return 0;
}
