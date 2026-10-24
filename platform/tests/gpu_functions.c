#include <stdio.h>

#include "game.h"
#include "tide/platform.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static int frame(void *context, const float seconds)
{
    (void)context;
    (void)seconds;
    printf("renderer: %s\n", tide_platform_renderer());
    local.Shapes.onlyBatch = true;
    const uint64_t hash = tide_world_hash(&world);
    const tide_float2 points[] = {{8, 8}, {24, 8}, {40, 8}, {56, 8}, {3, 3}, {42, 8}, {61, 8}};
    const uint32_t expected[] = {0xff0000ffu, 0x00ff00ffu, 0x0000ffffu, 0xffffffffu, 0x000000ffu, 0x000000ffu, 0xffffffffu};
    for (int repeat = 0; repeat < 3; repeat++) {
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
        if (draw.run_count != 1 || draw.runs[0].count != 4) {
            puts("FAIL: different SDF callbacks did not remain in one instance run");
            return 1;
        }
        uint32_t pixels[sizeof points / sizeof points[0]];
        tide_platform_read_pixels_as(&draw, 64, 32, 1, points, sizeof points / sizeof points[0], pixels);
        // Tide draws the background clear as one triangle; all four shapes
        // must contribute exactly one additional GPU draw.
        if (tide_platform_draw_calls() != 2) {
            printf("FAIL: SDF frame used %u GPU draw calls (one clear plus one shape batch expected)\n", tide_platform_draw_calls());
            return 1;
        }
        for (unsigned i = 0; i < sizeof points / sizeof points[0]; i++) {
            if (pixels[i] != expected[i]) {
                printf("FAIL: callable SDF pixel %u frame %d: %08x, wanted %08x\n", i, repeat, pixels[i], expected[i]);
                return 1;
            }
        }
        if (tide_world_hash(&world) != hash) return 1;
    }
    puts("ok: different SDF functions and captured radii share one instance run");
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
    tide_platform_open(&(tide_window_desc){.title = "GPU functions", .width = 64, .height = 32, .hidden = true});
    tide_platform_run(frame, NULL);
}
