#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

#define BIG 300000 // Bytes: 19 chunks

static tide_world world;
static tide_world snapshot;
static tide_world twin;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static uint8_t byte_at(const int i, const int seed)
{
    return (uint8_t)(i * 31 + (i >> 8) * 7 + seed * 13 + 1);
}

// A tick in which the game does what `op` says (see lists_out.tide's Run).
static void run(tide_world *w, const int op, const int count, const int seed, const int extra)
{
    w->Job.op = op;
    w->Job.count = count;
    w->Job.seed = seed;
    w->Job.extra = extra;
    tide_world_tick(w);
}

static void show(const int op, const int count, const int seed)
{
    local.Displayed.op = op;
    local.Displayed.count = count;
    local.Displayed.seed = seed;
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
}

// Whether a list is the `count` bytes C makes from `seed`, but the one at `poked` (-1: none), which is another.
static bool bytes_are(const tide_heap *heap, const tide_list l, const int count, const int seed, const int poked)
{
    if (tide_list_read_count(heap, l) != count) return false;
    for (int i = 0; i < count; i++) {
        const uint8_t *b = tide_list_read(heap, l, i, 1);
        if (!b || *b != (uint8_t)(byte_at(i, seed) ^ (i == poked ? 0xFF : 0))) return false;
    }
    return tide_list_read(heap, l, count, 1) == NULL;
}

static bool cells_are(const tide_heap *heap, const tide_list l, const int count, const int seed)
{
    if (tide_list_read_count(heap, l) != count) return false;
    for (int i = 0; i < count; i++) {
        Cell expected;
        memset(&expected, 0, sizeof expected);
        expected.id = i + seed;
        expected.weight = (float)i * 0.5f;
        expected.alive = i % 3 == 0;
        const Cell *c = tide_list_read(heap, l, i, sizeof(Cell));
        if (!c || memcmp(c, &expected, sizeof expected) != 0) return false;
    }
    return true;
}

static bool chunked(const tide_heap *heap, const tide_list l)
{
    return l.at && (tide_heap_block(heap, tide_handle_offset(l.at))->b & TIDE_LIST_CHUNKED);
}

static Blob blob_of(const tide_world *w, const int seed)
{
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Blob(w, e) && tide_get_Blob(w, e).seed == seed) return tide_get_Blob(w, e);
    }
    return (Blob){0};
}

static bool blobs_are(const tide_world *w, const int count)
{
    bool right = true;
    for (int seed = 1; seed <= 3; seed++) {
        const Blob blob = blob_of(w, seed);
        right &= blob.seed == seed && bytes_are(&w->heap, blob.bytes, count * seed, seed, -1);
    }
    return right;
}

static uint32_t pages_apart(const tide_heap *a, const tide_heap *b)
{
    uint32_t apart = a->pages > b->pages ? a->pages - b->pages : b->pages - a->pages;
    for (uint32_t i = 0; i < a->pages && i < b->pages; i++) apart += a->page[i] != b->page[i];
    return apart;
}

TIDE_TEST(lists_out_from_empty_to_a_few_to_chunks_and_back)
{
    tide_world_init(&world, 1.0f);
    TIDE_CHECK(world.Store.image.at == 0);
    run(&world, 1, 100, 1, 0);
    TIDE_CHECK(world.Store.said == 100 && bytes_are(&world.heap, world.Store.image, 100, 1, -1));
    TIDE_CHECK(!chunked(&world.heap, world.Store.image) && tide_handle_where(world.Store.image.at) == TIDE_IN_MATCH);
    run(&world, 1, BIG, 2, 0);
    TIDE_CHECK(world.Store.said == BIG && bytes_are(&world.heap, world.Store.image, BIG, 2, -1));
    TIDE_CHECK(chunked(&world.heap, world.Store.image));
    run(&world, 1, BIG + 50000, 3, 0);
    TIDE_CHECK(bytes_are(&world.heap, world.Store.image, BIG + 50000, 3, -1));
    run(&world, 1, 200000, 4, 0);
    TIDE_CHECK(bytes_are(&world.heap, world.Store.image, 200000, 4, -1));
    const uint64_t used = world.heap.used;
    run(&world, 1, 40, 5, 0);
    TIDE_CHECK(bytes_are(&world.heap, world.Store.image, 40, 5, -1));
    run(&world, 1, 0, 0, 0);
    TIDE_CHECK(world.Store.said == 0 && tide_list_read_count(&world.heap, world.Store.image) == 0);
    run(&world, 1, BIG + 50000, 6, 0);
    TIDE_CHECK(bytes_are(&world.heap, world.Store.image, BIG + 50000, 6, -1) && world.heap.used == used);
    run(&world, 7, 0, 0, 0);
    TIDE_CHECK(tide_list_read_count(&world.heap, world.Store.image) == 0);
    run(&world, 1, 9, 7, 0);
    TIDE_CHECK(bytes_are(&world.heap, world.Store.image, 9, 7, -1) && !chunked(&world.heap, world.Store.image));

    run(&world, 1, BIG, 8, 0);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    tide_world_pack(&world, bytes, size);
    TIDE_REQUIRE(tide_world_unpack(&twin, bytes, size));
    TIDE_CHECK(tide_world_hash(&twin) == tide_world_hash(&world));
    run(&world, 1, 70000, 9, 0);
    run(&twin, 1, 70000, 9, 0);
    TIDE_CHECK(tide_world_hash(&twin) == tide_world_hash(&world));
    TIDE_CHECK(bytes_are(&twin.heap, twin.Store.image, 70000, 9, -1));
    free(bytes);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_world_free(&world);
    tide_world_free(&twin);
}

// Structs whose size is no power of two: a chunk holds 1364 of them, with room left over.
TIDE_TEST(lists_out_structs_across_chunks)
{
    const int per = (int)(TIDE_LIST_CHUNK / sizeof(Cell));
    tide_world_init(&world, 1.0f);
    run(&world, 2, 10, 1, 0);
    TIDE_CHECK(world.Store.said == 10 && cells_are(&world.heap, world.Store.cells, 10, 1));
    run(&world, 2, per, 2, 0);
    TIDE_CHECK(cells_are(&world.heap, world.Store.cells, per, 2) && !chunked(&world.heap, world.Store.cells));
    run(&world, 2, per + 1, 3, 0);
    TIDE_CHECK(cells_are(&world.heap, world.Store.cells, per + 1, 3) && chunked(&world.heap, world.Store.cells));
    run(&world, 2, 30000, 4, 0);
    TIDE_CHECK(cells_are(&world.heap, world.Store.cells, 30000, 4));
    run(&world, 2, per, 5, 0);
    TIDE_CHECK(cells_are(&world.heap, world.Store.cells, per, 5));
    run(&world, 2, 2 * per + 1, 6, 0);
    TIDE_CHECK(cells_are(&world.heap, world.Store.cells, 2 * per + 1, 6));
    run(&world, 2, 1, 7, 0);
    TIDE_CHECK(cells_are(&world.heap, world.Store.cells, 1, 7));
    run(&world, 2, 0, 8, 0);
    TIDE_CHECK(cells_are(&world.heap, world.Store.cells, 0, 8));
    tide_world_free(&world);
}

// The same bytes again write nothing and leave a snapshot sharing every heap page; a changed byte costs its chunk.
TIDE_TEST(lists_out_writes_only_what_differs)
{
    tide_world_init(&world, 1.0f);
    run(&world, 1, BIG, 1, 0);
    tide_world_copy(&snapshot, &world);
    tide_world_copy(&twin, &world);
    const uint64_t before = tide_world_hash(&snapshot);
    const uint64_t heap_before = tide_heap_hash(0, &world.heap);

    run(&world, 1, BIG, 1, 0);
    run(&twin, 0, BIG, 1, 0);
    TIDE_CHECK(bytes_are(&world.heap, world.Store.image, BIG, 1, -1));
    TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&twin));
    TIDE_CHECK(tide_heap_hash(0, &world.heap) == heap_before);
    TIDE_CHECK(pages_apart(&world.heap, &snapshot.heap) == 0);

    run(&world, 3, BIG, 1, 123456);
    TIDE_CHECK(bytes_are(&world.heap, world.Store.image, BIG, 1, 123456));
    TIDE_CHECK(pages_apart(&world.heap, &snapshot.heap) == 1);
    TIDE_CHECK(tide_heap_hash(0, &world.heap) != heap_before);

    run(&world, 1, 1000, 9, 0);
    TIDE_CHECK(bytes_are(&world.heap, world.Store.image, 1000, 9, -1));
    run(&world, 7, 0, 0, 0);
    TIDE_CHECK(tide_list_read_count(&world.heap, world.Store.image) == 0);
    TIDE_CHECK(bytes_are(&snapshot.heap, snapshot.Store.image, BIG, 1, -1));
    TIDE_CHECK(tide_world_hash(&snapshot) == before);
    tide_world_free(&world);
    tide_world_free(&snapshot);
    tide_world_free(&twin);
}

TIDE_TEST(lists_out_room_asked_twice_and_counts_said)
{
    tide_world_init(&world, 1.0f);
    run(&world, 4, 5000, 1, 20000);
    TIDE_CHECK(bytes_are(&world.heap, world.Store.image, 5000, 1, -1));
    run(&world, 4, BIG, 2, 10);
    TIDE_CHECK(bytes_are(&world.heap, world.Store.image, BIG, 2, -1));
    run(&world, 5, 1000, 3, 600);
    TIDE_CHECK(bytes_are(&world.heap, world.Store.image, 600, 3, -1));
    run(&world, 5, 1000, 4, 5000);
    TIDE_CHECK(bytes_are(&world.heap, world.Store.image, 1000, 4, -1));
    run(&world, 6, 800, 5, 0);
    TIDE_CHECK(tide_list_read_count(&world.heap, world.Store.image) == 0);
    run(&world, 1, BIG, 6, 0);
    run(&world, 6, BIG, 7, 0);
    TIDE_CHECK(tide_list_read_count(&world.heap, world.Store.image) == 0);
    tide_world_free(&world);
}

// A match's list through functions, structs and locals, read and made in one call or among a statement's reads.
TIDE_TEST(lists_out_wherever_a_list_is)
{
    tide_world_init(&world, 1.0f);
    run(&world, 9, BIG, 5, 0);
    TIDE_CHECK(bytes_are(&world.heap, world.Store.image, BIG, 5, -1));
    run(&world, 10, 30000, 6, 0);
    TIDE_CHECK(bytes_are(&world.heap, world.Store.held.bytes, 30000, 6, -1) && world.Store.held.tag == 0);
    run(&world, 10, 12, 7, 0);
    TIDE_CHECK(bytes_are(&world.heap, world.Store.held.bytes, 12, 7, -1));

    run(&world, 13, 5000, 0, 0);
    run(&world, 8, 0, 0, 0);
    TIDE_CHECK(world.Job.result == 5000 && tide_list_read_count(&world.heap, world.Store.doubles) == 5000);
    bool doubled = true;
    for (int i = 0; i < 5000; i++) {
        const float *twice = tide_list_read(&world.heap, world.Store.doubles, i, sizeof(float));
        doubled &= twice && *twice == (float)i;
    }
    TIDE_CHECK(doubled);
    run(&world, 13, 3, 0, 0);
    run(&world, 8, 0, 0, 0);
    TIDE_CHECK(world.Job.result == 3 && tide_list_read_count(&world.heap, world.Store.doubles) == 3);
    TIDE_CHECK(tide_list_read_count(&world.heap, world.Store.halves) == 3);

    run(&world, 11, 50, 3, 0);
    TIDE_CHECK(world.Job.result == 8);
    run(&world, 11, 700000, 4, 0);
    TIDE_CHECK(world.Job.result == 8);
    TIDE_CHECK(tide_scratch_mark() == 0);
    run(&world, 1, 500, 1, 0);
    run(&world, 14, 20000, 2, 0);
    TIDE_CHECK(world.Job.result == 1 && bytes_are(&world.heap, world.Store.image, 500, 1, -1));

    run(&world, 12, 0, 0, 0);
    TIDE_CHECK(world.Job.result == 3544);
    TIDE_CHECK(bytes_are(&world.heap, world.Store.image, 4, 1, -1));
    tide_world_free(&world);
}

// A component's list, which a system makes for each entity it runs for.
TIDE_TEST(lists_out_for_each_entity)
{
    tide_world_init(&world, 1.0f);
    TIDE_CHECK(blobs_are(&world, 0));
    run(&world, 15, 10, 0, 0);
    TIDE_CHECK(blobs_are(&world, 10));
    tide_world_copy(&snapshot, &world);
    run(&world, 15, 9000, 0, 0);
    TIDE_CHECK(blobs_are(&world, 9000) && !chunked(&world.heap, blob_of(&world, 1).bytes));
    TIDE_CHECK(chunked(&world.heap, blob_of(&world, 2).bytes) && chunked(&world.heap, blob_of(&world, 3).bytes));
    run(&world, 15, 7000, 0, 0);
    TIDE_CHECK(blobs_are(&world, 7000));
    run(&world, 15, 0, 0, 0);
    TIDE_CHECK(blobs_are(&world, 0));
    run(&world, 15, 3, 0, 0);
    TIDE_CHECK(blobs_are(&world, 3));
    TIDE_CHECK(blobs_are(&snapshot, 10));
    tide_world_free(&world);
    tide_world_free(&snapshot);
}

TIDE_TEST(lists_out_in_the_local_world)
{
    tide_world_init(&world, 1.0f);
    tide_local_init(&local);
    const uint64_t before = tide_world_hash(&world);
    show(1, 100, 1);
    TIDE_CHECK(local.Displayed.said == 100 && bytes_are(&local.heap, local.Displayed.image, 100, 1, -1));
    TIDE_CHECK(tide_handle_where(local.Displayed.image.at) == TIDE_IN_LOCAL);
    show(1, BIG, 2);
    TIDE_CHECK(bytes_are(&local.heap, local.Displayed.image, BIG, 2, -1) && chunked(&local.heap, local.Displayed.image));
    show(1, 30, 3);
    TIDE_CHECK(bytes_are(&local.heap, local.Displayed.image, 30, 3, -1));
    show(7, 0, 0);
    TIDE_CHECK(tide_list_read_count(&local.heap, local.Displayed.image) == 0);
    show(1, BIG, 4);
    TIDE_CHECK(bytes_are(&local.heap, local.Displayed.image, BIG, 4, -1));
    TIDE_CHECK(tide_world_hash(&world) == before);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_local_free(&local);
    tide_world_free(&world);
}
