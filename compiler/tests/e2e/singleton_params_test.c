#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_world replay;
static tide_world snapshot;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

TIDE_TEST(singleton_params_mutate_explicit_values_and_keep_readonly_snapshots)
{
    tide_world_init(&world, 0.25f);
    TIDE_CHECK(world.Duel.points == 10 && world.Duel.calls == 0); // Handler's whole-value assignment.
    tide_world_tick(&world);
    TIDE_CHECK(world.Trace.before == 10 && world.Trace.after == 14);
    TIDE_CHECK(world.Duel.points == 14 && world.Duel.calls == 2);
    TIDE_CHECK(world.Trace.methodCalls == 1 && world.Trace.dt == 0.25f);
    TIDE_CHECK(world.Ledger.before == 0 && tide_list_count(world.Ledger.marks) == 2);
    TIDE_CHECK(world.Ledger.actionBefore == 1 && world.Ledger.actionAfter == 2);
    TIDE_CHECK(world.Ledger.beforeFirst == 0 && world.Ledger.actionFirst == 1 && world.Ledger.callbacks == 1);
    tide_world_tick(&world);
    TIDE_CHECK(world.Trace.before == 14 && world.Trace.after == 18);
    TIDE_CHECK(world.Ledger.before == 2 && tide_list_count(world.Ledger.marks) == 4);
    TIDE_CHECK(world.Ledger.actionBefore == 3 && world.Ledger.actionAfter == 4);
    TIDE_CHECK(world.Ledger.beforeFirst == 111 && world.Ledger.actionFirst == 121 && world.Ledger.callbacks == 2);
    tide_world_free(&world);
}

TIDE_TEST(singleton_params_local_helpers_views_and_readonly_sample)
{
    tide_world_init(&world, 0.25f);
    tide_local_init(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, (tide_player_id){1}, 20, true, true, "K7QF2M");
    tide_world_tick(&world);
    const uint64_t before = tide_world_hash(&world);
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Display.points == 14 && local.Display.copyPoints == 114);
    TIDE_CHECK(local.Display.frames == 1 && local.Display.copyFrames == 2);
    TIDE_CHECK(local.Display.methodCalls == 1 && local.Display.server);
    TIDE_CHECK(local.Display.paint == 19 && local.Duel_local.paint == 19 && local.Duel_local.marks == 8);
    TIDE_CHECK(world.Duel.points == 14 && tide_world_hash(&world) == before);
    const tide_devices devices = {0};
    const tide_input input = tide_input_sample(&devices, &local);
    TIDE_CHECK(input.frames == 1 && local.Display.frames == 1); // Sample's helper cannot mutate the source.

    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Display.frames == 2 && local.Duel_local.paint == 33 && local.Duel_local.marks == 9);
    world.Reset.now = true;
    tide_world_tick(&world); // Whole-value replacement changes match fields and retains the local sidecar.
    TIDE_CHECK(world.Duel.points == 14 && world.Duel.calls == 2);
    TIDE_CHECK(local.Duel_local.paint == 33 && local.Duel_local.marks == 9);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
}

TIDE_TEST(singleton_params_snapshots_and_replay_have_the_same_match)
{
    tide_world_init(&world, 0.25f);
    tide_world_init(&replay, 0.25f);
    for (int tick = 0; tick < 64; tick++) {
        tide_world_copy(&snapshot, &world);
        const uint64_t before = tide_world_hash(&snapshot);
        const int32_t points = snapshot.Duel.points;
        tide_world_tick(&world);
        tide_world_tick(&replay);
        TIDE_REQUIRE(tide_world_hash(&world) == tide_world_hash(&replay));
        TIDE_REQUIRE(tide_world_hash(&snapshot) == before && snapshot.Duel.points == points);
        TIDE_REQUIRE(world.Trace.before == points && world.Trace.after == points + 4);
    }
    // The same semantic result is pinned on native and WASI, including alias copies.
    TIDE_CHECK(world.Duel.points == 266 && world.Duel.calls == 128);
    TIDE_CHECK(world.Trace.before == 262 && world.Trace.after == 266);
    TIDE_CHECK(world.Ledger.before == 126 && tide_list_count(world.Ledger.marks) == 128);
    TIDE_CHECK(world.Ledger.actionBefore == 127 && world.Ledger.actionAfter == 128);
    TIDE_CHECK(world.Ledger.beforeFirst == 7551 && world.Ledger.actionFirst == 7561 && world.Ledger.callbacks == 64);
    tide_text_use(&world.heap, NULL);
    TIDE_CHECK(*(const int32_t *)tide_list_at(world.Ledger.marks, 0, sizeof(int32_t)) == 7671);
    TIDE_CHECK(*(const int32_t *)tide_list_at(world.Ledger.marks, 127, sizeof(int32_t)) == 128);
    TIDE_CHECK(tide_text_read(&world.heap, world.Ledger.title).bytes == 133); // "start" and two marks per tick.
    tide_world_free(&snapshot);
    tide_world_free(&replay);
    tide_world_free(&world);
}
