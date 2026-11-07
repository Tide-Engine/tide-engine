#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "tide/platform.h"

enum { WIDTH = 128, HEIGHT = 128 };
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static int failures;

#define CHECK(condition) do { if (!(condition)) { printf("FAIL: line %d: %s\n", __LINE__, #condition); failures++; } } while (0)

static void record(void)
{
    const uint64_t hash = tide_world_hash(&world);
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, (tide_float2){WIDTH, HEIGHT});
    gui.width = WIDTH; gui.height = HEIGHT;
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    CHECK(tide_world_hash(&world) == hash && world.PixelSentinel.value == 47);
}

static void pixels(const tide_float2 *points, const uint32_t *expected, const int count, const float scale, const int tolerance)
{
    tide_float2 scaled[8];
    uint32_t actual[8];
    for (int i = 0; i < count; i++) scaled[i] = (tide_float2){floorf(points[i].x * scale), floorf(points[i].y * scale)};
    tide_platform_read_pixels_as(&draw, WIDTH, HEIGHT, scale, scaled, count, actual);
    for (int i = 0; i < count; i++) {
        for (int channel = 0; channel < 4; channel++) {
            const int shift = 24 - channel * 8;
            const int got = (int)((actual[i] >> shift) & 255u);
            const int want = (int)((expected[i] >> shift) & 255u);
            if (abs(got - want) <= tolerance) continue;
            printf("FAIL: particle pixels scenario=%d target=%d world2D=%d rotated=%d sort=%d tied=%d scale=%.0f sample=%d channel=%d: %d expected %d\n",
                local.PixelProbe.scenario, local.PixelProbe.useTarget, local.PixelProbe.world2D, local.PixelProbe.rotated,
                local.PixelProbe.sort, local.PixelProbe.tied, (double)scale, i, channel, got, want);
            failures++;
        }
    }
}

static void run_pixels(void)
{
    PixelProbe *p = &local.PixelProbe;
    for (int target = 0; target < 2; target++) {
        p->useTarget = target != 0;
        p->scenario = 0; p->world2D = false; p->rotated = false;
        record();
        const tide_float2 soft[] = {{64, 64}, {72, 64}, {80, 64}, {64, 80}, {78, 78}};
        const uint32_t soft_colors[] = {0xff0000ffu, 0x8f0000ffu, 0x000000ffu, 0x000000ffu, 0x000000ffu};
        pixels(soft, soft_colors, 5, 1, 3);
        // Quadrant colors verify UV orientation independently of the quad data.
        const tide_float2 quadrants[] = {{48, 56}, {80, 56}, {48, 72}, {80, 72}, {20, 64}};
        const uint32_t pattern[] = {0xff0000ffu, 0x00ff00ffu, 0x0000ffffu, 0xffffffffu, 0x000000ffu};
        p->scenario = 1;
        for (int world2d = 0; world2d < 2; world2d++) {
            p->world2D = world2d != 0;
            p->rotated = false;
            record();
            pixels(quadrants, pattern, 5, 1, 2);
            pixels(quadrants, pattern, 5, 2, 2);
        }
        p->world2D = false; p->rotated = true;
        record();
        const tide_float2 turned[] = {{72, 48}, {72, 80}, {56, 48}, {56, 80}, {88, 64}};
        pixels(turned, pattern, 5, 1, 2);
        pixels(turned, pattern, 5, 2, 2);

        p->scenario = 3;
        const tide_float2 billboard[] = {{56, 60}, {72, 60}, {56, 68}, {72, 68}, {88, 64}};
        for (int rotated = 0; rotated < 2; rotated++) {
            p->rotated = rotated != 0;
            record();
            pixels(billboard, pattern, 5, 1, 2);
            pixels(billboard, pattern, 5, 2, 2);
        }
        const tide_float2 middle[] = {{64, 64}};
        p->scenario = 4; p->rotated = true; p->tied = false; p->sort = true;
        record();
        const uint32_t sorted[] = {0x400080ffu};
        pixels(middle, sorted, 1, 1, 2);
        p->sort = false;
        record();
        const uint32_t unsorted[] = {0x800040ffu};
        pixels(middle, unsorted, 1, 1, 2); // Far red still draws: near blue wrote no depth.
        p->sort = true; p->tied = true;
        record();
        pixels(middle, unsorted, 1, 1, 2); // Stable ties retain submission order.
        p->scenario = 5; p->tied = false;
        record();
        const uint32_t occluded[] = {0x00ff00ffu};
        pixels(middle, occluded, 1, 1, 2);
        bool particle_depth = false, restored = false;
        for (uint32_t i = 0; i < draw.count; i++) {
            const tide_draw_command *command = &draw.commands[i];
            if (command->kind == TIDE_DRAW_SHADED) {
                CHECK(command->depth == TIDE_DEPTH_READ && command->cull == TIDE_CULL_NONE);
                particle_depth = true;
            } else if (particle_depth && command->kind == TIDE_DRAW_MESH_3D) {
                CHECK(command->depth == TIDE_DEPTH_READ_WRITE && command->cull == TIDE_CULL_BACK);
                CHECK(command->blend == TIDE_BLEND_ALPHA);
                restored = true;
            }
        }
        CHECK(particle_depth && restored);
        p->scenario = 6; p->world2D = false; p->rotated = false;
        record();
        const tide_float2 retained[] = {{32, 64}, {96, 64}};
        const uint32_t retained_colors[] = {0xff0000ffu, 0x0000ffffu};
        pixels(retained, retained_colors, 2, 1, 3);
        p->scenario = 7;
        record();
        uint32_t particle_runs = 0, instances = 0;
        for (uint32_t i = 0; i < draw.count; i++) {
            if (draw.commands[i].kind != TIDE_DRAW_SHADED) continue;
            particle_runs++;
            instances += draw.runs[draw.commands[i].shaded.run].count;
        }
        CHECK(particle_runs == 1 && instances == 257);
    }
}

static int frame(void *context, float seconds)
{
    (void)context; (void)seconds;
    printf("renderer: %s\n", tide_platform_renderer());
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    run_pixels();
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
    if (failures) return 1;
    puts("ok: particle sprites, texture orientation, rotated billboards, stable alpha order, depth, owned captures and batching");
    return 0;
}

int main(void)
{
    tide_platform_open(&(tide_window_desc){.title = "Particle tests", .width = WIDTH, .height = HEIGHT, .hidden = true});
    tide_platform_run(frame, NULL);
}
