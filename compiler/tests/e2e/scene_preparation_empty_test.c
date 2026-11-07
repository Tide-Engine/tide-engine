#include "game.h"
#include "tide_test.h"

TIDE_TEST(scene_preparation_empty_and_local_readiness)
{
    tide_world world = {0};
    tide_local local = {0};
    static tide_draw_list draw;
    static tide_gui gui;
    tide_world_init(&world, 0.1f);
    tide_local_init(&local);
    TIDE_CHECK(!tide_world_prepare_pending(&world));
    tide_frame(&world, NULL, 1.0f, &world, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.UI.matchReady && !local.UI.ready);
    tide_frame(&world, NULL, 1.0f, &world, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.UI.matchReady && local.UI.ready);
    TIDE_CHECK(!tide_world_scene_prepared(&world, (tide_entity){0}, (tide_player_id){0}));
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
}
