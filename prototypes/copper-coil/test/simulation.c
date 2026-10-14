#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tide/net.h"
#include "tide_test.h"

static tide_world world;
#define station world.Station
#define AT(x, y) ((y) * 13 + (x))
static tide_player_id player(int seat) { return tide_player_from_index(seat); }
static void start(int players)
{
    tide_world_free(&world);
    tide_world_init(&world, 1.0f / 30.0f);
    for (int i = 0; i < players; i++) tide_world_player_joined(&world, player(i));
    tide_world_tick(&world);
    tide_world_tick(&world);
}
static bool send(uint32_t id, int sender, int serial, int first, int second)
{
    uint8_t bytes[TIDE_COMMAND_BYTES];
    tide_bits bits = {bytes, TIDE_COMMAND_BYTES, 0, false};
    tide_bits_put(&bits, id, 16);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put_varint(&bits, tide_zigzag(serial));
    if (id == TIDE_COMMAND_ID_Operate || id == TIDE_COMMAND_ID_Probe || id == TIDE_COMMAND_ID_Visit)
        tide_bits_put_varint(&bits, tide_zigzag(first));
    if (id == TIDE_COMMAND_ID_Operate) tide_bits_put_varint(&bits, tide_zigzag(second));
    uint32_t size = tide_bits_end(&bits);
    bool accepted = tide_game_api.command(&world, player(sender), 0,
        tide_game_api.command_sequence(&world, player(sender), 0) + 1, bytes, size);
    tide_world_tick(&world);
    return accepted;
}
static void act(int sender, int robot, int action) { (void)send(TIDE_COMMAND_ID_Operate, sender, station.serial, robot, action); }
static void visit(int level) { (void)send(TIDE_COMMAND_ID_Visit, 0, station.serial, level, 0); }
static void tick(int count) { for (int i = 0; i < count; i++) tide_world_tick(&world); }

static bool walkable(int cell, int level, int robot)
{
    int x = cell % 13, y = cell / 13;
    if (cell < 0 || cell >= 117 || y < 1 || y > 7) return false;
    if (x >= 5 && x <= 7 && !(station.circuit.powered && (y == 4 || y == 5))) return false;
    static const int sockets[5] = {AT(2,2), AT(10,2), AT(4,3), AT(8,3), AT(10,4)};
    for (int i = 0; i < (level == 5 ? 5 : 4); i++) if (cell == sockets[i]) return false;
    return cell != AT(1,4) && cell != AT(11,4) && !(level == 5 && cell == AT(11,6))
        && cell != station.circuit.robot.at[1 - robot];
}
static bool walk_to(int robot, int destination)
{
    static const int delta[4] = {-13, 1, 13, -1};
    int queue[117], prev[117], dir[117], read = 0, write = 0;
    for (int i = 0; i < 117; i++) prev[i] = -1;
    int origin = station.circuit.robot.at[robot];
    prev[origin] = origin;
    queue[write++] = origin;
    while (read < write && prev[destination] < 0) {
        int from = queue[read++];
        for (int d = 0; d < 4; d++) {
            int to = from + delta[d];
            if ((d == 1 && from % 13 == 12) || (d == 3 && from % 13 == 0)) continue;
            if (!walkable(to, station.level, robot) || prev[to] >= 0) continue;
            prev[to] = from; dir[to] = d; queue[write++] = to;
        }
    }
    if (prev[destination] < 0) return false;
    int path[117], count = 0;
    for (int at = destination; at != origin; at = prev[at]) path[count++] = dir[at];
    while (count > 0) act(robot, robot, path[--count]);
    return station.circuit.robot.at[robot] == destination;
}

TIDE_TEST(copper_coil_campaign_solved_through_real_player_commands)
{
    static const int consoles[5] = {AT(2,3), AT(10,3), AT(4,4), AT(8,4), AT(9,4)};
    static const Gate solutions[6][5] = {
        {Gate_Wire,Gate_Wire,Gate_And,Gate_Wire,Gate_Or},
        {Gate_Wire,Gate_Not,Gate_And,Gate_Wire,Gate_Or},
        {Gate_Wire,Gate_Wire,Gate_Xor,Gate_Wire,Gate_Or},
        {Gate_Wire,Gate_Wire,Gate_Or,Gate_Not,Gate_Or},
        {Gate_Wire,Gate_Wire,Gate_Xor,Gate_Not,Gate_Or},
        {Gate_Wire,Gate_Wire,Gate_Xor,Gate_Wire,Gate_And},
    };
    static const int live[6] = {3,1,1,0,0,5};
    start(2);
    for (int level = 0; level < 6; level++) {
        visit(level);
        for (int socket = 0; socket < 5; socket++) {
            if (station.circuit.gates.at[socket] == solutions[level][socket]) continue;
            int robot = socket == 0 || socket == 2 ? 0 : 1;
            TIDE_REQUIRE(walk_to(robot, consoles[socket]));
            for (int guard = 0; guard < 3 && station.circuit.gates.at[socket] != solutions[level][socket]; guard++) act(robot, robot, 4);
            TIDE_REQUIRE(station.circuit.gates.at[socket] == solutions[level][socket]);
        }
        (void)send(TIDE_COMMAND_ID_Probe, 1, station.serial, live[level], 0);
        TIDE_REQUIRE(walk_to(0, AT(3,6)));
        TIDE_REQUIRE(walk_to(1, AT(9,6)));
        tick(36);
        TIDE_REQUIRE(station.circuit.powered);
        TIDE_REQUIRE(walk_to(0, AT(5,4)));
        TIDE_REQUIRE(walk_to(1, AT(7,4)));
        tick(1);
        TIDE_REQUIRE(station.circuit.solved);
    }
    TIDE_CHECK(station.completed == 63);
    tide_world_free(&world);
}

TIDE_TEST(copper_coil_charge_requires_live_power_and_resets_when_a_partner_leaves_pad)
{
    start(2);
    station.circuit.gates.at[2] = Gate_And;
    station.circuit.robot.at[0] = AT(3,6);
    station.circuit.robot.at[1] = AT(9,6);
    tick(40);
    TIDE_CHECK(!station.circuit.powered && station.circuit.charge == 0);
    (void)send(TIDE_COMMAND_ID_Probe, 1, station.serial, 3, 0);
    tick(12);
    TIDE_CHECK(station.circuit.charge >= 12 && !station.circuit.powered);
    act(1,1,1); tick(1);
    TIDE_CHECK(station.circuit.charge == 0);
    act(1,1,3); tick(36);
    TIDE_CHECK(station.circuit.powered);
    (void)send(TIDE_COMMAND_ID_Rewind,0,station.serial,0,0);
    TIDE_CHECK(!station.circuit.powered);
    tide_world_player_left(&world,player(1)); tick(1);
    act(0,1,1);
    TIDE_CHECK(station.circuit.robot.at[1] == AT(10,6));
    tide_world_free(&world);
}

TIDE_TEST(copper_coil_authority_and_bounds)
{
    start(2);
    TIDE_CHECK(station.present.at[0] && station.present.at[1]);
    int before = station.circuit.robot.at[1];
    act(0, 1, 0);
    TIDE_CHECK(station.circuit.robot.at[1] == before);
    act(1, 1, 0);
    TIDE_CHECK(station.circuit.robot.at[1] == before - 13);
    Circuit unchanged = station.circuit;
    act(0, -1, 0);
    act(0, 2, 0);
    act(0, 0, 99);
    (void)send(TIDE_COMMAND_ID_Operate, 0, station.serial - 1, 0, 0);
    (void)send(TIDE_COMMAND_ID_Operate, 7, station.serial, 0, 0);
    TIDE_CHECK(memcmp(&unchanged, &station.circuit, sizeof unchanged) == 0);
    (void)send(TIDE_COMMAND_ID_Probe, 0, station.serial, -1, 0);
    (void)send(TIDE_COMMAND_ID_Probe, 0, station.serial, 8, 0);
    TIDE_CHECK(station.circuit.probe == 0);
    station.circuit.robot.at[0] = AT(4, 4);
    act(0, 0, 1);
    TIDE_CHECK(station.circuit.robot.at[0] == AT(4, 4));
    station.circuit.robot.at[0] = AT(0, 1);
    act(0, 0, 3);
    act(0, 0, 0);
    TIDE_CHECK(station.circuit.robot.at[0] == AT(0, 1));
    tide_world_free(&world);
}

TIDE_TEST(copper_coil_gate_interaction_and_undo)
{
    start(1);
    station.circuit.robot.at[0] = AT(4, 4);
    TIDE_CHECK(station.circuit.gates.at[2] == Gate_Or);
    act(0, 0, 4);
    TIDE_CHECK(station.circuit.gates.at[2] == Gate_Xor);
    act(0, 0, 4);
    TIDE_CHECK(station.circuit.gates.at[2] == Gate_And);
    (void)send(TIDE_COMMAND_ID_Rewind, 0, station.serial, 0, 0);
    TIDE_CHECK(station.circuit.gates.at[2] == Gate_Xor);
    station.circuit.robot.at[0] = AT(2, 3); // locked input console
    act(0, 0, 4);
    TIDE_CHECK(station.circuit.gates.at[0] == Gate_Wire);
    station.circuit.robot.at[1] = AT(10, 5);
    act(0, 1, 0); // blocked by lever? at (10,4) is floor in chamber 1
    TIDE_CHECK(station.circuit.robot.at[1] == AT(10, 4));
    tide_world_free(&world);
}

TIDE_TEST(copper_coil_all_chambers_require_correct_circuits_and_both_robots)
{
    static const Gate solutions[6][5] = {
        {Gate_Wire, Gate_Wire, Gate_And, Gate_Wire, Gate_Or},
        {Gate_Wire, Gate_Not, Gate_And, Gate_Wire, Gate_Or},
        {Gate_Wire, Gate_Wire, Gate_Xor, Gate_Wire, Gate_Or},
        {Gate_Wire, Gate_Wire, Gate_Or, Gate_Not, Gate_Or},
        {Gate_Wire, Gate_Wire, Gate_Xor, Gate_Not, Gate_Or},
        {Gate_Wire, Gate_Wire, Gate_Xor, Gate_Wire, Gate_And},
    };
    static const int live_rows[6] = {3, 1, 1, 0, 0, 5};
    start(2);
    for (int level = 0; level < 6; level++) {
        visit(level);
        station.circuit.robot.at[0] = AT(3, 6);
        station.circuit.robot.at[1] = AT(9, 6);
        tick(40);
        TIDE_CHECK(!station.circuit.powered); // deliberately wrong starting build
        for (int i = 0; i < 5; i++) station.circuit.gates.at[i] = solutions[level][i];
        station.circuit.probe = live_rows[level];
        station.circuit.robot.at[1] = AT(10, 6);
        tick(40);
        TIDE_CHECK(!station.circuit.powered); // one pad never suffices
        act(1, 1, 3);
        tick(36);
        TIDE_CHECK(station.circuit.powered);
        TIDE_CHECK(!station.circuit.solved);
        station.circuit.robot.at[0] = AT(4, 4);
        act(0, 0, 1); // cross newly commissioned bridge
        TIDE_CHECK(station.circuit.robot.at[0] == AT(5, 4));
        station.circuit.robot.at[1] = AT(8, 4);
        act(1, 1, 3);
        tick(1); // commands are dispatched at the end of their delivery tick
        TIDE_CHECK(station.circuit.solved);
        TIDE_CHECK((station.completed & (1 << level)) != 0);
    }
    TIDE_CHECK(station.completed == 63);
    tide_world_free(&world);
}

TIDE_TEST(copper_coil_snapshot_and_packed_state)
{
    start(2);
    visit(5);
    act(0, 0, 0);
    act(1, 1, 3);
    tide_world copy = {0}, unpacked = {0};
    tide_world_copy(&copy, &world);
    const uint64_t hash = tide_world_hash(&world);
    act(0, 0, 0);
    TIDE_CHECK(tide_world_hash(&copy) == hash);
    uint32_t capacity = 4 * 1024 * 1024;
    uint8_t *bytes = malloc(capacity);
    TIDE_REQUIRE(bytes != NULL);
    uint32_t size = tide_world_pack(&copy, bytes, capacity);
    TIDE_REQUIRE(size > 0 && size <= capacity);
    TIDE_REQUIRE(tide_world_unpack(&unpacked, bytes, size));
    TIDE_CHECK(tide_world_hash(&unpacked) == hash);
    free(bytes);
    tide_world_free(&copy);
    tide_world_free(&unpacked);
    tide_world_free(&world);
}
