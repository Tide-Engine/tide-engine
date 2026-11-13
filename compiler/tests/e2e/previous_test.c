// `previous` bindings (previous.tide): a query that reads components as the
// tick started. What it reads is the tick's start whatever ran before it in
// the tick, on one thread and on many; the world is one thread's tick by
// tick, written in place and beside a snapshot; and nothing of what the tick
// kept is in the world, its hash, its bytes or its snapshots.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/jobs.h"
#include "tide_test.h"
#ifdef TIDE_PREVIOUS_THREADS
#include "tide/platform.h"
#endif

// The build with every loop gone through plainly (tidec_e2e_previous_plain) only prints its ticks, for
// tidec_previous_same: its loops go through every row for every row
#ifdef TIDE_PREVIOUS_PLAIN
static const bool plain = true;
#else
static const bool plain = false;
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

static bool same_tally(const tide_world *a, const tide_world *b)
{
    return memcmp(&a->Tally, &b->Tally, sizeof a->Tally) == 0;
}

// What the systems that run first read is what the ones that read `previous` after every writer did
static void check_start(const tide_world *w, const int tick)
{
    TIDE_CHECK(w->Tally.startPlaces == w->Tally.beforePlaces);
    TIDE_CHECK(w->Tally.startNames == w->Tally.beforeNames);
    TIDE_CHECK(w->Tally.startRanks == w->Tally.beforeRanks);
    if (tick > 2) TIDE_CHECK(w->Tally.nowRanks != w->Tally.beforeRanks); // ...and not the world as they found it
}

// One thread against `jobs`; with `beside`, each world keeps a snapshot of the
// tick before, so every page a tick writes is shared as it starts
static void compare(const tide_jobs *jobs, const bool beside)
{
    static tide_world one, many, one_was, many_was;
    tide_world_init(&one, 1.0f);
    tide_world_init(&many, 1.0f);
    long long last_places = 0;
    for (int i = 0; i < 10; i++) {
        if (beside) {
            tide_world_copy(&one_was, &one);
            tide_world_copy(&many_was, &many);
        }
        const uint64_t before = tide_world_hash(&one);
        tide_world_tick(&one);
        tide_world_tick_on(&many, jobs);
        TIDE_CHECK(tide_world_hash(&one) == tide_world_hash(&many));
        TIDE_CHECK(same_tally(&one, &many));
        check_start(&one, i);
        check_start(&many, i);
        TIDE_CHECK(i < 2 || one.Tally.startPlaces != last_places); // The world moves on
        last_places = one.Tally.startPlaces;
        TIDE_CHECK(many.tide_previous != NULL);
        if (beside) {
            TIDE_CHECK(tide_world_hash(&one_was) == before && tide_world_hash(&many_was) == before);
            tide_world_free(&one_was);
            tide_world_free(&many_was);
        }
    }
    TIDE_CHECK(one.Tally.lost > 100 && one.Tally.heard > 0 && one.Tally.startNames != 0 && one.Tally.told > 0);
    tide_world_free(&many);
    tide_world_free(&one);
}

TIDE_TEST(previous_reads_the_ticks_start_on_threads_as_on_one)
{
    const tide_jobs fake = {.threads = 8u, .run = second_first};
    if (plain) return;
    rounds = 0;
    compare(&fake, false);
    TIDE_CHECK(rounds >= 10u); // Every tick was worth threads
    compare(&fake, true);
    TIDE_CHECK(tide_scratch_mark() == 0);
}

// The world's bytes, a copy and a snapshot rolled back go on as the world does: what a tick kept is no part of them
TIDE_TEST(previous_is_no_part_of_the_world)
{
    static tide_world w, copy, back, unpacked;
    tide_world_init(&w, 1.0f);
    for (int i = 0; i < 5; i++) tide_world_tick(&w);
    tide_world_copy(&copy, &w);
    tide_world_copy(&back, &w);
    const uint32_t size = tide_world_pack(&w, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL && tide_world_pack(&w, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&unpacked, bytes, size));
    free(bytes);
    TIDE_CHECK(!unpacked.tide_previous);
    uint64_t hashes[6];
    for (int i = 0; i < 6; i++) {
        tide_world_tick(&w);
        hashes[i] = tide_world_hash(&w);
        tide_world_tick(&copy);
        tide_world_tick(&unpacked);
        TIDE_CHECK(tide_world_hash(&copy) == hashes[i]);
        TIDE_CHECK(tide_world_hash(&unpacked) == hashes[i]);
        check_start(&unpacked, 5 + i);
        // What the same game says with every loop gone through plainly (tidec_previous_same)
        printf("previousloops %d %016llx %lld %lld %lld %d %d %lld %d %lld\n", i, (unsigned long long)hashes[i],
               (long long)w.Tally.beforePlaces, (long long)w.Tally.beforeNames, (long long)w.Tally.beforeRanks, w.Tally.lost,
               w.Tally.heard, (long long)w.Tally.heardSum, w.Tally.told, (long long)w.Tally.toldSum);
    }
    // Rolled back and run again
    tide_world_copy(&w, &back);
    for (int i = 0; i < 6; i++) {
        tide_world_tick(&w);
        TIDE_CHECK(tide_world_hash(&w) == hashes[i]);
    }
    tide_world_free(&unpacked);
    tide_world_free(&back);
    tide_world_free(&copy);
    tide_world_free(&w);
}

#ifdef TIDE_PREVIOUS_THREADS
TIDE_TEST(previous_on_the_pool_is_one_threads)
{
    const tide_jobs *pool = tide_platform_jobs();
    if (plain) return;
    if (!pool || pool->threads < 2u) {
        printf("    one core: nothing to compare\n");
        return;
    }
    for (int attempt = 0; attempt < 3; attempt++) {
        compare(pool, false);
        compare(pool, true);
    }
}
#endif
