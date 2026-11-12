// A frame's parallel loops take the pool's threads, with what one thread
// gives (parallel_views.tide).

#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/jobs.h"
#include "tide_test.h"
#ifdef TIDE_PARALLEL_VIEWS_THREADS
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

typedef struct run {
    tide_world world, before;
    tide_local local;
    tide_draw_list draw;
    tide_gui gui;
    tide_devices devices;
} run;

static void start(run *r)
{
    memset(r, 0, sizeof *r);
    tide_world_init(&r->world, 1.0f / 60.0f);
    tide_local_init(&r->local);
}

static void stop(run *r)
{
    tide_draw_free(&r->draw);
    tide_local_free(&r->local);
    tide_world_free(&r->before);
    tide_world_free(&r->world);
}

// A tick, then a frame blended halfway from the tick before, its loops
// handed the pool as a host hands it (tide/host.h)
static void step(run *r, const tide_jobs *tick, const tide_jobs *frame)
{
    tide_world_copy(&r->before, &r->world);
    if (tick) tide_world_tick_on(&r->world, tick);
    else tide_world_tick(&r->world);
    tide_draw_reset(&r->draw);
    tide_gui_begin(&r->gui, &r->devices, tide_f2(640.0f, 360.0f), NULL);
    const tide_jobs *const were = tide_loops_enter(frame);
    tide_frame(&r->world, &r->before, 0.5f, NULL, NULL, 1.0f, &r->local, &r->draw, &r->gui);
    tide_loops_leave(were);
    tide_gui_end(&r->gui, &r->draw);
}

static bool same(const run *a, const run *b)
{
    return tide_world_hash(&a->world) == tide_world_hash(&b->world)
        && memcmp(&a->local.Jobs.total, &b->local.Jobs.total, sizeof(float)) == 0
        && memcmp(&a->local.Jobs.fields, &b->local.Jobs.fields, sizeof(float)) == 0
        && memcmp(&a->local.Jobs.read, &b->local.Jobs.read, sizeof(float)) == 0;
}

TIDE_TEST(parallel_view_loops_call_the_pool_in)
{
    static run one, two;
    start(&one);
    start(&two);
    uint32_t rounds = 0;
    const tide_jobs fake = {&rounds, 2u, second_first, NULL, NULL, NULL, NULL, NULL, false};
    for (int i = 0; i < 4; i++) {
        step(&one, NULL, NULL);
        const uint32_t before = rounds;
        step(&two, NULL, &fake); // The tick on one thread: every round is a view's loop
        // Collect's list and Pose's rows went to the pool; Scratch's steps read
        // a list its view made, and stayed
        TIDE_CHECK(rounds == before + 2u);
        if (rounds != before + 2u) printf("    %u rounds\n", (unsigned)(rounds - before));
        TIDE_CHECK(same(&one, &two));
    }
    TIDE_CHECK(one.local.Jobs.total != 0.0f);
    TIDE_CHECK(one.local.Jobs.frames == 4 && two.local.Jobs.frames == 4);
    TIDE_CHECK(tide_scratch_mark() == 0);
    stop(&two);
    stop(&one);
}

#ifdef TIDE_PARALLEL_VIEWS_THREADS
TIDE_TEST(parallel_view_loops_on_the_pool_match_one_thread)
{
    const tide_jobs *pool = tide_platform_jobs();
    if (!pool || pool->threads < 2u) {
        printf("    one core: nothing to compare\n");
        return;
    }
    static run one, many;
    for (int attempt = 0; attempt < 5; attempt++) {
        start(&one);
        start(&many);
        for (int i = 0; i < 6; i++) {
            step(&one, NULL, NULL);
            step(&many, pool, pool);
            TIDE_CHECK(same(&one, &many));
        }
        stop(&many);
        stop(&one);
    }
}
#endif
