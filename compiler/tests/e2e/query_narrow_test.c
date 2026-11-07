#include <stdlib.h>
#include "game.h"
#include "entity_order_test.h"
#include "tide_test.h"

static uint32_t whole_calls;

int32_t CheckWhole(const Wide value)
{
    whole_calls++;
    if (value.index < 0 || value.index >= 3000
        || value.position.x != (float)(value.index % 16)
        || value.position.y != (float)(-1000 - value.index)
        || value.position.z != (float)(value.index % 7)) return 0;
    for (int k = 0; k < 84; k++)
        if (value.payload.at[k] != (float)(value.index * 100 + k)) return 0;
    return 1;
}

static uint32_t keys(tide_world *world, entity_order_value *out)
{
    uint32_t count = 0;
    for (uint32_t slot = 0; slot < world->entities.next_unused; slot++) {
        const tide_entity entity = tide_entity_in_slot(&world->entities, slot);
        if (!tide_has_Wide(world, entity)) continue;
        if (count == 3000u) return 0;
        out[count++] = (entity_order_value){entity, tide_get_Wide(world, entity).index};
    }
    qsort(out, count, sizeof *out, entity_order_compare);
    return count;
}

TIDE_TEST(query_narrow_reads_swizzles_and_entity_only_bindings_in_snapshots)
{
    static tide_world world, snapshot, replay;
    tide_world_init(&world, 1.0f);
    tide_world_copy(&snapshot, &world);
    const uint64_t before = tide_world_hash(&snapshot);
    tide_world_tick(&world);
    float expected = 0;
    for (int i = 0; i < 3000; i++) expected += (float)(i % 16 + i % 7);
    TIDE_CHECK(world.Probe.sum == 24.0f * expected);
    TIDE_CHECK(world.Probe.zeroDemand == 1000);
    TIDE_CHECK(tide_world_hash(&snapshot) == before);
    tide_world_copy(&replay, &snapshot);
    tide_world_tick(&replay);
    TIDE_CHECK(tide_world_hash(&replay) == tide_world_hash(&world));
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    void *bytes = malloc(size);
    TIDE_REQUIRE(bytes);
    TIDE_REQUIRE(tide_world_pack(&world, bytes, size) == size);
    tide_world_free(&replay);
    TIDE_REQUIRE(tide_world_unpack(&replay, bytes, size));
    TIDE_CHECK(tide_world_hash(&replay) == tide_world_hash(&world));
    free(bytes);
    tide_world_free(&replay);
    tide_world_free(&snapshot);
    tide_world_free(&world);
}

TIDE_TEST(query_narrow_whole_values_retain_every_lane_for_c)
{
    static tide_world world, snapshot;
    tide_world_init(&world, 1.0f);
    entity_order_value *order = malloc(3000u * sizeof *order);
    TIDE_REQUIRE(order && keys(&world, order) == 3000u);
    for (uint32_t i = 0; i < 3000u; i++) {
        Wide value = tide_get_Wide(&world, order[i].key);
        for (int k = 0; k < 84; k++) value.payload.at[k] = (float)(value.index * 100 + k);
        TIDE_REQUIRE(tide_set_Wide(&world, order[i].key, value));
    }
    world.Probe.whole = true;
    tide_world_copy(&snapshot, &world);
    const uint64_t before = tide_world_hash(&snapshot);
    whole_calls = 0;
    tide_world_tick(&world);
    TIDE_CHECK(world.Probe.wholeSum == 3000 && whole_calls == 3000u);
    TIDE_CHECK(tide_world_hash(&snapshot) == before);
    free(order);
    tide_world_free(&snapshot);
    tide_world_free(&world);
}

TIDE_TEST(query_narrow_live_set_refreshes_values_and_preserves_other_cached_columns)
{
    static tide_world world, snapshot;
    tide_world_init(&world, 1.0f);
    entity_order_value *order = malloc(3000u * sizeof *order);
    TIDE_REQUIRE(order && keys(&world, order) == 3000u);
    world.Probe.repeats = 0;
    world.Probe.live = true;
    world.Probe.target = order[2999].key;
    tide_world_copy(&snapshot, &world);
    const uint64_t before = tide_world_hash(&snapshot);
    tide_world_tick(&world);
    // Every earlier source updates the last logical row before it is read.
    TIDE_CHECK(world.Probe.observed == 999.0f + 777.0f);
    TIDE_CHECK(world.Probe.markerSum == 3000 * 2999 / 2);
    for (uint32_t i = 0; i < 3000u; i++) {
        TIDE_CHECK(tide_get_Auxiliary(&world, order[i].key).value == order[i].value + 1);
        TIDE_CHECK(tide_get_Marker(&world, order[i].key).value == order[i].value);
    }
    TIDE_CHECK(tide_world_hash(&snapshot) == before);
    TIDE_CHECK(tide_get_Wide(&world, order[2999].key).position.y == (float)(-1000 - order[2999].value));
    free(order);
    tide_world_free(&snapshot);
    tide_world_free(&world);
}
