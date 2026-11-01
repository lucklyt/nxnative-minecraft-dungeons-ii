#include "hle.h"
// Exact-build, read-only authentication observations. The profile inserts the
// signed gates; DM_AUTH_TRACE enables only stage/error diagnostics. In particular
// neither device codes nor account names, passwords or service tokens are logged.
#include <mach/mach.h>
#include <stdatomic.h>
static int auth_read(uint64_t a, void *out, size_t n) {
    vm_size_t got = 0;
    return a >= 0x1000 && vm_read_overwrite(mach_task_self(), (vm_address_t)a, n,
           (vm_address_t)out, &got) == KERN_SUCCESS && got == n;
}
#define AUTH_STAGE(fn, text) static void fn(const uint64_t *regs) { \
    (void)regs; static _Atomic unsigned count; \
    if (atomic_fetch_add(&count, 1) < 16) LOG("auth: " text); }
static void auth_primary(const uint64_t *regs) {
    unsigned char mode = 255;
    if (auth_read(regs[1] + 0x188, &mode, sizeof mode))
        LOG("auth: Microsoft primary flow started (context mode=%u)", (unsigned)mode);
    else LOG("auth: Microsoft primary flow started (context mode unavailable)");
}
AUTH_STAGE(auth_playfab, "PlayFab audience provider requested")
AUTH_STAGE(auth_minecraft, "Minecraft audience provider requested")
AUTH_STAGE(auth_game_response, "original Minecraft login response callback")
AUTH_STAGE(auth_cloud_request, "original cloud-list request")
#undef AUTH_STAGE
static void auth_cloud_response(const uint64_t *regs) {
    uint32_t count = 0;
    if (auth_read(regs[19] + 8, &count, sizeof count) && count <= 4096)
        LOG("auth: original cloud-list character delivery (characters=%u)", count);
    else LOG("auth: original cloud-list character delivery (count unavailable)");
}
static void auth_xal_begin(const uint64_t *regs) { LOG("auth: XAL interactive start HRESULT=%#x", (unsigned)regs[0]); }
static void auth_xal_result(const uint64_t *regs) { LOG("auth: XAL interactive result HRESULT=%#x", (unsigned)regs[0]); }
static void auth_failure(const uint64_t *regs) { LOG("auth: original login failure enum=%u", (unsigned)regs[24]); }
static void auth_device_code(const uint64_t *regs) {
    // This build's parser returns a libc++ string in fp-0x20 before moving it
    // into its user_code field. Observe only a bounded validated character count.
    unsigned char raw[24], chars[16]; uint64_t n, ptr, a = regs[29] - 0x20;
    if (!auth_read(a, raw, sizeof raw)) { LOG("auth: device-code parser result unreadable"); return; }
    n = raw[0] >> 1; ptr = a + 1;
    if (raw[0] & 1) { memcpy(&n, raw + 8, 8); memcpy(&ptr, raw + 16, 8); }
    if (n < 6 || n > sizeof chars || !auth_read(ptr, chars, (size_t)n)) { LOG("auth: device-code parser result unavailable"); return; }
    for (unsigned i = 0; i < n; i++) if (!((chars[i] >= 'A' && chars[i] <= 'Z') ||
        (chars[i] >= '0' && chars[i] <= '9') || chars[i] == '-')) { LOG("auth: device-code parser result rejected"); return; }
    LOG("auth: Microsoft device code ready (%u characters)", (unsigned)n);
}
void dm_auth_observe_install(void) {
    const char *e = getenv("DM_AUTH_TRACE"); if (!e || strcmp(e, "1")) return;
    int match = 0;
    for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main") &&
        !strcmp(g_mod[i].build_id, "c6b32f4d3d4243ff190b082b4f6aea18")) match = 1;
    if (!match) return;
    static const struct { int id; void (*fn)(const uint64_t *); } rows[] = {
        {OBS_AUTH_PRIMARY, auth_primary}, {OBS_AUTH_XAL_BEGIN, auth_xal_begin},
        {OBS_AUTH_DEVICE_CODE, auth_device_code}, {OBS_AUTH_XAL_RESULT, auth_xal_result},
        {OBS_AUTH_PLAYFAB, auth_playfab}, {OBS_AUTH_MINECRAFT, auth_minecraft},
        {OBS_AUTH_GAME_RESPONSE, auth_game_response}, {OBS_AUTH_FAILURE, auth_failure},
        {OBS_AUTH_CLOUD_REQUEST, auth_cloud_request}, {OBS_AUTH_CLOUD_RESPONSE, auth_cloud_response},
    };
    unsigned attached = 0;
    for (unsigned i = 0; i < sizeof rows / sizeof *rows; i++) if (gate_obs_present(rows[i].id)) {
        gate_obs_set(rows[i].id, rows[i].fn); attached++;
    }
    LOG("auth: stage diagnostics attached (%u gates; credentials omitted)", attached);
}
