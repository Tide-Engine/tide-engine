// A flock's benchmark (flock.tide): ticks a second of birds that each steer by the birds near them and move,
// one component and one system. bench_flock reads the others as the tick started (`previous`); bench_flock_now
// is the same game without the word (flock_now.tide). Each prints its world's hash at the end, the same for the
// same options whatever the threads; the two games' differ, as one's birds see the others' moves of the tick.
//
//   bench_flock[_now] [--n <birds>] [--ticks <timed>] [--warm <untimed first>] [--threads <0: every core>]
//                     [--beside] [--expect <hash>] [--quick]
//
// --beside keeps a copy of the world as each tick starts, as a session does every tick: every page a tick writes
// is then shared as it starts.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flock.h"
#include "tide/platform.h"
#include "tide/time.h"

typedef struct limited_jobs {
    const tide_jobs *pool;
    uint32_t threads;
    void (*work)(void *context, uint32_t thread);
    void *context;
} limited_jobs;

static void limited_work(void *context, const uint32_t thread)
{
    limited_jobs *limited = context;
    if (thread < limited->threads) limited->work(limited->context, thread);
}

static void limited_run(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    limited_jobs *limited = self;
    limited->work = work;
    limited->context = context;
    limited->pool->run(limited->pool->self, limited_work, limited);
}

int main(const int argc, char **argv)
{
    int32_t n = 10000, ticks = 20, warm = 3;
    uint32_t threads = 0;
    bool beside = false;
    const char *expect = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const bool more = i + 1 < argc;
        if (strcmp(a, "--n") == 0 && more) n = atoi(argv[++i]);
        else if (strcmp(a, "--ticks") == 0 && more) ticks = atoi(argv[++i]);
        else if (strcmp(a, "--warm") == 0 && more) warm = atoi(argv[++i]);
        else if (strcmp(a, "--threads") == 0 && more) threads = (uint32_t)atoi(argv[++i]);
        else if (strcmp(a, "--expect") == 0 && more) expect = argv[++i];
        else if (strcmp(a, "--beside") == 0) beside = true;
        else if (strcmp(a, "--quick") == 0) {
            n = 300;
            ticks = 5;
            warm = 1;
        } else {
            printf("bench_flock [--n <birds>] [--ticks <timed>] [--warm <untimed>] [--threads <0: every core>] [--beside] "
                   "[--expect <hash>] [--quick]\n");
            return strcmp(a, "--help") == 0 ? 0 : 1;
        }
    }
    if (n < 0 || ticks < 1 || warm < 0) return 1;
    const tide_jobs *pool = tide_platform_jobs();
    limited_jobs limited = {.pool = pool, .threads = 1};
    if (pool) limited.threads = threads == 0 || threads > pool->threads ? pool->threads : threads;
    // The platform's pool times its ticks (tide_jobs.timed), which is how costly rows come to the threads
    tide_jobs jobs = {.self = &limited, .threads = limited.threads, .run = limited_run};
    if (pool) jobs.timed = pool->timed;
    const tide_jobs *use = pool && limited.threads > 1 ? &jobs : NULL;

    static tide_world world, was;
    tide_world_init(&world, 1.0f / 60.0f);
    world.Config.birds = n;
    for (int32_t t = 0; t < warm + 1; t++) tide_world_tick_on(&world, use); // The first spawns them
    const uint64_t began = tide_time_now_ns();
    for (int32_t t = 0; t < ticks; t++) {
        if (beside) tide_world_copy(&was, &world);
        tide_world_tick_on(&world, use);
    }
    const double seconds = (double)(tide_time_now_ns() - began) / 1e9;
    const uint64_t hash = tide_world_hash(&world);
    printf("%s n=%d threads=%u%s: %.3f ms a tick, %.1f ticks/s, hash %016" PRIx64 "\n", argv[0], (int)n, limited.threads,
           beside ? " beside a copy" : "", seconds * 1000.0 / ticks, ticks / seconds, hash);
    tide_world_free(&was);
    tide_world_free(&world);
    if (expect && strtoull(expect, NULL, 16) != hash) {
        printf("expected hash %s\n", expect);
        return 1;
    }
    return 0;
}
