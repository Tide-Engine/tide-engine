// Systems that touch different fields of one component or singleton run at
// the same time (fields.tide), with one thread's world tick by tick: written
// in place, and beside a snapshot of the tick before, where each lane a system
// writes is a page of its own that takes the old one's place.

#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/jobs.h"
#include "tide_test.h"
#ifdef TIDE_FIELDS_THREADS
#include "tide/platform.h"
#endif

// A pool that's one thread saying it has eight: a round runs its work as
// thread 1 first, which takes every task there is, and then as the caller's.
// The same every time; the platform's pool, below, runs them at once.
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

// One thread against `jobs`, twelve ticks; with `beside`, each world keeps a
// snapshot of the tick before, so every page a tick writes is shared as it starts
static void compare(const tide_jobs *jobs, const bool beside)
{
    static tide_world one, many, one_was, many_was;
    tide_world_init(&one, 1.0f);
    tide_world_init(&many, 1.0f);
    for (int i = 0; i < 12; i++) {
        if (beside) {
            tide_world_copy(&one_was, &one);
            tide_world_copy(&many_was, &many);
        }
        const uint64_t before = tide_world_hash(&one);
        tide_world_tick(&one);
        tide_world_tick_on(&many, jobs);
        TIDE_CHECK(tide_world_hash(&one) == tide_world_hash(&many));
        TIDE_CHECK(same_tally(&one, &many));
        if (beside) {
            TIDE_CHECK(tide_world_hash(&one_was) == before && tide_world_hash(&many_was) == before);
            tide_world_free(&one_was);
            tide_world_free(&many_was);
        }
    }
    TIDE_CHECK(one.Tally.heats != 0 && one.Tally.wears > 30000 && one.Tally.farthest > 60.0f && one.Tally.scouts > 0);
    TIDE_CHECK(one.Tally.sides.at[0] > 0 && one.Tally.sides.at[1] > 0);
    tide_world_free(&many);
    tide_world_free(&one);
}

TIDE_TEST(fields_on_threads_are_one_threads)
{
    const tide_jobs fake = {.threads = 8u, .run = second_first};
    rounds = 0;
    compare(&fake, false);
    TIDE_CHECK(rounds >= 12u); // Every tick was worth threads
    compare(&fake, true);
    TIDE_CHECK(tide_scratch_mark() == 0);
}

#ifdef TIDE_FIELDS_THREADS
TIDE_TEST(fields_on_the_pool_are_one_threads)
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
