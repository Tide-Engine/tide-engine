#include <stdlib.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;

// Main is slot 0; Setup spawns bodies 0..5 in slots 1..6.
static tide_entity body(const int id)
{
    return (tide_entity){(uint32_t)id + 1u, 1};
}

static uint32_t archetype_of(const tide_world *w, const int id)
{
    return tide_entity_location(&w->entities, body(id)).archetype;
}

static void ticks(tide_world *w, const int n)
{
    for (int i = 0; i < n; i++) tide_world_tick(w);
}

TIDE_TEST(lasting_tags_move_entities_and_ordinary_tags_do_not)
{
    tide_world_init(&world, 1.0f);
    const uint32_t awake = archetype_of(&world, 0);
    for (int id = 1; id < 6; id++) TIDE_CHECK(archetype_of(&world, id) == awake);

    // Tick 0 steps everyone; tick 1 steps again, then puts the even ones to sleep
    ticks(&world, 2);
    const uint32_t asleep = archetype_of(&world, 0);
    TIDE_CHECK(asleep != awake);
    for (int id = 0; id < 6; id++) {
        TIDE_CHECK(tide_has_Sleeping(&world, body(id)) == (id % 2 == 0));
        TIDE_CHECK(tide_has_Drowsy(&world, body(id)) == (id % 2 == 1));
        // Drowsy is a presence bit: the odd ones stay where they were
        TIDE_CHECK(archetype_of(&world, id) == (id % 2 == 0 ? asleep : awake));
        TIDE_CHECK(tide_get_Body(&world, body(id)).id == id && tide_get_Body(&world, body(id)).steps == 2);
    }

    // Ticks 2 and 3: sleepers nap instead of stepping, and wake at the end of tick 3
    ticks(&world, 2);
    for (int id = 0; id < 6; id++) {
        const Body b = tide_get_Body(&world, body(id));
        TIDE_CHECK(b.id == id);
        TIDE_CHECK(b.steps == (id % 2 == 0 ? 2 : 4));
        TIDE_CHECK(b.naps == (id % 2 == 0 ? 2 : 0));
        TIDE_CHECK(!tide_has_Sleeping(&world, body(id)) && !tide_has_Drowsy(&world, body(id)));
        TIDE_CHECK(archetype_of(&world, id) == awake);
    }

    // Tick 4: all awake again
    ticks(&world, 1);
    for (int id = 0; id < 6; id++) TIDE_CHECK(tide_get_Body(&world, body(id)).steps == (id % 2 == 0 ? 3 : 5));
    TIDE_CHECK(tide_world_entity_count(&world) == 7);
    tide_world_free(&world);
}

// A snapshot and a packed copy taken while bodies sleep play on to the same world.
TIDE_TEST(lasting_tags_survive_snapshots_and_packing)
{
    tide_world_init(&world, 1.0f);
    ticks(&world, 2);
    tide_world copy = {0}, back = {0};
    tide_world_copy(&copy, &world);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes);
    TIDE_REQUIRE(tide_world_pack(&world, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&back, bytes, size));
    free(bytes);
    TIDE_CHECK(tide_world_hash(&back) == tide_world_hash(&world));
    TIDE_CHECK(tide_has_Sleeping(&back, body(2)) && archetype_of(&back, 2) == archetype_of(&world, 2));

    const uint64_t kept = tide_world_hash(&copy);
    ticks(&world, 3);
    TIDE_CHECK(tide_world_hash(&copy) == kept); // The snapshot keeps its sleepers
    TIDE_CHECK(tide_has_Sleeping(&copy, body(4)));
    ticks(&copy, 3);
    ticks(&back, 3);
    TIDE_CHECK(tide_world_hash(&copy) == tide_world_hash(&world));
    TIDE_CHECK(tide_world_hash(&back) == tide_world_hash(&world));
    TIDE_CHECK(!tide_has_Sleeping(&copy, body(4)) && tide_get_Body(&copy, body(4)).naps == 2);
    tide_world_free(&back);
    tide_world_free(&copy);
    tide_world_free(&world);
}
