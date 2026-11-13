// A per-entity system that changes its rows' text and lists goes in runs on
// threads (row_heaps.tide), each run's blocks in a heap of its own: the world
// is one thread's, tick by tick, in its hash, its bytes and its deltas.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/jobs.h"
#include "tide/time.h"
#include "tide_test.h"
#ifdef TIDE_ROW_HEAPS_THREADS
#include "tide/platform.h"
#endif

#define TICKS 14

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

// The world as bytes: its length, in memory the caller frees.
static uint8_t *packed(const tide_world *w, uint32_t *size)
{
    *size = tide_world_pack(w, NULL, 0);
    uint8_t *bytes = malloc(*size);
    if (bytes && tide_world_pack(w, bytes, *size) != *size) *size = 0;
    return bytes;
}

static bool same_bytes(const tide_world *a, const tide_world *b)
{
    uint32_t na, nb;
    uint8_t *x = packed(a, &na), *y = packed(b, &nb);
    const bool same = x && y && na && na == nb && memcmp(x, y, na) == 0;
    free(x);
    free(y);
    return same;
}

// How many of an owner's runs' heaps have handed anything out, past the first.
static uint32_t runs_used(tide_world *w, const uint32_t where)
{
    uint32_t used = 0;
    for (uint32_t run = 1; run < TIDE_HEAP_RUNS; run++) {
        used += tide_heaps_of(&w->heap, where >> TIDE_WHERE_HEAP_SHIFT | run << TIDE_HEAP_ID_BITS)->used != 0;
    }
    return used;
}

TIDE_TEST(row_heaps_runs_have_heaps_of_their_own)
{
    static tide_world world;
    tide_world_init(&world, 1.0f);
    for (int i = 0; i < 4; i++) tide_world_tick(&world);
    // On one thread too: the runs are the entities' order's, not the threads'
    TIDE_CHECK(runs_used(&world, TIDE_WHERE_Bag) == TIDE_HEAP_RUNS - 1u);
    TIDE_CHECK(runs_used(&world, TIDE_WHERE_Told) > 0 && runs_used(&world, TIDE_WHERE_Echo) > 0);
    TIDE_CHECK(!tide_heaps_pending(&world.heap) && world.Tally.sum > 0 && world.Tally.told > 0 && world.Tally.echoes > 0);
    // Nothing a run kept is left once its system is done
    for (uint32_t run = 0; run < TIDE_HEAP_RUNS; run++) {
        const tide_heap *h = tide_heaps_of(&world.heap, TIDE_WHERE_Bag >> TIDE_WHERE_HEAP_SHIFT | run << TIDE_HEAP_ID_BITS);
        TIDE_CHECK(!h->away && !h->held && !h->pending && !h->retired_count && !h->later_count);
    }
    tide_world_free(&world);
}

TIDE_TEST(row_heaps_on_threads_are_one_threads)
{
    static tide_world one, eight, kept;
    tide_world_init(&one, 1.0f);
    tide_world_init(&eight, 1.0f);
    const tide_jobs fake = {.threads = 8u, .run = second_first};
    rounds = 0;
    for (int i = 0; i < TICKS; i++) {
        tide_world_copy(&kept, &eight); // Its pages are shared every tick, as a session's are
        tide_world_tick(&one);
        tide_world_tick_on(&eight, &fake);
        TIDE_CHECK(tide_world_hash(&one) == tide_world_hash(&eight));
        TIDE_CHECK(same_bytes(&one, &eight));
        TIDE_CHECK(one.Tally.sum == eight.Tally.sum && one.Tally.told == eight.Tally.told && one.Tally.echoes == eight.Tally.echoes);
    }
    TIDE_CHECK(rounds >= (uint32_t)TICKS); // Every tick was worth threads
    TIDE_CHECK(one.Tally.next > 900 + 100 && one.Tally.told > 1000);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_world_free(&kept);
    tide_world_free(&eight);
    tide_world_free(&one);
}

// A world's bytes, and a delta from a snapshot of it, give the same world back, every run's heap with it, and
// both go on as one; a snapshot taken between ticks is the world it was when the world has gone on
TIDE_TEST(row_heaps_pack_and_delta_give_the_world_back)
{
    static tide_world world, snapshot, back, later;
    const tide_jobs fake = {.threads = 8u, .run = second_first};
    tide_world_init(&world, 1.0f);
    for (int i = 0; i < 6; i++) tide_world_tick_on(&world, &fake);
    const uint64_t hash = tide_world_hash(&world);

    uint32_t size;
    uint8_t *bytes = packed(&world, &size);
    TIDE_REQUIRE(bytes != NULL && size);
    TIDE_REQUIRE(tide_world_unpack(&back, bytes, size));
    free(bytes);
    TIDE_CHECK(tide_world_hash(&back) == hash && same_bytes(&world, &back));

    tide_world_copy(&snapshot, &world);
    TIDE_CHECK(tide_heaps_pages_apart(&world.heap, &snapshot.heap) == 0);
    for (int i = 0; i < 3; i++) tide_world_tick_on(&world, &fake);
    TIDE_CHECK(tide_world_hash(&snapshot) == hash && same_bytes(&snapshot, &back));
    TIDE_CHECK(tide_heaps_pages_apart(&world.heap, &snapshot.heap) > 0);
    uint32_t delta_size = 0;
    uint8_t *delta = tide_world_pack_delta(&world, &snapshot, NULL, 0, &delta_size);
    TIDE_REQUIRE(delta != NULL);
    TIDE_REQUIRE(tide_world_unpack_delta(&later, &back, delta, delta_size)); // From the unpacked copy of the same base
    free(delta);
    TIDE_CHECK(tide_world_hash(&later) == tide_world_hash(&world) && same_bytes(&world, &later));

    // Rolled back to the snapshot and run again, on one thread: the same three ticks
    static tide_world again;
    tide_world_copy(&again, &snapshot);
    for (int i = 0; i < 3; i++) tide_world_tick(&again);
    TIDE_CHECK(tide_world_hash(&again) == tide_world_hash(&world) && same_bytes(&again, &world));
    tide_world_free(&again);

    for (int i = 0; i < 4; i++) {
        tide_world_tick_on(&world, &fake);
        tide_world_tick(&later);
        TIDE_CHECK(tide_world_hash(&later) == tide_world_hash(&world) && same_bytes(&world, &later));
    }
    tide_world_free(&later);
    tide_world_free(&back);
    tide_world_free(&snapshot);
    tide_world_free(&world);
}

#ifdef TIDE_ROW_HEAPS_THREADS
// For measuring, from a release build with TIDE_ROW_HEAPS_TIME set: a tick's
// mean time on one thread and on the pool, over 200 ticks after 20 (three
// rounds, a snapshot taken every tick as a session takes one).
TIDE_TEST(row_heaps_time)
{
    const tide_jobs *pool = tide_platform_jobs();
    if (!getenv("TIDE_ROW_HEAPS_TIME") || !pool) return;
    static tide_world world, kept;
    for (int round = 0; round < 3; round++) {
        for (int threads = 0; threads < 2; threads++) {
            tide_world_init(&world, 1.0f);
            uint64_t began = 0;
            for (int i = 0; i < 220; i++) {
                if (i == 20) began = tide_time_now_ns();
                tide_world_copy(&kept, &world);
                if (threads) tide_world_tick_on(&world, pool);
                else tide_world_tick(&world);
                (void)tide_world_hash(&world);
            }
            printf("    %s: %.3f ms a tick\n", threads ? "the pool" : "one thread", (double)(tide_time_now_ns() - began) / 200.0 / 1e6);
            tide_world_free(&kept);
            tide_world_free(&world);
        }
    }
}

TIDE_TEST(row_heaps_on_the_pool_are_one_threads)
{
    const tide_jobs *pool = tide_platform_jobs();
    if (!pool || pool->threads < 2u) {
        printf("    one core: nothing to compare\n");
        return;
    }
    static tide_world one, many, kept;
    for (int attempt = 0; attempt < 5; attempt++) {
        tide_world_init(&one, 1.0f);
        tide_world_init(&many, 1.0f);
        for (int i = 0; i < TICKS; i++) {
            tide_world_copy(&kept, &many); // Its pages are shared every tick, as a session's are
            tide_world_tick(&one);
            tide_world_tick_on(&many, pool);
            TIDE_CHECK(tide_world_hash(&one) == tide_world_hash(&many));
            TIDE_CHECK(same_bytes(&one, &many));
        }
        tide_world_free(&kept);
        tide_world_free(&many);
        tide_world_free(&one);
    }
}
#endif
