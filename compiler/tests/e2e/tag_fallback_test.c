#include "game.h"
#include "tide_test.h"

static tide_world world;

// Main is slot 0; Setup spawns the picked marker, then the other.
static const tide_entity PICKED = {1, 1};
static const tide_entity OTHER = {2, 1};

static uint32_t archetype_of(const tide_entity e)
{
    return tide_entity_location(&world.entities, e).archetype;
}

TIDE_TEST(tag_fallback_keeps_every_tag_within_the_limit)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world); // Tick 0
    tide_world_tick(&world); // Tick 1: every tag
    TIDE_CHECK(tide_has_T0(&world, PICKED) && tide_has_T5(&world, PICKED) && tide_has_T11(&world, PICKED));
    TIDE_CHECK(!tide_has_T0(&world, OTHER) && !tide_has_T11(&world, OTHER));
    const uint32_t before = archetype_of(OTHER);
    tide_world_tick(&world); // Tick 2: the other marker is near, which moves it
    TIDE_CHECK(tide_has_Near(&world, OTHER) && archetype_of(OTHER) != before);
    tide_world_tick(&world); // Tick 3: the even tags go
    TIDE_CHECK(!tide_has_T0(&world, PICKED) && tide_has_T1(&world, PICKED) && !tide_has_T10(&world, PICKED));
    TIDE_CHECK(tide_has_T11(&world, PICKED) && tide_get_Marker(&world, PICKED).id == 1);
    TIDE_CHECK(tide_world_entity_count(&world) == 3);
    tide_world_free(&world);
}
