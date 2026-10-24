#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

TIDE_TEST(gpu_functions_own_captures_and_batch_distinct_distance_functions)
{
    static tide_world world;
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    const uint64_t hash = tide_world_hash(&world);
    const float expected[] = {0.5f, 0.4f, 0.75f, 0.25f, 0, 0.875f, 0.75f, 0.25f, 0.75f, 0.2f, 0.125f, 0.75f, 0.5f};
    for (int frame = 0; frame < 3; frame++) {
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
        TIDE_CHECK(local.Shapes.frames == frame + 1);
        for (unsigned i = 0; i < sizeof expected / sizeof expected[0]; i++)
            TIDE_CHECK(fabsf(local.Shapes.results.at[i] - expected[i]) < 0.00001f);
        TIDE_REQUIRE(draw.run_count == 8);
        TIDE_CHECK(local.Shapes.operandOrder == 12);
        const tide_draw_run *batch = &draw.runs[0];
        TIDE_CHECK(batch->count == 4);
        TIDE_CHECK(batch->shader->uniform_floats == 0 && batch->shader->uniform_ints == 0);
        TIDE_CHECK(draw.runs[1].shader == draw.runs[2].shader && draw.runs[2].shader == draw.runs[3].shader);
        TIDE_CHECK(tide_world_hash(&world) == hash);
        if (frame == 0) {
            const uint32_t size = tide_local_pack(&local, NULL, 0);
            uint8_t *bytes = malloc(size);
            TIDE_REQUIRE(bytes != NULL);
            TIDE_REQUIRE(tide_local_pack(&local, bytes, size) == size);
            tide_draw_reset(&draw);
            tide_local_free(&local);
            tide_local_init(&local);
            TIDE_REQUIRE(tide_local_unpack(&local, bytes, size));
            free(bytes);
        }
    }
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}
