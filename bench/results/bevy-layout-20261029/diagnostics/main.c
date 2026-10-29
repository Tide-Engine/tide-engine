// Wide, Tide against C (see bench/versus/versus.h).

#include <stdlib.h>

#include "tide/page.h"
#include "versus.h"
#include "wide.h"

extern const versus_way wide_c;
extern const versus_way layout_way;
int layout_validate(void);
int layout_maintenance(void);
#include <string.h>

static versus_flag flags[] = {
    {"count", "bodies", 1000000, 10000, 0, 0},
    {"kept", "1 to keep each tick's world before the tick, as a client keeps every tick it predicts", 0, 0, 0, 0},
    {"tagged", "1 for crates, which a system moves unless they're frozen", 0, 0, 0, 0},
    {"frozen", "of a hundred crates, how many are frozen", 0, 0, 0, 0},
    {"layout", "0 records, 1 blocks4, 2 packed, 3 writeback, 4 rebuild", 0, 0, 0, 0},
};

// With --kept 1, each tick's world is kept first, as a client keeps every tick
// it predicts: the snapshot shares the world's pages, so the tick writes none
// of them in place.
static tide_world kept;
static bool keeps;

static void *make(const versus_flag *f)
{
    tide_world *w = tide_alloc_zeroed(1, sizeof *w);
    tide_world_init(w, VERSUS_DT);
    w->Yard.count = (int32_t)f[0].value;
    keeps = f[1].value != 0;
    w->Yard.tagged = f[2].value != 0;
    w->Yard.frozen = (int32_t)f[3].value;
    return w;
}

static void tick(void *world, const tide_jobs *jobs)
{
    if (keeps) tide_world_copy(&kept, world);
    tide_world_tick_on(world, jobs);
}

static uint64_t world_digest(const tide_world *w)
{
    uint64_t sum = 0;
    for (uint32_t row = 0; row < w->arch0_Body.count; row++) {
        const Body b = TIDE_AT(w, arch0_Body, Body, row);
        sum += versus_item(&b, sizeof b);
    }
    for (uint32_t row = 0; row < w->arch1_Crate.count; row++) {
        const Crate c = TIDE_AT(w, arch1_Crate, Crate, row);
        sum += versus_item(&c, sizeof c);
    }
    return sum;
}

static uint64_t digest(const void *world)
{
    uint64_t sum = world_digest(world);
    if (keeps) {
        // Read the retained world after the tick, checking that the tick's
        // writes left it as it was as well as checking the current world's.
        const uint64_t before = world_digest(&kept);
        sum += versus_item(&before, sizeof before);
    }
    return sum;
}

static void destroy(void *world)
{
    tide_world_free(world);
    tide_world_free(&kept);
    free(world);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--validate-layout") == 0) return layout_validate();
    if (argc == 2 && strcmp(argv[1], "--maintenance") == 0) return layout_maintenance();
    versus_way ways[] = {{"Tide", make, tick, digest, destroy}, layout_way, wide_c};
    if (getenv("TIDE_LAYOUT_FIRST")) { const versus_way swap=ways[0]; ways[0]=ways[1]; ways[1]=swap; }
    const versus_desc desc = {
        .name = "versus_wide",
        .about = "bodies of a dozen floats each fall and bounce, a tick changing half of them",
        .flags = flags,
        .flag_count = sizeof flags / sizeof flags[0],
        .warmup = 10,
        .ticks = 100,
        .quick_warmup = 2,
        .quick_ticks = 20,
        .ways = ways,
        .way_count = sizeof ways / sizeof ways[0],
    };
    return versus_main(&desc, argc, argv);
}
