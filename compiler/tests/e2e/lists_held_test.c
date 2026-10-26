#include <stdio.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_world copy;

TIDE_TEST(lists_held_all_checks_pass)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_world_tick(&world);
    if (world.Results.firstFailure) printf("    check %d failed\n", (int)world.Results.firstFailure);
    TIDE_CHECK(world.Results.checks == 7);
    TIDE_CHECK(world.Results.passed == world.Results.checks);
    TIDE_CHECK(tide_scratch_mark() == 0);
}

// A struct's list is the world's, like a component's own: in its snapshots and its hash.
TIDE_TEST(lists_held_are_in_snapshots)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_world_copy(&copy, &world);
    TIDE_CHECK(tide_world_hash(&copy) == tide_world_hash(&world));
    tide_world_tick(&world);
    TIDE_CHECK(tide_world_hash(&copy) != tide_world_hash(&world));
    tide_world_tick(&copy);
    TIDE_CHECK(tide_world_hash(&copy) == tide_world_hash(&world));
}
