// Optional native transport for the Horizon socket ABI. Disabled unless DM_NETWORK=1.
#pragma once
#include <stdint.h>
int dm_net_enabled(void);
int dm_net_available(void);
uint32_t dm_net_ipv4(void);
int dm_net_errno(void);
void dm_net_set_errno(int error);
int dm_net_native_fd(int guest);
int dm_net_eventfd(uint64_t initial, int flags);
long dm_net_read(int fd, void *buf, uint64_t len);
long dm_net_write(int fd, const void *buf, uint64_t len);
int dm_net_socket(int family, int type, int protocol);
int dm_net_connect(int fd, const void *addr, unsigned len);
int dm_net_bind(int fd, const void *addr, unsigned len);
int dm_net_listen(int fd, int backlog);
int dm_net_accept(int fd, void *addr, unsigned *len);
int dm_net_close(int fd);
int dm_net_shutdown(int fd, int how);
int dm_net_name(int fd, void *addr, unsigned *len, int peer);
int dm_net_opt(int fd, int level, int option, void *value, unsigned *len, int set);
int dm_net_fcntl(int fd, int command, uint64_t value);
int dm_net_ioctl(int fd, unsigned request, void *value, uint64_t len);
long dm_net_recv(int fd, void *buf, uint64_t len, int flags, void *addr, unsigned *addrlen);
long dm_net_send(int fd, const void *buf, uint64_t len, int flags, const void *addr, unsigned addrlen);
int dm_net_poll(void *fds, uint64_t count, int timeout_ms);
int dm_net_select(int count, void *readfds, void *writefds, void *exceptfds, void *timeout);
int dm_net_getaddrinfo(const char *host, const char *service, const void *hints, void **result);
void dm_net_freeaddrinfo(void *result);
void *dm_net_gethostbyname(const char *host);
int dm_net_getnameinfo(const void *addr, unsigned len, char *host, unsigned hostlen, char *service, unsigned servicelen, int flags);

// FreeBSD/Horizon layout, not Darwin's constants. Ownership belongs to FreeAddrInfo.
typedef struct dm_net_addrinfo {
    int32_t flags, family, socktype, protocol;
    uint32_t addrlen, pad;
    char *canonname;
    void *addr;
    struct dm_net_addrinfo *next;
} dm_net_addrinfo;

// NSD translates service names, not IP addresses. Literal DNS names need no exchange.
int dm_net_resolve_fqdn(void *out, const void *in);
