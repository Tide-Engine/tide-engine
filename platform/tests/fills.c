#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "tide/platform.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

// Whether each channel of `got` (0xRRGGBBAA) is within `within` of `want`'s.
static int close_to(const uint32_t got, const uint32_t want, const int within)
{
    for (int shift = 0; shift < 32; shift += 8) {
        const int a = (int)((got >> shift) & 0xffu), b = (int)((want >> shift) & 0xffu);
        if (abs(a - b) > within) return 0;
    }
    return 1;
}

static int frame(void *context, const float seconds)
{
    (void)context;
    (void)seconds;
    printf("renderer: %s\n", tide_platform_renderer());
    // The gradient's top and bottom, a fill's middle, and outside its round corner
    const tide_float2 points[] = {{16, 1}, {16, 30}, {16, 16}, {48, 16}, {33, 1}};
    const uint32_t expected[] = {0xf8000aff, 0x0a00f8ff, 0x800080ff, 0x00ff00ff, 0x000000ff};
    for (int repeat = 0; repeat < 3; repeat++) {
        tide_draw_reset(&draw);
        tide_gui_begin(&gui, &devices, tide_f2(64, 32), NULL); // The screen's size, which fills read
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
        if (draw.run_count != 1 || draw.runs[0].count != 2) {
            puts("FAIL: the fills were not one instance run");
            return 1;
        }
        uint32_t pixels[sizeof points / sizeof points[0]];
        tide_platform_read_pixels_as(&draw, 64, 32, 1, points, sizeof points / sizeof points[0], pixels);
        if (tide_platform_draw_calls() != 2) { // The background's clear, and the fills
            printf("FAIL: fills used %u GPU draw calls (one clear plus one batch expected)\n", tide_platform_draw_calls());
            return 1;
        }
        for (unsigned i = 0; i < sizeof points / sizeof points[0]; i++) {
            if (!close_to(pixels[i], expected[i], 12)) {
                printf("FAIL: fill pixel %u frame %d: %08x, wanted about %08x\n", i, repeat, pixels[i], expected[i]);
                return 1;
            }
        }
    }
    puts("ok: a gradient and a lambda's fill draw together, with their colors");
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    return 0;
}

int main(void)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    tide_platform_open(&(tide_window_desc){.title = "Fills", .width = 64, .height = 32, .hidden = true});
    tide_platform_run(frame, NULL);
}
