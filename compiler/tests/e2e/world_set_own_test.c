#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_world shared;
static tide_world kept;

// What every counter holds, added up.
static int64_t total(const tide_world *w, int64_t *seen)
{
    int64_t sum = 0;
    *seen = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (!tide_has_Counter(w, e)) continue;
        const Counter c = tide_get_Counter(w, e);
        sum += c.value;
        *seen += c.seen;
    }
    return sum;
}

TIDE_TEST(world_sets_this_entitys_own_beside_a_read_parameter)
{
    tide_world_init(&world, 1.0f);
    for (int t = 0; t < 20; t++) tide_world_tick(&world);
    // Every counter climbed to its target a tick at a time, each tick's Set
    // going on from the one before
    int64_t targets = 0;
    int slow = 0;
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Counter(&world, e)) continue;
        const Counter c = tide_get_Counter(&world, e);
        if (tide_has_Slow(&world, e)) {
            // The handler went on raising its target, through its own Set
            TIDE_CHECK(c.target > 3 && c.value >= c.target - 1);
            slow = c.value;
        } else {
            TIDE_CHECK(c.value == c.target);
            targets += c.target;
        }
    }
    int64_t expected = 0;
    for (int i = 0; i < 5000; i++) expected += i % 7;
    TIDE_CHECK(targets == expected && slow > 3);
    int64_t seen;
    TIDE_CHECK(total(&world, &seen) == expected + slow);
    TIDE_CHECK(seen == expected + slow);           // Get after the Set had what was set
    TIDE_CHECK(world.Tally.set == expected + slow); // One Set for each step up
    TIDE_CHECK(world.Tally.stale == 0);            // The parameter didn't follow the Set
    tide_world_free(&world);
}

// Whether a snapshot shares the world's pages makes no difference: with one,
// a Set gives the page a copy of its own while the system goes on reading the
// rest of its rows where they were.
TIDE_TEST(world_set_own_is_the_same_under_a_snapshot)
{
    tide_world_init(&world, 1.0f);
    tide_world_init(&shared, 1.0f);
    for (int t = 0; t < 12; t++) {
        tide_world_tick(&world);
        tide_world_copy(&kept, &shared);
        const uint64_t before = tide_world_hash(&kept);
        tide_world_tick(&shared);
        TIDE_REQUIRE(tide_world_hash(&world) == tide_world_hash(&shared));
        TIDE_REQUIRE(tide_world_hash(&kept) == before); // And the snapshot is as it was
    }
    tide_world_free(&world);
    tide_world_free(&shared);
    tide_world_free(&kept);
}
