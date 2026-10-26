// The debugger tests' program: a few ticks of the fixture's match, on this thread.
#include "game.h"

static tide_world world;

int main(void)
{
    tide_world_init(&world, 1.0f / 30.0f);
    for (int i = 0; i < 4; i++) tide_world_tick(&world);
    tide_world_free(&world);
    return 0;
}
