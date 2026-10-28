// Native BSD/DNS transport, explicitly enabled with DM_NETWORK=1. Authentication
// and TLS policy are handled above this layer; a successful socket is not sign-in.
// Guest constants/layouts checked against switchbrew/libnx's primary BSD headers:
// nx/external/bsd/include/{sys/socket.h,netdb.h,netinet/in.h,sys/filio.h}.
#include "net_transport.h"
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

void dm_thread_native_wait(int entering);
enum { NX_AF_INET = 2, NX_AF_INET6 = 28, NX_FD_FIRST = 32, NX_FD_MAX = 1024 };
typedef struct net_event {
    uint64_t value;
    unsigned refs;
    int flags, fdflags, closed, read_ready, write_ready;
    int readable[2], writable[2];
    pthread_cond_t changed;
} net_event;
typedef struct { int native, family, type, protocol; net_event *event; } net_fd;
static net_fd g_net_fds[NX_FD_MAX];
static pthread_mutex_t g_net_mu = PTHREAD_MUTEX_INITIALIZER;
static __thread int g_net_error = 50;
_Static_assert(sizeof(dm_net_addrinfo) == 48, "Horizon AddrInfo ABI");

int dm_net_enabled(void) { const char *s = getenv("DM_NETWORK"); return s && !strcmp(s, "1"); }
int dm_net_errno(void) { return g_net_error; }
void dm_net_set_errno(int e) { g_net_error = e; }
static int nx_errno(int e) {
    // The ordinary BSD socket errors (9, 35-66, etc.) agree. Later Darwin
    // additions do not: do not leak Apple's values into a guest comparison.
    if (e == ECANCELED) return 85;
    if (e == EILSEQ) return 86;
    if (e == EPROTO) return 92;
    if (e == EOPNOTSUPP) return 45;
    return e;
}
static int fail(int e) { g_net_error = e; return -1; }
static int host_result(int r, int e) { if (r < 0) g_net_error = nx_errno(e); return r; }
#define NATIVE_RESULT(call) ({ int net_r = (call); int net_e = errno; host_result(net_r, net_e); })
static int online(void) { return dm_net_enabled() ? 1 : (fail(50), 0); }
static int native_family(int f) { return f == NX_AF_INET ? AF_INET : f == NX_AF_INET6 ? AF_INET6 : f == 0 ? AF_UNSPEC : -1; }
static int guest_family(int f) { return f == AF_INET ? NX_AF_INET : f == AF_INET6 ? NX_AF_INET6 : 0; }
static int lookup(int guest, net_fd *out) {
    if (!online()) return -1;
    pthread_mutex_lock(&g_net_mu);
    int found = guest >= NX_FD_FIRST && guest < NX_FD_MAX && g_net_fds[guest].native > 0;
    if (found) *out = g_net_fds[guest];
    pthread_mutex_unlock(&g_net_mu);
    return !found ? fail(EBADF) : out->event ? fail(ENOTSOCK) : out->native;
}
int dm_net_native_fd(int guest) { net_fd f; return lookup(guest, &f); }
static int publish(int native, int family, int type, int protocol) {
    if (native < 0) return host_result(native, errno);
    if (native < 3) { int higher = fcntl(native, F_DUPFD_CLOEXEC, 3), e = errno; close(native); if (higher < 0) return host_result(-1, e); native = higher; }
    int one = 1; (void)setsockopt(native, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    (void)fcntl(native, F_SETFD, FD_CLOEXEC);
    pthread_mutex_lock(&g_net_mu);
    for (int i = NX_FD_FIRST; i < NX_FD_MAX; i++) if (!g_net_fds[i].native) {
        g_net_fds[i] = (net_fd){native, family, type, protocol, NULL};
        pthread_mutex_unlock(&g_net_mu); return i;
    }
    pthread_mutex_unlock(&g_net_mu); close(native); return fail(EMFILE);
}
// FreeBSD primary sys/sys/eventfd.h: semaphore=1, nonblock=4,
// cloexec=0x100000. Pipe bytes represent conditions, never the event count.
static void event_marker(int pipefd[2], int *old, int ready) {
    if (*old == ready) return;
    unsigned char byte = 1;
    ssize_t result;
    do { result = ready ? write(pipefd[1], &byte, 1) : read(pipefd[0], &byte, 1); } while (result < 0 && errno == EINTR);
    // The pipes have one private marker, and are only touched under g_net_mu.
    if (result == 1) *old = ready;
}
static void event_update(net_event *e) {
    event_marker(e->readable, &e->read_ready, e->closed || e->value != 0);
    event_marker(e->writable, &e->write_ready, e->closed || e->value < UINT64_MAX - 1);
    pthread_cond_broadcast(&e->changed);
}
// Caller owns g_net_mu. Registry, I/O and poll each hold a reference.
static void event_release(net_event *e) {
    if (--e->refs) return;
    close(e->readable[0]); close(e->readable[1]);
    close(e->writable[0]); close(e->writable[1]);
    pthread_cond_destroy(&e->changed); free(e);
}
static net_event *event_acquire(int fd) {
    pthread_mutex_lock(&g_net_mu);
    net_event *e = fd >= NX_FD_FIRST && fd < NX_FD_MAX ? g_net_fds[fd].event : NULL;
    if (e) e->refs++;
    pthread_mutex_unlock(&g_net_mu); return e;
}
int dm_net_eventfd(uint64_t initial, int flags) {
    if (!online()) return -1;
    if ((flags & ~0x100005) || initial == UINT64_MAX) return fail(EINVAL);
    net_event *e = calloc(1, sizeof *e); if (!e) return fail(ENOMEM);
    e->readable[0] = e->readable[1] = e->writable[0] = e->writable[1] = -1;
    if (pipe(e->readable) || pipe(e->writable)) {
        int error = errno;
        for (int k = 0; k < 2; k++) { if (e->readable[k] >= 0) close(e->readable[k]); if (e->writable[k] >= 0) close(e->writable[k]); }
        free(e); return fail(nx_errno(error));
    }
    int error = 0;
    for (int k = 0; k < 4 && !error; k++) {
        int *at = k < 2 ? &e->readable[k] : &e->writable[k - 2];
        if (*at < 3) {
            int higher = fcntl(*at, F_DUPFD_CLOEXEC, 3);
            if (higher < 0) { error = errno; break; }
            close(*at); *at = higher;
        }
        if (fcntl(*at, F_SETFL, O_NONBLOCK) < 0 || fcntl(*at, F_SETFD, FD_CLOEXEC) < 0) error = errno;
    }
    if (!error) error = pthread_cond_init(&e->changed, NULL);
    if (error) {
        for (int k = 0; k < 2; k++) { close(e->readable[k]); close(e->writable[k]); }
        free(e); return fail(nx_errno(error));
    } e->refs = 1; e->value = initial; e->flags = flags;
    e->fdflags = flags & 0x100000 ? 1 : 0;
    pthread_mutex_lock(&g_net_mu); event_update(e);
    for (int fd = NX_FD_FIRST; fd < NX_FD_MAX; fd++) if (!g_net_fds[fd].native) {
        g_net_fds[fd] = (net_fd){e->readable[0], 0, 0, 0, e};
        pthread_mutex_unlock(&g_net_mu); return fd;
    }
    event_release(e); pthread_mutex_unlock(&g_net_mu); return fail(EMFILE);
}
static long event_io(net_event *e, void *buf, uint64_t len, int writing) {
    int error = 0; uint64_t value = 0;
    if (!buf || len < 8) error = EINVAL;
    else if (writing) { memcpy(&value, buf, 8); if (value == UINT64_MAX) error = EINVAL; }
    dm_thread_native_wait(1); pthread_mutex_lock(&g_net_mu);
    while (!error && !e->closed && (writing ? value > UINT64_MAX - 1 - e->value : !e->value)) {
        if (e->flags & 4) { error = EAGAIN; break; }
        pthread_cond_wait(&e->changed, &g_net_mu);
    }
    if (!error && e->closed) error = EBADF;
    if (!error) {
        if (writing) e->value += value;
        else { value = e->flags & 1 ? 1 : e->value; e->value -= value; memcpy(buf, &value, 8); }
        event_update(e);
    }
    event_release(e); pthread_mutex_unlock(&g_net_mu); dm_thread_native_wait(0);
    return error ? fail(error) : 8;
}
long dm_net_read(int fd, void *buf, uint64_t len) {
    if (!online()) return -1;
    net_event *e = event_acquire(fd); return e ? event_io(e, buf, len, 0) : dm_net_recv(fd, buf, len, 0, NULL, NULL);
}
long dm_net_write(int fd, const void *buf, uint64_t len) {
    if (!online()) return -1;
    net_event *e = event_acquire(fd); return e ? event_io(e, (void *)buf, len, 1) : dm_net_send(fd, buf, len, 0, NULL, 0);
}
static int addr_in(const void *input, unsigned n, struct sockaddr_storage *out, socklen_t *len) {
    if (!input || n < 2) return fail(EINVAL);
    const unsigned char *p = input;
    int fam = native_family(p[1]);
    unsigned need = fam == AF_INET ? sizeof(struct sockaddr_in) : fam == AF_INET6 ? sizeof(struct sockaddr_in6) : 0;
    if (!need) return fail(EAFNOSUPPORT);
    if (n < need || (p[0] && p[0] != need)) return fail(EINVAL);
    memset(out, 0, sizeof *out); memcpy(out, p, need);
    ((struct sockaddr *)out)->sa_family = (sa_family_t)fam;
    ((struct sockaddr *)out)->sa_len = (uint8_t)need;
    *len = (socklen_t)need; return 0;
}
static int addr_out(const struct sockaddr *input, socklen_t n, void *output, unsigned *len) {
    if (!output) return 0;
    if (!len || n > sizeof(struct sockaddr_storage)) return fail(EINVAL);
    struct sockaddr_storage a; memset(&a, 0, sizeof a); memcpy(&a, input, n);
    ((struct sockaddr *)&a)->sa_family = (sa_family_t)guest_family(input->sa_family);
    ((struct sockaddr *)&a)->sa_len = (uint8_t)n;
    unsigned copy = *len < n ? *len : n; memcpy(output, &a, copy); *len = n; return 0;
}
int dm_net_socket(int family, int type, int protocol) {
    if (!online()) return -1;
    int f = native_family(family), base_type = type & 0xf;
    if (f != AF_INET && f != AF_INET6) return fail(EAFNOSUPPORT);
    if ((type & ~0x3000000f) || (base_type != SOCK_STREAM && base_type != SOCK_DGRAM)) return fail(EPROTONOSUPPORT);
    int fd = socket(f, base_type, protocol);
    if (fd < 0) return host_result(fd, errno);
    if (type & 0x20000000) (void)fcntl(fd, F_SETFL, O_NONBLOCK);
    return publish(fd, family, base_type, protocol);
}
static int address_call(int fd, const void *address, unsigned n, int binding) {
    net_fd f; int native = lookup(fd, &f); if (native < 0) return -1;
    struct sockaddr_storage a; socklen_t al;
    if (addr_in(address, n, &a, &al)) return -1;
    dm_thread_native_wait(1);
    int r = binding ? bind(native, (struct sockaddr *)&a, al) : connect(native, (struct sockaddr *)&a, al), e = errno;
    dm_thread_native_wait(0); return host_result(r, e);
}
int dm_net_connect(int fd, const void *a, unsigned n) { return address_call(fd, a, n, 0); }
int dm_net_bind(int fd, const void *a, unsigned n) { return address_call(fd, a, n, 1); }
int dm_net_listen(int fd, int n) { int native = dm_net_native_fd(fd); return native < 0 ? -1 : NATIVE_RESULT(listen(native, n)); }
int dm_net_accept(int fd, void *address, unsigned *n) {
    net_fd f; int native = lookup(fd, &f); if (native < 0) return -1;
    if (address && !n) return fail(EINVAL);
    struct sockaddr_storage a; socklen_t al = sizeof a;
    dm_thread_native_wait(1); int r = accept(native, (struct sockaddr *)&a, &al), e = errno; dm_thread_native_wait(0);
    if (r < 0) return host_result(r, e);
    if (addr_out((struct sockaddr *)&a, al, address, n)) { close(r); return -1; }
    return publish(r, f.family, f.type, f.protocol);
}
int dm_net_close(int fd) {
    pthread_mutex_lock(&g_net_mu);
    int native = fd >= NX_FD_FIRST && fd < NX_FD_MAX ? g_net_fds[fd].native : 0;
    net_event *event = native ? g_net_fds[fd].event : NULL;
    if (native) memset(&g_net_fds[fd], 0, sizeof g_net_fds[fd]);
    if (event) { event->closed = 1; event_update(event); event_release(event); }
    pthread_mutex_unlock(&g_net_mu);
    // Disabling transport must still allow owned descriptors to be cleaned up.
    if (!native) return fail(dm_net_enabled() ? EBADF : 50);
    return event ? 0 : NATIVE_RESULT(close(native));
}
int dm_net_shutdown(int fd, int how) {
    int native = dm_net_native_fd(fd); if (native < 0) return -1;
    if (how < 0 || how > 2) return fail(EINVAL);
    return NATIVE_RESULT(shutdown(native, how));
}
int dm_net_name(int fd, void *address, unsigned *len, int peer) {
    int native = dm_net_native_fd(fd); if (native < 0) return -1;
    if (!address || !len) return fail(EINVAL);
    struct sockaddr_storage a; socklen_t n = sizeof a;
    int r = peer ? getpeername(native, (struct sockaddr *)&a, &n) : getsockname(native, (struct sockaddr *)&a, &n);
    if (r < 0) return host_result(r, errno);
    return addr_out((struct sockaddr *)&a, n, address, len);
}
static int option_host(int level, int option) {
    if (level == 0xffff) {
        if (option == 0x800) return SO_NOSIGPIPE;
        // Only the shared BSD options: later option numbers have unrelated
        // meanings on Darwin and must not silently configure another feature.
        if (option == 1 || option == 2 || option == 4 || option == 8 || option == 0x10 ||
            option == 0x20 || option == 0x40 || option == 0x80 || option == 0x100 ||
            option == 0x200 || option == 0x400 || (option >= 0x1001 && option <= 0x1008)) return option;
        return -1;
    }
    if (level == IPPROTO_TCP) {
        if (option == 0x100) return TCP_KEEPALIVE;
        if (option == 1 || option == 2 || option == 4 || option == 8 || option == 0x101 || option == 0x102) return option;
        return -1;
    }
    if (level == IPPROTO_IP && option >= 1 && option <= 4) return option;
    if (level == IPPROTO_IPV6 && (option == 4 || (option >= 9 && option <= 13) || option == 27)) return option;
    return -1;
}
int dm_net_opt(int fd, int level, int option, void *value, unsigned *len, int set) {
    net_fd f; int native = lookup(fd, &f); if (native < 0) return -1;
    if (!value || !len) return fail(EINVAL);
    if (!set && level == 0xffff && option == 0x1016) {
        if (*len < 4) return fail(EINVAL);
        int protocol = f.protocol ? f.protocol : f.type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP;
        memcpy(value, &protocol, 4); *len = 4; return 0;
    }
    int opt = option_host(level, option); socklen_t n = *len;
    if (opt < 0) return fail(ENOPROTOOPT);
    if (level == 0xffff && (option == 0x1005 || option == 0x1006)) {
        if (*len < 16) return fail(EINVAL);
        int64_t *guest = value; struct timeval tv;
        if (set) {
            if (guest[0] < 0 || guest[1] < 0 || guest[1] > 999999) return fail(EINVAL);
            tv.tv_sec = (time_t)guest[0]; tv.tv_usec = (suseconds_t)guest[1];
            return NATIVE_RESULT(setsockopt(native, level, opt, &tv, sizeof tv));
        }
        n = sizeof tv; int r = getsockopt(native, level, opt, &tv, &n);
        if (r < 0) return host_result(r, errno);
        guest[0] = tv.tv_sec; guest[1] = tv.tv_usec; *len = 16; return 0;
    }
    int r = set ? setsockopt(native, level, opt, value, n) : getsockopt(native, level, opt, value, &n);
    if (!set && r == 0) {
        *len = n;
        if (level == 0xffff && option == 0x1007 && n >= sizeof(int)) *(int *)value = nx_errno(*(int *)value);
    }
    return host_result(r, errno);
}
int dm_net_fcntl(int fd, int command, uint64_t value) {
    if (!online()) return -1;
    net_event *e = event_acquire(fd);
    if (e) {
        pthread_mutex_lock(&g_net_mu); int r = 0;
        if (e->closed) r = fail(EBADF);
        else if (command == 1) r = e->fdflags;
        else if (command == 2) e->fdflags = (int)value & 1;
        else if (command == 3) r = 2 | (e->flags & 4);
        else if (command == 4) { e->flags = (e->flags & ~4) | ((int)value & 4); pthread_cond_broadcast(&e->changed); }
        else r = fail(EINVAL);
        event_release(e); pthread_mutex_unlock(&g_net_mu); return r;
    }
    int native = dm_net_native_fd(fd); if (native < 0) return -1;
    if (command != 1 && command != 2 && command != 3 && command != 4) return fail(EINVAL);
    return NATIVE_RESULT(fcntl(native, command, (int)value));
}
int dm_net_ioctl(int fd, unsigned request, void *value, uint64_t len) {
    if (!online()) return -1;
    net_event *e = event_acquire(fd);
    if (e) {
        pthread_mutex_lock(&g_net_mu); int r = 0;
        if (e->closed) r = fail(EBADF);
        else if (!value || len < 4) r = fail(EINVAL);
        else if (request == 0x8004667e) { int nonblock; memcpy(&nonblock, value, 4); e->flags = (e->flags & ~4) | (nonblock ? 4 : 0); pthread_cond_broadcast(&e->changed); }
        else r = fail(45);
        event_release(e); pthread_mutex_unlock(&g_net_mu); return r;
    }
    int native = dm_net_native_fd(fd); if (native < 0) return -1;
    if (!value || len < sizeof(int)) return fail(EINVAL);
    if (request != 0x8004667e && request != 0x4004667f) return fail(45);
    return NATIVE_RESULT(ioctl(native, request == 0x8004667e ? FIONBIO : FIONREAD, value));
}
static int msg_flags(int guest) {
    int flags = guest & 0xff;
    if (guest & 0x100) flags |= MSG_EOF;
    if (guest & 0x20000) flags |= MSG_NOSIGNAL;
    if (guest & ~0x201ff) return fail(EINVAL);
    return flags;
}
long dm_net_recv(int fd, void *buf, uint64_t len, int flags, void *address, unsigned *addresslen) {
    int native = dm_net_native_fd(fd); if (native < 0) return -1;
    if (len > SSIZE_MAX || (len && !buf) || (address && !addresslen)) return fail(EINVAL);
    int mf = msg_flags(flags); if (mf < 0) return -1;
    struct sockaddr_storage a; socklen_t al = sizeof a;
    dm_thread_native_wait(1);
    ssize_t r = address ? recvfrom(native, buf, (size_t)len, mf, (struct sockaddr *)&a, &al) : recv(native, buf, (size_t)len, mf);
    int e = errno; dm_thread_native_wait(0);
    if (r < 0) return host_result(-1, e);
    if (address && addr_out((struct sockaddr *)&a, al, address, addresslen)) return -1;
    return r;
}
long dm_net_send(int fd, const void *buf, uint64_t len, int flags, const void *address, unsigned addresslen) {
    int native = dm_net_native_fd(fd); if (native < 0) return -1;
    if (len > SSIZE_MAX || (len && !buf)) return fail(EINVAL);
    int mf = msg_flags(flags); if (mf < 0) return -1;
    struct sockaddr_storage a; socklen_t al;
    if (address && addr_in(address, addresslen, &a, &al)) return -1;
    dm_thread_native_wait(1);
    ssize_t r = address ? sendto(native, buf, (size_t)len, mf, (struct sockaddr *)&a, al) : send(native, buf, (size_t)len, mf);
    int e = errno; dm_thread_native_wait(0); return r < 0 ? host_result(-1, e) : r;
}
typedef struct { int32_t fd; int16_t events, revents; } nx_pollfd;
int dm_net_poll(void *fds, uint64_t n, int timeout) {
    if (!online()) return -1;
    if (n > 4096 || (n && !fds)) return fail(EINVAL);
    nx_pollfd *guest = fds;
    struct pollfd *p = calloc(n ? (size_t)n * 2 : 1, sizeof *p);
    net_event **events = calloc(n ? (size_t)n : 1, sizeof *events);
    if (!p || !events) { free(p); free(events); return fail(ENOMEM); }
    int invalid = 0;
    pthread_mutex_lock(&g_net_mu);
    for (uint64_t i = 0; i < n; i++) {
        int fd = guest[i].fd; guest[i].revents = 0;
        p[i*2].fd = p[i*2+1].fd = -1;
        if (fd < 0) continue;
        if (fd < NX_FD_FIRST || fd >= NX_FD_MAX || !g_net_fds[fd].native) { guest[i].revents = POLLNVAL; invalid++; continue; }
        net_fd *f = &g_net_fds[fd]; net_event *e = f->event;
        if (e) {
            events[i] = e; e->refs++;
            p[i*2] = (struct pollfd){e->readable[0], guest[i].events & (POLLIN | POLLRDNORM) ? POLLIN : 0, 0};
            p[i*2+1] = (struct pollfd){e->writable[0], guest[i].events & (POLLOUT | POLLWRNORM) ? POLLIN : 0, 0};
        } else p[i*2] = (struct pollfd){f->native, guest[i].events, 0};
    }
    pthread_mutex_unlock(&g_net_mu);
    dm_thread_native_wait(1); int r = poll(p, (nfds_t)n * 2, invalid ? 0 : timeout), error = errno; dm_thread_native_wait(0);
    int found = 0; pthread_mutex_lock(&g_net_mu);
    for (uint64_t i = 0; i < n; i++) {
        net_event *e = events[i];
        if (r >= 0 && e) {
            if (e->closed) guest[i].revents = POLLNVAL;
            else {
                if (p[i*2].revents & POLLIN) guest[i].revents |= guest[i].events & (POLLIN | POLLRDNORM);
                if (p[i*2+1].revents & POLLIN) guest[i].revents |= guest[i].events & (POLLOUT | POLLWRNORM);
            }
        } else if (r >= 0) guest[i].revents |= p[i*2].revents;
        if (guest[i].revents) found++;
        if (e) event_release(e);
    }
    pthread_mutex_unlock(&g_net_mu); free(events); free(p);
    return r < 0 ? host_result(r, error) : found;
}
static int bit(const uint64_t *p, int fd) { return p && ((p[fd >> 6] >> (fd & 63)) & 1); }
static void setbit(uint64_t *p, int fd) { p[fd >> 6] |= 1ull << (fd & 63); }
int dm_net_select(int nfds, void *readset, void *writeset, void *exceptset, void *timeout) {
    if (!online()) return -1;
    if (nfds < 0 || nfds > NX_FD_MAX) return fail(EINVAL);
    uint64_t *sets[3] = {readset, writeset, exceptset}, ready[3][16] = {{0}};
    nx_pollfd p[NX_FD_MAX]; int guest[NX_FD_MAX], count = 0, ms = -1;
    if (timeout) { int64_t *tv = timeout;
        if (tv[0] < 0 || tv[1] < 0 || tv[1] > 999999) return fail(EINVAL);
        uint64_t duration = tv[0] > INT_MAX / 1000 ? INT_MAX : (uint64_t)tv[0] * 1000 + (uint64_t)(tv[1] + 999) / 1000;
        ms = duration > INT_MAX ? INT_MAX : (int)duration;
    }
    for (int i = 0; i < nfds; i++) {
        int events = (bit(sets[0], i) ? POLLIN : 0) | (bit(sets[1], i) ? POLLOUT : 0) | (bit(sets[2], i) ? POLLPRI : 0);
        if (!events) continue;
        guest[count] = i; p[count++] = (nx_pollfd){i, (short)events, 0};
    }
    int r = dm_net_poll(p, (uint64_t)count, ms);
    if (r < 0) return -1;
    int found = 0;
    for (int k = 0; k < count; k++) {
        if (p[k].revents & POLLNVAL) return fail(EBADF);
        int fd = guest[k], re = p[k].revents;
        if (bit(sets[0], fd) && (re & (POLLIN | POLLHUP | POLLERR))) { setbit(ready[0], fd); found++; }
        if (bit(sets[1], fd) && (re & (POLLOUT | POLLERR))) { setbit(ready[1], fd); found++; }
        if (bit(sets[2], fd) && (re & POLLPRI)) { setbit(ready[2], fd); found++; }
    }
    for (int k = 0; k < 3; k++) if (sets[k]) memcpy(sets[k], ready[k], sizeof ready[k]);
    return found;
}

static int ai_flags_in(int nx) {
    if (nx & ~0xf0f) return -1;
    return (nx & 1 ? AI_PASSIVE : 0) | (nx & 2 ? AI_CANONNAME : 0) | (nx & 4 ? AI_NUMERICHOST : 0) |
           (nx & 8 ? AI_NUMERICSERV : 0) | (nx & 0x100 ? AI_ALL : 0) | (nx & 0x200 ? AI_V4MAPPED_CFG : 0) |
           (nx & 0x400 ? AI_ADDRCONFIG : 0) | (nx & 0x800 ? AI_V4MAPPED : 0);
}
static int ai_flags_out(int h) { return (h & ~AI_NUMERICSERV) | (h & AI_NUMERICSERV ? 8 : 0); }
static void ai_free(dm_net_addrinfo *p) { while (p) { dm_net_addrinfo *next = p->next; free(p->addr); free(p->canonname); free(p); p = next; } }
typedef struct ai_owner { dm_net_addrinfo *head; struct ai_owner *next; } ai_owner;
static pthread_mutex_t g_ai_mu = PTHREAD_MUTEX_INITIALIZER;
static ai_owner *g_ai_owners;
int dm_net_getaddrinfo(const char *host, const char *service, const void *hints, void **result) {
    if (!result) return 4;
    *result = NULL;
    if (!online()) return 4;
    struct addrinfo hi = {0}, *hr = NULL;
    if (hints) { const dm_net_addrinfo *h = hints;
        hi.ai_family = native_family(h->family); hi.ai_socktype = h->socktype; hi.ai_protocol = h->protocol;
        hi.ai_flags = ai_flags_in(h->flags);
        if (hi.ai_family < 0) return EAI_FAMILY;
        if (hi.ai_flags < 0) return EAI_BADFLAGS;
    }
    dm_thread_native_wait(1); int r = getaddrinfo(host, service, hints ? &hi : NULL, &hr), e = errno; dm_thread_native_wait(0);
    if (r) { if (r == EAI_SYSTEM) g_net_error = nx_errno(e); return r; }
    dm_net_addrinfo *head = NULL, **next = &head;
    for (struct addrinfo *h = hr; h; h = h->ai_next) {
        if (!guest_family(h->ai_family)) continue;
        dm_net_addrinfo *a = calloc(1, sizeof *a);
        if (!a) { r = EAI_MEMORY; break; }
        a->flags = ai_flags_out(h->ai_flags); a->family = guest_family(h->ai_family); a->socktype = h->ai_socktype; a->protocol = h->ai_protocol;
        a->addrlen = h->ai_addrlen; a->addr = malloc(a->addrlen);
        a->canonname = h->ai_canonname ? strdup(h->ai_canonname) : NULL;
        if (!a->addr || (h->ai_canonname && !a->canonname)) { ai_free(a); r = EAI_MEMORY; break; }
        unsigned n = a->addrlen; addr_out(h->ai_addr, h->ai_addrlen, a->addr, &n);
        *next = a; next = &a->next;
    }
    freeaddrinfo(hr);
    if (r || !head) { ai_free(head); return r ? r : EAI_NONAME; }
    ai_owner *owner = malloc(sizeof *owner);
    if (!owner) { ai_free(head); return EAI_MEMORY; }
    owner->head = head; pthread_mutex_lock(&g_ai_mu); owner->next = g_ai_owners; g_ai_owners = owner; pthread_mutex_unlock(&g_ai_mu);
    *result = head; return 0;
}
void dm_net_freeaddrinfo(void *result) {
    if (!result) return;
    pthread_mutex_lock(&g_ai_mu); ai_owner **at = &g_ai_owners;
    while (*at && (*at)->head != result) at = &(*at)->next;
    ai_owner *p = *at; if (p) *at = p->next; pthread_mutex_unlock(&g_ai_mu);
    if (p) { ai_free(p->head); free(p); }
}
void *dm_net_gethostbyname(const char *host) {
    static __thread struct hostent result;
    static __thread char name[NI_MAXHOST], addresses[16][4], *address_list[17], *aliases[1];
    dm_net_addrinfo hints = {.family = NX_AF_INET, .socktype = SOCK_STREAM, .flags = 2}, *list = NULL;
    if (!host || dm_net_getaddrinfo(host, NULL, &hints, (void **)&list)) return NULL;
    const char *canonical = list->canonname ? list->canonname : host;
    size_t len = strlen(canonical); if (len >= sizeof name) len = sizeof name - 1;
    memcpy(name, canonical, len); name[len] = 0;
    int count = 0;
    for (dm_net_addrinfo *p = list; p && count < 16; p = p->next) if (p->family == NX_AF_INET && p->addrlen >= 8) {
        memcpy(addresses[count], (char *)p->addr + 4, 4); address_list[count] = addresses[count]; count++;
    }
    address_list[count] = NULL; aliases[0] = NULL;
    result = (struct hostent){name, aliases, NX_AF_INET, 4, address_list}; dm_net_freeaddrinfo(list);
    return count ? &result : NULL;
}
int dm_net_getnameinfo(const void *address, unsigned len, char *host, unsigned hn, char *service, unsigned sn, int flags) {
    if (!online()) return 4;
    struct sockaddr_storage a; socklen_t n;
    if (addr_in(address, len, &a, &n)) return EAI_FAMILY;
    if (flags & ~31) return EAI_BADFLAGS;
    dm_thread_native_wait(1); int r = getnameinfo((struct sockaddr *)&a, n, host, hn, service, sn, flags), e = errno; dm_thread_native_wait(0);
    if (r == EAI_SYSTEM) g_net_error = nx_errno(e);
    return r;
}
static uint32_t interface_address(int *available) {
    if (!dm_net_enabled()) return 0;
    struct ifaddrs *all = NULL; uint32_t address = 0;
    dm_thread_native_wait(1); int r = getifaddrs(&all); dm_thread_native_wait(0);
    if (r) return 0;
    for (struct ifaddrs *i = all; i; i = i->ifa_next) if (i->ifa_addr && (i->ifa_flags & IFF_UP) && !(i->ifa_flags & IFF_LOOPBACK)) {
        int af = i->ifa_addr->sa_family;
        if (af == AF_INET || af == AF_INET6) *available = 1;
        if (!address && af == AF_INET) address = ((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr;
    }
    freeifaddrs(all); return address;
}
int dm_net_available(void) { int available = 0; (void)interface_address(&available); return available; }
uint32_t dm_net_ipv4(void) { int available = 0; return interface_address(&available); }

// NSD's 0x100-byte Fqdn carries a hostname. Literal DNS names pass through;
// environment placeholders require Nintendo's exchange settings, not host DNS.
int dm_net_resolve_fqdn(void *out, const void *in) {
    if (!dm_net_enabled() || !out || !in) return -1;
    const unsigned char *name=in; size_t n=strnlen(in,256), label=0;
    if (!n || n>253) return -1;
    for (size_t i=0;i<n;i++) {
        unsigned c=name[i];
        if (c=='.') {
            if (!label || name[i-1]=='-') return -1;
            label=0; continue;
        }
        if (!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-')) return -1;
        if ((!label && c=='-') || ++label>63) return -1;
    }
    if (name[n-1]=='-') return -1;
    char copy[256]; memcpy(copy,in,n+1); memset(out,0,256); memcpy(out,copy,n+1);
    return 0;
}
