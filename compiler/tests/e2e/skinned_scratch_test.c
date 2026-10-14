#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static uint32_t expected_mark;
static uint32_t observed;
static uint32_t bad_marks;

void skinned_scratch_observe(const int32_t index)
{
    if (index != (int32_t)observed || tide_scratch_mark() != expected_mark) bad_marks++;
    observed++;
}

TIDE_TEST(skinned_struct_temporaries_reclaim_scratch_after_each_entity_and_frame)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world world = {0};
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    expected_mark = tide_scratch_mark();
    bad_marks = 0u;
    for (uint32_t frame = 0; frame < 16u; frame++) {
        observed = 0u;
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
        TIDE_CHECK(observed == 32u);
        TIDE_CHECK(bad_marks == 0u);
        TIDE_CHECK(tide_scratch_mark() == expected_mark);
        uint32_t instances = 0u;
        for (uint32_t i = 0; i < draw.run_count; i++) instances += draw.runs[i].count;
        TIDE_CHECK(instances == 32u);
        // Keep failures bounded even when testing a compiler without cleanup.
        tide_scratch_reset(expected_mark);
    }
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_pack_unload();
}
