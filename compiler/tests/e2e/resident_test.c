#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/resident.h"
#include "tide_test.h"

// Resident state in a world: a stand-in library (resident_c.c) keeps eight
// counters in the engine's arena, and the world keeps the pages. What the
// host does to a world (snapshots, rollbacks, packing, deltas) carries the
// state with it, and the match's hash covers the digest and nothing else of it.

int64_t resident_test_cell(const tide_resident *r, int i);
tide_arena *resident_test_arena(void);
int resident_test_packs(void);
int resident_test_unpacks(void);

static tide_world world;
static tide_world snapshot;
static tide_world twin;
static tide_world again;

static uint64_t tick(tide_world *w)
{
    tide_world_tick(w);
    return tide_world_hash(w);
}

static bool same_cells(const tide_resident *a, const tide_resident *b)
{
    for (int i = 0; i < 8; i++) {
        if (resident_test_cell(a, i) != resident_test_cell(b, i)) return false;
    }
    return true;
}

TIDE_TEST(resident_state_rides_in_the_world_and_rolls_back_with_it)
{
    tide_world_init(&world, 1.0f / 60.0f);
    uint64_t hashes[8];
    for (int t = 0; t < 2; t++) hashes[t] = tick(&world);
    TIDE_CHECK(world.Tally.steps == 2);
    TIDE_CHECK(world.Tally.state.kind == tide_resident_kind_of("tally"));
    TIDE_CHECK(world.Tally.state.stamp == 2 && world.Tally.state.pages != NULL);
    TIDE_CHECK((int64_t)world.Tally.state.digest == world.Tally.seen);
    TIDE_CHECK(hashes[0] != hashes[1]);
    TIDE_CHECK(tide_arena_stamp(resident_test_arena()) == 2);

    // A snapshot shares the pages, and hashes the same
    tide_world_copy(&snapshot, &world);
    TIDE_CHECK(snapshot.Tally.state.pages == world.Tally.state.pages && world.Tally.state.pages->refs == 2);
    TIDE_CHECK(tide_world_hash(&snapshot) == hashes[1]);

    // On to tick 5: the world's pages part from the snapshot's as they're committed into
    for (int t = 2; t < 5; t++) hashes[t] = tick(&world);
    TIDE_CHECK(snapshot.Tally.state.pages != world.Tally.state.pages && snapshot.Tally.state.pages->refs == 1);
    TIDE_CHECK(snapshot.Tally.state.stamp == 2 && world.Tally.state.stamp == 5);
    int64_t at_five = 0, at_two = 0;
    for (int i = 0; i < 8; i++) at_five += resident_test_cell(&world.Tally.state, i);
    for (int i = 0; i < 8; i++) at_two += resident_test_cell(&snapshot.Tally.state, i); // Restores the arena to the snapshot's state
    TIDE_CHECK(tide_arena_stamp(resident_test_arena()) == 2);
    TIDE_CHECK(at_two < at_five); // Three steps of counting fewer

    // A rollback: the snapshot ticks on from where it was, and comes out as the world did
    TIDE_CHECK(tick(&snapshot) == hashes[2]);
    TIDE_CHECK(snapshot.Tally.state.stamp == 3);
    // ...and the world, at tick 5, goes on from its own state, which the arena no longer held
    hashes[5] = tick(&world);
    TIDE_CHECK(world.Tally.state.stamp == 6);
    tide_world_init(&twin, 1.0f / 60.0f);
    for (int t = 0; t < 6; t++) tick(&twin);
    TIDE_CHECK(tide_world_hash(&twin) == hashes[5]);
    TIDE_CHECK(same_cells(&twin.Tally.state, &world.Tally.state));

    tide_world_free(&snapshot);
    tide_world_free(&twin);
    TIDE_CHECK(world.Tally.state.pages->refs == 1);
    tide_world_free(&world);
}

// Packed and unpacked, a world has the state again through the library's
// canonical bytes, with nothing of the pages in them, and the same hash.
TIDE_TEST(resident_state_is_packed_as_canonical_bytes)
{
    tide_world_init(&world, 1.0f / 60.0f);
    for (int t = 0; t < 4; t++) tick(&world);
    const uint64_t hash = tide_world_hash(&world);
    const int packs = resident_test_packs();
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes);
    TIDE_CHECK(tide_world_pack(&world, bytes, size) == size);
    TIDE_CHECK(resident_test_packs() == packs + 1); // Sized and written with one call of the library's
    // The pages' pointer isn't in the bytes
    tide_world header;
    memcpy(&header, bytes, sizeof header);
    TIDE_CHECK(header.Tally.state.pages == NULL && header.Tally.state.stamp == 4);

    TIDE_REQUIRE(tide_world_unpack(&again, bytes, size));
    TIDE_CHECK(again.Tally.state.pages != NULL && again.Tally.state.pages != world.Tally.state.pages);
    TIDE_CHECK(again.Tally.state.stamp == 4 && again.Tally.state.digest == world.Tally.state.digest);
    TIDE_CHECK(tide_world_hash(&again) == hash);
    TIDE_CHECK(same_cells(&again.Tally.state, &world.Tally.state));
    // Both go on the same
    TIDE_CHECK(tick(&again) == tick(&world));
    TIDE_CHECK(same_cells(&again.Tally.state, &world.Tally.state));
    free(bytes);

    // Bytes cut short are refused, and leave nothing behind
    const uint32_t whole = tide_world_pack(&world, NULL, 0);
    bytes = malloc(whole);
    TIDE_REQUIRE(bytes);
    tide_world_pack(&world, bytes, whole);
    TIDE_CHECK(!tide_world_unpack(&twin, bytes, whole - 20u));
    TIDE_CHECK(twin.Tally.state.pages == NULL);
    free(bytes);
    tide_world_free(&again);
    tide_world_free(&world);
}

// As a delta, from nothing and from a base: the bytes go whole either way.
TIDE_TEST(resident_state_goes_whole_in_a_delta)
{
    tide_world_init(&world, 1.0f / 60.0f);
    for (int t = 0; t < 3; t++) tick(&world);
    tide_world_copy(&snapshot, &world);
    for (int t = 0; t < 3; t++) tick(&world);
    const uint64_t hash = tide_world_hash(&world);

    uint32_t size;
    uint8_t *delta = tide_world_pack_delta(&world, NULL, NULL, 0, &size);
    TIDE_REQUIRE(delta);
    TIDE_REQUIRE(tide_world_unpack_delta(&again, NULL, delta, size));
    TIDE_CHECK(tide_world_hash(&again) == hash && same_cells(&again.Tally.state, &world.Tally.state));
    free(delta);
    tide_world_free(&again);

    delta = tide_world_pack_delta(&world, &snapshot, NULL, 0, &size);
    TIDE_REQUIRE(delta);
    TIDE_REQUIRE(tide_world_unpack_delta(&again, &snapshot, delta, size));
    TIDE_CHECK(tide_world_hash(&again) == hash && same_cells(&again.Tally.state, &world.Tally.state));
    TIDE_CHECK(again.Tally.state.pages != snapshot.Tally.state.pages);
    free(delta);
    tide_world_free(&again);
    tide_world_free(&snapshot);
    tide_world_free(&world);
}
