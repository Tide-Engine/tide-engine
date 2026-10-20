#include "game.h"
#include "tide_test.h"

static tide_world world;

void readonly_aggregate_change(void) { world.Big.words.at[0] = 99; }

TIDE_TEST(readonly_aggregates_avoid_recursive_stack_copies_and_preserve_value_snapshots)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    TIDE_CHECK(world.Results.deep == 11 && world.Results.plain == 17);
    TIDE_CHECK(world.Results.method == 17 && world.Results.methodAlias == 17);
    TIDE_CHECK(world.Results.collect == 17);
    TIDE_CHECK(world.Results.alias == 11 && world.Results.later == 22);
    TIDE_CHECK(world.Results.external == 33 && world.Big.words.at[0] == 99);
    TIDE_CHECK(world.Results.nested == 17);
    tide_world_free(&world);
}
