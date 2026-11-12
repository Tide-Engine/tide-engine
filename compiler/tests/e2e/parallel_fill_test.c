// Parallel loops over a match singleton's lists go to the pool's threads in a
// tick that runs on one thread (its reckoning found the lists empty, as they
// were filled in the tick), and a short list of costly steps does too, with
// the same results as on one thread (parallel_fill.tide).

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/jobs.h"
#include "tide_test.h"
#ifdef TIDE_PARALLEL_FILL_THREADS
#include "tide/platform.h"
#endif

#define COUNT 2000

// A pool that's one thread saying it has two: a round runs its work on
// thread 1 first, which takes every step there is, as a thread other than the
// caller's would, and then on the caller's.
static void second_first(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    (*(uint32_t *)self)++;
    work(context, 1);
    work(context, 0);
}

static void start(tide_world *w)
{
    memset(w, 0, sizeof *w);
    tide_world_init(w, 1.0f);
    w->Filled.count = COUNT;
}

static float element(const tide_world *w, const tide_list list, const int32_t i)
{
    const float *at = tide_list_read(&w->heap, list, i, sizeof(float));
    return at ? *at : -1.0f;
}

TIDE_TEST(parallel_list_filled_in_its_tick_uses_the_pool)
{
    tide_world one, many;
    start(&one);
    start(&many);
    uint32_t rounds = 0;
    const tide_jobs two = {&rounds, 2u, second_first, NULL, NULL, NULL, NULL, NULL, false, NULL, NULL};

    // The first tick finds both lists empty: one thread, and a round for each loop
    tide_world_tick(&one);
    tide_world_tick_on(&many, &two);
    TIDE_CHECK(rounds == 2u);
    if (rounds != 2u) printf("    %u rounds\n", (unsigned)rounds);
    TIDE_CHECK(tide_world_hash(&one) == tide_world_hash(&many));
    TIDE_CHECK(tide_list_count(many.Filled.values) == COUNT && tide_list_count(many.Filled.few) == 400);
    bool same = true;
    for (int32_t i = 0; i < COUNT; i++) same &= element(&one, one.Filled.values, i) == element(&many, many.Filled.values, i);
    for (int32_t i = 0; i < 400; i++) same &= element(&one, one.Filled.few, i) == element(&many, many.Filled.few, i);
    TIDE_CHECK(same);

    // Later ticks reckon their steps: on threads, in one round
    rounds = 0;
    tide_world_tick(&one);
    tide_world_tick_on(&many, &two);
    TIDE_CHECK(rounds == 1u);
    if (rounds != 1u) printf("    %u rounds\n", (unsigned)rounds);
    TIDE_CHECK(tide_world_hash(&one) == tide_world_hash(&many));
    tide_world_free(&many);
    tide_world_free(&one);
}

#ifdef TIDE_PARALLEL_FILL_THREADS
// The platform layer's pool, watched: which of its threads came into a round.
typedef struct watched {
    const tide_jobs *pool;
    void (*work)(void *context, uint32_t thread);
    void *context;
    _Atomic uint64_t threads;
} watched;

static void watched_work(void *context, const uint32_t thread)
{
    watched *w = context;
    w->work(w->context, thread);
    atomic_fetch_or(&w->threads, UINT64_C(1) << (thread < 63u ? thread : 63u));
}

static void watched_run(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    watched *w = self;
    w->work = work;
    w->context = context;
    w->pool->run(w->pool->self, watched_work, w);
}

TIDE_TEST(parallel_list_filled_in_its_tick_goes_to_other_threads)
{
    const tide_jobs *pool = tide_platform_jobs();
    if (!pool || pool->threads < 2u) {
        printf("    one core: nothing to compare\n");
        return;
    }
    watched w = {.pool = pool};
    atomic_init(&w.threads, 0u);
    tide_jobs jobs = *pool;
    jobs.self = &w;
    jobs.run = watched_run;
    jobs.run_some = NULL;
    // A first tick on one thread, whose loops' rounds the pool's threads join
    // (each a few milliseconds of work, which a woken thread has time to join)
    for (int attempt = 0; attempt < 50 && !(atomic_load(&w.threads) & ~UINT64_C(1)); attempt++) {
        tide_world one, many;
        start(&one);
        start(&many);
        tide_world_tick(&one);
        tide_world_tick_on(&many, &jobs);
        TIDE_CHECK(tide_world_hash(&one) == tide_world_hash(&many));
        tide_world_tick(&one); // And one on threads, unwatched
        tide_world_tick_on(&many, pool);
        TIDE_CHECK(tide_world_hash(&one) == tide_world_hash(&many));
        tide_world_free(&many);
        tide_world_free(&one);
    }
    const uint64_t seen = atomic_load(&w.threads);
    printf("    %u threads in the pool; its rounds had %d\n", (unsigned)pool->threads, __builtin_popcountll(seen));
    TIDE_CHECK(seen & ~UINT64_C(1));
}
#endif
