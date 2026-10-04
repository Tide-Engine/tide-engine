#include "game.h"
#include "tide_test.h"

static tide_world world;

TIDE_TEST(empty_world_ticks_with_no_entities)
{
    tide_world_init(&world, 0.5f);
    TIDE_CHECK(tide_world_entity_count(&world) == 1); // Only the Main scene
    for (int i = 0; i < 3; i++) tide_world_tick(&world);
    TIDE_CHECK(tide_world_entity_count(&world) == 1);
    TIDE_CHECK(world.Time.tick == 3);
    TIDE_CHECK(world.Time.dt == 0.5f);
    TIDE_CHECK(world.Time.t == 1.5f); // tick * dt
    TIDE_CHECK(world.Time.tickRate == 2);
}

// Time.tickRate is 1 / dt to the nearest: every rate a game can set comes back,
// and a dt no rate fits is 0.
TIDE_TEST(empty_world_tick_rate_is_one_over_dt)
{
    for (int rate = 1; rate <= 1000; rate++) {
        tide_world_init(&world, 1.0f / (float)rate);
        TIDE_CHECK(world.Time.tickRate == rate && world.Time.t == 0.0f);
        if (world.Time.tickRate != rate) break;
    }
    tide_world_init(&world, 0.3f);
    TIDE_CHECK(world.Time.tickRate == 3);
    tide_world_init(&world, 0.0f);
    TIDE_CHECK(world.Time.tickRate == 0);
    tide_world_init(&world, -1.0f);
    TIDE_CHECK(world.Time.tickRate == 0);
    tide_world_init(&world, 1e-30f);
    TIDE_CHECK(world.Time.tickRate == 0);
    tide_world_free(&world);
}
