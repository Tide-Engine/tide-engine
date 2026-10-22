#include "game.h"
#include "tide_test.h"

#ifndef TIDE_HAS_AUDIO
#error Placing the listener is audio: the local state carries the frame's audio list.
#endif

TIDE_TEST(audio_listener_without_sounds_builds_and_runs)
{
    static tide_world world;
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_world_free(&world);
}
