// Optional native networking. DM_NETWORK=1 enables real BSD/DNS and verified TLS;
// every other game retains its previous offline behavior. Online account services
// still require genuine user authentication; transport success never supplies tokens.
#include "hle.h"
#include "net_transport.h"
#include "net_tls.h"
#include <stdatomic.h>
void dm_guest_sleep_ns(int64_t ns);

static Result tls_config_result(const char *op,Result r,unsigned a,unsigned b) {
    if (r && dm_net_enabled()) LOG("[RED] ssl configure: %s result=%#x arg0=%#x arg1=%#x",op,r,a,b);
    return r;
}
enum { NIFM_HOLD_NS = 50000000 };
static _Atomic uint64_t g_nifm_submit;
HLE_FORCE("_ZN2nn6socket10InitializeERKNS0_6ConfigE", "native transport when DM_NETWORK=1; offline otherwise", Result, h_socket_Initialize, const void *cfg) { (void)cfg; return RES_OK; }
HLE_FORCE("_ZN2nn6socket10InitializeEPvmmi", "native transport when DM_NETWORK=1; offline otherwise", Result, h_socket_Init4, void *m, uint64_t s, uint64_t a, int c) { (void)m; (void)s; (void)a; (void)c; return RES_OK; }
HLE_FORCE("_ZN2nn6socket8FinalizeEv", "native transport when DM_NETWORK=1; offline otherwise", Result, h_socket_Finalize, void) { return RES_OK; }
HLE_FORCE("_ZN2nn4nifm10InitializeEv", "native transport when DM_NETWORK=1; offline otherwise", Result, h_nifm_Initialize, void) { if (dm_net_enabled()) LOG("network: native BSD/DNS and verified TLS enabled"); return RES_OK; }
HLE_FORCE("_ZN2nn4nifm20SubmitNetworkRequestEv", "native transport when DM_NETWORK=1; offline otherwise", void, h_nifm_Submit, void) { atomic_store(&g_nifm_submit, now_ns() | 1); }
HLE_FORCE("_ZN2nn4nifm20CancelNetworkRequestEv", "native transport when DM_NETWORK=1; offline otherwise", void, h_nifm_Cancel, void) { atomic_store(&g_nifm_submit, 0); }
HLE_FORCE("_ZN2nn4nifm22IsNetworkRequestOnHoldEv", "native transport when DM_NETWORK=1; offline otherwise", int, h_nifm_OnHold, void) { uint64_t t = atomic_load(&g_nifm_submit); return t && now_ns() - t < NIFM_HOLD_NS; }
HLE_FORCE("_ZN2nn4nifm18IsNetworkAvailableEv", "native transport when DM_NETWORK=1; offline otherwise", int, h_nifm_Available, void) { return dm_net_available(); }
HLE_FORCE("_ZN2nn4nifm31HandleNetworkRequestErrorResultEv", "native transport when DM_NETWORK=1; offline otherwise", Result, h_nifm_HandleErr, void) { return dm_net_available() ? RES_OK : RES_FAIL; }
HLE_FORCE("_ZN2nn4nifm26GetCurrentPrimaryIpAddressEPNS_6socket6InAddrE", "native transport when DM_NETWORK=1; offline otherwise", Result, h_nifm_GetIp, uint32_t *a) { if (!a) return RES_FAIL; *a = dm_net_ipv4(); return *a ? RES_OK : RES_FAIL; }
HLE_FORCE("_ZN2nn4nifm22GetCurrentProxySettingEPNS0_12ProxySettingE", "native transport when DM_NETWORK=1; offline otherwise", Result, h_nifm_GetProxy, void *p) { if (!dm_net_enabled() || !p) return RES_FAIL; *(uint8_t *)p = 0; return RES_OK; }
HLE_FORCE("_ZN2nn4nifm27SubmitNetworkRequestAndWaitEv", "native transport when DM_NETWORK=1; offline otherwise", void, h_nifm_SubmitWait, void) { h_nifm_Submit(); dm_guest_sleep_ns(NIFM_HOLD_NS); }
void dm_guest_sleep_ns(int64_t ns);
// Preserve the offline empty-descriptor wait: immediate ENETDOWN made network
// workers spin. Infinite waits yield back periodically for guest shutdown/GC.
int dm_sk_poll(void *f, uint64_t n, int t) {
    if (dm_net_enabled()) return dm_net_poll(f,n,t);
    if (n>4096 || (n && !f)) { dm_net_set_errno(22); return -1; }
    typedef struct { int32_t fd; int16_t events,revents; } idle_pollfd;
    idle_pollfd *p=f;
    for (uint64_t i=0;i<n;i++) if (p[i].fd>=0) { dm_net_set_errno(50); return -1; }
    for (uint64_t i=0;i<n;i++) p[i].revents=0;
    dm_guest_sleep_ns((int64_t)(t<0 ? 100 : t)*1000000); return 0;
}
int dm_sk_select(int n,const uint64_t *r,const uint64_t *w,const uint64_t *e,const int64_t *t) {
    if (dm_net_enabled()) return dm_net_select(n,(void *)r,(void *)w,(void *)e,(void *)t);
    if (n<0 || n>1024 || (t && (t[0]<0 || t[1]<0 || t[1]>999999))) { dm_net_set_errno(22); return -1; }
    const uint64_t *sets[3]={r,w,e};
    for (int k=0;k<3;k++) for (int i=0;sets[k] && i<n;i++) if ((sets[k][i>>6]>>(i&63))&1) { dm_net_set_errno(50); return -1; }
    int64_t ns=100000000;
    if (t) ns=t[0]>(INT64_MAX-t[1]*1000)/1000000000 ? INT64_MAX : t[0]*1000000000+t[1]*1000;
    dm_guest_sleep_ns(ns); return 0;
}
HLE_FORCE("_ZN2nn6socket6SocketENS0_6FamilyENS0_4TypeENS0_8ProtocolE", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Socket, int a, int b, int c) { return dm_net_socket(a,b,c); }
HLE_FORCE("_ZN2nn6socket6SocketEiii", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Socket_1, int a, int b, int c) { return dm_net_socket(a,b,c); }
HLE_FORCE("nnsocketSocket", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Socket_2, int a, int b, int c) { return dm_net_socket(a,b,c); }
HLE_FORCE("_ZN2nn6socket4BindEiPKNS0_8SockAddrEj", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Bind, int s, const void *a, unsigned n) { return dm_net_bind(s,a,n); }
HLE_FORCE("_ZN2nn6socket4BindEiPK8sockaddrj", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Bind_1, int s, const void *a, unsigned n) { return dm_net_bind(s,a,n); }
HLE_FORCE("nnsocketBind", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Bind_2, int s, const void *a, unsigned n) { return dm_net_bind(s,a,n); }
HLE_FORCE("_ZN2nn6socket7ConnectEiPKNS0_8SockAddrEj", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Connect, int s, const void *a, unsigned n) { return dm_net_connect(s,a,n); }
HLE_FORCE("_ZN2nn6socket7ConnectEiPK8sockaddrj", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Connect_1, int s, const void *a, unsigned n) { return dm_net_connect(s,a,n); }
HLE_FORCE("nnsocketConnect", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Connect_2, int s, const void *a, unsigned n) { return dm_net_connect(s,a,n); }
HLE_FORCE("_ZN2nn6socket6ListenEii", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Listen, int s, int n) { return dm_net_listen(s,n); }
HLE_FORCE("_ZN2nn6socket6AcceptEiPNS0_8SockAddrEPj", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Accept, int s, void *a, unsigned *n) { return dm_net_accept(s,a,n); }
HLE_FORCE("_ZN2nn6socket6AcceptEiP8sockaddrPj", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Accept_1, int s, void *a, unsigned *n) { return dm_net_accept(s,a,n); }
HLE_FORCE("_ZN2nn6socket5CloseEi", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Close, int s) { return dm_net_close(s); }
HLE_FORCE("nnsocketClose", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Close_1, int s) { return dm_net_close(s); }
HLE_FORCE("_ZN2nn6socket8ShutdownEii", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Shutdown, int s, int h) { return dm_net_shutdown(s,h); }
HLE_FORCE("_ZN2nn6socket11GetPeerNameEiPNS0_8SockAddrEPj", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_GetPeerName, int s, void *a, unsigned *n) { return dm_net_name(s,a,n,1); }
HLE_FORCE("_ZN2nn6socket11GetPeerNameEiP8sockaddrPj", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_GetPeerName_1, int s, void *a, unsigned *n) { return dm_net_name(s,a,n,1); }
HLE_FORCE("nnsocketGetPeerName", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_GetPeerName_2, int s, void *a, unsigned *n) { return dm_net_name(s,a,n,1); }
HLE_FORCE("_ZN2nn6socket11GetSockNameEiPNS0_8SockAddrEPj", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_GetSockName, int s, void *a, unsigned *n) { return dm_net_name(s,a,n,0); }
HLE_FORCE("_ZN2nn6socket11GetSockNameEiP8sockaddrPj", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_GetSockName_1, int s, void *a, unsigned *n) { return dm_net_name(s,a,n,0); }
HLE_FORCE("nnsocketGetSockName", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_GetSockName_2, int s, void *a, unsigned *n) { return dm_net_name(s,a,n,0); }
HLE_FORCE("_ZN2nn6socket10GetSockOptEiNS0_5LevelENS0_6OptionEPvPj", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_GetSockOpt, int s, int l, int o, void *v, unsigned *n) { return dm_net_opt(s,l,o,v,n,0); }
HLE_FORCE("_ZN2nn6socket10GetSockOptEiiiPvPj", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_GetSockOpt_1, int s, int l, int o, void *v, unsigned *n) { return dm_net_opt(s,l,o,v,n,0); }
HLE_FORCE("nnsocketGetSockOpt", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_GetSockOpt_2, int s, int l, int o, void *v, unsigned *n) { return dm_net_opt(s,l,o,v,n,0); }
HLE_FORCE("_ZN2nn6socket10SetSockOptEiNS0_5LevelENS0_6OptionEPKvj", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_SetSockOpt, int s, int l, int o, const void *v, unsigned n) { return dm_net_opt(s,l,o,(void *)v,&n,1); }
HLE_FORCE("_ZN2nn6socket10SetSockOptEiiiPKvj", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_SetSockOpt_1, int s, int l, int o, const void *v, unsigned n) { return dm_net_opt(s,l,o,(void *)v,&n,1); }
HLE_FORCE("nnsocketSetSockOpt", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_SetSockOpt_2, int s, int l, int o, const void *v, unsigned n) { return dm_net_opt(s,l,o,(void *)v,&n,1); }
HLE_FORCE("_ZN2nn6socket5FcntlEiNS0_12FcntlCommandEz", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Fcntl, int s, int c, uint64_t v) { return dm_net_fcntl(s,c,v); }
HLE_FORCE("_ZN2nn6socket5FcntlEiiz", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Fcntl_1, int s, int c, uint64_t v) { return dm_net_fcntl(s,c,v); }
HLE_FORCE("nnsocketFcntl", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Fcntl_2, int s, int c, uint64_t v) { return dm_net_fcntl(s,c,v); }
HLE_FORCE("_ZN2nn6socket5IoctlEijPvm", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Ioctl, int s, unsigned r, void *v, uint64_t n) { return dm_net_ioctl(s,r,v,n); }
HLE_FORCE_BIG("_ZN2nn6socket4PollEPNS0_6PollFdEmi", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Poll, void *f, uint64_t n, int t) { return dm_sk_poll(f,n,t); }
HLE_FORCE_BIG("_ZN2nn6socket4PollEP6pollfdmi", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Poll_1, void *f, uint64_t n, int t) { return dm_sk_poll(f,n,t); }
HLE_FORCE_BIG("nnsocketPoll", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Poll_2, void *f, uint64_t n, int t) { return dm_sk_poll(f,n,t); }
HLE_FORCE_BIG("_ZN2nn6socket6SelectEiPNS0_5FdSetES2_S2_PNS0_7TimeValE", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Select, int n, void *r, void *w, void *e, void *t) { return dm_sk_select(n,r,w,e,t); }
HLE_FORCE_BIG("_ZN2nn6socket6SelectEiP6fd_setS2_S2_P7timeval", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_Select_1, int n, void *r, void *w, void *e, void *t) { return dm_sk_select(n,r,w,e,t); }
HLE_FORCE("_ZN2nn6socket4RecvEiPvmNS0_7MsgFlagE", "native transport when DM_NETWORK=1; offline otherwise", long, h_sk_Recv, int s, void *b, uint64_t n, int f) { return dm_net_recv(s,b,n,f,NULL,NULL); }
HLE_FORCE("_ZN2nn6socket4RecvEiPvmi", "native transport when DM_NETWORK=1; offline otherwise", long, h_sk_Recv_1, int s, void *b, uint64_t n, int f) { return dm_net_recv(s,b,n,f,NULL,NULL); }
HLE_FORCE("nnsocketRecv", "native transport when DM_NETWORK=1; offline otherwise", long, h_sk_Recv_2, int s, void *b, uint64_t n, int f) { return dm_net_recv(s,b,n,f,NULL,NULL); }
HLE_FORCE_BIG("_ZN2nn6socket4ReadEiPvm", "native transport when DM_NETWORK=1; offline otherwise", long, h_sk_Read, int s, void *b, uint64_t n) { return dm_net_read(s,b,n); }
HLE_FORCE("_ZN2nn6socket8RecvFromEiPvmNS0_7MsgFlagEPNS0_8SockAddrEPj", "native transport when DM_NETWORK=1; offline otherwise", long, h_sk_RecvFrom, int s, void *b, uint64_t n, int f, void *a, unsigned *al) { return dm_net_recv(s,b,n,f,a,al); }
HLE_FORCE("_ZN2nn6socket8RecvFromEiPvmiP8sockaddrPj", "native transport when DM_NETWORK=1; offline otherwise", long, h_sk_RecvFrom_1, int s, void *b, uint64_t n, int f, void *a, unsigned *al) { return dm_net_recv(s,b,n,f,a,al); }
HLE_FORCE("_ZN2nn6socket4SendEiPKvmNS0_7MsgFlagE", "native transport when DM_NETWORK=1; offline otherwise", long, h_sk_Send, int s, const void *b, uint64_t n, int f) { return dm_net_send(s,b,n,f,NULL,0); }
HLE_FORCE("_ZN2nn6socket4SendEiPKvmi", "native transport when DM_NETWORK=1; offline otherwise", long, h_sk_Send_1, int s, const void *b, uint64_t n, int f) { return dm_net_send(s,b,n,f,NULL,0); }
HLE_FORCE("nnsocketSend", "native transport when DM_NETWORK=1; offline otherwise", long, h_sk_Send_2, int s, const void *b, uint64_t n, int f) { return dm_net_send(s,b,n,f,NULL,0); }
HLE_FORCE_BIG("_ZN2nn6socket5WriteEiPKvm", "native transport when DM_NETWORK=1; offline otherwise", long, h_sk_Write, int s, const void *b, uint64_t n) { return dm_net_write(s,b,n); }
HLE_FORCE("_ZN2nn6socket6SendToEiPKvmNS0_7MsgFlagEPKNS0_8SockAddrEj", "native transport when DM_NETWORK=1; offline otherwise", long, h_sk_SendTo, int s, const void *b, uint64_t n, int f, const void *a, unsigned al) { return dm_net_send(s,b,n,f,a,al); }
HLE_FORCE("_ZN2nn6socket6SendToEiPKvmiPK8sockaddrj", "native transport when DM_NETWORK=1; offline otherwise", long, h_sk_SendTo_1, int s, const void *b, uint64_t n, int f, const void *a, unsigned al) { return dm_net_send(s,b,n,f,a,al); }
HLE_FORCE_BIG("_ZN2nn6socket11GetAddrInfoEPKcS2_PKNS0_8AddrInfoEPPS3_", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_GetAddrInfo, const char *h, const char *s, const void *hint, void **res) { return dm_net_getaddrinfo(h,s,hint,res); }
HLE_FORCE_BIG("_ZN2nn6socket11GetAddrInfoEPKcS2_PK8addrinfoPPS3_", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_GetAddrInfo_1, const char *h, const char *s, const void *hint, void **res) { return dm_net_getaddrinfo(h,s,hint,res); }
HLE_FORCE("_ZN2nn6socket12FreeAddrInfoEPNS0_8AddrInfoE", "native transport when DM_NETWORK=1; offline otherwise", void, h_sk_FreeAddrInfo, void *ai) { dm_net_freeaddrinfo(ai); }
HLE_FORCE("_ZN2nn6socket12FreeAddrInfoEP8addrinfo", "native transport when DM_NETWORK=1; offline otherwise", void, h_sk_FreeAddrInfo_1, void *ai) { dm_net_freeaddrinfo(ai); }
HLE_FORCE_BIG("_ZN2nn6socket16GetHostEntByNameEPKcPKNS0_14ResolverOptionEm", "native transport when DM_NETWORK=1; offline otherwise", void *, h_sk_GetHostEnt, const char *h, const void *o, uint64_t n) { (void)o; (void)n; return dm_net_gethostbyname(h); }
HLE_FORCE_BIG("nnsocketGetHostByNameWithOptions", "native transport when DM_NETWORK=1; offline otherwise", void *, h_sk_GetHostEnt_1, const char *h, const void *o, uint64_t n) { (void)o; (void)n; return dm_net_gethostbyname(h); }
HLE_FORCE_BIG("_ZN2nn6socket11GetNameInfoEPK8sockaddrjPcjS4_ji", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_GetNameInfo, const void *a, unsigned n, char *h, unsigned hn, char *s, unsigned sn, int f) { return dm_net_getnameinfo(a,n,h,hn,s,sn,f); }
HLE_FORCE("_ZN2nn6socket12GetLastErrorEv", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_GetLastError, void) { return dm_net_errno(); }
HLE_FORCE("_ZN2nn6socket12GetLastErrnoEv", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_GetLastError_1, void) { return dm_net_errno(); }
HLE_FORCE("nnsocketGetLastErrno", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_GetLastError_2, void) { return dm_net_errno(); }
HLE_FORCE("nnsocketSetLastErrno", "native transport when DM_NETWORK=1; offline otherwise", void, h_sk_SetLastErrno, int e) { dm_net_set_errno(e); }
// Resolver option helpers remain native: key 0x2711 generates a local random
// request ID (SDK 20.5.15 resolver::CreateRandomId), not a service connection.
HLE_FORCE_BIG("_ZN2nn6socket7EventFdEmNS0_12EventFdFlagsE", "native event counter in explicit network mode", int, h_sk_EventFd, uint64_t v, int f) { return dm_net_eventfd(v,f); }
HLE_FORCE_BIG("_ZN2nn6socket11EventFdReadEiPm", "read native event counter", int, h_sk_EventFdRead, int s, uint64_t *v) { return dm_net_read(s,v,8)==8 ? 0 : -1; }
HLE_FORCE_BIG("_ZN2nn6socket12EventFdWriteEim", "write native event counter", int, h_sk_EventFdWrite, int s, uint64_t v) { return dm_net_write(s,&v,8)==8 ? 0 : -1; }
typedef struct ssl_local_context { void *guest; uint64_t id; int version; struct ssl_local_context *next; } ssl_local_context;
static pthread_mutex_t g_ssl_context_mu = PTHREAD_MUTEX_INITIALIZER;
static ssl_local_context *g_ssl_contexts;
static uint64_t g_ssl_context_id;
HLE_FORCE("_ZN2nn3ssl7Context6CreateENS1_10SslVersionE", "create a local SSL context; transport remains offline", Result, h_ssl_CtxCreate, void *self, int v) { if (dm_net_enabled()) LOG("ssl: context create version=%#x", v);
    if (!self || !v) return RES_FAIL;
    ssl_local_context *ctx = calloc(1, sizeof *ctx);
    if (!ctx) return RES_FAIL;
    pthread_mutex_lock(&g_ssl_context_mu);
    for (ssl_local_context *p = g_ssl_contexts; p; p = p->next) if (p->guest == self) {
        pthread_mutex_unlock(&g_ssl_context_mu); free(ctx); return RES_FAIL;
    }
    if (dm_net_enabled() && dm_tls_context_create(self, v)) { pthread_mutex_unlock(&g_ssl_context_mu); free(ctx); return RES_FAIL; }
    ctx->guest = self; ctx->version = v; ctx->id = ++g_ssl_context_id;
    ctx->next = g_ssl_contexts; g_ssl_contexts = ctx; *(uint64_t *)self = ctx->id;
    pthread_mutex_unlock(&g_ssl_context_mu);
    return RES_OK;
}
HLE_FORCE("_ZN2nn3ssl7Context7DestroyEv", "destroy a local SSL context", Result, h_ssl_CtxDestroy, void *self) {
    if (!self) return RES_FAIL;
    pthread_mutex_lock(&g_ssl_context_mu);
    ssl_local_context **p = &g_ssl_contexts;
    while (*p && (*p)->guest != self) p = &(*p)->next;
    ssl_local_context *ctx = *p;
    if (ctx) { *p = ctx->next; *(uint64_t *)self = 0; }
    pthread_mutex_unlock(&g_ssl_context_mu);
    int found = ctx != NULL;
    if (found) dm_tls_context_destroy(self);
    free(ctx);
    return found ? RES_OK : RES_FAIL;
}
HLE_FORCE("nnsslInitialize", "native transport when DM_NETWORK=1; offline otherwise", Result, h_ssl_Initialize, void) { return RES_OK; }
HLE_FORCE("nnsslFinalize", "native transport when DM_NETWORK=1; offline otherwise", Result, h_ssl_Finalize, void) { return RES_OK; }
HLE_FORCE("_ZN2nn3ssl10InitializeEv", "native transport when DM_NETWORK=1; offline otherwise", Result, h_ssl_InitCpp, void) { return RES_OK; }
HLE_FORCE("_ZN2nn3ssl8FinalizeEv", "native transport when DM_NETWORK=1; offline otherwise", Result, h_ssl_FinCpp, void) { return RES_OK; }
HLE_FORCE_BIG("_ZN2nn3ssl7Context15ImportServerPkiEPmPKcjNS0_17CertificateFormatE", "native transport when DM_NETWORK=1; offline otherwise", Result, h_ssl_CtxImportServer, void *self, uint64_t *id, const char *b, unsigned n, int f) { return tls_config_result("ImportServerCa",dm_tls_import_ca(self,id,b,n,f),n,f); }
HLE_FORCE_BIG("_ZN2nn3ssl7Context9RemovePkiEm", "native transport when DM_NETWORK=1; offline otherwise", Result, h_ssl_CtxRemovePki, void *self, uint64_t id) { return dm_tls_remove_ca(self,id); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection6CreateEPNS0_7ContextE", "native transport when DM_NETWORK=1; offline otherwise", Result, h_ssl_CnCreate, void *self, void *ctx) { return tls_config_result("ConnectionCreate",dm_tls_create(self,ctx),0,0); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection7DestroyEv", "native transport when DM_NETWORK=1; offline otherwise", Result, h_ssl_CnDestroy, void *self) { return dm_tls_destroy(self); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection11DoHandshakeEv", "native transport when DM_NETWORK=1; offline otherwise", Result, h_ssl_CnHandshake, void *self) { Result r=dm_tls_handshake(self); if (r && r!=DM_TLS_WOULD_BLOCK && dm_net_enabled()) LOG("ssl: handshake failed (%#x; endpoint redacted)", r); return r; }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection11SetHostNameEPKcj", "native transport when DM_NETWORK=1; offline otherwise", Result, h_ssl_CnHost, void *self, const char *h, unsigned n) { return tls_config_result("HostName",dm_tls_hostname(self,h,n),n,0); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection9SetIoModeENS1_6IoModeE", "native transport when DM_NETWORK=1; offline otherwise", Result, h_ssl_CnIoMode, void *self, int m) { return tls_config_result("IoMode",dm_tls_mode(self,m),m,0); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection19SetSessionCacheModeENS1_16SessionCacheModeE", "native transport when DM_NETWORK=1; offline otherwise", Result, h_ssl_CnCache, void *self, int m) { return tls_config_result("SessionCache",dm_tls_session_cache(self,m),m,0); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection19SetSocketDescriptorEi", "native transport when DM_NETWORK=1; offline otherwise", Result, h_ssl_CnSock, void *self, int s) { return tls_config_result("Socket",dm_tls_socket(self,s),s,0); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection15SetVerifyOptionENS1_12VerifyOptionE", "native transport when DM_NETWORK=1; offline otherwise", Result, h_ssl_CnVerifyOption, void *self, unsigned v) { return tls_config_result("VerifyOption",dm_tls_verify_option(self,v),v,0); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection9SetOptionENS1_10OptionTypeEb", "native transport when DM_NETWORK=1; offline otherwise", Result, h_ssl_CnOption, void *self, int o, int v) { return tls_config_result("Option",dm_tls_option(self,o,v),o,v); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection12GetLastErrorEPNS_6ResultE", "native transport when DM_NETWORK=1; offline otherwise", Result, h_ssl_CnLastErr, void *self, uint32_t *r) { return dm_tls_error(self,r,0); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection18GetVerifyCertErrorEPNS_6ResultE", "native transport when DM_NETWORK=1; offline otherwise", Result, h_ssl_CnVerifyErr, void *self, uint32_t *r) { return dm_tls_error(self,r,1); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection4PeekEPcPij", "native transport when DM_NETWORK=1; offline otherwise", Result, h_ssl_CnPeek, void *self, char *b, int *o, unsigned n) { return dm_tls_peek(self,b,o,n); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection4PollEPNS1_9PollEventES3_j", "native transport when DM_NETWORK=1; offline otherwise", Result, h_ssl_CnPoll, void *self, unsigned *o, const unsigned *i, unsigned t) { return dm_tls_poll(self,o,i,t); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection4ReadEPcj", "native transport when DM_NETWORK=1; offline otherwise", int, h_ssl_CnRead, void *self, char *b, unsigned n) { return dm_tls_read(self,b,n); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection5WriteEPKcj", "native transport when DM_NETWORK=1; offline otherwise", int, h_ssl_CnWrite, void *self, const char *b, unsigned n) { return dm_tls_write(self,b,n); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection7PendingEv", "native transport when DM_NETWORK=1; offline otherwise", int, h_ssl_CnPending, void *self) { return dm_tls_pending(self); }
#define SSF(m, cname, ...) HLE_FORCE(m, "unsupported certificate/device-key operation", Result, cname, __VA_ARGS__)
SSF("_ZN2nn3ssl7Context19RegisterInternalPkiEPmNS1_11InternalPkiE", h_ssl_CtxRegPki, void *self, uint64_t *id, int p) { if (dm_net_enabled()) LOG("[RED] ssl: unsupported RegisterInternalPki"); (void)self; (void)id; (void)p; return RES_FAIL; }
SSF("_ZN2nn3ssl14BuiltInManager28GetBuiltInCertificateBufSizeEPjPNS0_15CaCertificateIdEj", h_ssl_BiBufSize, uint32_t *o, void *ids, unsigned n) { if (dm_net_enabled()) LOG("[RED] ssl: unsupported BuiltInCaBufferSize"); (void)ids; (void)n; if (o) *o = 0; return RES_FAIL; }
SSF("_ZN2nn3ssl14BuiltInManager22GetBuiltInCertificatesEPPNS1_22BuiltInCertificateInfoEPhjPNS0_15CaCertificateIdEj", h_ssl_BiCerts, void **o, uint8_t *b, unsigned bs, void *ids, unsigned n) { if (dm_net_enabled()) LOG("[RED] ssl: unsupported BuiltInCaCertificates"); (void)o; (void)b; (void)bs; (void)ids; (void)n; return RES_FAIL; }
HLE_FORCE("_ZN2nn3ssl7Context15ImportClientPkiEPmPKcS4_jj", "native transport when DM_NETWORK=1; offline otherwise", Result, h_ssl_ClientPki, void *self, uint64_t *id, const char *a, const char *b, unsigned an, unsigned bn) { if (dm_net_enabled()) LOG("[RED] ssl: unsupported ImportClientPki"); (void)self; (void)a; (void)b; (void)an; (void)bn; if (id) *id=0; return RES_FAIL; }
HLE_FORCE("_ZN2nn3ssl7Context9ImportCrlEPmPKcj", "native transport when DM_NETWORK=1; offline otherwise", Result, h_ssl_ImportCrl, void *self, uint64_t *id, const char *b, unsigned n) { if (dm_net_enabled()) LOG("[RED] ssl: unsupported ImportCrl"); (void)self; (void)b; (void)n; if (id) *id=0; return RES_FAIL; }
// The C interface returns Result and writes byte counts through an output
// parameter. It is used by libcurl alongside Xbox's C++ overloads.
static Result ssl_context_id(void *self,uint64_t *out) {
    if (!self || !out) return 0xe27b;
    // SDK 20.5.15 GetContextId is a local field getter. Zero is a
    // successful query on a constructed, not-yet-created context; libcurl
    // uses that result to decide whether to call Create.
    memcpy(out,self,sizeof *out); return RES_OK;
}
static Result ssl_bytes_result(void *self,int *out,int n) {
    if (!out) return 0xe27b;
    *out=n>=0 ? n : 0;
    if (n>=0) return RES_OK;
    uint32_t r=RES_FAIL; if (dm_tls_error(self,&r,0)) return RES_FAIL;
    return r ? r : RES_FAIL;
}
static Result ssl_read_result(void *self,char *buf,int *out,unsigned n) {
    if (!out) return 0xe27b;
    return ssl_bytes_result(self,out,dm_tls_read(self,buf,n));
}
static Result ssl_write_result(void *self,const char *buf,int *out,unsigned n) {
    if (!out) return 0xe27b;
    return ssl_bytes_result(self,out,dm_tls_write(self,buf,n));
}
static Result ssl_pending_result(void *self,int *out) {
    if (!out) return 0xe27b;
    return ssl_bytes_result(self,out,dm_tls_pending(self));
}
HLE_FORCE_BIG("nnsslContextGetContextId","native TLS C interface, Result with output arguments",Result,h_cssl_CtxId,void *self,uint64_t *out) { return ssl_context_id(self,out); }
HLE_FORCE_BIG("nnsslContextDestroy","native TLS C interface, Result with output arguments",Result,h_cssl_CtxDestroy,void *self) { return h_ssl_CtxDestroy(self); }
HLE_FORCE_BIG("nnsslConnectionCreate","native TLS C interface, Result with output arguments",Result,h_cssl_Create,void *self,void *ctx) { return tls_config_result("C ConnectionCreate",dm_tls_create(self,ctx),0,0); }
HLE_FORCE_BIG("nnsslConnectionDestroy","native TLS C interface, Result with output arguments",Result,h_cssl_Destroy,void *self) { return dm_tls_destroy(self); }
HLE_FORCE_BIG("nnsslConnectionGetConnectionId","native TLS C interface, Result with output arguments",Result,h_cssl_Id,void *self,uint64_t *out) { return dm_tls_connection_id(self,out); }
HLE_FORCE_BIG("nnsslConnectionSetSocketDescriptor","native TLS C interface, Result with output arguments",Result,h_cssl_Socket,void *self,int fd) { return tls_config_result("C Socket",dm_tls_socket(self,fd),fd,0); }
HLE_FORCE_BIG("nnsslConnectionSetHostName","native TLS C interface, Result with output arguments",Result,h_cssl_Host,void *self,const char *h,unsigned n) { return tls_config_result("C HostName",dm_tls_hostname(self,h,n),n,0); }
HLE_FORCE_BIG("nnsslConnectionSetIoMode","native TLS C interface, Result with output arguments",Result,h_cssl_Mode,void *self,int m) { return tls_config_result("C IoMode",dm_tls_mode(self,m),m,0); }
HLE_FORCE_BIG("nnsslConnectionSetVerifyOption","native TLS C interface, Result with output arguments",Result,h_cssl_Verify,void *self,unsigned v) { return tls_config_result("C VerifyOption",dm_tls_verify_option(self,v),v,0); }
HLE_FORCE_BIG("nnsslConnectionSetSessionCacheMode","native TLS C interface, Result with output arguments",Result,h_cssl_Cache,void *self,int m) { return tls_config_result("C SessionCache",dm_tls_session_cache(self,m),m,0); }
HLE_FORCE_BIG("nnsslConnectionSetOption","native TLS C interface, Result with output arguments",Result,h_cssl_Option,void *self,int o,int v) { return tls_config_result("C Option",dm_tls_option(self,o,v),o,v); }
HLE_FORCE_BIG("nnsslConnectionSetServerCertBuffer","native TLS C interface, Result with output arguments",Result,h_cssl_CertBuffer,void *self,char *b,unsigned n) { return tls_config_result("C CertBuffer",dm_tls_cert_buffer(self,b,n),n,0); }
HLE_FORCE_BIG("nnsslConnectionDoHandshakeWithCertBuffer","native TLS C interface, Result with output arguments",Result,h_cssl_Handshake,void *self,unsigned *size,unsigned *count) { Result r=dm_tls_handshake_certs(self,size,count); if (r && r!=DM_TLS_WOULD_BLOCK) LOG("ssl: C handshake result=%#x",r); return r; }
HLE_FORCE_BIG("nnsslConnectionGetServerCertDetail","native TLS C interface, Result with output arguments",Result,h_cssl_CertDetail,void *self,void *out,const char *b,unsigned i) { return dm_tls_cert_detail(self,out,b,i); }
HLE_FORCE_BIG("nnsslConnectionGetVerifyCertError","native TLS C interface, Result with output arguments",Result,h_cssl_VerifyError,void *self,uint32_t *out) { return dm_tls_error(self,out,1); }
HLE_FORCE_BIG("nnsslConnectionSetNextAlpnProto","native TLS C interface, Result with output arguments",Result,h_cssl_SetAlpn,void *self,const unsigned char *b,unsigned n) { return tls_config_result("C AlpnSet",dm_tls_alpn_set(self,b,n),n,0); }
HLE_FORCE_BIG("nnsslConnectionGetNextAlpnProto","native TLS C interface, Result with output arguments",Result,h_cssl_GetAlpn,void *self,int *state,unsigned char *b,unsigned *out,unsigned n) { return dm_tls_alpn_get(self,state,b,out,n); }
HLE_FORCE_BIG("nnsslConnectionPeek","native TLS C interface, Result with output arguments",Result,h_cssl_Peek,void *self,char *b,int *out,unsigned n) { return dm_tls_peek(self,b,out,n); }
HLE_FORCE_BIG("nnsslConnectionPending","native TLS C interface, Result with output arguments",Result,h_cssl_Pending,void *self,int *out) { return ssl_pending_result(self,out); }
HLE_FORCE_BIG("nnsslConnectionRead","native TLS C interface, Result with output arguments",Result,h_cssl_Read,void *self,char *b,int *out,unsigned n) { return ssl_read_result(self,b,out,n); }
HLE_FORCE_BIG("nnsslConnectionWrite","native TLS C interface, Result with output arguments",Result,h_cssl_Write,void *self,const char *b,int *out,unsigned n) { return ssl_write_result(self,b,out,n); }
// The equivalent C++ overloads have the same register ABI.
HLE_FORCE_BIG("_ZN2nn3ssl7Context12GetContextIdEPm","native TLS C interface, Result with output arguments",Result,h_cppssl_CtxId,void *self,uint64_t *out) { return ssl_context_id(self,out); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection15GetConnectionIdEPm","native TLS C interface, Result with output arguments",Result,h_cppssl_Id,void *self,uint64_t *out) { return dm_tls_connection_id(self,out); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection19SetServerCertBufferEPcj","native TLS C interface, Result with output arguments",Result,h_cppssl_CertBuffer,void *self,char *b,unsigned n) { return dm_tls_cert_buffer(self,b,n); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection11DoHandshakeEPjS2_","native TLS C interface, Result with output arguments",Result,h_cppssl_HandshakeCerts,void *self,unsigned *size,unsigned *count) { return dm_tls_handshake_certs(self,size,count); }
HLE_FORCE_BIG("_ZNK2nn3ssl10Connection19GetServerCertDetailEPNS1_16ServerCertDetailEPKcj","native TLS C interface, Result with output arguments",Result,h_cppssl_CertDetail,void *self,void *out,const char *b,unsigned i) { return dm_tls_cert_detail(self,out,b,i); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection16SetNextAlpnProtoEPKhj","native TLS C interface, Result with output arguments",Result,h_cppssl_SetAlpn,void *self,const unsigned char *b,unsigned n) { return dm_tls_alpn_set(self,b,n); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection16GetNextAlpnProtoEPNS1_14AlpnProtoStateEPhPjj","native TLS C interface, Result with output arguments",Result,h_cppssl_GetAlpn,void *self,int *state,unsigned char *b,unsigned *out,unsigned n) { return dm_tls_alpn_get(self,state,b,out,n); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection4ReadEPcPij","native TLS C interface, Result with output arguments",Result,h_cppssl_ReadResult,void *self,char *b,int *out,unsigned n) { return ssl_read_result(self,b,out,n); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection5WriteEPKcPij","native TLS C interface, Result with output arguments",Result,h_cppssl_WriteResult,void *self,const char *b,int *out,unsigned n) { return ssl_write_result(self,b,out,n); }
HLE_FORCE_BIG("_ZN2nn3ssl10Connection7PendingEPi","native TLS C interface, Result with output arguments",Result,h_cppssl_PendingResult,void *self,int *out) { return ssl_pending_result(self,out); }

HLE_FORCE_BIG("nnsocketAccept", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_AcceptC, int s, void *a, unsigned *n) { return dm_net_accept(s,a,n); }

HLE_FORCE_BIG("nnsocketListen", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_ListenC, int s, int n) { return dm_net_listen(s,n); }

HLE_FORCE_BIG("nnsocketShutdown", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_ShutdownC, int s, int h) { return dm_net_shutdown(s,h); }

HLE_FORCE_BIG("nnsocketGetAddrInfo", "native transport when DM_NETWORK=1; offline otherwise", int, h_sk_GaiC, const char *h, const char *s, const void *hint, void **r) { return dm_net_getaddrinfo(h,s,hint,r); }

HLE_FORCE_BIG("nnsocketFreeAddrInfo", "native transport when DM_NETWORK=1; offline otherwise", void, h_sk_FreeAiC, void *a) { dm_net_freeaddrinfo(a); }

HLE_FORCE_BIG("_ZN2nn6socket11GAIStrErrorENS0_7AiErrnoE", "native transport when DM_NETWORK=1; offline otherwise", const char *, h_sk_GaiStr, int e) { (void)e; return "resolver failure"; }
