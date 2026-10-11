// Rules are exercised through generated commands, including sender validation,
// the real queue ordering, snapshot state and the same ABI used by WebRTC.
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/net.h"
#include "tide_test.h"
#include "test_helpers.h"

tide_world inside_out_test_world;
#define world inside_out_test_world

static tide_player_id player(const int seat) { return tide_player_from_index(seat); }

void inside_out_test_start(const int participants)
{
    tide_world_free(&world);
    tide_world_init(&world, 1.0f / 30.0f);
    for (int seat = 0; seat < participants; seat++) tide_world_player_joined(&world, player(seat));
    tide_world_tick(&world);
    tide_world_tick(&world);
}

bool inside_out_test_command(const uint32_t id, const int sender, const int serial, const int value)
{
    uint8_t bytes[TIDE_COMMAND_BYTES];
    tide_bits bits = {bytes, TIDE_COMMAND_BYTES, 0, false};
    tide_bits_put(&bits, id, 16);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put_varint(&bits, tide_zigzag(serial));
    if (id == TIDE_COMMAND_ID_Move || id == TIDE_COMMAND_ID_ChooseLevel)
        tide_bits_put_varint(&bits, tide_zigzag(value));
    const uint32_t size = tide_bits_end(&bits);
    return tide_game_api.command(&world, player(sender), 0,
        tide_game_api.command_sequence(&world, player(sender), 0) + 1, bytes, size);
}

bool inside_out_test_act(const uint32_t id, const int sender, const int value)
{
    const bool received = inside_out_test_command(id, sender, world.Workshop.serial, value);
    tide_world_tick(&world);
    return received;
}

static bool same_cell(const tide_int2 a, const tide_int2 b) { return a.x == b.x && a.y == b.y; }
static int cell(const int x, const int y, const int size) { return y * size + x; }

static void fixture(const int rooms)
{
    inside_out_test_start(2);
    memset(&world.Workshop.layout, 0, sizeof world.Workshop.layout);
    memset(&world.Workshop.puzzle, 0, sizeof world.Workshop.puzzle);
    world.Workshop.historyCount = 0;
    world.Workshop.layout.roomCount = rooms;
    for (int i = 0; i < 4; i++) world.Workshop.layout.rooms.at[i].dock = tide_i2(-1, -1);
    world.Workshop.puzzle.smallSeat = 1;
    world.Workshop.puzzle.outside = tide_i2(1, 3);
    world.Workshop.puzzle.inside = tide_i2(2, 2);
    world.Workshop.puzzle.rooms.at[0] = tide_i2(3, 3);
    world.Workshop.puzzle.rooms.at[1] = tide_i2(4, 3);
}

static void crate(const int index, const int room, const int x, const int y)
{
    world.Workshop.layout.crateCount = index + 1;
    world.Workshop.puzzle.crates.at[index] = (Cargo){.room = room, .position = {x, y}, .active = true};
}

TIDE_TEST(inside_out_requires_two_real_present_participants)
{
    inside_out_test_start(1);
    const Puzzle before = world.Workshop.puzzle;
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 0, 1));
    TIDE_CHECK(memcmp(&before, &world.Workshop.puzzle, sizeof before) == 0);
    tide_world_player_joined(&world, player(1));
    tide_world_tick(&world);
    TIDE_CHECK(world.Workshop.present.at[0] && world.Workshop.present.at[1]);
    tide_world_player_joined(&world, player(2));
    tide_world_tick(&world);
    const int serial = world.Workshop.serial;
    tide_world_player_left(&world, player(1));
    tide_world_tick(&world);
    TIDE_CHECK(!world.Workshop.present.at[1]);
    TIDE_CHECK(world.Workshop.serial != serial);
    const Puzzle paused = world.Workshop.puzzle;
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Reset, 0, 0));
    TIDE_CHECK(memcmp(&paused, &world.Workshop.puzzle, sizeof paused) == 0);
    tide_world_player_joined(&world, player(1));
    tide_world_tick(&world);
    TIDE_CHECK(world.Workshop.present.at[1]);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_rejects_stale_and_malformed_game_actions)
{
    fixture(1);
    const Puzzle before = world.Workshop.puzzle;
    TIDE_CHECK(inside_out_test_command(TIDE_COMMAND_ID_Move, 0, world.Workshop.serial - 1, 1));
    tide_world_tick(&world);
    TIDE_CHECK(memcmp(&before, &world.Workshop.puzzle, sizeof before) == 0);
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 0, -1));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 1, INT32_MAX));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 0, -1));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 1, 12));
    TIDE_CHECK(memcmp(&before, &world.Workshop.puzzle, sizeof before) == 0);
    TIDE_CHECK(world.Workshop.historyCount == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_real_sender_identity_rejoin_and_progression_guards)
{
    fixture(1);
    const Puzzle before = world.Workshop.puzzle;
    TIDE_CHECK(!inside_out_test_command(TIDE_COMMAND_ID_Move, 2, world.Workshop.serial, 1));
    tide_world_tick(&world);
    TIDE_CHECK(memcmp(&before, &world.Workshop.puzzle, sizeof before) == 0);
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Next, 0, 0));
    TIDE_CHECK(world.Workshop.level == 0);
    TIDE_CHECK(memcmp(&before, &world.Workshop.puzzle, sizeof before) == 0);

    const int old_serial = world.Workshop.serial;
    tide_world_player_left(&world, player(0));
    tide_world_player_left(&world, player(1));
    tide_world_tick(&world);
    TIDE_CHECK(!world.Workshop.present.at[0] && !world.Workshop.present.at[1]);
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Reset, 1, 0));
    TIDE_CHECK(memcmp(&before, &world.Workshop.puzzle, sizeof before) == 0);
    // Joining in the opposite order cannot exchange the persistent seats.
    tide_world_player_joined(&world, player(1));
    tide_world_player_joined(&world, player(0));
    tide_world_tick(&world);
    TIDE_CHECK(world.Workshop.present.at[0] && world.Workshop.present.at[1]);
    TIDE_CHECK(tide_player_equal(world.Workshop.players.at[0], player(0)));
    TIDE_CHECK(tide_player_equal(world.Workshop.players.at[1], player(1)));
    TIDE_CHECK(inside_out_test_command(TIDE_COMMAND_ID_Move, 0, old_serial, 1));
    tide_world_tick(&world);
    TIDE_CHECK(memcmp(&before, &world.Workshop.puzzle, sizeof before) == 0);
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 0, 1));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.outside, tide_i2(2, 3)));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.inside, before.inside));
    world.Workshop.puzzle.solved = true;
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Next, 1, 0));
    TIDE_CHECK(world.Workshop.level == 1);
    TIDE_CHECK(world.Workshop.puzzle.moves == 0 && world.Workshop.historyCount == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_moving_a_room_carries_its_contents)
{
    fixture(1);
    crate(0, 0, 1, 2);
    world.Workshop.puzzle.outside = tide_i2(2, 3);
    const tide_int2 inside = world.Workshop.puzzle.inside;
    const Cargo contents = world.Workshop.puzzle.crates.at[0];
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 0, 1));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[0], tide_i2(4, 3)));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.outside, tide_i2(3, 3)));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.inside, inside));
    TIDE_CHECK(memcmp(&world.Workshop.puzzle.crates.at[0], &contents, sizeof contents) == 0);
    TIDE_CHECK(world.Workshop.historyCount == 1);
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Undo, 1, 0));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[0], tide_i2(3, 3)));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.outside, tide_i2(2, 3)));
    TIDE_CHECK(world.Workshop.historyCount == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_room_latch_needs_an_inside_switch_occupant)
{
    fixture(1);
    world.Workshop.puzzle.outside = tide_i2(2, 3);
    world.Workshop.layout.rooms.at[0].latched = true;
    world.Workshop.layout.rooms.at[0].tiles.at[cell(2, 1, 5)] = Tile_Switch;
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 0, 1));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[0], tide_i2(3, 3)));
    TIDE_CHECK(world.Workshop.historyCount == 0);
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 1, 0));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 0, 1));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[0], tide_i2(4, 3)));
    fixture(1);
    world.Workshop.puzzle.outside = tide_i2(2, 3);
    world.Workshop.layout.rooms.at[0].latched = true;
    world.Workshop.layout.rooms.at[0].tiles.at[cell(1, 2, 5)] = Tile_Switch;
    crate(0, 0, 1, 2);
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 0, 1));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[0], tide_i2(4, 3)));
    tide_world_free(&world);
}

TIDE_TEST(inside_out_matching_doorways_connect_and_push_at_the_entry)
{
    fixture(2);
    world.Workshop.puzzle.inside = tide_i2(4, 2);
    world.Workshop.layout.rooms.at[0].doors = 2;
    world.Workshop.layout.rooms.at[1].doors = 8;
    crate(0, 1, 0, 2);
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 1, 1));
    TIDE_CHECK(world.Workshop.puzzle.insideRoom == 1);
    TIDE_CHECK(same_cell(world.Workshop.puzzle.inside, tide_i2(0, 2)));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.crates.at[0].position, tide_i2(1, 2)));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Undo, 0, 0));
    world.Workshop.layout.rooms.at[1].doors = 1;
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 1, 1));
    TIDE_CHECK(world.Workshop.puzzle.insideRoom == 0);
    TIDE_CHECK(world.Workshop.historyCount == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_crates_change_scale_but_players_need_connected_rooms)
{
    fixture(1);
    world.Workshop.layout.rooms.at[0].doors = 2;
    world.Workshop.puzzle.inside = tide_i2(3, 2);
    crate(0, 0, 4, 2);
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 1, 1));
    TIDE_CHECK(world.Workshop.puzzle.crates.at[0].room == -1);
    TIDE_CHECK(same_cell(world.Workshop.puzzle.crates.at[0].position, tide_i2(4, 3)));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 1, 1));
    TIDE_CHECK(world.Workshop.puzzle.insideRoom == 0);
    TIDE_CHECK(same_cell(world.Workshop.puzzle.inside, tide_i2(4, 2)));
    world.Workshop.puzzle.outside = tide_i2(5, 3);
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 0, 3));
    TIDE_CHECK(world.Workshop.puzzle.crates.at[0].room == -1); // Entry occupied.
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 1, 0));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 0, 3));
    TIDE_CHECK(world.Workshop.puzzle.crates.at[0].room == 0);
    TIDE_CHECK(same_cell(world.Workshop.puzzle.crates.at[0].position, tide_i2(4, 2)));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.outside, tide_i2(4, 3)));
    tide_world_free(&world);
}

TIDE_TEST(inside_out_never_pushes_chains_or_exports_into_an_occupant)
{
    fixture(1);
    world.Workshop.layout.rooms.at[0].doors = 2;
    world.Workshop.puzzle.inside = tide_i2(3, 2);
    world.Workshop.puzzle.outside = tide_i2(4, 3);
    crate(0, 0, 4, 2);
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 1, 1));
    TIDE_CHECK(world.Workshop.puzzle.crates.at[0].room == 0);
    crate(1, 0, 2, 2);
    world.Workshop.puzzle.inside = tide_i2(1, 2);
    world.Workshop.puzzle.crates.at[0].position = tide_i2(3, 2);
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 1, 1));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.inside, tide_i2(1, 2)));
    TIDE_CHECK(world.Workshop.historyCount == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_hatch_swaps_roles_and_shared_undo_restores_them)
{
    fixture(1);
    world.Workshop.puzzle.outside = tide_i2(2, 3);
    world.Workshop.layout.rooms.at[0].tiles.at[cell(2, 2, 5)] = Tile_Hatch;
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Swap, 0, 0));
    TIDE_CHECK(world.Workshop.puzzle.smallSeat == 0);
    const tide_int2 outside = world.Workshop.puzzle.outside;
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 0, 0));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.outside, outside));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.inside, tide_i2(2, 1)));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Undo, 1, 0));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Undo, 1, 0));
    TIDE_CHECK(world.Workshop.puzzle.smallSeat == 1);
    TIDE_CHECK(world.Workshop.puzzle.moves == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_completion_requires_both_exits_docks_and_cargo_targets)
{
    fixture(1);
    world.Workshop.layout.tiles.at[cell(2, 3, 7)] = Tile_Exit;
    world.Workshop.layout.rooms.at[0].tiles.at[cell(2, 1, 5)] = Tile_Exit;
    world.Workshop.layout.rooms.at[0].dock = tide_i2(3, 3);
    world.Workshop.layout.rooms.at[0].tiles.at[cell(1, 2, 5)] = Tile_Target;
    crate(0, 0, 1, 3);
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 0, 1));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 1, 0));
    TIDE_CHECK(!world.Workshop.puzzle.solved);
    world.Workshop.puzzle.inside = tide_i2(1, 4);
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 1, 0));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.crates.at[0].position, tide_i2(1, 2)));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 1, 1));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 1, 0));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 1, 0));
    TIDE_CHECK(world.Workshop.puzzle.solved);
    TIDE_CHECK((world.Workshop.completed & 1) != 0);
    const Puzzle solved = world.Workshop.puzzle;
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 0, 3));
    TIDE_CHECK(memcmp(&solved, &world.Workshop.puzzle, sizeof solved) == 0);
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Undo, 0, 0));
    TIDE_CHECK(!world.Workshop.puzzle.solved);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_bounded_history_drops_only_oldest_steps)
{
    fixture(1);
    for (int i = 0; i < 140; i++) TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 0, i % 2 ? 3 : 1));
    TIDE_CHECK(world.Workshop.historyCount == 128);
    TIDE_CHECK(world.Workshop.puzzle.moves == 140);
    for (int i = 0; i < 130; i++) TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Undo, 1, 0));
    TIDE_CHECK(world.Workshop.historyCount == 0);
    TIDE_CHECK(world.Workshop.puzzle.moves == 12);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_snapshot_pack_and_replay_preserve_match_state)
{
    fixture(1);
    crate(0, 0, 1, 2);
    world.Workshop.puzzle.outside = tide_i2(2, 3);
    tide_world checkpoint = {0};
    tide_world_copy(&checkpoint, &world);
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 0, 1));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 1, 0));
    const uint64_t hash = tide_world_hash(&world);
    uint8_t *bytes = malloc(1024 * 1024);
    TIDE_REQUIRE(bytes != NULL);
    const uint32_t size = tide_world_pack(&world, bytes, 1024 * 1024);
    TIDE_CHECK(size > 0);
    tide_world restored = {0};
    TIDE_CHECK(tide_world_unpack(&restored, bytes, size));
    TIDE_CHECK(tide_world_hash(&restored) == hash);
    tide_world_copy(&world, &checkpoint);
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 0, 1));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 1, 0));
    TIDE_CHECK(tide_world_hash(&world) == hash);
    TIDE_CHECK(same_cell(checkpoint.Workshop.puzzle.rooms.at[0], tide_i2(3, 3)));
    free(bytes);
    tide_world_free(&restored);
    tide_world_free(&checkpoint);
    tide_world_free(&world);
}
