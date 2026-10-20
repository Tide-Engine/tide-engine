#pragma once

// The groups of a Linux machine's CPUs that share its last level of cache,
// read from sysfs (normally /sys/devices/system/cpu), for the desktop pool
// (jobs.c): a chiplet of a Ryzen, say, six of its twelve cores. Only plain C
// file reading, so a test can read a made-up tree on any system
// (platform/tests/cpu_groups.c).
//
// What's read: `online` (the CPUs there are), each one's
// cache/index*/{level,type,shared_cpu_list} (the unified caches; the highest
// level's are the groups), and its topology/thread_siblings_list (a core's
// first CPU counts it, so a core that runs two threads counts once). Groups
// count only where there are several, each of two cores or more, and between
// them every CPU: anything missing, odd or more than TIDE_CPU_GROUPS gives
// none, and the pool keeps one group.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TIDE_CPU_GROUPS 8u
#define TIDE_CPU_MOST 1024u // CPUs read, as a cpu_set_t has room for
#define TIDE_CPU_WORDS (TIDE_CPU_MOST / 64u)

typedef struct tide_cpu_set {
    uint64_t bits[TIDE_CPU_WORDS];
} tide_cpu_set;

typedef struct tide_cpu_groups {
    uint32_t count;                         // 0 when there are none to use
    uint32_t physical;                      // Cores in all, with count
    uint32_t cores[TIDE_CPU_GROUPS];        // Each group's
    tide_cpu_set cpus[TIDE_CPU_GROUPS];     // ...and its CPUs
} tide_cpu_groups;

static inline bool tide_cpu_has(const tide_cpu_set *s, const uint32_t cpu)
{
    return cpu < TIDE_CPU_MOST && (s->bits[cpu / 64u] >> (cpu % 64u) & 1u);
}

// A file's first line, without its line end; false if it can't be read.
static bool tide_cpu_read(const char *path, char *out, const size_t room)
{
    FILE *f = fopen(path, "r");
    if (!f) return false;
    const bool got = fgets(out, (int)room, f) != NULL;
    fclose(f);
    if (!got) return false;
    out[strcspn(out, "\r\n")] = '\0';
    return true;
}

// A list like "0-5,12-17" into `s`; false for anything else, or a CPU past TIDE_CPU_MOST.
static bool tide_cpu_list(const char *text, tide_cpu_set *s)
{
    memset(s, 0, sizeof *s);
    const char *at = text;
    bool any = false;
    while (*at) {
        uint32_t from = 0, to;
        if (*at < '0' || *at > '9') return false;
        while (*at >= '0' && *at <= '9') {
            from = from * 10u + (uint32_t)(*at++ - '0');
            if (from >= TIDE_CPU_MOST) return false;
        }
        to = from;
        if (*at == '-') {
            at++;
            to = 0;
            if (*at < '0' || *at > '9') return false;
            while (*at >= '0' && *at <= '9') {
                to = to * 10u + (uint32_t)(*at++ - '0');
                if (to >= TIDE_CPU_MOST) return false;
            }
            if (to < from) return false;
        }
        for (uint32_t c = from; c <= to; c++) s->bits[c / 64u] |= 1ull << (c % 64u);
        any = true;
        if (*at == ',') at++;
        else if (*at) return false;
    }
    return any;
}

static bool tide_cpu_same(const tide_cpu_set *a, const tide_cpu_set *b)
{
    return memcmp(a->bits, b->bits, sizeof a->bits) == 0;
}

// The shared_cpu_list of `cpu`'s unified cache at `level`, into `s`.
static bool tide_cpu_cache(const char *root, const uint32_t cpu, const int level, tide_cpu_set *s)
{
    char path[512], text[1024];
    for (int index = 0; index < 16; index++) {
        snprintf(path, sizeof path, "%s/cpu%u/cache/index%d/level", root, (unsigned)cpu, index);
        if (!tide_cpu_read(path, text, sizeof text)) return false; // Past its last cache
        if (atoi(text) != level) continue;
        snprintf(path, sizeof path, "%s/cpu%u/cache/index%d/type", root, (unsigned)cpu, index);
        if (!tide_cpu_read(path, text, sizeof text) || strcmp(text, "Unified") != 0) continue;
        snprintf(path, sizeof path, "%s/cpu%u/cache/index%d/shared_cpu_list", root, (unsigned)cpu, index);
        return tide_cpu_read(path, text, sizeof text) && tide_cpu_list(text, s);
    }
    return false;
}

static void tide_cpu_groups_find(const char *root, tide_cpu_groups *out)
{
    memset(out, 0, sizeof *out);
    char path[512], text[1024];
    tide_cpu_set online;
    snprintf(path, sizeof path, "%s/online", root);
    if (!tide_cpu_read(path, text, sizeof text) || !tide_cpu_list(text, &online)) return;
    uint32_t first = 0;
    while (first < TIDE_CPU_MOST && !tide_cpu_has(&online, first)) first++;
    // The highest level of unified cache, as the first CPU has them
    int level = 0;
    for (int index = 0; index < 16; index++) {
        snprintf(path, sizeof path, "%s/cpu%u/cache/index%d/level", root, (unsigned)first, index);
        if (!tide_cpu_read(path, text, sizeof text)) break;
        const int l = atoi(text);
        snprintf(path, sizeof path, "%s/cpu%u/cache/index%d/type", root, (unsigned)first, index);
        if (l > level && tide_cpu_read(path, text, sizeof text) && strcmp(text, "Unified") == 0) level = l;
    }
    if (!level) return;
    tide_cpu_groups g;
    memset(&g, 0, sizeof g);
    for (uint32_t cpu = first; cpu < TIDE_CPU_MOST; cpu++) {
        if (!tide_cpu_has(&online, cpu)) continue;
        tide_cpu_set shares;
        if (!tide_cpu_cache(root, cpu, level, &shares) || !tide_cpu_has(&shares, cpu)) return;
        uint32_t group = 0;
        while (group < g.count && !tide_cpu_same(&g.cpus[group], &shares)) group++;
        if (group == g.count) {
            if (g.count == TIDE_CPU_GROUPS) return; // More than the pool keeps apart
            g.cpus[g.count++] = shares;
        }
        // A core counts at its first CPU
        tide_cpu_set siblings;
        snprintf(path, sizeof path, "%s/cpu%u/topology/thread_siblings_list", root, (unsigned)cpu);
        if (!tide_cpu_read(path, text, sizeof text) || !tide_cpu_list(text, &siblings) || !tide_cpu_has(&siblings, cpu)) return;
        uint32_t lowest = 0;
        while (!tide_cpu_has(&siblings, lowest)) lowest++;
        if (lowest == cpu) {
            g.cores[group]++;
            g.physical++;
        }
    }
    // Several groups, each of two cores or more, sharing no CPU, between them every one that's online
    if (g.count < 2u) return;
    tide_cpu_set all;
    memset(&all, 0, sizeof all);
    for (uint32_t k = 0; k < g.count; k++) {
        if (g.cores[k] < 2u) return;
        for (uint32_t w = 0; w < TIDE_CPU_WORDS; w++) {
            if (all.bits[w] & g.cpus[k].bits[w]) return;
            all.bits[w] |= g.cpus[k].bits[w];
        }
    }
    for (uint32_t w = 0; w < TIDE_CPU_WORDS; w++) {
        if ((all.bits[w] & online.bits[w]) != online.bits[w]) return;
    }
    *out = g;
}
