#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_world copy;

static const tide_entity THING = {1, 1};

// Whether two worlds hold the same: the same bytes and the same hash.
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

TIDE_TEST(maps_all_checks_pass)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    const Results r = tide_get_Results(&world, THING);
    if (r.passed != r.checks) printf("first failed check: %d\n", r.firstFailure);
    TIDE_CHECK(r.checks > 0 && r.passed == r.checks);
    const tide_str shown = tide_text_read(&world.heap, r.shown);
    const char *expected = "{8: 8} {1, 2, 7} {\"ann\": Stats { title = \"captain\", wins = 3 }, \"bob\": Stats { title = \"rookie!\", wins = 7 }}";
    TIDE_CHECK(shown.bytes == (int32_t)strlen(expected) && memcmp(shown.ptr, expected, (size_t)shown.bytes) == 0);
    if (shown.bytes != (int32_t)strlen(expected) || memcmp(shown.ptr, expected, (size_t)shown.bytes) != 0) printf("shown: %.*s\n", (int)shown.bytes, shown.ptr);
    tide_world_free(&world);
}

// Maps live in the heap like lists: snapshots share and copy pages, and the
// same ticks give the same bytes and hashes.
TIDE_TEST(maps_snapshot_and_hash_like_the_rest_of_the_world)
{
    tide_world_init(&world, 1.0f);
    tide_world_init(&copy, 1.0f);
    for (int i = 0; i < 20; i++) {
        tide_world_tick(&world);
        tide_world_tick(&copy);
    }
    TIDE_CHECK(same(&world, &copy));
    tide_world snapshot = {0};
    tide_world_copy(&snapshot, &world);
    tide_world_tick(&world);
    TIDE_CHECK(!same(&world, &snapshot));
    tide_world_tick(&copy);
    TIDE_CHECK(same(&world, &copy));
    tide_world_free(&snapshot);
    tide_world_free(&world);
    tide_world_free(&copy);
}
