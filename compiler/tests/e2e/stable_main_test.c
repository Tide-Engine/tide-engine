#include "game.h"
#include "tide_test.h"
TIDE_TEST(stable_main_can_restart_within_its_initial_identity_clock)
{
    tide_world world = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    TIDE_CHECK(world.Reset.starts == 2);
    TIDE_CHECK(world.tide_identity_clock == 1 && world.tide_main_occurrence == 2);
    TIDE_CHECK(tide_world_entity_count(&world) == 1);
    tide_world_tick(&world);
    TIDE_CHECK(world.tide_identity_clock == 2 && world.tide_main_occurrence == 0);
    TIDE_CHECK(world.Reset.starts == 2 && tide_world_entity_count(&world) == 1);
    tide_world_free(&world);
}
