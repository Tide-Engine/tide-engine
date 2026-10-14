#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tide/net.h"
#include "tide_test.h"

static tide_world world;
#define H world.Heist
static tide_player_id player(int seat) { return tide_player_from_index(seat); }
static bool send_on(tide_world *w, uint32_t id, int sender, int serial, int target)
{
    uint8_t bytes[TIDE_COMMAND_BYTES] = {0};
    tide_bits bits = {bytes, TIDE_COMMAND_BYTES, 0, false};
    tide_bits_put(&bits, id, 16);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put_varint(&bits, tide_zigzag(serial));
    if (id == TIDE_COMMAND_ID_Jump) tide_bits_put_varint(&bits, tide_zigzag(target));
    const uint32_t size = tide_bits_end(&bits);
    const bool accepted = tide_game_api.command(w, player(sender), 0,
        tide_game_api.command_sequence(w, player(sender), 0) + 1, bytes, size);
    tide_world_set_input(w, player(0), (Controls){0});
    tide_world_set_input(w, player(1), (Controls){0});
    tide_world_tick(w);
    return accepted;
}
static void start(int participants)
{
    tide_world_free(&world);
    tide_world_init(&world, 1.0f / 30.0f);
    for (int s = 0; s < participants; s++) tide_world_player_joined(&world, player(s));
    tide_world_tick(&world);
    tide_world_tick(&world);
    TIDE_CHECK(send_on(&world, TIDE_COMMAND_ID_BeginHeist, 0, H.serial, 0));
}
static void tick_on(tide_world *w, Controls a, Controls b)
{
    tide_world_set_input(w, player(0), a);
    tide_world_set_input(w, player(1), b);
    tide_world_tick(w);
}
static void tick(Controls input, int seat) { tick_on(&world, seat == 0 ? input : (Controls){0}, seat == 1 ? input : (Controls){0}); }
static void use(int seat) { tick((Controls){0}, seat); tick((Controls){.use = true}, seat); tick((Controls){0}, seat); }
static void special(int seat) { tick((Controls){0}, seat); tick((Controls){.special = true}, seat); tick((Controls){0}, seat); }
static void jump(int seat, int target)
{
    TIDE_CHECK(send_on(&world, TIDE_COMMAND_ID_Jump, seat, H.serial, target));
    if (H.possessed.at[seat] != target) fprintf(stderr, "jump failed: %d -> %d at %.2f %.2f\n", H.possessed.at[seat], target, H.machines.at[H.possessed.at[seat]].position.x, H.machines.at[H.possessed.at[seat]].position.y);
    TIDE_CHECK(H.possessed.at[seat] == target);
}
static void quiet(void) { H.guards.at[0].stun = H.guards.at[1].stun = 3600; }
static bool drive(int seat, float x, float y)
{
    for (int step = 0; step < 2500; step++) {
        const int body = H.possessed.at[seat];
        const tide_float2 at = H.machines.at[body].position;
        const float dx = x - at.x, dy = y - at.y;
        if (fabsf(dx) < 0.14f && fabsf(dy) < 0.14f) { tick((Controls){0}, seat); return true; }
        const tide_float2 move = fabsf(dx) >= 0.14f ? tide_f2(dx > 0 ? 1 : -1, 0) : tide_f2(0, dy > 0 ? 1 : -1);
        tick((Controls){.move = move}, seat);
        if (H.won) return true;
    }
    fprintf(stderr, "drive stuck: body %d at %.3f %.3f, wanted %.2f %.2f\n", H.possessed.at[seat],
        H.machines.at[H.possessed.at[seat]].position.x, H.machines.at[H.possessed.at[seat]].position.y, x, y);
    return false;
}

TIDE_TEST(hot_swap_commands_validate_sender_revision_range_power_and_occupancy)
{
    start(2); quiet();
    TIDE_CHECK(H.possessed.at[0] == 0 && H.possessed.at[1] == 1);
    send_on(&world, TIDE_COMMAND_ID_Jump, 0, H.serial, 1);
    TIDE_CHECK(H.possessed.at[0] == 0);
    send_on(&world, TIDE_COMMAND_ID_Jump, 0, H.serial - 1, 2);
    send_on(&world, TIDE_COMMAND_ID_Jump, 0, H.serial, 14);
    send_on(&world, TIDE_COMMAND_ID_Jump, 0, H.serial, -1);
    send_on(&world, TIDE_COMMAND_ID_Jump, 0, H.serial, INT32_MAX);
    TIDE_CHECK(H.possessed.at[0] == 0);
    H.machines.at[2].offline = 5;
    send_on(&world, TIDE_COMMAND_ID_Jump, 0, H.serial, 2);
    TIDE_CHECK(H.possessed.at[0] == 0);
    H.machines.at[2].offline = 0; jump(0, 2);
    send_on(&world, TIDE_COMMAND_ID_Jump, 1, H.serial, 2);
    TIDE_CHECK(H.possessed.at[1] == 1);
    tide_world_player_left(&world, player(1)); tide_world_tick(&world);
    TIDE_CHECK(!H.present.at[1]);
    send_on(&world, TIDE_COMMAND_ID_Restart, 1, H.serial, 0);
    TIDE_CHECK(H.possessed.at[0] == 2);
}
TIDE_TEST(hot_swap_late_join_never_takes_an_occupied_machine)
{
    start(1); jump(0, 1);
    tide_world_player_joined(&world, player(1)); tide_world_tick(&world);
    TIDE_CHECK(H.present.at[1] && H.possessed.at[0] != H.possessed.at[1]);
    tide_world_player_left(&world, player(0)); tide_world_tick(&world);
    TIDE_CHECK(!H.present.at[0] && H.present.at[1]);
    tide_world_player_joined(&world, player(0)); tide_world_tick(&world);
    TIDE_CHECK(H.present.at[0] && tide_player_equal(H.players.at[0], player(0)));
    TIDE_CHECK(H.possessed.at[0] != H.possessed.at[1]);
}
TIDE_TEST(hot_swap_wired_hops_cross_shutters_but_nearby_hops_need_sight)
{
    start(1); quiet(); H.possessed.at[0] = 4; jump(0, 5);
    H.machines.at[0].position = tide_f2(14, 18); H.machines.at[1].position = tide_f2(18, 18); H.possessed.at[0] = 0;
    send_on(&world, TIDE_COMMAND_ID_Jump, 0, H.serial, 1); TIDE_CHECK(H.possessed.at[0] == 0);
    H.doors.at[0] = true; H.pallets.at[1] = tide_f2(35, 33); jump(0, 1);
}
TIDE_TEST(hot_swap_shutters_latch_foam_expires_and_forklifts_lift_cleaners)
{
    start(2); quiet(); H.possessed.at[0] = 3; use(0); TIDE_CHECK(H.doors.at[0]);
    H.possessed.at[0] = 0; special(0); TIDE_CHECK(H.doors.at[0] && H.slicks.at[0].life > 17);
    H.possessed.at[0] = 2; H.machines.at[1].position = tide_f2(12, 27); special(0);
    TIDE_CHECK(H.machines.at[1].raised > 14 && H.lifts == 1);
    for (int i = 0; i < 600; i++) tick((Controls){0}, 0);
    TIDE_CHECK(H.slicks.at[0].life == 0 && H.machines.at[1].raised == 0);
}
TIDE_TEST(hot_swap_camera_scan_needs_aim_and_flash_interrupts_guards)
{
    start(1); quiet(); H.possessed.at[0] = 8;
    for (int i = 0; i < 100; i++) tick((Controls){.move = {0, -1}, .use = true}, 0);
    TIDE_CHECK(!H.manifest && H.scan.at[0] == 0);
    for (int i = 0; i < 95; i++) tick((Controls){.move = {1, 0}, .use = true}, 0);
    TIDE_CHECK(H.manifest);
    H.guards.at[0].position = tide_f2(28, 6); H.guards.at[0].stun = 0; special(0);
    TIDE_CHECK(H.guards.at[0].stun > 3.8f && H.machines.at[8].cooldown > 3.8f);
}
TIDE_TEST(hot_swap_pallet_power_and_service_bypass_are_independent_solutions)
{
    start(1); quiet(); H.manifest = true; H.possessed.at[0] = 6;
    H.machines.at[6].position = tide_f2(36.3f, 11); H.machines.at[6].facing = tide_f2(1, 0); H.machines.at[6].cargo = 0;
    use(0); TIDE_CHECK(H.machines.at[6].cargo == -1 && H.released && !H.bypass);
    start(1); quiet(); H.manifest = true; H.possessed.at[0] = 11; H.machines.at[11].position = tide_f2(49, 6);
    use(0); TIDE_CHECK(H.bypass && H.released);
}
TIDE_TEST(hot_swap_capture_preserves_loot_and_routes_and_reboots_the_body)
{
    start(1); H.possessed.at[0] = 7; H.safe.at[0] = 0;
    H.manifest = H.bypass = H.released = H.lockdown = true; H.doors.at[0] = true;
    H.machines.at[7].position = tide_f2(28, 16); H.machines.at[7].cargo = 5; H.carrier = 7;
    H.guards.at[0].position = tide_f2(28, 17.5f); H.guards.at[0].facing = tide_f2(0, -1); H.guards.at[0].notice = 1;
    H.guards.at[1].stun = 999; tick((Controls){0}, 0);
    TIDE_CHECK(H.catches == 1 && H.possessed.at[0] != 7 && H.carrier == -1);
    TIDE_CHECK(H.machines.at[7].offline > 7.9f && H.doors.at[0] && H.released);
    TIDE_CHECK(H.loot.x > 26 && H.loot.x < 29 && H.loot.y == 16);
    quiet(); for (int i = 0; i < 250; i++) tick((Controls){0}, 0);
    TIDE_CHECK(H.machines.at[7].offline == 0);
}
TIDE_TEST(hot_swap_complete_solo_heist_through_the_authored_duct_route)
{
    start(1); quiet(); // Guards have a separate threat test; authored geometry is unchanged.
    jump(0, 2); TIDE_REQUIRE(drive(0, 13, 25)); jump(0, 3); use(0); TIDE_CHECK(H.doors.at[0]);
    jump(0, 4); jump(0, 5); jump(0, 7);
    TIDE_REQUIRE(drive(0, 19, 13)); TIDE_REQUIRE(drive(0, 28, 13)); TIDE_REQUIRE(drive(0, 26.5f, 13)); TIDE_REQUIRE(drive(0, 26.5f, 4)); TIDE_REQUIRE(drive(0, 28, 4)); jump(0, 8);
    for (int i = 0; i < 95; i++) tick((Controls){.move = {1, 0}, .use = true}, 0);
    TIDE_REQUIRE(H.manifest); jump(0, 10); jump(0, 11); TIDE_REQUIRE(drive(0, 49, 6)); use(0); TIDE_REQUIRE(H.released);
    TIDE_REQUIRE(drive(0, 54, 6)); TIDE_REQUIRE(drive(0, 54, 18)); TIDE_REQUIRE(drive(0, 52, 18)); use(0);
    TIDE_REQUIRE(H.carrier == 11 && H.lockdown && !H.doors.at[0] && !H.doors.at[1]);
    TIDE_REQUIRE(drive(0, 54, 18)); TIDE_REQUIRE(drive(0, 54, 6)); TIDE_REQUIRE(drive(0, 26.5f, 6));
    TIDE_REQUIRE(drive(0, 26.5f, 13)); TIDE_REQUIRE(drive(0, 19, 13)); TIDE_REQUIRE(drive(0, 19, 6));
    TIDE_REQUIRE(drive(0, 14.5f, 6)); TIDE_REQUIRE(drive(0, 14.5f, 27)); TIDE_REQUIRE(drive(0, 5, 27));
    TIDE_CHECK(H.won && H.route == 1 && H.swaps >= 8);
}
TIDE_TEST(hot_swap_loading_shutter_escape_can_be_prepared_by_a_partner)
{
    start(2); quiet(); H.manifest = H.bypass = H.released = true;
    H.possessed.at[0] = 14; H.machines.at[14].position = tide_f2(52, 20); H.possessed.at[1] = 9;
    use(0); TIDE_REQUIRE(H.carrier == 14 && H.lockdown); use(1); TIDE_REQUIRE(H.doors.at[1]);
    TIDE_REQUIRE(drive(0, 54, 20)); TIDE_REQUIRE(drive(0, 54, 18)); TIDE_REQUIRE(drive(0, 44, 18));
    TIDE_REQUIRE(drive(0, 36, 18)); TIDE_REQUIRE(drive(0, 29.5f, 18));
    H.possessed.at[1] = 3; use(1); TIDE_REQUIRE(H.doors.at[0]);
    TIDE_REQUIRE(drive(0, 29.5f, 25)); TIDE_REQUIRE(drive(0, 19, 25)); TIDE_REQUIRE(drive(0, 21, 20));
    TIDE_REQUIRE(drive(0, 13, 20)); TIDE_REQUIRE(drive(0, 13, 25)); TIDE_REQUIRE(drive(0, 5, 25)); TIDE_REQUIRE(drive(0, 5, 27));
    TIDE_CHECK(H.won && H.route == 2);
}
TIDE_TEST(hot_swap_conveyor_escape_crosses_closed_shutters)
{
    start(1); quiet(); H.manifest = H.bypass = H.released = true; H.possessed.at[0] = 14;
    H.machines.at[14].position = tide_f2(52, 20); use(0); TIDE_REQUIRE(H.carrier == 14 && H.lockdown);
    TIDE_REQUIRE(drive(0, 54, 20)); TIDE_REQUIRE(drive(0, 54, 29.8f)); TIDE_REQUIRE(drive(0, 49, 29.8f));
    use(0); TIDE_REQUIRE(H.carrier == -1); jump(0, 13); special(0); TIDE_REQUIRE(H.belt);
    for (int i = 0; i < 600 && !H.won; i++) tick((Controls){0}, 0);
    TIDE_CHECK(H.won && H.route == 3 && !H.doors.at[0] && !H.doors.at[1]);
}
TIDE_TEST(hot_swap_solo_carrier_can_leave_cargo_reopen_both_shutters_and_return)
{
    start(1); quiet(); H.manifest = H.bypass = H.released = true;
    H.possessed.at[0] = 14; H.machines.at[14].position = tide_f2(52, 20); use(0);
    TIDE_REQUIRE(H.carrier == 14 && H.lockdown);
    TIDE_REQUIRE(drive(0, 54, 20)); TIDE_REQUIRE(drive(0, 54, 18)); TIDE_REQUIRE(drive(0, 49, 18));
    jump(0, 12); jump(0, 11);
    TIDE_REQUIRE(drive(0, 39, 6)); jump(0, 10); jump(0, 9); use(0);
    TIDE_REQUIRE(H.doors.at[1] && H.carrier == 14); // Vacant carrier still holds the case.
    jump(0, 10); jump(0, 11);
    TIDE_REQUIRE(drive(0, 44, 6)); TIDE_REQUIRE(drive(0, 44, 18)); TIDE_REQUIRE(drive(0, 44, 21)); TIDE_REQUIRE(drive(0, 46, 21)); jump(0, 14);
    TIDE_REQUIRE(drive(0, 36, 18)); TIDE_REQUIRE(drive(0, 29.5f, 18));
    TIDE_REQUIRE(drive(0, 29.5f, 25)); TIDE_REQUIRE(drive(0, 19, 25)); TIDE_REQUIRE(drive(0, 21, 20));
    jump(0, 5); jump(0, 3); use(0); TIDE_REQUIRE(H.doors.at[0]); jump(0, 5); jump(0, 14);
    TIDE_REQUIRE(drive(0, 13, 20)); TIDE_REQUIRE(drive(0, 13, 25)); TIDE_REQUIRE(drive(0, 5, 25)); TIDE_REQUIRE(drive(0, 5, 27));
    TIDE_CHECK(H.won && H.route == 2 && H.swaps >= 11);
}
TIDE_TEST(hot_swap_forklift_catwalk_shortcut_and_expiry_cannot_strand_a_cleaner)
{
    start(2); quiet(); H.possessed.at[0] = 6; H.possessed.at[1] = 7;
    H.machines.at[6].position = tide_f2(40, 14);
    H.machines.at[7].position = tide_f2(41.5f, 14);
    for (int i = 0; i < 30; i++) tick((Controls){.move = {1, 0}}, 1);
    TIDE_CHECK(H.machines.at[7].position.x < 42);
    special(0);
    TIDE_REQUIRE(H.machines.at[7].raised > 14);
    for (int i = 0; i < 30; i++) tick((Controls){.move = {1, 0}}, 1);
    TIDE_CHECK(H.machines.at[7].position.x > 43.5f);
    H.machines.at[7].position = tide_f2(42.2f, 14);
    H.machines.at[7].raised = 0.02f;
    tick((Controls){0}, 1);
    TIDE_CHECK(H.machines.at[7].position.x < 42);
}
TIDE_TEST(hot_swap_snapshot_packing_and_replayed_inputs_agree)
{
    start(2); tide_world saved = {0}, packed = {0}; tide_world_copy(&saved, &world);
    const uint32_t size = tide_world_pack(&world, NULL, 0); uint8_t *bytes = malloc(size); TIDE_REQUIRE(bytes != NULL);
    TIDE_CHECK(tide_world_pack(&world, bytes, size) == size); TIDE_REQUIRE(tide_world_unpack(&packed, bytes, size)); free(bytes);
    for (int i = 0; i < 600; i++) {
        const Controls a = {.move = {i % 120 < 60 ? 1 : -1, 0}, .special = i % 80 < 5};
        const Controls b = {.move = {0, i % 120 < 60 ? -1 : 1}, .special = i % 93 < 6};
        tick_on(&world, a, b); tick_on(&saved, a, b); tick_on(&packed, a, b);
        TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&saved)); TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&packed));
    }
    printf("HOT SWAP deterministic replay: %016llx\n", (unsigned long long)tide_world_hash(&world));
    TIDE_CHECK(tide_world_hash(&world) == UINT64_C(0x12f0f9acbfa1453c));
    tide_world_free(&saved); tide_world_free(&packed);
}

TIDE_TEST(hot_swap_restart_keeps_players_and_rejects_old_heist_commands)
{
    start(2); quiet(); H.manifest = H.bypass = H.released = H.lockdown = H.won = true;
    H.machines.at[0].cargo = 5; H.carrier = 0; H.swaps = 7;
    const int old = H.serial;
    TIDE_REQUIRE(send_on(&world, TIDE_COMMAND_ID_Restart, 1, old, 0));
    TIDE_CHECK(H.started && H.serial == old + 1 && !H.won && !H.released && H.swaps == 0);
    TIDE_CHECK(H.present.at[0] && H.present.at[1] && H.carrier == -1 && H.machines.at[0].cargo == -1);
    TIDE_CHECK(H.possessed.at[0] == 0 && H.possessed.at[1] == 1);
    send_on(&world, TIDE_COMMAND_ID_Jump, 0, old, 2);
    TIDE_CHECK(H.possessed.at[0] == 0);
    jump(0, 2);
}
