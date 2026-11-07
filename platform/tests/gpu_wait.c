// A deliberately pending end-present fence exercises cooperative match work
// without relying on the GPU being slow. Every successful fake wait delegates
// to the real fence; rendering, readback and resource reuse stay real.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gpu_vk.h"
#include "tide/platform.h"
#include "tide/time.h"

typedef enum { ORDINARY, DEADLINES, READY, DISABLED, WITHDRAW, LOST, HEADLESS } wait_mode;

static tide_draw_list list;
static wait_mode mode;
static int failures, frames, waits, updates, total_waits;
static bool in_frame, headless;
static double last_elapsed;
static uint32_t epoch;

static void expect(const bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) failures++;
}

static double update(void *user, const double elapsed)
{
    expect(user == &list && !in_frame, "match work runs after the frame callback, with its user");
    expect(isfinite(elapsed) && elapsed >= last_elapsed, "elapsed time advances from the render frame's start");
    last_elapsed = elapsed;
    const int call = updates++;
    if (mode == WITHDRAW) {
        tide_platform_update_while_waiting(NULL, NULL);
        return 0.0;
    }
    if (call == 0) {
        // The deadline is relative to callback entry. Work done here must be
        // subtracted, so this 12 ms deadline leaves at most 8 ms to wait.
        const uint64_t until = tide_time_now_ns() + UINT64_C(4000000);
        while (tide_time_now_ns() < until) tide_yield();
        return 0.012;
    }
    if (call == 1) return 0.0;
    if (call == 2) return NAN;
    if (call == 3) return INFINITY;
    return -1.0;
}

static VkResult wait_hook(const uint64_t timeout, VkResult (*wait)(uint64_t timeout))
{
    total_waits++;
    expect(!in_frame, "only end-present waits reach the cooperative wait hook");
    const int call = waits++;
    switch (mode) {
    case DEADLINES:
        if (call == 0) expect(timeout == 0, "a ready fence is polled before any match work");
        else if (call == 1) expect(timeout >= UINT64_C(1000000) && timeout <= UINT64_C(8000000),
                                   "the wait subtracts callback work from its deadline");
        else expect(timeout == (call == 4 ? UINT64_C(100000000) : UINT64_C(1000000)),
                    "overdue and invalid deadlines stay within bounded sleeping waits");
        return call < 5 ? VK_TIMEOUT : wait(UINT64_MAX);
    case READY:
        expect(timeout == 0, "a ready frame needs no timed wait");
        return wait(UINT64_MAX);
    case DISABLED:
    case HEADLESS:
        if (timeout == 0) return VK_TIMEOUT;
        expect(timeout == UINT64_MAX, "without an active updater the fence waits normally");
        return wait(timeout);
    case WITHDRAW:
        if (call < 2) return VK_TIMEOUT;
        expect(timeout == UINT64_MAX, "withdrawing the callback restores an ordinary wait");
        return wait(timeout);
    case LOST:
        return VK_ERROR_DEVICE_LOST;
    case ORDINARY:
        return wait(timeout);
    }
    return VK_ERROR_UNKNOWN;
}

static void begin_wait(const wait_mode next, const bool enabled)
{
    mode = next;
    waits = updates = 0;
    last_elapsed = 0.0;
    tide_platform_update_while_waiting(enabled ? update : NULL, &list);
}

static void draw(void)
{
    tide_draw_reset(&list);
    tide_draw_set_screen(&list);
    tide_draw_clear(&list, TIDE_COLOR_BLUE);
    tide_draw_rect(&list, tide_f2(32, 32), tide_f2(16, 16), frames % 2 ? TIDE_COLOR_RED : TIDE_COLOR_GREEN);
    tide_platform_draw(&list);
}

static void read_pixels(void)
{
    const int before = total_waits;
    const tide_float2 point = {32, 32};
    uint32_t pixel = 0;
    tide_platform_read_pixels(&list, &point, 1, &pixel);
    expect(total_waits == before, "readback and its flush do not run the cooperative hook");
    expect((pixel >> 8) == (frames % 2 ? 0xFF0000u : 0x00FF00u), "frame resources still draw and read back the right pixels");
}

static int frame(void *user, const float seconds)
{
    (void)user, (void)seconds;
    in_frame = true;
    frames++;
    if (headless) {
        draw();
        if (frames == 8) {
            expect(total_waits > 0 && updates == 0, "headless frames never run the installed updater");
            read_pixels();
        }
    } else {
        switch (frames) {
        case 2: begin_wait(DEADLINES, true); break;
        case 3:
            expect(waits == 6 && updates == 5, "a pending frame services each update until its fence completes");
            begin_wait(READY, true);
            break;
        case 4:
            expect(waits == 1 && updates == 0, "a ready frame invokes no match callback");
            begin_wait(DISABLED, false);
            break;
        case 5:
            expect(waits == 2 && updates == 0, "an absent callback retains ordinary blocking completion");
            begin_wait(WITHDRAW, true);
            break;
        case 6:
            expect(waits == 3 && updates == 1, "a callback can withdraw itself while waiting");
            begin_wait(LOST, true);
            epoch = tide_gpu_vulkan.epoch();
            break;
        case 7:
            expect(waits == 1 && updates == 0, "a failed fence does not call match work");
            begin_wait(ORDINARY, false);
            break;
        default: break;
        }
        const int before = total_waits;
        draw();
        expect(total_waits == before, "drawing and acquisition do not run the cooperative hook");
        if (frames == 7) expect(tide_gpu_vulkan.epoch() != epoch, "a failed fence recreates the device before the next draw");
        if (frames == 7 || frames == 10) read_pixels();
    }
    in_frame = false;
    if (frames == (headless ? 8 : 10)) {
        tide_gpu_vk_wait_for_test(NULL);
        tide_platform_update_while_waiting(NULL, NULL);
        tide_draw_free(&list);
        return failures ? 1 : 0;
    }
    return TIDE_KEEP_RUNNING;
}

int main(const int argc, char **argv)
{
    headless = argc > 1 && strcmp(argv[1], "--headless") == 0;
    if (headless) tide_platform_open_headless(&(tide_headless_desc){.width = 64, .height = 64, .frames = 8});
    else tide_platform_open(&(tide_window_desc){.title = "GPU waiting", .width = 64, .height = 64, .hidden = true});
    tide_gpu_vk_wait_for_test(wait_hook);
    begin_wait(headless ? HEADLESS : ORDINARY, headless);
    tide_platform_run(frame, NULL);
}
