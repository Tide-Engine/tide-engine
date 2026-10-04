#include "game.h"
#include "tide/jobs.h"
#include "tide_test.h"

#ifndef EXPECTED_ROUNDS
#define EXPECTED_ROUNDS 1u
#endif
#ifndef EXPECTED_TOTAL
#define EXPECTED_TOTAL 1999000
#endif

// A pool may run entirely on its caller when no worker joins. Count entering
// it, so the test checks the scheduling decision without depending on timing
// or on the machine having more than one core.
static void count_round(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    (*(uint32_t *)self)++;
    work(context, 0);
}

TIDE_TEST(parallel_unknown_inner_work_uses_the_pool)
{
    tide_world one = {0}, many = {0};
    tide_world_init(&one, 1.0f);
    one.Pairs.count = 2000;
    tide_world_tick(&one); // Set up before checking the cost of an ordinary tick.
    tide_world_copy(&many, &one);

    uint32_t rounds = 0;
    const tide_jobs jobs = {&rounds, 2u, count_round, NULL};
    tide_world_tick(&one);
    tide_world_tick_on(&many, &jobs);
    TIDE_CHECK(rounds == EXPECTED_ROUNDS);
    TIDE_CHECK(tide_world_hash(&one) == tide_world_hash(&many));
    const tide_int2 *last = tide_list_read(&many.heap, many.Pairs.values, 1999, sizeof(tide_int2));
    TIDE_REQUIRE(last);
    TIDE_CHECK(last->x == 1999 && last->y == EXPECTED_TOTAL);
    tide_world_free(&many);
    tide_world_free(&one);
}
