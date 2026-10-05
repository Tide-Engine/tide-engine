#include "local_events_same.h"

static tide_world world;
static tide_world base;

static void tick(tide_world *w)
{
    tide_world_tick(w);
}

// The game without events for local code: the same world as with them
TIDE_TEST(local_events_plain_match_is_the_same)
{
    same_match_check(&world, &base, tick);
    tide_world_free(&world);
    tide_world_free(&base);
}

// ...and the same bytes between its machines
TIDE_TEST(local_events_plain_traffic_is_the_same)
{
    same_traffic_check(NULL, NULL);
}
