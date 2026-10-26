// C against Tide's harness (see versus.h).

#include "versus.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tide/page.h"
#include "tide/platform.h"
#include "tide/time.h"

// Saved results go here, every concept's in one file.
#ifndef TIDE_BENCH_RESULTS
#define TIDE_BENCH_RESULTS "results"
#endif

enum { MAX_WAYS = 8, MAX_RUNS = 16, MAX_PARTS = 8 };

// The platform's pool, with at most `threads` of its threads at work at once,
// the caller's among them. Its workers still sleep when they have nothing to
// do and are asked back as the pool's are (tide_jobs.more), and a round that
// wants a few of them wakes a few (run_some), so a tick on fewer threads goes
// as it would on a pool of that many.
typedef struct limited {
    tide_jobs jobs;
    const tide_jobs *pool;
    atomic_uint inside; // The pool's workers in the round's work, beside the caller
} limited;

typedef struct limited_round {
    limited *l;
    void (*work)(void *context, uint32_t thread);
    void *context;
} limited_round;

// A worker takes part while fewer than `threads` are at it, the caller's
// included; past that it goes back to the pool at once. Tide's work only
// tells the caller (thread 0) from the rest, so each keeps the pool's number.
static void limited_work(void *context, const uint32_t thread)
{
    const limited_round *r = context;
    if (thread == 0) {
        r->work(r->context, 0);
        return;
    }
    if (atomic_fetch_add(&r->l->inside, 1u) + 1u < r->l->jobs.threads) r->work(r->context, thread);
    atomic_fetch_sub(&r->l->inside, 1u);
}

static void limited_run_some(void *self, const uint32_t others, void (*work)(void *context, uint32_t thread), void *context)
{
    limited *l = self;
    limited_round r = {l, work, context};
    const uint32_t most = l->jobs.threads - 1u;
    if (l->pool->run_some) l->pool->run_some(l->pool->self, others < most ? others : most, limited_work, &r);
    else l->pool->run(l->pool->self, limited_work, &r);
}

static void limited_run(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    limited_run_some(self, UINT32_MAX, work, context);
}

static void limited_more(void *self, const uint32_t count)
{
    const limited *l = self;
    const uint32_t inside = atomic_load(&l->inside);
    const uint32_t room = inside + 1u < l->jobs.threads ? l->jobs.threads - 1u - inside : 0u;
    if (room) l->pool->more(l->pool->self, count < room ? count : room);
}

// Threads to tick on: NULL for this one alone, 0 for every core. `l` holds a
// pool of fewer threads than the platform's, for as long as it's used.
static const tide_jobs *jobs_for(const uint32_t threads, limited *l)
{
    const tide_jobs *pool = tide_platform_jobs();
    if (!pool || threads == 1) return NULL;
    if (threads == 0 || threads >= pool->threads) return pool;
    l->jobs = (tide_jobs){l, threads, limited_run, NULL, pool->more ? limited_more : NULL, limited_run_some, NULL, NULL};
    l->pool = pool;
    atomic_init(&l->inside, 0u);
    return &l->jobs;
}

static uint32_t thread_count(const tide_jobs *jobs)
{
    return jobs ? jobs->threads : 1u;
}

typedef struct pieces_round {
    void (*work)(void *context, uint32_t piece);
    void *context;
    uint32_t count;
    atomic_uint next;
} pieces_round;

// On each thread that joins: pieces until there are none left. Threads take
// them as they come, as a pool can start with fewer threads than it has.
static void pieces_work(void *context, const uint32_t thread)
{
    (void)thread;
    pieces_round *r = context;
    tide_memory_sync(); // What the thread that started it made (see tide/page.h)
    for (;;) {
        const uint32_t piece = atomic_fetch_add(&r->next, 1u);
        if (piece >= r->count) return;
        r->work(r->context, piece);
    }
}

void versus_for(const tide_jobs *jobs, const uint32_t count, void (*work)(void *context, uint32_t piece), void *context)
{
    if (!jobs || count <= 1u) {
        for (uint32_t piece = 0; piece < count; piece++) work(context, piece);
        return;
    }
    pieces_round r = {work, context, count, 0u};
    jobs->run(jobs->self, pieces_work, &r);
    tide_memory_sync(); // What the other threads made
}

// Split work however little there is (--split, --quick).
static bool always_split;

uint32_t versus_pieces(const tide_jobs *jobs, const uint32_t count, const uint32_t enough)
{
    if (!jobs || (count < enough && !always_split)) return 1u;
    const uint32_t pieces = jobs->threads * 8u;
    return count < pieces ? (count ? count : 1u) : pieces;
}

uint64_t versus_item(const void *data, const size_t size)
{
    // FNV-1a over the bytes, then SplitMix64's finish, so that sums of items
    // that differ a little differ a lot
    uint64_t h = 0xcbf29ce484222325ull;
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) h = (h ^ bytes[i]) * 0x100000001b3ull;
    h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ull;
    h = (h ^ (h >> 27)) * 0x94d049bb133111ebull;
    return h ^ (h >> 31);
}

// A way's run on some number of threads.
typedef struct timing {
    double median_ms; // Of a tick
    double mean_ms;
    uint64_t digest;
    bool parts; // It timed its parts: their medians and means
    double part_median_ms[MAX_PARTS];
    double part_mean_ms[MAX_PARTS];
} timing;

typedef struct options {
    uint32_t warmup, ticks;
    uint32_t threads; // Only this many (0 for every core), when `one_count`
    bool one_count;
    uint32_t list[MAX_RUNS]; // --threads-list: each of these in turn
    uint32_t list_count;
} options;

static int compare_doubles(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

// Sorts `ms` and gives its median, and its mean.
static void summarize(double *ms, const uint32_t count, double *median, double *mean)
{
    double total = 0.0;
    for (uint32_t i = 0; i < count; i++) total += ms[i];
    qsort(ms, count, sizeof *ms, compare_doubles);
    *median = count % 2u ? ms[count / 2u] : (ms[count / 2u - 1u] + ms[count / 2u]) / 2.0;
    *mean = total / (double)count;
}

// `ms` has room for each timed tick, and `part_ms` for each tick's parts.
static timing run_way(const versus_way *way, const versus_parts parts, const uint32_t part_count, const versus_flag *flags,
                      const options *o, const tide_jobs *jobs, double *ms, double *part_ms)
{
    void *state = way->make(flags);
    for (uint32_t i = 0; i < o->warmup; i++) way->tick(state, jobs);
    double these[MAX_PARTS];
    for (uint32_t i = 0; i < o->ticks; i++) {
        const uint64_t start = tide_time_now_ns();
        way->tick(state, jobs);
        ms[i] = (double)(tide_time_now_ns() - start) / 1e6;
        if (!parts) continue;
        parts(state, these);
        for (uint32_t p = 0; p < part_count; p++) part_ms[(size_t)p * o->ticks + i] = these[p];
    }
    timing t = {.digest = way->digest(state), .parts = parts != NULL};
    way->free(state);
    summarize(ms, o->ticks, &t.median_ms, &t.mean_ms);
    for (uint32_t p = 0; parts && p < part_count; p++) {
        summarize(part_ms + (size_t)p * o->ticks, o->ticks, &t.part_median_ms[p], &t.part_mean_ms[p]);
    }
    return t;
}

static void json_text(FILE *f, const char *text)
{
    fputc('"', f);
    for (const unsigned char *c = (const unsigned char *)text; *c; c++) {
        if (*c == '"' || *c == '\\') fprintf(f, "\\%c", *c);
        else if (*c < 0x20) fprintf(f, "\\u%04x", *c);
        else fputc(*c, f);
    }
    fputc('"', f);
}

// One line of JSON: what ran, where, and how long each way's ticks took.
static void write_record(FILE *f, const versus_desc *d, const options *o, const char *label, const uint32_t runs,
                         const uint32_t *threads, const timing (*t)[MAX_WAYS], const bool ok)
{
    char date[32] = "";
    const time_t now = time(NULL);
    const struct tm *utc = gmtime(&now);
    if (utc) strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%SZ", utc);
#if defined(__wasm__)
    const char *os = "web";
#elif defined(__ANDROID__)
    const char *os = "android";
#elif defined(_WIN32)
    const char *os = "windows";
#elif defined(__APPLE__)
    const char *os = "macos";
#else
    const char *os = "linux";
#endif
#if defined(__wasm__)
    const char *arch = "wasm32";
#elif defined(__aarch64__) || defined(_M_ARM64)
    const char *arch = "arm64";
#elif defined(__arm__) || defined(_M_ARM)
    const char *arch = "arm";
#elif defined(__x86_64__) || defined(_M_X64)
    const char *arch = "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    const char *arch = "x86";
#else
    const char *arch = "unknown";
#endif
#if defined(__OPTIMIZE__)
    const bool optimized = true;
#else
    const bool optimized = false;
#endif
    fprintf(f, "{\"benchmark\":");
    json_text(f, d->name);
    fprintf(f, ",\"label\":");
    json_text(f, label);
    fprintf(f, ",\"date\":\"%s\",\"os\":\"%s\",\"arch\":\"%s\",\"optimized\":%s,\"compiler\":", date, os, arch, optimized ? "true" : "false");
    json_text(f, __clang_version__);
    const char *machine = getenv("TIDE_BENCH_MACHINE"), *revision = getenv("TIDE_BENCH_REVISION");
    fprintf(f, ",\"machine\":");
    json_text(f, machine ? machine : "unknown");
    fprintf(f, ",\"revision\":");
    json_text(f, revision ? revision : "unknown");
    fprintf(f, ",\"options\":{\"warmup\":%u,\"ticks\":%u,\"split\":%s", o->warmup, o->ticks, always_split ? "true" : "false");
    for (uint32_t i = 0; i < d->flag_count; i++) fprintf(f, ",\"%s\":%u", d->flags[i].name, d->flags[i].value);
    fprintf(f, "},\"ok\":%s,\"digest\":\"0x%016llx\",\"runs\":[", ok ? "true" : "false",
            (unsigned long long)t[0][0].digest);
    for (uint32_t r = 0; r < runs; r++) {
        fprintf(f, "%s{\"threads\":%u,\"ways\":{", r ? "," : "", threads[r]);
        for (uint32_t w = 0; w < d->way_count; w++) {
            fprintf(f, "%s", w ? "," : "");
            json_text(f, d->ways[w].name);
            fprintf(f, ":{\"median_ms\":%.6g,\"mean_ms\":%.6g", t[r][w].median_ms, t[r][w].mean_ms);
            if (t[r][w].parts) { // Each part's, for a way that timed them
                fprintf(f, ",\"parts\":{");
                for (uint32_t p = 0; p < d->part_count; p++) {
                    fprintf(f, "%s", p ? "," : "");
                    json_text(f, d->part_names[p]);
                    fprintf(f, ":{\"median_ms\":%.6g,\"mean_ms\":%.6g}", t[r][w].part_median_ms[p], t[r][w].part_mean_ms[p]);
                }
                fprintf(f, "}");
            }
            fprintf(f, "}");
        }
        fprintf(f, "}}");
    }
    fprintf(f, "]}\n");
}

// Adds the run to TIDE_BENCH_RESULTS/versus.jsonl, or prints its record where
// that can't be written (as on the web, which only sees its own folder).
static void save_results(const versus_desc *d, const options *o, const char *label, const uint32_t runs,
                         const uint32_t *threads, const timing (*t)[MAX_WAYS], const bool ok)
{
    const char *path = TIDE_BENCH_RESULTS "/versus.jsonl";
    FILE *f = fopen(path, "ab");
    if (!f) {
        printf("\n  couldn't add to %s; here's the record instead:\n", path);
        write_record(stdout, d, o, label, runs, threads, t, ok);
        return;
    }
    write_record(f, d, o, label, runs, threads, t, ok);
    fclose(f);
    printf("\n  saved as '%s' in %s\n", label, path);
}

static void usage(const versus_desc *d)
{
    printf("usage: %s [--quick] [options]\n\n", d->name);
    printf("  --quick             a short, small run, as tests make\n");
    printf("  --ticks <n>         ticks to time (%u)\n", d->ticks);
    printf("  --warmup <n>        ticks to run first, untimed, at least 1 (%u)\n", d->warmup);
    printf("  --threads <n>       only this many threads, 0 for every core (one, then every core)\n");
    printf("  --threads-list <l>  each count of threads in a list like 1,2,4,8,12 in turn (0: every core)\n");
    printf("  --split             C splits its work across threads however little there is\n");
    printf("  --save <label>      adds the results to bench/results/versus.jsonl\n");
    for (uint32_t i = 0; i < d->flag_count; i++) {
        printf("  --%-17s %s (%u)\n", d->flags[i].name, d->flags[i].help, d->flags[i].full);
    }
}

// The options from the command line, over a full run's or --quick's.
static bool read_number(const char *text, uint32_t *out)
{
    if (*text < '0' || *text > '9') return false;
    for (const char *c = text; *c; c++) if (*c < '0' || *c > '9') return false;
    errno = 0;
    char *end;
    const unsigned long long number = strtoull(text, &end, 10);
    if (errno == ERANGE || *end || number > UINT32_MAX) return false;
    *out = (uint32_t)number;
    return true;
}

// Counts of threads, with commas between them: 1,2,4,8,12.
static bool read_list(const char *text, options *o)
{
    o->list_count = 0;
    for (const char *at = text;;) {
        char number[16];
        const size_t length = strcspn(at, ",");
        if (length == 0 || length >= sizeof number || o->list_count == MAX_RUNS) return false;
        memcpy(number, at, length);
        number[length] = 0;
        if (!read_number(number, &o->list[o->list_count++])) return false;
        if (!at[length]) return true;
        at += length + 1u;
    }
}

static bool parse(const versus_desc *d, const int argc, char **argv, options *o, const char **save)
{
    bool quick = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quick") == 0) quick = true;
    }
    always_split = quick;
    *o = (options){.warmup = quick ? d->quick_warmup : d->warmup, .ticks = quick ? d->quick_ticks : d->ticks};
    for (uint32_t i = 0; i < d->flag_count; i++) d->flags[i].value = quick ? d->flags[i].quick : d->flags[i].full;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--quick") == 0) continue;
        if (strcmp(arg, "--split") == 0) {
            always_split = true;
            continue;
        }
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) return false;
        if (i + 1 >= argc || strncmp(arg, "--", 2) != 0) {
            fprintf(stderr, "%s: '%s' isn't an option, or it needs a value\n", d->name, arg);
            return false;
        }
        const char *value = argv[++i];
        uint32_t number = 0;
        const bool numeric = read_number(value, &number);
        bool known = true;
        if (strcmp(arg, "--save") == 0) {
            *save = value;
        } else if (strcmp(arg, "--threads-list") == 0) {
            known = read_list(value, o);
        } else if (!numeric) {
            known = false;
        } else if (strcmp(arg, "--ticks") == 0) {
            o->ticks = number;
        } else if (strcmp(arg, "--warmup") == 0) {
            o->warmup = number;
        } else if (strcmp(arg, "--threads") == 0) {
            o->threads = number;
            o->one_count = true;
        } else {
            known = false;
            for (uint32_t k = 0; k < d->flag_count; k++) {
                if (strcmp(arg + 2, d->flags[k].name) == 0) {
                    known = number <= INT32_MAX && number >= d->flags[k].minimum;
                    if (known) d->flags[k].value = number;
                }
            }
        }
        if (!known) {
            fprintf(stderr, "%s: '%s %s' isn't a valid option and value\n", d->name, arg, value);
            return false;
        }
    }
    if (o->warmup < 1u) {
        fprintf(stderr, "%s: --warmup is at least 1, the tick that sets every way up\n", d->name);
        return false;
    }
    if (o->ticks < 1u || (size_t)o->ticks > SIZE_MAX / sizeof(double)) {
        fprintf(stderr, "%s: --ticks needs a positive count that fits in memory\n", d->name);
        return false;
    }
    return true;
}

// A cell of the tables: milliseconds, to the microsecond, or to a hundredth
// of one where they're under a tenth of a millisecond, and what's beside them.
static void print_ms(const double ms, const char *beside)
{
    printf(ms < 0.1 ? "  %9.5f %-10s" : "  %9.3f %-10s", ms, beside);
}

int versus_main(const versus_desc *d, const int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(d);
            return 0;
        }
    }
    options o;
    const char *save = NULL; // The label to save the results under
    if (!parse(d, argc, argv, &o, &save) || d->way_count < 1u || d->way_count > MAX_WAYS) {
        usage(d);
        return 2;
    }

    if (d->part_count > MAX_PARTS || (d->part_count && (!d->parts || !d->part_names))) {
        fprintf(stderr, "%s: a concept has at most %u parts, with their names and a function for each way\n", d->name,
                (unsigned)MAX_PARTS);
        return 2;
    }

    // One thread, then every core; or the threads asked for
    static limited limits[MAX_RUNS]; // The pools of fewer threads, one for each run that has one
    const tide_jobs *jobs[MAX_RUNS];
    uint32_t threads[MAX_RUNS];
    uint32_t runs = 0;
    if (o.list_count) {
        for (; runs < o.list_count; runs++) jobs[runs] = jobs_for(o.list[runs], &limits[runs]);
    } else if (o.one_count) {
        jobs[runs] = jobs_for(o.threads, &limits[runs]);
        runs++;
    } else {
        jobs[runs++] = NULL;
        const tide_jobs *every = jobs_for(0, &limits[runs]);
        if (thread_count(every) > 1u) jobs[runs++] = every;
    }
    for (uint32_t r = 0; r < runs; r++) threads[r] = thread_count(jobs[r]);

#if defined(__OPTIMIZE__)
    const char *build = "optimized";
#else
    const char *build = "unoptimized (the times mean little)";
#endif
#if defined(__wasm__)
    const char *platform = "web";
#else
    const char *platform = "native";
#endif
    printf("%s: %s\n  ", d->name, d->about);
    for (uint32_t i = 0; i < d->flag_count; i++) printf("%s %u; ", d->flags[i].name, d->flags[i].value);
    printf("%u ticks timed after %u; %s %s build\n", o.ticks, o.warmup, build, platform);
    fflush(stdout);

    double *ms = malloc((size_t)o.ticks * sizeof *ms);
    double *part_ms = d->part_count ? malloc((size_t)o.ticks * d->part_count * sizeof *part_ms) : NULL;
    if (!ms || (d->part_count && !part_ms)) {
        fprintf(stderr, "%s: out of memory\n", d->name);
        return 1;
    }
    timing t[MAX_RUNS][MAX_WAYS];
    bool ok = true;
    bool parts = false; // Some way timed its parts
    for (uint32_t r = 0; r < runs; r++) {
        for (uint32_t w = 0; w < d->way_count; w++) {
            const versus_parts way_parts = d->part_count ? d->parts[w] : NULL;
            t[r][w] = run_way(&d->ways[w], way_parts, d->part_count, d->flags, &o, jobs[r], ms, part_ms);
            ok &= t[r][w].digest == t[0][0].digest;
            parts |= t[r][w].parts;
        }
    }
    free(ms);
    free(part_ms);

    // Ways down, threads across: each one's milliseconds a tick, and beside
    // the other ways', how many times as long Tide's took
    printf("\n  %-14s", "ms a tick");
    for (uint32_t r = 0; r < runs; r++) {
        char head[32];
        snprintf(head, sizeof head, "%u thread%s", threads[r], threads[r] == 1u ? "" : "s");
        printf("  %-20s", head);
    }
    printf("\n");
    for (uint32_t w = 0; w < d->way_count; w++) {
        printf("  %-14s", d->ways[w].name);
        for (uint32_t r = 0; r < runs; r++) {
            char cell[32] = "";
            if (w) snprintf(cell, sizeof cell, "%.2fx", t[r][0].median_ms / t[r][w].median_ms);
            print_ms(t[r][w].median_ms, cell);
        }
        printf("\n");
    }
    // Then each part of each way that timed them, beside Tide's the same way
    if (parts) {
        printf("\n  %-22s", "ms a tick, by part");
        for (uint32_t r = 0; r < runs; r++) {
            char head[32];
            snprintf(head, sizeof head, "%u thread%s", threads[r], threads[r] == 1u ? "" : "s");
            printf("  %-20s", head);
        }
        printf("\n");
    }
    for (uint32_t w = 0; parts && w < d->way_count; w++) {
        if (!t[0][w].parts) continue;
        for (uint32_t p = 0; p < d->part_count; p++) {
            char name[64];
            snprintf(name, sizeof name, "%s: %s", d->ways[w].name, d->part_names[p]);
            printf("  %-22s", name);
            for (uint32_t r = 0; r < runs; r++) {
                char cell[32] = "";
                if (w && t[r][0].parts && t[r][w].part_median_ms[p] > 0.0) {
                    snprintf(cell, sizeof cell, "%.2fx", t[r][0].part_median_ms[p] / t[r][w].part_median_ms[p]);
                }
                print_ms(t[r][w].part_median_ms[p], cell);
            }
            printf("\n");
        }
    }
    printf("\n  medians; beside C, how many times as long Tide takes\n");

    if (ok) {
        printf("  every way ended the same: 0x%016llx\n", (unsigned long long)t[0][0].digest);
    } else {
        printf("  FAILED: the ways ended differently:\n");
        for (uint32_t r = 0; r < runs; r++) {
            for (uint32_t w = 0; w < d->way_count; w++) {
                printf("    %-14s on %u thread%s: 0x%016llx\n", d->ways[w].name, threads[r], threads[r] == 1u ? "" : "s",
                       (unsigned long long)t[r][w].digest);
            }
        }
    }
    if (save) save_results(d, &o, save, runs, threads, (const timing(*)[MAX_WAYS])t, ok);
    return ok ? 0 : 1;
}
