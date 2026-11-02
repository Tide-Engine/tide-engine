#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "game.h"
#include "tide/platform.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
enum { WIDTH = 128, HEIGHT = 96 };
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static int display(double value)
{
    const double mapped = value / (1 + value);
    return (int)(255 * (mapped <= .0031308 ? 12.92 * mapped : 1.055 * pow(mapped, 1 / 2.4) - .055) + .5);
}

static bool near3(tide_float3 got, double x, double y, double z)
{
    return fabs(got.x - x) < .00001 && fabs(got.y - y) < .00001 && fabs(got.z - z) < .00001;
}

static bool scenario(bool dither)
{
    tide_world_init(&world, 1.0f / 60.0f);
    world.FogScene.dither = dither;
    tide_world_tick(&world);
    tide_local_init(&local);
    const uint64_t hash = tide_world_hash(&world);
    bool passed = true;
    for (int enabled = 0; enabled < (dither ? 1 : 2) && passed; enabled++) {
        local.FogProbe.enabled = enabled != 0;
        tide_draw_reset(&draw);
        tide_draw_screen_size(&draw, tide_f2(WIDTH, HEIGHT));
        gui.width = WIDTH; gui.height = HEIGHT;
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
        const FogProbe *probe = &local.FogProbe;
        passed = near3(probe->values.at[0], 2, 4, 8) && near3(probe->values.at[1], 2, 4, 8)
            && near3(probe->values.at[2], 1.1, 2.2, 4.3) && near3(probe->values.at[3], 2, 4, 8)
            && near3(probe->values.at[4], 0, .4, .6) && probe->complements == 80;
        for (int i = 0; i < 5; i++) passed = passed && probe->coverage.at[i] == 16 - i * 4;
        for (int scale = 1; scale <= 2 && passed; scale++) {
            tide_float2 points[64];
            uint32_t pixels[64];
            const int side = 4 * scale;
            const int count = dither ? side * side : 2;
            for (int i = 0; i < count; i++)
                points[i] = dither ? tide_f2(48 * scale + i % side, 40 * scale + i / side)
                    : tide_f2((float)(64 + (i ? 1 : -1) * .9 * 96 / (6 * .5773502691896258)) * scale, 48 * scale);
            tide_platform_read_pixels_as(&draw, WIDTH, HEIGHT, (float)scale, points, (uint32_t)count, pixels);
            unsigned red = 0, blue = 0;
            for (int i = 0; i < count && passed; i++) {
                const int r = (int)(pixels[i] >> 24), g = (int)(pixels[i] >> 16 & 255u), b = (int)(pixels[i] >> 8 & 255u);
                if (dither) {
                    if (abs(r - display(.8)) <= 3 && g <= 3 && b <= 3) red++;
                    else if (abs(b - display(.8)) <= 3 && g <= 3 && r <= 3) blue++;
                    else passed = false;
                } else {
                    const int wanted[] = {display(enabled ? .2 : i == 0 ? .8 : 0),
                        display(enabled ? .4 : 0), display(enabled ? .6 : i == 1 ? .8 : 0)};
                    passed = abs(r - wanted[0]) <= 3 && abs(g - wanted[1]) <= 3 && abs(b - wanted[2]) <= 3;
                }
                if (!passed) printf("FAIL fog/dither %d enabled %d scale %d sample %d rgba %08x\n", dither, enabled, scale, i, pixels[i]);
            }
            if (dither && (red != (unsigned)count / 2 || blue != (unsigned)count / 2)) passed = false;
        }
        passed = passed && tide_world_hash(&world) == hash;
    }
    tide_draw_forget(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
    return passed;
}

static int frame(void *context, float seconds)
{
    (void)context; (void)seconds;
    const bool passed = scenario(false) && scenario(true);
    tide_draw_free(&draw);
    if (!passed) return 1;
    puts("ok: Render3D post-light fog, disabled controls, complementary dither, 1x/2x and unchanged match hashes");
    return 0;
}

int main(void)
{
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_platform_open(&(tide_window_desc){.title = "Render3D fog", .width = WIDTH, .height = HEIGHT, .hidden = true});
    tide_platform_run(frame, NULL);
}
