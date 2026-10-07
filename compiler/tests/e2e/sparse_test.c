#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;

// Main is slot 0; Setup spawns the stunned rock, the rock, the wet body and
// the body stunned through Add, in that order.
static const tide_entity STUNNED_ROCK = {1, 1};
static const tide_entity ROCK = {2, 1};
static const tide_entity WET = {3, 1};
static const tide_entity ADDED = {4, 1};

static void ticks(const int n)
{
    for (int i = 0; i < n; i++) tide_world_tick(&world);
}

TIDE_TEST(sparse_components_come_and_go_in_place)
{
    tide_world_init(&world, 1.0f);
    // Both rocks are one archetype (Body, Rock): Stunned and Burning are rows' own
    TIDE_CHECK(tide_world_entity_count(&world) == 5);
    TIDE_CHECK(tide_has_Stunned(&world, STUNNED_ROCK) && !tide_has_Stunned(&world, ROCK));
    TIDE_CHECK(tide_has_Stunned(&world, ADDED) && tide_get_Stunned(&world, ADDED).left == 1);
    TIDE_CHECK(tide_get_Stunned(&world, STUNNED_ROCK).left == 2);
    TIDE_CHECK(strcmp(tide_text_read(&world.heap, tide_get_Stunned(&world, STUNNED_ROCK).by).ptr, "setup") == 0);
    TIDE_CHECK(!tide_has_Burning(&world, ROCK) && !tide_has_Burning(&world, STUNNED_ROCK));
    const uint64_t fresh = tide_world_hash(&world);

    // Tick 0: the stunned ones count down (one recovers), the others step; nothing burns yet
    ticks(1);
    TIDE_CHECK(tide_get_Stunned(&world, STUNNED_ROCK).left == 1 && tide_get_Body(&world, STUNNED_ROCK).steps == 0);
    TIDE_CHECK(!tide_has_Stunned(&world, ADDED) && tide_get_Body(&world, ADDED).hits == 1);
    TIDE_CHECK(tide_get_Body(&world, ROCK).steps == 1 && tide_get_Body(&world, WET).steps == 1);
    TIDE_CHECK(!tide_has_Burning(&world, ROCK));

    // Tick 1: the rocks catch fire; the last stunned one recovers
    ticks(1);
    TIDE_CHECK(tide_has_Burning(&world, ROCK) && tide_has_Burning(&world, STUNNED_ROCK));
    TIDE_CHECK(!tide_has_Stunned(&world, STUNNED_ROCK) && tide_get_Body(&world, STUNNED_ROCK).hits == 2);
    TIDE_CHECK(tide_get_Body(&world, ADDED).steps == 1);

    // Tick 2: fires go out; the wet body slips, its text new
    ticks(1);
    TIDE_CHECK(!tide_has_Burning(&world, ROCK));
    TIDE_CHECK(tide_has_Stunned(&world, WET));
    TIDE_CHECK(strcmp(tide_text_read(&world.heap, tide_get_Stunned(&world, WET).by).ptr, "slip 2") == 0);
    TIDE_CHECK(tide_get_Body(&world, WET).steps == 3);

    // Tick 3: it recovers, and its text goes with it
    ticks(1);
    TIDE_CHECK(!tide_has_Stunned(&world, WET) && tide_get_Stunned(&world, WET).left == 0);
    TIDE_CHECK(tide_set_Stunned(&world, WET, (Stunned){.left = 5}) == false); // Not there to set

    // Everything in place: the same entities as at the start, in the same archetypes
    TIDE_CHECK(tide_world_entity_count(&world) == 5);
    TIDE_CHECK(tide_world_hash(&world) != fresh);
    tide_world_free(&world);
}

// Setting a sparse component through the host's API only works where the row has it.
TIDE_TEST(sparse_set_and_get_follow_presence)
{
    tide_world_init(&world, 1.0f);
    TIDE_CHECK(tide_set_Stunned(&world, ROCK, (Stunned){.left = 9}) == false);
    TIDE_CHECK(tide_get_Stunned(&world, ROCK).left == 0);
    TIDE_CHECK(tide_set_Stunned(&world, STUNNED_ROCK, (Stunned){.left = 9, .by = tide_get_Stunned(&world, STUNNED_ROCK).by}));
    TIDE_CHECK(tide_get_Stunned(&world, STUNNED_ROCK).left == 9);
    tide_world_free(&world);
}
