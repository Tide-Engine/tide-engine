#include <string.h>

#include "game.h"
#include "tide_test.h"
#ifdef TWEAKS_LIVE
#include <stdio.h>

#include "tide/tweaks.h"
#endif

// tweaks/game.tide with its game.tweaks, built with dev (TWEAKS_LIVE) and without: both read the file's values,
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
    const Fired *fired = &world.Fired; // Tweak fields
    TIDE_CHECK(fired->kick == 0.3f && fired->kicked == 0.6f && fired->fast == 1);
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Drawn.kick == 0.75f);
#ifdef TWEAKS_LIVE
    // What tidec worked out beside the C, which a running dev build takes as a .tweaks file changes: the build's values
    const tide_tweaks_api *api = tide_game_tweaks();
    TIDE_REQUIRE(api != NULL && api->values[0]);
// A web build's sandbox has no files of this machine's (a page takes no tweaks as they change), and an Android
// test runs on a device, which has none of the build machine's either
#if !defined(__wasm__) && !defined(__ANDROID__)
    FILE *f = fopen(api->values, "rb");
    TIDE_REQUIRE(f != NULL);
    static uint8_t bytes[1024];
    const size_t n = fread(bytes, 1, sizeof bytes, f);
    fclose(f);
    TideTweakValues worked;
    TIDE_CHECK(tide_tweaks_load(api, &worked, bytes, n) && memcmp(&worked, &world.TideTweaks.values, sizeof worked) == 0);
#endif
    // Written plainly in game.tweaks, the menu can write over it; worked out there, or the code's, it leaves it
    for (uint32_t i = 0; i < api->count; i++) {
        const tide_tweak *t = &api->tweaks[i];
        if (strcmp(t->name, "KickUp") == 0) TIDE_CHECK(t->plain && strstr(t->file, "tweaks/game.tweaks"));
        if (strcmp(t->name, "Rounds") == 0) TIDE_CHECK(!t->plain);
        if (strcmp(t->name, "Gun.kick") == 0) TIDE_CHECK(!t->plain && strcmp(t->type, "float") == 0);
        if (strcmp(t->name, "Gun.pace") == 0) TIDE_CHECK(t->plain && strcmp(t->type, "Combat.Rate") == 0);
    }
    // Changed in the match, the match's code reads it; changed here, local code does
    world.TideTweaks.values.KickUp = 1.0f;
    world.TideTweaks.values.Combat_Speed = 0;
    world.TideTweaks.values.Gun_kick = 1.5f;
    tide_world_tick(&world);
    TIDE_CHECK(s->kick == 1.0f && s->doubled == 2.0f && s->fast == 0);
    TIDE_CHECK(fired->kick == 1.5f && fired->kicked == 3.0f);
    local.TideTweaksHere.values.KickUp = 2.0f;
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Drawn.kick == 6.0f);
#endif
    tide_local_free(&local);
    tide_world_free(&world);
}
