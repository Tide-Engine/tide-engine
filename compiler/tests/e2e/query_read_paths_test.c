#include "game.h"
#include "tide_test.h"

TIDE_TEST(query_read_paths_keep_property_and_swizzle_lane_meanings)
{
    static tide_world world, snapshot;
    tide_world_init(&world, 1.0f);
    tide_world_copy(&snapshot, &world);
    const uint64_t before = tide_world_hash(&snapshot);
    tide_world_tick(&world);
    TIDE_CHECK(world.Seen.property == 5.0f);
    TIDE_CHECK(world.Seen.swizzle == 5.0f);
    TIDE_CHECK(tide_world_hash(&snapshot) == before);
    tide_world_free(&snapshot);
    tide_world_free(&world);
}
