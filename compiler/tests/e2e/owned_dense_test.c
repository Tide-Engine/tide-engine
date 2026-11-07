#include "game.h"
#include "tide_test.h"

TIDE_TEST(owned_dense_dependency_cycles_select_all_producers_only)
{
    tide_world world = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    const tide_player_id player = tide_player_from_index(0);
    // No owned root: none of the cyclic producers is selected.
    TIDE_CHECK(tide_game_api.predict_far(&world, player));
    tide_world_tick(&world);
    TIDE_CHECK(world.Shared.value == 0 && world.Unrelated.calls == 0);
    tide_world_player_joined(&world, player);
    tide_world_tick(&world); // Joined handlers run at tick end.
    TIDE_CHECK(world.Shared.value == 80 && world.Unrelated.calls == 1);
    for (int tick = 1; tick <= 100; tick++) {
        TIDE_CHECK(tide_game_api.predict_far(&world, player));
        tide_world_tick(&world);
        TIDE_CHECK(world.Shared.value == (tick + 1) * 80);
        TIDE_CHECK(world.Unrelated.calls == 1);
    }
    // The prediction flag resets: confirmed execution still runs everything.
    tide_world_tick(&world);
    TIDE_CHECK(world.Shared.value == 8160 && world.Unrelated.calls == 2);
    tide_world_free(&world);
}
