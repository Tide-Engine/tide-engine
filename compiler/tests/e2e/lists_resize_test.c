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

// A tick in which the game does what `op` says (see lists_resize.tide's Run).
static void run(tide_world *w, const int op, const int count)
{
    w->Job.op = op;
    w->Job.count = count;
    tide_world_tick(w);
}

// A frame in which its view makes its list `count` long.
static void show(const int count)
{
    local.Shown.count = count;
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
}

// Whether the world's bytes are what Run's filling left up to `filled`, and zeros from there up to `count`.
static bool bytes_are(const tide_world *w, const int filled, const int count)
{
    if (tide_list_read_count(&w->heap, w->Store.bytes) != count) return false;
    for (int i = 0; i < count; i++) {
        const uint8_t *b = tide_list_read(&w->heap, w->Store.bytes, i, 1);
        if (!b || *b != (i < filled ? (uint8_t)(i * 7 + 3) : 0)) return false;
    }
    return true;
}

static uint32_t chunks(const tide_world *w, const tide_list l)
{
    const tide_block *b = tide_heap_block(&w->heap, l.at & 0x3FFFFFFFu);
    return l.at && (b->b & TIDE_LIST_CHUNKED) ? b->b & ~TIDE_LIST_CHUNKED : 0u;
}

// How many of a heap's pages aren't the ones another has.
static uint32_t pages_apart(const tide_heap *a, const tide_heap *b)
{
    uint32_t apart = a->pages > b->pages ? a->pages - b->pages : b->pages - a->pages;
    for (uint32_t i = 0; i < a->pages && i < b->pages; i++) apart += a->page[i] != b->page[i];
    return apart;
}

TIDE_TEST(lists_resize_all_checks_pass)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_world_tick(&world);
    const Results r = world.Results;
    if (r.firstFailure) printf("    check %d failed\n", (int)r.firstFailure);
    TIDE_CHECK(r.checks == 31);
    TIDE_CHECK(r.passed == r.checks);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_world_free(&world);
}

// A snapshot keeps what the list had, and shares the chunks a resize leaves alone; the chunks a list gives back
// are the ones it grows into again.
TIDE_TEST(lists_resize_snapshots_and_chunks)
{
    tide_world_init(&world, 1.0f);
    run(&world, 0, 0); // The game's own checks, done with
    run(&world, 1, 0);
    run(&world, 1, BIG);
    TIDE_CHECK(bytes_are(&world, 0, BIG) && chunks(&world, world.Store.bytes) == 19);
    run(&world, 2, 0);
    TIDE_CHECK(bytes_are(&world, BIG, BIG));
    tide_world_copy(&snapshot, &world);
    tide_world_copy(&twin, &world);
    const uint64_t before = tide_world_hash(&snapshot);
    const uint32_t used = world.heap.used;

    run(&world, 1, BIG); // As long already: nothing is written
    run(&twin, 0, BIG);
    TIDE_CHECK(pages_apart(&world.heap, &snapshot.heap) == 0);
    TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&twin));

    run(&world, 1, BIG - 1000); // The end of the last chunk, and the list's own block
    TIDE_CHECK(bytes_are(&world, BIG - 1000, BIG - 1000) && pages_apart(&world.heap, &snapshot.heap) == 2);
    run(&world, 1, BIG); // Zeros where the rest were
    TIDE_CHECK(bytes_are(&world, BIG - 1000, BIG) && pages_apart(&world.heap, &snapshot.heap) == 2);

    run(&world, 1, 100000); // Fewer chunks
    TIDE_CHECK(bytes_are(&world, 100000, 100000) && chunks(&world, world.Store.bytes) == 7);
    run(&world, 1, BIG); // ...and the ones it gave back, zeroed
    TIDE_CHECK(bytes_are(&world, 100000, BIG) && world.heap.used == used);
    run(&world, 1, 5); // To a few, to none, and to chunks again
    TIDE_CHECK(bytes_are(&world, 5, 5));
    run(&world, 1, 0);
    TIDE_CHECK(bytes_are(&world, 0, 0));
    run(&world, 1, BIG);
    TIDE_CHECK(bytes_are(&world, 0, BIG) && world.heap.used == used);

    TIDE_CHECK(bytes_are(&snapshot, BIG, BIG)); // The snapshot has what it had
    TIDE_CHECK(tide_world_hash(&snapshot) == before);

    // The world is as its bytes say: packed and unpacked, it's the same one, and goes on the same
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    tide_world_pack(&world, bytes, size);
    tide_world_free(&twin);
    TIDE_REQUIRE(tide_world_unpack(&twin, bytes, size));
    TIDE_CHECK(tide_world_hash(&twin) == tide_world_hash(&world));
    run(&world, 1, 123456);
    run(&twin, 1, 123456);
    TIDE_CHECK(tide_world_hash(&twin) == tide_world_hash(&world) && bytes_are(&twin, 0, 123456));
    free(bytes);
    tide_world_free(&world);
    tide_world_free(&snapshot);
    tide_world_free(&twin);
}

// Text that elements held goes back to the heap with them.
TIDE_TEST(lists_resize_lets_go_of_text)
{
    tide_world_init(&world, 1.0f);
    run(&world, 0, 0);
    run(&world, 3, 0);
    run(&world, 4, 6000); // Text in every element, in chunks
    TIDE_CHECK(tide_list_read_count(&world.heap, world.Store.names) == 6000 && chunks(&world, world.Store.names) == 2);
    run(&world, 3, 10);
    TIDE_CHECK(tide_list_read_count(&world.heap, world.Store.names) == 10 && chunks(&world, world.Store.names) == 1);
    const tide_text *ninth = tide_list_read(&world.heap, world.Store.names, 9, sizeof(tide_text));
    TIDE_REQUIRE(ninth != NULL);
    const tide_str name = tide_text_read(&world.heap, *ninth);
    TIDE_CHECK(name.bytes == 6 && memcmp(name.ptr, "name 9", 6) == 0);
    run(&world, 3, 0);
    run(&world, 4, 6000);
    const uint32_t used = world.heap.used;
    run(&world, 3, 10); // Again: in the blocks the text had that went with its elements
    run(&world, 3, 0);
    run(&world, 4, 6000);
    TIDE_CHECK(tide_list_read_count(&world.heap, world.Store.names) == 6000 && world.heap.used == used);
    tide_world_free(&world);
}

TIDE_TEST(lists_resize_in_the_local_world)
{
    tide_world_init(&world, 1.0f);
    tide_local_init(&local);
    const uint64_t before = tide_world_hash(&world);
    show(10);
    TIDE_CHECK(tide_list_read_count(&local.heap, local.Shown.numbers) == 10 && local.Shown.sum == 1);
    TIDE_CHECK(local.Shown.numbers.at >> 30 == TIDE_IN_LOCAL);
    show(50000); // In chunks: the tenth is still 1, and so is the last
    TIDE_CHECK(tide_list_read_count(&local.heap, local.Shown.numbers) == 50000 && local.Shown.sum == 2);
    show(20); // The last one went
    TIDE_CHECK(tide_list_read_count(&local.heap, local.Shown.numbers) == 20 && local.Shown.sum == 2);
    show(0);
    TIDE_CHECK(tide_list_read_count(&local.heap, local.Shown.numbers) == 0 && local.Shown.sum == 0);
    show(50000);
    TIDE_CHECK(local.Shown.sum == 1);
    TIDE_CHECK(tide_world_hash(&world) == before); // Nothing of it is the match's
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_local_free(&local);
    tide_world_free(&world);
}
