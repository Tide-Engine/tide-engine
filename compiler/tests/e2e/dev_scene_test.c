#include <string.h>

#include "game.h"
#include "tide_test.h"

// Built with --scene "Battle { side = 100, alone = true }" (dev_scene) or --scene Battle (dev_scene_named): the match
// starts in the dev scene, with those fields or its defaults, and Main never loads.

TIDE_TEST(dev_scene_starts)
{
    static tide_world world;
    tide_world_init(&world, 1.0f / 60.0f);
    for (int i = 0; i < 3; i++) tide_world_tick(&world);
    const Started *s = &world.Started;
    TIDE_CHECK(s->mains == 0);
    TIDE_CHECK(s->battles == 1);
#ifdef NAMED_ONLY
    TIDE_CHECK(s->side == 500 && !s->alone); // Its defaults
#else
    TIDE_CHECK(s->side == 100 && s->alone);
#endif
    uint32_t soldiers = 0;
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        if (tide_has_Soldier(&world, tide_entity_in_slot(&world.entities, i))) soldiers++;
    }
    TIDE_CHECK(soldiers == (uint32_t)(s->side / 50));
    tide_world_free(&world);
}
