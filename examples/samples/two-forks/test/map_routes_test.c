// Concrete movement witnesses for the map. Starting at the normal spawn,
// the real match walks both chefs to all six bridge plates and every camp.
// Kitchen solvability is proved separately; these credit bits stand for the
// two completed kitchens needed before each next bridge can open.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/net.h"
#include "tide/session.h"
#include "tide_test.h"

static tide_world route_world;

static const int CREDIT[] = {
    0,
    (1 << 0) | (1 << 1),
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 4),
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6),
};

// A token is a chef (0 or 1) and its key (north, east, south or west).
// Each gate's path starts where the preceding gate's path ended.
static const char *const GATE[] = {
    "0N 0N 0N 1E 1N 1N 1N",
    "0E 0E 0E 0N 0N 0N 0N 0N 0N 0E 0E 0E 1E 1W 1W 1N 1N 1N 1N 1N 1N 1E 1W 1W",
    "0N 0S 0S 0E 0E 0E 0E 0E 0E 0E 0N 0S 1N 1S 1S 1E 1E 1E 1E 1E 1E 1E 1N 1S",
};

// From the entry to a region (its preceding bridge's plates), gather close
// enough to enter each kitchen. Coastal camps start at the normal spawn.
static const struct camp_path {
    int region;
    const char *path;
} CAMP[] = {
    {0, "0N 1W 1N 1S"},
    {0, "0N 0N 1W"},
    {0, "1S 0E 1S 0E 0E"},
    {1, "0E 0E 0E 0N 0N 0N 0N 0N 0W 0W 1E 1W 1W 1N 1N 1N 1N 1N"},
    {1, "0E 0E 0E 0N 0N 0N 1E 1W 1W 1S 1N"},
    {2, "0N 0S 0S 0E 0E 0E 1E 1E 1E 1E 1N 1S"},
    {2, "0N 0S 0S 0E 0E 0E 0E 1N 1S 1S 1W 1E 1E 1E 1E 1E 1E"},
    {3, "0E 0W 0W 0S 0S 0S 0S 0S 1E 1E 1E 1N 1S 1S 1S"},
    {1, "0E 0E 0E 0N 0N 0N 0N 0N 0E 0E 1E 1W 1W 1N 1N 1N 1N 1N"},
    {2, "0N 0S 0S 0E 0E 0E 0E 0E 0E 0E 1N 1S 1S 1W 1E 1E 1E 1E 1E 1E 1E 1E 1E"},
    {3, "0E 0W 0W 0N 0S 0S 0S 0E 0E 0E 1E 1E 1E 1N 1S 1S 1S 1S"},
};

static void begin_route(void)
{
    tide_world_free(&route_world);
    tide_world_init(&route_world, 1.0f / 60.0f);
    tide_world_player_joined(&route_world, tide_player_from_index(0));
    tide_world_player_joined(&route_world, tide_player_from_index(1));
    tide_world_tick(&route_world);
}

static bool walk_route(const char *path)
{
    for (const char *at = path; *at; at += at[2] ? 3 : 2) {
        const int who = at[0] - '0';
        const char *const ways = "NESW";
        const int way = (int)(strchr(ways, at[1]) - ways);
        const int moves = route_world.Kitchen.moves;
        const Controls keys = {.north = way == 0, .east = way == 1, .south = way == 2, .west = way == 3};
        tide_world_set_input(&route_world, tide_player_from_index(who), keys);
        tide_world_tick(&route_world);
        tide_world_set_input(&route_world, tide_player_from_index(who), (Controls){0});
        for (int i = 0; i < 9; i++) tide_world_tick(&route_world);
        if (route_world.Kitchen.moves != moves + 1) {
            printf("    map witness move %c%c was blocked\n", at[0], at[1]);
            return false;
        }
    }
    return true;
}

static bool open_to(const int region)
{
    for (int gate = 0; gate < region; gate++) {
        route_world.Kitchen.done = CREDIT[gate + 1];
        if (!walk_route(GATE[gate])) return false;
        if (route_world.Kitchen.bridges != (1 << (gate + 1)) - 1) return false;
    }
    return true;
}

static bool enter_camp(const int level)
{
    uint8_t bytes[TIDE_COMMAND_BYTES];
    tide_bits bits = {bytes, sizeof bytes, 0, false};
    tide_bits_put(&bits, TIDE_COMMAND_ID_EnterKitchen, 16);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put_varint(&bits, tide_zigzag(level));
    const uint32_t size = tide_bits_end(&bits);
    const tide_player_id player = tide_player_from_index(0);
    return tide_game_api.command(&route_world, player, 0,
        tide_game_api.command_sequence(&route_world, player, 0) + 1, bytes, size);
}

TIDE_TEST(two_forks_journey_every_bridge_plate_can_be_walked_to)
{
    begin_route();
    TIDE_REQUIRE(open_to(3));
    TIDE_CHECK(route_world.Kitchen.travelling && route_world.Kitchen.board.count == 0);
    TIDE_CHECK(route_world.Kitchen.board.tiles.at[4 * 12 + 3] == Tile_Grass);
    TIDE_CHECK(route_world.Kitchen.board.tiles.at[2 * 12 + 6] == Tile_Grass);
    TIDE_CHECK(route_world.Kitchen.board.tiles.at[4 * 12 + 9] == Tile_Grass);
    TIDE_CHECK(route_world.Kitchen.done == CREDIT[3]);
    tide_world_free(&route_world);
}

TIDE_TEST(two_forks_journey_every_camp_can_be_walked_to)
{
    for (int level = 0; level < (int)(sizeof CAMP / sizeof CAMP[0]); level++) {
        begin_route();
        TIDE_REQUIRE(open_to(CAMP[level].region));
        TIDE_REQUIRE(walk_route(CAMP[level].path));
        TIDE_REQUIRE(enter_camp(level));
        tide_world_tick(&route_world);
        TIDE_CHECK(!route_world.Kitchen.travelling && route_world.Kitchen.level == level);
        TIDE_CHECK(route_world.Kitchen.done == CREDIT[CAMP[level].region]);
    }
    tide_world_free(&route_world);
}
