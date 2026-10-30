#include <stdlib.h>

#include "game.h"
#include "tide/jobs.h"
#include "tide_test.h"

enum { TICKS = 3 };
static tide_world world, kept;
static bool pooled;

static bool wanted(const int i, const int count)
{
    const int at = i % 128;
    const bool needed_a = at == 0 || at == 15 || at == 16 || at == 31 || at == 32 ||
        at == 63 || at == 64 || at == 127 || i == count - 1;
    return needed_a && i % 5 != 2 && at != 31 && at != 16;
}

static void alone(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    (void)self;
    pooled = true;
    work(context, 0);
}

static const tide_jobs eight = {.threads = 8, .run = alone};

static void check_world(const int count, const int tick)
{
    int seen = 0, items = 0;
    for (int i = 0; i < count; i++) {
        if (!wanted(i, count) || (tick > 1 && i % 3 == 0)) continue;
        const int32_t *value = tide_list_at(world.Rules.seen, seen++, sizeof *value);
        TIDE_REQUIRE(value && *value == i);
    }
    TIDE_CHECK(tide_list_count(world.Rules.seen) == seen);
    for (uint32_t slot = 0; slot < world.entities.next_unused; slot++) {
        const tide_entity entity = tide_entity_in_slot(&world.entities, slot);
        if (!tide_has_Item(&world, entity)) continue;
        const Item item = tide_get_Item(&world, entity);
        const int visits = !wanted(item.ordinal, count) ? 0 : item.ordinal % 3 == 0 ? 1 : tick;
        TIDE_REQUIRE(item.visits == visits && item.small == 9 + visits * 3);
        items++;
    }
    TIDE_CHECK(items == count);
}

static void play(const int count, const bool snapshot, const tide_jobs *jobs, const bool packed, uint64_t hashes[TICKS])
{
    tide_world_init(&world, 1.0f / 60.0f);
    world.Rules.count = count;
    tide_world_tick(&world); // Setup's births become available after the tick.
    if (packed) {
        tide_world restored = {0};
        const uint32_t size = tide_world_pack(&world, NULL, 0);
        uint8_t *bytes = malloc(size);
        TIDE_REQUIRE(bytes && tide_world_pack(&world, bytes, size) == size);
        TIDE_REQUIRE(tide_world_unpack(&restored, bytes, size));
        TIDE_REQUIRE(tide_world_hash(&restored) == tide_world_hash(&world));
        free(bytes);
        tide_world_free(&world);
        world = restored;
    }
    for (int tick = 1; tick <= TICKS; tick++) {
        uint64_t before = 0;
        if (snapshot) {
            tide_world_copy(&kept, &world);
            before = tide_world_hash(&kept);
        }
        tide_world_tick_on(&world, jobs);
        check_world(count, tick);
        hashes[tick - 1] = tide_world_hash(&world);
        if (snapshot) TIDE_CHECK(tide_world_hash(&kept) == before);
    }
    tide_world_free(&world);
    tide_world_free(&kept);
}

TIDE_TEST(sparse_masks_preserve_intersections_order_tails_and_snapshots)
{
    // Empty, the four-row first allocation, scalar tails, vector ends, former
    // compaction blocks, and chunk/piece boundaries, with bit 0 and bit 15.
    static const int counts[] = {0, 1, 4, 5, 15, 16, 17, 31, 32, 33, 63, 64, 65,
        127, 128, 129, 2047, 2048, 2049, 4099};
    for (size_t c = 0; c < sizeof counts / sizeof *counts; c++) {
        uint64_t baseline[TICKS], shared[TICKS], pieces[TICKS], restored[TICKS];
        play(counts[c], false, NULL, false, baseline);
        play(counts[c], true, NULL, false, shared);
        play(counts[c], false, &eight, false, pieces);
        play(counts[c], true, &eight, true, restored);
        for (int tick = 0; tick < TICKS; tick++) {
            TIDE_CHECK(shared[tick] == baseline[tick]);
            TIDE_CHECK(pieces[tick] == baseline[tick]);
            TIDE_CHECK(restored[tick] == baseline[tick]);
        }
    }
    TIDE_CHECK(pooled);
}
