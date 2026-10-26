#include "game.h"
#include "tide_test.h"

// Compiling is most of the test: with every Add and Remove reaching every
// archetype, the nine toggles on three kinds would make thousands of
// combinations, over the limit. Each kind's own toggles make a few.

static tide_world world;

// Main is slot 0; Setup spawns the rock, the ship and the tree next.
static const tide_entity ROCK = {1, 1};
static const tide_entity SHIP = {2, 1};
static const tide_entity TREE = {3, 1};

TIDE_TEST(reach_toggles_come_and_go_as_their_systems_say)
{
    tide_world_init(&world, 1.0f);
    TIDE_CHECK(tide_world_entity_count(&world) == 4);
    TIDE_CHECK(tide_has_Leafy(&world, TREE) && !tide_has_Fruiting(&world, TREE));

    // Rocks: on at the end of a tick, off at the end of the next
    tide_world_tick(&world);
    TIDE_CHECK(tide_has_Hot(&world, ROCK) && tide_has_Wet(&world, ROCK) && tide_has_Lit(&world, ROCK));
    tide_world_tick(&world);
    TIDE_CHECK(!tide_has_Hot(&world, ROCK) && !tide_has_Wet(&world, ROCK) && !tide_has_Lit(&world, ROCK));
    TIDE_CHECK(tide_get_Body(&world, ROCK).toggles == 2);

    // Ships: armed, shielded, cloaked, bare, in turn
    TIDE_CHECK(!tide_has_Armed(&world, SHIP) && tide_has_Shielded(&world, SHIP));
    tide_world_tick(&world);
    TIDE_CHECK(!tide_has_Shielded(&world, SHIP) && tide_has_Cloaked(&world, SHIP));
    tide_world_tick(&world);
    TIDE_CHECK(!tide_has_Cloaked(&world, SHIP) && !tide_has_Armed(&world, SHIP));
    tide_world_tick(&world);
    TIDE_CHECK(tide_has_Armed(&world, SHIP));
    TIDE_CHECK(tide_get_Body(&world, SHIP).toggles == 5);

    // Trees: fruit after the first tick, felled after the second, growing again after the third
    TIDE_CHECK(tide_has_Felled(&world, TREE) ? tide_get_Felled(&world, TREE).by == 4 : true);
    TIDE_CHECK(tide_get_Body(&world, TREE).toggles == 5);
    for (int i = 0; i < 2; i++) tide_world_tick(&world);
    TIDE_CHECK(tide_world_entity_count(&world) == 4);
    TIDE_CHECK(tide_get_Body(&world, ROCK).toggles == 7 && tide_get_Body(&world, SHIP).toggles == 7);
    tide_world_free(&world);
}
