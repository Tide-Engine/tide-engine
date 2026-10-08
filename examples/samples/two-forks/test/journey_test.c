// Progression and the selection meatball use the match's commands/ticks,
// independently of the kitchen solver and the 24 concrete yard witnesses.
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "game.h"
#include "tide/net.h"
#include "tide/session.h"
#include "tide_test.h"

static tide_world journey;
static const int REGIONAL_LEVELS[4][6] = {
    {0, 1, 2, 11, 12, 13}, {3, 4, 8, 14, 15, 16},
    {5, 6, 9, 17, 18, 19}, {7, 10, 20, 21, 22, 23},
};

static int region_credit(int region, int count)
{
    int bits = 0;
    for (int i = 0; i < count; i++) bits |= 1 << REGIONAL_LEVELS[region][i];
    return bits;
}

static void begin_journey(int region)
{
    tide_world_free(&journey);
    tide_world_init(&journey, 1.0f / 60.0f);
    journey.Kitchen.level = REGIONAL_LEVELS[region][0];
    for (int r = 0; r < region; r++) journey.Kitchen.done |= region_credit(r, 6);
    tide_world_player_joined(&journey, tide_player_from_index(0));
    tide_world_player_joined(&journey, tide_player_from_index(1));
    tide_world_tick(&journey);
}

static bool ask_journey(uint32_t id, int sender, int value)
{
    uint8_t bytes[TIDE_COMMAND_BYTES];
    tide_bits bits = {bytes, sizeof bytes, 0, false};
    tide_bits_put(&bits, id, 16);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put_varint(&bits, tide_zigzag(value));
    uint32_t size = tide_bits_end(&bits);
    tide_player_id player = tide_player_from_index(sender);
    return tide_game_api.command(&journey, player, 0,
        tide_game_api.command_sequence(&journey, player, 0) + 1, bytes, size);
}

static void move_yard(int who, int way)
{
    const Controls keys = {.north = way == 0, .east = way == 1, .south = way == 2, .west = way == 3};
    tide_world_set_input(&journey, tide_player_from_index(who), keys);
    tide_world_tick(&journey);
    tide_world_set_input(&journey, tide_player_from_index(who), (Controls){0});
    for (int i = 0; i < 9; i++) tide_world_tick(&journey);
}

static void park(int ax, int ay, int bx, int by)
{
    journey.Kitchen.board.chefs.at[0] = (Chef){.at = {ax, ay}, .facing = 0};
    journey.Kitchen.board.chefs.at[1] = (Chef){.at = {bx, by}, .facing = 0};
    memset(&journey.Kitchen.grips, 0, sizeof journey.Kitchen.grips);
}

TIDE_TEST(two_forks_journey_requires_a_rolled_meatball_and_preserves_the_yard)
{
    begin_journey(0);
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_EnterKitchen, 0, 0));
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.travelling);
    // A ball goes into the middle-left pan; both chefs have to gather there.
    move_yard(0, 0);
    move_yard(1, 3);
    move_yard(1, 3);
    move_yard(1, 3);
    TIDE_CHECK(!journey.Kitchen.travelling && journey.Kitchen.level == 1);
    const Chef first = journey.Kitchen.campers.at[0], second = journey.Kitchen.campers.at[1];
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_BackToMap, 0, 0));
    tide_world_tick(&journey);
    TIDE_CHECK(!journey.Kitchen.travelling);
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_BackToMap, 1, 1));
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.travelling && journey.Kitchen.done == 0);
    TIDE_CHECK(journey.Kitchen.meatball.x == 2 && journey.Kitchen.meatball.y == 3);
    TIDE_CHECK(memcmp(&journey.Kitchen.board.chefs.at[0], &first, sizeof first) == 0);
    TIDE_CHECK(memcmp(&journey.Kitchen.board.chefs.at[1], &second, sizeof second) == 0);
    // Returning disarms this pan: idle ticks cannot immediately re-enter.
    for (int i = 0; i < 15; i++) tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.travelling && journey.Kitchen.selection == -1);
    tide_world_free(&journey);
}

TIDE_TEST(two_forks_journey_meatball_undo_and_reset_keep_cooking_credit)
{
    begin_journey(0);
    journey.Kitchen.done = region_credit(0, 4);
    journey.Kitchen.bridges = 1;
    move_yard(0, 1);
    move_yard(0, 1);
    TIDE_CHECK(journey.Kitchen.meatball.x == 5 && journey.Kitchen.moves == 2);
    // Blue's fork is a physical stopper for the next attempted roll.
    move_yard(0, 1);
    TIDE_CHECK(journey.Kitchen.meatball.x == 5 && journey.Kitchen.moves == 2);
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_Undo, 1, 0));
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.meatball.x == 4 && journey.Kitchen.moves == 1);
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_Restart, 0, 0));
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.travelling && journey.Kitchen.moves == 0);
    TIDE_CHECK(journey.Kitchen.done == region_credit(0, 4) && journey.Kitchen.bridges == 1);
    TIDE_CHECK(journey.Kitchen.meatball.x == 4 && journey.Kitchen.meatball.y == 3);
    TIDE_CHECK(journey.Kitchen.board.tiles.at[4 * 12 + 7] == Tile_Grass);
    tide_world_free(&journey);
}

TIDE_TEST(two_forks_journey_ice_carries_the_ball_and_waits_for_both_chefs)
{
    begin_journey(2);
    move_yard(0, 1);
    move_yard(0, 1);
    TIDE_CHECK(journey.Kitchen.meatball.x == 5); // Blue's fork stops the glide.
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_Restart, 0, 5));
    tide_world_tick(&journey);
    move_yard(1, 0); // Clear that fork.
    move_yard(0, 1);
    move_yard(0, 1);
    TIDE_CHECK(journey.Kitchen.meatball.x == 6 && journey.Kitchen.selection == 4);
    TIDE_CHECK(journey.Kitchen.travelling); // Red is still three tiles away.
    move_yard(0, 1);
    TIDE_CHECK(!journey.Kitchen.travelling && journey.Kitchen.level == 18);
    tide_world_free(&journey);
}

TIDE_TEST(two_forks_journey_routes_require_progress_and_gathering)
{
    begin_journey(0);
    journey.Kitchen.done = region_credit(0, 3);
    park(1, 2, 7, 2);
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.bridges == 0);
    journey.Kitchen.done |= 1 << 11;
    park(1, 2, 7, 3);
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.bridges == 0);
    park(1, 2, 7, 2);
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.bridges == 1);
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_Traverse, 1, 0));
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.routeRegion == 0); // Still too far from the gate.
    park(7, 3, 6, 4);
    const int done = journey.Kitchen.done;
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_Traverse, 1, 0));
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.routeRegion == 1 && journey.Kitchen.done == done);
    park(1, 3, 2, 5);
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_Traverse, 0, 1));
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.routeRegion == 0 && journey.Kitchen.done == done);
    // All six coastal kitchens earn a two-way ferry past the fields.
    journey.Kitchen.done = region_credit(0, 6);
    park(3, 3, 5, 3);
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_Traverse, 0, 2));
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.routeRegion == 2 && journey.Kitchen.done == region_credit(0, 6));
    park(3, 3, 5, 3);
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_Traverse, 1, 2));
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.routeRegion == 0);
    // The fields earn the same optional route to/from the crater.
    begin_journey(1);
    journey.Kitchen.done |= region_credit(1, 6);
    const int fields_done = journey.Kitchen.done;
    park(3, 3, 5, 3);
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_Traverse, 1, 2));
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.routeRegion == 3 && journey.Kitchen.done == fields_done);
    park(3, 3, 5, 3);
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_Traverse, 0, 2));
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.routeRegion == 1 && journey.Kitchen.done == fields_done);
    tide_world_free(&journey);
}

TIDE_TEST(two_forks_journey_meatball_and_progress_survive_snapshot_restore)
{
    begin_journey(0);
    journey.Kitchen.done = region_credit(0, 4);
    journey.Kitchen.bridges = 1;
    move_yard(0, 1);
    move_yard(0, 1);
    tide_world saved = {0};
    tide_world_copy(&saved, &journey);
    const uint64_t hash = tide_world_hash(&saved);
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_Goto, 0, 23));
    tide_world_tick(&journey);
    TIDE_CHECK(!journey.Kitchen.travelling && journey.Kitchen.routeRegion == 3);
    tide_world_copy(&journey, &saved);
    TIDE_CHECK(journey.Kitchen.travelling && journey.Kitchen.routeRegion == 0 && journey.Kitchen.bridges == 1);
    TIDE_CHECK(journey.Kitchen.meatball.x == 5 && tide_world_hash(&journey) == hash);
    tide_world_free(&saved);
    tide_world_free(&journey);
}
