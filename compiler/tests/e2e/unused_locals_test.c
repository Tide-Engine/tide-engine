#include "game.h"
#include "tide_test.h"

static tide_world world;

// The Main scene is slot 0, and its Setup spawns the counter into slot 1.
static const tide_entity COUNTER = {1, 1};

// A local nothing reads is a warning, not a change to the program: its value
// is still worked out, with what that does.
TIDE_TEST(unused_locals_values_still_run)
{
    tide_world_init(&world, 0.5f);
    TIDE_REQUIRE(tide_has_Counter(&world, COUNTER));
    TIDE_CHECK(tide_world_entity_count(&world) == 2);
    for (int i = 0; i < 3; i++) tide_world_tick(&world);
    const Counter c = tide_get_Counter(&world, COUNTER);
    TIDE_CHECK(c.ticks == 3);
    TIDE_CHECK(c.bumps == 3);                         // Bump ran each tick
    TIDE_CHECK(c.position.x == 1.5f);                 // Moved by `move`
    TIDE_CHECK(tide_world_entity_count(&world) == 5); // A spark a tick
    tide_world_free(&world);
}
