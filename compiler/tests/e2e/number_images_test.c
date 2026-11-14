#include "game.h"
#include "tide_test.h"

extern const tide_shader *const tide_game_shaders[];

TIDE_TEST(numeric_outputs_keep_their_runtime_types)
{
    TIDE_CHECK(tide_game_shaders[0]->numeric_outputs == 1 && tide_game_shaders[0]->output_types == 1);
    TIDE_CHECK(tide_game_shaders[1]->numeric_outputs == 1 && tide_game_shaders[1]->output_types == 2);
    TIDE_CHECK(tide_game_shaders[2]->numeric_outputs == 1 && tide_game_shaders[2]->output_types == 3);
    TIDE_CHECK(tide_game_shaders[3]->numeric_outputs == 1 && tide_game_shaders[3]->output_types == 4);
    TIDE_CHECK(tide_game_shaders[4]->numeric_outputs == 14 && tide_game_shaders[4]->output_types == (1u << 3 | 2u << 6 | 4u << 9));
    TIDE_CHECK(tide_game_shaders[5]->numeric_outputs == 0 && tide_game_shaders[5]->output_types == 0);
}
