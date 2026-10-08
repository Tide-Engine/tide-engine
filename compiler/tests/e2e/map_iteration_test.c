#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static bool sequence(tide_heap *heap, const tide_list list, const int32_t *expected, const int32_t count)
{
    tide_text_use(heap, NULL);
    if (tide_list_count(list) != count) return false;
    for (int32_t i = 0; i < count; i++) {
        const int32_t *value = tide_list_at(list, i, sizeof *value);
        if (!value || *value != expected[i]) return false;
    }
    return true;
}

TIDE_TEST(map_iteration_follows_insertions_through_compaction)
{
    tide_world world = {0};
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    const int32_t added[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 100};
    const int32_t reinserted[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 8};
    const int32_t cleared[] = {0, 99};
    const int32_t outer[] = {0, 9, 10, 11, 100};
    TIDE_CHECK(sequence(&world.heap, world.Iteration.mapSeen, added, 13));
    TIDE_CHECK(sequence(&world.heap, world.Iteration.setSeen, added, 13));
    TIDE_CHECK(sequence(&world.heap, world.Iteration.clearedSeen, cleared, 2));
    TIDE_CHECK(sequence(&world.heap, world.Iteration.reinsertedSeen, reinserted, 13));
    TIDE_CHECK(sequence(&world.heap, world.Iteration.innerSeen, added, 13));
    TIDE_CHECK(sequence(&world.heap, world.Iteration.outerSeen, outer, 5));
    TIDE_CHECK(world.Iteration.values.live == 4 && world.Iteration.keys.live == 4);
    tide_world_free(&world);
}

TIDE_TEST(map_iteration_resumes_after_another_system_compacts)
{
    tide_world world = {0};
    tide_world snapshot = {0};
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    TIDE_REQUIRE(!world.Waiting.mapDone && !world.Waiting.setDone);
    tide_world_copy(&snapshot, &world);
    const uint64_t kept = tide_world_hash(&snapshot);
    for (int i = 0; i < 7; i++) {
        tide_world_tick(&world);
        TIDE_CHECK(tide_world_hash(&snapshot) == kept);
    }
    const int32_t expected[] = {0, 9, 10, 11, 100};
    TIDE_CHECK(world.Waiting.mapDone && world.Waiting.setDone);
    TIDE_CHECK(sequence(&world.heap, world.Waiting.mapSeen, expected, 5));
    TIDE_CHECK(sequence(&world.heap, world.Waiting.setSeen, expected, 5));

    const uint32_t size = tide_world_pack(&snapshot, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    tide_world_pack(&snapshot, bytes, size);
    tide_world restored = {0};
    TIDE_REQUIRE(tide_world_unpack(&restored, bytes, size));
    free(bytes);
    for (int i = 0; i < 7; i++) tide_world_tick(&restored);
    TIDE_CHECK(tide_world_hash(&restored) == tide_world_hash(&world));
    TIDE_CHECK(sequence(&restored.heap, restored.Waiting.mapSeen, expected, 5));
    TIDE_CHECK(sequence(&restored.heap, restored.Waiting.setSeen, expected, 5));
    tide_world_free(&restored);
    tide_world_free(&snapshot);
    tide_world_free(&world);
}
