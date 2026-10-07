#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;

static bool reads(const tide_text t, const char *expected)
{
    const tide_str s = tide_text_read(&world.heap, t);
    return s.bytes == (int32_t)strlen(expected) && memcmp(s.ptr, expected, strlen(expected)) == 0;
}

// Main is slot 0; Setup spawns a, b, three attackers, the healer and two watchers.
static const tide_entity A = {1, 1};
static const tide_entity B = {2, 1};
static const tide_entity ATTACKER_A = {3, 1};
static const tide_entity ATTACKER_B = {4, 1};
static const tide_entity IDLE = {5, 1};
static const tide_entity WATCHES_A = {7, 1};
static const tide_entity WATCHES_B = {8, 1};

// Each tick: Attack sends hits (applied at the end), Heal gives b 2 at once,
// then Spread runs for a then b, each reading the other's Health as it is
// right then and adding 1 to its own; the hits land last.
TIDE_TEST(world_reads_and_changes_other_entities)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    // a: 7, +1, -3 = 5. b: 20, +2 (healed), +1, -1 = 22.
    TIDE_CHECK(tide_get_Health(&world, A).value == 5);
    TIDE_CHECK(tide_get_Health(&world, B).value == 22);
    // Heal's Set put its text in the world, b's own; a's is as it was spawned
    TIDE_CHECK(reads(tide_get_Health(&world, B).name, "healed to 22") && reads(tide_get_Health(&world, A).name, "a"));
    // Spread saw b at 22 from a, then a at 8 from b: the row a changed in this run, in place
    TIDE_CHECK(world.Stats.seen == 30);
    TIDE_CHECK(tide_get_Attacker(&world, ATTACKER_A).hits == 1 && tide_get_Attacker(&world, IDLE).misses == 1);
    TIDE_CHECK(world.Stats.kills == 0);
    // Watch saw a at 8 and b at 23, before the hits: '<' and '>' around a type in <> still compare
    TIDE_CHECK(!tide_get_Watcher(&world, WATCHES_A).ahead && tide_get_Watcher(&world, WATCHES_A).behind);
    TIDE_CHECK(tide_get_Watcher(&world, WATCHES_B).ahead && !tide_get_Watcher(&world, WATCHES_B).behind);

    tide_world_tick(&world); // a 3, b 24, seen 60
    // Count's task, started by a's first hit with a at 5, went on a tick later and found it lower
    TIDE_CHECK(world.Stats.slow == 1);
    tide_world_tick(&world); // a at 3 counts as a kill before the hit; it ends at 1
    TIDE_CHECK(world.Stats.kills == 1);
    TIDE_CHECK(tide_get_Health(&world, A).value == 1 && tide_get_Health(&world, B).value == 26);
    TIDE_CHECK(world.Stats.seen == 90);
    TIDE_CHECK(world.Stats.slow == 2);

    tide_world_tick(&world); // Another kill, and a is gone
    TIDE_CHECK(world.Stats.kills == 2);
    TIDE_CHECK(!tide_has_Health(&world, A) && !tide_has_Rival(&world, A));
    TIDE_CHECK(tide_get_Attacker(&world, ATTACKER_A).hits == 4);

    tide_world_tick(&world); // a's attacker misses; the gone a's Health reads as zeros
    TIDE_CHECK(tide_get_Attacker(&world, ATTACKER_A).misses == 1 && tide_get_Attacker(&world, ATTACKER_B).hits == 5);
    TIDE_CHECK(tide_get_Health(&world, B).value == 30);
    TIDE_CHECK(reads(tide_get_Health(&world, B).name, "healed to 30"));
    TIDE_CHECK(world.Stats.seen == 120);
    TIDE_CHECK(tide_get_Watcher(&world, WATCHES_B).ahead && !tide_get_Watcher(&world, WATCHES_A).ahead);
    // The tasks a's last hits started ended with it: a task goes when its entity does
    TIDE_CHECK(world.Stats.slow == 2);
    tide_world_free(&world);
}
