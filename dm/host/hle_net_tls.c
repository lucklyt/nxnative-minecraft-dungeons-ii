// Optional nn::ssl transport through Apple's TLS implementation. Trust and host
// name checks are mandatory; a network service cannot authenticate a user by itself.
#include "hle.h"
#include "net_transport.h"
#include "net_tls.h"
#include <Security/SecureTransport.h>
#include <Security/SecTrust.h>
#include <Security/SecPolicy.h>
#include <Security/SecCertificate.h>
#include <CoreFoundation/CoreFoundation.h>
#include <sys/socket.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>

typedef struct tls_ca { uint64_t id; SecCertificateRef cert; struct tls_ca *next; } tls_ca;
typedef struct tls_context { void *guest; int version; tls_ca *ca; struct tls_context *next; } tls_context;
typedef struct tls_connection {
    void *guest; SSLContextRef ssl; CFArrayRef roots, peer_der;
    uint64_t id; void *cert_buffer; unsigned cert_capacity, cert_size, cert_count;
    int chain, verified, complete, alpn_enabled, alpn_offered; int fd, mode, cache, no_close; short wait_events;
    uint32_t error, verify_error; unsigned refs; int removed; pthread_mutex_t mu;
    char hostname[256]; unsigned char peek[16384]; size_t peek_n, write_buffered;
    struct tls_connection *next;
} tls_connection;
static pthread_mutex_t tls_mu = PTHREAD_MUTEX_INITIALIZER;
static tls_context *tls_contexts;
static tls_connection *tls_connections;
static uint64_t tls_id;

static tls_connection *tls_get(void *guest) {
    pthread_mutex_lock(&tls_mu);
    tls_connection *c = tls_connections;
    while (c && c->guest != guest) c = c->next;
    if (c) c->refs++;
    pthread_mutex_unlock(&tls_mu);
    if (c && pthread_mutex_trylock(&c->mu)) { dm_thread_native_wait(1); pthread_mutex_lock(&c->mu); dm_thread_native_wait(0); }
    return c;
}
static void tls_free(tls_connection *c) {
    if (c->ssl) { if (c->fd >= 0) SSLClose(c->ssl); CFRelease(c->ssl); }
    if (c->roots) CFRelease(c->roots);
    if (c->peer_der) CFRelease(c->peer_der);
    if (c->fd >= 0) close(c->fd);
    pthread_mutex_destroy(&c->mu); free(c);
}
static void tls_put(tls_connection *c) {
    pthread_mutex_unlock(&c->mu);
    pthread_mutex_lock(&tls_mu); int done = --c->refs == 0 && c->removed; pthread_mutex_unlock(&tls_mu);
    if (done) tls_free(c);
}
uint32_t dm_tls_context_create(void *guest, int version) {
    if (!dm_net_enabled() || !guest || !version) return RES_FAIL;
    tls_context *c = calloc(1, sizeof *c); if (!c) return RES_FAIL;
    c->guest = guest; c->version = version;
    pthread_mutex_lock(&tls_mu);
    for (tls_context *p = tls_contexts; p; p = p->next) if (p->guest == guest) { pthread_mutex_unlock(&tls_mu); free(c); return RES_FAIL; }
    c->next = tls_contexts; tls_contexts = c; pthread_mutex_unlock(&tls_mu); return RES_OK;
}
void dm_tls_context_destroy(void *guest) {
    pthread_mutex_lock(&tls_mu); tls_context **p = &tls_contexts;
    while (*p && (*p)->guest != guest) p = &(*p)->next;
    tls_context *c = *p; if (c) *p = c->next; pthread_mutex_unlock(&tls_mu);
    if (!c) return;
    while (c->ca) { tls_ca *a = c->ca; c->ca = a->next; CFRelease(a->cert); free(a); }
    free(c);
}
// Decode one PEM certificate without accepting arbitrary bytes between base64 digits.
static CFDataRef tls_pem(const char *p, size_t n) {
    unsigned char *out = malloc(n ? n : 1); if (!out) return NULL;
    size_t used = 0; uint32_t bits = 0; int count = 0, padded = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char x = p[i]; int v;
        if (x == ' ' || x == '\n' || x == '\r' || x == '\t') continue;
        if (x == '=') { padded = 1; continue; }
        if (padded) { free(out); return NULL; }
        if (x >= 'A' && x <= 'Z') v = x - 'A'; else if (x >= 'a' && x <= 'z') v = x - 'a' + 26;
        else if (x >= '0' && x <= '9') v = x - '0' + 52; else if (x == '+') v = 62; else if (x == '/') v = 63;
        else { free(out); return NULL; }
        bits = (bits << 6) | (unsigned)v; count += 6;
        if (count >= 8) { count -= 8; out[used++] = (unsigned char)(bits >> count); }
    }
    CFDataRef data = used ? CFDataCreate(NULL, out, (CFIndex)used) : NULL; free(out); return data;
}
uint32_t dm_tls_import_ca(void *guest, uint64_t *id, const void *bytes, unsigned n, int format) {
    if (!dm_net_enabled() || !id || !bytes || !n || n > 16 * 1024 * 1024 || (format != 1 && format != 2)) return RES_FAIL;
    const char *p = bytes, *end = p + n; tls_ca *added = NULL; int ok = 0;
    do {
        CFDataRef der = NULL;
        if (format == 2) { der = CFDataCreate(NULL, bytes, n); p = end; }
        else {
            static const char begin[] = "-----BEGIN CERTIFICATE-----", finish[] = "-----END CERTIFICATE-----";
            const char *a = NULL, *z = NULL;
            for (; (size_t)(end - p) >= sizeof begin - 1; p++) if (!memcmp(p, begin, sizeof begin - 1)) { a = p + sizeof begin - 1; break; }
            if (!a) break;
            for (p = a; (size_t)(end - p) >= sizeof finish - 1; p++) if (!memcmp(p, finish, sizeof finish - 1)) { z = p; break; }
            if (!z) goto fail;
            der = tls_pem(a, (size_t)(z - a)); p = z + sizeof finish - 1;
        }
        if (!der) goto fail;
        SecCertificateRef cert = SecCertificateCreateWithData(NULL, der); CFRelease(der);
        if (!cert) goto fail;
        tls_ca *a = calloc(1, sizeof *a); if (!a) { CFRelease(cert); goto fail; }
        a->cert = cert; a->next = added; added = a; ok++;
    } while (p < end);
    if (!ok) goto fail;
    pthread_mutex_lock(&tls_mu); tls_context *c = tls_contexts;
    while (c && c->guest != guest) c = c->next;
    if (!c) { pthread_mutex_unlock(&tls_mu); goto fail; }
    *id = ++tls_id;
    while (added) { tls_ca *a = added; added = a->next; a->id = *id; a->next = c->ca; c->ca = a; }
    pthread_mutex_unlock(&tls_mu); return RES_OK;
fail:
    while (added) { tls_ca *a = added; added = a->next; CFRelease(a->cert); free(a); }
    return RES_FAIL;
}
uint32_t dm_tls_remove_ca(void *guest, uint64_t id) {
    int found = 0; pthread_mutex_lock(&tls_mu);
    for (tls_context *c = tls_contexts; c; c = c->next) if (c->guest == guest) {
        tls_ca **p = &c->ca;
        while (*p) { tls_ca *a = *p; if (a->id != id) { p = &a->next; continue; } *p = a->next; CFRelease(a->cert); free(a); found = 1; }
    }
    pthread_mutex_unlock(&tls_mu); return found ? RES_OK : RES_FAIL;
}
static OSStatus tls_socket_read(SSLConnectionRef connection, void *data, size_t *length) {
    tls_connection *c = (tls_connection *)connection; size_t want = *length; *length = 0;
    c->wait_events = POLLIN;
    ssize_t n = recv(c->fd, data, want, MSG_DONTWAIT);
    if (n > 0) { *length = (size_t)n; return (size_t)n == want ? noErr : errSSLWouldBlock; }
    if (!n) return errSSLClosedGraceful;
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? errSSLWouldBlock : errSSLClosedAbort;
}
static OSStatus tls_socket_write(SSLConnectionRef connection, const void *data, size_t *length) {
    tls_connection *c = (tls_connection *)connection; size_t want = *length; *length = 0;
    c->wait_events = POLLOUT;
    ssize_t n = send(c->fd, data, want, MSG_DONTWAIT);
    if (n >= 0) { *length = (size_t)n; return (size_t)n == want ? noErr : errSSLWouldBlock; }
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? errSSLWouldBlock : errSSLClosedAbort;
}
uint32_t dm_tls_create(void *guest, void *context) {
    if (!dm_net_enabled() || !guest) return RES_FAIL;
    tls_connection *c = calloc(1, sizeof *c); if (!c) return RES_FAIL;
    c->guest = guest; c->fd = -1; c->mode = 1; pthread_mutex_init(&c->mu, NULL);
    c->ssl = SSLCreateContext(NULL, kSSLClientSide, kSSLStreamType);
    if (!c->ssl) { tls_free(c); return RES_FAIL; }
    CFMutableArrayRef roots = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    if (!roots) { tls_free(c); return RES_FAIL; }
    pthread_mutex_lock(&tls_mu); tls_context *ctx = tls_contexts;
    while (ctx && ctx->guest != context) ctx = ctx->next;
    for (tls_connection *p = tls_connections; p; p = p->next) if (p->guest == guest) ctx = NULL;
    if (!ctx) { pthread_mutex_unlock(&tls_mu); CFRelease(roots); tls_free(c); return RES_FAIL; }
    if (!(ctx->version & (1 | (1 << 24) | (1 << 5)))) { pthread_mutex_unlock(&tls_mu); CFRelease(roots); tls_free(c); return RES_FAIL; }
    for (tls_ca *a = ctx->ca; a; a = a->next) CFArrayAppendValue(roots, a->cert);
    c->roots = roots;
    OSStatus status = SSLSetIOFuncs(c->ssl, tls_socket_read, tls_socket_write);
    if (!status) status = SSLSetConnection(c->ssl, c);
    if (!status) status = SSLSetProtocolVersionMin(c->ssl, kTLSProtocol12);
    if (!status) status = SSLSetSessionOption(c->ssl, kSSLSessionOptionBreakOnServerAuth, true);
    if (status) { pthread_mutex_unlock(&tls_mu); tls_free(c); return RES_FAIL; }
    c->id = ++tls_id; *(uint64_t *)guest = c->id; c->next = tls_connections; tls_connections = c;
    pthread_mutex_unlock(&tls_mu); return RES_OK;
}
uint32_t dm_tls_destroy(void *guest) {
    pthread_mutex_lock(&tls_mu); tls_connection **p = &tls_connections;
    while (*p && (*p)->guest != guest) p = &(*p)->next;
    tls_connection *c = *p; int done = 0;
    if (c) { *p = c->next; c->removed = 1; done = !c->refs; *(uint64_t *)guest = 0; }
    int found = c != NULL; pthread_mutex_unlock(&tls_mu); if (done) tls_free(c); return found ? RES_OK : RES_FAIL;
}
uint32_t dm_tls_socket(void *guest, int fd) {
    tls_connection *c = tls_get(guest); if (!c) return RES_FAIL;
    int host = dm_net_native_fd(fd), copy = host >= 0 ? dup(host) : -1;
    uint32_t result = RES_FAIL;
    if (copy >= 0 && c->fd < 0) {
        int flags = fcntl(copy, F_GETFL); int yes = 1;
        if (flags >= 0 && !fcntl(copy, F_SETFL, flags | O_NONBLOCK)) { setsockopt(copy, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof yes); c->fd = copy; result = RES_OK; }
    }
    if (copy >= 0 && result) close(copy);
    tls_put(c); return result;
}
uint32_t dm_tls_hostname(void *guest, const char *name, unsigned n) {
    if (name && n && name[n - 1] == 0) n--;
    if (!name || !n || n >= 256 || memchr(name, 0, n)) return RES_FAIL;
    tls_connection *c = tls_get(guest); if (!c) return RES_FAIL;
    OSStatus s = SSLSetPeerDomainName(c->ssl, name, n);
    if (!s) { memcpy(c->hostname, name, n); c->hostname[n] = 0; }
    tls_put(c); return s ? RES_FAIL : RES_OK;
}
uint32_t dm_tls_mode(void *guest, int mode) {
    tls_connection *c = tls_get(guest); if (!c) return RES_FAIL;
    uint32_t r = mode == 1 || mode == 2 ? RES_OK : RES_FAIL; if (!r) c->mode = mode; tls_put(c); return r;
}
uint32_t dm_tls_option(void *guest, int option, int enabled) {
    tls_connection *c = tls_get(guest); if (!c) return RES_FAIL;
    uint32_t r = RES_FAIL;
    if (option == 0) { c->no_close = !!enabled; r = RES_OK; } // The native socket copy is owned; never close the caller's guest descriptor.
    else if (option == 1) { c->chain = !!enabled; c->cert_size = c->cert_count = 0; r = RES_OK; }
    else if (option == 2 && !enabled) r = RES_OK; // Never disable default peer/hostname verification.
    else if (option == 3 && !c->complete) {
        if (!enabled) {
            CFArrayRef empty = CFArrayCreate(NULL, NULL, 0, &kCFTypeArrayCallBacks);
            if (empty) { r = SSLSetALPNProtocols(c->ssl, empty) ? RES_FAIL : RES_OK; CFRelease(empty); }
        } else r = RES_OK;
        if (!r) { c->alpn_enabled = !!enabled; if (!enabled) c->alpn_offered = 0; }
    }
    tls_put(c); return r;
}
uint32_t dm_tls_verify_option(void *guest, unsigned option) {
    tls_connection *c = tls_get(guest); if (!c) return RES_FAIL;
    uint32_t r = (option & 3) == 3 && !(option & ~7u) ? RES_OK : RES_FAIL;
    tls_put(c); return r;
}
uint32_t dm_tls_session_cache(void *guest, int mode) {
    tls_connection *c = tls_get(guest); if (!c) return RES_FAIL;
    uint32_t r = mode >= 0 && mode <= 2 ? RES_OK : RES_FAIL; if (!r) c->cache = mode;
    tls_put(c); return r;
}
static uint32_t tls_status(OSStatus s) {
    if (!s) return RES_OK;
    if (s == errSSLWouldBlock) return DM_TLS_WOULD_BLOCK;
    if (s == errSSLXCertChainInvalid || s == errSSLUnknownRootCert || s == errSSLNoRootCert || s == errSSLHostNameMismatch || s == errSSLCertExpired || s == errSSLCertNotYetValid) return DM_TLS_VERIFY_FAILED;
    return RES_FAIL;
}
static int tls_wait(tls_connection *c, uint64_t deadline) {
    uint64_t now = now_ns(); if (now >= deadline) return 0;
    // TLS may need either direction, but polling a connected socket's writable bit
    // during a read spins. SecureTransport's callbacks identify the needed side below.
    struct pollfd p = {c->fd, c->wait_events ? c->wait_events : POLLIN, 0};
    int ms = (int)((deadline - now + 999999) / 1000000); if (ms > 1000) ms = 1000;
    int r; do { r = poll(&p, 1, ms); } while (r < 0 && errno == EINTR);
    return r >= 0 && now_ns() < deadline;
}
uint32_t dm_tls_handshake(void *guest) {
    if (!dm_net_enabled()) return RES_FAIL;
    tls_connection *c = tls_get(guest); if (!c) return RES_FAIL;
    if (c->fd < 0 || !c->hostname[0]) { tls_put(c); return RES_FAIL; }
    if (c->complete) { tls_put(c); return RES_OK; }
    OSStatus s; uint64_t deadline = now_ns() + 300000000000ull;
    dm_thread_native_wait(1);
    for (;;) {
        s = SSLHandshake(c->ssl);
        if (s == errSSLServerAuthCompleted) {
            SecTrustRef trust = NULL;
            s = SSLCopyPeerTrust(c->ssl, &trust);
            if (!s && trust) {
                CFStringRef host = CFStringCreateWithCString(NULL, c->hostname, kCFStringEncodingUTF8);
                SecPolicyRef policy = host ? SecPolicyCreateSSL(true, host) : NULL;
                if (host) CFRelease(host);
                if (!policy) s = errSSLXCertChainInvalid;
                else {
                    s = SecTrustSetPolicies(trust, policy); CFRelease(policy);
                    if (!s && CFArrayGetCount(c->roots)) { s = SecTrustSetAnchorCertificates(trust, c->roots); if (!s) s = SecTrustSetAnchorCertificatesOnly(trust, false); }
                    if (!s && !SecTrustEvaluateWithError(trust, NULL)) s = errSSLXCertChainInvalid;
                    if (!s) {
                        CFArrayRef chain = SecTrustCopyCertificateChain(trust);
                        CFMutableArrayRef der = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
                        if (!chain || !der || !CFArrayGetCount(chain)) s = errSSLXCertChainInvalid;
                        if (!s) for (CFIndex k = 0; k < CFArrayGetCount(chain); k++) {
                            CFDataRef data = SecCertificateCopyData((SecCertificateRef)CFArrayGetValueAtIndex(chain, k));
                            if (!data) { s = errSSLXCertChainInvalid; break; }
                            CFArrayAppendValue(der, data); CFRelease(data);
                        }
                        if (!s) { if (c->peer_der) CFRelease(c->peer_der); c->peer_der = der; der = NULL; c->verified = 1; }
                        if (chain) CFRelease(chain);
                        if (der) CFRelease(der);
                    }
                }
            } else s = errSSLXCertChainInvalid;
            if (trust) CFRelease(trust);
            if (!s) continue;
        }
        if (s != errSSLWouldBlock || c->mode == 2) break;
        if (!tls_wait(c, deadline)) { s = errSSLWouldBlock; c->error = DM_TLS_TIMEOUT; break; }
    }
    dm_thread_native_wait(0);
    c->error = s == errSSLWouldBlock && now_ns() >= deadline ? DM_TLS_TIMEOUT : tls_status(s);
    if (c->error == DM_TLS_VERIFY_FAILED) c->verify_error = c->error;
    if (!c->error) { if (!c->verified) c->error = DM_TLS_VERIFY_FAILED; else c->complete = 1; }
    uint32_t r = c->error; if (r && r!=DM_TLS_WOULD_BLOCK) LOG("ssl: native handshake status=%d result=%#x verified=%d",(int)s,r,c->verified); tls_put(c); return r;
}
static int tls_io(tls_connection *c, void *buffer, unsigned n, int write_it) {
    if (!dm_net_enabled() || c->fd < 0 || !buffer || !n) { c->error = RES_FAIL; return -1; }
    size_t done = 0; OSStatus s; uint64_t deadline = now_ns() + 300000000000ull;
    dm_thread_native_wait(1);
    do {
        if (write_it) {
            // SecureTransport can accept plaintext into its encrypted queue and
            // still return WouldBlock. Do not acknowledge that input until the
            // queue is flushed, or the caller starts waiting for a response
            // while the server is still waiting for the end of a large PUT.
            if (c->write_buffered) {
                s = SSLWrite(c->ssl, NULL, 0, &done);
                if (!s) { done = c->write_buffered; c->write_buffered = 0; }
            } else {
                s = SSLWrite(c->ssl, buffer, n, &done);
                if (s == errSSLWouldBlock) c->write_buffered = n;
            }
            if (s == errSSLWouldBlock) done = 0;
        } else s = SSLRead(c->ssl, buffer, n, &done);
        if (done || s != errSSLWouldBlock || c->mode == 2) break;
    } while (tls_wait(c, deadline));
    dm_thread_native_wait(0);
    if (done) { c->error = RES_OK; return done > INT_MAX ? INT_MAX : (int)done; }
    if (!write_it && s == errSSLClosedGraceful) { c->error = RES_OK; return 0; }
    c->error = s == errSSLWouldBlock && now_ns() >= deadline ? DM_TLS_TIMEOUT : tls_status(s);
    return c->error ? -1 : 0;
}
int dm_tls_read(void *guest, void *buffer, unsigned n) {
    tls_connection *c = tls_get(guest); if (!c) return -1; int r;
    if (c->peek_n && buffer && n) { size_t k = n < c->peek_n ? n : c->peek_n; memcpy(buffer, c->peek, k); memmove(c->peek, c->peek + k, c->peek_n - k); c->peek_n -= k; c->error = RES_OK; r = (int)k; }
    else r = tls_io(c, buffer, n, 0);
    tls_put(c); return r;
}
int dm_tls_write(void *guest, const void *buffer, unsigned n) {
    tls_connection *c = tls_get(guest); if (!c) return -1; int r = tls_io(c, (void *)buffer, n, 1); tls_put(c); return r;
}
uint32_t dm_tls_peek(void *guest, void *buffer, int *out, unsigned n) {
    if (out) *out = 0;
    tls_connection *c = tls_get(guest); if (!c || !buffer || !out || !n) { if (c) tls_put(c); return RES_FAIL; }
    if (!c->peek_n) { int r = tls_io(c, c->peek, n < sizeof c->peek ? n : sizeof c->peek, 0); if (r < 0) { uint32_t s = c->error; tls_put(c); return s; } c->peek_n = (size_t)r; }
    size_t k = n < c->peek_n ? n : c->peek_n; memcpy(buffer, c->peek, k); *out = (int)k; tls_put(c); return RES_OK;
}
int dm_tls_pending(void *guest) {
    tls_connection *c = tls_get(guest); if (!c) return -1; size_t n = 0;
    OSStatus s = SSLGetBufferedReadSize(c->ssl, &n); int r = s ? -1 : (int)(n + c->peek_n); tls_put(c); return r;
}
uint32_t dm_tls_poll(void *guest, unsigned *out, const unsigned *in, unsigned ms) {
    if (!out || !in || !dm_net_enabled()) return RES_FAIL; *out = 0;
    tls_connection *c = tls_get(guest); if (!c) return RES_FAIL;
    if (c->fd < 0) { tls_put(c); return RES_FAIL; }
    size_t buffered = 0; SSLGetBufferedReadSize(c->ssl, &buffered);
    if ((*in & 1) && (c->peek_n || buffered)) { *out = 1; tls_put(c); return RES_OK; }
    struct pollfd p = {c->fd, (short)((*in & 1 ? POLLIN : 0) | (*in & 2 ? POLLOUT : 0) | (*in & 4 ? POLLPRI : 0)), 0};
    dm_thread_native_wait(1); int n = poll(&p, 1, ms > INT_MAX ? INT_MAX : (int)ms); dm_thread_native_wait(0);
    if (n > 0) *out = (p.revents & (POLLIN | POLLHUP) ? 1u : 0) | (p.revents & POLLOUT ? 2u : 0) | (p.revents & (POLLPRI | POLLERR | POLLNVAL) ? 4u : 0);
    uint32_t r = n < 0 ? RES_FAIL : n ? RES_OK : DM_TLS_TIMEOUT; c->error = r; tls_put(c); return r;
}
uint32_t dm_tls_error(void *guest, uint32_t *out, int verify) {
    if (!out) return RES_FAIL; tls_connection *c = tls_get(guest); if (!c) { *out = RES_FAIL; return RES_FAIL; }
    *out = verify ? c->verify_error : c->error; tls_put(c); return RES_OK;
}

uint32_t dm_tls_connection_id(void *guest, uint64_t *out) {
    // Like Context::GetContextId, the SDK getter accepts the zero ID
    // before Create and after Destroy. It does not perform a service lookup.
    if (!out) return 0xe27b;
    if (!guest) { *out=0; return 0xe27b; }
    memcpy(out,guest,sizeof *out); return RES_OK;
}
uint32_t dm_tls_cert_buffer(void *guest, void *buf, unsigned cap) {
    if (!buf || !cap || cap > 16u * 1024 * 1024) return RES_FAIL;
    tls_connection *c = tls_get(guest); if (!c) return RES_FAIL;
    c->cert_buffer = buf; c->cert_capacity = cap; c->cert_size = c->cert_count = 0;
    tls_put(c); return RES_OK;
}
// Serialize only certificates from the chain Apple already authenticated.
uint32_t dm_tls_handshake_certs(void *guest, unsigned *size, unsigned *count) {
    unsigned local_size=0,local_count=0;
    if (!size) size=&local_size;
    if (!count) count=&local_count;
    *size = *count = 0;
    uint32_t result = dm_tls_handshake(guest); if (result) return result;
    tls_connection *c = tls_get(guest); if (!c) return RES_FAIL;
    // The SDK's two handshake overloads use the same service operation.
    // Certificate export is optional: the default buffer/capacity is NULL/0.
    // A verified handshake remains successful when no export was requested.
    result = c->complete && !c->cert_buffer ? RES_OK : RES_FAIL;
    if (c->complete && c->peer_der && c->cert_buffer) {
        CFIndex n = c->chain ? CFArrayGetCount(c->peer_der) : 1;
        uint64_t total = c->chain ? 16 + (uint64_t)n * 8 : 0;
        for (CFIndex i = 0; i < n; i++) total += (uint64_t)CFDataGetLength(CFArrayGetValueAtIndex(c->peer_der, i));
        if (n > 0 && n <= UINT_MAX && total <= UINT_MAX) {
            *size = (unsigned)total; *count = (unsigned)n;
            if (total <= c->cert_capacity) {
                unsigned char *buf = c->cert_buffer; unsigned offset = c->chain ? 16 + (unsigned)n * 8 : 0;
                if (c->chain) { uint64_t magic = 0x4e4d684374726543ull; uint32_t header[2] = {(uint32_t)n, 0}; memcpy(buf, &magic, 8); memcpy(buf + 8, header, 8); }
                for (CFIndex i = 0; i < n; i++) {
                    CFDataRef data = CFArrayGetValueAtIndex(c->peer_der, i); unsigned len = (unsigned)CFDataGetLength(data);
                    if (c->chain) { uint32_t entry[2] = {len, offset}; memcpy(buf + 16 + i * 8, entry, 8); }
                    memcpy(buf + offset, CFDataGetBytePtr(data), len); offset += len;
                }
                c->cert_size = (unsigned)total; c->cert_count = (unsigned)n; result = RES_OK;
            }
        }
    }
    if (result) LOG("ssl: certificate buffer result=%#x required=%u capacity=%u count=%u registered=%d",result,*size,c->cert_capacity,*count,c->cert_buffer!=NULL);
    tls_put(c); return result;
}
uint32_t dm_tls_cert_detail(void *guest, void *out, const void *optional, unsigned index) {
    if (!out) return RES_FAIL;
    struct { uint32_t size, pad; uint64_t pointer; } detail = {0};
    memcpy(out, &detail, sizeof detail);
    tls_connection *c = tls_get(guest); if (!c) return RES_FAIL;
    uint32_t result = RES_FAIL;
    if (c->complete && c->cert_size && index < c->cert_count && (!optional || optional == c->cert_buffer)) {
        unsigned offset = c->chain ? 16 + c->cert_count * 8 : 0;
        for (unsigned k = 0; k < index; k++) offset += (unsigned)CFDataGetLength(CFArrayGetValueAtIndex(c->peer_der, k));
        unsigned len = (unsigned)CFDataGetLength(CFArrayGetValueAtIndex(c->peer_der, index));
        if (offset <= c->cert_size && len <= c->cert_size - offset) {
            detail.size = len; detail.pointer = (uint64_t)(uintptr_t)((unsigned char *)c->cert_buffer + offset);
            memcpy(out, &detail, sizeof detail); result = RES_OK;
        }
    }
    tls_put(c); return result;
}
uint32_t dm_tls_alpn_set(void *guest, const unsigned char *list, unsigned n) {
    if (!list || !n || n > 65535) return RES_FAIL;
    CFMutableArrayRef array = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks); if (!array) return RES_FAIL;
    unsigned at = 0; int valid = 1;
    while (at < n) {
        unsigned len = list[at++]; if (!len || len > n - at) { valid = 0; break; }
        // SecureTransport's public API accepts ASCII protocol strings.
        // Reject opaque binary IDs rather than silently changing wire bytes.
        for (unsigned k = 0; k < len; k++) if (list[at + k] < 0x21 || list[at + k] > 0x7e) valid = 0;
        if (!valid) break;
        CFStringRef protocol = CFStringCreateWithBytes(NULL, list + at, len, kCFStringEncodingASCII, false);
        if (!protocol) { valid = 0; break; }
        CFArrayAppendValue(array, protocol); CFRelease(protocol); at += len;
    }
    tls_connection *c = valid ? tls_get(guest) : NULL; uint32_t result = RES_FAIL;
    if (c) {
        if (!c->complete && !SSLSetALPNProtocols(c->ssl, array)) { c->alpn_enabled = c->alpn_offered = 1; result = RES_OK; }
        tls_put(c);
    }
    CFRelease(array); return result;
}
uint32_t dm_tls_alpn_get(void *guest, int *state, unsigned char *out, unsigned *outlen, unsigned cap) {
    if (!state || !outlen || (cap && !out)) return RES_FAIL;
    *state = 0; *outlen = 0;
    tls_connection *c = tls_get(guest); if (!c) return RES_FAIL;
    uint32_t result = RES_FAIL; CFArrayRef selected = NULL;
    if (c->complete && !SSLCopyALPNProtocols(c->ssl, &selected)) {
        CFIndex n = selected ? CFArrayGetCount(selected) : 0;
        if (!n) { *state = c->alpn_enabled && c->alpn_offered ? 2 : 0; result = RES_OK; }
        else if (n == 1) {
            CFStringRef protocol = CFArrayGetValueAtIndex(selected, 0); CFIndex used = 0, len = CFStringGetLength(protocol);
            CFIndex converted = CFStringGetBytes(protocol, CFRangeMake(0, len), kCFStringEncodingISOLatin1, 0, false, NULL, 0, &used);
            if (converted == len && used > 0 && used <= 255) {
                *outlen = (unsigned)used; *state = 1;
                if ((unsigned)used <= cap && out) { CFStringGetBytes(protocol, CFRangeMake(0, len), kCFStringEncodingISOLatin1, 0, false, out, cap, NULL); result = RES_OK; }
            }
        }
    }
    if (selected) CFRelease(selected); tls_put(c); return result;
}
