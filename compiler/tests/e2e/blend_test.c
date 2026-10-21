#include <string.h>

#include "game.h"
#include "tide_test.h"
#include "draw_test.h"

static tide_world before;
static tide_world now;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

static void frame(const tide_world *previous, const float alpha)
{
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(1920.0f, 1080.0f), NULL);
    tide_frame(&now, previous, alpha, NULL, NULL, 1.0f, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
    tide_test_flatten(&draw); // Its plain shapes a command each, as the checks read them
}

static const tide_draw_command *nth(const uint32_t kind, int n)
{
    for (uint32_t i = 0; i < draw.count; i++) {
        if (draw.commands[i].kind == kind && n-- == 0) return &draw.commands[i];
    }
    return NULL;
}

TIDE_TEST(blend_verified_parameters_and_queries_use_the_confirmed_interval)
{
    static tide_world confirmed_before, confirmed, predicted;
    tide_world_init(&confirmed, 1.0f / 20.0f);
    tide_world_tick(&confirmed);
    tide_world_copy(&confirmed_before, &confirmed);
    tide_world_tick(&confirmed);
    tide_world_copy(&predicted, &confirmed);
    tide_world_tick(&predicted);
    tide_local_init(&local);
    const uint64_t hash = tide_world_hash(&confirmed);
    tide_draw_reset(&draw);
    tide_frame(&predicted, &confirmed, 0.75f, &confirmed, &confirmed_before, 0.25f, &local, &draw, &gui);
    tide_test_flatten(&draw); // Its plain shapes a command each, as the checks read them
    TIDE_CHECK(local.ConfirmedSeen.bodyX == 5.0f && local.ConfirmedSeen.mixedX == 5.0f);
    TIDE_CHECK(local.ConfirmedSeen.queryX == 105.0f && local.ConfirmedSeen.mixedQueryX == 105.0f);
    TIDE_CHECK(local.ConfirmedSeen.heading == 355.0f && local.ConfirmedSeen.radius == 3.0f && local.ConfirmedSeen.lives == 2);
    TIDE_CHECK(local.ConfirmedSeen.jumperX == 1000.0f);
    TIDE_CHECK(local.ConfirmedSeen.cameraY == 10.0f && local.ConfirmedSeen.shotY == 500.0f);
    TIDE_CHECK(tide_world_hash(&confirmed) == hash);
    tide_local_free(&local);
    tide_world_free(&predicted);
    tide_world_free(&confirmed);
    tide_world_free(&confirmed_before);
}

TIDE_TEST(blend_views_see_the_match_between_ticks)
{
    tide_local_init(&local);
    tide_world_init(&now, 1.0f / 20.0f);
    tide_world_tick(&now); // Tick 0: the first body moves
    tide_world_copy(&before, &now);
    tide_world_tick(&now); // Tick 1: it moves again, and a second body appears

    frame(&before, 0.25f);
    const tide_draw_command *first = nth(TIDE_DRAW_CIRCLE, 0);
    const tide_draw_command *second = nth(TIDE_DRAW_CIRCLE, 1);
    TIDE_REQUIRE(first && second);
    TIDE_CHECK(first->a.x == 5.0f);                   // 4 then 8: a quarter of the way
    TIDE_CHECK(first->b.x == 3.0f);                   // [Snap]: as it is, 1 + 2
    TIDE_CHECK(first->color.r > 0.62f && first->color.r < 0.63f); // A struct's floats blend too: 0.5 then 1 (a byte)
    TIDE_CHECK(second->a.x == 100.0f);                // It wasn't there last tick: as it is
    const tide_draw_command *camera = nth(TIDE_DRAW_CAMERA, 0);
    TIDE_REQUIRE(camera != NULL);
    TIDE_CHECK(camera->a.y == 10.0f); // A singleton: 8 then 16
    const tide_draw_command *text = nth(TIDE_DRAW_TEXT, 0);
    TIDE_REQUIRE(text != NULL);
    TIDE_CHECK(strcmp(draw.text + text->text, "2 2") == 0); // Ints snap

    // Angle has its own Interpolate: 350 then 10, the short way round
    const tide_draw_command *heading = nth(TIDE_DRAW_LINE, 0);
    TIDE_REQUIRE(heading != NULL);
    TIDE_CHECK(heading->b.x - heading->a.x == 355.0f);
    // What jumped is drawn where it is: an entity that snapped, and a singleton
    const tide_draw_command *jumper = nth(TIDE_DRAW_WIRE_RECT, 0);
    TIDE_REQUIRE(jumper != NULL);
    TIDE_CHECK(jumper->a.x == 1000.0f);
    const tide_draw_command *shot = nth(TIDE_DRAW_RECT, 0);
    TIDE_REQUIRE(shot != NULL);
    TIDE_CHECK(shot->a.y == 500.0f);
    // Time.t is a float like any other: a quarter of the way from one tick's to the next's
    const tide_draw_command *clock = nth(TIDE_DRAW_RECT, 1);
    TIDE_REQUIRE(clock != NULL);
    TIDE_CHECK(before.Time.t == 1.0f / 20.0f && now.Time.t == 2.0f * (1.0f / 20.0f));
    TIDE_CHECK(clock->a.x == tide_lerp_f(before.Time.t, now.Time.t, 0.25f));
    TIDE_CHECK(clock->a.x > before.Time.t && clock->a.x < now.Time.t && clock->a.y == 20.0f);

    frame(NULL, 0.25f); // No previous tick: as it is
    TIDE_CHECK(nth(TIDE_DRAW_CIRCLE, 0)->a.x == 8.0f);
    TIDE_CHECK(nth(TIDE_DRAW_RECT, 1)->a.x == now.Time.t);
    frame(&before, 1.0f);
    TIDE_CHECK(nth(TIDE_DRAW_CIRCLE, 0)->a.x == 8.0f);
    TIDE_CHECK(nth(TIDE_DRAW_RECT, 1)->a.x == now.Time.t);
}

// A snapshot shares the pages of what it's a snapshot of, and lets go of what
// the world it goes into had: the same world, ticking the same way.
TIDE_TEST(blend_snapshots_are_what_is_in_use)
{
    tide_world_init(&before, 1.0f / 20.0f);
    for (int i = 0; i < 5; i++) tide_world_tick(&before); // Further along: more entities
    tide_world_init(&now, 1.0f / 20.0f);
    tide_world_tick(&now);
    tide_world_copy(&before, &now);
    TIDE_CHECK(tide_world_entity_count(&before) == tide_world_entity_count(&now));
    TIDE_CHECK(tide_world_hash(&before) == tide_world_hash(&now));
    tide_world_tick(&before);
    tide_world_tick(&now);
    TIDE_CHECK(tide_world_hash(&before) == tide_world_hash(&now));
    TIDE_CHECK(tide_world_entity_count(&before) == tide_world_entity_count(&now));
}
