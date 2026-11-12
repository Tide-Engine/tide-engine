// A per-entity system that spawns, sends and destroys runs its entities'
// order in runs on threads, with one thread's results (ordered_split.tide):
// the same events in the same order, the same entities and the same world,
// tick by tick.

#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/jobs.h"
#include "tide_test.h"
#ifdef TIDE_ORDERED_SPLIT_THREADS
#include "tide/platform.h"
#endif

// A pool that's one thread saying it has eight: a round runs its work as
// thread 1 first, which takes every task there is, and then as the caller's.
// Every run has a queue of its own there, joined after: the same every time,
// so a failure is a failure on any machine. The platform's pool, below, runs
// them at once.
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
    if (tide_list_count(la) != tide_list_count(lb)) return false;
    for (int32_t i = 0; i < tide_list_count(la); i++) {
        const int32_t *x = tide_list_read(&a->heap, la, i, sizeof(int32_t));
        const int32_t *y = tide_list_read(&b->heap, lb, i, sizeof(int32_t));
        if (!x || !y || *x != *y) return false;
    }
    return true;
}

static bool same_log(const tide_world *a, const tide_world *b)
{
    return same_ints(a, a->Log.fired, b, b->Log.fired) && same_ints(a, a->Log.hit, b, b->Log.hit)
        && same_ints(a, a->Log.speeds, b, b->Log.speeds) && a->Log.rounds == b->Log.rounds; // Speeds by their bits
}

TIDE_TEST(ordered_split_runs_queue_what_one_thread_queues)
{
    static tide_world one, eight;
    tide_world_init(&one, 1.0f);
    tide_world_init(&eight, 1.0f);
    const tide_jobs fake = {.threads = 8u, .run = second_first};
    rounds = 0;
    for (int i = 0; i < 6; i++) {
        tide_world_tick(&one);
        tide_world_tick_on(&eight, &fake);
        TIDE_CHECK(tide_world_hash(&one) == tide_world_hash(&eight));
        TIDE_CHECK(same_log(&one, &eight));
    }
    TIDE_CHECK(rounds >= 6u); // Every tick was worth threads
    TIDE_CHECK(tide_list_count(one.Log.fired) > 4000 && tide_list_count(one.Log.hit) > 3000 && one.Log.rounds > 3000);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_world_free(&eight);
    tide_world_free(&one);
}

#ifdef TIDE_ORDERED_SPLIT_THREADS
TIDE_TEST(ordered_split_on_the_pool_matches_one_thread)
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
        for (int i = 0; i < 6; i++) {
            tide_world_tick(&one);
            tide_world_tick_on(&many, pool);
            TIDE_CHECK(tide_world_hash(&one) == tide_world_hash(&many));
            TIDE_CHECK(same_log(&one, &many));
        }
        tide_world_free(&many);
        tide_world_free(&one);
    }
}
#endif
