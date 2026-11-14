#include <stdio.h>
#include <string.h>
#include "game.h"
#include "tide/platform.h"
#ifdef __wasm__
#include "tide_web.h"
#endif

static tide_world world;
static tide_local local;
static tide_draw_list list;
static tide_gui gui;
static tide_devices devices;
static int failures;

static void check(const uint32_t pixel, const unsigned r, const unsigned g, const unsigned b, const char *what)
{
    const unsigned rgb[3] = {pixel >> 24 & 255u, pixel >> 16 & 255u, pixel >> 8 & 255u};
    const bool ok = rgb[0] == r && rgb[1] == g && rgb[2] == b;
    printf("%s: %s (%u,%u,%u)\n", ok ? "ok" : "FAIL", what, rgb[0], rgb[1], rgb[2]);
    if (!ok) failures++;
}

static void blended(const uint32_t pixel, const unsigned r, const unsigned g, const unsigned b, const char *what)
{
    // Filtering is performed at the backend's precision; the final UNORM8 conversion can round half a step either way.
    const unsigned rgb[3] = {pixel >> 24 & 255u, pixel >> 16 & 255u, pixel >> 8 & 255u};
    const unsigned wanted[3] = {r, g, b};
    bool ok = true;
    for (unsigned k = 0; k < 3; k++) if (rgb[k] + 1 < wanted[k] || rgb[k] > wanted[k] + 1) ok = false;
    printf("%s: %s (%u,%u,%u)\n", ok ? "ok" : "FAIL", what, rgb[0], rgb[1], rgb[2]);
    if (!ok) failures++;
}

static int frame(void *user, const float seconds)
{
    (void)user; (void)seconds;
    static int stage, warm;
    static int64_t revision;
#ifdef __wasm__
    if (stage == 1) {
        if (!tide_web_eval("Tide.gpuDevices() > 1")) return TIDE_KEEP_RUNNING;
        stage = 2; warm = 0;
    }
#endif
    tide_draw_reset(&list);
    tide_gui_begin(&gui, &devices, tide_f2(24, 24), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    const tide_float2 places[9] = {{4,4},{12,4},{20,4},{4,12},{12,12},{20,12},{4,20},{12,20},{20,20}};
    uint32_t pixels[9];
    tide_platform_read_pixels_as(&list, 24, 24, 1, places, 9, pixels);
    if (++warm < 3) return TIDE_KEEP_RUNNING;
    check(pixels[0], 0, 0, 0, "a draw before the image write sees the earlier black image");
    check(pixels[1], 0, 0, 255, "a draw between writes sees the exact first HDR color with alpha undone");
    check(pixels[2], 0, 255, 0, "a draw after both writes sees the last color; out-of-range stores do nothing");
    blended(pixels[3], 128, 0, 0, "a volume blends between cells on x");
    blended(pixels[4], 0, 128, 0, "a volume blends between cells on y");
    blended(pixels[5], 0, 0, 128, "a volume blends between cells on z");
    blended(pixels[6], local.Images.frames % 2 ? 159 : 128, 128, 128, "a changed Grid3 of colors converts to a blended volume");
    check(pixels[7], 255, 0, 0, "a Grid3 of floats converts to a blended numeric volume");
    check(pixels[8], 255, 0, 255, "an exact volume cell read survives out-of-range stores");
    if (local.Images.revision <= 0 || (stage == 2 && local.Images.revision == revision)) failures++;
#ifdef __wasm__
    if (stage == 0 && !failures) {
        revision = local.Images.revision;
        tide_web_eval("Tide.loseDevice()");
        stage = 1;
        return TIDE_KEEP_RUNNING;
    }
#endif
    (void)revision;
    return failures;
}

int main(void)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    tide_platform_open(&(tide_window_desc){.title = "images", .width = 24, .height = 24, .hidden = true});
    printf("renderer: %s\n", tide_platform_renderer());
    tide_platform_run(frame, NULL);
}
