#include "game.h"
#include "tide_test.h"
#include "tide/time.h"
#ifdef TIDE_WORKER_THREADS
#include "tide/platform.h"
#endif

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

TIDE_TEST(workers_keep_nested_values_arrays_optionals_and_errors)
{
    for (int pass = 0; pass < 2; pass++) {
        tide_world_init(&world, 1.0f);
        tide_local_init(&local);
        gui.jobs = NULL;
#ifdef TIDE_WORKER_THREADS
        if (pass) gui.jobs = tide_platform_jobs();
#else
        (void)pass;
#endif
        const uint64_t until = tide_time_now_ns() + 10000000000ull;
        while (!local.Results.done && tide_time_now_ns() < until) {
            tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
            tide_yield();
        }
        TIDE_CHECK(local.Results.done);
        TIDE_CHECK(local.Results.good);
        tide_local_free(&local);
        tide_world_free(&world);
        tide_draw_free(&draw);
    }
}
