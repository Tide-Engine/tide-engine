#include "game.h"
#include "tide/jobs.h"
#include "tide_test.h"

enum { COUNT = 6147, TICKS = 3 };
static tide_world world, kept;

static bool wanted(const int i)
{
    const bool needed = i >= 4096 ? i % 100 != 0 : i >= 2048 ? i % 100 < 10
        : i % 512 < 128 || (i % 512 >= 256 && i % 3 != 0);
    return needed && !(i % 512 >= 384 && i % 2 == 0);
}

static void check_rows(const tide_list list, const int scene, const int tick)
{
    int at = 0;
    for (int side = 0; side < 2; side++) {
        if (scene >= 0 && side != scene) continue;
        for (int i = 0; i < COUNT; i++) {
            const int ordinal = side * 10000 + i;
            if (!wanted(i) || (tick > 0 && ordinal % 17 == 5)) continue;
            const int32_t *value = tide_list_at(list, at++, sizeof *value);
            TIDE_REQUIRE(value && *value == ordinal);
        }
    }
    TIDE_CHECK(tide_list_count(list) == at);
}

// Pooled entry must match serial entry. Existing rows tests cover actual
// component-loop pieces; this fixture's ordered list writes stay serial.
static void alone(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    (void)self;
    work(context, 0);
}

static const tide_jobs eight = {.threads = 8, .run = alone};

static void play(const bool snapshot, const tide_jobs *jobs, uint64_t hashes[TICKS])
{
    tide_world_init(&world, 1.0f / 60.0f);
    for (int tick = 0; tick < TICKS; tick++) {
        uint64_t before = 0;
        if (snapshot) {
            tide_world_copy(&kept, &world);
            before = tide_world_hash(&kept);
        }
        tide_world_tick_on(&world, jobs);
        check_rows(world.Probe.rows, -1, tick);
        check_rows(world.Probe.arena, 0, tick);
        check_rows(world.Probe.outside, 1, tick);
        for (uint32_t slot = 0; slot < world.entities.next_unused; slot++) {
            const tide_entity entity = tide_entity_in_slot(&world.entities, slot);
            if (!tide_has_Item(&world, entity)) continue;
            const Item item = tide_get_Item(&world, entity);
            const int index = item.ordinal % 10000;
            const int visits = !wanted(index) ? 0 : item.ordinal % 17 == 5 ? 1 : tick + 1;
            TIDE_REQUIRE(item.visits == visits && item.small == 7 + visits);
        }
        hashes[tick] = tide_world_hash(&world);
        if (snapshot) TIDE_CHECK(tide_world_hash(&kept) == before);
    }
    tide_world_free(&world);
    tide_world_free(&kept);
}

TIDE_TEST(sparse_row_selection_preserves_order_and_deferred_presence)
{
    uint64_t baseline[TICKS], shared[TICKS], pooled[TICKS];
    play(false, NULL, baseline);
    play(true, NULL, shared);
    play(false, &eight, pooled);
    for (int tick = 0; tick < TICKS; tick++) {
        TIDE_CHECK(shared[tick] == baseline[tick]);
        TIDE_CHECK(pooled[tick] == baseline[tick]);
    }
}
