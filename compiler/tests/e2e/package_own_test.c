#include "game.h"
#include "tide_test.h"

static tide_world world;

// The package's Side(i) is its enum's conversion, not the game's function of that name.
TIDE_TEST(package_own_names_come_first)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    TIDE_CHECK(world.Seen.facing == 10); // Combat.Side(1) is Right
    TIDE_CHECK(world.Seen.side == 7);    // The game's Side
}
