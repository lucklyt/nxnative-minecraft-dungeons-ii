#pragma once
#include <stdint.h>
// nn::ssl Result values checked by this SDK's callers (main+0x41d28a4,
// +0x71887dc): nonblocking retry, timeout, certificate verification failure.
#define DM_TLS_WOULD_BLOCK 0x1987bu
#define DM_TLS_TIMEOUT 0x19a7bu
#define DM_TLS_VERIFY_FAILED 0xda7bu
uint32_t dm_tls_context_create(void *, int);
void dm_tls_context_destroy(void *);
uint32_t dm_tls_import_ca(void *, uint64_t *, const void *, unsigned, int);
uint32_t dm_tls_remove_ca(void *, uint64_t);
uint32_t dm_tls_create(void *, void *);
uint32_t dm_tls_destroy(void *);
uint32_t dm_tls_socket(void *, int);
uint32_t dm_tls_hostname(void *, const char *, unsigned);
uint32_t dm_tls_mode(void *, int);
uint32_t dm_tls_option(void *, int, int);
uint32_t dm_tls_verify_option(void *, unsigned);
uint32_t dm_tls_session_cache(void *, int);
uint32_t dm_tls_handshake(void *);
int dm_tls_read(void *, void *, unsigned);
int dm_tls_write(void *, const void *, unsigned);
uint32_t dm_tls_peek(void *, void *, int *, unsigned);
int dm_tls_pending(void *);
uint32_t dm_tls_poll(void *, unsigned *, const unsigned *, unsigned);
uint32_t dm_tls_error(void *, uint32_t *, int);

uint32_t dm_tls_connection_id(void *, uint64_t *);
uint32_t dm_tls_cert_buffer(void *, void *, unsigned);
uint32_t dm_tls_handshake_certs(void *, unsigned *, unsigned *);
// Detail output: {u32 size,u32 pad,u64 ptr}. Optional input must be NULL
// or the registered buffer; metadata is never read from an unbounded pointer.
uint32_t dm_tls_cert_detail(void *, void *, const void *, unsigned);
// RFC7301 length-prefixed list, printable ASCII protocol names supported by
// SecureTransport. Get returns the selected raw protocol bytes, without NUL.
uint32_t dm_tls_alpn_set(void *, const unsigned char *, unsigned);
uint32_t dm_tls_alpn_get(void *, int *, unsigned char *, unsigned *, unsigned);
