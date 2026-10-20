// The desktop pool's cache groups on Linux and Android (src/cpu_groups.h),
// read from made-up sysfs trees, so every system tests what only a Linux
// machine with several last-level caches would otherwise show.
#include "tide_test.h"

#include "../src/cpu_groups.h"

#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define MAKE_DIR(path) mkdir(path, 0777)
#endif

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    TIDE_CHECK(f != NULL);
    if (!f) return;
    fputs(text, f);
    fputc('\n', f);
    fclose(f);
}

// A tree under `root` of `cpus` CPUs, `online` of them: each one's caches, an L1
// data and instruction cache and an L2 of its core's own, then the last level,
// which `shared(cpu)` says the CPUs of, and its core's threads, `siblings(cpu)`.
typedef const char *(*cpu_text)(unsigned cpu, char *out, size_t room);

static void make_tree(const char *root, const char *online, const unsigned cpus, const int last_level, cpu_text shared, cpu_text siblings)
{
    char path[512], text[128];
    MAKE_DIR("cpu_groups_trees");
    MAKE_DIR(root);
    snprintf(path, sizeof path, "%s/online", root);
    put(path, online);
    for (unsigned cpu = 0; cpu < cpus; cpu++) {
        snprintf(path, sizeof path, "%s/cpu%u", root, cpu);
        MAKE_DIR(path);
        snprintf(path, sizeof path, "%s/cpu%u/topology", root, cpu);
        MAKE_DIR(path);
        snprintf(path, sizeof path, "%s/cpu%u/topology/thread_siblings_list", root, cpu);
        put(path, siblings(cpu, text, sizeof text));
        snprintf(path, sizeof path, "%s/cpu%u/cache", root, cpu);
        MAKE_DIR(path);
        const char *types[4] = {"Data", "Instruction", "Unified", "Unified"};
        const int levels[4] = {1, 1, 2, last_level};
        for (int i = 0; i < (last_level > 2 ? 4 : 3); i++) {
            snprintf(path, sizeof path, "%s/cpu%u/cache/index%d", root, cpu, i);
            MAKE_DIR(path);
            snprintf(path, sizeof path, "%s/cpu%u/cache/index%d/type", root, cpu, i);
            put(path, types[i]);
            snprintf(path, sizeof path, "%s/cpu%u/cache/index%d/level", root, cpu, i);
            snprintf(text, sizeof text, "%d", levels[i]);
            put(path, text);
            snprintf(path, sizeof path, "%s/cpu%u/cache/index%d/shared_cpu_list", root, cpu, i);
            put(path, i < 3 && last_level > 2 ? siblings(cpu, text, sizeof text) : shared(cpu, text, sizeof text));
        }
    }
}

// A Ryzen 9 7900X: 24 CPUs, cores 0-11 with their second threads 12-23, two
// chiplets of six cores, each its own L3
static const char *ryzen_l3(const unsigned cpu, char *out, const size_t room)
{
    snprintf(out, room, "%s", cpu % 12u < 6u ? "0-5,12-17" : "6-11,18-23");
    return out;
}

static const char *ryzen_threads(const unsigned cpu, char *out, const size_t room)
{
    snprintf(out, room, "%u,%u", cpu % 12u, cpu % 12u + 12u);
    return out;
}

// One L3 for every core (a single chiplet, or a virtual machine that shows one)
static const char *one_l3(const unsigned cpu, char *out, const size_t room)
{
    (void)cpu;
    snprintf(out, room, "0-23");
    return out;
}

// Four CPUs, two cores of two threads, a cache each: groups of one core, too
// small to keep apart
static const char *tiny(const unsigned cpu, char *out, const size_t room)
{
    snprintf(out, room, "%u,%u", cpu % 2u, cpu % 2u + 2u);
    return out;
}

// No SMT: each CPU a core of its own
static const char *alone(const unsigned cpu, char *out, const size_t room)
{
    snprintf(out, room, "%u", cpu);
    return out;
}

static const char *quad_l3(const unsigned cpu, char *out, const size_t room)
{
    snprintf(out, room, "%u-%u", cpu / 4u * 4u, cpu / 4u * 4u + 3u);
    return out;
}

TIDE_TEST(cpu_groups_two_chiplets)
{
    make_tree("cpu_groups_trees/ryzen", "0-23", 24, 3, ryzen_l3, ryzen_threads);
    tide_cpu_groups g;
    tide_cpu_groups_find("cpu_groups_trees/ryzen", &g);
    TIDE_CHECK(g.count == 2u);
    TIDE_CHECK(g.physical == 12u);
    TIDE_CHECK(g.cores[0] == 6u && g.cores[1] == 6u);
    for (unsigned cpu = 0; cpu < 24; cpu++) {
        const unsigned group = cpu % 12u < 6u ? 0u : 1u;
        TIDE_CHECK(tide_cpu_has(&g.cpus[group], cpu));
        TIDE_CHECK(!tide_cpu_has(&g.cpus[1u - group], cpu));
    }
}

TIDE_TEST(cpu_groups_one_cache_is_one_group)
{
    make_tree("cpu_groups_trees/one", "0-23", 24, 3, one_l3, ryzen_threads);
    tide_cpu_groups g;
    tide_cpu_groups_find("cpu_groups_trees/one", &g);
    TIDE_CHECK(g.count == 0u);
}

TIDE_TEST(cpu_groups_of_one_core_are_none)
{
    make_tree("cpu_groups_trees/tiny", "0-3", 4, 3, tiny, tiny);
    tide_cpu_groups g;
    tide_cpu_groups_find("cpu_groups_trees/tiny", &g);
    TIDE_CHECK(g.count == 0u);
}

TIDE_TEST(cpu_groups_without_smt_count_each_cpu)
{
    make_tree("cpu_groups_trees/quads", "0-15", 16, 3, quad_l3, alone);
    tide_cpu_groups g;
    tide_cpu_groups_find("cpu_groups_trees/quads", &g);
    TIDE_CHECK(g.count == 4u);
    TIDE_CHECK(g.physical == 16u);
    for (unsigned k = 0; k < 4; k++) TIDE_CHECK(g.cores[k] == 4u);
}

TIDE_TEST(cpu_groups_need_every_cpu_read)
{
    // The last-level cache only at level 2, shared by pairs of cores: groups
    // of the highest unified level there is, which here is L2
    make_tree("cpu_groups_trees/l2", "0-7", 8, 2, quad_l3, alone);
    tide_cpu_groups g;
    tide_cpu_groups_find("cpu_groups_trees/l2", &g);
    TIDE_CHECK(g.count == 2u);
    // A tree with no such files at all: none
    tide_cpu_groups_find("cpu_groups_trees/missing", &g);
    TIDE_CHECK(g.count == 0u);
}

TIDE_TEST(cpu_groups_lists)
{
    tide_cpu_set s;
    TIDE_CHECK(tide_cpu_list("0-5,12-17", &s));
    TIDE_CHECK(tide_cpu_has(&s, 0) && tide_cpu_has(&s, 5) && !tide_cpu_has(&s, 6) && tide_cpu_has(&s, 12) && tide_cpu_has(&s, 17));
    TIDE_CHECK(tide_cpu_list("7", &s) && tide_cpu_has(&s, 7) && !tide_cpu_has(&s, 6));
    TIDE_CHECK(!tide_cpu_list("", &s));
    TIDE_CHECK(!tide_cpu_list("5-3", &s));
    TIDE_CHECK(!tide_cpu_list("1,x", &s));
    TIDE_CHECK(!tide_cpu_list("4096", &s));
    TIDE_CHECK(tide_cpu_list("1023", &s) && tide_cpu_has(&s, 1023));
}
