#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/platform.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

static int frame(void *context, const float seconds)
{
    (void)context;
    (void)seconds;
    printf("renderer: %s\n", tide_platform_renderer());
    for (int repeat = 0; repeat < 2; repeat++) {
        local.Bits.value = repeat ? tide_f4(3.25f, -2.5f, 17.75f, -100.125f) : tide_f4(-1.25f, 0.0f, 1.0f, -17.5f);
        local.Bits.expected = tide_asint_f4(local.Bits.value);
        local.Bits.hashes = tide_i4(tide_hash_f(local.Bits.value.x), tide_hash_f2(tide_f2(local.Bits.value.x, local.Bits.value.y)),
                                   tide_hash_f3(tide_f3(local.Bits.value.x, local.Bits.value.y, local.Bits.value.z)), tide_hash_f4(local.Bits.value));
        tide_draw_reset(&draw);
        draw.screen_scale = tide_platform_screen_scale();
        tide_gui_begin(&gui, &devices, tide_f2(32, 32), tide_platform_measure_text);
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
        tide_gui_end(&gui, &draw);
        if (local.Bits.screen.x != draw.screen_scale.x || local.Bits.screen.y != draw.screen_scale.y) return 1;
        const tide_float2 points[] = {{8, 8}, {24, 24}};
        uint32_t pixels[2];
        tide_platform_read_pixels(&draw, points, 2, pixels);
        if (pixels[0] != 0x00ff00ffu || pixels[1] != 0x00ff00ffu) {
            printf("FAIL: float bits/hash pixels %08x %08x\n", pixels[0], pixels[1]);
            return 1;
        }
    }
    puts("ok: scalar/vector bit casts and float hashes match CPU bits");
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    return 0;
}

int main(void)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_platform_open(&(tide_window_desc){.title = "float bits", .width = 32, .height = 32, .hidden = true});
    tide_platform_run(frame, NULL);
}
