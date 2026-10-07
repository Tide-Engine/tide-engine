#include "game.h"
#include "tide_test.h"

static tide_world world;

// Logs go to stderr and assertions that hold do nothing: the world is the same
// as it would be without them, tick for tick.
TIDE_TEST(debug_output_changes_nothing)
{
    tide_world_init(&world, 1.0f);
    const tide_entity counter = {1, 1};
    for (int i = 0; i < 4; i++) tide_world_tick(&world);
    TIDE_CHECK(tide_get_Counter(&world, counter).value == 16);
    tide_world_free(&world);
}
