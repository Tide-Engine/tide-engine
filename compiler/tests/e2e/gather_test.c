// A per-entity system that only adds to its singletons runs its rows on
// threads and what they'd change from a log, in the rows' order
// (gather.tide): the singletons' bytes, their lists and text, and the world's
// hash are one thread's, tick by tick.

#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/jobs.h"
#include "tide_test.h"
#ifdef TIDE_GATHER_THREADS
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

static bool same_list(const tide_world *a, const tide_list la, const tide_world *b, const tide_list lb, const uint32_t size)
{
    if (tide_list_count(la) != tide_list_count(lb)) return false;
    for (int32_t i = 0; i < tide_list_count(la); i++) {
        const void *x = tide_list_read(&a->heap, la, i, size);
        const void *y = tide_list_read(&b->heap, lb, i, size);
        if (!x || !y || memcmp(x, y, size) != 0) return false;
    }
    return true;
}

// The singletons as bytes (their handles too: the heaps handed out the same blocks), and what the lists hold
static bool same_values(const tide_world *a, const tide_world *b)
{
    return memcmp(&a->Crowd, &b->Crowd, sizeof a->Crowd) == 0 && memcmp(&a->Scenery, &b->Scenery, sizeof a->Scenery) == 0
        && memcmp(&a->Flat, &b->Flat, sizeof a->Flat) == 0
        && same_list(a, a->Crowd.spots, b, b->Crowd.spots, sizeof(Spot))
        && same_list(a, a->Crowd.names, b, b->Crowd.names, sizeof(tide_text))
        && same_list(a, a->Scenery.kept, b, b->Scenery.kept, sizeof(int32_t))
        && same_list(a, a->Log.seen, b, b->Log.seen, sizeof(int32_t));
}

static void compare(const tide_jobs *jobs, const bool beside)
{
    static tide_world one, many, one_was, many_was;
    tide_world_init(&one, 1.0f);
    tide_world_init(&many, 1.0f);
    for (int i = 0; i < 8; i++) {
        if (beside) {
            tide_world_copy(&one_was, &one);
            tide_world_copy(&many_was, &many);
        }
        tide_world_tick(&one);
        tide_world_tick_on(&many, jobs);
        TIDE_CHECK(tide_world_hash(&one) == tide_world_hash(&many));
        TIDE_CHECK(same_values(&one, &many));
        if (beside) {
            tide_world_free(&one_was);
            tide_world_free(&many_was);
        }
    }
    TIDE_CHECK(tide_list_count(one.Crowd.spots) > 1800 && tide_list_count(one.Crowd.names) > 15 && one.Crowd.total != 0.0f);
    TIDE_CHECK(one.Crowd.standing.at[0] > 900 && one.Crowd.standing.at[1] > 900 && one.Scenery.men > 2000);
    TIDE_CHECK(tide_list_count(one.Scenery.kept) > 10 && one.Flat.count > 3000 && tide_list_count(one.Log.seen) > 100);
    tide_world_free(&many);
    tide_world_free(&one);
}

TIDE_TEST(gather_on_threads_is_one_threads)
{
    const tide_jobs fake = {.threads = 8u, .run = second_first};
    rounds = 0;
    compare(&fake, false);
    TIDE_CHECK(rounds >= 8u); // Every tick was worth threads
    compare(&fake, true);
    TIDE_CHECK(tide_scratch_mark() == 0);
}

#ifdef TIDE_GATHER_THREADS
TIDE_TEST(gather_on_the_pool_is_one_threads)
{
    const tide_jobs *pool = tide_platform_jobs();
    if (!pool || pool->threads < 2u) {
        printf("    one core: nothing to compare\n");
        return;
    }
    for (int attempt = 0; attempt < 4; attempt++) {
        compare(pool, false);
        compare(pool, true);
    }
}
#endif
