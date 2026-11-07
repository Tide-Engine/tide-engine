// The debugger tests' program: a few ticks of the fixture's match, on this thread.
#include "game.h"

static tide_world world;
// Full logical keys, including a nonnull trusted-C namespace key.
tide_entity tide_debug_identity = {0x12345678ABCDEF01ull, 0xFEDCBA9876543210ull};
tide_entity tide_debug_half = {0, 0xFEDCBA9876543210ull};
tide_entity tide_debug_null = {0};

int main(void)
{
    if (tide_entity_is_null(tide_debug_identity) || tide_entity_is_null(tide_debug_half) || !tide_entity_is_null(tide_debug_null)) return 1;
    tide_world_init(&world, 1.0f / 30.0f);
    for (int i = 0; i < 4; i++) tide_world_tick(&world);
    tide_world_free(&world);
    return 0;
}
