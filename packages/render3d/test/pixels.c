#include <stdio.h>

#include "game.h"
#include "tide/platform.h"
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static int byte(float value)
{
    if (!(value > 0)) return 0;
    if (value >= 1) return 255;
    return (int)(value * 255 + 0.5f);
}

static int compare(const uint32_t pixel, const tide_color expected, const int point, const int repeat)
{
    const int want[] = {byte(expected.r), byte(expected.g), byte(expected.b), 255};
    for (int channel = 0; channel < 4; channel++) {
        const int actual = (int)((pixel >> (24 - channel * 8)) & 255u);
        const int error = actual - want[channel];
        if (error < -3 || error > 3) {
            printf("FAIL: Render3D pixel %d frame %d channel %d: %d wanted %d (pixel %08x)\n",
                   point, repeat, channel, actual, want[channel], pixel);
            return 1;
        }
    }
    return 0;
}

static int frame(void *context, const float seconds)
{
    (void)context;
    (void)seconds;
    printf("renderer: %s\n", tide_platform_renderer());
    const uint64_t hash = tide_world_hash(&world);
    const tide_float2 points[] = {{26, 32}, {50, 32}, {78, 32}, {103, 32}, {88, 48}, {112, 48}, {56, 48}};
    gui.width = 128, gui.height = 64;
    uint64_t hdr = 0, shadow = 0;
    for (int repeat = 0; repeat < 4; repeat++) {
        tide_draw_reset(&draw);
        tide_draw_screen_size(&draw, (tide_float2){128, 64});
        local.Probe.disableShadows = repeat == 3;
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
        if (repeat && (hdr != local.Render3D_Renderer.hdr.id || shadow != local.Render3D_Renderer.shadowMap.id)) return 1;
        hdr = local.Render3D_Renderer.hdr.id;
        shadow = local.Render3D_Renderer.shadowMap.id;
        uint32_t pixels[7];
        const float scale = repeat == 2 ? 2.0f : 1.0f;
        tide_float2 physical[7];
        for (int i = 0; i < 7; i++) physical[i] = (tide_float2){(points[i].x + 0.5f) * scale, (points[i].y + 0.5f) * scale};
        tide_platform_read_pixels_as(&draw, 128, 64, scale, physical, 7, pixels);
        for (int i = 0; i < 7; i++) {
            tide_color expected = local.Probe.expected.at[i];
            if (i == 6 && !local.Probe.disableShadows) expected = (tide_color){0, 0, 0, 1};
            if (compare(pixels[i], expected, i, repeat)) return 1;
        }
        if (tide_world_hash(&world) != hash) return 1;
    }
    tide_draw_reset(&draw);
    local.Probe.disableRenderer = true;
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    if (tide_render_target_live(hdr) || tide_render_target_live(shadow) || draw.run_count) return 1;
    puts("ok: Render3D PBR, shadows, HDR, mirrored normals and captured surface callbacks");
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
    return 0;
}

int main(void)
{
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    tide_platform_open(&(tide_window_desc){.title = "Render3D", .width = 128, .height = 64, .hidden = true});
    tide_platform_run(frame, NULL);
}
