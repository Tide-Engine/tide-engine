#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "tide_test.h"

#ifdef TIDE_IX_THREADS
#include "tide/platform.h"
#endif

// indexed_loops.tide, built with its loops' indexes (IXTEST_INDEXED) and without: each build prints what its
// world and its views came to, tick by tick, as `ixloops` lines, which tidec_indexed_loops_same compares. Each
// also checks itself: a snapshot taken, run on and rolled back to gives the same ticks again, threads give what
// one thread does, and the loops found something.

static tide_world world;
static tide_world snapshot;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static uint64_t frame(void)
{
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    return (uint64_t)local.Drawn.rows * 1000003u + (uint64_t)local.Drawn.helper;
}

TIDE_TEST(indexed_loops_give_what_plain_loops_give)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    uint64_t at[80] = {0};
    for (int t = 0; t < 80; t++) {
        tide_world_tick(&world);
        const uint64_t drawn = frame();
        at[t] = tide_world_hash(&world);
        printf("ixloops %d %016" PRIx64 " %" PRIu64 " %.9g %d %d %d %d %.9g\n", t, at[t], drawn, (double)local.Drawn.sum,
               world.Stats.teams, world.Stats.low, world.Stats.band, world.Stats.tagged, (double)world.Stats.sum);
        if (t == 30) tide_world_copy(&snapshot, &world);
    }
    TIDE_CHECK(world.Stats.teams > 0 && world.Stats.low > 0 && world.Stats.band > 0 && world.Stats.tagged > 0);
    TIDE_CHECK(local.Drawn.rows > 0 && local.Drawn.helper > 0);
#ifdef IXTEST_INDEXED
    TIDE_CHECK(world.tide_index_cache != NULL);
    TIDE_CHECK(local.tide_index_cache != NULL);
#endif
    // Back to tick 30, and on again: the cache holds pages of ticks since, which the restored world doesn't have
    tide_world_copy(&world, &snapshot);
    for (int t = 31; t < 80; t++) {
        tide_world_tick(&world);
        if (tide_world_hash(&world) != at[t]) printf("    differs after rolling back, at tick %d\n", t);
        TIDE_REQUIRE(tide_world_hash(&world) == at[t]);
    }
    // A copy starts with no cache of its own, and packed bytes carry none
    tide_world copy = {0};
    tide_world_copy(&copy, &world);
    tide_world_tick(&copy);
    tide_world_tick(&world);
    TIDE_CHECK(tide_world_hash(&copy) == tide_world_hash(&world));
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes && tide_world_pack(&world, bytes, size) == size);
    tide_world unpacked = {0};
    TIDE_REQUIRE(tide_world_unpack(&unpacked, bytes, size));
    tide_world_tick(&unpacked);
    tide_world_tick(&world);
    TIDE_CHECK(tide_world_hash(&unpacked) == tide_world_hash(&world));
    free(bytes);
    tide_world_free(&unpacked);
    tide_world_free(&copy);
    tide_world_free(&snapshot);
    tide_local_free(&local);
    tide_world_free(&world);
}

#ifdef TIDE_IX_THREADS
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

TIDE_TEST(indexed_loops_on_threads_give_what_one_thread_gives)
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
    for (int t = 0; t < 40; t++) {
        tide_world_tick(&world);
        tide_world_tick_on(&many, &jobs);
        if (tide_world_hash(&world) != tide_world_hash(&many)) printf("    %u threads differ at tick %d\n", threads, t);
        TIDE_REQUIRE(tide_world_hash(&world) == tide_world_hash(&many));
    }
#ifndef __wasm__ // The web's ticks need more work before they take threads
    TIDE_CHECK(limited.runs > 0);
#endif
    tide_world_free(&many);
    tide_world_free(&world);
}
#endif
