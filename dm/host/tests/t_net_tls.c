// Integration test: a test-runner supplied local TLS server and its explicit CA.
// Trust rejection and hostname rejection run before the successful HTTPS exchange.
#include "../net_transport.h"
#include "../net_tls.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>

uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; }
void dm_thread_native_wait(int entering) { (void)entering; }

static int connect_local(int port) {
    int fd = dm_net_socket(2, 1, 6); assert(fd >= 0);
    struct sockaddr_in a = {.sin_len = sizeof a, .sin_family = 2, .sin_port = htons((uint16_t)port)};
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(!dm_net_connect(fd, &a, sizeof a)); return fd;
}
static void init_connection(uint64_t *c, uint64_t *ctx, int fd, const char *host) {
    assert(!dm_tls_create(c, ctx) && *c);
    assert(!dm_tls_socket(c, fd)); assert(!dm_tls_hostname(c, host, (unsigned)strlen(host) + 1));
    assert(!dm_tls_verify_option(c, 7)); assert(dm_tls_verify_option(c, 0));
    uint64_t id = 0; assert(!dm_tls_connection_id(c, &id) && id == *c);
    assert(dm_tls_connection_id(c, NULL)); assert(dm_tls_connection_id(NULL, &id) && id == 0);
    assert(dm_tls_option(c, 2, 1)); // Never permit skip-default-verification.
}
int main(int argc, char **argv) {
    assert(argc == 4); int port = atoi(argv[1]);
    uint64_t ctx = 0, c = 0;
    unsetenv("DM_NETWORK"); assert(dm_tls_context_create(&ctx, 1));
    setenv("DM_NETWORK", "1", 1); assert(!dm_tls_context_create(&ctx, 1));
    int fd = connect_local(port); init_connection(&c, &ctx, fd, "localhost");
    assert(dm_tls_handshake(&c) == DM_TLS_VERIFY_FAILED);
    uint32_t err = 0; assert(!dm_tls_error(&c, &err, 1) && err == DM_TLS_VERIFY_FAILED);
    assert(!dm_tls_destroy(&c) && !c); assert(dm_net_native_fd(fd) >= 0); assert(!dm_net_close(fd));
    FILE *f = fopen(argv[2], "rb"); assert(f); assert(!fseek(f, 0, SEEK_END)); long n = ftell(f); rewind(f);
    char *pem = malloc((size_t)n); assert(pem && fread(pem, 1, (size_t)n, f) == (size_t)n); fclose(f);
    uint64_t ca = 0; assert(!dm_tls_import_ca(&ctx, &ca, pem, (unsigned)n, 1) && ca); free(pem);
    fd = connect_local(port); init_connection(&c, &ctx, fd, "wrong.invalid");
    assert(dm_tls_handshake(&c) == DM_TLS_VERIFY_FAILED);
    assert(!dm_tls_destroy(&c)); assert(!dm_net_close(fd));
    fd = connect_local(port); init_connection(&c, &ctx, fd, "localhost");
    const unsigned char protocols[] = {2, 'h', '2', 8, 'h', 't', 't', 'p', '/', '1', '.', '1'};
    const unsigned char empty[] = {0}, truncated[] = {3, 'h', '2'}, binary[] = {1, 0xff};
    assert(dm_tls_alpn_set(&c, NULL, 1) && dm_tls_alpn_set(&c, protocols, 0));
    assert(dm_tls_alpn_set(&c, empty, sizeof empty) && dm_tls_alpn_set(&c, truncated, sizeof truncated));
    assert(dm_tls_alpn_set(&c, binary, sizeof binary));
    assert(!dm_tls_option(&c, 3, 1) && !dm_tls_alpn_set(&c, protocols, sizeof protocols));
    int state = -1; unsigned alpn_n = 99; unsigned char alpn[16] = {0};
    assert(dm_tls_alpn_get(&c, &state, alpn, &alpn_n, sizeof alpn));
    unsigned cert_size = 0, cert_count = 0; unsigned char tiny = 0x5a;
    assert(dm_tls_cert_buffer(&c, NULL, 1) && dm_tls_cert_buffer(&c, &tiny, 0));
    assert(!dm_tls_cert_buffer(&c, &tiny, 1));
    assert(!dm_tls_mode(&c, 2)); uint32_t result; uint64_t deadline = now_ns() + 10000000000ull;
    while ((result = dm_tls_handshake(&c)) == DM_TLS_WOULD_BLOCK) {
        assert(now_ns() < deadline); unsigned ready = 0, events = 3;
        uint32_t r = dm_tls_poll(&c, &ready, &events, 10); assert(!r || r == DM_TLS_TIMEOUT);
        struct timespec pause = {0, 1000000}; nanosleep(&pause, NULL);
    }
    if (result) fprintf(stderr, "trusted handshake result=%#x\n", result);
    assert(!result); assert(!dm_tls_mode(&c, 1));
    assert(!dm_tls_alpn_get(&c, &state, alpn, &alpn_n, sizeof alpn));
    assert(state == 1 && alpn_n == 8 && !memcmp(alpn, "http/1.1", 8));
    assert(dm_tls_alpn_set(&c, protocols, sizeof protocols)); // Configuration is immutable after handshake.
    memset(alpn, 0x5a, sizeof alpn);
    assert(dm_tls_alpn_get(&c, &state, alpn, &alpn_n, 7) && alpn_n == 8 && alpn[0] == 0x5a);
    assert(dm_tls_alpn_get(&c, NULL, alpn, &alpn_n, sizeof alpn));
    assert(dm_tls_alpn_get(&c, &state, NULL, &alpn_n, sizeof alpn));
    assert(dm_tls_handshake_certs(&c, NULL, &cert_count));
    assert(dm_tls_handshake_certs(&c, &cert_size, &cert_count) && cert_size > 1 && cert_count == 1 && tiny == 0x5a);
    unsigned char certs[32768]; memset(certs, 0x5a, sizeof certs);
    assert(!dm_tls_cert_buffer(&c, certs, sizeof certs));
    assert(!dm_tls_handshake_certs(&c, &cert_size, &cert_count) && cert_count == 1);
    struct { uint32_t size, pad; uint64_t pointer; } detail;
    assert(!dm_tls_cert_detail(&c, &detail, NULL, 0) && detail.size == cert_size && detail.pointer == (uint64_t)(uintptr_t)certs);
    FILE *der_file = fopen(argv[3], "rb"); assert(der_file);
    unsigned char real_der[16384]; size_t der_n = fread(real_der, 1, sizeof real_der, der_file); fclose(der_file);
    assert(der_n == cert_size && !memcmp(certs, real_der, der_n));
    assert(dm_tls_cert_detail(&c, &detail, NULL, 1) && !detail.pointer && !detail.size);
    assert(dm_tls_cert_detail(&c, &detail, &tiny, 0));
    assert(dm_tls_cert_detail(&c, NULL, NULL, 0));
    assert(!dm_tls_option(&c, 1, 1));
    assert(!dm_tls_handshake_certs(&c, &cert_size, &cert_count) && cert_count >= 1);
    uint64_t magic; memcpy(&magic, certs, 8); uint32_t header_count; memcpy(&header_count, certs + 8, 4);
    assert(magic == 0x4e4d684374726543ull && header_count == cert_count);
    for (unsigned i = 0; i < cert_count; i++) {
        uint32_t entry[2]; memcpy(entry, certs + 16 + i * 8, 8);
        assert(entry[1] >= 16 + cert_count * 8 && entry[1] <= cert_size && entry[0] <= cert_size - entry[1]);
        assert(!dm_tls_cert_detail(&c, &detail, certs, i) && detail.size == entry[0] && detail.pointer == (uint64_t)(uintptr_t)(certs + entry[1]));
        if (!i) assert(entry[0] == der_n && !memcmp(certs + entry[1], real_der, der_n));
    }
    const char request[] = "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n";
    assert(dm_tls_write(&c, request, sizeof request - 1) == sizeof request - 1);
    char peek[4]; int got = 0; assert(!dm_tls_peek(&c, peek, &got, sizeof peek) && got == 4 && !memcmp(peek, "HTTP", 4));
    assert(dm_tls_pending(&c) >= 4);
    char response[4096]; int used = 0, k;
    while ((k = dm_tls_read(&c, response + used, sizeof response - 1 - (unsigned)used)) > 0) { used += k; assert((unsigned)used < sizeof response - 1); }
    assert(k == 0); response[used] = 0; assert(strstr(response, "200 OK") && strstr(response, "native-tls-ok"));
    assert(!dm_tls_destroy(&c)); assert(!dm_net_close(fd)); assert(dm_tls_destroy(&c));
    // A save-sized PUT must survive short encrypted socket writes. The server
    // checks every payload byte; successful small GETs do not cover this path.
    fd = connect_local(port); init_connection(&c, &ctx, fd, "localhost");
    assert(!dm_tls_handshake(&c));
    int send_buffer = 1024;
    assert(!setsockopt(dm_net_native_fd(fd), SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof send_buffer));
    const char put[] = "PUT /save HTTP/1.0\r\nHost: localhost\r\nContent-Length: 1048576\r\n\r\n";
    assert(dm_tls_write(&c, put, sizeof put - 1) == sizeof put - 1);
    unsigned char *payload = malloc(1048576); assert(payload);
    for (unsigned i = 0; i < 1048576; i++) payload[i] = (unsigned char)i;
    assert(!dm_tls_mode(&c, 2));
    unsigned sent = 0; deadline = now_ns() + 20000000000ull;
    while (sent < 1048576) {
        int wrote = dm_tls_write(&c, payload + sent, 1048576 - sent);
        if (wrote > 0) sent += (unsigned)wrote;
        else {
            uint32_t why = 0; assert(!dm_tls_error(&c, &why, 0) && why == DM_TLS_WOULD_BLOCK);
            unsigned ready = 0, events = 2;
            uint32_t r = dm_tls_poll(&c, &ready, &events, 10); assert(!r || r == DM_TLS_TIMEOUT);
        }
        assert(now_ns() < deadline);
    }
    free(payload); assert(!dm_tls_mode(&c, 1)); used = 0;
    while ((k = dm_tls_read(&c, response + used, sizeof response - 1 - (unsigned)used)) > 0) { used += k; assert((unsigned)used < sizeof response - 1); }
    assert(k == 0); response[used] = 0; assert(strstr(response, "201 Created"));
    assert(!dm_tls_destroy(&c)); assert(!dm_net_close(fd));
    assert(!dm_tls_remove_ca(&ctx, ca)); assert(dm_tls_remove_ca(&ctx, ca)); dm_tls_context_destroy(&ctx);
    puts("TLS: untrusted certificate rejected; wrong hostname rejected; trusted nonblocking handshake, real certificate buffers/details, ALPN and HTTPS read/write/peek passed");
    return 0;
}
