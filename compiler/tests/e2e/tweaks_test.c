#include <string.h>

#include "game.h"
#include "tide_test.h"

// tweaks/game.tide with its tweaks.json, built with dev (TWEAKS_LIVE) and without: both read the file's values,
// and a dev build reads the match's TideTweaks and this machine's copy as they are now.

static tide_draw_list draw;
static tide_gui gui;

TIDE_TEST(tweaks_read)
{
    static tide_world world;
    static tide_local local;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_world_tick(&world);
    const Seen *s = &world.Seen;
    TIDE_CHECK(s->kick == 0.25f && s->rounds == 5 && s->auto_ && s->fast == 1 && s->unchanged == 2.0f);
    TIDE_CHECK(s->fog.r == 0.5f && s->fog.a == 1.0f);
    TIDE_CHECK(s->doubled == 0.5f && s->start == 5);
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Drawn.kick == 0.75f);
#ifdef TWEAKS_LIVE
    // Changed in the match, the match's code reads it; changed here, local code does
    world.TideTweaks.values.KickUp = 1.0f;
    world.TideTweaks.values.Combat_Speed = 0;
    tide_world_tick(&world);
    TIDE_CHECK(s->kick == 1.0f && s->doubled == 2.0f && s->fast == 0);
    local.TideTweaksHere.values.KickUp = 2.0f;
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Drawn.kick == 6.0f);
#endif
    tide_local_free(&local);
    tide_world_free(&world);
}
