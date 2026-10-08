// Direct kitchen progression through the match's actual input and commands.
// Every puzzle is cooked using a solver witness; there is no selection phase
// between kitchens. The exhaustive rule proof lives in two_forks_test.c.
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "game.h"
#include "tide/list.h"
#include "tide/net.h"
#include "tide/session.h"
#include "tide_test.h"

#define CHEER 190

static tide_world progression;
static const int ORDER[] = {
    0, 1, 2, 11, 12, 13, 3, 4, 8, 14, 15, 16,
    5, 6, 9, 17, 18, 19, 7, 10, 20, 21, 22, 23,
};

// Paths indexed by kitchen id, reproduced by tools/levels.mjs --ways.
static const char *const SOLUTION[] = {
    "0E 0E 0E 1N 1N",
    "0E 0E 1S 1S",
    "1W 0E 0E 1E 1N",
    "0W 0W 1N 1N 0S 1S 1E 1S",
    "0W 0W 1W 1W 1S 1E 0E 0N 1E",
    "0S 0W 1E 1E 0S 0W 1S 1W 0E 0W 1W 1N 1E",
    "0N 0S 0E 1S 1E 1N 0S 1N 1E 1W 1N 1W",
    "0S 0S 0E 0E 0N 0N 0W 1W 0W 0N 0N 1N 0W 0N 0S 0W 1W 1S 1E 1S 0S 0E",
    "0E 1N 1S 1W 1S 1S 1E 0S 0S 1S 0W 0W 0W 0N 0N 0E 1N 1W 1E 1E 1S 1N 1E",
    "1W 1W 0S 0S 1W 1N 1W 1E 1N 1N 0W 0W 1E 0E 0S 0E",
    "1S 1E 1W 1S 1W 1S 1E 1W 1S 0S 0S 1W 1W 1N 0N 0W 0S 0E 0N",
    "1N 1S 1E 1S 1S 0E 0E 1W 0W 0N 0W",
    "0E 0S 0N 0E 0E 0N 0N 0E 0W 0N 1E 1E 0W 1W",
    "0W 0E 0N 1S 1W 1N 0S 0N 1N 1W 1E 1S 1E 1S 0E 0E 0S 0E 1E",
    "0S 0E 1N 1W 1W 1N 1N 1W 1W 0N 0E 1N 1E 0W 0W",
    "0E 0S 0N 0E 0N 0N 1E 0W 1E 1E 1S 0S 1E 1W 1W 1W",
    "1E 0E 0S 0W 0W 0N 0W 0S 1N 1W 1W 1W 0E 0S 1E 1S 0N 0N 1E 1E",
    "0W 0N 0W 0W 0S 0E 0E 0S 1E 1W 1S 1W",
    "0N 0W 1E 1N 1S 1E 1E 1S 0N 0E 1E 1W 1N 1W 0N 0W",
    "0N 0W 0W 0W 0W 0S 0S 0E 0N 0W 1N 1E 1W 1N 1N 1W 0N 1S 1E",
    "0E 0N 0S 0E 1W 0E 0W",
    "0E 1W 1N 1W 1E 0W 0S 1E 1S 0S 0S 1E 1N 0N 0N 0W 0E",
    "0E 0W 1S 1S 0W 1W 1W 0S 0S 1N 1N 0W 1W 1E 0W 1E 1S 0E 1E 1E 1N 1E 1W",
    "0N 0W 0N 1E 1W 1S 0E 0E 0E 1N 1W 0S 0N 0E 1S 1E 1N 1S 1W 1E 1S 1E 1W 1N 1N 1E 1E 1W 0S",
};

static void tick_progression(const int ticks)
{
    for (int i = 0; i < ticks; i++) tide_world_tick(&progression);
}

static void begin_progression(void)
{
    tide_world_free(&progression);
    tide_world_init(&progression, 1.0f / 60.0f);
    tide_world_player_joined(&progression, tide_player_from_index(0));
    tide_world_player_joined(&progression, tide_player_from_index(1));
    tide_world_tick(&progression);
}

static bool ask_progression(const int sender, const int level)
{
    uint8_t bytes[TIDE_COMMAND_BYTES];
    tide_bits bits = {bytes, sizeof bytes, 0, false};
    tide_bits_put(&bits, TIDE_COMMAND_ID_Goto, 16);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put_varint(&bits, tide_zigzag(level));
    const uint32_t size = tide_bits_end(&bits);
    const tide_player_id player = tide_player_from_index(sender);
    return tide_game_api.command(&progression, player, 0,
        tide_game_api.command_sequence(&progression, player, 0) + 1, bytes, size);
}

static void press_progression(const int who, const int way)
{
    const Controls keys = {.north = way == 0, .east = way == 1, .south = way == 2, .west = way == 3};
    tide_world_set_input(&progression, tide_player_from_index(who), keys);
    tide_world_tick(&progression);
    tide_world_set_input(&progression, tide_player_from_index(who), (Controls){0});
    tide_world_tick(&progression);
}

static void cook_progression(const int level)
{
    const char *const ways = "NESW";
    for (const char *at = SOLUTION[level]; *at; at += at[2] ? 3 : 2) {
        press_progression(at[0] - '0', (int)(strchr(ways, at[1]) - ways));
        tick_progression(8);
    }
}

TIDE_TEST(two_forks_starts_in_a_kitchen_and_advances_through_all_regions)
{
    begin_progression();
    TIDE_CHECK(progression.Kitchen.laid && progression.Kitchen.level == 0);
    TIDE_CHECK(progression.Kitchen.board.count == 1 && progression.Kitchen.done == 0);
    TIDE_CHECK(progression.Kitchen.layout == 1 && progression.Kitchen.moves == 0);
    int credit = 0;
    for (int index = 0; index < (int)(sizeof ORDER / sizeof ORDER[0]); index++) {
        const int level = ORDER[index];
        TIDE_REQUIRE(progression.Kitchen.level == level);
        const int layout = progression.Kitchen.layout;
        cook_progression(level);
        credit |= 1 << level;
        TIDE_REQUIRE(progression.Kitchen.cleared != 0);
        TIDE_CHECK(progression.Kitchen.level == level && progression.Kitchen.done == credit);
        const Board cooked = progression.Kitchen.board;
        press_progression(0, 0);
        TIDE_CHECK(memcmp(&progression.Kitchen.board, &cooked, sizeof cooked) == 0);
        tick_progression(CHEER);
        TIDE_CHECK(progression.Kitchen.level == ORDER[(index + 1) % 24]);
        TIDE_CHECK(progression.Kitchen.layout == layout + 1 && progression.Kitchen.board.count > 0);
        TIDE_CHECK(progression.Kitchen.moves == 0 && progression.Kitchen.cleared == 0 && progression.Kitchen.done == credit);
        TIDE_CHECK(tide_list_count(progression.Kitchen.past) == 0);
    }
    TIDE_CHECK(progression.Kitchen.level == 0 && progression.Kitchen.done == (1 << 24) - 1);
    tide_world_free(&progression);
}

TIDE_TEST(two_forks_goto_resets_a_shared_kitchen_without_awarding_credit)
{
    begin_progression();
    cook_progression(0);
    TIDE_REQUIRE(progression.Kitchen.cleared != 0 && progression.Kitchen.done == 1);
    TIDE_REQUIRE(ask_progression(1, 17));
    tide_world_tick(&progression);
    // A quick second press waits behind the first move, leaving real pending
    // movement and undo history for the skip to discard.
    press_progression(0, 3);
    press_progression(0, 3);
    TIDE_REQUIRE(progression.Kitchen.moves == 1 && progression.Kitchen.grips.at[0].wish != 0);
    const int layout = progression.Kitchen.layout;
    TIDE_REQUIRE(ask_progression(1, 20));
    tide_world_tick(&progression);
    TIDE_CHECK(progression.Kitchen.level == 20 && progression.Kitchen.layout == layout + 1);
    TIDE_CHECK(progression.Kitchen.moves == 0 && progression.Kitchen.cleared == 0 && progression.Kitchen.done == 1);
    TIDE_CHECK(progression.Kitchen.grips.at[0].wish == 0 && progression.Kitchen.grips.at[0].wait == 0);
    TIDE_CHECK(tide_list_count(progression.Kitchen.past) == 0);
    const Board opened = progression.Kitchen.board;
    const int opened_layout = progression.Kitchen.layout;
    (void)ask_progression(2, 23); // This sender has never joined the match.
    tide_world_tick(&progression);
    TIDE_REQUIRE(ask_progression(0, -1));
    tide_world_tick(&progression);
    TIDE_REQUIRE(ask_progression(0, 24));
    tide_world_tick(&progression);
    TIDE_CHECK(progression.Kitchen.level == 20 && progression.Kitchen.layout == opened_layout);
    TIDE_CHECK(memcmp(&progression.Kitchen.board, &opened, sizeof opened) == 0 && progression.Kitchen.done == 1);
    tide_world_free(&progression);
}

TIDE_TEST(two_forks_snapshot_restores_its_own_kitchen_and_layout)
{
    begin_progression();
    cook_progression(0);
    TIDE_REQUIRE(ask_progression(0, 17));
    tide_world_tick(&progression);
    press_progression(0, 3);
    tick_progression(8);
    tide_world saved = {0};
    tide_world_copy(&saved, &progression);
    const uint64_t hash = tide_world_hash(&saved);
    const int layout = saved.Kitchen.layout;
    const Board board = saved.Kitchen.board;
    TIDE_REQUIRE(ask_progression(1, 23));
    tide_world_tick(&progression);
    TIDE_CHECK(progression.Kitchen.level == 23 && progression.Kitchen.layout == layout + 1);
    TIDE_CHECK(memcmp(&progression.Kitchen.board, &board, sizeof board) != 0);
    tide_world_copy(&progression, &saved);
    TIDE_CHECK(progression.Kitchen.level == 17 && progression.Kitchen.layout == layout);
    TIDE_CHECK(progression.Kitchen.done == 1 && progression.Kitchen.moves == 1);
    TIDE_CHECK(memcmp(&progression.Kitchen.board, &board, sizeof board) == 0 && tide_world_hash(&progression) == hash);
    tide_world_free(&saved);
    tide_world_free(&progression);
}
