#include "game.h"
#include "tide_test.h"

static tide_world world;

TIDE_TEST(predict_resets_action_and_keeps_movement)
{
    tide_world_init(&world, 1.0f / 60.0f);
    const Controls input = {.jump = true, .selection = 9, .move = 2.0f};
    tide_world_set_server_input(&world, input);
    tide_world_tick(&world);
    TIDE_CHECK(world.Seen.jumps == 1 && world.Seen.position == 2.0f);
    const tide_player_set keep = {{0}};
    TIDE_REQUIRE(tide_game_api.predict_inputs != NULL);
    TIDE_CHECK(tide_game_api.predict_inputs(&world, &keep));
    TIDE_CHECK(!world.inputs[TIDE_SERVER_INPUT].jump && world.inputs[TIDE_SERVER_INPUT].selection == 3);
    tide_world_tick(&world);
    TIDE_CHECK(world.Seen.jumps == 1 && world.Seen.releases == 0 && world.Seen.position == 4.0f);
    TIDE_CHECK(!tide_game_api.predict_inputs(&world, &keep));
    tide_world_free(&world);
}
