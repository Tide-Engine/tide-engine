#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

TIDE_TEST(prototype_nested_foreach_and_captured_literals)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world world = {0};
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    const uint64_t hash = tide_world_hash(&world);
    for (int frame = 0; frame < 3; frame++) {
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
        TIDE_CHECK(local.Art.checks == 13);
        TIDE_CHECK(local.Art.passed == local.Art.checks);
        TIDE_REQUIRE(draw.run_count == 1);
        const tide_draw_run *run = &draw.runs[0];
        float values[2] = {0};
        memcpy(values, draw.uniforms + run->uniforms, sizeof values);
        TIDE_CHECK(values[0] == 1.0f && values[1] == -1.0f);
        TIDE_CHECK(tide_world_hash(&world) == hash);
        TIDE_CHECK(tide_scratch_mark() == 0);
    }
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_pack_unload();
}

TIDE_TEST(prototype_nested_foreach_waits_and_replays)
{
    tide_world world = {0};
    tide_world snapshot = {0};
    tide_world restored = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_world_tick(&world);
    TIDE_CHECK(!world.Waiting.done);
    tide_world_copy(&snapshot, &world);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_REQUIRE(tide_world_pack(&world, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&restored, bytes, size));
    free(bytes);
    for (int tick = 0; tick < 10; tick++) {
        tide_world_tick(&world);
        tide_world_tick(&snapshot);
        tide_world_tick(&restored);
        TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&snapshot));
        TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&restored));
    }
    TIDE_CHECK(world.Waiting.done);
    TIDE_CHECK(world.Waiting.sum == 36);
    TIDE_CHECK(world.Waiting.index == 1);
    tide_world_free(&snapshot);
    tide_world_free(&restored);
    tide_world_free(&world);
}
