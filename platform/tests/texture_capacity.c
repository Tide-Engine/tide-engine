#include <stdio.h>
#include <string.h>
#include "game.h"
#include "tide/platform.h"
#ifdef __wasm__
#include "tide_web.h"
#endif

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static uint8_t patterns[4][64 * 64 * 4];
static const uint8_t white[] = {255, 255, 255, 255};

static void record(const int mode)
{
    local.CapacityArt.mode = mode;
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
}
static int frame(void *user, const float seconds)
{
    (void)user; (void)seconds;
#ifdef __wasm__
    static int lost;
    if (lost == 1) {
        if (!tide_web_eval("Tide.gpuDevices() > 1")) return TIDE_KEEP_RUNNING;
        lost = 2;
    }
#endif
    printf("renderer: %s\n", tide_platform_renderer());
    tide_float2 points[4 * 16];
    for (uint32_t slot = 0; slot < 4; slot++)
        for (uint32_t i = 0; i < 16; i++)
            points[slot * 16 + i] = tide_f2((float)(slot * 32 + 1 + i * 2), (float)(1 + (i * 7) % 30));
    const uint64_t hash = tide_world_hash(&world);
    for (uint32_t pass = 0; pass < 9; pass++) {
        tide_texture_ref *high[] = {&local.CapacityArt.high0, &local.CapacityArt.high1,
                                   &local.CapacityArt.high2, &local.CapacityArt.high3};
        for (uint32_t slot = 0; slot < 4; slot++) {
            const tide_texture_ref image = {.image = {.pixels = patterns[slot], .width = 64, .height = 64, .version = 1}};
            *high[slot] = tide_texture_sampled(image,
                tide_sampling_bits((int32_t)((pass + slot) % 3u), (int32_t)((pass / 3u + slot) % 3u)));
        }
        uint32_t high_pixels[64], low_pixels[64];
        record(0);
        tide_platform_read_pixels_as(&draw, 128, 32, 1, points, 64, high_pixels);
        record(1);
        tide_platform_read_pixels_as(&draw, 128, 32, 1, points, 64, low_pixels);
        for (uint32_t i = 0; i < 64; i++) {
            if (high_pixels[i] != low_pixels[i]) {
                printf("FAIL: high slot %u, filter/wrap case %u, pixel %u: %08x != reference %08x\n",
                       8u + i / 16u, pass, i, high_pixels[i], low_pixels[i]);
                return 1;
            }
        }
    }
    uint32_t fixed_pixels[64], reference_pixels[64];
    record(4);
    tide_platform_read_pixels_as(&draw, 128, 32, 1, points, 64, fixed_pixels);
    tide_texture_ref *fixed_sources[] = {&local.CapacityArt.high0, &local.CapacityArt.high1,
                                       &local.CapacityArt.high2, &local.CapacityArt.high3};
    for (uint32_t i = 0; i < 4; i++)
        *fixed_sources[i] = tide_texture_sampled(*fixed_sources[i], tide_sampling_bits((int32_t)(i % 3u), (int32_t)((i + 1u) % 3u)));
    record(1);
    tide_platform_read_pixels_as(&draw, 128, 32, 1, points, 64, reference_pixels);
    if (memcmp(fixed_pixels, reference_pixels, sizeof fixed_pixels) != 0) {
        puts("FAIL: fixed high-slot filter/wrap/mip settings differ from low-slot references");
        return 1;
    }
    record(2);
    const tide_float2 point = {16, 16};
    uint32_t pixel;
    tide_platform_read_pixels_as(&draw, 128, 32, 1, &point, 1, &pixel);
    // Each read kind contributes to the green/red witness in the frag.
    if (pixel != 0x00ff00ffu || tide_world_hash(&world) != hash) {
        printf("FAIL: high-slot data/depth/comparison/cube result %08x\n", pixel);
        return 1;
    }
    puts("ok: slots 8..11 match independent low-slot draws for all filters/wraps and high read kinds");
#ifdef __wasm__
    if (!lost && strcmp(tide_platform_renderer(), "WebGPU") == 0) {
        tide_web_eval("Tide.loseDevice()");
        lost = 1;
        return TIDE_KEEP_RUNNING;
    }
#endif
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    return 0;
}
int main(void)
{
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    const tide_texture_ref plain = {.image = {.pixels = white, .width = 1, .height = 1, .version = 1}};
    local.CapacityArt.pad0 = plain; local.CapacityArt.pad1 = plain;
    local.CapacityArt.pad2 = plain; local.CapacityArt.pad3 = plain;
    local.CapacityArt.pad4 = plain; local.CapacityArt.pad5 = plain;
    local.CapacityArt.pad6 = plain; local.CapacityArt.pad7 = plain;
    for (uint32_t slot = 0; slot < 4; slot++) {
        for (uint32_t y = 0; y < 64; y++) {
            for (uint32_t x = 0; x < 64; x++) {
                uint8_t *p = patterns[slot] + (y * 64 + x) * 4;
                p[0] = (x + 3u * y) % 8u <= slot ? (uint8_t)(255u - slot * 32u) : (uint8_t)(slot * 16u);
                p[1] = 0; p[2] = 0; p[3] = 255;
            }
        }
    }
    tide_platform_open(&(tide_window_desc){.title = "Texture capacity", .width = 128, .height = 32, .hidden = true});
    tide_platform_run(frame, NULL);
}
