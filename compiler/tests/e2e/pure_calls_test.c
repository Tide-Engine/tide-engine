#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

#ifdef TIDE_PURE_THREADS
#include "tide/platform.h"
#endif

// pure_calls.tide, built as it is (PURETEST_MERGED: repeated pure calls are one, cached functions remember) and
// with neither: each build prints what its world and its view came to, tick by tick, as `purecalls` lines, which
// tidec_pure_calls_same compares. Each also checks itself: the values a call's own copy of a remembered list
// must give, a snapshot rolled back to, threads against one thread, and what the tables counted.

#define TICKS 48

static tide_world world;
static tide_world snapshot;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void frame(void)
{
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
}

#ifdef PURETEST_MERGED
static void counts(uint64_t *hits, uint64_t *misses, uint64_t *dropped)
{
    tide_cached_counts(hits, misses, dropped);
}
#endif

TIDE_TEST(merged_and_cached_calls_give_what_the_code_gives)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    uint64_t at[TICKS] = {0};
    for (int t = 0; t < TICKS; t++) {
        tide_world_tick(&world);
        frame();
        at[t] = tide_world_hash(&world);
        const Totals *totals = &world.Totals;
        printf("purecalls %d %016" PRIx64 " %.9g %.9g %.9g %d %d %d %.9g %d %.9g %d %.9g %d %.9g\n", t, at[t], (double)totals->boxes,
               (double)totals->winds, (double)totals->halves, totals->listed, totals->labels, totals->rolled, (double)totals->kept,
               totals->tried, (double)totals->tails, totals->many, (double)local.Drawn.boxes, local.Drawn.rows, (double)local.Drawn.once);
        // A call's own copy of a list a cached function made: adding to one leaves the next call's as it was made
        TIDE_CHECK(totals->listed == 605);
        // ...and of a struct with text and a list: the heaviest part's five corners, its name, and the cleared copy
        TIDE_CHECK(totals->kept == 55.0f);
        TIDE_CHECK(totals->tried == 58); // Five parts' weights, and two that fail
        TIDE_CHECK(local.Drawn.rows == 1536);
        if (t == 20) tide_world_copy(&snapshot, &world);
    }
#ifdef PURETEST_MERGED
    uint64_t hits, misses, dropped;
    counts(&hits, &misses, &dropped);
    printf("    cached: %" PRIu64 " found, %" PRIu64 " run, %" PRIu64 " let go\n", hits, misses, dropped);
    TIDE_CHECK(hits > misses);  // Most calls find their result
    TIDE_CHECK(misses > 0);
    TIDE_CHECK(dropped > 0);    // 700 arguments a tick are more than a function keeps
#endif
    // Back to tick 20, and on again: what the tables kept of later ticks is still right for these
    tide_world_copy(&world, &snapshot);
    for (int t = 21; t < TICKS; t++) {
        tide_world_tick(&world);
        if (tide_world_hash(&world) != at[t]) printf("    differs after rolling back, at tick %d\n", t);
        TIDE_REQUIRE(tide_world_hash(&world) == at[t]);
    }
    // A world carried as bytes, and a copy: neither has anything of the tables, and both go on the same
    tide_world copy = {0};
    tide_world_copy(&copy, &world);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes && tide_world_pack(&world, bytes, size) == size);
    tide_world unpacked = {0};
    TIDE_REQUIRE(tide_world_unpack(&unpacked, bytes, size));
    tide_world_tick(&copy);
    tide_world_tick(&unpacked);
    tide_world_tick(&world);
    TIDE_CHECK(tide_world_hash(&copy) == tide_world_hash(&world));
    TIDE_CHECK(tide_world_hash(&unpacked) == tide_world_hash(&world));
    free(bytes);
#ifdef TIDE_HAS_CACHED
    // Let go of, as before a reload: nothing is remembered, and the next ticks are what they'd have been
    tide_cached_free();
    uint64_t found, run, gone;
    tide_cached_counts(&found, &run, &gone);
    TIDE_CHECK(found == 0 && run == 0 && gone == 0);
    tide_world_tick(&copy);
    tide_world_tick(&world);
    TIDE_CHECK(tide_world_hash(&copy) == tide_world_hash(&world));
    tide_cached_counts(&found, &run, &gone);
    TIDE_CHECK(run > 0);
#endif
    tide_world_free(&unpacked);
    tide_world_free(&copy);
    tide_world_free(&snapshot);
    tide_local_free(&local);
    tide_world_free(&world);
}

// Two worlds of one program share the tables: each one's results are its own arguments', whichever asked first
TIDE_TEST(two_worlds_share_what_is_remembered)
{
    tide_world other = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_init(&other, 1.0f / 60.0f);
    uint64_t alone[12];
    for (int t = 0; t < 12; t++) {
        tide_world_tick(&world);
        alone[t] = tide_world_hash(&world);
    }
    for (int t = 0; t < 12; t++) {
        tide_world_tick(&other);
        TIDE_REQUIRE(tide_world_hash(&other) == alone[t]);
    }
    tide_world_free(&other);
    tide_world_free(&world);
}

#ifdef TIDE_PURE_THREADS
typedef struct limited_jobs {
    const tide_jobs *pool;
    uint32_t threads;
    uint32_t runs;
    void (*work)(void *context, uint32_t thread);
    void *context;
} limited_jobs;

static void limited_work(void *context, const uint32_t thread)
{
    limited_jobs *limited = context;
    if (thread < limited->threads) limited->work(limited->context, thread);
}

static void limited_run(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    limited_jobs *limited = self;
    limited->runs++;
    limited->work = work;
    limited->context = context;
    limited->pool->run(limited->pool->self, limited_work, limited);
}

TIDE_TEST(threads_give_what_one_thread_gives)
{
    const tide_jobs *pool = tide_platform_jobs();
    if (!pool || pool->threads < 2) {
        printf("    one core: nothing to compare\n");
        return;
    }
    const uint32_t threads = pool->threads < 4 ? pool->threads : 4;
    limited_jobs limited = {.pool = pool, .threads = threads};
    const tide_jobs jobs = {.self = &limited, .threads = threads, .run = limited_run};
    tide_world many = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_init(&many, 1.0f / 60.0f);
#ifdef TIDE_HAS_CACHED
    tide_cached_free(); // The threads' ticks fill the tables: the one thread's then finds theirs
#endif
    for (int t = 0; t < 30; t++) {
        tide_world_tick_on(&many, &jobs);
        tide_world_tick(&world);
        if (tide_world_hash(&world) != tide_world_hash(&many)) printf("    %u threads differ at tick %d\n", threads, t);
        TIDE_REQUIRE(tide_world_hash(&world) == tide_world_hash(&many));
    }
#ifndef __wasm__ // The web's ticks need more work before they take threads
    TIDE_CHECK(limited.runs > 0);
#endif
#ifdef PURETEST_MERGED
    uint64_t hits, misses, dropped;
    counts(&hits, &misses, &dropped);
    TIDE_CHECK(hits > 0 && misses > 0);
#endif
    tide_world_free(&many);
    tide_world_free(&world);
}
#endif
