// Each owner's text, lists and grids are in a heap of its own (heaps.tide):
// systems that change different owners' run at the same time, and the world is
// one thread's, heap by heap, tick by tick, and through its bytes and a delta.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/jobs.h"
#include "tide_test.h"
#ifdef TIDE_HEAPS_THREADS
#include "tide/platform.h"
#endif

// A pool that's one thread saying it has eight: a round runs its work as
// thread 1 first, which takes every task there is, and then as the caller's.
static uint32_t rounds;

static void second_first(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    (void)self;
    rounds++;
    work(context, 1);
    work(context, 0);
}

static bool same_ints(const tide_world *a, const tide_list la, const tide_world *b, const tide_list lb)
{
    if (tide_list_read_count(&a->heap, la) != tide_list_read_count(&b->heap, lb)) return false;
    for (int32_t i = 0; i < tide_list_read_count(&a->heap, la); i++) {
        const int32_t *x = tide_list_read(&a->heap, la, i, sizeof(int32_t));
        const int32_t *y = tide_list_read(&b->heap, lb, i, sizeof(int32_t));
        if (!x || !y || *x != *y) return false;
    }
    return true;
}

static bool same_text(const tide_world *a, const tide_text ta, const tide_world *b, const tide_text tb)
{
    const tide_str x = tide_text_read(&a->heap, ta), y = tide_text_read(&b->heap, tb);
    return x.bytes == y.bytes && memcmp(x.ptr, y.ptr, (size_t)x.bytes) == 0;
}

static bool same_names(const tide_world *a, const tide_world *b)
{
    const int32_t count = tide_list_read_count(&a->heap, a->Roster.names);
    if (count != tide_list_read_count(&b->heap, b->Roster.names)) return false;
    for (int32_t i = 0; i < count; i++) {
        const tide_text *x = tide_list_read(&a->heap, a->Roster.names, i, sizeof(tide_text));
        const tide_text *y = tide_list_read(&b->heap, b->Roster.names, i, sizeof(tide_text));
        if (!x || !y || !same_text(a, *x, b, *y)) return false;
    }
    return true;
}

// What code would read of the two worlds is the same, whatever their heaps' bytes are
static bool same_values(const tide_world *a, const tide_world *b)
{
    return same_ints(a, a->Ledger.entries, b, b->Ledger.entries) && same_text(a, a->Ledger.last, b, b->Ledger.last)
        && same_names(a, b) && a->Tally.ticks == b->Tally.ticks && a->Tally.ghosts == b->Tally.ghosts;
}

// The owners have heaps of their own: their handles say which, and no two the same
TIDE_TEST(heaps_owners_have_heaps_of_their_own)
{
    static tide_world world;
    tide_world_init(&world, 1.0f);
    for (int i = 0; i < 3; i++) tide_world_tick(&world);
    const uint32_t ledger = tide_handle_where(world.Ledger.entries.at), last = tide_handle_where(world.Ledger.last.at);
    const uint32_t roster = tide_handle_where(world.Roster.names.at);
    TIDE_CHECK(ledger == TIDE_WHERE_Ledger && last == ledger && roster == TIDE_WHERE_Roster);
    TIDE_CHECK(TIDE_WHERE_Ledger != TIDE_IN_MATCH && TIDE_WHERE_Roster != TIDE_IN_MATCH && TIDE_WHERE_Ledger != TIDE_WHERE_Roster);
    TIDE_CHECK(TIDE_WHERE_Board != TIDE_WHERE_Ledger && TIDE_WHERE_Pack != TIDE_WHERE_Trail && TIDE_WHERE_Ghost != TIDE_WHERE_Named);
    // ...and what a list's elements hold is in their list's heap
    const tide_text *name = tide_list_read(&world.heap, world.Roster.names, 0, sizeof *name);
    TIDE_REQUIRE(name != NULL);
    TIDE_CHECK(tide_handle_where(name->at) == TIDE_WHERE_Roster);
    // Nothing of the match's own: every block is an owner's
    TIDE_CHECK(tide_heaps_of(&world.heap, 0u)->used == 0);
    TIDE_CHECK(tide_heaps_used(&world.heap) > 0 && !tide_heaps_pending(&world.heap));
    tide_world_free(&world);
}

TIDE_TEST(heaps_on_threads_are_one_threads)
{
    static tide_world one, eight;
    tide_world_init(&one, 1.0f);
    tide_world_init(&eight, 1.0f);
    const tide_jobs fake = {.threads = 8u, .run = second_first};
    rounds = 0;
    for (int i = 0; i < 12; i++) {
        tide_world_tick(&one);
        tide_world_tick_on(&eight, &fake);
        TIDE_CHECK(tide_world_hash(&one) == tide_world_hash(&eight));
        TIDE_CHECK(same_values(&one, &eight));
    }
    TIDE_CHECK(rounds >= 12u); // Every tick was worth threads
    TIDE_CHECK(one.Tally.ghosts > 100 && tide_list_read_count(&one.heap, one.Roster.names) > 100);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_world_free(&eight);
    tide_world_free(&one);
}

// A world's bytes, and a delta from a snapshot of it, give the same world back: every heap, by its owner
TIDE_TEST(heaps_pack_and_delta_give_the_world_back)
{
    static tide_world world, snapshot, back, later;
    tide_world_init(&world, 1.0f);
    for (int i = 0; i < 5; i++) tide_world_tick(&world);
    const uint64_t hash = tide_world_hash(&world);

    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL && tide_world_pack(&world, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&back, bytes, size));
    free(bytes);
    TIDE_CHECK(tide_world_hash(&back) == hash && same_values(&world, &back));

    // A snapshot shares the world's pages; three ticks on, the delta carries what changed
    tide_world_copy(&snapshot, &world);
    TIDE_CHECK(tide_heaps_pages_apart(&world.heap, &snapshot.heap) == 0);
    for (int i = 0; i < 3; i++) tide_world_tick(&world);
    TIDE_CHECK(tide_world_hash(&snapshot) == hash && tide_heaps_pages_apart(&world.heap, &snapshot.heap) > 0);
    uint32_t delta_size = 0;
    uint8_t *delta = tide_world_pack_delta(&world, &snapshot, NULL, 0, &delta_size);
    TIDE_REQUIRE(delta != NULL);
    TIDE_REQUIRE(tide_world_unpack_delta(&later, &back, delta, delta_size)); // From the unpacked copy of the same base
    free(delta);
    TIDE_CHECK(tide_world_hash(&later) == tide_world_hash(&world) && same_values(&world, &later));

    // ...and both go on as one
    for (int i = 0; i < 4; i++) {
        tide_world_tick(&world);
        tide_world_tick(&later);
        TIDE_CHECK(tide_world_hash(&later) == tide_world_hash(&world));
    }
    tide_world_free(&later);
    tide_world_free(&back);
    tide_world_free(&snapshot);
    tide_world_free(&world);
}

#ifdef TIDE_HEAPS_THREADS
TIDE_TEST(heaps_on_the_pool_are_one_threads)
{
    const tide_jobs *pool = tide_platform_jobs();
    if (!pool || pool->threads < 2u) {
        printf("    one core: nothing to compare\n");
        return;
    }
    static tide_world one, many;
    for (int attempt = 0; attempt < 5; attempt++) {
        tide_world_init(&one, 1.0f);
        tide_world_init(&many, 1.0f);
        for (int i = 0; i < 12; i++) {
            tide_world_tick(&one);
            tide_world_tick_on(&many, pool);
            TIDE_CHECK(tide_world_hash(&one) == tide_world_hash(&many));
            TIDE_CHECK(same_values(&one, &many));
        }
        tide_world_free(&many);
        tide_world_free(&one);
    }
}
#endif
