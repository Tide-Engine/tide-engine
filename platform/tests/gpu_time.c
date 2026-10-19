// The real renderer's timer: optional, asynchronous, unchanged drawing,
// discarded on closing, and usable again on reopening. The mock tests cover
// delayed completions and unsupported devices without depending on a GPU.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "tide/platform.h"

#ifdef __wasm__
#include "tide_web.h"
#else
#include "../src/gl.h"
#include "../src/gpu_vk.h"
#endif

// How many frames a timer that's supported is given to hand back its first
// sample: results come back when the device answers, not after a count of
// frames, so each round ends at the first sample and only fails at this many.
enum { ROUND = 60, PATIENCE = 3600 };

static tide_draw_list list;
static int failures, frames, samples, rounds, since;
static bool supported;

static void check(const bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) failures++;
}

static bool has_timer(void)
{
#ifdef __wasm__
    return strcmp(tide_platform_renderer(), "WebGPU") == 0 && tide_web_eval("Tide.gpuTimingSupported()");
#else
    if (strcmp(tide_platform_renderer(), "Vulkan") == 0) return tide_gpu_vk_timestamps();
    if (!tide_gl.GetQueryiv || !tide_gl.GetQueryObjectui64v) return false;
    GLint bits = 0;
    glGetQueryiv(GL_TIME_ELAPSED, GL_QUERY_COUNTER_BITS, &bits);
    return bits > 0;
#endif
}

static int frame(void *user, const float seconds)
{
    (void)user, (void)seconds;
    frames++;
    if (frames == 1) {
        check(tide_platform_gpu_ms() < 0.0, "disabled timers have no samples");
        supported = has_timer();
        printf("renderer: %s; GPU timing %s\n", tide_platform_renderer(), supported ? "supported" : "unavailable");
        tide_draw_set_screen(&list);
        tide_draw_clear(&list, TIDE_COLOR_BLUE);
        tide_draw_rect(&list, tide_f2(32, 32), tide_f2(16, 16), TIDE_COLOR_RED);
        tide_platform_gpu_timing(true);
    }
    tide_platform_draw(&list);
    const double ms = tide_platform_gpu_ms();
    if (ms >= 0.0) {
        check(isfinite(ms), "a completed GPU time is finite and nonnegative");
        samples++;
    }
    if (frames == 10) {
        const tide_float2 point = {32, 32};
        uint32_t color = 0;
        tide_platform_read_pixels(&list, &point, 1, &color);
        check((color >> 8) == 0xFF0000u, "timing leaves the rendered pixels unchanged");
    }
    since++;
    if (since >= ROUND && (!supported || samples > 0 || since >= PATIENCE)) {
        since = 0;
        rounds++;
        check(!supported || samples > 0, "a supported timer produces asynchronous samples");
        check(supported || samples == 0, "an unsupported timer produces no invented sample");
        tide_platform_gpu_timing(false);
        check(tide_platform_gpu_ms() < 0.0, "closing discards pending samples");
        if (rounds == 2) {
            tide_draw_free(&list);
            return failures ? 1 : 0;
        }
        samples = 0;
        tide_platform_gpu_timing(true);
        check(tide_platform_gpu_ms() < 0.0, "reopening starts with no old samples");
    }
    return TIDE_KEEP_RUNNING;
}

int main(void)
{
    tide_platform_open(&(tide_window_desc){.title = "GPU timing", .width = 64, .height = 64, .hidden = true});
    tide_platform_run(frame, NULL);
}
