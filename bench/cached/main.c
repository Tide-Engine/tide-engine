// What a `cached` function costs (cached.tide; notes/pure-functions.md): for each kind of function, the
// nanoseconds a call takes from each of `--n` probes a tick, less what a tick of the probes costs with no call.
// bench_cached remembers; bench_cached_plain is the same game with `cached` meaning nothing, so its numbers
// are the functions' own. `--spread` is how many arguments the probes share: 16 finds nearly every result
// kept, and one as big as `--n` never does (every call runs the code and keeps a result nobody asks for).
//
//   bench_cached[_plain] [--n <probes>] [--ticks <timed>] [--spread <arguments>] [--threads <1; 0: every core>] [--quick]

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cached.h"
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

static const char *const modes[] = {
    "no call", "Tiny(float)", "Curve: 16 sines", "Curve: 256 sines", "Size(Box)", "Bounds(Catalog, int): 64 corners",
    "BigBounds(Big, int): a 6,000-part heap", "Letters(string)", "Outline: a list of 64 out",
};
enum { MODES = sizeof modes / sizeof modes[0] };

int main(const int argc, char **argv)
{
    int32_t n = 20000, ticks = 40, spread = 16;
    uint32_t threads = 1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const bool more = i + 1 < argc;
        if (strcmp(a, "--n") == 0 && more) n = atoi(argv[++i]);
        else if (strcmp(a, "--ticks") == 0 && more) ticks = atoi(argv[++i]);
        else if (strcmp(a, "--spread") == 0 && more) spread = atoi(argv[++i]);
        else if (strcmp(a, "--threads") == 0 && more) threads = (uint32_t)atoi(argv[++i]);
        else if (strcmp(a, "--quick") == 0) {
            n = 400;
            ticks = 3;
        } else {
            printf("bench_cached [--n <probes>] [--ticks <timed>] [--spread <arguments>] [--threads <1; 0: every core>] [--quick]\n");
            return strcmp(a, "--help") == 0 ? 0 : 1;
        }
    }
    if (n < 1 || ticks < 1 || spread < 1) return 1;
    const tide_jobs *pool = tide_platform_jobs();
    if (threads == 0 || (pool && threads > pool->threads)) threads = pool ? pool->threads : 1;
    limited_jobs limited = {.pool = pool, .threads = threads};
    const tide_jobs limited_to = {.self = &limited, .threads = threads, .run = limited_run};
    const tide_jobs *jobs = pool && threads > 1 ? &limited_to : NULL;

    static tide_world world;
    tide_world_init(&world, 1.0f / 60.0f);
    world.Bench.spread = spread;
    world.Bench.probes = n;
    tide_world_tick_on(&world, jobs); // Fills the catalogs
    tide_world_tick_on(&world, jobs);
    // A match's world is hashed at each tick's end, which is what gives its lists their versions: once here, as
    // nothing changes the catalogs after
    (void)tide_world_hash(&world);
#ifdef TIDE_HAS_CACHED
    printf("cached, ");
#else
    printf("plain, ");
#endif
    printf("%d probes, %d arguments between them, %u thread%s\n", n, spread, threads, threads == 1 ? "" : "s");
    double none = 0;
    for (int mode = 0; mode < MODES; mode++) {
        world.Bench.mode = mode;
        for (int t = 0; t < 2; t++) tide_world_tick_on(&world, jobs); // The first calls, which run the code
        uint64_t best = UINT64_MAX;
        for (int t = 0; t < ticks; t++) {
            const uint64_t began = tide_time_now_ns();
            tide_world_tick_on(&world, jobs);
            const uint64_t took = tide_time_now_ns() - began;
            if (took < best) best = took;
        }
        const double each = (double)best / (double)n;
        if (mode == 0) none = each;
        printf("  %-40s %9.1f ns a call\n", modes[mode], mode == 0 ? each : each - none);
    }
#ifdef TIDE_HAS_CACHED
    uint64_t hits, misses, dropped;
    tide_cached_counts(&hits, &misses, &dropped);
    printf("  found %" PRIu64 ", ran %" PRIu64 ", let go of %" PRIu64 "\n", hits, misses, dropped);
#endif
    printf("hash %016" PRIx64 "\n", tide_world_hash(&world));
    tide_world_free(&world);
    return 0;
}
