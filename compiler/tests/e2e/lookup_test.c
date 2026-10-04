#include "game.h"
#include "tide_test.h"

static tide_world world;

// Main is slot 0; Setup spawns a, b, three attackers and the healer.
static const tide_entity A = {1, 1};
static const tide_entity B = {2, 1};
static const tide_entity ATTACKER_A = {3, 1};
static const tide_entity ATTACKER_B = {4, 1};
static const tide_entity IDLE = {5, 1};

// Each tick: Attack sends hits (applied at the end), Heal gives b 2 at once,
// then Spread runs for a then b, each reading the other's Health as it is
// right then and adding 1 to its own; the hits land last.
TIDE_TEST(lookup_reads_and_changes_other_entities)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    // a: 7, +1, -3 = 5. b: 20, +2 (healed), +1, -1 = 22.
    TIDE_CHECK(tide_get_Health(&world, A).value == 5);
    TIDE_CHECK(tide_get_Health(&world, B).value == 22);
    // Spread saw b at 22 from a, then a at 8 from b: the row a changed in this run, in place
    TIDE_CHECK(world.Stats.seen == 30);
    TIDE_CHECK(tide_get_Attacker(&world, ATTACKER_A).hits == 1 && tide_get_Attacker(&world, IDLE).misses == 1);
    TIDE_CHECK(world.Stats.kills == 0);

    tide_world_tick(&world); // a 3, b 24, seen 60
    tide_world_tick(&world); // a at 3 counts as a kill before the hit; it ends at 1
    TIDE_CHECK(world.Stats.kills == 1);
    TIDE_CHECK(tide_get_Health(&world, A).value == 1 && tide_get_Health(&world, B).value == 26);
    TIDE_CHECK(world.Stats.seen == 90);

    tide_world_tick(&world); // Another kill, and a is gone
    TIDE_CHECK(world.Stats.kills == 2);
    TIDE_CHECK(!tide_has_Health(&world, A) && !tide_has_Rival(&world, A));
    TIDE_CHECK(tide_get_Attacker(&world, ATTACKER_A).hits == 4);

    tide_world_tick(&world); // a's attacker misses; a lookup of the gone a gives zeros
    TIDE_CHECK(tide_get_Attacker(&world, ATTACKER_A).misses == 1 && tide_get_Attacker(&world, ATTACKER_B).hits == 5);
    TIDE_CHECK(tide_get_Health(&world, B).value == 30);
    TIDE_CHECK(world.Stats.seen == 120);
    tide_world_free(&world);
}
