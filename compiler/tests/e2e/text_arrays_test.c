#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_world copy;

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

static bool reads(const tide_world *w, const tide_text t, const char *expected)
{
    const tide_str s = tide_text_read(&w->heap, t);
    return s.bytes == (int32_t)strlen(expected) && memcmp(s.ptr, expected, strlen(expected)) == 0;
}

static bool has(const tide_world *w, const tide_text t, const char *part)
{
    const tide_str s = tide_text_read(&w->heap, t);
    const size_t n = strlen(part);
    for (int32_t i = 0; i + (int32_t)n <= s.bytes; i++) {
        if (memcmp(s.ptr + i, part, n) == 0) return true;
    }
    return false;
}

static int32_t int_at(const tide_list l, const int32_t i)
{
    const int32_t *p = tide_list_at(l, i, sizeof(int32_t));
    return p ? *p : -1;
}

TIDE_TEST(text_arrays_keep_their_own_text)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    const tide_entity fox = {1, 1}, cat = {2, 1};
    TIDE_REQUIRE(tide_has_Named(&world, fox) && tide_has_Named(&world, cat));
    const Named a = tide_get_Named(&world, fox);
    const Named b = tide_get_Named(&world, cat);
    TIDE_CHECK(reads(&world, a.names.at[0], "foxs") && reads(&world, a.names.at[1], "owls"));
    TIDE_CHECK(reads(&world, b.names.at[0], "cats") && reads(&world, b.names.at[1], "dogs")); // From the field's default
    TIDE_CHECK(reads(&world, a.tags.at[0].label, "foxs!"));                                  // A struct element's field, written where it is
    TIDE_CHECK(reads(&world, a.tags.at[1].label, "owls") && a.tags.at[1].level == 1);     // A struct element, written whole
    TIDE_CHECK(tide_list_count(a.counts.at[0]) == 1 && int_at(a.counts.at[0], 0) == 0);    // A list element, added to
    TIDE_CHECK(tide_list_count(a.counts.at[1]) == 3 && int_at(a.counts.at[1], 2) == 3);    // ...and assigned
    TIDE_CHECK(world.Log.kept == 2);
    TIDE_CHECK(world.Log.ticks == 1);
    TIDE_CHECK(reads(&world, world.Log.last.at[1], "tick 1") && reads(&world, world.Log.last.at[0], ""));
    TIDE_CHECK(reads(&world, world.Log.grid.at[0].at[1], "b") && reads(&world, world.Log.grid.at[1].at[0], "cc"));
    TIDE_CHECK(tide_list_count(world.Log.pairs) == 1);
    TIDE_CHECK(has(&world, world.Log.shown, "tick 1"));
    TIDE_CHECK(reads(&world, world.Log.joined, "ab"));
    TIDE_CHECK(reads(&world, world.Log.outside, "") && reads(&world, world.Log.grid.at[0].at[0], "a")); // Nothing past the ends
    TIDE_CHECK(world.heap.pending == 0);
    // Forward structs, nested arrays in a list, T? fields
    TIDE_CHECK(tide_list_count(world.Store.shelf.bags.at[0].parts) == 1);
    TIDE_CHECK(reads(&world, world.Store.shelf.bags.at[0].label.text, "bag 0"));
    TIDE_CHECK(reads(&world, world.Store.shelf.bags.at[1].label.text, "second"));
    TIDE_CHECK(tide_list_count(world.Store.crates) == 1);

    tide_world_tick(&world);
    tide_world_tick(&world);
    TIDE_CHECK(!tide_has_Named(&world, cat)); // "dogsss": destroyed, its text released
    TIDE_CHECK(world.Log.heard >= 4);
    TIDE_CHECK(reads(&world, world.Log.grid.at[1].at[0], "cccc"));
    TIDE_CHECK(tide_list_count(world.Log.pairs) == 3);
}

TIDE_TEST(text_arrays_snapshot_and_repeat)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_world_copy(&copy, &world); // A snapshot, heap and all
    for (int i = 0; i < 6; i++) {
        tide_world_tick(&world);
        tide_world_tick(&copy);
    }
    TIDE_CHECK(same(&world, &copy));
}

TIDE_TEST(text_arrays_reuse_their_memory)
{
    // Once the names stop growing, each tick's new text reuses what was released
    tide_world_init(&world, 1.0f);
    for (int i = 0; i < 50; i++) tide_world_tick(&world);
    const uint32_t used = world.heap.used;
    for (int i = 0; i < 200; i++) tide_world_tick(&world);
    TIDE_CHECK(world.heap.used == used);
    const Named a = tide_get_Named(&world, (tide_entity){1, 1});
    TIDE_CHECK(reads(&world, a.names.at[0], "FOXSSSSSSS"));
}

static bool local_reads(const tide_local *l, const tide_text t, const char *expected)
{
    const tide_str s = tide_text_read(&l->heap, t);
    return s.bytes == (int32_t)strlen(expected) && memcmp(s.ptr, expected, strlen(expected)) == 0;
}

TIDE_TEST(text_arrays_in_local_state_pack_and_unpack)
{
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local_reads(&local, local.Recent.words.at[0], "one") && local_reads(&local, local.Recent.words.at[1], "frame 1"));
    TIDE_CHECK(tide_list_count(local.Recent.logs.at[1]) == 2);
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_REQUIRE(tide_local_pack(&local, bytes, size) == size);
    tide_local_free(&local);
    tide_local_init(&local);
    TIDE_REQUIRE(tide_local_unpack(&local, bytes, size));
    free(bytes);
    TIDE_CHECK(local_reads(&local, local.Recent.words.at[0], "one") && local_reads(&local, local.Recent.words.at[1], "frame 1"));
    TIDE_CHECK(local_reads(&local, local.Recent.words.at[2], "three"));
    TIDE_CHECK(tide_list_count(local.Recent.logs.at[1]) == 2);
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui); // Goes on from what it unpacked
    TIDE_CHECK(local_reads(&local, local.Recent.words.at[2], "frame 2"));
    tide_draw_free(&draw);
    tide_local_free(&local);
}
