#include <string.h>

#include "game.h"
#include "tide_test.h"

// The Main scene is slot 0; its Setup spawns these in order.
static const tide_entity SMALL = {1, 1};
static const tide_entity WIDE = {2, 1};

static bool is(const tide_str s, const char *expected)
{
    return s.bytes == (int32_t)strlen(expected) && memcmp(s.ptr, expected, strlen(expected)) == 0;
}

// A snapshot kept beside the world, as a client keeps one: each tick writes its
// rows apart, so only the lanes a system writes are begun and put back.
TIDE_TEST(wide_lanes_written_apart_beside_a_snapshot)
{
    tide_world world = {0}, kept = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    for (int tick = 1; tick <= 6; tick++) {
        tide_world_free(&kept);
        tide_world_copy(&kept, &world);
        tide_world_tick(&world);
    }
    TIDE_REQUIRE(tide_has_Small(&world, SMALL) && tide_has_Wide(&world, WIDE));
    const Small s = tide_get_Small(&world, SMALL);
    // The second element's field: on a mask that took it for the first's, it was lost
    TIDE_CHECK(s.pair.at[1].b == 18 && s.pair.at[0].b == 0 && s.touched == 6);
    const Wide w = tide_get_Wide(&world, WIDE);
    TIDE_CHECK(w.steps == 6 && w.x == 3.0f && w.speed == 0.5f && w.spare.at[40] == 7.0f && w.spare.at[0] == 0.0f);
    TIDE_CHECK(is(tide_text_read(&world.heap, w.name), "even"));
    // The snapshot is as the world was a tick before
    const Wide before = tide_get_Wide(&kept, WIDE);
    TIDE_CHECK(before.steps == 5 && before.x == 2.5f && before.spare.at[40] == 7.0f);
    tide_world_free(&kept);
    tide_world_free(&world);
}
