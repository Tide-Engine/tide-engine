// Baked poses' pixels (baked.tide): rows a clip was baked into, skinned on the GPU from each instance's frame,
// on every backend, twice to check the rows are kept.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "game.h"
#include "tide/pack.h"
#include "tide/platform.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

static int frame(void *user, const float seconds)
{
    (void)user;
    (void)seconds;
    printf("renderer: %s\n", tide_platform_renderer());
    int failures = 0;
    static const struct {
        tide_float2 at;
        uint32_t color;
        const char *what;
    } checks[] = {
        {{20, 20}, 0xff0000ffu, "a clip's first row draws the model at rest"},
        {{20, 52}, 0x000000ffu, "its last row leaves the place at rest"},
        {{44, 52}, 0x00ff00ffu, "...for where the clip ends"},
        {{36, 84}, 0x0000ffffu, "halfway through, between two rows"},
        {{18, 84}, 0x000000ffu, "...and no longer at rest"},
        {{96, 20}, 0xffeb04ffu, "a fade halfway between two frames"},
        {{80, 20}, 0x000000ffu, "...is neither of them"},
        {{108, 52}, 0xffffffffu, "each corner on its heaviest bone alone"},
        {{44, 116}, 0x00ffffffu, "a socket carries a prop, through a function given the poses whole"},
        {{20, 116}, 0x000000ffu, "...from where it was at rest"},
        {{108, 84}, 0xff00ffffu, "a depth target's draw of the same frame is where the color's is"},
    };
    enum { COUNT = sizeof checks / sizeof checks[0] };
    for (int repeat = 0; repeat < 2; repeat++) {
        tide_draw_reset(&draw);
        tide_gui_begin(&gui, &devices, tide_f2(128, 128), tide_platform_measure_text);
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
        tide_gui_end(&gui, &draw);
        tide_float2 points[COUNT];
        uint32_t pixels[COUNT];
        for (int i = 0; i < COUNT; i++) points[i] = checks[i].at;
        tide_platform_read_pixels_as(&draw, 128, 128, 1.0f, points, COUNT, pixels);
        for (int i = 0; i < COUNT; i++) {
            bool same = true;
            for (int c = 0; c < 3; c++) {
                const int actual = (int)(pixels[i] >> (24 - 8 * c) & 255u);
                const int expected = (int)(checks[i].color >> (24 - 8 * c) & 255u);
                same = same && actual >= expected - 5 && actual <= expected + 5;
            }
            printf("%s: %s%s (%08X, expected %08X)\n", same ? "ok" : "FAIL", checks[i].what, repeat ? ", again" : "",
                   (unsigned)pixels[i], (unsigned)checks[i].color);
            if (!same) failures++;
        }
    }
    return failures;
}

int main(void)
{
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_platform_open(&(tide_window_desc){.title = "baked", .width = 128, .height = 128, .hidden = true});
    tide_platform_run(frame, NULL);
}
