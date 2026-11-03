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

static tide_draw_list list, overlay, offscreen;
static tide_render_target target;
static int pass_samples, seen_passes;
static bool empty_window_seen;
static uint32_t window_width, window_height;
static int failures, frames, samples, rounds, since;
static bool supported;

static void check(const bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) failures++;
}

static void collect_pass(void *user, const tide_gpu_pass_sample *sample)
{
    (void)user;
    if (!(sample->ms >= 0.0)) return;
    pass_samples++;
    bool valid = isfinite(sample->ms) && sample->frame > 0 && sample->pass >= 1 && sample->pass <= 2
        && sample->shaders;
    const bool empty_window = sample->pass == 2 && sample->target == 0 && sample->draw_calls == 0;
    if (empty_window) {
        empty_window_seen = true;
        valid = valid && sample->vertices == 0 && sample->shaders[0] == 0;
    } else {
        valid = valid && sample->draw_calls == 2 && sample->vertices == 9
            && strstr(sample->shaders, "Builtin clear") && strstr(sample->shaders, "Builtin shapes");
    }
    if (sample->pass == 1) valid = valid && sample->target != 0 && sample->width == 32 && sample->height == 16;
    if (sample->pass == 2) valid = valid && sample->target == 0 && sample->width == window_width && sample->height == window_height;
    if (!valid && failures == 0) fprintf(stderr, "pass=%u target=%llu size=%ux%u calls=%llu vertices=%llu shaders=%s\n",
        (unsigned)sample->pass, (unsigned long long)sample->target, (unsigned)sample->width, (unsigned)sample->height,
        (unsigned long long)sample->draw_calls, (unsigned long long)sample->vertices, sample->shaders);
    if (!valid) check(false, "pass timings preserve target, order, dimensions, shader membership and submitted work");
    if (sample->pass <= 2) seen_passes |= 1 << sample->pass;
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
        const tide_float2 size = tide_platform_screen_size(), scale = tide_platform_screen_scale();
        window_width = (uint32_t)(size.x * scale.x + 0.5f);
        window_height = (uint32_t)(size.y * scale.y + 0.5f);
        printf("renderer: %s; GPU timing %s\n", tide_platform_renderer(), supported ? "supported" : "unavailable");
        target = tide_render_target_make_scaled(32, 16, 1);
        tide_draw_render_target(&list, target);
        tide_draw_set_screen(&list);
        tide_draw_clear(&list, TIDE_COLOR_BLUE);
        tide_draw_rect(&list, tide_f2(8, 8), tide_f2(4, 4), TIDE_COLOR_RED);
        tide_draw_render_target(&offscreen, target);
        tide_draw_set_screen(&offscreen);
        tide_draw_clear(&offscreen, TIDE_COLOR_BLUE);
        tide_draw_rect(&offscreen, tide_f2(8, 8), tide_f2(4, 4), TIDE_COLOR_RED);
        tide_draw_to(&list, 0, 0, 0);
        tide_draw_set_screen(&list);
        tide_draw_clear(&list, TIDE_COLOR_BLUE);
        tide_draw_rect(&list, tide_f2(32, 32), tide_f2(16, 16), TIDE_COLOR_RED);
        tide_platform_gpu_timing(true);
        tide_platform_gpu_profile(collect_pass, NULL);
        tide_draw_rect(&overlay, tide_f2(1, 1), tide_f2(1, 1), TIDE_COLOR_BLUE);
    }
    tide_platform_draw(since == 19 ? &offscreen : &list);
    tide_platform_draw_over(&overlay);
    tide_platform_gpu_profile_read();
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
    if (rounds == 0 && since == 30) {
#ifdef __wasm__
        if (strcmp(tide_platform_renderer(), "WebGPU") == 0) tide_web_eval("Tide.loseDevice()");
#else
        if (strcmp(tide_platform_renderer(), "Vulkan") == 0) tide_gpu_vk_lose();
#endif
    }
    if (since >= ROUND && (!supported || samples > 0 || since >= PATIENCE)) {
        since = 0;
        rounds++;
        check(!supported || samples > 0, "a supported timer produces asynchronous samples");
        check(supported || samples == 0, "an unsupported timer produces no invented sample");
        check(!supported || (pass_samples > 0 && seen_passes == 6), "supported timers return attributed passes, excluding overlays");
        check(!supported || empty_window_seen, "offscreen-only frames attribute the fallback clear to the window");
        tide_platform_gpu_profile(NULL, NULL);
        check(tide_platform_gpu_profile_pending() == 0, "closing frees pending pass identities");
        tide_platform_gpu_timing(false);
        check(tide_platform_gpu_ms() < 0.0, "closing discards pending samples");
        if (rounds == 2) {
            tide_draw_free(&list);
            tide_draw_free(&overlay);
            tide_draw_free(&offscreen);
            tide_render_target_dispose(target);
            return failures ? 1 : 0;
        }
        samples = pass_samples = seen_passes = 0;
        empty_window_seen = false;
        tide_platform_gpu_timing(true);
        tide_platform_gpu_profile(collect_pass, NULL);
        check(tide_platform_gpu_ms() < 0.0, "reopening starts with no old samples");
    }
    return TIDE_KEEP_RUNNING;
}

int main(void)
{
    tide_platform_open(&(tide_window_desc){.title = "GPU timing", .width = 64, .height = 64, .hidden = true});
    tide_platform_run(frame, NULL);
}
