#include "game.h"
#include "tide_test.h"

static tide_world world;

TIDE_TEST(scenes_switch_unloads_the_loaded_scenes_and_loads_one)
{
    tide_world_init(&world, 1.0f);
    TIDE_CHECK(world.Stats.mains == 1);
    TIDE_CHECK(tide_world_entity_count(&world) == 1);
    tide_world_tick(&world);
    TIDE_CHECK(world.Stats.huds == 1);
    TIDE_CHECK(tide_world_entity_count(&world) == 3); // Main, the HUD a function loaded, and the wall

    // A function's switch: Main and the HUD go, the arena loads with its props, and the wall stays
    tide_world_tick(&world);
    TIDE_CHECK(world.Stats.arenas == 1 && world.Stats.round == 1);
    TIDE_CHECK(world.Stats.hudsGone == 1);
    TIDE_CHECK(world.Stats.mains == 1); // Never without a scene, so Main didn't come back
    TIDE_CHECK(tide_world_entity_count(&world) == 4);

    // A HUD loaded earlier in the tick goes with the first arena
    tide_world_tick(&world);
    TIDE_CHECK(world.Stats.arenas == 2 && world.Stats.round == 2);
    TIDE_CHECK(world.Stats.huds == 2 && world.Stats.hudsGone == 2);
    TIDE_CHECK(world.Stats.propsGone == 2);
    TIDE_CHECK(world.Stats.mains == 1);
    TIDE_CHECK(tide_world_entity_count(&world) == 4);

    // A function's unload of the last scene: Main loads again
    tide_world_tick(&world);
    TIDE_CHECK(world.Stats.propsGone == 4);
    TIDE_CHECK(world.Stats.mains == 2);
    TIDE_CHECK(tide_world_entity_count(&world) == 2); // Main and the wall
}

TIDE_TEST(scenes_switch_is_the_same_from_a_snapshot)
{
    tide_world a = {0}, b = {0};
    tide_world_init(&a, 1.0f);
    tide_world_tick(&a);
    tide_world_tick(&a);
    tide_world_copy(&b, &a);
    tide_world_tick(&a);
    tide_world_tick(&b);
    TIDE_CHECK(tide_world_hash(&a) == tide_world_hash(&b));
    TIDE_CHECK(b.Stats.arenas == 2 && tide_world_entity_count(&b) == 4);
    tide_world_free(&a);
    tide_world_free(&b);
}
