// A parallel loop over a query's rows gives what the foreach that does the
// same gives: the same rows, and the events its steps send in the same order,
// on one thread and on the pool's (parallel_query.tide).

#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/jobs.h"
#include "tide_test.h"
#ifdef TIDE_PARALLEL_QUERY_THREADS
#include "tide/platform.h"
#endif

// A pool that's one thread saying it has two: a round runs its work on
// thread 1 first, which takes every task there is, and then on the caller's.
static void second_first(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    (*(uint32_t *)self)++;
    work(context, 1);
    work(context, 0);
}

static int32_t at(const tide_world *w, const tide_list l, const int32_t i)
{
    const int32_t *v = tide_list_read(&w->heap, l, i, sizeof(int32_t));
    return v ? *v : -1;
}

static float sum_at(const tide_world *w, const int32_t i)
{
    const float *v = tide_list_read(&w->heap, w->Log.sums, i, sizeof(float));
    return v ? *v : -1.0f;
}

static bool same_list(const tide_world *w, const tide_list a, const tide_list b)
{
    if (tide_list_count(a) != tide_list_count(b)) return false;
    for (int32_t i = 0; i < tide_list_count(a); i++) {
        if (at(w, a, i) != at(w, b, i)) return false;
    }
    return true;
}

// The rows the foreach and both loops go through: fighters (i % 3 != 2),
// not benched (i % 5 != 0), past the guard (i % 7 != 3, x * 2 <= 5900)
static int32_t expected(void)
{
    int32_t n = 0;
    for (int32_t i = 0; i < 6000; i++) n += i % 3 != 2 && i % 5 != 0 && i % 7 != 3 && i <= 5900;
    return n;
}

static void check_log(const tide_world *w)
{
    const int32_t n = expected();
    TIDE_CHECK(tide_list_count(w->Log.serial) == n);
    TIDE_CHECK(same_list(w, w->Log.serial, w->Log.atOnce));
    TIDE_CHECK(same_list(w, w->Log.serial, w->Log.handled));
    TIDE_CHECK(tide_list_count(w->Log.hitSerial) == n);
    TIDE_CHECK(same_list(w, w->Log.hitSerial, w->Log.hitParallel));
    TIDE_CHECK(same_list(w, w->Log.serial, w->Log.hitSerial)); // Each row's Hit went to its own entity
    TIDE_CHECK(tide_list_count(w->Log.sums) == 3 * n);
    bool sums = true;
    for (int32_t i = 0; i < n; i++) sums &= sum_at(w, i) == sum_at(w, n + i) && sum_at(w, i) == sum_at(w, 2 * n + i);
    TIDE_CHECK(sums);
}

TIDE_TEST(parallel_query_loop_sends_in_its_foreach_order)
{
    static tide_world one, two;
    tide_world_init(&one, 1.0f);
    tide_world_init(&two, 1.0f);
    uint32_t rounds = 0;
    const tide_jobs fake = {&rounds, 2u, second_first, NULL, NULL, NULL, NULL, NULL};
    for (int i = 0; i < 3; i++) {
        tide_world_tick(&one);
        tide_world_tick_on(&two, &fake);
    }
    check_log(&one);
    check_log(&two);
    TIDE_CHECK(tide_world_hash(&one) == tide_world_hash(&two));
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_world_free(&two);
    tide_world_free(&one);
}

#ifdef TIDE_PARALLEL_QUERY_THREADS
TIDE_TEST(parallel_query_loop_on_the_pool_matches_one_thread)
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
        for (int i = 0; i < 3; i++) {
            tide_world_tick(&one);
            tide_world_tick_on(&many, pool);
            TIDE_CHECK(tide_world_hash(&one) == tide_world_hash(&many));
        }
        check_log(&many);
        TIDE_CHECK(same_list(&many, many.Log.serial, one.Log.serial));
        tide_world_free(&many);
        tide_world_free(&one);
    }
}
#endif
