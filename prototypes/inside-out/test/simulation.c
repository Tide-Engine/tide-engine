// Replay real typed commands: sender validation, ordering and state use the
// same generated ABI as the two-browser game, with no duplicate rule engine.
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/events.h"
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
static void actor(const int seat, const int room, const int x, const int y)
{
    Traveler *at = &world.Workshop.puzzle.actors.at[seat];
    memset(at, 0, sizeof *at);
    at->room = room;
    at->position = tide_i2(x, y);
    for (int i = 0; i < 8; i++) at->trail.at[i] = (Entry){.box = -1};
    if (room >= 0) {
        at->depth = 1;
        at->trail.at[0] = (Entry){.box = room};
    }
}
static void fixture(const int rooms)
{
    inside_out_test_start(2);
    memset(&world.Workshop.layout, 0, sizeof world.Workshop.layout);
    memset(&world.Workshop.puzzle, 0, sizeof world.Workshop.puzzle);
    world.Workshop.historyCount = 0;
    world.Workshop.layout.width = 7;
    world.Workshop.layout.height = 7;
    world.Workshop.layout.roomCount = rooms;
    for (int i = 0; i < 4; i++) {
        world.Workshop.layout.rooms.at[i].width = 5;
        world.Workshop.layout.rooms.at[i].height = 5;
        world.Workshop.layout.rooms.at[i].north = -1;
        world.Workshop.layout.rooms.at[i].east = -1;
        world.Workshop.layout.rooms.at[i].south = -1;
        world.Workshop.layout.rooms.at[i].west = -1;
        world.Workshop.layout.rooms.at[i].dock = tide_i2(-1, -1);
        world.Workshop.layout.rooms.at[i].dockRoom = -1;
        world.Workshop.layout.rooms.at[i].doors = 15;
        world.Workshop.puzzle.rooms.at[i] = (RoomShell){.room = -1, .position = {3 + i % 2, 3 + 2 * (i / 2)}};
    }
    actor(0, -1, 1, 3);
    actor(1, -1, 1, 5);
}
static void crate(const int index, const int room, const int x, const int y)
{
    world.Workshop.layout.crateCount = index + 1;
    world.Workshop.puzzle.crates.at[index] = (Cargo){.room = room, .position = {x, y}, .active = true};
}
static void move(const int seat, const int direction)
{
    const int before = world.Workshop.puzzle.moves;
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, seat, direction));
    TIDE_CHECK(world.Workshop.puzzle.moves == before + 1);
}
static void blocked(const int seat, const int direction)
{
    const Puzzle before = world.Workshop.puzzle;
    const int history = world.Workshop.historyCount;
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, seat, direction));
    TIDE_CHECK(memcmp(&before, &world.Workshop.puzzle, sizeof before) == 0);
    TIDE_CHECK(world.Workshop.historyCount == history);
}

TIDE_TEST(inside_out_name_hold_waits_for_partner_and_same_level_retries_are_quick)
{
    inside_out_test_start(1);
    TIDE_REQUIRE(world.Workshop.initialized && world.Workshop.introTicks == 114);
    for (int tick = 0; tick < 180; tick++) tide_world_tick(&world);
    const int joined_at = world.Time.tick;
    tide_world_player_joined(&world, player(1));
    tide_world_tick(&world);
    TIDE_REQUIRE(world.Workshop.present.at[1]);
    TIDE_CHECK(world.Workshop.openedTick == joined_at && world.Workshop.introTicks == 114);
    TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_Reset, 0, 0));
    TIDE_CHECK(world.Workshop.introTicks == 0);
    TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 0, 0));
    TIDE_CHECK(world.Workshop.introTicks == 0);
    TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 0, 1));
    TIDE_CHECK(world.Workshop.introTicks == 114);
    TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_Reset, 0, 0));
    TIDE_CHECK(world.Workshop.introTicks == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_rejoining_a_moved_room_does_not_restart_its_name_hold)
{
    inside_out_test_start(2);
    TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_Move, 0, 1));
    TIDE_REQUIRE(world.Workshop.puzzle.moves == 1);
    const int opened_at = world.Workshop.openedTick;
    tide_world_player_left(&world, player(1));
    tide_world_tick(&world);
    for (int tick = 0; tick < 180; tick++) tide_world_tick(&world);
    tide_world_player_joined(&world, player(1));
    tide_world_tick(&world);
    TIDE_CHECK(world.Workshop.present.at[1] && world.Workshop.openedTick == opened_at);
    tide_world_free(&world);
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
    TIDE_CHECK(!world.Workshop.present.at[1] && world.Workshop.serial != serial);
    const Puzzle paused = world.Workshop.puzzle;
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Reset, 0, 0));
    TIDE_CHECK(memcmp(&paused, &world.Workshop.puzzle, sizeof paused) == 0);
    tide_world_player_joined(&world, player(1));
    tide_world_tick(&world);
    TIDE_CHECK(world.Workshop.present.at[1]);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_rejects_stale_malformed_and_forged_actions)
{
    fixture(1);
    const Puzzle before = world.Workshop.puzzle;
    TIDE_CHECK(inside_out_test_command(TIDE_COMMAND_ID_Move, 0, world.Workshop.serial - 1, 1));
    tide_world_tick(&world);
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 0, -1));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Move, 1, INT32_MAX));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 0, -1));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 1, INT32_MAX));
    TIDE_CHECK(!inside_out_test_command(TIDE_COMMAND_ID_Move, 2, world.Workshop.serial, 1));
    tide_world_tick(&world);
    TIDE_CHECK(memcmp(&before, &world.Workshop.puzzle, sizeof before) == 0);
    TIDE_CHECK(world.Workshop.historyCount == 0 && world.Workshop.level == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_rejoin_keeps_independent_seats_and_serial)
{
    fixture(1);
    const Puzzle before = world.Workshop.puzzle;
    const int old_serial = world.Workshop.serial;
    tide_world_player_left(&world, player(0));
    tide_world_player_left(&world, player(1));
    tide_world_tick(&world);
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Reset, 1, 0));
    tide_world_player_joined(&world, player(1));
    tide_world_player_joined(&world, player(0));
    tide_world_tick(&world);
    TIDE_CHECK(tide_player_equal(world.Workshop.players.at[0], player(0)));
    TIDE_CHECK(tide_player_equal(world.Workshop.players.at[1], player(1)));
    TIDE_CHECK(inside_out_test_command(TIDE_COMMAND_ID_Move, 0, old_serial, 1));
    tide_world_tick(&world);
    TIDE_CHECK(memcmp(&before, &world.Workshop.puzzle, sizeof before) == 0);
    move(0, 1);
    TIDE_CHECK(same_cell(world.Workshop.puzzle.actors.at[0].position, tide_i2(2, 3)));
    TIDE_CHECK(memcmp(&before.actors.at[1], &world.Workshop.puzzle.actors.at[1], sizeof(Traveler)) == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_room_push_is_automatic_and_only_blocked_pushes_enter)
{
    fixture(1);
    actor(0, -1, 2, 3);
    world.Workshop.layout.rooms.at[0].doors = 0;
    const Puzzle initial = world.Workshop.puzzle;
    move(0, 1); // A push needs space, rather than an open facing doorway.
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].room == -1);
    TIDE_CHECK(same_cell(world.Workshop.puzzle.actors.at[0].position, tide_i2(3, 3)));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(4, 3)));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Undo, 0, 0));
    TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
    world.Workshop.layout.rooms.at[0].doors = 15;
    world.Workshop.layout.tiles.at[cell(4, 3, 7)] = Tile_Wall;
    move(0, 1);
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].room == 0 && world.Workshop.puzzle.actors.at[0].depth == 1);
    TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(3, 3)));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Undo, 1, 0));
    world.Workshop.layout.tiles.at[cell(4, 3, 7)] = Tile_Floor;
    actor(1, -1, 4, 3); // An occupied push destination also permits entry.
    const Traveler partner = world.Workshop.puzzle.actors.at[1];
    move(0, 1);
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].room == 0);
    TIDE_CHECK(memcmp(&partner, &world.Workshop.puzzle.actors.at[1], sizeof partner) == 0);
    TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(3, 3)));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Undo, 1, 0));
    world.Workshop.layout.rooms.at[0].tiles.at[cell(0, 2, 5)] = Tile_Wall;
    blocked(0, 1); // Both possible interpretations fail atomically.
    tide_world_free(&world);
}

TIDE_TEST(inside_out_fallback_entry_pushes_doorway_cargo_atomically)
{
    fixture(1);
    actor(0, -1, 2, 3);
    world.Workshop.layout.tiles.at[cell(4, 3, 7)] = Tile_Wall;
    crate(0, 0, 0, 2);
    const Puzzle initial = world.Workshop.puzzle;
    move(0, 1);
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].room == 0 && world.Workshop.puzzle.actors.at[0].depth == 1);
    TIDE_CHECK(same_cell(world.Workshop.puzzle.actors.at[0].position, tide_i2(0, 2)));
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].trail.at[0].box == 0);
    TIDE_CHECK(same_cell(world.Workshop.puzzle.crates.at[0].position, tide_i2(1, 2)));
    TIDE_CHECK(memcmp(&initial.rooms, &world.Workshop.puzzle.rooms, sizeof initial.rooms) == 0);
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Undo, 1, 0));
    TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
    world.Workshop.layout.rooms.at[0].tiles.at[cell(1, 2, 5)] = Tile_Wall;
    blocked(0, 1);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_both_travelers_enter_share_and_leave_the_same_room)
{
    fixture(1);
    world.Workshop.layout.tiles.at[cell(4, 3, 7)] = Tile_Wall;
    actor(0, -1, 2, 3);
    actor(1, -1, 2, 4);
    move(0, 1);
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].room == 0 && world.Workshop.puzzle.actors.at[0].depth == 1);
    move(1, 0);
    blocked(1, 1); // The shared doorway is occupied by the other maker.
    move(0, 1);
    move(1, 1);
    blocked(1, 1);
    move(0, 0);
    move(1, 1);
    move(1, 3);
    move(1, 3);
    TIDE_CHECK(world.Workshop.puzzle.actors.at[1].room == -1 && world.Workshop.puzzle.actors.at[1].depth == 0);
    TIDE_CHECK(same_cell(world.Workshop.puzzle.actors.at[1].position, tide_i2(2, 3)));
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].room == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_simultaneous_entries_obey_shared_occupancy_and_undo)
{
    fixture(1);
    world.Workshop.layout.tiles.at[cell(4, 3, 7)] = Tile_Wall;
    actor(0, -1, 2, 3);
    actor(1, 0, 1, 2);
    const Puzzle before = world.Workshop.puzzle;
    // One bot enters by the west doorway while the other walks onto the same
    // doorway cell from inside. Both players' reliable commands arrive
    // before one authoritative tick, and only the first is taken.
    TIDE_CHECK(inside_out_test_command(TIDE_COMMAND_ID_Move, 0, world.Workshop.serial, 1));
    TIDE_CHECK(inside_out_test_command(TIDE_COMMAND_ID_Move, 1, world.Workshop.serial, 3));
    tide_world_tick(&world);
    TIDE_CHECK(world.Workshop.puzzle.moves == 1 && world.Workshop.historyCount == 1);
    const Traveler *const first = &world.Workshop.puzzle.actors.at[0];
    const Traveler *const second = &world.Workshop.puzzle.actors.at[1];
    TIDE_CHECK((first->room == 0 && same_cell(first->position, tide_i2(0, 2))) !=
        (second->room == 0 && same_cell(second->position, tide_i2(0, 2))));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Undo, 1, 0));
    TIDE_CHECK(memcmp(&before, &world.Workshop.puzzle, sizeof before) == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_automatic_push_carries_nested_contents_and_updates_exit_anchor)
{
    fixture(2);
    world.Workshop.puzzle.rooms.at[1] = (RoomShell){.room = 0, .position = {2, 2}};
    actor(0, -1, 2, 3);
    actor(1, 1, 0, 2);
    world.Workshop.puzzle.actors.at[1].depth = 2;
    world.Workshop.puzzle.actors.at[1].trail.at[0].box = 0;
    world.Workshop.puzzle.actors.at[1].trail.at[1] = (Entry){.box = 1};
    crate(0, 1, 3, 2);
    const Traveler passenger = world.Workshop.puzzle.actors.at[1];
    const Cargo contents = world.Workshop.puzzle.crates.at[0];
    move(0, 1);
    TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(4, 3)));
    TIDE_CHECK(memcmp(&passenger, &world.Workshop.puzzle.actors.at[1], sizeof passenger) == 0);
    TIDE_CHECK(memcmp(&contents, &world.Workshop.puzzle.crates.at[0], sizeof contents) == 0);
    move(1, 3);
    TIDE_CHECK(world.Workshop.puzzle.actors.at[1].room == 0 && world.Workshop.puzzle.actors.at[1].depth == 1);
    world.Workshop.puzzle.actors.at[1].position = tide_i2(0, 2);
    blocked(1, 3); // Exit would land on the pusher.
    move(0, 0);
    move(1, 3);
    TIDE_CHECK(world.Workshop.puzzle.actors.at[1].room == -1);
    TIDE_CHECK(same_cell(world.Workshop.puzzle.actors.at[1].position, tide_i2(3, 3)));
    tide_world_free(&world);
}

TIDE_TEST(inside_out_shell_export_discards_stale_deep_parent_trail)
{
    fixture(2);
    world.Workshop.puzzle.rooms.at[0] = (RoomShell){.room = 1, .position = {4, 2}};
    actor(0, 1, 3, 2);
    actor(1, 0, 2, 0);
    world.Workshop.puzzle.actors.at[1].depth = 3;
    world.Workshop.puzzle.actors.at[1].trail.at[0].box = 1;
    world.Workshop.puzzle.actors.at[1].trail.at[1] = (Entry){.box = 4};
    world.Workshop.puzzle.actors.at[1].trail.at[2] = (Entry){.box = 0};
    move(0, 1);
    TIDE_CHECK(world.Workshop.puzzle.rooms.at[0].room == -1);
    TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(5, 3)));
    move(1, 0);
    TIDE_CHECK(world.Workshop.puzzle.actors.at[1].room == -1 && world.Workshop.puzzle.actors.at[1].depth == 0);
    TIDE_CHECK(same_cell(world.Workshop.puzzle.actors.at[1].position, tide_i2(5, 2)));
    tide_world_free(&world);
}

TIDE_TEST(inside_out_partner_backstop_enables_entry_and_releases_room_motion)
{
    fixture(1);
    actor(0, -1, 2, 3);
    actor(1, -1, 4, 3);
    crate(0, 0, 2, 2);
    const Puzzle initial = world.Workshop.puzzle;
    const RoomShell parked = world.Workshop.puzzle.rooms.at[0];
    move(0, 1); // The partner occupies the pushing destination.
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].room == 0);
    TIDE_CHECK(memcmp(&parked, &world.Workshop.puzzle.rooms.at[0], sizeof parked) == 0);
    move(1, 0);
    move(0, 3);
    move(0, 1);
    TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(4, 3)));
    TIDE_CHECK(memcmp(&initial.crates, &world.Workshop.puzzle.crates, sizeof initial.crates) == 0);
    for (int step = 0; step < 4; step++) TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Undo, 1, 0));
    TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_rebased_self_exit_emits_real_source_and_travel_direction)
{
    fixture(1);
    world.Workshop.puzzle.rooms.at[0] = (RoomShell){.room = 0, .position = {0, 2}};
    actor(0, 0, 4, 2);
    const Traveler source = world.Workshop.puzzle.actors.at[0];
    tide_event_log events = {0};
    TIDE_CHECK(inside_out_test_command(TIDE_COMMAND_ID_Move, 0, world.Workshop.serial, 1));
    tide_world_tick_events(&world, NULL, &events);
    TIDE_REQUIRE(events.count == 1);
    uint32_t offset = 0;
    const tide_heard *heard = tide_event_log_next(&events, &offset);
    TIDE_REQUIRE(heard != NULL && heard->size >= sizeof *heard + sizeof(PuzzleCue));
    PuzzleCue cue;
    memcpy(&cue, heard + 1, sizeof cue);
    TIDE_CHECK(cue.kind == 2 && cue.seat == 0 && cue.direction == 1);
    TIDE_CHECK(memcmp(&source, &cue.source, sizeof source) == 0);
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].room == source.room && world.Workshop.puzzle.actors.at[0].depth == source.depth);
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].trail.at[0].box == source.trail.at[0].box);
    TIDE_CHECK(same_cell(world.Workshop.puzzle.actors.at[0].position, tide_i2(1, 2)));
    tide_event_log_clear(&events);
    TIDE_CHECK(inside_out_test_command(TIDE_COMMAND_ID_Move, 0, world.Workshop.serial, 0));
    tide_world_tick_events(&world, NULL, &events);
    TIDE_REQUIRE(events.count == 1);
    offset = 0;
    heard = tide_event_log_next(&events, &offset);
    TIDE_REQUIRE(heard != NULL && heard->size >= sizeof *heard + sizeof(PuzzleCue));
    memcpy(&cue, heard + 1, sizeof cue);
    TIDE_CHECK(cue.kind == 0 && cue.direction == -1);
    tide_event_log_free(&events);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_rectangular_rooms_use_all_four_offset_doorways)
{
    const tide_int2 deltas[4] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    const tide_int2 entries[4] = {{0, 0}, {2, 1}, {2, 4}, {0, 3}};
    for (int side = 0; side < 4; side++) {
        fixture(1);
        RoomLayout *room = &world.Workshop.layout.rooms.at[0];
        room->width = 3;
        room->height = 5;
        room->north = 0;
        room->east = 1;
        room->south = 2;
        room->west = 3;
        const tide_int2 outside = tide_i2(3 + deltas[side].x, 3 + deltas[side].y);
        actor(0, -1, outside.x, outside.y);
        world.Workshop.layout.tiles.at[cell(3 - deltas[side].x, 3 - deltas[side].y, 7)] = Tile_Wall;
        const Puzzle initial = world.Workshop.puzzle;
        move(0, (side + 2) % 4);
        TIDE_CHECK(world.Workshop.puzzle.actors.at[0].room == 0);
        TIDE_CHECK(same_cell(world.Workshop.puzzle.actors.at[0].position, entries[side]));
        move(0, side);
        TIDE_CHECK(world.Workshop.puzzle.actors.at[0].room == -1 && world.Workshop.puzzle.actors.at[0].depth == 0);
        TIDE_CHECK(same_cell(world.Workshop.puzzle.actors.at[0].position, outside));
        TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Undo, 1, 0));
        TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Undo, 1, 0));
        TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
        tide_world_free(&world);
    }
}

TIDE_TEST(inside_out_narrow_nested_start_returns_outward_and_undo_restores_it)
{
    fixture(3);
    world.Workshop.layout.rooms.at[0].width = 3;
    world.Workshop.layout.rooms.at[0].height = 5;
    world.Workshop.layout.rooms.at[0].west = 2;
    world.Workshop.layout.rooms.at[1].width = 5;
    world.Workshop.layout.rooms.at[1].height = 3;
    world.Workshop.layout.rooms.at[1].north = 4;
    world.Workshop.layout.rooms.at[2].width = 2;
    world.Workshop.layout.rooms.at[2].height = 4;
    world.Workshop.layout.rooms.at[2].north = 1;
    world.Workshop.puzzle.rooms.at[1] = (RoomShell){.room = 0, .position = {0, 3}};
    world.Workshop.puzzle.rooms.at[2] = (RoomShell){.room = 1, .position = {4, 1}};
    actor(0, 2, 1, 0);
    actor(1, -1, 0, 0);
    world.Workshop.puzzle.actors.at[0].depth = 3;
    for (int entry = 0; entry < 3; entry++)
        world.Workshop.puzzle.actors.at[0].trail.at[entry] = (Entry){.box = entry};
    const Puzzle initial = world.Workshop.puzzle;
    move(0, 0);
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].room == 1 && world.Workshop.puzzle.actors.at[0].depth == 2);
    TIDE_CHECK(same_cell(world.Workshop.puzzle.actors.at[0].position, tide_i2(4, 0)));
    move(0, 0);
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].room == 0 && world.Workshop.puzzle.actors.at[0].depth == 1);
    TIDE_CHECK(same_cell(world.Workshop.puzzle.actors.at[0].position, tide_i2(0, 2)));
    move(0, 3);
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].room == -1 && world.Workshop.puzzle.actors.at[0].depth == 0);
    TIDE_CHECK(same_cell(world.Workshop.puzzle.actors.at[0].position, tide_i2(2, 3)));
    for (int step = 0; step < 3; step++) TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Undo, 1, 0));
    TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_rectangular_completion_ignores_unused_capacity)
{
    fixture(2);
    world.Workshop.layout.width = 3;
    world.Workshop.layout.height = 4;
    world.Workshop.layout.rooms.at[0].width = 6;
    world.Workshop.layout.rooms.at[0].height = 6;
    world.Workshop.layout.rooms.at[1].width = 2;
    world.Workshop.layout.rooms.at[1].height = 7;
    world.Workshop.puzzle.rooms.at[0].position = tide_i2(1, 1);
    world.Workshop.puzzle.rooms.at[1].position = tide_i2(1, 2);
    actor(0, -1, 0, 0);
    actor(1, 0, 5, 1);
    world.Workshop.layout.tiles.at[cell(0, 0, 3)] = Tile_Exit;
    world.Workshop.layout.rooms.at[0].tiles.at[cell(5, 0, 6)] = Tile_Exit;
    world.Workshop.layout.tiles.at[cell(2, 3, 3)] = Tile_Target;
    world.Workshop.layout.rooms.at[0].tiles.at[cell(5, 5, 6)] = Tile_Target;
    world.Workshop.layout.rooms.at[0].tiles.at[cell(4, 5, 6)] = Tile_Target;
    world.Workshop.layout.tiles.at[48] = Tile_Target;
    world.Workshop.layout.rooms.at[0].tiles.at[48] = Tile_Target;
    world.Workshop.layout.rooms.at[1].tiles.at[48] = Tile_Target;
    crate(0, -1, 2, 3);
    crate(1, 0, 5, 5);
    move(1, 0);
    TIDE_CHECK(!world.Workshop.puzzle.solved); // An actual target beyond the old 25-cell capacity is empty.
    crate(2, 0, 4, 5);
    move(1, 2);
    move(1, 0);
    TIDE_CHECK(world.Workshop.puzzle.solved); // Padded capacity does not introduce phantom targets.
    tide_world_free(&world);
}

TIDE_TEST(inside_out_dimensions_and_invalid_offsets_remain_bounded)
{
    fixture(1);
    world.Workshop.layout.width = INT32_MAX;
    world.Workshop.layout.height = INT32_MIN;
    world.Workshop.layout.rooms.at[0].width = INT32_MIN;
    world.Workshop.layout.rooms.at[0].height = INT32_MAX;
    world.Workshop.puzzle.rooms.at[0].position = tide_i2(3, 0);
    actor(0, -1, 5, 0);
    actor(1, 0, 0, 5);
    move(0, 1);
    blocked(0, 1);
    move(1, 2);
    blocked(1, 2); // The containing root has one row after dimension repair.
    actor(0, -1, 4, 0);
    world.Workshop.layout.tiles.at[cell(2, 0, 7)] = Tile_Wall;
    world.Workshop.layout.rooms.at[0].east = INT32_MAX;
    blocked(0, 3);
    world.Workshop.layout.rooms.at[0].east = -2;
    blocked(0, 3);
    world.Workshop.layout.rooms.at[0].east = -1;
    move(0, 3);
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].room == 0);
    TIDE_CHECK(same_cell(world.Workshop.puzzle.actors.at[0].position, tide_i2(0, 3)));
    tide_world_free(&world);
}

TIDE_TEST(inside_out_completion_checks_distinct_exits_nested_docks_and_all_cargo)
{
    fixture(2);
    actor(0, -1, 2, 3);
    actor(1, 0, 2, 2);
    world.Workshop.layout.tiles.at[cell(2, 3, 7)] = Tile_Exit;
    world.Workshop.layout.rooms.at[0].tiles.at[cell(2, 1, 5)] = Tile_Exit;
    world.Workshop.layout.rooms.at[1].dock = tide_i2(3, 3);
    world.Workshop.layout.rooms.at[1].dockRoom = 0;
    world.Workshop.puzzle.rooms.at[1] = (RoomShell){.room = -1, .position = {3, 3}};
    crate(0, 1, 1, 2);
    world.Workshop.layout.rooms.at[1].tiles.at[cell(1, 2, 5)] = Tile_Target;
    move(1, 0);
    TIDE_CHECK(!world.Workshop.puzzle.solved); // Dock coordinate in wrong dimension.
    world.Workshop.puzzle.rooms.at[1].room = 0;
    move(1, 2);
    move(1, 0);
    TIDE_CHECK(world.Workshop.puzzle.solved && (world.Workshop.completed & 1) != 0);
    blocked(0, 3);
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Undo, 0, 0));
    TIDE_CHECK(!world.Workshop.puzzle.solved);
    world.Workshop.layout.rooms.at[1].tiles.at[cell(1, 2, 5)] = Tile_Floor;
    move(1, 0);
    TIDE_CHECK(!world.Workshop.puzzle.solved); // An un-targeted active crate matters.
    tide_world_free(&world);
}

TIDE_TEST(inside_out_bounded_history_drops_only_oldest_steps)
{
    fixture(1);
    for (int i = 0; i < 140; i++) move(0, i % 2 ? 3 : 1);
    TIDE_CHECK(world.Workshop.historyCount == 128 && world.Workshop.puzzle.moves == 140);
    for (int i = 0; i < 130; i++) TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Undo, 1, 0));
    TIDE_CHECK(world.Workshop.historyCount == 0 && world.Workshop.puzzle.moves == 12);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_snapshot_pack_and_replay_preserve_recursive_match_state)
{
    fixture(1);
    actor(0, 0, 2, 2);
    // The room's own box stands inside it, against a wall: the bot enters it.
    world.Workshop.puzzle.rooms.at[0] = (RoomShell){.room = 0, .position = {3, 2}};
    world.Workshop.layout.rooms.at[0].tiles.at[cell(4, 2, 5)] = Tile_Wall;
    crate(0, 0, 2, 3);
    tide_world checkpoint = {0};
    tide_world_copy(&checkpoint, &world);
    move(0, 1);
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].room == 0 && same_cell(world.Workshop.puzzle.actors.at[0].position, tide_i2(0, 2)));
    move(1, 1);
    const uint64_t hash = tide_world_hash(&world);
    uint8_t *bytes = malloc(1024 * 1024);
    TIDE_REQUIRE(bytes != NULL);
    const uint32_t size = tide_world_pack(&world, bytes, 1024 * 1024);
    TIDE_CHECK(size > 0);
    tide_world restored = {0};
    TIDE_CHECK(tide_world_unpack(&restored, bytes, size));
    TIDE_CHECK(tide_world_hash(&restored) == hash);
    tide_world_copy(&world, &checkpoint);
    move(0, 1);
    move(1, 1);
    TIDE_CHECK(tide_world_hash(&world) == hash);
    TIDE_CHECK(checkpoint.Workshop.puzzle.actors.at[0].depth == 1);
    free(bytes);
    tide_world_free(&restored);
    tide_world_free(&checkpoint);
    tide_world_free(&world);
}

// The rules as of the push-or-enter rewrite: one rule for bots, parcels and
// boxes, rows of any length, and only a wall or a bot to stop one. Since
// 2026-10-16 a box pushed at a stopped parcel or box takes it in by its own
// facing doorway.

TIDE_TEST(inside_out_a_row_of_any_length_is_pushed_and_only_a_wall_stops_it)
{
    fixture(2);
    crate(0, -1, 5, 3);
    actor(0, -1, 2, 3);
    const Puzzle initial = world.Workshop.puzzle;
    move(0, 1); // Two boxes and a parcel in a row all move.
    TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(4, 3)));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[1].position, tide_i2(5, 3)));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.crates.at[0].position, tide_i2(6, 3)));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.actors.at[0].position, tide_i2(3, 3)));
    // The parcel is against the edge now and is no box: the second box takes
    // it in by its east door and moves onto its cell, the row behind it too.
    move(0, 1);
    TIDE_CHECK(world.Workshop.puzzle.crates.at[0].room == 1 && same_cell(world.Workshop.puzzle.crates.at[0].position, tide_i2(4, 2)));
    TIDE_CHECK(world.Workshop.puzzle.rooms.at[1].room == -1 && same_cell(world.Workshop.puzzle.rooms.at[1].position, tide_i2(6, 3)));
    TIDE_CHECK(world.Workshop.puzzle.rooms.at[0].room == -1 && same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(5, 3)));
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].room == -1 && same_cell(world.Workshop.puzzle.actors.at[0].position, tide_i2(4, 3)));
    // Now the second box is against the edge: the first goes into it by its
    // west door.
    move(0, 1);
    TIDE_CHECK(world.Workshop.puzzle.rooms.at[0].room == 1 && same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(0, 2)));
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].room == -1 && same_cell(world.Workshop.puzzle.actors.at[0].position, tide_i2(5, 3)));
    // Then the bot is at the second box, which cannot move: it goes in, and
    // pushes the first box on across the room.
    move(0, 1);
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].room == 1 && same_cell(world.Workshop.puzzle.actors.at[0].position, tide_i2(0, 2)));
    TIDE_CHECK(world.Workshop.puzzle.rooms.at[0].room == 1 && same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(1, 2)));
    for (int step = 0; step < 4; step++) TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Undo, 1, 0));
    TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_only_the_other_bot_can_be_put_in_a_box_s_way)
{
    fixture(1);
    actor(0, -1, 2, 3);
    crate(0, -1, 4, 3);
    move(0, 1); // A parcel behind a box holds nothing: both move.
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].room == -1);
    TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(4, 3)));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.crates.at[0].position, tide_i2(5, 3)));
    TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Undo, 0, 0));
    world.Workshop.layout.crateCount = 0;
    actor(1, -1, 4, 3); // The partner behind it is a wall: the bot goes in.
    const Traveler partner = world.Workshop.puzzle.actors.at[1];
    move(0, 1);
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].room == 0 && same_cell(world.Workshop.puzzle.actors.at[0].position, tide_i2(0, 2)));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(3, 3)));
    TIDE_CHECK(memcmp(&partner, &world.Workshop.puzzle.actors.at[1], sizeof partner) == 0);
    // A bot is never pushed, by a bot or by what a bot pushes.
    actor(0, -1, 2, 3);
    world.Workshop.layout.rooms.at[0].doors = 0;
    blocked(0, 1);
    tide_world_free(&world);
}

// Real recursion: a room's one box can stand inside that room, or two boxes
// inside each other. There is no other way for a room to appear twice.

static bool bot_at(const int seat, const int room, const int x, const int y)
{
    const Traveler *const bot = &world.Workshop.puzzle.actors.at[seat];
    return bot->room == room && same_cell(bot->position, tide_i2(x, y));
}
static void undo_all(const int moves)
{
    for (int step = 0; step < moves; step++) TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Undo, step % 2, 0));
}

TIDE_TEST(inside_out_a_box_inside_its_own_room_is_left_beside_itself_and_entered_by_its_doors)
{
    fixture(1);
    // Room 0's box stands in the middle of room 0, with wall west and north
    // of it, so it cannot be pushed west or north.
    world.Workshop.puzzle.rooms.at[0] = (RoomShell){.room = 0, .position = {2, 2}};
    world.Workshop.layout.rooms.at[0].tiles.at[cell(1, 2, 5)] = Tile_Wall;
    world.Workshop.layout.rooms.at[0].tiles.at[cell(2, 1, 5)] = Tile_Wall;
    actor(0, 0, 4, 2);
    const Puzzle initial = world.Workshop.puzzle;
    move(0, 1); // Out by the east door: beside the box, in the same room.
    TIDE_CHECK(bot_at(0, 0, 3, 2));
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].depth == 1 && world.Workshop.puzzle.actors.at[0].trail.at[0].box == 0);
    move(0, 3); // The box cannot move west, so the bot goes in by its east door.
    TIDE_CHECK(bot_at(0, 0, 4, 2));
    TIDE_CHECK(memcmp(&initial.rooms, &world.Workshop.puzzle.rooms, sizeof initial.rooms) == 0);
    move(0, 2);
    move(0, 3);
    move(0, 3);
    TIDE_CHECK(bot_at(0, 0, 2, 3));
    move(0, 0); // Into it from the south: in at the south door cell.
    TIDE_CHECK(bot_at(0, 0, 2, 4));
    move(0, 2); // And out by the south door, beside it again.
    TIDE_CHECK(bot_at(0, 0, 2, 3));
    move(0, 3);
    move(0, 3);
    move(0, 0);
    TIDE_CHECK(bot_at(0, 0, 0, 2));
    blocked(0, 3); // Out by the west door would be the wall beside the box.
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].depth == 1 && world.Workshop.puzzle.actors.at[0].trail.at[0].box == 0);
    undo_all(10);
    TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_a_battery_pushed_out_of_a_room_holding_its_own_box_lands_beside_it)
{
    fixture(1);
    world.Workshop.puzzle.rooms.at[0] = (RoomShell){.room = 0, .position = {2, 2}};
    actor(0, 0, 3, 2);
    crate(0, 0, 4, 2); // In the east doorway.
    // Out of the east door is the cell east of the box, where the bot that
    // pushes stands: the row comes round onto itself and nothing moves.
    blocked(0, 1);
    world.Workshop.puzzle.rooms.at[0].position = tide_i2(1, 2);
    const Puzzle initial = world.Workshop.puzzle;
    move(0, 1); // Now it comes out beside the box, in the same room.
    TIDE_CHECK(world.Workshop.puzzle.crates.at[0].room == 0 && same_cell(world.Workshop.puzzle.crates.at[0].position, tide_i2(2, 2)));
    TIDE_CHECK(bot_at(0, 0, 4, 2));
    move(0, 1); // So does the bot, pushing the battery on.
    TIDE_CHECK(bot_at(0, 0, 2, 2));
    TIDE_CHECK(world.Workshop.puzzle.crates.at[0].room == 0 && same_cell(world.Workshop.puzzle.crates.at[0].position, tide_i2(3, 2)));
    TIDE_CHECK(memcmp(&initial.rooms, &world.Workshop.puzzle.rooms, sizeof initial.rooms) == 0);
    undo_all(2);
    TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_a_box_pushed_within_itself_carries_its_room_and_is_left_beside_its_new_place)
{
    fixture(1);
    world.Workshop.puzzle.rooms.at[0] = (RoomShell){.room = 0, .position = {2, 2}};
    actor(0, 0, 1, 2);
    actor(1, 0, 0, 4);
    crate(0, 0, 4, 4);
    const Puzzle initial = world.Workshop.puzzle;
    // The box is pushed east within its own room. Everything in that room is
    // inside the box and goes with it, so none of it moves within the room:
    // the partner and the battery keep their cells, and the bot that pushed,
    // inside the box too, takes the one step of any push.
    move(0, 1);
    TIDE_CHECK(world.Workshop.puzzle.rooms.at[0].room == 0 && same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(3, 2)));
    TIDE_CHECK(bot_at(0, 0, 2, 2));
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].depth == 1 && world.Workshop.puzzle.actors.at[0].trail.at[0].box == 0);
    TIDE_CHECK(memcmp(&initial.actors.at[1], &world.Workshop.puzzle.actors.at[1], sizeof(Traveler)) == 0);
    TIDE_CHECK(memcmp(&initial.crates, &world.Workshop.puzzle.crates, sizeof initial.crates) == 0);
    move(0, 0);
    move(0, 0);
    move(0, 0); // Out by the north door: beside the box where it stands now.
    TIDE_CHECK(bot_at(0, 0, 3, 1));
    move(1, 0);
    move(1, 0);
    move(1, 3); // Out by the west door: beside it on the other side.
    TIDE_CHECK(bot_at(1, 0, 2, 2));
    undo_all(7);
    TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_two_boxes_inside_each_other_are_left_into_each_other)
{
    fixture(2);
    // Room 0's box stands in room 1 and room 1's in room 0: each room is
    // inside the other, and neither is on the table.
    world.Workshop.puzzle.rooms.at[0] = (RoomShell){.room = 1, .position = {2, 2}};
    world.Workshop.puzzle.rooms.at[1] = (RoomShell){.room = 0, .position = {2, 2}};
    actor(0, 0, 4, 2);
    actor(1, 1, 0, 2);
    // Each bot's trail as the game keeps it: the room around it, then its own.
    for (int seat = 0; seat < 2; seat++) {
        Traveler *const bot = &world.Workshop.puzzle.actors.at[seat];
        bot->depth = 2;
        bot->trail.at[0] = (Entry){.box = 1 - bot->room};
        bot->trail.at[1] = (Entry){.box = bot->room};
    }
    const Puzzle initial = world.Workshop.puzzle;
    move(0, 1); // Out of room 0: beside its box, which is in room 1.
    TIDE_CHECK(bot_at(0, 1, 3, 2));
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].depth == 2);
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].trail.at[0].box == 0 && world.Workshop.puzzle.actors.at[0].trail.at[1].box == 1);
    move(1, 3); // Out of room 1: beside its box, which is in room 0.
    TIDE_CHECK(bot_at(1, 0, 1, 2));
    TIDE_CHECK(world.Workshop.puzzle.actors.at[1].depth == 2);
    TIDE_CHECK(world.Workshop.puzzle.actors.at[1].trail.at[0].box == 1 && world.Workshop.puzzle.actors.at[1].trail.at[1].box == 0);
    move(0, 1);
    move(0, 1); // On out of room 1, back into room 0: round the cycle.
    TIDE_CHECK(bot_at(0, 0, 3, 2));
    // Room 1's box is held by seat 0 standing behind it, so seat 1 goes in.
    move(1, 1);
    TIDE_CHECK(bot_at(1, 1, 0, 2));
    TIDE_CHECK(memcmp(&initial.rooms, &world.Workshop.puzzle.rooms, sizeof initial.rooms) == 0);
    undo_all(5);
    TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
    tide_world_free(&world);
}

// Plates and shutters: a plate is pressed by anything on it and powers its
// channel's shutters in every room. An occupied shutter stays lowered, and
// only an empty unpowered shutter blocks movement.

static void tile(const int room, const int x, const int y, const Tile kind)
{
    if (room < 0) world.Workshop.layout.tiles.at[cell(x, y, world.Workshop.layout.width)] = kind;
    else world.Workshop.layout.rooms.at[room].tiles.at[cell(x, y, world.Workshop.layout.rooms.at[room].width)] = kind;
}
static bool crate_at(const int index, const int room, const int x, const int y)
{
    const Cargo *const battery = &world.Workshop.puzzle.crates.at[index];
    return battery->active && battery->room == room && same_cell(battery->position, tide_i2(x, y));
}
// The kind of the cue a move sends, -1 for none.
static int move_cue(const int seat, const int direction)
{
    tide_event_log events = {0};
    TIDE_CHECK(inside_out_test_command(TIDE_COMMAND_ID_Move, seat, world.Workshop.serial, direction));
    tide_world_tick_events(&world, NULL, &events);
    int kind = -1;
    uint32_t offset = 0;
    const tide_heard *const heard = tide_event_log_next(&events, &offset);
    if (heard != NULL && heard->size >= sizeof *heard + sizeof(PuzzleCue)) {
        PuzzleCue cue;
        memcpy(&cue, heard + 1, sizeof cue);
        kind = cue.kind;
    }
    tide_event_log_free(&events);
    return kind;
}

TIDE_TEST(inside_out_a_plate_pressed_by_a_bot_a_battery_or_a_box_opens_only_its_channel)
{
    for (int presser = 0; presser < 3; presser++) {
        fixture(1);
        tile(-1, 1, 1, Tile_PlateA);
        tile(-1, 6, 5, Tile_ShutterA);
        tile(-1, 5, 6, Tile_ShutterB);
        actor(1, -1, 5, 5);
        // The bot steps onto the plate, or pushes a battery or a box onto it.
        actor(0, -1, 1, presser == 0 ? 2 : 3);
        if (presser == 1) crate(0, -1, 1, 2);
        if (presser == 2) world.Workshop.puzzle.rooms.at[0].position = tide_i2(1, 2);
        const Puzzle initial = world.Workshop.puzzle;
        blocked(1, 1);
        blocked(1, 2);
        move(0, 0);
        if (presser == 0) TIDE_CHECK(bot_at(0, -1, 1, 1));
        if (presser == 1) TIDE_CHECK(crate_at(0, -1, 1, 1) && bot_at(0, -1, 1, 2));
        if (presser == 2) TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(1, 1)) && bot_at(0, -1, 1, 2));
        move(1, 1); // Channel A's shutter is open.
        TIDE_CHECK(bot_at(1, -1, 6, 5));
        move(1, 3);
        blocked(1, 2); // Channel B's is still shut.
        undo_all(3);
        TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
        tide_world_free(&world);
    }
}

TIDE_TEST(inside_out_a_plate_in_one_room_opens_a_shutter_in_another)
{
    fixture(1);
    tile(0, 2, 2, Tile_PlateA);    // In box 1's room.
    tile(-1, 4, 3, Tile_ShutterA); // On the table, beside that box's east door.
    tile(-1, 1, 1, Tile_PlateB);   // On the table.
    tile(0, 3, 2, Tile_ShutterB);  // In box 1's room.
    actor(0, 0, 4, 2);
    actor(1, 0, 2, 3);
    const Puzzle initial = world.Workshop.puzzle;
    blocked(0, 1); // Out of the east door is beside the box: a closed shutter.
    move(1, 0);    // Onto the plate in the room.
    move(0, 1);    // The shutter on the table is open now.
    TIDE_CHECK(bot_at(0, -1, 4, 3) && world.Workshop.puzzle.actors.at[0].depth == 0);
    blocked(1, 1); // The room's own shutter is channel B's.
    move(0, 0);
    move(0, 3);
    move(0, 3);
    move(0, 3);
    move(0, 0); // Onto the table's plate.
    TIDE_CHECK(bot_at(0, -1, 1, 1));
    move(1, 1);
    TIDE_CHECK(bot_at(1, 0, 3, 2));
    undo_all(8);
    TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_an_occupied_shutter_stays_down_as_a_battery_hands_it_to_a_bot)
{
    fixture(1);
    world.Workshop.puzzle.rooms.at[0].position = tide_i2(5, 5);
    tile(-1, 1, 1, Tile_PlateA);
    tile(-1, 3, 1, Tile_ShutterA);
    actor(0, -1, 1, 1); // On the plate.
    actor(1, -1, 3, 2);
    crate(0, -1, 2, 1);
    const Puzzle initial = world.Workshop.puzzle;
    // Stepping off the plate and pushing the battery into the shutter is one
    // move, worked out from before it: the shutter was open, so it goes in.
    move(0, 1);
    TIDE_CHECK(bot_at(0, -1, 2, 1) && crate_at(0, -1, 3, 1));
    move(1, 2); // Power is off; the battery keeps the shutter down.
    move(1, 0);
    TIDE_CHECK(crate_at(0, -1, 3, 1));
    // Pushed, it goes on and the bot follows onto the same lowered slab.
    const int moves = world.Workshop.puzzle.moves;
    TIDE_CHECK(move_cue(0, 1) == 1);
    TIDE_CHECK(world.Workshop.puzzle.moves == moves + 1);
    TIDE_CHECK(bot_at(0, -1, 3, 1) && crate_at(0, -1, 4, 1));
    blocked(1, 0); // The bot stops another bot, as on any floor.
    move(0, 3);
    blocked(0, 1); // Now empty and unpowered, it rises and blocks entry.
    blocked(1, 0);
    move(0, 3); // Power it again, then leave it occupied by the other bot.
    move(1, 0);
    TIDE_CHECK(bot_at(1, -1, 3, 1));
    move(0, 1);
    blocked(0, 1);
    move(1, 0);
    TIDE_CHECK(bot_at(1, -1, 3, 0));
    blocked(1, 2);
    undo_all(9);
    TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_a_push_follows_the_whole_row_across_an_occupied_shutter)
{
    // The rear battery replaces the one on the shutter, then the bot
    // replaces that battery. Every push follows normally.
    fixture(1);
    tile(-1, 2, 1, Tile_ShutterA);
    actor(0, -1, 0, 1);
    crate(0, -1, 1, 1);
    crate(1, -1, 2, 1);
    Puzzle initial = world.Workshop.puzzle;
    move(0, 1);
    TIDE_CHECK(bot_at(0, -1, 1, 1) && crate_at(0, -1, 2, 1) && crate_at(1, -1, 3, 1));
    move(0, 1);
    TIDE_CHECK(bot_at(0, -1, 2, 1) && crate_at(0, -1, 3, 1) && crate_at(1, -1, 4, 1));
    move(0, 2);
    blocked(0, 0); // Clear and unpowered, the shutter has closed.
    undo_all(3);
    TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
    tide_world_free(&world);

    // A room box keeps its shutter lowered, so a stopped box can be entered.
    fixture(1);
    world.Workshop.puzzle.rooms.at[0].position = tide_i2(2, 1);
    tile(-1, 2, 1, Tile_ShutterA);
    tile(-1, 3, 1, Tile_Wall);
    actor(0, -1, 1, 1);
    initial = world.Workshop.puzzle;
    move(0, 1);
    TIDE_CHECK(bot_at(0, 0, 0, 2) && same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(2, 1)));
    undo_all(1);
    TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
    tide_world_free(&world);

    // Outward through a doorway, the bot follows a pushed battery onto an
    // occupied shutter. This really crosses the room boundary.
    fixture(1);
    tile(-1, 4, 3, Tile_ShutterA);
    crate(0, -1, 4, 3);
    actor(0, 0, 4, 2);
    initial = world.Workshop.puzzle;
    TIDE_CHECK(move_cue(0, 1) == 2);
    TIDE_CHECK(world.Workshop.puzzle.moves == 1);
    TIDE_CHECK(bot_at(0, -1, 4, 3) && crate_at(0, -1, 5, 3));
    move(0, 0);
    blocked(0, 2);
    undo_all(2);
    TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_a_shutter_in_a_doorway_blocks_only_when_empty_and_unpowered)
{
    fixture(1);
    tile(-1, 4, 3, Tile_Wall); // The box cannot be pushed east.
    tile(0, 0, 2, Tile_ShutterA); // Its west door cell.
    tile(-1, 2, 5, Tile_PlateA);
    actor(0, -1, 2, 3);
    actor(1, -1, 1, 5);
    Puzzle initial = world.Workshop.puzzle;
    blocked(0, 1);
    move(1, 1); // Onto the plate.
    move(0, 1);
    TIDE_CHECK(bot_at(0, 0, 0, 2));
    move(0, 3); // Out again, beside the box.
    TIDE_CHECK(bot_at(0, -1, 2, 3));
    undo_all(3);
    TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
    tide_world_free(&world);

    // A battery holds the doorway down; pushing it lets the bot follow in.
    fixture(1);
    tile(-1, 4, 3, Tile_Wall);
    tile(0, 0, 2, Tile_ShutterA);
    actor(0, -1, 2, 3);
    crate(0, 0, 0, 2);
    initial = world.Workshop.puzzle;
    move(0, 1);
    TIDE_CHECK(bot_at(0, 0, 0, 2) && crate_at(0, 0, 1, 2));
    move(0, 3);
    TIDE_CHECK(bot_at(0, -1, 2, 3));
    blocked(0, 1);
    undo_all(2);
    TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_a_box_carries_its_pressed_plate_and_keeps_the_shutter_open)
{
    for (int pressed = 1; pressed >= 0; pressed--) {
        fixture(1);
        tile(0, 2, 2, Tile_PlateA); // Inside the box.
        tile(-1, 5, 3, Tile_ShutterA);
        actor(0, -1, 2, 3);
        crate(0, 0, pressed ? 2 : 1, pressed ? 2 : 1);
        const Puzzle initial = world.Workshop.puzzle;
        move(0, 1);
        move(0, 1);
        if (pressed) {
            // The plate went with the box and the battery is still on it.
            TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(5, 3)));
            TIDE_CHECK(bot_at(0, -1, 4, 3) && crate_at(0, 0, 2, 2));
        } else {
            // Nothing presses it: the shutter stops the box, and the bot goes in.
            TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(4, 3)));
            TIDE_CHECK(bot_at(0, 0, 0, 2));
        }
        undo_all(2);
        TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
        tide_world_free(&world);
    }
}

TIDE_TEST(inside_out_a_bot_faces_the_way_it_last_moved_and_undo_turns_it_back)
{
    fixture(1);
    actor(0, -1, 1, 1);
    const Puzzle initial = world.Workshop.puzzle;
    move(0, 1);
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].facing == 1);
    move(0, 2);
    TIDE_CHECK(world.Workshop.puzzle.actors.at[0].facing == 2);
    world.Workshop.layout.tiles.at[cell(2, 3, 7)] = Tile_Wall;
    blocked(0, 2); // A step not taken turns nothing.
    for (int step = 0; step < 2; step++) TIDE_CHECK(inside_out_test_act(TIDE_COMMAND_ID_Undo, 0, 0));
    TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_a_room_box_holds_a_shutter_doorway_down_for_nested_entry)
{
    for (int battery = 0; battery < 2; battery++) {
        fixture(2);
        tile(-1, 4, 3, Tile_Wall);
        tile(0, 0, 2, Tile_ShutterA);
        tile(0, 1, 2, Tile_Wall);
        world.Workshop.puzzle.rooms.at[1] = (RoomShell){.room = 0, .position = {0, 2}};
        actor(0, -1, battery ? 1 : 2, 3);
        if (battery) crate(0, -1, 2, 3);
        const Puzzle initial = world.Workshop.puzzle;
        move(0, 1);
        if (battery) TIDE_CHECK(crate_at(0, 1, 0, 2));
        else TIDE_CHECK(bot_at(0, 1, 0, 2));
        undo_all(1);
        TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
        tide_world_free(&world);
    }
}

TIDE_TEST(inside_out_occupied_shutters_hold_only_their_cell_for_batteries_and_room_boxes)
{
    for (int channel = 0; channel < 2; channel++)
        for (int box = 0; box < 2; box++) {
            fixture(1);
            const Tile shutter = channel ? Tile_ShutterB : Tile_ShutterA;
            tile(-1, 2, 1, shutter);
            tile(-1, 4, 1, shutter);
            actor(0, -1, 1, 1);
            actor(1, -1, 4, 2);
            if (box) {
                world.Workshop.puzzle.rooms.at[0].position = tide_i2(2, 1);
                actor(1, 0, 2, 2); // A passenger is carried normally.
            } else crate(0, -1, 2, 1);
            const Puzzle initial = world.Workshop.puzzle;
            move(0, 1);
            TIDE_CHECK(bot_at(0, -1, 2, 1));
            if (box) {
                TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(3, 1)));
                TIDE_CHECK(bot_at(1, 0, 2, 2));
            } else {
                TIDE_CHECK(crate_at(0, -1, 3, 1));
                blocked(1, 0); // Occupancy does not power another shutter.
            }
            // The next empty shutter blocks a pushed battery. A blocked
            // room may instead be entered, which other fixtures cover.
            if (!box) blocked(0, 1);
            move(0, 0);
            blocked(0, 2); // Once vacated, the first shutter is closed too.
            undo_all(2);
            TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
            tide_world_free(&world);
        }
}

TIDE_TEST(inside_out_entering_its_own_box_emits_a_doorway_cue)
{
    fixture(1);
    world.Workshop.puzzle.rooms.at[0] = (RoomShell){.room = 0, .position = {2, 2}};
    tile(0, 1, 2, Tile_Wall);
    actor(0, 0, 3, 2);
    const Puzzle initial = world.Workshop.puzzle;
    TIDE_CHECK(move_cue(0, 3) == 2);
    TIDE_CHECK(bot_at(0, 0, 4, 2));
    TIDE_CHECK(move_cue(0, 1) == 2);
    TIDE_CHECK(bot_at(0, 0, 3, 2));
    undo_all(2);
    TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_doorways_use_power_from_before_the_move)
{
    // Leaving the plate to enter a shutter doorway still uses its power.
    fixture(1);
    tile(-1, 4, 3, Tile_Wall);
    tile(-1, 2, 3, Tile_PlateA);
    tile(0, 0, 2, Tile_ShutterA);
    actor(0, -1, 2, 3);
    const Puzzle entry = world.Workshop.puzzle;
    move(0, 1);
    TIDE_CHECK(bot_at(0, 0, 0, 2));
    undo_all(1);
    TIDE_CHECK(memcmp(&entry, &world.Workshop.puzzle, sizeof entry) == 0);
    tide_world_free(&world);

    // The same holds at the outside end of an exit.
    fixture(1);
    tile(0, 4, 2, Tile_PlateA);
    tile(-1, 4, 3, Tile_ShutterA);
    actor(0, 0, 4, 2);
    const Puzzle exit = world.Workshop.puzzle;
    move(0, 1);
    TIDE_CHECK(bot_at(0, -1, 4, 3));
    undo_all(1);
    TIDE_CHECK(memcmp(&exit, &world.Workshop.puzzle, sizeof exit) == 0);
    tide_world_free(&world);

    // A battery leaving its plate may be posted through a shutter too.
    fixture(1);
    tile(-1, 4, 3, Tile_Wall);
    tile(-1, 2, 3, Tile_PlateA);
    tile(0, 0, 2, Tile_ShutterA);
    actor(0, -1, 1, 3);
    crate(0, -1, 2, 3);
    const Puzzle posted = world.Workshop.puzzle;
    move(0, 1);
    TIDE_CHECK(crate_at(0, 0, 0, 2));
    undo_all(1);
    TIDE_CHECK(memcmp(&posted, &world.Workshop.puzzle, sizeof posted) == 0);
    tide_world_free(&world);
}

// A box goes into a stopped box only by a doorway on the side it comes from:
// with none there, and none on its own side facing the other to take it in,
// it stays out, and the bot behind it goes into it instead.
TIDE_TEST(inside_out_a_box_goes_into_a_stopped_box_only_by_a_facing_doorway)
{
    fixture(2);
    world.Workshop.layout.rooms.at[1].doors = 7; // No west doorway.
    world.Workshop.layout.rooms.at[0].doors = 13; // No east doorway.
    tile(-1, 5, 3, Tile_Wall);
    actor(0, -1, 2, 3);
    const Puzzle initial = world.Workshop.puzzle;
    move(0, 1);
    TIDE_CHECK(bot_at(0, 0, 0, 2));
    TIDE_CHECK(memcmp(&initial.rooms, &world.Workshop.puzzle.rooms, sizeof initial.rooms) == 0);
    undo_all(1);
    TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
    // With a west doorway the first box goes in there, and the bot follows it
    // onto the table cell it left.
    world.Workshop.layout.rooms.at[1].doors = 15;
    move(0, 1);
    TIDE_CHECK(world.Workshop.puzzle.rooms.at[0].room == 1 && same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(0, 2)));
    TIDE_CHECK(bot_at(0, -1, 3, 3));
    tide_world_free(&world);
}

// A box pushed at something that cannot move takes it in by its own
// doorway on that side, unless the thing is a bot or clamped, and moves onto
// where it stood; what stands in that doorway is pushed on further in.
TIDE_TEST(inside_out_a_stopped_thing_goes_into_the_box_pushed_at_it)
{
    fixture(1);
    tile(-1, 5, 3, Tile_Wall);
    actor(0, -1, 2, 3);
    crate(0, -1, 4, 3);
    const Puzzle initial = world.Workshop.puzzle;
    move(0, 1);
    TIDE_CHECK(crate_at(0, 0, 4, 2)); // In by box 1's east doorway.
    TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(4, 3)));
    TIDE_CHECK(bot_at(0, -1, 3, 3));
    undo_all(1);
    TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
    // A battery in that doorway is pushed on, back along the row's way.
    world.Workshop.layout.crateCount = 2;
    world.Workshop.puzzle.crates.at[1] = (Cargo){.room = 0, .position = {4, 2}, .active = true};
    const Puzzle occupied = world.Workshop.puzzle;
    move(0, 1);
    TIDE_CHECK(crate_at(0, 0, 4, 2) && crate_at(1, 0, 3, 2));
    undo_all(1);
    TIDE_CHECK(memcmp(&occupied, &world.Workshop.puzzle, sizeof occupied) == 0);
    // Where it cannot be pushed on, nothing is taken in: the bot goes into
    // the box by its west doorway instead, and so it does with no east one.
    tile(0, 3, 2, Tile_Wall);
    move(0, 1);
    TIDE_CHECK(bot_at(0, 0, 0, 2) && crate_at(0, -1, 4, 3));
    undo_all(1);
    tile(0, 3, 2, Tile_Floor);
    world.Workshop.layout.crateCount = 1;
    world.Workshop.layout.rooms.at[0].doors = 13;
    move(0, 1);
    TIDE_CHECK(bot_at(0, 0, 0, 2) && crate_at(0, -1, 4, 3));
    undo_all(1);
    // A bot is never taken in: the pusher goes into the box.
    world.Workshop.layout.rooms.at[0].doors = 15;
    world.Workshop.layout.crateCount = 0;
    actor(1, -1, 4, 3);
    move(0, 1);
    TIDE_CHECK(bot_at(0, 0, 0, 2) && bot_at(1, -1, 4, 3));
    tide_world_free(&world);

    // A box is taken in too, where the other cannot be entered from this
    // side, but not a clamped one.
    fixture(2);
    tile(-1, 5, 3, Tile_Wall);
    actor(0, -1, 2, 3);
    world.Workshop.layout.rooms.at[1].doors = 7; // No west doorway into box 2.
    const Puzzle boxes = world.Workshop.puzzle;
    move(0, 1);
    TIDE_CHECK(world.Workshop.puzzle.rooms.at[1].room == 0 && same_cell(world.Workshop.puzzle.rooms.at[1].position, tide_i2(4, 2)));
    TIDE_CHECK(world.Workshop.puzzle.rooms.at[0].room == -1 && same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(4, 3)));
    undo_all(1);
    TIDE_CHECK(memcmp(&boxes, &world.Workshop.puzzle, sizeof boxes) == 0);
    tile(-1, 4, 3, Tile_Clamp);
    move(0, 1);
    TIDE_CHECK(bot_at(0, 0, 0, 2));
    TIDE_CHECK(memcmp(&boxes.rooms, &world.Workshop.puzzle.rooms, sizeof boxes.rooms) == 0);
    tide_world_free(&world);
}

// A box standing on a clamp never moves: it is entered as a box against a
// wall is, stops a row, and still goes with the room it stands in.
TIDE_TEST(inside_out_a_clamp_holds_its_box_and_the_room_carries_it)
{
    fixture(1);
    tile(-1, 4, 3, Tile_Clamp);
    actor(0, -1, 2, 3);
    const Puzzle initial = world.Workshop.puzzle;
    move(0, 1); // Onto the clamp: a box is pushed onto one like any floor.
    TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(4, 3)));
    move(0, 1); // Now it holds, with open floor beyond: the bot goes in.
    TIDE_CHECK(bot_at(0, 0, 0, 2));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(4, 3)));
    move(0, 3); // Out, and in again from the other side.
    TIDE_CHECK(bot_at(0, -1, 3, 3));
    actor(1, -1, 5, 3);
    move(1, 3);
    TIDE_CHECK(bot_at(1, 0, 4, 2));
    TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(4, 3)));
    undo_all(4);
    world.Workshop.puzzle.actors.at[1] = initial.actors.at[1];
    TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
    // A battery against the clamped box goes in only by a doorway.
    world.Workshop.puzzle.rooms.at[0].position = tide_i2(4, 3);
    crate(0, -1, 3, 3);
    move(0, 1);
    TIDE_CHECK(crate_at(0, 0, 0, 2));
    undo_all(1);
    world.Workshop.layout.rooms.at[0].doors = 7;
    blocked(0, 1);
    tide_world_free(&world);

    // A clamp in a room holds its box there while that room's own box moves.
    fixture(2);
    world.Workshop.puzzle.rooms.at[0] = (RoomShell){.room = 1, .position = {2, 2}};
    tile(1, 2, 2, Tile_Clamp);
    actor(0, -1, 3, 3);
    actor(1, 1, 1, 2);
    const Puzzle nested = world.Workshop.puzzle;
    move(0, 1); // Box 1 moves on the table, with box 0 and the partner in it.
    TIDE_CHECK(same_cell(world.Workshop.puzzle.rooms.at[1].position, tide_i2(5, 3)));
    TIDE_CHECK(world.Workshop.puzzle.rooms.at[0].room == 1 && same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(2, 2)));
    move(1, 1); // Inside, box 0 holds: the partner goes into it.
    TIDE_CHECK(bot_at(1, 0, 0, 2));
    TIDE_CHECK(world.Workshop.puzzle.rooms.at[0].room == 1 && same_cell(world.Workshop.puzzle.rooms.at[0].position, tide_i2(2, 2)));
    undo_all(2);
    TIDE_CHECK(memcmp(&nested, &world.Workshop.puzzle, sizeof nested) == 0);
    tide_world_free(&world);
}
