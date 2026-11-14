// Offline local event descriptors and readiness, retained from upstream.
#define SK_ENETDOWN 50                  // Horizon sockets use FreeBSD error codes
static __thread int t_skerr = SK_ENETDOWN;
static int sk_fail(void) { t_skerr = SK_ENETDOWN; return -1; }
#define SKF(m, cname, ...) HLE_FORCE(m, "bsd socket service (cut): returns -1/ENETDOWN", int, cname, __VA_ARGS__)
// Poll / Select with nothing to wait on: the host creates no socket, so a game's descriptors are all -1, or it passes none. POSIX and Horizon's bsd service ignore negative
// descriptors and wait out the timeout, returning 0. Failing at once made Dark Souls Remastered's network thread ("NexusRevolution Socket", main+0x92c7c4) spin a core at 100%
// on Poll(fds, 0, 33 ms), 11,000 calls a second. An endless wait on nothing never returns on hardware; here it returns 0 after 100 ms: a thread in a host hook pauses for a garbage
// collector (IL2CPP's, hle_thread.c) only once the hook returns, and the caller can still see a shutdown
void dm_guest_sleep_ns(int64_t ns);
void dm_guest_cond_wait_ns(pthread_cond_t *c, pthread_mutex_t *m, int64_t ns);   // hle_thread.c
static int sk_idle(int64_t ms) { dm_guest_sleep_ns((ms < 0 ? 100 : ms) * 1000000); return 0; }
typedef struct { int32_t fd; int16_t events, revents; } sk_pollfd;
// EventFd is a local descriptor of the bsd service and works with no network, so it is real here: gRPC (nn::npln, Ocarina of Time at startup) wakes its pollers with one, and with no
// wakeup descriptor its poll engine's factory (main+0x21f1280) returns null and grpc_init calls abort (main+0x21f0c7c). The descriptors are the host's only ones: Socket still fails.
// The flags are Horizon's: gRPC creates its descriptor with 6 and consumes after a poll says it is ready. A descriptor with bit 0 reads one at a time; bits 1 and 2 never block.
// The numbers are the bsd service's: the lowest free one, from 0, as for its sockets (the host makes no other descriptor). Games size their tables for those small numbers: Minecraft's
// NetherNet threads put theirs in a 1024-bit fd_set on the stack for Select (FD_SET at main+0x18b2ae8 in 1.21.120), and when the host numbered them from 0x4000 the bit landed 2 KB
// past the set and the thread crashed (SIGBUS)
enum { EFD_MAX = 64, SK_POLLIN = 1, SK_POLLOUT = 4, SK_POLLRDNORM = 0x40, SK_POLLWRNORM = 4, SK_POLLNVAL = 0x20, SK_EBADF = 9, SK_EAGAIN = 35 };
static struct { uint64_t count; int used, flags; } g_efd[EFD_MAX];
static pthread_mutex_t g_efd_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_efd_c = PTHREAD_COND_INITIALIZER;
static int efd_used(int fd) { return fd >= 0 && fd < EFD_MAX && g_efd[fd].used; }   // with g_efd_m held
static int efd_create(uint64_t v, int flags) {
    pthread_mutex_lock(&g_efd_m);
    for (int i = 0; i < EFD_MAX; i++) if (!g_efd[i].used) { g_efd[i].used = 1; g_efd[i].count = v; g_efd[i].flags = flags; pthread_mutex_unlock(&g_efd_m); return i; }
    pthread_mutex_unlock(&g_efd_m);
    LOG("[RED] net: more than %d event descriptors", EFD_MAX);
    t_skerr = 24; return -1;   // EMFILE
}
static int efd_close(int fd) {
    pthread_mutex_lock(&g_efd_m);
    int ok = efd_used(fd);
    if (ok) { g_efd[fd].used = 0; pthread_cond_broadcast(&g_efd_c); }
    pthread_mutex_unlock(&g_efd_m);
    if (!ok) return sk_fail();
    return 0;
}
static int efd_read(int fd, uint64_t *out) {
    pthread_mutex_lock(&g_efd_m);
    int r = -1;
    for (;;) {
        if (!efd_used(fd)) { t_skerr = SK_EBADF; break; }
        uint64_t *c = &g_efd[fd].count; int f = g_efd[fd].flags;
        if (*c) { if (f & 1) { *out = 1; *c -= 1; } else { *out = *c; *c = 0; } r = 0; break; }
        if (f & 6) { t_skerr = SK_EAGAIN; break; }
        dm_guest_cond_wait_ns(&g_efd_c, &g_efd_m, dm_detclock_enabled() ? 1000000 : 100000000);
    }
    pthread_mutex_unlock(&g_efd_m);
    return r;
}
static int efd_write(int fd, uint64_t v) {
    pthread_mutex_lock(&g_efd_m);
    int ok = efd_used(fd);
    if (ok) { g_efd[fd].count += v; pthread_cond_broadcast(&g_efd_c); }
    pthread_mutex_unlock(&g_efd_m);
    if (!ok) { t_skerr = SK_EBADF; return -1; }
    return 0;
}
// With g_efd_m held: fills revents, returns how many descriptors have any. open holds the descriptors that were open when the wait began: one of those closed since is POLLNVAL, any other
// is one the host never made, with nothing to say (see dm_sk_poll). The numbers start at 0, where a game's stale or never-valid descriptors fall too, so the range alone cannot tell them apart
static int efd_ready(sk_pollfd *p, uint64_t n, uint64_t open) {
    int ready = 0;
    for (uint64_t i = 0; i < n; i++) {
        p[i].revents = 0;
        if (p[i].fd < 0 || p[i].fd >= EFD_MAX || !(open >> p[i].fd & 1)) continue;
        if (!efd_used(p[i].fd)) { p[i].revents = SK_POLLNVAL; ready++; continue; }   // closed while waiting
        if (g_efd[p[i].fd].count) p[i].revents |= p[i].events & (SK_POLLIN | SK_POLLRDNORM);
        p[i].revents |= p[i].events & (SK_POLLOUT | SK_POLLWRNORM);
        if (p[i].revents) ready++;
    }
    return ready;
}
// A descriptor the host never made (it creates no socket): Star Fox's HTTP thread polls two with a zero timeout, its wake-up eventfd that failed (-1) and a garbage one, and takes any failure
// but EINTR as fatal (main+0xbe2efc4). On a console a socket with nothing to say reports no event at the timeout, so that is the answer for every descriptor: nothing ready, the timeout
// waited out, 0. Failing with ENETDOWN here made the loop abort
static int offline_poll(void *fds, uint64_t n, int ms) {   // also hle_offline.c's plain pollfd form
    sk_pollfd *p = fds;
    uint64_t open = 0;   // EFD_MAX is 64: one bit per descriptor
    pthread_mutex_lock(&g_efd_m);
    for (uint64_t i = 0; i < n; i++) if (p[i].fd >= 0) {
        if (efd_used(p[i].fd)) { open |= 1ull << p[i].fd; continue; }
        static int once; if (!once++) LOG("socket: Poll of %llu descriptors with one the host never made (fd %d, events %#x; %d ms): nothing ready at the timeout", (unsigned long long)n, p[i].fd, p[i].events, ms);
    }
    pthread_mutex_unlock(&g_efd_m);
    for (uint64_t i = 0; i < n; i++) p[i].revents = 0;
    if (!open) return sk_idle(ms);
    // As sk_idle: an endless wait comes back with 0 after 100 ms, so the hook returns to a garbage collector's pause and the caller can see a shutdown
    int64_t limit = (ms < 0 ? 100 : (int64_t)ms) * 1000000;
    uint64_t start = guest_ns();
    pthread_mutex_lock(&g_efd_m);
    int ready;
    while (!(ready = efd_ready(p, n, open))) {
        int64_t left = limit - (int64_t)(guest_ns() - start);
        if (left <= 0) break;
        dm_guest_cond_wait_ns(&g_efd_c, &g_efd_m, dm_detclock_enabled() && left > 1000000 ? 1000000 : left);
    }
    pthread_mutex_unlock(&g_efd_m);
    return ready;
}
// Select is Poll over the descriptors in its sets, so the same rules hold (Minecraft's NetherNet threads wait on their event descriptors this way): the read set asks POLLIN, the write
// set POLLOUT, and the sets come back with the ready ones. A descriptor closed during the wait fails with EBADF, as select does. Only the host's numbers (below EFD_MAX) are passed on:
// Poll has nothing to say about any other, and the array stays small on the guest thread's stack
static int offline_select(int nfds, uint64_t *r, uint64_t *w, uint64_t *e, const int64_t *tv) {   // fd_set: 1024 bits; timeval: {i64 seconds, i64 microseconds}
    uint64_t *sets[3] = {r, w, e};
    static const int16_t ask[3] = {SK_POLLIN, SK_POLLOUT, 0};
    sk_pollfd p[EFD_MAX]; int np = 0;
    if (nfds > 1024) nfds = 1024;
    for (int fd = 0; fd < nfds && fd < EFD_MAX; fd++) {
        int in = 0; int16_t ev = 0;
        for (int k = 0; k < 3; k++) if (sets[k] && sets[k][fd >> 6] >> (fd & 63) & 1) { in = 1; ev |= ask[k]; }
        if (in) p[np++] = (sk_pollfd){fd, ev, 0};
    }
    int64_t ms = tv ? tv[0] * 1000 + tv[1] / 1000 : -1;
    if (ms > 0x7fffffff) ms = 0x7fffffff;
    offline_poll(p, (uint64_t)np, (int)ms);
    for (int k = 0; k < 3; k++) if (sets[k]) for (int fd = 0; fd < nfds; fd++) sets[k][fd >> 6] &= ~(1ull << (fd & 63));
    int n = 0;
    for (int i = 0; i < np; i++) {
        if (p[i].revents & SK_POLLNVAL) { t_skerr = SK_EBADF; return -1; }
        if (p[i].revents & (SK_POLLIN | SK_POLLRDNORM) && r) { r[p[i].fd >> 6] |= 1ull << (p[i].fd & 63); n++; }
        if (p[i].revents & SK_POLLOUT && w) { w[p[i].fd >> 6] |= 1ull << (p[i].fd & 63); n++; }
    }
    return n;
}
