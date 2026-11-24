#include "game.h"
#include "tide_test.h"

// The game builds, with generated C's warnings as errors, and every comparison gives Tide's answer.
TIDE_TEST(comparisons_settled_by_their_types_and_constants_build_and_give_their_answers)
{
    tide_world world = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    TIDE_CHECK(world.Probe.failed == 0);
    TIDE_CHECK(world.Probe.passed == 60);
    tide_world_free(&world);
}
