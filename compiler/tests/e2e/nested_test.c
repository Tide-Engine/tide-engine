#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_world copy;
static tide_local local;

// Whether two worlds hold the same: the same bytes (tide_world_pack, which
// leaves out where their pages are), and the same hash.
static bool same(const tide_world *a, const tide_world *b)
{
    const uint32_t size = tide_world_pack(a, NULL, 0);
    if (tide_world_pack(b, NULL, 0) != size) return false;
    uint8_t *x = malloc(size);
    uint8_t *y = malloc(size);
    tide_world_pack(a, x, size);
    tide_world_pack(b, y, size);
    const bool equal = memcmp(x, y, size) == 0;
    free(x);
    free(y);
    return equal && tide_world_hash(a) == tide_world_hash(b);
}

static const tide_entity grid_entity = {1, 1};

TIDE_TEST(nested_all_checks_pass)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_world_tick(&world); // The event's handler runs at the end of the tick it's sent in: the first; a second tick settles it
    TIDE_REQUIRE(tide_has_Results(&world, grid_entity));
    const Results r = tide_get_Results(&world, grid_entity);
    if (r.firstFailure) printf("    check %d failed\n", (int)r.firstFailure);
    const tide_str shown = tide_text_read(&world.heap, r.shown);
    printf("    shown: %.*s\n", (int)shown.bytes, shown.ptr);
    printf("    checks: %d\n", (int)r.checks);
    TIDE_CHECK(r.checks == 27);
    TIDE_CHECK(r.passed == r.checks);
    TIDE_CHECK(tide_scratch_mark() == 0);
}

// Churning the nested lists for a while takes no more memory: removed and
// cleared elements let their lists go, and the heap hands their blocks out again.
TIDE_TEST(nested_lists_reuse_their_memory)
{
    tide_world_init(&world, 1.0f);
    for (int i = 0; i < 60; i++) tide_world_tick(&world);
    const uint64_t used = world.heap.used;
    for (int i = 0; i < 400; i++) tide_world_tick(&world);
    TIDE_CHECK(world.heap.used == used);
}

TIDE_TEST(nested_snapshot_and_repeat)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_world_copy(&copy, &world);
    for (int i = 0; i < 12; i++) {
        tide_world_tick(&world);
        tide_world_tick(&copy);
    }
    TIDE_CHECK(same(&world, &copy));
    tide_world_free(&copy);
}

// The world as bytes and back: the same lists in the same blocks.
TIDE_TEST(nested_pack_and_unpack)
{
    tide_world_init(&world, 1.0f);
    for (int i = 0; i < 9; i++) tide_world_tick(&world);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    tide_world_pack(&world, bytes, size);
    memset(&copy, 0, sizeof copy);
    TIDE_REQUIRE(tide_world_unpack(&copy, bytes, size));
    free(bytes);
    TIDE_CHECK(same(&world, &copy));
    for (int i = 0; i < 5; i++) {
        tide_world_tick(&world);
        tide_world_tick(&copy);
    }
    TIDE_CHECK(same(&world, &copy));
    tide_world_free(&copy);
}

// The same bytes on every platform: the hash after a while of churn.
TIDE_TEST(nested_crossplatform_hash)
{
    tide_world_init(&world, 1.0f);
    for (int i = 0; i < 30; i++) tide_world_tick(&world);
    const uint64_t hash = tide_world_hash(&world);
    printf("    hash: %llu\n", (unsigned long long)hash);
    TIDE_CHECK(hash == NESTED_HASH);
}

// A view reads nested lists of the world it draws, and of the one the server
// confirmed, where their elements come as copies whose lists say which heap
// they're in.
TIDE_TEST(nested_views_read_both_worlds)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_world_tick(&world); // Checks it: [[0, 2], [1, 3, 9]], and two bags of 3 and 1 parts
    tide_world_copy(&copy, &world);
    for (int i = 0; i < 3; i++) tide_world_tick(&world); // The confirmed world stays a few ticks behind
    tide_local_init(&local);
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_frame(&world, NULL, 1.0f, &copy, NULL, 1.0f, &local, &draw, &gui);
    const Mirror m = local.Mirror;
    // The drawn world's rows, with what Churn added, and the confirmed world's as it was after the first tick: [[0, 2], [1, 3, 9]]
    printf("    seen %d, verified %d\n", (int)m.seenInView, (int)m.verifiedTotal);
    printf("    confirmed rows %d, first %d, bags %d\n", (int)m.confirmedRows, (int)m.confirmedFirst, (int)m.confirmedBags);
    TIDE_CHECK(m.seenInView > 15);
    TIDE_CHECK(m.verifiedTotal == 15 + 3 + 1);
    TIDE_CHECK(m.confirmedRows == 2 && m.confirmedFirst == 2 && m.confirmedBags == 2);
    TIDE_CHECK(tide_list_read_count(&local.heap, m.rows) >= 2);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_local_free(&local);
    tide_world_free(&copy);
}
