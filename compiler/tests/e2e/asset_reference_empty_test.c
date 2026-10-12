#include "game.h"
#include "tide_test.h"
TIDE_TEST(asset_reference_local_type_without_any_assets)
{
    tide_world w = {0};
    tide_world_init(&w, .1f); tide_world_tick(&w);
    TIDE_CHECK(w.Probe.count == 0);
    tide_world_free(&w);
}
