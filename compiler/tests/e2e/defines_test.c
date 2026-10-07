#include "game.h"
#include "tide_test.h"

static tide_world world;

TIDE_TEST(defines_are_the_files_the_builds_and_the_packages)
{
    tide_world_init(&world, 1.0f);
    TIDE_CHECK(world.Picked.own == 1);
    TIDE_CHECK(world.Picked.fastHere == 0); // #undef, in that file alone
    TIDE_CHECK(world.Picked.fastThere == 1);
    TIDE_CHECK(world.Picked.localThere == 0); // A file's #define is its own
    TIDE_CHECK(world.Picked.fromBuild == 1);
    TIDE_CHECK(world.Picked.unknown == 0);
    TIDE_CHECK(world.Picked.debugging == DEFINES_DEBUG);
    TIDE_CHECK(world.Picked.withExtra == 7);
    TIDE_CHECK(world.Picked.withBox2D == 0);
    tide_world_free(&world);
}
