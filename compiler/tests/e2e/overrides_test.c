#include <string.h>

#include "game.h"
#include "tide_test.h"

// virtual and override (overrides/game.tide with package Expansion): each
// override runs in the place of what it replaced, and what was replaced, which
// counts itself in `replaced`, never does.

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

static const tide_entity body = {1u, 1u};
static const tide_entity unit = {2u, 1u};

static void start(void)
{
    tide_world_init(&world, 1.0f);
    tide_local_init(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, false, false, "");
}

static void frame(void)
{
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(640, 480), NULL);
    tide_frame(&world, NULL, 1, &world, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

TIDE_TEST(overrides_systems_run_where_what_they_replace_did)
{
    start();
    tide_world_tick(&world);
    // One, Two's override (7), Three, Four's override (9), which keeps Four's [After(Three)], and Five, which
    // names Two. The package's files compile first: an override where it's written would have run first
    TIDE_CHECK(world.Combat_Log.order == 17395);
    // The package's P1, the game's override of P2 (8), P3
    TIDE_CHECK(world.Expansion_Trace.order == 183);
    TIDE_CHECK(world.Combat_Log.replaced == 0 && world.Expansion_Trace.replaced == 0);
    // A per-entity system's override runs for its entities
    TIDE_CHECK(tide_get_Combat_Body(&world, body).x == 2.0f);
    // A hook with nothing in it, which the game filled in
    TIDE_CHECK(world.Expansion_Trace.hooked == 1);
}

TIDE_TEST(overrides_functions_and_methods_are_what_callers_call)
{
    start();
    tide_world_tick(&world);
    TIDE_CHECK(world.Combat_Log.damage == 108.0f);  // 4 * 2 + 100, not 8
    TIDE_CHECK(world.Combat_Log.weak == 112.0f);    // The override's default, 3: 4 * 3 + 100
    TIDE_CHECK(world.Combat_Log.leveled == 30.0f);  // The Damage that shares the name, which nothing replaced
    TIDE_CHECK(world.Expansion_Trace.bonus == 5);   // The package's Bonus, which the game replaced
    const Combat_Unit u = tide_get_Combat_Unit(&world, unit);
    TIDE_CHECK(u.reach == 7); // A component's method
    // Strike, which isn't virtual, calls the overrides: Hurt takes twice the 30, and 40 is dead to IsDead
    TIDE_CHECK(world.Combat_Log.dead == 1);
    // ...then the handler's override, at the tick's end: 40 - 5 * 3
    TIDE_CHECK(u.stats.health == 25.0f);
    TIDE_CHECK(world.Combat_Log.replaced == 0);
}

TIDE_TEST(overrides_a_task_is_the_overrides)
{
    start();
    TIDE_CHECK(world.tide_tasks_function_Combat_Later.count == 1); // Started as Main loaded
    tide_world_tick(&world);
    tide_world_tick(&world);
    TIDE_CHECK(world.Combat_Log.later == 0); // It waits two ticks, where what it replaced waited one
    tide_world_tick(&world);
    TIDE_CHECK(world.Combat_Log.later == 1 && world.tide_tasks_function_Combat_Later.count == 0);
    TIDE_CHECK(world.Combat_Log.replaced == 0);
}

TIDE_TEST(overrides_a_commands_handler)
{
    start();
    local.Combat_Hud.buy = true;
    frame();
    uint8_t bytes[TIDE_COMMAND_BYTES];
    const uint32_t size = tide_local_command(&local, bytes, TIDE_COMMAND_BYTES);
    TIDE_REQUIRE(size > 0 && tide_game_api.check_command(bytes, size));
    TIDE_REQUIRE(tide_game_api.command(&world, tide_player_from_index(0), 1, bytes, size));
    tide_world_tick(&world);
    TIDE_CHECK(world.Combat_Log.bought == 103);
    TIDE_CHECK(world.Combat_Log.replaced == 0);
}

TIDE_TEST(overrides_views_and_local_handlers)
{
    start();
    tide_event_log events = {0};
    tide_world_tick_events(&world, NULL, &events);
    tide_local_events(&local, &events);
    frame();
    TIDE_CHECK(local.Combat_Hud.shown == 1);  // The view's override, for the one Body
    TIDE_CHECK(local.Combat_Hud.heard == 50); // The local handler's, of the tick's Hit: 5 * 10
    TIDE_CHECK(local.Combat_Hud.replaced == 0);
    // What the view's override draws: a circle of radius 2
    bool drew = false;
    for (uint32_t i = 0; i < draw.count; i++) {
        if (draw.commands[i].kind == TIDE_DRAW_CIRCLE) drew |= draw.commands[i].b.x == 2.0f;
    }
    TIDE_CHECK(drew);
    tide_event_log_free(&events);
}

TIDE_TEST(overrides_frags_and_verts)
{
    start();
    tide_world_tick(&world);
    frame();
    const tide_shader *shader = NULL;
    for (uint32_t i = 0; i < draw.count && !shader; i++) {
        if (draw.commands[i].kind == TIDE_DRAW_SHADED) shader = draw.runs[draw.commands[i].shaded.run].shader;
    }
    TIDE_REQUIRE(shader != NULL);
    // The mesh names Wave and Shade, and draws through what's built under those names
    TIDE_CHECK(strcmp(shader->name, "Wave and Shade on Corner") == 0);
    TIDE_CHECK(strstr(shader->glsl_vertex, "0.25") && !strstr(shader->glsl_vertex, "0.75"));
    TIDE_CHECK(strstr(shader->glsl_fragment, "0.125") && !strstr(shader->glsl_fragment, "0.875"));
#ifdef __wasm__ // A game has its WGSL where WebGPU is
    TIDE_CHECK(strstr(shader->wgsl, "0.125") && !strstr(shader->wgsl, "0.875"));
#endif
}

TIDE_TEST(overrides_replaced_code_is_not_in_the_build)
{
    start();
    for (int i = 0; i < 4; i++) tide_world_tick(&world);
    // Two, as written, spawned a Ghost each tick: nothing has one, and only the two Setup spawned are there
    // beside Main's own entity
    TIDE_CHECK(tide_world_entity_count(&world) == 3);
    TIDE_CHECK(world.Combat_Log.replaced == 0 && world.Expansion_Trace.replaced == 0);
}
