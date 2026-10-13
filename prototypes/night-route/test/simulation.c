#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "night_route_rules.h"
#include "tide/net.h"
#include "tide_test.h"
#define CHECK_EQ(a, b) TIDE_CHECK((a) == (b))

static tide_world world;
static tide_player_id player(int index) { return tide_player_from_index(index); }
static void drive(float x, float y, bool brace, bool handoff)
{
    CourierInput input = {0};
    input.move = (tide_float2){x, y};
    input.brace = brace;
    input.handoff = handoff;
    tide_world_set_server_input(&world, input);
    tide_world_set_input(&world, player(0), input);
}
static void ticks(int count)
{
    for (int i = 0; i < count; i++) tide_world_tick(&world);
}
static void start(void)
{
    tide_world_free(&world);
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_player_joined(&world, player(0));
    drive(0, 0, false, false);
    ticks(2);
}
static bool send_as(uint32_t id, int sender, int serial, int value)
{
    uint8_t bytes[TIDE_COMMAND_BYTES] = {0};
    tide_bits bits = {bytes, sizeof bytes, 0, false};
    tide_bits_put(&bits, id, 16);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put_varint(&bits, tide_zigzag(serial));
    if (id != TIDE_COMMAND_ID_ReturnToDepot) tide_bits_put_varint(&bits, tide_zigzag(value));
    const uint32_t size = tide_bits_end(&bits);
    const bool result = tide_game_api.command(&world, player(sender), 0,
        tide_game_api.command_sequence(&world, player(sender), 0) + 1, bytes, size);
    ticks(1);
    return result;
}
static void depart(int mask)
{
    TIDE_CHECK(send_as(TIDE_COMMAND_ID_Depart, 0, world.Night.serial, mask));
    CHECK_EQ(world.Night.phase, 1);
}
static void place(float x, float y)
{
    world.Night.position = (tide_float2){x, y};
}
static void aim(float x, float y)
{
    world.Night.facing = (tide_float2){x, y};
}

TIDE_TEST(night_route_manifest_and_authorization)
{
    start();
    CHECK_EQ(world.Night.phase, 0);
    CHECK_EQ(world.Night.parcels.at[0].destination, 0);
    CHECK_EQ(world.Night.parcels.at[3].destination, 3);
    ticks(600);
    TIDE_CHECK(world.Night.left == 180.0f);
    const int serial = world.Night.serial;
    TIDE_CHECK(send_as(TIDE_COMMAND_ID_Depart, 0, serial, 15));
    CHECK_EQ(world.Night.phase, 0); // four cannot fit
    TIDE_CHECK(send_as(TIDE_COMMAND_ID_Depart, 0, serial - 1, 1));
    CHECK_EQ(world.Night.phase, 0);
    TIDE_CHECK(send_as(TIDE_COMMAND_ID_Depart, 0, serial, 0));
    CHECK_EQ(world.Night.phase, 0);
    depart(7);
    TIDE_CHECK(world.Night.parcels.at[0].carried && world.Night.parcels.at[1].carried && world.Night.parcels.at[2].carried);
    TIDE_CHECK(!world.Night.parcels.at[3].carried);
}
TIDE_TEST(night_route_mothbox_light_and_dark_budget)
{
    start(); depart(1);
    place(0, 0); // the unlit middle cut-through
    ticks(180);
    TIDE_CHECK(world.Night.parcels.at[0].strain > 53 && world.Night.parcels.at[0].strain < 55);
    TIDE_CHECK(!world.Night.lit);
    TIDE_CHECK(world.Night.parcels.at[0].breaking);
    place(-15, -9);
    ticks(180);
    TIDE_CHECK(world.Night.lit);
    TIDE_CHECK(world.Night.parcels.at[0].strain > 29 && world.Night.parcels.at[0].strain < 31);
    place(0, 0); ticks(300);
    TIDE_CHECK(world.Night.parcels.at[0].spoiled);
    CHECK_EQ(world.Night.spoiled, 1);
    CHECK_EQ(world.Night.earnings, -10);
    ticks(300);
    CHECK_EQ(world.Night.spoiled, 1); // no repeated penalty
}
TIDE_TEST(night_route_restless_uses_actual_motion_including_walls)
{
    start(); depart(2);
    drive(-1, 0, false, false); // walk into the west boundary
    ticks(300);
    TIDE_CHECK(world.Night.parcels.at[1].spoiled);
    TIDE_CHECK(world.Night.violations.at[1] > 4);
    start(); depart(2);
    drive(0, 1, false, false); ticks(180);
    TIDE_CHECK(world.Night.speed > 4);
    TIDE_CHECK(world.Night.parcels.at[1].strain == 0);
}
TIDE_TEST(night_route_northbound_brace_trades_pace_for_strain)
{
    start(); depart(4);
    drive(1, 0, false, false); ticks(60);
    TIDE_CHECK(world.Night.parcels.at[2].strain > 14);
    TIDE_CHECK(world.Night.speed > 4);
    const float east = world.Night.position.x;
    drive(1, 0, true, false); ticks(60);
    TIDE_CHECK(world.Night.speed > 2.3f && world.Night.speed < 2.5f);
    TIDE_CHECK(fabsf(world.Night.position.x - east - 2.4f) < 0.02f);
    TIDE_CHECK(world.Night.facing.y == 1);
    TIDE_CHECK(world.Night.parcels.at[2].strain < 8);
}
TIDE_TEST(night_route_echo_counts_reentry_and_same_street_uturn)
{
    start(); depart(8);
    drive(1, 0, false, false); ticks(80);
    TIDE_CHECK(world.Night.visited.at[6]);
    TIDE_CHECK(!world.Night.repeated);
    drive(-1, 0, false, false); ticks(20);
    TIDE_CHECK(world.Night.repeated);
    TIDE_CHECK(world.Night.parcels.at[3].strain > 5);
    place(-5, -9); ticks(1); // neutral junction
    CHECK_EQ(world.Night.edge, -1);
    place(0, -9); drive(1, 0, false, false); ticks(1);
    TIDE_CHECK(world.Night.visited.at[7]);
    TIDE_CHECK(!world.Night.repeated);
    place(-10, -9); ticks(1);
    TIDE_CHECK(world.Night.repeated);
}
TIDE_TEST(night_route_delivery_releases_rule_and_pays_bundle)
{
    start(); depart(7);
    world.Night.parcels.at[0].strain = 99.9f;
    place(-15, 9);
    aim(1, 0);
    drive(0, 0, false, true); ticks(1);
    TIDE_CHECK(world.Night.parcels.at[0].delivered);
    TIDE_CHECK(!world.Night.parcels.at[0].carried);
    const int base = world.Night.parcels.at[0].value;
    CHECK_EQ(world.Night.earnings, base);
    CHECK_EQ(world.Night.pendingBonus, base * 70 / 100);
    CHECK_EQ(world.Night.delivered, 1);
    const int earned = world.Night.earnings;
    ticks(1);
    CHECK_EQ(world.Night.earnings, earned); // held handoff cannot pay twice
    drive(0, 0, false, false);
    place(0, 0); ticks(30);
    CHECK_EQ(world.Night.spoiled, 0); // delivered Mothbox ignores darkness
    TIDE_CHECK(world.Night.parcels.at[1].strain > 0 && world.Night.parcels.at[2].strain > 0);
}
TIDE_TEST(night_route_return_clears_echo_restock_and_abandonment)
{
    start(); depart(9);
    world.Night.visited.at[6] = true;
    const int serial = world.Night.serial;
    place(0, 0);
    TIDE_CHECK(send_as(TIDE_COMMAND_ID_ReturnToDepot, 0, serial, 0));
    CHECK_EQ(world.Night.phase, 1); // cannot unload remotely
    place(-15, -9);
    TIDE_CHECK(send_as(TIDE_COMMAND_ID_ReturnToDepot, 0, serial, 0));
    CHECK_EQ(world.Night.phase, 0);
    CHECK_EQ(world.Night.spoiled, 2);
    CHECK_EQ(world.Night.earnings, -20);
    CHECK_EQ(world.Night.trip, 1);
    CHECK_EQ(world.Night.parcels.at[0].destination, 1);
    TIDE_CHECK(!world.Night.visited.at[6]);
    const float left = world.Night.left;
    ticks(60);
    TIDE_CHECK(world.Night.left < left - 0.99f); // depot planning after departure costs time
}
TIDE_TEST(night_route_dawn_and_replay_seed)
{
    start(); depart(4);
    world.Night.left = 0.01f;
    ticks(1);
    CHECK_EQ(world.Night.phase, 2);
    CHECK_EQ(world.Night.spoiled, 1);
    const int serial = world.Night.serial;
    TIDE_CHECK(send_as(TIDE_COMMAND_ID_Shift, 0, serial, 2));
    CHECK_EQ(world.Night.phase, 0);
    TIDE_CHECK(!world.Night.started);
    CHECK_EQ(world.Night.earnings, 0);
    CHECK_EQ(world.Night.parcels.at[0].destination, 1);
    TIDE_CHECK(world.Night.left == 180);
    TIDE_CHECK(send_as(TIDE_COMMAND_ID_Shift, 0, serial, 3));
    CHECK_EQ(world.Night.seed, 2); // stale actions cannot change the new shift
}
TIDE_TEST(night_route_bundle_bonus_requires_all_deliveries)
{
    start(); depart(7);
    const int first = world.Night.parcels.at[0].value;
    const int second = world.Night.parcels.at[1].value;
    const int third = world.Night.parcels.at[2].value;
    drive(0, 0, true, true);
    place(-15, 9); ticks(1);
    place(15, 9); ticks(1);
    TIDE_CHECK(world.Night.pendingBonus > 0);
    CHECK_EQ(world.Night.bonuses, 0);
    place(15, 0); ticks(1);
    const int bonus = first * 70 / 100 + second * 70 / 100 + third * 70 / 100;
    CHECK_EQ(world.Night.bonuses, bonus);
    CHECK_EQ(world.Night.earnings, first + second + third + bonus);
    CHECK_EQ(world.Night.pendingBonus, 0);
    start(); depart(7);
    drive(0, 0, true, true); place(-15, 9); ticks(1);
    world.Night.parcels.at[1].strain = 99.9f;
    drive(0, 0, true, false); ticks(1);
    TIDE_CHECK(world.Night.tripFailed);
    CHECK_EQ(world.Night.pendingBonus, 0);
    drive(0, 0, true, true); place(15, 0); ticks(1);
    CHECK_EQ(world.Night.bonuses, 0);
    TIDE_CHECK(world.Night.parcels.at[2].delivered);
}
static void walk_to(float x, float y, bool brace)
{
    for (int limit = 0; limit < 3000; limit++) {
        const float dx = x - world.Night.position.x;
        const float dy = y - world.Night.position.y;
        if (fabsf(dx) < 0.10f && fabsf(dy) < 0.10f) return;
        drive(fabsf(dx) < 0.10f ? 0 : dx > 0 ? 1 : -1,
            fabsf(dy) < 0.10f ? 0 : dy > 0 ? 1 : -1, brace, false);
        ticks(1);
    }
    TIDE_CHECK(false);
}
TIDE_TEST(night_route_every_parcel_can_reach_every_address)
{
    for (int seed = 1; seed <= 4; seed++) for (int kind = 0; kind < 4; kind++) {
        start();
        TIDE_CHECK(send_as(TIDE_COMMAND_ID_Shift, 0, world.Night.serial, seed));
        depart(1 << kind);
        const int address = world.Night.parcels.at[kind].destination;
        const bool brace = kind == 2;
        if (address == 0 || address == 1) walk_to(-15, 9, brace);
        if (address == 1) walk_to(15, 9, brace);
        if (address == 2) { walk_to(-15, 0, brace); walk_to(15, 0, brace); }
        if (address == 3) walk_to(5, -9, brace);
        TIDE_CHECK(!world.Night.parcels.at[kind].spoiled);
        drive(0, 0, brace, true); ticks(1);
        TIDE_CHECK(world.Night.parcels.at[kind].delivered);
        CHECK_EQ(world.Night.delivered, 1);
        TIDE_CHECK(world.Night.earnings > 0);
    }
}
TIDE_TEST(night_route_three_rule_bundle_succeeds_with_ordered_handoffs)
{
    start(); depart(7);
    walk_to(-15, 9, true);
    drive(0, 0, true, true); ticks(1); // shed Mothbox before taking a dark shortcut
    TIDE_CHECK(world.Night.parcels.at[0].delivered);
    walk_to(15, 9, true);
    drive(0, 0, true, true); ticks(1);
    TIDE_CHECK(world.Night.parcels.at[1].delivered);
    walk_to(15, 0, true);
    drive(0, 0, true, true); ticks(1);
    CHECK_EQ(world.Night.delivered, 3);
    CHECK_EQ(world.Night.spoiled, 0);
    TIDE_CHECK(world.Night.bonuses > 0);
    TIDE_CHECK(world.Night.violations.at[2] == 0);
}
TIDE_TEST(night_route_snapshot_pack_and_replay)
{
    start(); depart(13);
    drive(1, 0, true, false); ticks(100);
    tide_world snapshot = {0}, packed = {0};
    tide_world_copy(&snapshot, &world);
    const size_t capacity = 1024 * 1024;
    uint8_t *bytes = malloc(capacity);
    TIDE_CHECK(bytes != NULL);
    const uint32_t size = tide_world_pack(&world, bytes, (uint32_t)capacity);
    TIDE_CHECK(size > 0);
    TIDE_CHECK(tide_world_unpack(&packed, bytes, size));
    TIDE_CHECK(tide_world_hash(&packed) == tide_world_hash(&snapshot));
    ticks(80);
    const uint64_t expected = tide_world_hash(&world);
    tide_world_copy(&world, &snapshot);
    ticks(80);
    TIDE_CHECK(tide_world_hash(&world) == expected);
    free(bytes);
    tide_world_free(&packed);
    tide_world_free(&snapshot);
    tide_world_free(&world);
}

// Exercise the real view -> GUI ownership -> Sample boundary, in the same
// order as the standard host. Direct simulation inputs cannot catch a HUD
// accidentally consuming WASD, arrows, Shift or the handoff key.
static tide_local ui;
static tide_gui gui;
static tide_draw_list draw;
static tide_devices devices;
static float ui_width, ui_height;
static void ui_frame(void)
{
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, tide_f2(ui_width, ui_height));
    tide_local_frame_time(&ui, 1.0f / 60.0f);
    tide_pointer_poll(&devices, !devices.touchscreen.primaryTouch.press.held);
    tide_gui_begin(&gui, &devices, tide_f2(ui_width, ui_height), NULL);
    tide_frame(&world, NULL, 1, &world, NULL, 1, &ui, &draw, &gui);
    tide_gui_end(&gui, &draw);
}
static void ui_start(float width, float height)
{
    tide_local_free(&ui);
    memset(&gui, 0, sizeof gui);
    tide_draw_free(&draw);
    memset(&devices, 0, sizeof devices);
    devices.mouse.position = tide_f2(-1, -1);
    ui_width = width; ui_height = height;
    tide_local_init(&ui);
    tide_local_set_session(&ui, TIDE_SESSION_CONNECTED, player(0), 0, true, false, "");
    ui_frame();
}
static void ui_tick(void)
{
    tide_touches_poll(&devices.touchscreen);
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    const CourierInput input = tide_input_sample(&sampled, &ui);
    tide_world_set_server_input(&world, input);
    tide_world_set_input(&world, player(0), input);
    tide_devices_consume(&devices);
    uint32_t size = 0;
    const uint8_t *bytes;
    while ((bytes = tide_local_command(&ui, 0, &size)) != NULL) {
        TIDE_CHECK(tide_game_api.command(&world, player(0), 0,
            tide_game_api.command_sequence(&world, player(0), 0) + 1, bytes, size));
        tide_local_command_sent(&ui, 0);
    }
    ticks(1);
    ui_frame();
}
static void ui_click(float x, float y)
{
    devices.mouse.position = tide_f2(x, ui_height - y);
    ui_frame();
    tide_button_set(&devices.mouse.left, true); ui_tick();
    tide_button_set(&devices.mouse.left, false); ui_tick();
    ui_tick();
}
static bool drawn(const char *text)
{
    for (uint32_t i = 0; i < draw.count; i++)
        if (draw.commands[i].kind == TIDE_DRAW_TEXT && strcmp(draw.text + draw.commands[i].text, text) == 0) return true;
    return false;
}
static void ui_cleanup(void)
{
    tide_local_free(&ui);
    memset(&gui, 0, sizeof gui);
    tide_draw_free(&draw);
}
TIDE_TEST(night_route_keyboard_hud_selection_motion_handoff_and_debrief)
{
    start(); ui_start(1280, 800);
    ui_click(1100, 315); // add Restless to Mothbox
    TIDE_CHECK(ui.Desk.mask == 3);
    ui_click(1100, 650); // load through the actual GUI/command queue
    CHECK_EQ(world.Night.phase, 1);
    CHECK_EQ(world.Night.bundle, 2);
    tide_button_set(&devices.keyboard.w, true); ui_frame();
    for (int i = 0; i < 120; i++) ui_tick();
    TIDE_CHECK(world.Night.position.y > -1);
    TIDE_CHECK(world.Night.parcels.at[1].strain < 1);
    tide_button_set(&devices.keyboard.w, false);
    tide_button_set(&devices.keyboard.rightArrow, true);
    tide_button_set(&devices.keyboard.leftShift, true);
    ui_frame(); ui_tick();
    TIDE_CHECK(world.Night.facing.y == 1);
    TIDE_CHECK(world.Night.speed < 2.5f);
    tide_button_set(&devices.keyboard.rightArrow, false);
    tide_button_set(&devices.keyboard.leftShift, false);
    place(-15, 9);
    tide_button_set(&devices.keyboard.e, true); ui_frame(); ui_tick();
    TIDE_CHECK(world.Night.parcels.at[0].delivered);
    TIDE_CHECK(drawn("DELIVERED"));
    tide_button_set(&devices.keyboard.e, false); ui_frame();
    world.Night.left = 0.01f; ui_tick();
    TIDE_CHECK(drawn("THE TOWN WAKES"));
    ui_click(500, 540); // retry the same shift
    CHECK_EQ(world.Night.phase, 0);
    CHECK_EQ(world.Night.seed, 1);
    TIDE_CHECK(!world.Night.started);
    ui_cleanup();
}
TIDE_TEST(night_route_touch_pad_north_toggle_and_handoff)
{
    start(); ui_start(390, 844);
    ui_click(100, 555); // Northbound: actual compact card position
    TIDE_CHECK(ui.Desk.mask == 5);
    ui_click(190, 645);
    CHECK_EQ(world.Night.phase, 1);
    ui_click(310, 750); // hold-north toggle
    TIDE_CHECK(ui.Desk.brace);
    tide_touch_event(&devices.touchscreen, TIDE_TOUCH_BEGAN, 1, tide_f2(65, 844 - 722));
    ui_frame();
    for (int i = 0; i < 30; i++) ui_tick();
    TIDE_CHECK(world.Night.position.y > -7);
    TIDE_CHECK(world.Night.facing.y == 1);
    tide_touch_event(&devices.touchscreen, TIDE_TOUCH_ENDED, 1, tide_f2(65, 844 - 722));
    ui_frame(); ui_tick();
    place(-15, 9); ui_frame();
    ui_click(190, 645); // compact handoff button
    TIDE_CHECK(world.Night.parcels.at[0].delivered);
    ui_cleanup();
}
