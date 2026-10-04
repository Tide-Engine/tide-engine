// Ticks on every core (tide_platform_jobs) give the same worlds as ticks on
// one thread, tick after tick (platform/tests/jobs.tide).

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "tide/jobs.h"
#include "tide/platform.h"
#include "tide/time.h"
#include "tide_test.h"

static tide_world one;      // Ticked on this thread
static tide_world many;     // On every core
static tide_world snapshot; // Of `many`, before each tick: it shares its pages

// The threads that took part in a round of work, a bit each.
static uint32_t threads_seen;

static void note_thread(void *context, const uint32_t thread)
{
    (void)context;
    __atomic_fetch_or(&threads_seen, 1u << (thread & 31u), __ATOMIC_RELAXED);
    for (volatile uint32_t i = 0; i < 100000u; i++) {
    } // Long enough for a waiting worker to wake and join
}

// The pool's workers take part in its rounds of work, not only the caller (on
// the web: workers, which run where the page shares the program's memory).
TIDE_TEST(jobs_workers_join)
{
    const tide_jobs *jobs = tide_platform_jobs();
    if (!jobs) {
        printf("    one core: nothing to check\n");
        return;
    }
    for (int round = 0; round < 20000 && !(threads_seen & ~1u); round++) jobs->run(jobs->self, note_thread, NULL);
    printf("    %u threads in the pool; one round had %d\n", (unsigned)jobs->threads, __builtin_popcount(threads_seen));
    TIDE_CHECK(threads_seen & ~1u);
}

TIDE_TEST(jobs_threads_change_nothing)
{
    const tide_jobs *jobs = tide_platform_jobs();
    if (!jobs) {
        printf("    one core: nothing to compare\n");
        return;
    }
    tide_world_init(&one, 1.0f / 60.0f);
    tide_world_init(&many, 1.0f / 60.0f);
    TIDE_REQUIRE(tide_world_hash(&one) == tide_world_hash(&many));
    for (int t = 0; t < 240; t++) {
        tide_world_copy(&snapshot, &many);
        const uint64_t before = tide_world_hash(&snapshot);
        tide_world_tick(&one);
        tide_world_tick_on(&many, jobs);
        if (tide_world_hash(&one) != tide_world_hash(&many)) printf("    tick %d came out different\n", t);
        TIDE_REQUIRE(tide_world_hash(&one) == tide_world_hash(&many));
        TIDE_REQUIRE(tide_world_hash(&snapshot) == before); // Its pages stayed as they were
    }
    TIDE_CHECK(one.Stats.hits > 100 && one.Stats.spawned == 80);
    TIDE_CHECK(one.Stats.wobbled > 1000); // Tasks went on, the same on threads
    TIDE_CHECK(tide_world_entity_count(&one) == tide_world_entity_count(&many));

    // Entities spawned on many threads: every handle kept is the real one
    TIDE_CHECK(many.Stats.chipped > 1000 && many.Stats.matched == many.Stats.chipped);
    int trails = 0;
    int shards = 0;
    for (uint32_t i = 0; i < many.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&many.entities, i);
        const Trail trail = tide_get_Trail(&many, e);
        if (tide_has_Trail(&many, e) && trail.count) {
            const Debris debris = tide_get_Debris(&many, trail.last);
            TIDE_CHECK(!tide_entity_is_temporary(trail.last) && tide_entity_equal(trail.last, trail.link.target));
            TIDE_CHECK(tide_has_Debris(&many, trail.last) && tide_entity_equal(debris.from, e) && tide_has_Size(&many, trail.last));
            trails++;
        }
        const Shard shard = tide_get_Shard(&many, e);
        if (tide_has_Shard(&many, e)) {
            TIDE_CHECK(!tide_entity_is_null(shard.of) && !tide_entity_is_temporary(shard.of));
            if (shard.debris) TIDE_CHECK(tide_has_Debris(&many, shard.of)); // Rocks burn up, but debris stays
            shards += shard.debris;
        }
    }
    TIDE_CHECK(trails == 6000 && shards == many.Stats.chipped);
    tide_world_free(&one);
    tide_world_free(&many);
    tide_world_free(&snapshot);
}

// ---------------------------------------------------------------------------
// A tick of thousands of systems, made up here rather than generated: each
// starts only once the systems it waits for are done, and runs once, however
// the tick spreads them over the threads.

#define MANY 3000
static atomic_uint many_ran[MANY];
static atomic_uint many_early; // Systems that started before one they wait for was done
static uint32_t many_waits[MANY][4];
static tide_system_tasks many_systems[MANY];
static uint32_t many_spin; // How long each system works

static void many_step(const uint32_t s)
{
    const tide_system_tasks *system = &many_systems[s];
    for (uint32_t i = 0; i < system->wait_count; i++) {
        if (!atomic_load(&many_ran[system->waits[i]])) atomic_fetch_add(&many_early, 1u);
    }
    for (volatile uint32_t i = 0; i < many_spin; i++) {
    }
    atomic_fetch_add(&many_ran[s], 1u);
}

static uint32_t many_count(const void *world, uint64_t *work)
{
    (void)world;
    *work = 1u << 20; // Enough, together, for the tick to go on threads
    return 1;
}

// A system's function says which system it is, so each has one of its own:
// many_run_1000 to many_run_3999.
#define MANY_RUN(n)                                                                    \
    static void many_run_##n(void *world, const uint32_t task, const uint32_t pieces) \
    {                                                                                  \
        (void)world;                                                                   \
        (void)task;                                                                    \
        (void)pieces;                                                                  \
        many_step(n - 1000);                                                           \
    }
#define MANY_NAME(n) many_run_##n,
#define TEN(F, p) F(p##0) F(p##1) F(p##2) F(p##3) F(p##4) F(p##5) F(p##6) F(p##7) F(p##8) F(p##9)
#define HUNDRED(F, p)                                                                                            \
    TEN(F, p##0) TEN(F, p##1) TEN(F, p##2) TEN(F, p##3) TEN(F, p##4) TEN(F, p##5) TEN(F, p##6) TEN(F, p##7) \
    TEN(F, p##8) TEN(F, p##9)
#define THOUSAND(F, p)                                                                                    \
    HUNDRED(F, p##0) HUNDRED(F, p##1) HUNDRED(F, p##2) HUNDRED(F, p##3) HUNDRED(F, p##4) HUNDRED(F, p##5) \
    HUNDRED(F, p##6) HUNDRED(F, p##7) HUNDRED(F, p##8) HUNDRED(F, p##9)
THOUSAND(MANY_RUN, 1)
THOUSAND(MANY_RUN, 2)
THOUSAND(MANY_RUN, 3)
static void (*const many_runs[MANY])(void *, uint32_t, uint32_t) = {THOUSAND(MANY_NAME, 1) THOUSAND(MANY_NAME, 2)
                                                                    THOUSAND(MANY_NAME, 3)};

TIDE_TEST(jobs_thousands_of_systems)
{
    // Each waits for up to four of the systems before it, most of them near
    // it: a schedule made up here, the same every run
    uint32_t random = 12345u;
    for (uint32_t s = 0; s < MANY; s++) {
        uint32_t count = 0;
        for (uint32_t i = 0; s > 0 && i < 4u; i++) {
            random = random * 1664525u + 1013904223u;
            if ((random >> 24 & 3u) == 0) continue;
            const uint32_t reach = (random >> 28) < 12u && s > 16u ? 16u : s;
            const uint32_t on = s - 1u - (random >> 8) % reach;
            bool have = false;
            for (uint32_t k = 0; k < count; k++) have |= many_waits[s][k] == on;
            if (!have) many_waits[s][count++] = on;
        }
        many_systems[s] = (tide_system_tasks){.count = many_count, .run = many_runs[s], .wait_count = count,
                                              .waits = many_waits[s], .name = "many"};
    }
    tide_queue queue = {0};
    tide_entities entities = {0};
    const tide_tick_systems tick = {many_systems, MANY, NULL, &queue, 16u, &entities, NULL};
    const tide_jobs *jobs = tide_platform_jobs();
    for (int pass = 0; pass < (jobs ? 4 : 2); pass++) {
        const tide_jobs *with = pass >= 2 ? jobs : NULL; // On one thread, then on every core
        many_spin = pass % 2 ? 2000u : 0u;               // Systems with nothing to do, then with a little
        uint64_t best = UINT64_MAX;
        for (int t = 0; t < 20; t++) {
            for (uint32_t s = 0; s < MANY; s++) atomic_store(&many_ran[s], 0u);
            const uint64_t began = tide_time_now_ns();
            tide_run_systems(&queue, &tick, with);
            const uint64_t took = tide_time_now_ns() - began;
            if (took < best) best = took;
            bool once = true;
            for (uint32_t s = 0; s < MANY; s++) once &= atomic_load(&many_ran[s]) == 1u;
            TIDE_REQUIRE(once);
        }
        TIDE_CHECK(atomic_load(&many_early) == 0u);
        printf("    %d systems with %s to do: a tick takes %.2f ms on %s\n", MANY, many_spin ? "a little" : "nothing",
               (double)best * 1e-6, with ? "every core" : "one thread");
    }
}
