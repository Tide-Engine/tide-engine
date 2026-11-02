#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "tide/platform.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
enum { WIDTH = 192, HEIGHT = 96 };
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void record(void)
{
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, (tide_float2){WIDTH, HEIGHT});
    gui.width = WIDTH; gui.height = HEIGHT;
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
}

static int display(double value)
{
    const double m = value / (1 + value);
    const double encoded = m <= 0.0031308 ? 12.92 * m : 1.055 * pow(m, 1 / 2.4) - 0.055;
    return (int)(255 * encoded + 0.5);
}

static bool check_pixels(int side, float scale)
{
    // Independent perspective projection onto the known z=0 receiver plane.
    // All samples are well inside leaf/mask/occluder silhouettes.
    static const double positions[] = {-2.3, -1.7, 0, 2};
    tide_float2 points[4];
    uint32_t pixels[4];
    for (int i = 0; i < 4; i++)
        points[i] = (tide_float2){(float)floor(WIDTH * .5 + side * positions[i] * HEIGHT / (16 * .57735026918962576451)) * scale,
                                 HEIGHT * .5f * scale};
    tide_platform_read_pixels_as(&draw, WIDTH, HEIGHT, scale, points, 4, pixels);
    for (int i = 0; i < 4; i++) {
        const bool lit = i == 0 || (i == 3 && !local.FoliagePixelProbe.shadows);
        const double amount = lit ? local.FoliagePixelProbe.strength * 2 / 3.14159265358979323846 : 0;
        const double tint[] = {.5, 1, .25};
        for (int channel = 0; channel < 3; channel++) {
            const int wanted = display(amount * tint[channel]);
            const int got = (int)(pixels[i] >> (24 - channel * 8) & 255u);
            if (abs(got - wanted) > 3) {
                printf("FAIL: leaf side %d scale %.0f strength %.1f shadows %d sample %d channel %d: %d expected %d\n",
                    side, (double)scale, (double)local.FoliagePixelProbe.strength,
                    local.FoliagePixelProbe.shadows, i, channel, got, wanted);
                return false;
            }
        }
        if ((pixels[i] & 255u) != 255u) return false;
    }
    return true;
}

static bool scenario(int side)
{
    tide_world_init(&world, 1.0f / 60.0f);
    world.FoliagePixelFixture.side = side;
    tide_world_tick(&world);
    tide_local_init(&local);
    const uint64_t hash = tide_world_hash(&world);
    bool passed = true;
    for (int shadows = 0; shadows < 2 && passed; shadows++) {
        local.FoliagePixelProbe.shadows = shadows != 0;
        for (int enabled = 0; enabled < 2 && passed; enabled++) {
            local.FoliagePixelProbe.strength = enabled ? .5f : 0;
            for (int scale = 1; scale <= 2 && passed; scale++) {
                record();
                passed = tide_world_hash(&world) == hash && check_pixels(side, (float)scale);
            }
        }
    }
    local.FoliagePixelProbe.disabled = true;
    record();
    tide_draw_forget(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
    return passed;
}

static bool wind_pixels(void)
{
    tide_world_init(&world, 1.0f / 60.0f);
    world.FoliagePixelFixture.wind = true;
    tide_world_tick(&world);
    tide_local_init(&local);
    const uint64_t hash = tide_world_hash(&world);
    bool passed = true;
    for (int moving = 0; moving < 2 && passed; moving++) {
        // Smooth-wave extrema give exactly zero breeze at time 0 and .6 at
        // time .5. Authored y=1.5/2.5 corners move 1.35/3.75 in X; their
        // triangle's middle row is centered on 2.55. The alpha mask keeps
        // its left half. These landmarks use no package wind helper.
        local.FoliagePixelProbe.time = moving ? .5f : 0;
        for (int scale = 1; scale <= 2 && passed; scale++) {
            record();
            tide_float2 points[4];
            uint32_t pixels[4];
            const double x[] = {-.3, 2.25};
            for (int i = 0; i < 2; i++)
                points[i] = (tide_float2){(float)floor(WIDTH * .5 + x[i] * HEIGHT / (16 * .57735026918962576451)) * scale,
                    (float)floor(HEIGHT * .5 - 2 * HEIGHT / (16 * .57735026918962576451)) * scale};
            points[2] = (tide_float2){4.0f * scale, 4.0f * scale};
            points[3] = (tide_float2){12.0f * scale, 4.0f * scale};
            tide_platform_read_pixels_as(&draw, WIDTH, HEIGHT, (float)scale, points, 4, pixels);
            for (int i = 0; i < 4; i++) {
                const bool covered = (i % 2) == moving;
                for (int channel = 0; channel < 3; channel++) {
                    const int wanted = i < 2 ? (covered && channel == 0 ? display(4) : 0) : (covered ? 0 : 255);
                    const int got = (int)(pixels[i] >> (24 - channel * 8) & 255u);
                    if (abs(got - wanted) > 3) {
                        printf("FAIL: wind time %.1f scale %d sample %d channel %d: %d expected %d\n",
                            (double)local.FoliagePixelProbe.time, scale, i, channel, got, wanted);
                        passed = false;
                    }
                }
            }
            passed = passed && tide_world_hash(&world) == hash;
        }
    }
    local.FoliagePixelProbe.disabled = true;
    record();
    tide_draw_forget(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
    return passed;
}

static int frame(void *context, float seconds)
{
    (void)context; (void)seconds;
    printf("renderer: %s\n", tide_platform_renderer());
    const bool passed = scenario(1) && scenario(-1) && wind_pixels();
    tide_draw_free(&draw);
    if (!passed) return 1;
    puts("ok: thin-leaf colored backlight, optical thickness, two sides, disabled control, cutout holes, real occluder shadows, shared wind color/shadow motion, 1x/2x and unchanged match hashes");
    return 0;
}

int main(void)
{
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_platform_open(&(tide_window_desc){.title = "Render3D foliage", .width = WIDTH, .height = HEIGHT, .hidden = true});
    tide_platform_run(frame, NULL);
}
