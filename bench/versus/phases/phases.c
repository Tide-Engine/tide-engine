// Coarse phase diagnostics, separate from the normal versus measurements.
#include "versus.h"
#include "phases.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/page.h"
#include "tide/platform.h"
#include "tide/time.h"

enum { MAX_THREADS = 256 };
typedef struct phase_slot {
    _Alignas(64) uint64_t ns[PHASE_COUNT];
    uint64_t calls[PHASE_COUNT];
} phase_slot;
static phase_slot slots[MAX_THREADS];
static atomic_uint slot_count;
static TIDE_THREAD_LOCAL phase_slot *own_slot;

uint64_t tide_phase_stamp(void) { return tide_time_now_ns(); }

void tide_phase_add(const enum tide_phase phase, const uint64_t elapsed_ns)
{
    if (!own_slot) {
        const uint32_t slot = atomic_fetch_add(&slot_count, 1u);
        if (slot >= MAX_THREADS) abort();
        own_slot = &slots[slot];
    }
    own_slot->ns[phase] += elapsed_ns;
    own_slot->calls[phase]++;
}

static const char *const names[PHASE_COUNT] = {
    "loop_prepare", "window_gather", "output_copy", "cell_kernel", "window_writeback", "grid_commit",
    "task_other", "record_and_schedule", "replay_and_cleanup", "tick_other",
    "apply_commands", "queue_clear", "heap_cleanup", "parallel_region", "systems", "tick_cleanup"
};

typedef struct limited_jobs {
    tide_jobs jobs;
    const tide_jobs *pool;
} limited_jobs;
typedef struct limited_work {
    void (*work)(void *context, uint32_t thread);
    void *context;
    uint32_t threads;
} limited_work;
static void on_limited_thread(void *context, const uint32_t thread)
{
    const limited_work *r = context;
    if (thread < r->threads) r->work(r->context, thread);
}
static void run_limited(void *self, void (*work)(void *, uint32_t), void *context)
{
    const limited_jobs *l = self;
    limited_work r = {work, context, l->jobs.threads};
    l->pool->run(l->pool->self, on_limited_thread, &r);
}

static int compare_double(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median(double *values, const uint32_t count)
{
    qsort(values, count, sizeof *values, compare_double);
    return count % 2u ? values[count / 2u] : (values[count / 2u - 1u] + values[count / 2u]) / 2.0;
}

static bool read_number(const char *s, uint32_t *out)
{
    char *end;
    const unsigned long n = strtoul(s, &end, 10);
    if (!*s || *end || n > 100000000u) return false;
    *out = (uint32_t)n;
    return true;
}

int versus_phases_main(const versus_desc *desc, const int argc, char **argv)
{
    uint32_t warmup = desc->warmup, ticks = desc->ticks, threads = 1u;
    for (uint32_t f = 0; f < desc->flag_count; f++) desc->flags[f].value = desc->flags[f].full;
    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--quick") == 0) {
            warmup = desc->quick_warmup;
            ticks = desc->quick_ticks;
            for (uint32_t f = 0; f < desc->flag_count; f++) desc->flags[f].value = desc->flags[f].quick;
            continue;
        }
        uint32_t value;
        if (a + 1 >= argc || !read_number(argv[a + 1], &value)) return 2;
        const char *option = argv[a++];
        if (strcmp(option, "--threads") == 0) threads = value;
        else if (strcmp(option, "--ticks") == 0) ticks = value;
        else if (strcmp(option, "--warmup") == 0) warmup = value;
        else {
            bool found = false;
            for (uint32_t f = 0; f < desc->flag_count; f++) {
                if (option[0] == '-' && option[1] == '-' && strcmp(option + 2, desc->flags[f].name) == 0) {
                    desc->flags[f].value = value;
                    found = true;
                    break;
                }
            }
            if (!found) return 2;
        }
    }
    if (!warmup || !ticks || !threads) return 2;
    for (uint32_t f = 0; f < desc->flag_count; f++) if (!desc->flags[f].value) return 2;
    const tide_jobs *jobs = threads == 1u ? NULL : tide_platform_jobs();
    if (threads > 1u && (!jobs || jobs->threads < threads)) {
        fprintf(stderr, "--threads exceeds the pool's thread count (%u)\n", jobs ? jobs->threads : 1u);
        return 2;
    }
    limited_jobs limited = {{NULL, threads, run_limited, NULL}, jobs};
    if (jobs && jobs->threads > threads) {
        limited.jobs.self = &limited;
        jobs = &limited.jobs;
    }
    double *samples = tide_alloc_zeroed((size_t)ticks * (PHASE_COUNT + 1u), sizeof *samples);
    double total[PHASE_COUNT + 1u] = {0};
    uint64_t calls[PHASE_COUNT] = {0};
    uint64_t expected = 0;
    double c_median = 0;
    for (uint32_t way = 0; way < desc->way_count; way++) {
        const versus_way *w = &desc->ways[way];
        void *state = w->make(desc->flags);
        for (uint32_t tick = 0; tick < warmup; tick++) w->tick(state, jobs);
        for (uint32_t tick = 0; tick < ticks; tick++) {
            memset(slots, 0, sizeof slots); // No task is running between ticks.
            const uint64_t began = tide_time_now_ns();
            w->tick(state, jobs);
            const uint64_t elapsed = tide_time_now_ns() - began;
            samples[tick] = (double)elapsed / 1e6;
            if (way) continue;
            total[0] += samples[tick];
            for (uint32_t p = 0; p < PHASE_COUNT; p++) {
                uint64_t ns = 0, count = 0;
                const uint32_t n = atomic_load(&slot_count);
                for (uint32_t s = 0; s < n; s++) {
                    ns += slots[s].ns[p];
                    count += slots[s].calls[p];
                }
                if (p == PHASE_EVENTS_TICK_OTHER && total[PHASE_EVENTS_RECORD + 1u] > 0) {
                    const double record = samples[(PHASE_EVENTS_RECORD + 1u) * ticks + tick];
                    const double replay = samples[(PHASE_EVENTS_REPLAY + 1u) * ticks + tick];
                    ns = elapsed > (uint64_t)((record + replay) * 1e6) ? elapsed - (uint64_t)((record + replay) * 1e6) : 0;
                    count = 1;
                }
                const double ms = (double)ns / 1e6;
                samples[(p + 1u) * ticks + tick] = ms;
                total[p + 1u] += ms;
                calls[p] += count;
            }
        }
        const uint64_t digest = w->digest(state);
        w->free(state);
        if (!way) expected = digest;
        else if (digest != expected) {
            fprintf(stderr, "%s digest differs: %016llx vs %016llx\n", w->name,
                    (unsigned long long)digest, (unsigned long long)expected);
            free(samples);
            return 1;
        }
        if (way) c_median = median(samples, ticks);
        else {
            // Sorting only the tick column leaves each phase's independent samples intact.
            c_median = median(samples, ticks);
            printf("{\"benchmark\":\"%s\",\"instrumented\":%s,\"threads\":%u,\"warmup\":%u,\"ticks\":%u,\"options\":{",
                   desc->name, TIDE_PHASE_INSTRUMENTED ? "true" : "false", threads, warmup, ticks);
            for (uint32_t f = 0; f < desc->flag_count; f++) printf("%s\"%s\":%u", f ? "," : "", desc->flags[f].name, desc->flags[f].value);
            printf("},\"digest\":\"0x%016llx\",\"tick\":{\"median_ms\":%.9f,\"mean_ms\":%.9f},\"phases\":{",
                   (unsigned long long)digest, c_median, total[0] / ticks);
            bool comma = false;
            for (uint32_t p = 0; p < PHASE_COUNT; p++) {
                if (!calls[p]) continue;
                printf("%s\"%s\":{\"median_ms\":%.9f,\"mean_ms\":%.9f,\"calls_per_tick\":%.3f}",
                       comma ? "," : "", names[p], median(samples + (p + 1u) * ticks, ticks),
                       total[p + 1u] / ticks, (double)calls[p] / ticks);
                comma = true;
            }
            printf("}");
        }
    }
    printf(",\"c_median_ms\":%.9f,\"correct\":true}\n", c_median);
    free(samples);
    return 0;
}
