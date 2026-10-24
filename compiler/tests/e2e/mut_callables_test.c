#include "game.h"
#include "tide_test.h"

TIDE_TEST(mut_callables_forward_the_callers_storage_and_preserve_modes)
{
    static tide_world world;
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    const uint64_t hash = tide_world_hash(&world);
    const float expected[] = {3, 3, 7, 7, 11, 11, 11, 12, 12, 15, 5, 7, 20, 5, 2};
    for (int frame = 0; frame < 3; frame++) {
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
        TIDE_CHECK(local.State.frames == frame + 1);
        for (unsigned i = 0; i < sizeof expected / sizeof expected[0]; i++)
            TIDE_CHECK(local.State.results.at[i] == expected[i]);
        TIDE_CHECK(tide_world_hash(&world) == hash);
    }
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}
