#include <string.h>

#include "tide/map.h"
#include "tide/text.h"
#include "tide_test.h"

// Maps as generated code uses them (see tide/map.h): an entry of a live word,
// an int key and an int value, in a world's heap and in the scratch area.

typedef struct map_world {
    tide_map scores;
    tide_heap heap;
} map_world;

static map_world world;

typedef struct entry {
    uint32_t live;
    int32_t key;
    int32_t value;
} entry;

static uint32_t hash_int(const void *key)
{
    return tide_map_hash_bytes(key, sizeof(int32_t));
}

static bool equal_int(const void *key, const void *probe)
{
    return *(const int32_t *)key == *(const int32_t *)probe;
}

static const tide_map_kind kind = {sizeof(entry), offsetof(entry, key), hash_int, hash_int, equal_int};

static void start(void)
{
    tide_heap_free(&world.heap);
    memset(&world, 0, sizeof world);
    tide_text_use(&world.heap, NULL);
}

static void put(tide_map *m, const int32_t key, const int32_t value, const uint32_t where)
{
    bool added;
    entry *e = tide_map_insert(m, &kind, &key, where, &added);
    TIDE_REQUIRE(e != NULL);
    if (added) e->key = key;
    e->value = value;
}

static int32_t get(const tide_map m, const int32_t key)
{
    const int32_t i = tide_map_find(m, &kind, &key);
    if (i < 0) return -1;
    const entry *e = tide_map_entry(m, i, &kind);
    return e->value;
}

static bool remove_key(tide_map *m, const int32_t key, const uint32_t where)
{
    const int32_t i = tide_map_find(*m, &kind, &key);
    if (i < 0) return false;
    tide_map_erase(m, &kind, i, where);
    return true;
}

TIDE_TEST(map_keys_find_their_values_in_a_world)
{
    start();
    for (int32_t i = 0; i < 1000; i++) put(&world.scores, i * 7, i, TIDE_IN_MATCH);
    TIDE_CHECK(tide_map_count(world.scores) == 1000);
    for (int32_t i = 0; i < 1000; i++) TIDE_CHECK(get(world.scores, i * 7) == i);
    TIDE_CHECK(get(world.scores, 1) == -1 && get(world.scores, -7) == -1);
    put(&world.scores, 14, 99, TIDE_IN_MATCH); // Replaces
    TIDE_CHECK(get(world.scores, 14) == 99 && tide_map_count(world.scores) == 1000);
    tide_heap_free(&world.heap);
}

TIDE_TEST(map_entries_keep_the_order_keys_were_added_in)
{
    start();
    static const int32_t keys[] = {50, 3, 999, -4, 12};
    for (size_t i = 0; i < 5; i++) put(&world.scores, keys[i], (int32_t)i, TIDE_IN_MATCH);
    int seen = 0;
    for (int32_t i = 0; i < tide_map_entries(world.scores); i++) {
        const entry *e = tide_map_entry(world.scores, i, &kind);
        if (!tide_map_live(e)) continue;
        TIDE_CHECK(e->key == keys[seen]);
        seen++;
    }
    TIDE_CHECK(seen == 5);
    tide_heap_free(&world.heap);
}

TIDE_TEST(map_removing_keeps_the_rest_and_packs_once_mostly_dead)
{
    start();
    for (int32_t i = 0; i < 100; i++) put(&world.scores, i, i * 2, TIDE_IN_MATCH);
    for (int32_t i = 0; i < 100; i += 2) TIDE_CHECK(remove_key(&world.scores, i, TIDE_IN_MATCH));
    TIDE_CHECK(!remove_key(&world.scores, 0, TIDE_IN_MATCH));
    TIDE_CHECK(tide_map_count(world.scores) == 50);
    for (int32_t i = 0; i < 100; i++) TIDE_CHECK(get(world.scores, i) == (i % 2 ? i * 2 : -1));
    TIDE_CHECK(tide_map_entries(world.scores) == 100); // The dead stay in place
    for (int32_t i = 1; i < 60; i += 2) remove_key(&world.scores, i, TIDE_IN_MATCH); // Now mostly dead
    put(&world.scores, 1000, 1, TIDE_IN_MATCH); // Adding packs the live ones first
    TIDE_CHECK(tide_map_entries(world.scores) == 21 && tide_map_count(world.scores) == 21);
    for (int32_t i = 61; i < 100; i += 2) TIDE_CHECK(get(world.scores, i) == i * 2);
    TIDE_CHECK(get(world.scores, 1000) == 1);
    // Still in order
    int32_t last = -1;
    for (int32_t i = 0; i < tide_map_entries(world.scores); i++) {
        const entry *e = tide_map_entry(world.scores, i, &kind);
        TIDE_CHECK(tide_map_live(e) && e->key > last);
        last = e->key;
    }
    tide_heap_free(&world.heap);
}

TIDE_TEST(map_the_same_steps_give_the_same_bytes)
{
    map_world a = {0};
    map_world b = {0};
    for (int round = 0; round < 2; round++) {
        map_world *w = round ? &b : &a;
        tide_text_use(&w->heap, NULL);
        for (int32_t i = 0; i < 300; i++) put(&w->scores, i * 31 % 97, i, TIDE_IN_MATCH);
        for (int32_t i = 0; i < 300; i += 3) remove_key(&w->scores, i * 31 % 97, TIDE_IN_MATCH);
        for (int32_t i = 0; i < 40; i++) put(&w->scores, 1000 + i, i, TIDE_IN_MATCH);
    }
    TIDE_CHECK(memcmp(&a.scores, &b.scores, sizeof a.scores) == 0);
    TIDE_CHECK(tide_heap_hash(0, &a.heap) == tide_heap_hash(0, &b.heap));
    tide_heap_free(&a.heap);
    tide_heap_free(&b.heap);
    start();
}

TIDE_TEST(map_copies_and_assignment_dont_share)
{
    start();
    for (int32_t i = 0; i < 20; i++) put(&world.scores, i, i, TIDE_IN_MATCH);
    tide_map copy = tide_map_copy(world.scores, &kind); // In the scratch area
    put(&copy, 5, 500, TIDE_IN_SCRATCH);
    put(&copy, 100, 1, TIDE_IN_SCRATCH);
    TIDE_CHECK(get(copy, 5) == 500 && get(copy, 100) == 1 && tide_map_count(copy) == 21);
    TIDE_CHECK(get(world.scores, 5) == 5 && get(world.scores, 100) == -1);
    tide_map other = {0};
    tide_map_set(&other, copy, &kind, TIDE_IN_MATCH); // Into the world: its own blocks
    TIDE_CHECK(get(other, 100) == 1 && tide_map_count(other) == 21);
    remove_key(&other, 100, TIDE_IN_MATCH);
    TIDE_CHECK(get(copy, 100) == 1);
    tide_map_clear(&other, &kind);
    TIDE_CHECK(tide_map_count(other) == 0 && get(other, 5) == -1);
    put(&other, 7, 7, TIDE_IN_MATCH);
    TIDE_CHECK(get(other, 7) == 7 && tide_map_count(other) == 1);
    tide_map_release(&other, TIDE_IN_MATCH);
    tide_scratch_free();
    tide_heap_free(&world.heap);
}
