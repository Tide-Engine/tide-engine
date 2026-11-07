#include "game.h"
#include "tide_test.h"

TIDE_TEST(view_lookup_size_blends_without_changing_match)
{
    static tide_world before, now;
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    tide_world_init(&before, 1.0f / 60.0f);
    tide_world_tick(&before);
    tide_world_copy(&now, &before);
    tide_world_tick(&now);
    const uint64_t hash = tide_world_hash(&now);
    tide_local_init(&local);
    tide_frame(&now, &before, 0.5f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Seen.count == 32);
    TIDE_CHECK(local.Seen.sum == 32.0f * 193.0f);
    tide_frame(&now, &before, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Seen.count == 32);
    TIDE_CHECK(local.Seen.sum == 32.0f * 194.0f);
    TIDE_CHECK(tide_world_hash(&now) == hash);
    tide_local_free(&local);
    tide_world_free(&now);
    tide_world_free(&before);
}
