// Underfoot's rules, through the commands players send: each case lays out a
// small board and says what a move must do to it, worked out by hand.
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/net.h"
#include "tide_test.h"
#include "test_helpers.h"

tide_world underfoot_test_world;
#define world underfoot_test_world
#define yard world.Yard
#define AT(x, y) ((y) * UF_W + (x))

_Static_assert(Ground_Void == UF_VOID && Ground_Stone == UF_SLAB && Ground_Ice == UF_ICE && Ground_PortalP == UF_PORTAL_P
    && Ground_PortalQ == UF_PORTAL_Q && Ground_PlateA == UF_PLATE_A && Ground_PlateB == UF_PLATE_B && Ground_Rubble == UF_RUBBLE
    && Ground_Wall == UF_WALL, "the game and the model number ground alike");

enum { NORTH, EAST, SOUTH, WEST, HAND };

static tide_player_id player(const int seat) { return tide_player_from_index(seat); }

void underfoot_test_start(const int participants)
{
    tide_world_free(&world);
    tide_world_init(&world, 1.0f / 30.0f);
    for (int seat = 0; seat < participants; seat++) tide_world_player_joined(&world, player(seat));
    tide_world_tick(&world);
    tide_world_tick(&world);
}

bool underfoot_test_send(const uint32_t id, const int sender, const int serial, const int first, const int second)
{
    uint8_t bytes[TIDE_COMMAND_BYTES];
    tide_bits bits = {bytes, TIDE_COMMAND_BYTES, 0, false};
    tide_bits_put(&bits, id, 16);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put_varint(&bits, tide_zigzag(serial));
    if (id == TIDE_COMMAND_ID_Act || id == TIDE_COMMAND_ID_ChooseLevel) tide_bits_put_varint(&bits, tide_zigzag(first));
    if (id == TIDE_COMMAND_ID_Act) tide_bits_put_varint(&bits, tide_zigzag(second));
    const uint32_t size = tide_bits_end(&bits);
    const bool received = tide_game_api.command(&world, player(sender), 0,
        tide_game_api.command_sequence(&world, player(sender), 0) + 1, bytes, size);
    tide_world_tick(&world);
    return received;
}

bool underfoot_test_act(const int sender, const int paver, const int action)
{
    return underfoot_test_send(TIDE_COMMAND_ID_Act, sender, yard.serial, paver, action);
}

bool underfoot_test_do(const uint32_t id, const int sender, const int value)
{
    return underfoot_test_send(id, sender, yard.serial, value, 0);
}

void underfoot_test_to_model(uf_layout *const layout, uf_state *const state)
{
    uf_blank(layout, state);
    layout->actors = 2;
    for (int cell = 0; cell < UF_CELLS; cell++) {
        state->ground[cell] = yard.board.ground.at[cell];
        layout->fixed[cell] = yard.layout.fixed.at[cell];
        layout->gate[cell] = (uint8_t)yard.layout.gate.at[cell];
        layout->goal[cell] = yard.layout.goal.at[cell];
    }
    for (int paver = 0; paver < 2; paver++) {
        state->actor[paver] = (uint8_t)yard.board.paver.at[paver];
        state->held[paver] = yard.board.held.at[paver];
    }
    for (int crate = 0; crate < UF_CRATES; crate++)
        state->crate[crate] = yard.board.crate.at[crate] < 0 ? UF_NONE : (uint8_t)yard.board.crate.at[crate];
}

void underfoot_test_from_model(const uf_layout *const layout, const uf_state *const state)
{
    memset(&yard.board, 0, sizeof yard.board);
    memset(&yard.layout, 0, sizeof yard.layout);
    yard.historyCount = 0;
    for (int cell = 0; cell < UF_CELLS; cell++) {
        yard.board.ground.at[cell] = state->ground[cell];
        yard.layout.fixed.at[cell] = layout->fixed[cell];
        yard.layout.gate.at[cell] = layout->gate[cell];
        yard.layout.goal.at[cell] = layout->goal[cell];
    }
    for (int paver = 0; paver < 2; paver++) {
        yard.board.paver.at[paver] = state->actor[paver] == UF_NONE ? -1 : state->actor[paver];
        yard.board.held.at[paver] = state->held[paver];
        yard.board.facing.at[paver] = 2;
    }
    for (int crate = 0; crate < UF_CRATES; crate++)
        yard.board.crate.at[crate] = state->crate[crate] == UF_NONE ? -1 : state->crate[crate];
}

static int by_place(const void *const a, const void *const b) { return *(const int *)a - *(const int *)b; }

bool underfoot_test_same(const uf_layout *const layout, const uf_state *const state)
{
    int mine[UF_CRATES], theirs[UF_CRATES];
    for (int cell = 0; cell < UF_CELLS; cell++)
        if (yard.board.ground.at[cell] != state->ground[cell]) return false;
    for (int paver = 0; paver < 2; paver++)
        if (yard.board.paver.at[paver] != state->actor[paver] || yard.board.held.at[paver] != state->held[paver]) return false;
    for (int crate = 0; crate < UF_CRATES; crate++) {
        mine[crate] = yard.board.crate.at[crate];
        theirs[crate] = state->crate[crate] == UF_NONE ? -1 : state->crate[crate];
    }
    qsort(mine, UF_CRATES, sizeof mine[0], by_place);
    qsort(theirs, UF_CRATES, sizeof theirs[0], by_place);
    return memcmp(mine, theirs, sizeof mine) == 0 && yard.board.solved == uf_solved(layout, state);
}

void underfoot_test_lab(const char *const rows[7])
{
    uf_layout layout;
    uf_state state;
    uf_blank(&layout, &state);
    layout.actors = 2;
    for (int y = 0; y < UF_H; y++)
        for (size_t x = 0; rows[y] && x * 2 < strlen(rows[y]) && x < UF_W; x++)
            uf_parse_cell(&layout, &state, AT((int)x, y), rows[y][x * 2], x * 2 + 1 < strlen(rows[y]) ? rows[y][x * 2 + 1] : ' ');
    underfoot_test_start(1);
    underfoot_test_from_model(&layout, &state);
}

// One player alone works both pavers, so a case moves either.
static bool walk(const int paver, const int direction)
{
    const int moves = yard.board.moves;
    underfoot_test_act(0, paver, direction);
    return yard.board.moves == moves + 1;
}
static bool hand(const int paver, const int direction) { return walk(paver, HAND + direction); }

TIDE_TEST(underfoot_lifting_takes_firm_footing_and_an_empty_hand)
{
    underfoot_test_lab((const char *const[7]){
        "# # # # # # # # #",
        "# o .@o o ~ G # #",
        "# .&. ox. . G # #",
        "# # # # # # # # #"});
    TIDE_CHECK(!hand(0, NORTH));                  // A wall is nobody's to lift,
    TIDE_CHECK(!hand(0, SOUTH));                  // nor is set stone.
    TIDE_CHECK(!hand(1, EAST));
    TIDE_REQUIRE(hand(0, EAST));
    TIDE_CHECK(yard.board.held.at[0] == Ground_Stone && yard.board.ground.at[AT(3, 1)] == Ground_Void);
    TIDE_CHECK(!hand(0, WEST));                   // One stone in hand at a time.
    TIDE_CHECK(!walk(0, EAST));                   // Nothing to stand on there now.
    TIDE_REQUIRE(hand(0, EAST));                  // The same hand lays it back.
    TIDE_CHECK(yard.board.held.at[0] == Ground_Void && yard.board.ground.at[AT(3, 1)] == Ground_Stone);
    TIDE_REQUIRE(walk(0, EAST));
    TIDE_CHECK(!hand(0, EAST));                   // No lifting from a loose stone.
    TIDE_CHECK(yard.board.ground.at[AT(4, 1)] == Ground_Stone);
    TIDE_REQUIRE(walk(0, WEST) && walk(0, SOUTH));
    TIDE_CHECK(!hand(0, EAST));                   // Nor a stone with a crate on it.
    TIDE_REQUIRE(hand(1, NORTH));                 // From set stone, a free loose one comes up.
    TIDE_CHECK(yard.board.held.at[1] == Ground_Stone && yard.board.ground.at[AT(1, 1)] == Ground_Void);
    tide_world_free(&world);
}

TIDE_TEST(underfoot_a_stone_laid_over_nothing_is_a_way_across)
{
    underfoot_test_lab((const char *const[7]){
        "# # # # # # # # #",
        "# .@o ~ ~ .&G G #",
        "# # # # # # # # #"});
    TIDE_REQUIRE(hand(0, EAST) && !walk(0, EAST));
    TIDE_REQUIRE(hand(0, EAST) && walk(0, EAST));
    TIDE_CHECK(!walk(0, EAST));                   // The stone reaches one cell, not two.
    TIDE_CHECK(!hand(0, EAST) && !hand(1, WEST)); // And empty hands lay nothing.
    TIDE_CHECK(yard.board.ground.at[AT(3, 1)] == Ground_Void && yard.board.ground.at[AT(4, 1)] == Ground_Void);
    tide_world_free(&world);
}

TIDE_TEST(underfoot_a_crate_over_nothing_sinks_and_is_ground_for_good)
{
    underfoot_test_lab((const char *const[7]){
        "# # # # # # # # #",
        "# .@.x~ . .x.x# #",
        "# .&. . . . . G G",
        "# # # # # # # # #"});
    TIDE_REQUIRE(walk(0, EAST));
    TIDE_CHECK(yard.board.ground.at[AT(3, 1)] == Ground_Rubble);
    TIDE_CHECK(yard.board.crate.at[0] == -1 && yard.board.paver.at[0] == AT(2, 1));
    TIDE_CHECK(!hand(0, EAST));                   // What sank stays.
    TIDE_REQUIRE(walk(0, EAST) && walk(0, EAST));
    TIDE_CHECK(!walk(0, EAST));                   // One crate at a time.
    TIDE_CHECK(yard.board.paver.at[0] == AT(4, 1) && yard.board.crate.at[1] == AT(5, 1) && yard.board.crate.at[2] == AT(6, 1));
    tide_world_free(&world);
}

TIDE_TEST(underfoot_ice_carries_pavers_to_an_edge_and_crates_over_it)
{
    underfoot_test_lab((const char *const[7]){
        "# # # # # # # # #",
        "# .@I I I . .xI ~",
        "# .&I I ~ . . . #",
        "# . I . .xI I . G",
        "# # # # # # # # #"});
    TIDE_REQUIRE(walk(0, EAST));
    TIDE_CHECK(yard.board.paver.at[0] == AT(5, 1)); // Off the far side of the ice.
    TIDE_REQUIRE(walk(1, EAST));
    TIDE_CHECK(yard.board.paver.at[1] == AT(3, 2)); // A paver stops short of nothing.
    TIDE_REQUIRE(walk(0, EAST));
    TIDE_CHECK(yard.board.crate.at[0] == -1 && yard.board.ground.at[AT(8, 1)] == Ground_Rubble); // A crate does not.
    TIDE_CHECK(yard.board.paver.at[0] == AT(6, 1));
    TIDE_REQUIRE(walk(1, SOUTH) && walk(1, WEST));
    TIDE_CHECK(yard.board.paver.at[1] == AT(1, 3)); // Over one cell of ice and off it.
    TIDE_REQUIRE(walk(1, EAST));
    TIDE_CHECK(yard.board.paver.at[1] == AT(3, 3));
    TIDE_REQUIRE(walk(1, EAST));                    // A pushed crate rides the ice to its end.
    TIDE_CHECK(yard.board.crate.at[1] == AT(7, 3) && yard.board.paver.at[1] == AT(4, 3));
    TIDE_REQUIRE(walk(1, EAST));                    // A sliding paver pushes nothing.
    TIDE_CHECK(yard.board.crate.at[1] == AT(7, 3) && yard.board.paver.at[1] == AT(6, 3));
    tide_world_free(&world);
}

TIDE_TEST(underfoot_a_portal_sends_whatever_arrives_to_its_free_twin)
{
    underfoot_test_lab((const char *const[7]){
        "# # # # # # # # #",
        "# .@P . . . Q . #",
        "# .&. . . . . G G",
        "# # # # # # # # #"});
    TIDE_REQUIRE(walk(0, EAST));
    TIDE_CHECK(yard.board.paver.at[0] == AT(6, 1));
    TIDE_REQUIRE(walk(0, EAST) && walk(0, WEST));
    TIDE_CHECK(yard.board.paver.at[0] == AT(2, 1)); // And back the other way.
    tide_world_free(&world);

    underfoot_test_lab((const char *const[7]){
        "# # # # # # # # #",
        "# .@.xP ~ ~ Q . #",
        "# .&. . . . . G G",
        "# # # # # # # # #"});
    TIDE_REQUIRE(walk(0, EAST) && walk(0, EAST));
    // The crate went through; it has the twin, so its pusher stays on the portal.
    TIDE_CHECK(yard.board.crate.at[0] == AT(6, 1) && yard.board.paver.at[0] == AT(3, 1));
    tide_world_free(&world);

    underfoot_test_lab((const char *const[7]){
        "# # # # # # # # #",
        "# .@p . . .&q . #",
        "# . . . . . . G G",
        "# # # # # # # # #"});
    TIDE_REQUIRE(hand(1, EAST));
    TIDE_REQUIRE(walk(0, EAST));
    TIDE_CHECK(yard.board.paver.at[0] == AT(2, 1)); // A portal in hand leads nowhere.
    TIDE_REQUIRE(hand(1, EAST) && walk(0, EAST) && walk(0, WEST));
    TIDE_CHECK(yard.board.paver.at[0] == AT(6, 1)); // Laid again, it does.
    tide_world_free(&world);
}

TIDE_TEST(underfoot_a_plate_opens_its_gates_for_the_move_that_follows)
{
    underfoot_test_lab((const char *const[7]){
        "# # # # # # # # #",
        "# .@A 1 . .x. 1 #",
        "# .&. .xb 2 . G G",
        "# # # # # # # # #"});
    TIDE_CHECK(!hand(0, EAST));                    // A set plate stays.
    TIDE_REQUIRE(walk(0, EAST));
    // On the plate the gate is open, and a move keeps the gates it began with.
    TIDE_REQUIRE(walk(0, EAST));
    TIDE_CHECK(yard.board.paver.at[0] == AT(3, 1));
    TIDE_REQUIRE(walk(0, EAST) && walk(0, EAST));
    TIDE_CHECK(!walk(0, EAST));                    // A crate will not go through a shut gate,
    TIDE_CHECK(yard.board.crate.at[0] == AT(6, 1) && yard.board.paver.at[0] == AT(5, 1));
    TIDE_REQUIRE(walk(0, WEST));
    TIDE_CHECK(!walk(0, WEST));                    // nor a paver.
    TIDE_CHECK(yard.board.paver.at[0] == AT(4, 1));
    TIDE_REQUIRE(walk(1, EAST) && walk(1, EAST));
    TIDE_CHECK(yard.board.crate.at[1] == AT(4, 2)); // A crate holds plate B down,
    TIDE_CHECK(!hand(1, EAST));                    // so the plate is not to be lifted,
    TIDE_REQUIRE(walk(1, EAST));                   // and its gate lets the crate in.
    TIDE_CHECK(yard.board.crate.at[1] == AT(5, 2) && yard.board.paver.at[1] == AT(4, 2));
    tide_world_free(&world);

    underfoot_test_lab((const char *const[7]){
        "# # # # # # # # #",
        "# .@a 1 . . G G #",
        "# .&. # # # # # #",
        "# # # # # # # # #"});
    TIDE_CHECK(!hand(0, SOUTH));
    TIDE_REQUIRE(hand(0, EAST) && hand(0, EAST));  // A loose plate lifts and lays like a stone.
    TIDE_REQUIRE(walk(1, EAST) && walk(1, NORTH)); // Tide holds it down.
    TIDE_CHECK(!hand(0, EAST));                    // A stone under someone stays where it is.
    tide_world_free(&world);
}

TIDE_TEST(underfoot_both_lanterns_end_a_puzzle_and_open_the_next)
{
    underfoot_test_lab((const char *const[7]){
        "# # # # # # # # #",
        "# .@G . .&G . . #",
        "# # # # # # # # #"});
    yard.level = 3;
    TIDE_REQUIRE(walk(0, EAST));
    TIDE_CHECK(!yard.board.solved && yard.completed == 0);
    TIDE_REQUIRE(walk(1, EAST));
    TIDE_CHECK(yard.board.solved && yard.completed == 1 << 3 && yard.best.at[3] == 2);
    TIDE_CHECK(!walk(0, WEST));                    // Nothing moves once it's lit.
    TIDE_REQUIRE(underfoot_test_do(TIDE_COMMAND_ID_Undo, 0, 0));
    TIDE_CHECK(!yard.board.solved && yard.board.moves == 1);
    TIDE_REQUIRE(walk(1, EAST));
    const int serial = yard.serial;
    TIDE_REQUIRE(underfoot_test_do(TIDE_COMMAND_ID_Next, 0, 0));
    TIDE_CHECK(yard.level == 4 && yard.serial == serial + 1 && yard.historyCount == 0 && !yard.board.solved);
    tide_world_free(&world);
}

TIDE_TEST(underfoot_alone_a_player_works_both_pavers_and_together_only_their_own)
{
    underfoot_test_lab((const char *const[7]){
        "# # # # # # # # #",
        "# .@. . . . . G #",
        "# .&. . . . . G #",
        "# # # # # # # # #"});
    TIDE_REQUIRE(walk(0, EAST) && walk(1, EAST));
    tide_world_player_joined(&world, player(1));
    tide_world_tick(&world);
    TIDE_REQUIRE(yard.present.at[0] && yard.present.at[1]);
    const Board before = yard.board;
    underfoot_test_act(0, 1, EAST);                // Not the first player's paver now.
    underfoot_test_act(1, 0, EAST);
    underfoot_test_act(0, 0, 8);                   // No such action,
    underfoot_test_act(0, 2, EAST);                // nor such a paver,
    underfoot_test_send(TIDE_COMMAND_ID_Act, 0, yard.serial - 1, 0, EAST); // nor a board that's gone.
    TIDE_CHECK(memcmp(&before, &yard.board, sizeof before) == 0);
    underfoot_test_act(0, 0, EAST);
    underfoot_test_act(1, 1, EAST);
    TIDE_CHECK(yard.board.paver.at[0] == AT(3, 1) && yard.board.paver.at[1] == AT(3, 2));
    // A player who leaves keeps the seat for coming back; the other works both meanwhile.
    tide_world_player_left(&world, player(1));
    tide_world_tick(&world);
    TIDE_CHECK(!yard.present.at[1]);
    underfoot_test_act(0, 1, EAST);
    TIDE_CHECK(yard.board.paver.at[1] == AT(4, 2));
    tide_world_player_joined(&world, player(1));
    tide_world_tick(&world);
    underfoot_test_act(1, 1, EAST);
    TIDE_CHECK(yard.present.at[1] && yard.board.paver.at[1] == AT(5, 2));
    tide_world_free(&world);
}

TIDE_TEST(underfoot_undo_goes_back_through_a_reset_and_keeps_the_last_256_moves)
{
    underfoot_test_start(1);
    const Board initial = yard.board;
    underfoot_test_do(TIDE_COMMAND_ID_Reset, 0, 0);
    TIDE_CHECK(yard.historyCount == 0);            // Nothing to start over yet.
    static const char *const rows[7] = {
        "# # # # # # # # #",
        "# .@. o ~ . . G #",
        "# .&. . . # . G #",
        "# # # # # # # # #"};
    underfoot_test_lab(rows);
    yard.level = 0;
    TIDE_REQUIRE(walk(0, EAST) && hand(0, EAST));
    const Board carrying = yard.board;
    TIDE_REQUIRE(underfoot_test_do(TIDE_COMMAND_ID_Reset, 0, 0));
    TIDE_CHECK(memcmp(&initial, &yard.board, sizeof initial) == 0 && yard.historyCount == 3);
    TIDE_REQUIRE(underfoot_test_do(TIDE_COMMAND_ID_Undo, 0, 0));
    TIDE_CHECK(memcmp(&carrying, &yard.board, sizeof carrying) == 0);

    underfoot_test_lab(rows);
    for (int i = 0; i < 300; i++) TIDE_REQUIRE(walk(1, i % 2 ? WEST : EAST));
    TIDE_CHECK(yard.historyCount == 256 && yard.board.moves == 300);
    for (int i = 0; i < 260; i++) underfoot_test_do(TIDE_COMMAND_ID_Undo, 0, 0);
    TIDE_CHECK(yard.historyCount == 0 && yard.board.moves == 44);
    tide_world_free(&world);
}

TIDE_TEST(underfoot_a_match_survives_a_snapshot_its_bytes_and_a_replay)
{
    underfoot_test_lab((const char *const[7]){
        "# # # # # # # # #",
        "# .@o ~ I I P . #",
        "# .&.x. a 1 Q G G",
        "# # # # # # # # #"});
    tide_world checkpoint = {0};
    tide_world_copy(&checkpoint, &world);
    TIDE_REQUIRE(hand(0, EAST) && walk(1, EAST) && hand(0, EAST));
    const uint64_t hash = tide_world_hash(&world);
    uint8_t *const bytes = malloc(1024 * 1024);
    TIDE_REQUIRE(bytes != NULL);
    const uint32_t size = tide_world_pack(&world, bytes, 1024 * 1024);
    TIDE_CHECK(size > 0);
    tide_world restored = {0};
    TIDE_CHECK(tide_world_unpack(&restored, bytes, size));
    TIDE_CHECK(tide_world_hash(&restored) == hash);
    tide_world_copy(&world, &checkpoint);
    TIDE_REQUIRE(hand(0, EAST) && walk(1, EAST) && hand(0, EAST));
    TIDE_CHECK(tide_world_hash(&world) == hash);
    free(bytes);
    tide_world_free(&restored);
    tide_world_free(&checkpoint);
    tide_world_free(&world);
}
