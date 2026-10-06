// Buckets, Tide against C (see bench/versus/versus.h): a list of lists, the
// flat list a game wrote before a list could hold lists, and C.

#include <stdlib.h>

#include "buckets.h"
#include "cap.h"
#include "tide/list.h"
#include "tide/page.h"
#include "versus.h"

extern const versus_way buckets_c;

static versus_flag flags[] = {
    {"buckets", "buckets, each keeping its last 32 values", 4096, 256, 0, 1},
    {"kept", "1 to keep each tick's world before the tick, as a client keeps every tick it predicts", 0, 0, 0, 0},
};

// Retaining the world makes writes copy shared heap pages, as they do when
// a client predicts ticks while keeping snapshots for rollback.
static tide_world kept;
static bool keeps;

static void *make_mode(const versus_flag *f, const int32_t mode)
{
    tide_world *w = tide_alloc_zeroed(1, sizeof *w);
    tide_world_init(w, VERSUS_DT);
    w->Pool.buckets = (int32_t)f[0].value;
    w->Pool.mode = mode;
    keeps = f[1].value != 0;
    return w;
}

static void *make_rows(const versus_flag *f)
{
    return make_mode(f, 0);
}

static void *make_flat(const versus_flag *f)
{
    return make_mode(f, 1);
}

static void *make_arrays(const versus_flag *f)
{
    return make_mode(f, 2);
}

static void tick(void *world, const tide_jobs *jobs)
{
    if (keeps) tide_world_copy(&kept, world);
    tide_world_tick_on(world, jobs);
}

// What a bucket holds, oldest first: the same whichever way kept it.
typedef struct held {
    int32_t bucket;
    int32_t at;
    int32_t value;
} held;

static uint64_t world_digest(const tide_world *w)
{
    const Pool *pool = &w->Pool;
    uint64_t sum = 0;
    if (pool->mode == 2 ? !pool->rings.at : !pool->counts.at) return sum; // The snapshot before the first tick is empty
    for (int32_t b = 0; b < pool->buckets; b++) {
        const Ring *ring = pool->mode == 2 ? tide_list_read(&w->heap, pool->rings, b, sizeof(Ring)) : NULL;
        const int32_t count = ring ? ring->count : *(const int32_t *)tide_list_read(&w->heap, pool->counts, b, sizeof(int32_t));
        const int32_t head = ring ? ring->head : *(const int32_t *)tide_list_read(&w->heap, pool->heads, b, sizeof(int32_t));
        const tide_list *row = pool->mode == 0 ? tide_list_read(&w->heap, pool->rows, b, sizeof(tide_list)) : NULL;
        for (int32_t k = 0; k < count; k++) {
            const int32_t cell = (head + k) % CAP;
            const int32_t *value = ring ? &ring->cells.at[cell]
                                 : row  ? tide_list_read(&w->heap, *row, cell, sizeof(int32_t))
                                        : tide_list_read(&w->heap, pool->cells, b * CAP + cell, sizeof(int32_t));
            const held h = {b, k, *value};
            sum += versus_item(&h, sizeof h);
        }
    }
    return sum;
}

static uint64_t digest(const void *world)
{
    uint64_t sum = world_digest(world);
    if (keeps) {
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
    const versus_way ways[] = {{"Tide", make_rows, tick, digest, destroy},
                               {"Tide, flat", make_flat, tick, digest, destroy},
                               {"Tide, arrays", make_arrays, tick, digest, destroy},
                               buckets_c};
    const versus_desc desc = {
        .name = "versus_buckets",
        .about = "a value a tick goes into each of many buckets, which keep their last 32",
        .flags = flags,
        .flag_count = sizeof flags / sizeof flags[0],
        .warmup = 40, // Past the first 32 ticks, every bucket is full, and each tick drops one value for the one it adds
        .ticks = 300,
        .quick_warmup = 2,
        .quick_ticks = 40,
        .ways = ways,
        .way_count = sizeof ways / sizeof ways[0],
    };
    return versus_main(&desc, argc, argv);
}
