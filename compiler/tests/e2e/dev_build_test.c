#include <string.h>

#include "game.h"
#include "tide_test.h"

// One game built twice: with dev (DEV_BUILD defined here) and without. A dev build lists its dev views, which run
// only while they're shown; the other has none, and its match is the one the dev build's is when nothing dev is used.

static tide_draw_list draw;
static tide_gui gui;

static void frame(const tide_world *w, tide_local *local)
{
    tide_draw_reset(&draw);
    tide_frame(w, NULL, 1.0f, NULL, NULL, 1.0f, local, &draw, &gui);
}

TIDE_TEST(dev_build_views_shown)
{
    static tide_world world;
    static tide_local local;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_world_tick(&world);
#ifdef DEV_BUILD
    TIDE_REQUIRE(tide_dev_view_names[0] != NULL);
    TIDE_CHECK(strcmp(tide_dev_view_names[0], "GunTools") == 0);
    TIDE_CHECK(tide_dev_view_names[1] == NULL);
#else
    TIDE_CHECK(tide_dev_view_names[0] == NULL);
#endif
    frame(&world, &local); // Not shown: it doesn't run
    TIDE_CHECK(local.Drawn.game == 1 && local.Drawn.tools == 0);
    const uint8_t on[1] = {1};
    tide_show_dev_views(on);
    frame(&world, &local);
#ifdef DEV_BUILD
    TIDE_CHECK(local.Drawn.game == 2 && local.Drawn.tools == 1);
#else
    TIDE_CHECK(local.Drawn.game == 2 && local.Drawn.tools == 0);
#endif
    tide_show_dev_views(NULL);
    frame(&world, &local);
    TIDE_CHECK(local.Drawn.game == 3 && local.Drawn.tools <= 1);
    for (int i = 0; i < 10; i++) tide_world_tick(&world);
    TIDE_CHECK(world.Shots.fired == 3);
    tide_local_free(&local);
    tide_world_free(&world);
}
