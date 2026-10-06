// Life, Tide against C (see bench/versus/versus.h).

#include <stdlib.h>

#include "life.h"
#include "tide/page.h"
#include "versus.h"

extern const versus_way life_c;

static versus_flag flags[] = {
    {"size", "the grid's width and height, in cells", 1024, 128, 0, 1},
    {"kept", "1 to keep each tick's world before the tick, as a client keeps every tick it predicts", 0, 0, 0, 0},
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
    w->Board.size = (int32_t)f[0].value;
    keeps = f[1].value != 0;
    return w;
}

static void tick(void *world, const tide_jobs *jobs)
{
    if (keeps) tide_world_copy(&kept, world);
    tide_world_tick_on(world, jobs);
}

static uint64_t digest(const void *world)
{
    const tide_world *w = world;
    uint64_t sum = 0;
    for (int32_t y = 0; y < w->Board.size; y++) {
        for (int32_t x = 0; x < w->Board.size; x++) {
            const bool *alive = tide_grid_read(&w->heap, w->Board.cells, x, y, 0, &tide_shape_Grid2_bool);
            if (!alive || !*alive) continue;
            const int32_t item[2] = {x, y};
            sum += versus_item(item, sizeof item);
        }
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
    const versus_way ways[] = {{"Tide", make, tick, digest, destroy}, life_c};
    const versus_desc desc = {
        .name = "versus_life",
        .about = "Conway's Game of Life: every cell of a grid counts its eight neighbours",
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
