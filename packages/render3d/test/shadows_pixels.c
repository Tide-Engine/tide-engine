#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "tide/platform.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

enum { WIDTH = 192, HEIGHT = 64, MAX_SAMPLES = 48 };
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void record(void)
{
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, (tide_float2){WIDTH, HEIGHT});
    gui.width = WIDTH;
    gui.height = HEIGHT;
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
}

static bool pixels(const float *expected, const float scale)
{
    const uint64_t hash = tide_world_hash(&world);
    record();
    if (tide_world_hash(&world) != hash) return false;
    tide_float2 points[MAX_SAMPLES];
    uint32_t result[MAX_SAMPLES];
    const int count = local.ShadowProbe.sampleCount;
    for (int i = 0; i < count; i++) points[i] = (tide_float2){(float)(i * 4 + 2) * scale, 4 * scale};
    tide_platform_read_pixels_as(&draw, WIDTH, HEIGHT, scale, points, count, result);
    for (int i = 0; i < count; i++) {
        const int wanted = (int)(expected[i] * 255 + 0.5f);
        for (int channel = 0; channel < 3; channel++) {
            const int actual = (int)((result[i] >> (24 - channel * 8)) & 255u);
            if (abs(actual - wanted) > 3) {
                printf("FAIL: shadow mode %d, sample %d (%.7g), scale %.0f: channel %d = %d, expected %d\n",
                    local.ShadowProbe.gpuMode, i, (double)local.ShadowProbe.samples.at[i],
                    (double)scale, channel, actual, wanted);
                return false;
            }
        }
        if ((result[i] & 255u) != 255u) return false;
        if (local.ShadowProbe.gpuMode == 3 && i > 0 && (result[i] >> 24) < (result[i - 1] >> 24)) {
            puts("FAIL: tent-filtered edge is not monotonic");
            return false;
        }
    }
    return true;
}

static void append(float *expected, const float depth, const float visibility)
{
    const int index = local.ShadowProbe.sampleCount++;
    local.ShadowProbe.samples.at[index] = depth;
    expected[index] = visibility;
}

static bool cascade_pixels(const int mode)
{
    local.ShadowProbe.gpuMode = mode;
    local.ShadowProbe.sampleCount = 0;
    record();
    if (local.ShadowProbe.viewCount != 4) return false;
    float expected[MAX_SAMPLES];
    for (int i = 0; i < 4; i++) {
        const Render3D_ShadowView view = local.ShadowProbe.views.at[i];
        const float previous = i ? local.ShadowProbe.views.at[i - 1].cascadeEnd : local.ShadowProbe.camera.near;
        const float current = mode == 1 || (i & 1) == 0 ? 0 : 1;
        const float next = i == 3 ? 1 : mode == 1 || (i & 1) != 0 ? 0 : 1;
        append(expected, (previous + view.cascadeBlend) * 0.5f, current);
        append(expected, view.cascadeBlend, current);
        append(expected, (view.cascadeBlend + view.cascadeEnd) * 0.5f, (current + next) * 0.5f);
        append(expected, view.cascadeEnd, next);
    }
    append(expected, local.ShadowProbe.range + 1, 1);
    append(expected, -1, 1);
    return pixels(expected, 1) && pixels(expected, 2);
}

static double clamp_unit(const double value) { return fmax(0, fmin(1, value)); }

static bool tent_pixels(void)
{
    local.ShadowProbe.gpuMode = 3;
    local.ShadowProbe.sampleCount = 0;
    float expected[MAX_SAMPLES];
    for (int i = 0; i <= 24; i++) {
        const double position = (double)i / 4 - 3;
        // A half-filled tile has a depth step BETWEEN texel centers. Three
        // ordinary bilinear comparisons with weights 1,2,1 are the independent
        // oracle; no optimized ShadowAxis weights or offsets are reused.
        const double visibility = (clamp_unit(position - 0.5) + 2 * clamp_unit(position + 0.5)
                                 + clamp_unit(position + 1.5)) / 4;
        append(expected, (float)position, (float)visibility);
    }
    return pixels(expected, 1) && pixels(expected, 2);
}

static int frame(void *context, const float seconds)
{
    (void)context;
    (void)seconds;
    printf("renderer: %s\n", tide_platform_renderer());
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    const bool passed = cascade_pixels(1) && cascade_pixels(2) && tent_pixels();
    local.ShadowProbe.disabled = true;
    record();
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
    if (!passed) return 1;
    puts("ok: Render3D cascade selection, overlapping blends, range fade, off-camera caster and normalized monotonic tent edge at 1x/2x");
    return 0;
}

int main(void)
{
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_platform_open(&(tide_window_desc){.title = "Render3D shadows", .width = WIDTH, .height = HEIGHT, .hidden = true});
    tide_platform_run(frame, NULL);
}
