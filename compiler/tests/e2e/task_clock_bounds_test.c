#include <stdint.h>
#include "game.h"
#include "tide_test.h"

TIDE_TEST(task_deadlines_accept_negative_clocks_and_saturate)
{
    static tide_world world;
    tide_world_init(&world, 1.0f);
    world.Time.tick = -15;
    tide_world_tick(&world);
    TIDE_CHECK(world.Log.started && !world.Log.finished);
    TIDE_CHECK(world.Time.tick == -14);
    world.Time.tick = 84;
    tide_world_tick(&world);
    TIDE_CHECK(!world.Log.finished);
    tide_world_tick(&world);
    TIDE_CHECK(world.Log.finished);
    tide_world_free(&world);

    tide_world_init(&world, 1.0f);
    world.Time.tick = INT32_MAX - 1;
    tide_world_tick(&world);
    TIDE_CHECK(world.Log.started && !world.Log.finished);
    TIDE_CHECK(world.Time.tick == INT32_MAX);
    tide_world_tick(&world);
    TIDE_CHECK(world.Log.finished);
    TIDE_CHECK(world.Time.tick == INT32_MIN);
    tide_world_tick(&world);
    TIDE_CHECK(world.Time.tick == INT32_MIN + 1);
    tide_world_free(&world);
}
