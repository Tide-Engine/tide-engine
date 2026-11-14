// Indexed loops' benchmark (loops.tide): ticks a second of boids that look at their neighbours through a
// query, agents that look for obstacles in a list and lookers that find chunks by their place, built with their
// loops' indexes (bench_loops) or without (bench_loops_plain). Both print the world's hash at the end, which is
// the same for the same options.
//
//   bench_loops[_plain] [--n <boids, agents and obstacles>] [--boids <n>] [--agents <n>] [--ticks <timed>] [--warm <untimed first>]
//                       [--chunks <n> --lookers <n> [--ways <1: by place, 2: by key, 3: both>]] [--threads <0: every core>]
//                       [--quick]

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loops.h"
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
    int32_t n = 10000, ticks = 20, warm = 3, boids = -1, agents = -1, chunks = 0, lookers = 0, ways = 3;
    uint32_t threads = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const bool more = i + 1 < argc;
        if (strcmp(a, "--n") == 0 && more) n = atoi(argv[++i]);
        else if (strcmp(a, "--boids") == 0 && more) boids = atoi(argv[++i]);
        else if (strcmp(a, "--agents") == 0 && more) agents = atoi(argv[++i]);
        else if (strcmp(a, "--chunks") == 0 && more) chunks = atoi(argv[++i]);
        else if (strcmp(a, "--lookers") == 0 && more) lookers = atoi(argv[++i]);
        else if (strcmp(a, "--ways") == 0 && more) ways = atoi(argv[++i]);
        else if (strcmp(a, "--ticks") == 0 && more) ticks = atoi(argv[++i]);
        else if (strcmp(a, "--warm") == 0 && more) warm = atoi(argv[++i]);
        else if (strcmp(a, "--threads") == 0 && more) threads = (uint32_t)atoi(argv[++i]);
        else if (strcmp(a, "--quick") == 0) {
            n = 300;
            ticks = 5;
            warm = 1;
            chunks = 400;
            lookers = 50;
        } else {
            printf("bench_loops [--n <count>] [--boids <n>] [--agents <n>] [--chunks <n>] [--lookers <n>] [--ways <1-3>] [--ticks <timed>] "
                   "[--warm <untimed>] [--threads <0: every core>] [--quick]\n");
            return strcmp(a, "--help") == 0 ? 0 : 1;
        }
    }
    if (n < 0 || ticks < 1 || warm < 0 || chunks < 0 || lookers < 0 || ways < 1 || ways > 3) return 1;
    const tide_jobs *pool = tide_platform_jobs();
    limited_jobs limited = {.pool = pool, .threads = 1};
    if (pool) limited.threads = threads == 0 || threads > pool->threads ? pool->threads : threads;
    const tide_jobs jobs = {.self = &limited, .threads = limited.threads, .run = limited_run};
    const tide_jobs *use = pool && limited.threads > 1 ? &jobs : NULL;

    static tide_world world;
    tide_world_init(&world, 1.0f / 60.0f);
    world.Config.boids = boids >= 0 ? boids : n;
    world.Config.agents = agents >= 0 ? agents : n;
    world.Config.obstacles = n;
    world.Config.chunks = chunks;
    world.Config.lookers = chunks > 0 ? lookers : 0;
    world.Config.ways = ways;
    for (int32_t t = 0; t < warm + 1; t++) tide_world_tick_on(&world, use); // The first spawns them
    const uint64_t began = tide_time_now_ns();
    for (int32_t t = 0; t < ticks; t++) tide_world_tick_on(&world, use);
    const double seconds = (double)(tide_time_now_ns() - began) / 1e9;
    printf("%s n=%d chunks=%d lookers=%d threads=%u: %.3f ms a tick, %.1f ticks/s, hash %016" PRIx64 "\n", argv[0], (int)n,
           (int)chunks, (int)world.Config.lookers, limited.threads, seconds * 1000.0 / ticks, ticks / seconds,
           tide_world_hash(&world));
    // Every lookup found its one chunk, both ways: a look each tick after the first, which spawns them
    const int64_t lookups = (int64_t)world.Config.lookers * (warm + ticks);
    const bool right = world.Lookups.found == (ways & 1 ? lookups : 0) && world.Lookups.keyed == (ways & 2 ? lookups : 0);
    if (!right) {
        printf("lookups found %" PRId64 " and %" PRId64 " chunks, not %" PRId64 "\n", world.Lookups.found, world.Lookups.keyed,
               lookups);
    }
    tide_world_free(&world);
    return right ? 0 : 1;
}
