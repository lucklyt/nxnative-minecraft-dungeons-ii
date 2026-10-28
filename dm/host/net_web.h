// Horizon Web applet TLVs. URLs and callback results may contain credentials: never log them.
#pragma once
#include <stddef.h>
#include <stdint.h>
#define DM_WEB_ARG_SIZE 8192
#define DM_WEB_RETURN_SIZE 8192
#define DM_WEB_URL_SIZE 3072
typedef struct { char initial[DM_WEB_URL_SIZE]; char callback[1024]; } dm_web_args;
int dm_web_parse(dm_web_args *out, const void *arg);
void dm_web_result(void *out, unsigned reason, const char *url);
// Synchronous on the guest thread; UI is dispatched to the main thread.
int dm_web_present(const dm_web_args *arg, unsigned *reason, char *url, size_t cap);
