// Map and set lookups, Tide against C (see bench/versus/versus.h).

#include <stdlib.h>
#include <stdio.h>

#include "lookup.h"
#include "tide/list.h"
#include "tide/map.h"
#include "tide/page.h"
#include "versus.h"

extern const versus_way lookup_c;

static versus_flag flags[] = {
    {"count", "live keys in each collection", 16384, 257, 0, 0},
    {"changes", "keys removed and replaced each tick", 1024, 19, 0, 0},
};

static void *make(const versus_flag *f)
{
    if (f[0].value > 0x3FFFFFFFu) {
        fputs("versus_lookup: --count is at most 1073741823, the live keys' 30-bit range\n", stderr);
        exit(2);
    }
    tide_world *w = tide_alloc_zeroed(1, sizeof *w);
    tide_world_init(w, VERSUS_DT);
    w->Roster.count = (int32_t)f[0].value;
    w->Roster.changes = (int32_t)f[1].value;
    return w;
}

static void tick(void *world, const tide_jobs *jobs)
{
    tide_world_tick_on(world, jobs);
}

// Primitive-key entries have a signed insertion order followed by the key and value.
// Read directly from this world's heap, as the other versus hosts do for lists.
typedef struct map_entry {
    int64_t order;
    int32_t key, value;
} map_entry;

typedef struct set_entry {
    int64_t order;
    int32_t key;
    uint8_t unused[4];
} set_entry;

_Static_assert(sizeof(map_entry) == 16 && offsetof(map_entry, key) == 8 && offsetof(map_entry, value) == 12
                   && sizeof(set_entry) == 16 && offsetof(set_entry, key) == 8,
               "lookup's entries must match tidec's map and set layout");

static uint64_t digest(const void *world)
{
    const tide_world *w = world;
    const Roster *r = &w->Roster;
    const int32_t progress[4] = {r->count, r->cursor, r->next, w->Time.tick};
    const int64_t observations[4] = {r->observed, r->misses, r->removed, r->added};
    uint64_t sum = versus_item(progress, sizeof progress) + versus_item(observations, sizeof observations);
    int32_t order = 0;
    for (int32_t i = 0; i < tide_list_read_count(&w->heap, r->scores.entries); i++) {
        const map_entry *entry = tide_list_read(&w->heap, r->scores.entries, i, sizeof *entry);
        if (!tide_map_live(entry)) continue;
        const int32_t item[4] = {0, order++, entry->key, entry->value};
        sum += versus_item(item, sizeof item);
    }
    order = 0;
    for (int32_t i = 0; i < tide_list_read_count(&w->heap, r->seen.entries); i++) {
        const set_entry *entry = tide_list_read(&w->heap, r->seen.entries, i, sizeof *entry);
        if (!tide_map_live(entry)) continue;
        const int32_t item[4] = {1, order++, entry->key, 0};
        sum += versus_item(item, sizeof item);
    }
    for (int32_t i = 0; i < r->count; i++) {
        const int32_t key = *(const int32_t *)tide_list_read(&w->heap, r->keys, i, sizeof key);
        const int32_t item[4] = {2, i, key, 0};
        sum += versus_item(item, sizeof item);
    }
    return sum;
}

static void destroy(void *world)
{
    tide_world_free(world);
    free(world);
}

int main(int argc, char **argv)
{
    const versus_way ways[] = {{"Tide", make, tick, digest, destroy}, lookup_c};
    const versus_desc desc = {
        .name = "versus_lookup",
        .about = "map and set hits, misses, score updates and ordered key replacement",
        .flags = flags,
        .flag_count = sizeof flags / sizeof flags[0],
        .warmup = 10,
        .ticks = 200,
        .quick_warmup = 2,
        .quick_ticks = 37, // Enough replacement to exercise map entry compaction more than once
        .ways = ways,
        .way_count = sizeof ways / sizeof ways[0],
    };
    return versus_main(&desc, argc, argv);
}
