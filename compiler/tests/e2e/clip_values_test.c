#include "game.h"
#include "tide_test.h"

#if defined(TIDE_HAS_ASSETS) || defined(TIDE_HAS_AUDIO)
#error Clip values alone must not enable asset loading or playback.
#endif

TIDE_TEST(scalar_clips_compile_and_run_without_assets_or_playback)
{
    static tide_world world;
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    const uint64_t hash = tide_world_hash(&world);
    tide_local_init(&local);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Saved.clip.name == 0);
    TIDE_CHECK(tide_world_hash(&world) == hash);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}
