// Real loopback traffic through the production Horizon/Darwin ABI bridge.
// No game files, external network, authentication or credentials are involved.
#include "../net_transport.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <poll.h>

static __thread int wait_depth;
static _Atomic unsigned wait_entries;
void dm_thread_native_wait(int entering) {
    if (entering) { wait_depth++; wait_entries++; }
    else { assert(wait_depth > 0); wait_depth--; }
}
typedef struct { uint8_t len, family; uint16_t port; uint32_t address; uint8_t zero[8]; } nx_addr4;
typedef struct { int32_t fd; int16_t events, revents; } nx_pollfd;
static nx_addr4 local4(void) { return (nx_addr4){16, 2, 0, htonl(0x7f000001), {0}}; }
static void timeout(int fd) {
    int64_t tv[2] = {1, 0}; unsigned n = sizeof tv;
    assert(dm_net_opt(fd, 0xffff, 0x1006, tv, &n, 1) == 0);
}
static void bit_set(uint64_t *set, int fd) { set[fd >> 6] |= 1ull << (fd & 63); }
static int bit_get(uint64_t *set, int fd) { return (int)((set[fd >> 6] >> (fd & 63)) & 1); }

static void tcp(void) {
    int listener = dm_net_socket(2, 1, 0); assert(listener >= 32 && listener < 1024);
    nx_addr4 server = local4(); assert(dm_net_bind(listener, &server, sizeof server) == 0);
    assert(dm_net_listen(listener, 2) == 0);
    unsigned n = sizeof server; assert(dm_net_name(listener, &server, &n, 0) == 0);
    assert(n == 16 && server.len == 16 && server.family == 2 && server.port != 0);
    int client = dm_net_socket(2, 1 | 0x20000000, 0); assert(client >= 32);
    assert(dm_net_fcntl(client, 3, 0) & 4);
    assert(dm_net_fcntl(client, 2, 1) == 0 && dm_net_fcntl(client, 1, 0) == 1);
    int r = dm_net_connect(client, &server, sizeof server);
    assert(r == 0 || (r == -1 && dm_net_errno() == 36)); // FreeBSD EINPROGRESS.
    nx_pollfd p = {client, 4, 0}; assert(dm_net_poll(&p, 1, 1000) == 1 && (p.revents & 4));
    int err = -1; n = sizeof err; assert(dm_net_opt(client, 0xffff, 0x1007, &err, &n, 0) == 0 && err == 0);
    nx_addr4 peer = {0}; n = sizeof peer; int accepted = dm_net_accept(listener, &peer, &n); assert(accepted >= 32);
    assert(peer.family == 2 && peer.address == htonl(0x7f000001)); timeout(accepted);
    assert(dm_net_send(client, "hello", 5, 0x20000, NULL, 0) == 5); // Horizon MSG_NOSIGNAL.
    char buf[8] = {0}; assert(dm_net_recv(accepted, buf, 5, 2, NULL, NULL) == 5 && !memcmp(buf, "hello", 5)); // Peek.
    int unread = 0; assert(dm_net_ioctl(accepted, 0x4004667f, &unread, sizeof unread) == 0 && unread == 5);
    uint64_t readset[16] = {0}, writeset[16] = {0}; int64_t tv[2] = {1, 0};
    bit_set(readset, accepted); bit_set(writeset, client);
    assert(dm_net_select((client > accepted ? client : accepted) + 1, readset, writeset, NULL, tv) == 2);
    assert(bit_get(readset, accepted) && bit_get(writeset, client));
    assert(dm_net_recv(accepted, buf, sizeof buf, 0, NULL, NULL) == 5 && !memcmp(buf, "hello", 5));
    assert(dm_net_recv(client, buf, 1, 0, NULL, NULL) == -1 && dm_net_errno() == 35); // EAGAIN.
    int nonblocking = 0;
    assert(dm_net_ioctl(client, 0x8004667e, &nonblocking, sizeof nonblocking) == 0);
    assert(!(dm_net_fcntl(client, 3, 0) & 4));
    assert(dm_net_fcntl(client, 4, 4) == 0 && (dm_net_fcntl(client, 3, 0) & 4));
    n = sizeof peer; assert(dm_net_name(client, &peer, &n, 1) == 0 && peer.port == server.port);
    int protocol = 0; n = sizeof protocol; assert(dm_net_opt(client, 0xffff, 0x1016, &protocol, &n, 0) == 0 && protocol == 6);
    int one = 1; n = sizeof one; assert(dm_net_opt(client, 0xffff, 0x800, &one, &n, 1) == 0); // SO_NOSIGPIPE maps to Darwin 0x1022.
    assert(dm_net_shutdown(client, 1) == 0);
    assert(dm_net_recv(accepted, buf, sizeof buf, 0, NULL, NULL) == 0);
    assert(dm_net_close(client) == 0 && dm_net_close(accepted) == 0 && dm_net_close(listener) == 0);
    assert(dm_net_close(client) == -1 && dm_net_errno() == 9);
    assert(wait_depth == 0);
}

static void udp(void) {
    int server = dm_net_socket(2, 2, 0), client = dm_net_socket(2, 2, 0); assert(server >= 32 && client >= 32);
    nx_addr4 address = local4(), peer = {0}; assert(dm_net_bind(server, &address, sizeof address) == 0);
    unsigned n = sizeof address; assert(dm_net_name(server, &address, &n, 0) == 0); timeout(server);
    assert(dm_net_send(client, "udp", 3, 0, &address, sizeof address) == 3);
    char out[8] = {0}; n = sizeof peer;
    assert(dm_net_recv(server, out, sizeof out, 0, &peer, &n) == 3 && !memcmp(out, "udp", 3));
    assert(n == 16 && peer.family == 2 && peer.port != 0);
    assert(dm_net_send(server, "ok", 2, 0, &peer, sizeof peer) == 2); timeout(client);
    assert(dm_net_recv(client, out, sizeof out, 0, NULL, NULL) == 2 && !memcmp(out, "ok", 2));
    assert(dm_net_close(server) == 0 && dm_net_close(client) == 0);
}

static void dns(void) {
    dm_net_addrinfo hints = {.family = 2, .socktype = 1, .flags = 8}, *result = NULL;
    assert(dm_net_getaddrinfo("localhost", "80", &hints, (void **)&result) == 0 && result);
    assert(result->family == 2 && result->socktype == 1 && result->addrlen == 16);
    assert(offsetof(dm_net_addrinfo, canonname) == 24 && offsetof(dm_net_addrinfo, addr) == 32 && offsetof(dm_net_addrinfo, next) == 40);
    char host[64], service[32];
    assert(dm_net_getnameinfo(result->addr, result->addrlen, host, sizeof host, service, sizeof service, 2 | 8) == 0);
    assert(!strcmp(host, "127.0.0.1") && !strcmp(service, "80"));
    dm_net_freeaddrinfo(result); dm_net_freeaddrinfo(result); // Invalid/double frees do not reach libc.
    hints.flags = 8; result = NULL;
    assert(dm_net_getaddrinfo("localhost", "http", &hints, (void **)&result) == 8 && !result); // AI_NUMERICSERV 0x8 is not Darwin's 0x1000.
    struct hostent *ent = dm_net_gethostbyname("localhost");
    assert(ent && ent->h_addrtype == 2 && ent->h_length == 4 && ent->h_addr_list[0]);
    hints.family = 1234;
    assert(dm_net_getaddrinfo("localhost", "80", &hints, (void **)&result) == 5 && !result);
}

static void ipv6(void) {
    dm_net_addrinfo hints = {.family = 28, .socktype = 2, .flags = 4 | 8}, *result = NULL;
    assert(dm_net_getaddrinfo("::1", "0", &hints, (void **)&result) == 0 && result);
    assert(result->family == 28 && result->addrlen == 28 && ((uint8_t *)result->addr)[1] == 28);
    int server = dm_net_socket(28, 2, 0), client = dm_net_socket(28, 2, 0); assert(server >= 32 && client >= 32);
    assert(dm_net_bind(server, result->addr, result->addrlen) == 0);
    uint8_t address[28], peer[28]; unsigned n = sizeof address;
    assert(dm_net_name(server, address, &n, 0) == 0 && n == 28 && address[1] == 28); timeout(server);
    assert(dm_net_send(client, "v6", 2, 0, address, sizeof address) == 2);
    char out[8]; n = sizeof peer;
    assert(dm_net_recv(server, out, sizeof out, 0, peer, &n) == 2 && !memcmp(out, "v6", 2));
    assert(n == 28 && peer[1] == 28);
    assert(dm_net_close(server) == 0 && dm_net_close(client) == 0);
    dm_net_freeaddrinfo(result);
}

typedef struct { int fd, write, polling; long result; int error; uint64_t value; } event_job;
static void *event_worker(void *arg) {
    event_job *job = arg;
    if (job->polling) {
        nx_pollfd p = {job->fd, POLLIN, 0};
        job->result = dm_net_poll(&p, 1, 2000); job->value = (uint64_t)p.revents;
    } else job->result = job->write ? dm_net_write(job->fd, &job->value, 8) : dm_net_read(job->fd, &job->value, 8);
    job->error = dm_net_errno(); assert(wait_depth == 0); return NULL;
}
static void event_ready(int fd, int expected) {
    nx_pollfd p = {fd, POLLIN | POLLOUT, 0};
    assert(dm_net_poll(&p, 1, 0) == (expected ? 1 : 0)); assert(p.revents == expected);
    uint64_t reads[16] = {0}, writes[16] = {0}; int64_t tv[2] = {0, 0};
    bit_set(reads, fd); bit_set(writes, fd);
    assert(dm_net_select(fd + 1, reads, writes, NULL, tv) == (!!(expected & POLLIN) + !!(expected & POLLOUT)));
    assert(bit_get(reads, fd) == !!(expected & POLLIN)); assert(bit_get(writes, fd) == !!(expected & POLLOUT));
}
static void eventfds(void) {
    assert(dm_net_eventfd(0, 2) == -1 && dm_net_errno() == 22);
    assert(dm_net_eventfd(UINT64_MAX, 0) == -1 && dm_net_errno() == 22);
    int fd = dm_net_eventfd(0, 4 | 0x100000); assert(fd >= 32);
    assert(dm_net_native_fd(fd) == -1 && dm_net_errno() == 38);
    assert(dm_net_fcntl(fd, 1, 0) == 1 && dm_net_fcntl(fd, 3, 0) == 6);
    assert(dm_net_fcntl(fd, 2, 0) == 0 && dm_net_fcntl(fd, 1, 0) == 0);
    event_ready(fd, POLLOUT);
    uint64_t value = 7, out = 0;
    assert(dm_net_read(fd, &out, 8) == -1 && dm_net_errno() == 35);
    assert(dm_net_write(fd, &value, 7) == -1 && dm_net_errno() == 22);
    assert(dm_net_read(fd, NULL, 8) == -1 && dm_net_errno() == 22);
    assert(dm_net_write(fd, &value, 8) == 8 && dm_net_write(fd, &value, 8) == 8);
    event_ready(fd, POLLIN | POLLOUT);
    assert(dm_net_read(fd, &out, 16) == 8 && out == 14); event_ready(fd, POLLOUT);
    value = UINT64_MAX; assert(dm_net_write(fd, &value, 8) == -1 && dm_net_errno() == 22);
    value--; assert(dm_net_write(fd, &value, 8) == 8); event_ready(fd, POLLIN);
    value = 1; assert(dm_net_write(fd, &value, 8) == -1 && dm_net_errno() == 35);
    value = 0; assert(dm_net_write(fd, &value, 8) == 8);
    assert(dm_net_read(fd, &out, 8) == 8 && out == UINT64_MAX - 1);
    assert(dm_net_close(fd) == 0);
    assert(dm_net_read(fd, &out, 8) == -1 && dm_net_errno() == 9);
    fd = dm_net_eventfd(2, 1 | 4); assert(fd >= 32);
    assert(dm_net_read(fd, &out, 8) == 8 && out == 1); event_ready(fd, POLLIN | POLLOUT);
    assert(dm_net_read(fd, &out, 8) == 8 && out == 1); event_ready(fd, POLLOUT);
    assert(dm_net_close(fd) == 0);
    // Blocking readers and poll wake on a real write; overflowing writers wake on read.
    fd = dm_net_eventfd(0, 0); pthread_t thread;
    event_job job = {.fd = fd}; assert(pthread_create(&thread, NULL, event_worker, &job) == 0);
    usleep(20000); value = 42; assert(dm_net_write(fd, &value, 8) == 8);
    pthread_join(thread, NULL); assert(job.result == 8 && job.value == 42);
    job = (event_job){.fd = fd, .polling = 1}; assert(pthread_create(&thread, NULL, event_worker, &job) == 0);
    usleep(20000); value = UINT64_MAX - 1; assert(dm_net_write(fd, &value, 8) == 8);
    pthread_join(thread, NULL); assert(job.result == 1 && (job.value & POLLIN));
    job = (event_job){.fd = fd, .write = 1, .value = 1}; assert(pthread_create(&thread, NULL, event_worker, &job) == 0);
    usleep(20000); assert(dm_net_read(fd, &out, 8) == 8 && out == UINT64_MAX - 1);
    pthread_join(thread, NULL); assert(job.result == 8);
    assert(dm_net_read(fd, &out, 8) == 8 && out == 1);
    // Closing a descriptor wakes its blocked reader and keeps metadata alive.
    job = (event_job){.fd = fd}; assert(pthread_create(&thread, NULL, event_worker, &job) == 0);
    usleep(20000); assert(dm_net_close(fd) == 0);
    pthread_join(thread, NULL); assert(job.result == -1 && job.error == 9);
    fd = dm_net_eventfd(0, 0);
    job = (event_job){.fd = fd, .polling = 1}; assert(pthread_create(&thread, NULL, event_worker, &job) == 0);
    usleep(20000); assert(dm_net_close(fd) == 0);
    int replacement = dm_net_eventfd(0, 4); assert(replacement == fd);
    pthread_join(thread, NULL); assert(job.result == 1 && job.value == POLLNVAL);
    assert(dm_net_fcntl(replacement, 4, 0) == 0 && !(dm_net_fcntl(replacement, 3, 0) & 4));
    int nonblock = 1; assert(dm_net_ioctl(replacement, 0x8004667e, &nonblock, 4) == 0);
    assert(dm_net_read(replacement, &out, 8) == -1 && dm_net_errno() == 35);
    unsetenv("DM_NETWORK"); assert(dm_net_close(replacement) == 0); setenv("DM_NETWORK", "1", 1);
}

int main(void) {
    char fqdn[256];
    unsetenv("DM_NETWORK"); assert(!dm_net_enabled() && !dm_net_available() && !dm_net_ipv4());
    assert(dm_net_resolve_fqdn(fqdn,"login.live.com"));
    assert(dm_net_socket(2, 1, 0) == -1 && dm_net_errno() == 50);
    assert(dm_net_eventfd(0, 4) == -1 && dm_net_errno() == 50);
    void *result = (void *)1; assert(dm_net_getaddrinfo("localhost", "80", NULL, &result) == 4 && !result);
    setenv("DM_NETWORK", "1", 1); assert(dm_net_enabled());
    assert(!dm_net_resolve_fqdn(fqdn,"self.events.data.microsoft.com"));
    assert(!strcmp(fqdn,"self.events.data.microsoft.com") && !fqdn[255]);
    assert(!dm_net_resolve_fqdn(fqdn,fqdn));
    assert(dm_net_resolve_fqdn(fqdn,"https://login.live.com/"));
    assert(dm_net_resolve_fqdn(fqdn,"api.%.nintendo.net"));
    assert(dm_net_resolve_fqdn(fqdn,"bad..example.com"));
    assert(dm_net_resolve_fqdn(fqdn,"-bad.example.com"));
    assert(dm_net_resolve_fqdn(fqdn,"bad-.example.com"));
    char too_long[256]; memset(too_long,'a',sizeof too_long);
    assert(dm_net_resolve_fqdn(fqdn,too_long));
    int pipefd[2]; assert(pipe(pipefd) == 0); assert(pipefd[0] < 32);
    assert(dm_net_close(pipefd[0]) == -1 && dm_net_errno() == 9 && fcntl(pipefd[0], F_GETFD) >= 0);
    close(pipefd[0]); close(pipefd[1]);
    nx_pollfd bad = {999999, 1, 0}; assert(dm_net_poll(&bad, 1, 0) == 1 && (bad.revents & 32));
    uint64_t invalid[16] = {0}; bit_set(invalid, 1); int64_t tv[2] = {0, 0};
    assert(dm_net_select(2, invalid, NULL, NULL, tv) == -1 && dm_net_errno() == 9);
    tcp(); udp(); dns(); ipv6(); eventfds();
    int owned = dm_net_socket(2, 1, 0); assert(owned >= 32);
    int value = 1; unsigned n = sizeof value;
    assert(dm_net_opt(owned, 0xffff, 0x1017, &value, &n, 1) == -1 && dm_net_errno() == 42);
    unsetenv("DM_NETWORK"); assert(dm_net_close(owned) == 0); // Cleanup remains possible after opt-out.
    assert(wait_depth == 0 && wait_entries > 10);
    puts("native transport: default offline, IPv4 TCP/UDP, DNS, nonblocking I/O, select, IPv6 FD ownership and native event counters passed");
    return 0;
}
