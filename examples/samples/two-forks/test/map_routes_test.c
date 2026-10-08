// Each path rolls the meatball into a pan and gathers both chefs, through
// real match input. The search tool stops a branch as soon as it enters any
// kitchen, so these witnesses cannot silently pass through another puzzle.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "tide/net.h"
#include "tide/session.h"
#include "tide_test.h"

static tide_world route_world;
static const int CAMP_LEVEL[4][6] = {
    {0, 1, 2, 11, 12, 13}, {3, 4, 8, 14, 15, 16},
    {5, 6, 9, 17, 18, 19}, {7, 10, 20, 21, 22, 23},
};
static const char *const CAMP_PATH[4][6] = {
    {"0S 0S 1W 1W 1W 1W 0N 0N 0W 0N 0N 1W",
     "0N 1W 1W 1W",
     "0N 0N 1W 1W 1W 1W 0S 0S 0W 0S 0S 1W",
     "0N 0W 0E 0E 0E 1N 1N 1N 0E 0S 0E 0N 0N",
     "0E 0E 1N 0E",
     "0N 0W 0E 0E 1W 1N 0E 1W 1N 0E 0S 0S 1S"},
    {"1W 1W 1S 0E 1W 1N 1N 1N 1E 1W 1W",
     "0E 0E 0N 0N 1W 1W 1W 1S 0S",
     "1W 1W 1N 0E 1W 1S 1S 1S 1E 1W 1W",
     "0E 0E 0S 1W 0E 0N 0N 0N 0W 0E 0E",
     "1W 1W 1N 0E 1N 0E 0E 0S 1S",
     "0E 0E 0N 1W 0E 0S 0S 0S 0W 0E 0E"},
    {"0S 1W 1W 0E 0N 0N 0N 1W 1W 1W",
     "0N 1W 1W 1W",
     "0N 1W 1W 0E 0S 0S 0S 1W 1W 1W",
     "0E 0E 0S 1W 0E 0N 0N 0N 0W 0E 0E",
     "0E 0E 1N 0E",
     "0E 0E 0N 1W 0E 0N 0S 0S 0W 0E 0E"},
    {"0S 0S 1W 1W 1W 1W 0N 0N 0W 0N 0N 1W",
     "0N 1W 1W 1W",
     "0N 0N 1W 1W 1W 1W 0S 0S 0W 0S 0S 1W",
     "0E 0E 1S 1S 0E 0E 1N 1N 1E 0E 1N 1N",
     "0E 0E 1N 0E",
     "0E 0E 1N 1N 0E 0E 1S 1S 1E 0E 1S 1S"},
};
static const char *const GATE_PATH[3] = {
    "0N 0N 1N 1N",
    "0E 0E 0N 0N 0N 0E 0W 0W 0N 0S 1W 1W 1N 1N 1N 1E 1E 1E 1N 1S",
    "0N 0N 1N 1N",
};

static int credit_before(int region)
{
    int done = 0;
    for (int r = 0; r < region; r++)
        for (int i = 0; i < 6; i++) done |= 1 << CAMP_LEVEL[r][i];
    return done;
}

static void begin_route(int region)
{
    tide_world_free(&route_world);
    tide_world_init(&route_world, 1.0f / 60.0f);
    route_world.Kitchen.level = CAMP_LEVEL[region][0];
    route_world.Kitchen.done = credit_before(region);
    tide_world_player_joined(&route_world, tide_player_from_index(0));
    tide_world_player_joined(&route_world, tide_player_from_index(1));
    tide_world_tick(&route_world);
}

static bool walk_route(const char *path)
{
    for (const char *at = path; *at; at += at[2] ? 3 : 2) {
        if (!route_world.Kitchen.travelling) return false;
        const int who = at[0] - '0';
        const char *const ways = "NESW";
        const int way = (int)(strchr(ways, at[1]) - ways);
        const int moves = route_world.Kitchen.moves;
        const Controls keys = {.north = way == 0, .east = way == 1, .south = way == 2, .west = way == 3};
        tide_world_set_input(&route_world, tide_player_from_index(who), keys);
        tide_world_tick(&route_world);
        tide_world_set_input(&route_world, tide_player_from_index(who), (Controls){0});
        for (int i = 0; i < 9; i++) tide_world_tick(&route_world);
        if (route_world.Kitchen.travelling && route_world.Kitchen.moves != moves + 1) {
            printf("    map witness move %c%c was blocked\n", at[0], at[1]);
            return false;
        }
    }
    return true;
}

static bool return_to_yard(int level)
{
    uint8_t bytes[TIDE_COMMAND_BYTES];
    tide_bits bits = {bytes, sizeof bytes, 0, false};
    tide_bits_put(&bits, TIDE_COMMAND_ID_BackToMap, 16);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put_varint(&bits, tide_zigzag(level));
    const uint32_t size = tide_bits_end(&bits);
    const tide_player_id sender = tide_player_from_index(1);
    return tide_game_api.command(&route_world, sender, 0,
        tide_game_api.command_sequence(&route_world, sender, 0) + 1, bytes, size);
}

TIDE_TEST(two_forks_map_meatball_can_select_every_kitchen)
{
    for (int region = 0; region < 4; region++) {
        for (int slot = 0; slot < 6; slot++) {
            begin_route(region);
            const int done = route_world.Kitchen.done;
            TIDE_REQUIRE(walk_route(CAMP_PATH[region][slot]));
            TIDE_CHECK(!route_world.Kitchen.travelling);
            TIDE_CHECK(route_world.Kitchen.level == CAMP_LEVEL[region][slot]);
            TIDE_CHECK(route_world.Kitchen.done == done);
            TIDE_CHECK(route_world.Kitchen.meatball.x == (slot < 3 ? 2 : 6));
            TIDE_CHECK(route_world.Kitchen.meatball.y == 1 + (slot % 3) * 2);
            TIDE_REQUIRE(return_to_yard(CAMP_LEVEL[region][slot]));
            for (int i = 0; i < 5; i++) tide_world_tick(&route_world);
            TIDE_CHECK(route_world.Kitchen.travelling && route_world.Kitchen.routeRegion == region);
            TIDE_CHECK(route_world.Kitchen.selection == -1 && route_world.Kitchen.done == done);
        }
    }
    tide_world_free(&route_world);
}

TIDE_TEST(two_forks_map_both_chefs_can_reach_every_pair_of_bridge_plates)
{
    for (int region = 0; region < 3; region++) {
        begin_route(region);
        for (int i = 0; i < 4; i++) route_world.Kitchen.done |= 1 << CAMP_LEVEL[region][i];
        const int done = route_world.Kitchen.done;
        TIDE_REQUIRE(walk_route(GATE_PATH[region]));
        TIDE_CHECK(route_world.Kitchen.travelling && route_world.Kitchen.done == done);
        TIDE_CHECK(route_world.Kitchen.bridges == (1 << region));
        TIDE_CHECK(route_world.Kitchen.board.tiles.at[4 * 12 + 7] == Tile_Grass);
    }
    tide_world_free(&route_world);
}
