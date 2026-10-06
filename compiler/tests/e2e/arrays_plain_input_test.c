#include <string.h>

#include "game.h"
#include "tide_test.h"

TIDE_TEST(arrays_plain_input_repairs_struct_elements)
{
    tide_world world = {0};
    tide_world_init(&world, 1.0f);
    Controls input = {0};
    input.dials.at[0].enabled = true;
    input.dials.at[0].value = -5;
    input.dials.at[1].value = 30;
    for (int i = 0; i < 2; i++) {
        memset(input.dials.at[i].tide_pad0, 0xa5, sizeof input.dials.at[i].tide_pad0);
    }
    tide_world_set_server_input(&world, input);
    tide_world_tick(&world);
    TIDE_CHECK(world.Seen.total == 10);
    const Controls expected = {.dials = {.at = {{.enabled = true}, {.value = 10}}}};
    TIDE_CHECK(memcmp(&world.inputs[TIDE_SERVER_INPUT], &expected, sizeof expected) == 0);
    tide_world_free(&world);
}
