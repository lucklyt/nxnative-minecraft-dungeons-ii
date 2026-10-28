// The SDK capacity query and actual flip completion must drive swapchain fences.
#include "../../gfx/hle_vi.c"
#include <assert.h>

uint64_t dm_nvn_frame_count(void) { return 0; }

int main(void) {
    _Static_assert(sizeof(vi_frame_info) == 0x28, "SDK FrameInfo ABI");
    vi_frame_info out[10]; memset(out, 0xa5, sizeof out);
    assert(h_vi_FrameHist(NULL, 0, NULL) == 8);
    assert(h_vi_FrameHist(NULL, 8, NULL) == 8);
    assert(h_vi_FrameHist(out, 0, NULL) == 8 && out[0].state == 0xa5a5a5a5a5a5a5a5ull);
    assert(h_vi_FrameHist(out, -1, NULL) == 0);
    assert(h_vi_FrameHist(out, 8, NULL) == 0);
    dm_vi_frame_queued(1, 100);
    assert(h_vi_FrameHist(out, 8, NULL) == 0); // Enqueue and GPU completion are not flips.
    dm_vi_frame_done(1, 120);
    assert(h_vi_FrameHist(out, 8, NULL) == 1);
    assert(out[0].state == 2 && out[0].number == 1 && out[0].queued == vi_tick(100));
    assert(out[0].displayed_min == vi_tick(120) && out[0].displayed_max == vi_tick(120));
    assert(vi_tick(1000000000) == 19200000); // SDK timestamps are ticks, not nanoseconds.
    dm_vi_frame_queued(2, 150); dm_vi_frame_done(2, 0);
    assert(h_vi_FrameHist(out, 1, NULL) == 1);
    assert(out[0].state == 1 && out[0].number == 2 && !out[0].displayed_min && !out[0].displayed_max);
    for (uint64_t i = 3; i <= 10; i++) { dm_vi_frame_queued(i, i * 100); dm_vi_frame_done(i, i * 100 + 20); }
    dm_vi_frame_done(1, 99999); // A late callback cannot overwrite a reused ring slot.
    assert(h_vi_FrameHist(out, 10, NULL) == 8);
    for (int i = 0; i < 8; i++) {
        assert(out[i].number == (uint64_t)(10 - i) && out[i].state == 2);
        assert(out[i].displayed_min == vi_tick(out[i].number * 100 + 20));
    }
    puts("vi frame history capacity, completion, drops and ring rollover: passed");
    return 0;
}
