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
#include "tide/worker.h"
#include "tide_test.h"

static tide_world one;      // Ticked on this thread
static tide_world many;     // On every core
static tide_world snapshot; // Of `many`, before each tick: it shares its pages

extern atomic_int jobs_tasks_elsewhere; // jobs_tasks.c
extern atomic_int jobs_tasks_slept;

// The threads that took part in a round of work, a bit each.
static uint32_t threads_seen;

static TIDE_THREAD_LOCAL bool background_caller;
typedef struct background_probe {
    const tide_jobs *jobs;
    uint32_t values[128];
    uint32_t calls[128];
    bool elsewhere;
    bool released;
} background_probe;

static void background_step(void *context, const uint32_t index)
{
    background_probe *p = context;
    p->values[index] = index * 7u + 3u;
    __atomic_fetch_add(&p->calls[index], 1u, __ATOMIC_RELAXED);
}

static void background_work(void *context)
{
    background_probe *p = context;
    p->elsewhere = !background_caller;
    const tide_jobs *previous = tide_worker_enter(p->jobs);
    tide_parallel_for(128, background_step, p, 1000000u);
    tide_worker_leave(previous);
}

static void background_release(void *context)
{
    background_probe *p = context;
    p->released = background_caller;
}

TIDE_TEST(jobs_background_workers_share_parallel_work_without_waiting_on_the_frame)
{
    const tide_jobs *jobs = tide_platform_jobs();
    if (!jobs || !jobs->background) return;
    background_caller = true;
    tide_workers *workers = NULL;
    background_probe probes[64] = {{0}};
    uint32_t tickets[64];
    const uint32_t count = jobs->threads * 2u < 64u ? jobs->threads * 2u : 64u;
    for (uint32_t i = 0; i < count; i++) {
        probes[i].jobs = jobs;
        tickets[i] = tide_worker_start(&workers, jobs, background_work, background_release, &probes[i]);
    }
    const uint64_t until = tide_time_now_ns() + 10000000000ull;
    for (uint32_t i = 0; i < count; i++) {
        while (!tide_worker_poll(workers, tickets[i]) && tide_time_now_ns() < until) tide_yield();
        TIDE_CHECK(tide_worker_poll(workers, tickets[i]) == &probes[i]);
        TIDE_CHECK(probes[i].elsewhere);
        for (uint32_t k = 0; k < 128; k++) {
            TIDE_CHECK(probes[i].values[k] == k * 7u + 3u);
            TIDE_CHECK(probes[i].calls[k] == 1u);
        }
        tide_worker_forget(workers, tickets[i]);
        TIDE_CHECK(probes[i].released);
    }
    tide_workers_free(workers);
    background_caller = false;
}

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

// A round that calls in a few of the pool's threads (run_some) has them in
// it, beside the caller's.
TIDE_TEST(jobs_some_workers_join)
{
    const tide_jobs *jobs = tide_platform_jobs();
    if (!jobs || !jobs->run_some) {
        printf("    one core: nothing to check\n");
        return;
    }
    // On the web the workers are made by the first round that wants them all
    jobs->run(jobs->self, note_thread, NULL);
    static uint32_t some_seen;
    for (int round = 0; round < 20000 && !(some_seen & ~1u); round++) {
        threads_seen = 0;
        jobs->run_some(jobs->self, 2, note_thread, NULL);
        some_seen |= threads_seen;
    }
    TIDE_CHECK(some_seen & ~1u);
}

// A table with megabytes in it: an entity and four numbers a row, in chunks
// of 2,048 rows.
static const uint32_t wide_sizes[] = {sizeof(tide_entity), 4, 4, 4, 4};
static const tide_columns wide = {5, 11, wide_sizes};

static void widen(tide_table *t, const uint32_t rows)
{
    for (uint32_t i = 0; i < rows; i++) {
        const uint32_t row = tide_table_add(t, &wide);
        *(tide_entity *)tide_table_cell(t, &wide, row, 0) = (tide_entity){row, 1};
        for (uint32_t k = 1; k < 5; k++) *(uint32_t *)tide_table_cell(t, &wide, row, k) = row * 2654435761u + k;
    }
}

// What a hash would read again is hashed on a few of the pool's threads
// first (tide_pages_hash), each page by one of them, whichever takes it, and
// the hash that follows is the one a twin gets on one thread: round after
// round of changes, megabytes of them or a few pages, which are left to this
// thread. And as a game's own hash (tide_game.hash_world_on), whose generated
// code gathers every page its hash asks of.
TIDE_TEST(jobs_hash_what_changed_on_threads)
{
    const tide_jobs *jobs = tide_platform_jobs();
    if (!jobs || !jobs->run_some) {
        printf("    one core: nothing to compare\n");
        return;
    }
    enum { ROWS = 200000 };
    tide_table a = {0};
    tide_table b = {0};
    widen(&a, ROWS);
    widen(&b, ROWS);
    uint32_t dice = 1;
    int shared = 0;
    int alone = 0;
    for (int round = 0; round < 60; round++) {
        const uint32_t rows = round % 3 == 0 ? 40u : 4000u;
        for (uint32_t i = 0; i < rows; i++) {
            dice = dice * 1664525u + 1013904223u;
            const uint32_t row = (dice >> 8) % ROWS;
            const uint32_t column = 1u + (dice >> 28) % 4u;
            *(uint32_t *)tide_table_cell(&a, &wide, row, column) = dice;
            *(uint32_t *)tide_table_cell(&b, &wide, row, column) = dice;
        }
        tide_stale_pages stale = {0};
        tide_table_stale(&a, &wide, &stale);
        const uint32_t pages = stale.count;
        const uint32_t hashed = tide_pages_hash(&stale, jobs);
        TIDE_REQUIRE(hashed == 0u || hashed == pages);
        if (hashed) {
            shared++;
            tide_table_stale(&a, &wide, &stale); // Every one of them has its hash kept
            TIDE_REQUIRE(stale.count == 0u);
            (void)tide_pages_hash(&stale, NULL);
        } else {
            alone++;
        }
        if (tide_table_hash(TIDE_HASH_START, &a, &wide) != tide_table_hash(TIDE_HASH_START, &b, &wide)) {
            printf("    round %d came out different\n", round);
        }
        TIDE_REQUIRE(tide_table_hash(TIDE_HASH_START, &a, &wide) == tide_table_hash(TIDE_HASH_START, &b, &wide));
    }
    printf("    %d rounds were hashed with threads called in, and %d on this one alone\n", shared, alone);
    // The first round's is every page, as no hash was kept of any yet, then two rounds in three
    TIDE_CHECK(shared == 41 && alone == 19);
    tide_table_free(&a, &wide);
    tide_table_free(&b, &wide);

    tide_world_init(&one, 1.0f / 60.0f);
    tide_world_init(&many, 1.0f / 60.0f);
    for (int t = 0; t < 60; t++) {
        tide_world_tick(&one);
        tide_world_tick(&many);
        TIDE_REQUIRE(tide_game_api.hash_world_on(&many, jobs) == tide_world_hash(&one));
        TIDE_REQUIRE(tide_world_hash(&many) == tide_world_hash(&one));
    }
    tide_world_free(&one);
    tide_world_free(&many);
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

    // Generators rolled on many threads: every drifter went on from its own, so
    // they're where one thread left them (the hashes say), and no two alike.
    // Embers were spawned on threads and seeded once their handles were final:
    // each rolled something of its own first.
    int drifters = 0;
    int embers = 0;
    int alike = 0;
    int64_t last_state = 0;
    int32_t last_first = 0;
    for (uint32_t i = 0; i < many.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&many.entities, i);
        if (tide_has_Drifter(&many, e)) {
            const Drifter drifter = tide_get_Drifter(&many, e);
            TIDE_CHECK(drifter.random.state != 0 && drifter.random.state != last_state);
            TIDE_CHECK(drifter.rolls >= 240 && drifter.rolls <= 240 * 6);
            last_state = drifter.random.state;
            drifters++;
        }
        if (tide_has_Ember(&many, e)) {
            const Ember ember = tide_get_Ember(&many, e);
            TIDE_CHECK(ember.random.state != 0 && tide_has_Drifter(&many, ember.of));
            if (ember.first) alike += ember.first == last_first;
            if (ember.first) last_first = ember.first;
            embers++;
        }
    }
    TIDE_CHECK(drifters == 5000 && embers > 500 && alike < 3);

    // The ticks from here are as many as it takes for threads to have done
    // something, which is theirs to say: what's counted above is of the 240.

    // Work C shared out as tasks (jobs_tasks.c): each ran once, and the pool's
    // threads took some. A thread only takes one when it has nothing else, so
    // a few more ticks if none has yet.
    TIDE_CHECK(one.Stats.lost == 0 && many.Stats.lost == 0 && one.Stats.shared > 0);
    for (int t = 0; t < 20000 && atomic_load(&jobs_tasks_elsewhere) == 0; t++) {
        tide_world_tick(&one);
        tide_world_tick_on(&many, jobs);
        TIDE_REQUIRE(tide_world_hash(&one) == tide_world_hash(&many));
    }
    printf("    %d of C's tasks ran on another thread than the one that started them\n", atomic_load(&jobs_tasks_elsewhere));
    TIDE_CHECK(atomic_load(&jobs_tasks_elsewhere) > 0);

    // C's workers that wait for each other in stages (jobs_tasks.c) sleep while
    // they wait, on the web, and are woken for each stage: one that wasn't
    // would have held the tick up for good. Elsewhere they never sleep, as
    // there the CPU's pause and the system's yield do for a wait
#ifdef __wasm__
    const bool waits_sleep = true;
#else
    const bool waits_sleep = false;
#endif
    for (int t = 0; t < 20000 && waits_sleep && atomic_load(&jobs_tasks_slept) == 0; t++) {
        tide_world_tick(&one);
        tide_world_tick_on(&many, jobs);
        TIDE_REQUIRE(tide_world_hash(&one) == tide_world_hash(&many));
    }
    printf("    %d of their waits slept\n", atomic_load(&jobs_tasks_slept));
    TIDE_CHECK(waits_sleep ? atomic_load(&jobs_tasks_slept) > 0 : atomic_load(&jobs_tasks_slept) == 0);
    tide_world_free(&one);
    tide_world_free(&many);
    tide_world_free(&snapshot);
}

// ---------------------------------------------------------------------------
// A pool whose threads can sleep (tide_jobs.more, the web's): a thread that
// finds nothing to do leaves the tick, and comes back when it's asked for.
// The tick here is one system that goes on until the other threads have left,
// then sixteen tasks at once, which wait for it.

#define BACK_TASKS 16u
static const tide_jobs *back_pool;
static void (*back_work)(void *context, uint32_t thread);
static void *back_context;
static _Thread_local uint32_t back_thread; // Which of the pool's this is, while it's in the tick
static atomic_uint back_inside;            // Threads in the tick
static atomic_uint back_entries[32];       // Times each went into this one
static atomic_uint back_first_ran;
static atomic_uint back_ran[BACK_TASKS];
static atomic_uint back_returned; // Tasks run by a thread that had left this tick and come back

static void back_counted(void *context, const uint32_t thread)
{
    (void)context;
    back_thread = thread & 31u;
    atomic_fetch_add(&back_entries[back_thread], 1u);
    atomic_fetch_add(&back_inside, 1u);
    back_work(back_context, thread);
    atomic_fetch_sub(&back_inside, 1u);
}

static void back_run(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    (void)self;
    back_work = work;
    back_context = context;
    back_pool->run(back_pool->self, back_counted, NULL);
}

static void back_more(void *self, const uint32_t count)
{
    (void)self;
    back_pool->more(back_pool->self, count);
}

static uint32_t back_first_count(const void *world, uint64_t *work)
{
    (void)world;
    *work = 1u << 30; // Enough for the tick to go on threads
    return 1;
}

static void back_first(void *world, const uint32_t task, const uint32_t pieces)
{
    (void)world;
    (void)task;
    (void)pieces;
    for (volatile uint32_t i = 0; i < 100000u; i++) {
    } // Long enough for a waiting worker to wake and join
    // Until the tick has no thread in it but this one and the caller's, which
    // stays: the others have nothing to do until this is done
    const uint32_t stay = back_thread == 0u ? 1u : 2u;
    while (atomic_load(&back_inside) > stay) {
    }
    atomic_fetch_add(&back_first_ran, 1u);
}

static uint32_t back_tasks_count(const void *world, uint64_t *work)
{
    (void)world;
    *work = 0;
    return BACK_TASKS;
}

static void back_task(void *world, const uint32_t task, const uint32_t pieces)
{
    (void)world;
    (void)pieces;
    for (volatile uint32_t i = 0; i < 100000u; i++) {
    } // Long enough for the threads asked back to take some
    if (atomic_load(&back_entries[back_thread]) > 1u) atomic_fetch_add(&back_returned, 1u);
    atomic_fetch_add(&back_ran[task], 1u);
}

TIDE_TEST(jobs_threads_come_back)
{
    back_pool = tide_platform_jobs();
    if (!back_pool || !back_pool->more) {
        printf("    this pool's threads stay in a tick until it's done: nothing to check\n");
        return;
    }
    static const uint32_t waits[1] = {0};
    const tide_system_tasks systems[2] = {
        {.count = back_first_count, .run = back_first, .name = "first"},
        {.count = back_tasks_count, .run = back_task, .wait_count = 1, .waits = waits, .name = "tasks"},
    };
    tide_queue queue = {0};
    tide_entities entities = {0};
    const tide_tick_systems tick = {systems, 2, NULL, &queue, &entities, NULL, NULL};
    const tide_jobs counted = {NULL, back_pool->threads, back_run, NULL, back_more, NULL, NULL, NULL};
    int ticks = 0;
    // A worker has to have joined the tick while its first system ran to have
    // left it: a few more ticks if none had yet
    for (; ticks < 20000 && atomic_load(&back_returned) == 0u; ticks++) {
        for (uint32_t i = 0; i < 32u; i++) atomic_store(&back_entries[i], 0u);
        for (uint32_t i = 0; i < BACK_TASKS; i++) atomic_store(&back_ran[i], 0u);
        atomic_store(&back_first_ran, 0u);
        tide_run_systems(&queue, &tick, &counted);
        TIDE_REQUIRE(atomic_load(&back_inside) == 0u && atomic_load(&back_first_ran) == 1u);
        bool once = true;
        for (uint32_t i = 0; i < BACK_TASKS; i++) once &= atomic_load(&back_ran[i]) == 1u;
        TIDE_REQUIRE(once);
    }
    printf("    %u of %u tasks were run by threads that had left the tick, after %d tick%s\n",
           (unsigned)atomic_load(&back_returned), (unsigned)BACK_TASKS, ticks, ticks == 1 ? "" : "s");
    TIDE_CHECK(atomic_load(&back_returned) > 0u);
}

// The system says how much memory the program has (tide_platform_memory),
// which needs no window: checked here, where every platform's tests run.
TIDE_TEST(platform_counts_its_memory)
{
    const uint64_t bytes = tide_platform_memory();
    printf("    %.1f MB\n", (double)bytes / (1024.0 * 1024.0));
    TIDE_CHECK(bytes > 1u << 20); // In bytes: no program with a world in it has less than a megabyte
}

// The system gives random bits (tide_platform_entropy), which matches' seeds
// are made from: other ones each time, here where every platform's tests run.
TIDE_TEST(platform_gives_entropy)
{
    uint64_t bits[4];
    for (int i = 0; i < 4; i++) bits[i] = tide_platform_entropy();
    for (int a = 0; a < 4; a++) {
        TIDE_CHECK(bits[a] != 0);
        for (int b = a + 1; b < 4; b++) TIDE_CHECK(bits[a] != bits[b]);
    }
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
    const tide_tick_systems tick = {many_systems, MANY, NULL, &queue, &entities, NULL, NULL};
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
