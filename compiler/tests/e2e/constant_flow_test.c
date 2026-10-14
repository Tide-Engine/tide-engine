#include "game.h"
#include "tide_test.h"

static tide_world world;

TIDE_TEST(constant_conditions_keep_their_reachable_paths)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    TIDE_CHECK(world.Seen.returns == 21);
    TIDE_CHECK(world.Seen.switches == 34);
    TIDE_CHECK(world.Seen.filled == 11);
    TIDE_CHECK(world.Seen.fillElse == 12);
    TIDE_CHECK(world.Seen.fillLoop == 13);
    TIDE_CHECK(world.Seen.fromAnd == 14 && world.Seen.afterAnd == 14);
    TIDE_CHECK(world.Seen.fromOr == 14 && world.Seen.afterOr == 14);
    TIDE_CHECK(world.Seen.skippedCalls == 0);
    TIDE_CHECK(world.Seen.skippedSum == 35);
}
