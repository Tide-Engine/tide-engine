#include "game.h"
#include "tide_test.h"

TIDE_TEST(float_bitcasts_and_hashes_preserve_every_input_word)
{
    tide_world world = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    TIDE_CHECK(world.Results.checks == 12);
    tide_world_free(&world);
}
