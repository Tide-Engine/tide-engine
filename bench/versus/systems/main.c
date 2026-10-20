// Systems, Tide against C (see bench/versus/versus.h): what the schedule
// costs, from a tick with nothing to do to one big enough for threads.

#include <stdlib.h>

#include "kinds.h"
#include "systems.h"
#include "tide/page.h"
#include "versus.h"

extern const versus_way systems_c;

static versus_flag flags[] = {
    {"count", "entities of each of the four kinds (0: a tick of 64 systems with nothing to do)", 4096, 64, 0, 0},
};

// Each kind's table, its columns, and its components in order, their lanes
// two columns each from column 2 (after the entity and its scene)
_Static_assert(tide_column_arch0_C0_C1_C2_C3_C4_C5_C6_C7_C0 == 2, "a kind's components start at column 2");
_Static_assert(tide_column_arch3_C0_C1_C2_C3_C12_C13_C14_C15_C15 == 2 + 2 * 7, "each component is two columns");

static void *make(const versus_flag *f)
{
    tide_world *w = tide_alloc_zeroed(1, sizeof *w);
    tide_world_init(w, VERSUS_DT);
    w->Setup.count = (int32_t)f[0].value;
    return w;
}

static void tick(void *world, const tide_jobs *jobs)
{
    tide_world_tick_on(world, jobs);
}

static uint64_t digest(const void *world)
{
    const tide_world *w = world;
    const tide_table *tables[KINDS] = {&w->arch0_C0_C1_C2_C3_C4_C5_C6_C7, &w->arch1_C4_C5_C6_C7_C8_C9_C10_C11,
                                       &w->arch2_C8_C9_C10_C11_C12_C13_C14_C15, &w->arch3_C0_C1_C2_C3_C12_C13_C14_C15};
    const tide_columns *columns[KINDS] = {&tide_columns_arch0_C0_C1_C2_C3_C4_C5_C6_C7, &tide_columns_arch1_C4_C5_C6_C7_C8_C9_C10_C11,
                                          &tide_columns_arch2_C8_C9_C10_C11_C12_C13_C14_C15,
                                          &tide_columns_arch3_C0_C1_C2_C3_C12_C13_C14_C15};
    uint64_t sum = 0;
    for (uint32_t q = 0; q < KINDS; q++) {
        for (uint32_t row = 0; row < tables[q]->count; row++) {
            for (uint32_t j = 0; j < KIND_COMPONENTS; j++) {
                float pair[2];
                tide_table_gather(tables[q], columns[q], row, 2u + 2u * j, 2u, 4u, pair);
                sum += kind_item(q, row, j, pair[0], pair[1]);
            }
        }
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
    const versus_way ways[] = {{"Tide", make, tick, digest, destroy}, systems_c};
    const versus_desc desc = {
        .name = "versus_systems",
        .about = "64 systems over 16 components in 4 kinds of entity: twelve lanes of four, and a band of sixteen that wait for each other",
        .flags = flags,
        .flag_count = sizeof flags / sizeof flags[0],
        .warmup = 20,
        .ticks = 500,
        .quick_warmup = 2,
        .quick_ticks = 20,
        .ways = ways,
        .way_count = sizeof ways / sizeof ways[0],
    };
    return versus_main(&desc, argc, argv);
}
