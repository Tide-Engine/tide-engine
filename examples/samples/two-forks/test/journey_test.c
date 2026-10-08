// Progression is exercised through the match's real commands and ticks,
// independently of the exhaustive kitchen solver.
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "game.h"
#include "tide/net.h"
#include "tide/session.h"
#include "tide_test.h"

static tide_world journey;

static void begin_journey(void)
{
    tide_world_free(&journey);
    tide_world_init(&journey, 1.0f / 60.0f);
    tide_world_player_joined(&journey, tide_player_from_index(0));
    tide_world_player_joined(&journey, tide_player_from_index(1));
    tide_world_tick(&journey);
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

static Tile journey_tile(int x, int y)
{
    return journey.Kitchen.board.tiles.at[y * 12 + x];
}

static void park(int ax, int ay, int bx, int by)
{
    journey.Kitchen.board.chefs.at[0] = (Chef){.at = {ax, ay}, .facing = 0};
    journey.Kitchen.board.chefs.at[1] = (Chef){.at = {bx, by}, .facing = 0};
    memset(&journey.Kitchen.grips, 0, sizeof journey.Kitchen.grips);
}

TIDE_TEST(two_forks_journey_enters_together_and_remembers_the_map)
{
    begin_journey();
    TIDE_CHECK(journey.Kitchen.travelling && journey.Kitchen.board.count == 0);
    TIDE_CHECK(journey_tile(3, 4) == Tile_Rock && journey_tile(6, 2) == Tile_Rock);
    park(1, 7, 2, 8);
    const Chef first = journey.Kitchen.board.chefs.at[0];
    const Chef second = journey.Kitchen.board.chefs.at[1];
    // A remote camp cannot be selected by clicking or sending its command.
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_EnterKitchen, 0, 1));
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.travelling);
    // Either player enters a nearby camp when both chefs have gathered.
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_EnterKitchen, 1, 0));
    tide_world_tick(&journey);
    TIDE_CHECK(!journey.Kitchen.travelling && journey.Kitchen.level == 0 && journey.Kitchen.board.count == 1);
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_BackToMap, 0, 1));
    tide_world_tick(&journey);
    TIDE_CHECK(!journey.Kitchen.travelling);
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_BackToMap, 1, 0));
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.travelling && journey.Kitchen.done == 0);
    TIDE_CHECK(memcmp(&journey.Kitchen.board.chefs.at[0], &first, sizeof first) == 0);
    TIDE_CHECK(memcmp(&journey.Kitchen.board.chefs.at[1], &second, sizeof second) == 0);
    // A stale restart must not replace the map with a kitchen plan.
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_Restart, 0, 0));
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.travelling && journey.Kitchen.board.count == 0);
    tide_world_free(&journey);
}

TIDE_TEST(two_forks_journey_bridges_require_progress_and_both_plates)
{
    begin_journey();
    park(1, 5, 5, 5);
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.bridges == 0);
    // Any two coastal kitchens suffice; the third is an optional route.
    journey.Kitchen.done = (1 << 0) | (1 << 2);
    park(1, 5, 4, 5);
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.bridges == 0);
    park(1, 5, 5, 5);
    const int revision = journey.Kitchen.layout;
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.bridges == 1 && journey_tile(3, 4) == Tile_Grass);
    TIDE_CHECK(journey.Kitchen.layout == revision + 1);
    park(2, 5, 4, 5);
    tide_world_tick(&journey);
    TIDE_CHECK(journey_tile(3, 4) == Tile_Grass);
    journey.Kitchen.done |= (1 << 3) | (1 << 8);
    park(1, 0, 5, 0);
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.bridges == 3 && journey_tile(6, 2) == Tile_Grass);
    journey.Kitchen.done |= (1 << 5) | (1 << 9);
    park(7, 3, 11, 3);
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.bridges == 7 && journey_tile(9, 4) == Tile_Grass);
    tide_world_free(&journey);
}

TIDE_TEST(two_forks_journey_optional_routes_skip_regions_without_credit)
{
    begin_journey();
    park(5, 6, 4, 6);
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_Traverse, 0, 0));
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.board.chefs.at[0].at.x == 5);
    journey.Kitchen.done = 7;
    park(1, 7, 4, 7);
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_Traverse, 1, 0));
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.board.chefs.at[0].at.x == 1);
    park(5, 6, 4, 6);
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_Traverse, 1, 0));
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.board.chefs.at[0].at.x == 7 && journey.Kitchen.board.chefs.at[1].at.x == 9);
    TIDE_CHECK(journey.Kitchen.done == 7 && journey.Kitchen.travelling);
    // Ferry arrivals can enter mountain camps even before countryside clears.
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_EnterKitchen, 0, 6));
    tide_world_tick(&journey);
    TIDE_CHECK(!journey.Kitchen.travelling && journey.Kitchen.level == 6);
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_BackToMap, 0, 6));
    tide_world_tick(&journey);
    park(7, 2, 8, 3);
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_Traverse, 1, 0));
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.board.chefs.at[0].at.x == 5 && journey.Kitchen.board.chefs.at[0].at.y == 6);
    TIDE_CHECK(journey.Kitchen.done == 7);
    journey.Kitchen.done |= (1 << 3) | (1 << 4) | (1 << 8);
    park(5, 2, 4, 2);
    const int done = journey.Kitchen.done;
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_Traverse, 0, 1));
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.board.chefs.at[0].at.y == 5 && journey.Kitchen.board.chefs.at[1].at.y == 5);
    TIDE_CHECK(journey.Kitchen.done == done && journey.Kitchen.travelling);
    park(8, 5, 9, 5);
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_Traverse, 1, 1));
    tide_world_tick(&journey);
    TIDE_CHECK(journey.Kitchen.board.chefs.at[0].at.x == 5 && journey.Kitchen.board.chefs.at[0].at.y == 2);
    TIDE_CHECK(journey.Kitchen.done == done);
    tide_world_free(&journey);
}

TIDE_TEST(two_forks_journey_gate_state_survives_snapshot_restore)
{
    begin_journey();
    journey.Kitchen.done = 3;
    park(1, 5, 5, 5);
    tide_world_tick(&journey);
    tide_world saved = {0};
    tide_world_copy(&saved, &journey);
    const uint64_t hash = tide_world_hash(&saved);
    TIDE_REQUIRE(ask_journey(TIDE_COMMAND_ID_Goto, 0, 10));
    tide_world_tick(&journey);
    TIDE_CHECK(!journey.Kitchen.travelling && journey.Kitchen.done == 3);
    tide_world_copy(&journey, &saved);
    TIDE_CHECK(journey.Kitchen.travelling && journey.Kitchen.bridges == 1);
    TIDE_CHECK(journey_tile(3, 4) == Tile_Grass && tide_world_hash(&journey) == hash);
    tide_world_free(&saved);
    tide_world_free(&journey);
}
