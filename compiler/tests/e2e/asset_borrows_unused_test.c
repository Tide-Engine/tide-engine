#include "game.h"
#include "tide_test.h"

#if defined(TIDE_HAS_ASSETS) || defined(TIDE_HAS_AUDIO)
#error Borrowed signatures alone must not enable asset loading or playback.
#endif

TIDE_TEST(asset_borrow_signatures_compile_without_assets_or_playback)
{
    static tide_world world;
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_world_free(&world);
}
