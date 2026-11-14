#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tide/platform.h"
#include "window.h"
#include "gpu.h"
#ifdef __wasm__
#include "tide_web.h"
#endif
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;
static int failures;
static int frame(void *context, float seconds)
{
    (void)context; (void)seconds;
#ifdef __wasm__
    static int lost;
    if (lost == 1 && !tide_web_eval("Tide.gpuDevices() > 1")) return TIDE_KEEP_RUNNING;
    if (lost == 1) { tide_world_init(&world, 1.0f / 60.0f); tide_local_init(&local); lost = 2; }
#endif
    const tide_float2 center = {32, 32};
    int bad = 0;
    for (int sample_at = 0; sample_at < 2; sample_at++) {
      local.DepthReads.samples = sample_at ? 4 : 1;
      for (int test = 0; test < 7; test++) {
        local.DepthReads.test = test;
        tide_draw_reset(&draw);
        tide_gui_begin(&gui, &devices, tide_f2(64, 64), tide_platform_measure_text);
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
        tide_gui_end(&gui, &draw);
        uint32_t pixel;
        tide_platform_read_pixels_as(&draw, 64, 64, 1, &center, 1, &pixel);
        bool fallback;
#ifdef __wasm__
        fallback = tide_web_eval("Tide.depthFallback === true");
#else
        const char *force = getenv("TIDE_TEST_DEPTH_FALLBACK");
        fallback = force && *force && strcmp(force, "0") != 0;
#endif
        const uint32_t storage = tide_window_gpu()->depth_bytes();
        bad += storage != 4u && storage != 8u;
        bad += fallback && storage != 4u;
        bad += tide_render_target_samples(local.DepthReads.target.id) != (uint32_t)local.DepthReads.samples;
        printf("DEPTH_READ,%d,%d,%08X\n", local.DepthReads.samples, test, (unsigned)pixel);
        bad += pixel != 0x00ff00ffu;
      }
      tide_render_target_dispose(local.DepthReads.target);
    }
    tide_draw_free(&draw); tide_local_free(&local); tide_world_free(&world);
    printf("depth reads regression complete: %d failures\n", bad);
    failures = bad;
#ifdef __wasm__
    if (!bad && !lost) { tide_web_eval("Tide.loseDevice()"); lost = 1; return TIDE_KEEP_RUNNING; }
#endif
    return bad;
}
int main(int argc, char **argv)
{
    bool bounded = false;
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--frames=1")) bounded = true;
    if (!bounded) return 2;
    tide_world_init(&world, 1.0f / 60.0f); tide_local_init(&local);
    tide_platform_open(&(tide_window_desc){.title = "depth reads", .width = 64, .height = 64, .hidden = true});
    tide_platform_run(frame, NULL);
    return failures;
}
