#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_world copy;

static const tide_entity THING = {1, 1};

TIDE_TEST(sort_all_checks_pass)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    const Results r = tide_get_Results(&world, THING);
    if (r.passed != r.checks) printf("first failed check: %d\n", r.firstFailure);
    TIDE_CHECK(r.checks > 0 && r.passed == r.checks);
    const tide_str order = tide_text_read(&world.heap, r.order);
    TIDE_CHECK(order.bytes == 20 && memcmp(order.ptr, "bob dee ann cid eve ", 20) == 0);
    tide_world_free(&world);
}

// Sorting is part of the simulation: two worlds that sort the same give the same hash.
TIDE_TEST(sort_is_the_same_on_every_run)
{
    tide_world_init(&world, 1.0f);
    tide_world_init(&copy, 1.0f);
    tide_world_tick(&world);
    tide_world_tick(&copy);
    TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&copy));
    tide_world_free(&world);
    tide_world_free(&copy);
}
