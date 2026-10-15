// Witnesses exercise generated command handlers; no second movement
// implementation takes part in a replay or a search.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tide_test.h"
#include "test_helpers.h"
#include "solutions.h"
#define world inside_out_test_world
// LEVEL_COUNT in game.tide; inside_out_campaign_has_exactly_the_witnessed_puzzles
// fails if the two disagree.
#define PUZZLES 18
_Static_assert(sizeof inside_out_solutions / sizeof inside_out_solutions[0] == PUZZLES, "one witness per puzzle");
static bool same_cell(tide_int2 a, tide_int2 b) { return a.x == b.x && a.y == b.y; }
static Tile tile_at(const LevelLayout *const layout, const int room, const tide_int2 at)
{
    const int size = room < 0 ? layout->width : layout->rooms.at[room].width;
    if (at.x < 0 || at.y < 0 || at.x >= size || at.y >= size) return Tile_Wall;
    return room < 0 ? layout->tiles.at[at.y * size + at.x] : layout->rooms.at[room].tiles.at[at.y * size + at.x];
}
// What levels.tide's maps promise: a box's edge is wall but for its doorways,
// one open cell a side at most and none at a corner, and a side has a doorway
// exactly where it has that cell.
static void check_doorways(const LevelLayout *const layout, const int room)
{
    const int size = layout->rooms.at[room].width, last = size - 1;
    TIDE_CHECK(tile_at(layout, room, tide_i2(0, 0)) == Tile_Wall && tile_at(layout, room, tide_i2(last, 0)) == Tile_Wall);
    TIDE_CHECK(tile_at(layout, room, tide_i2(0, last)) == Tile_Wall && tile_at(layout, room, tide_i2(last, last)) == Tile_Wall);
    const int offsets[4] = {layout->rooms.at[room].north, layout->rooms.at[room].east, layout->rooms.at[room].south,
        layout->rooms.at[room].west};
    for (int side = 0; side < 4; side++) {
        int open = 0, door = -1;
        for (int along = 1; along < last; along++) {
            const tide_int2 at = side == 0 ? tide_i2(along, 0) : side == 1 ? tide_i2(last, along)
                : side == 2 ? tide_i2(along, last) : tide_i2(0, along);
            if (tile_at(layout, room, at) == Tile_Wall) continue;
            open++;
            door = along;
        }
        TIDE_CHECK(open <= 1);
        const bool has = (layout->rooms.at[room].doors & (1 << side)) != 0;
        TIDE_CHECK(has == (open == 1));
        if (has) TIDE_CHECK((offsets[side] == -1 ? size / 2 : offsets[side]) == door);
    }
}
static bool play_witness(int level, const char *witness, int *actions)
{
    int count = 0;
    unsigned seats = 0;
    const char *const upper_keys = "NESW", *const lower_keys = "nesw";
    for (const char *action = witness; *action; action++) {
        const char *upper = strchr(upper_keys, *action), *lower = strchr(lower_keys, *action);
        if (!upper && !lower) continue;
        const int seat = upper ? 0 : 1;
        const int direction = upper ? (int)(upper - upper_keys) : (int)(lower - lower_keys);
        const Puzzle before = world.Workshop.puzzle;
        if (!inside_out_test_act(TIDE_COMMAND_ID_Move, seat, direction) || world.Workshop.puzzle.moves != before.moves + 1) {
            fprintf(stderr, "Level %d action %d (%c) blocked: actor r%d(%d,%d) depth%d.\n", level + 1, count + 1, *action,
                before.actors.at[seat].room, before.actors.at[seat].position.x, before.actors.at[seat].position.y, before.actors.at[seat].depth);
            return false;
        }
        seats |= 1u << seat;
        count++;
    }
    if (actions) *actions = count;
    return world.Workshop.puzzle.solved && seats == 3;
}
static void wait_ticks(const int ticks)
{
    for (int tick = 0; tick < ticks; tick++) tide_world_tick(&world);
}

TIDE_TEST(inside_out_solved_puzzle_advances_after_150_simulation_ticks)
{
    inside_out_test_start(2);
    TIDE_CHECK(world.Workshop.introTicks == 114);
    TIDE_REQUIRE(play_witness(0, inside_out_solutions[0], NULL));
    const int solved_tick = world.Workshop.solvedTick;
    const int serial = world.Workshop.serial;
    const Puzzle solved = world.Workshop.puzzle;
    TIDE_CHECK(world.Time.tick == solved_tick + 1);
    // A tick's systems read Time.tick before the tick increments it.
    // All 149 earlier elapsed ticks retain the solved arrangement while
    // the next name fades in, holds for three seconds, then fades out.
    wait_ticks(149);
    TIDE_CHECK(world.Time.tick == solved_tick + 150);
    TIDE_CHECK(world.Workshop.level == 0 && world.Workshop.serial == serial);
    TIDE_CHECK(memcmp(&solved, &world.Workshop.puzzle, sizeof solved) == 0);
    tide_world_tick(&world);
    TIDE_CHECK(world.Workshop.level == 1 && !world.Workshop.puzzle.solved);
    TIDE_CHECK(world.Workshop.openedTick == solved_tick + 150 && world.Workshop.solvedTick == -1);
    TIDE_CHECK(world.Workshop.introTicks == 0); // Its name was already shown.
    TIDE_CHECK(world.Workshop.serial == serial + 1 && world.Workshop.historyCount == 0);
    TIDE_CHECK(world.Workshop.puzzle.moves == 0 && (world.Workshop.completed & 1) != 0);
    // A reliable command already in flight belongs to the previous puzzle.
    const Puzzle next = world.Workshop.puzzle;
    TIDE_REQUIRE(inside_out_test_command(TIDE_COMMAND_ID_Move, 0, serial, 0));
    tide_world_tick(&world);
    TIDE_CHECK(memcmp(&next, &world.Workshop.puzzle, sizeof next) == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_undo_and_reset_cancel_automatic_progression)
{
    for (int reset = 0; reset < 2; reset++) {
        inside_out_test_start(2);
        const Puzzle initial = world.Workshop.puzzle;
        TIDE_REQUIRE(play_witness(0, inside_out_solutions[0], NULL));
        wait_ticks(90); // Cancel during the opaque name hold.
        TIDE_REQUIRE(inside_out_test_act(reset ? TIDE_COMMAND_ID_Reset : TIDE_COMMAND_ID_Undo, 1, 0));
        TIDE_CHECK(!world.Workshop.puzzle.solved);
        wait_ticks(160); // Beyond the entire cancelled deadline.
        TIDE_CHECK(world.Workshop.level == 0 && world.Workshop.solvedTick == -1);
        if (reset) {
            TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
            TIDE_CHECK(world.Workshop.introTicks == 0); // A quick same-level retry.
            TIDE_REQUIRE(play_witness(0, inside_out_solutions[0], NULL));
        } else {
            // Undo removed the witness's final south step. Solving again
            // must start a fresh delay, not reuse the cancelled deadline.
            TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_Move, 1, 2));
        }
        TIDE_REQUIRE(world.Workshop.puzzle.solved);
        const int again = world.Workshop.solvedTick;
        wait_ticks(149);
        TIDE_CHECK(world.Workshop.level == 0);
        tide_world_tick(&world);
        TIDE_CHECK(world.Workshop.level == 1 && world.Workshop.openedTick == again + 150);
        TIDE_CHECK(world.Workshop.introTicks == 0);
        tide_world_free(&world);
    }
}

TIDE_TEST(inside_out_automatic_progression_waits_for_the_missing_partner)
{
    inside_out_test_start(2);
    TIDE_REQUIRE(play_witness(0, inside_out_solutions[0], NULL));
    const Puzzle solved = world.Workshop.puzzle;
    wait_ticks(20);
    tide_world_player_left(&world, tide_player_from_index(1));
    tide_world_tick(&world);
    TIDE_REQUIRE(!world.Workshop.present.at[1]);
    wait_ticks(200);
    TIDE_CHECK(world.Workshop.level == 0);
    TIDE_CHECK(memcmp(&solved, &world.Workshop.puzzle, sizeof solved) == 0);
    // Both players get a complete readable hold after an overdue rejoin.
    const int rejoined_at = world.Time.tick;
    tide_world_player_joined(&world, tide_player_from_index(1));
    tide_world_tick(&world);
    TIDE_CHECK(world.Workshop.present.at[1] && world.Workshop.level == 0);
    TIDE_CHECK(world.Workshop.solvedTick == rejoined_at);
    wait_ticks(149);
    TIDE_CHECK(world.Workshop.level == 0 && world.Time.tick == rejoined_at + 150);
    TIDE_CHECK(memcmp(&solved, &world.Workshop.puzzle, sizeof solved) == 0);
    tide_world_tick(&world);
    TIDE_CHECK(world.Workshop.level == 1 && !world.Workshop.puzzle.solved);
    TIDE_CHECK(world.Workshop.openedTick == rejoined_at + 150 && world.Workshop.introTicks == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_last_campaign_puzzle_retains_its_solved_ending)
{
    inside_out_test_start(2);
    TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 0, PUZZLES - 1));
    TIDE_REQUIRE(play_witness(PUZZLES - 1, inside_out_solutions[PUZZLES - 1], NULL));
    const Puzzle solved = world.Workshop.puzzle;
    const int serial = world.Workshop.serial;
    const int solved_tick = world.Workshop.solvedTick;
    wait_ticks(216);
    TIDE_CHECK(world.Workshop.level == PUZZLES - 1 && world.Workshop.serial == serial);
    TIDE_CHECK(world.Workshop.solvedTick == solved_tick && world.Workshop.puzzle.solved);
    TIDE_CHECK((world.Workshop.completed & (1 << (PUZZLES - 1))) != 0);
    TIDE_CHECK(memcmp(&solved, &world.Workshop.puzzle, sizeof solved) == 0);
    tide_world_free(&world);
}

TIDE_TEST(inside_out_every_campaign_puzzle_has_a_production_rules_solution)
{
    for (int level = 0; level < PUZZLES; level++) {
        inside_out_test_start(2);
        TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 0, level));
        const Puzzle initial = world.Workshop.puzzle;
        const LevelLayout layout = world.Workshop.layout;
        TIDE_CHECK(initial.actors.at[0].room >= 0 && initial.actors.at[1].room >= 0);
        TIDE_CHECK(!initial.solved);
        TIDE_CHECK(layout.roomCount >= 1 && layout.roomCount <= 4 && layout.crateCount <= 8);
        for (int room = 0; room < layout.roomCount; room++) {
            // Every box is a square, so it fills the cell it stands in.
            TIDE_CHECK(layout.rooms.at[room].width >= 3 && layout.rooms.at[room].width <= 7);
            TIDE_CHECK(layout.rooms.at[room].height == layout.rooms.at[room].width);
            check_doorways(&layout, room);
            // Its box stands on an open cell of a room or of the table.
            const RoomShell box = initial.rooms.at[room];
            TIDE_CHECK(box.room >= -1 && box.room < layout.roomCount && tile_at(&layout, box.room, box.position) != Tile_Wall);
        }
        TIDE_CHECK(layout.width == layout.height && layout.width >= 1 && layout.width <= 7);
        for (int room = layout.roomCount; room < 4; room++) TIDE_CHECK(layout.rooms.at[room].dock.x == -1);
        int actions = 0;
        TIDE_REQUIRE(play_witness(level, inside_out_solutions[level], &actions));
        const Puzzle solved = world.Workshop.puzzle;
        TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_Undo, 1, 0));
        TIDE_CHECK(!world.Workshop.puzzle.solved && world.Workshop.puzzle.moves == actions - 1);
        TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_Reset, 0, 0));
        TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
        TIDE_REQUIRE(play_witness(level, inside_out_solutions[level], NULL));
        TIDE_CHECK(memcmp(&solved, &world.Workshop.puzzle, sizeof solved) == 0);
        printf("  Puzzle %d: %2d moves.\n", level + 1, actions);
        tide_world_free(&world);
    }
}
TIDE_TEST(inside_out_campaign_has_exactly_the_witnessed_puzzles)
{
    // The game opens the last witnessed puzzle and refuses the one after it.
    inside_out_test_start(2);
    TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 0, PUZZLES - 1));
    TIDE_CHECK(world.Workshop.level == PUZZLES - 1);
    const int serial = world.Workshop.serial;
    TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 1, PUZZLES));
    TIDE_CHECK(world.Workshop.level == PUZZLES - 1 && world.Workshop.serial == serial);
    tide_world_free(&world);
}
TIDE_TEST(inside_out_campaign_players_can_exchange_their_jobs)
{
    for (int level = 0; level < PUZZLES; level++) {
        inside_out_test_start(2);
        TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 0, level));
        const Traveler first = world.Workshop.puzzle.actors.at[0];
        world.Workshop.puzzle.actors.at[0] = world.Workshop.puzzle.actors.at[1];
        world.Workshop.puzzle.actors.at[1] = first;
        char swapped[512];
        const size_t length = strlen(inside_out_solutions[level]);
        TIDE_REQUIRE(length < sizeof swapped);
        for (size_t i = 0; i <= length; i++) {
            const char mark = inside_out_solutions[level][i];
            swapped[i] = mark >= 'A' && mark <= 'Z' ? (char)(mark + ('a' - 'A'))
                : mark >= 'a' && mark <= 'z' ? (char)(mark - ('a' - 'A')) : mark;
        }
        TIDE_REQUIRE(play_witness(level, swapped, NULL));
        tide_world_free(&world);
    }
}

// Completion except the robots' goals: every battery and socket, and every
// box outline. The absent robot must never make an otherwise trivial goal
// condition masquerade as a dependency on a partner.
static bool cargo_ready(const LevelLayout *const layout, const Puzzle *const puzzle)
{
    for (int crate = 0; crate < layout->crateCount; crate++) {
        const Cargo battery = puzzle->crates.at[crate];
        if (battery.active && tile_at(layout, battery.room, battery.position) != Tile_Target) return false;
    }
    for (int room = 0; room < layout->roomCount; room++) {
        const RoomLayout inside = layout->rooms.at[room];
        const RoomShell box = puzzle->rooms.at[room];
        if (inside.dock.x >= 0 && (box.room != inside.dockRoom || !same_cell(box.position, inside.dock))) return false;
    }
    for (int room = -1; room < layout->roomCount; room++) {
        const int size = room < 0 ? layout->width : layout->rooms.at[room].width;
        for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
            const tide_int2 at = tide_i2(x, y);
            if (tile_at(layout, room, at) != Tile_Target) continue;
            bool occupied = false;
            for (int crate = 0; crate < layout->crateCount; crate++) {
                const Cargo battery = puzzle->crates.at[crate];
                if (battery.active && battery.room == room && same_cell(battery.position, at)) occupied = true;
            }
            if (!occupied) return false;
        }
    }
    return true;
}
static uint32_t state_hash(const Puzzle *const puzzle)
{
    uint32_t hash = 2166136261u;
    const unsigned char *const bytes = (const unsigned char *)puzzle;
    for (size_t i = 0; i < sizeof *puzzle; i++) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}
static void search_state(Puzzle *const puzzle)
{
    puzzle->moves = 0;
    puzzle->solved = false;
    for (int seat = 0; seat < 2; seat++) puzzle->actors.at[seat].facing = 0;
}
// Exhaust every position either robot can reach with the other's body
// removed. A limit hit is an unsuccessful test, never an impossibility proof.
static int alone(int level, int seat, bool *done)
{
    enum { LIMIT = 1000000, BUCKETS = 1 << 22 };
    inside_out_test_start(2);
    if (!inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 0, level)) return -1;
    Puzzle *const states = malloc((size_t)LIMIT * sizeof *states);
    int *const table = malloc((size_t)BUCKETS * sizeof *table);
    if (!states || !table) {
        free(states);
        free(table);
        tide_world_free(&world);
        return -1;
    }
    memset(table, 0xff, (size_t)BUCKETS * sizeof *table);
    const LevelLayout layout = world.Workshop.layout;
    states[0] = world.Workshop.puzzle;
    states[0].actors.at[1 - seat].room = -1;
    states[0].actors.at[1 - seat].position = tide_i2(-16, -16);
    states[0].actors.at[1 - seat].depth = 0;
    for (int entry = 0; entry < 8; entry++) states[0].actors.at[1 - seat].trail.at[entry] = (Entry){.box = -1};
    search_state(&states[0]);
    table[state_hash(&states[0]) & (uint32_t)(BUCKETS - 1)] = 0;
    int count = 1;
    *done = false;
    for (int index = 0; index < count && count < LIMIT; index++) {
        const Puzzle current = states[index];
        if (cargo_ready(&layout, &current)) *done = true;
        for (int direction = 0; direction < 4; direction++) {
            world.Workshop.puzzle = current;
            world.Workshop.historyCount = 0;
            if (!inside_out_test_act(TIDE_COMMAND_ID_Move, seat, direction)) continue;
            Puzzle next = world.Workshop.puzzle;
            if (next.moves == current.moves) continue;
            search_state(&next);
            uint32_t slot = state_hash(&next) & (uint32_t)(BUCKETS - 1);
            bool seen = false;
            while (table[slot] >= 0) {
                if (memcmp(&states[table[slot]], &next, sizeof next) == 0) { seen = true; break; }
                slot = (slot + 1) & (uint32_t)(BUCKETS - 1);
            }
            if (seen || count >= LIMIT) continue;
            table[slot] = count;
            states[count++] = next;
        }
    }
    free(states);
    free(table);
    tide_world_free(&world);
    return count;
}
TIDE_TEST(inside_out_a_box_is_entered_only_with_the_partner_behind_it)
{
    // Every puzzle about the partner as a backstop, and every power puzzle,
    // requires both robots even with the player goals ignored. The later
    // movement/recursion puzzles retain that stronger dependency too.
    const int levels[] = {5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17};
    for (size_t index = 0; index < sizeof levels / sizeof levels[0]; index++)
        for (int seat = 0; seat < 2; seat++) {
            const int level = levels[index];
            bool done = true;
            const int count = alone(level, seat, &done);
            TIDE_CHECK(count > 0 && count < 1000000);
            TIDE_CHECK(!done);
            printf("  Puzzle %d, seat %d alone: %d positions, cargo and outlines never all ready.\n", level + 1, seat, count);
        }
}

TIDE_TEST(inside_out_recursive_campaign_uses_the_partner_to_enter_its_own_room)
{
    inside_out_test_start(2);
    TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 0, 14));
    TIDE_CHECK(world.Workshop.layout.roomCount == 1 && world.Workshop.puzzle.rooms.at[0].room == 0);
    TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_Move, 0, 1));
    for (int step = 0; step < 4; step++) TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_Move, 1, 2));
    TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_Move, 1, 0));
    const Puzzle before = world.Workshop.puzzle;
    TIDE_CHECK(same_cell(before.actors.at[0].position, tide_i2(2, 1)));
    TIDE_CHECK(same_cell(before.rooms.at[0].position, tide_i2(2, 2)));
    TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_Move, 0, 2));
    const Puzzle entered = world.Workshop.puzzle;
    TIDE_CHECK(entered.actors.at[0].room == 0);
    TIDE_CHECK(same_cell(entered.actors.at[0].position, tide_i2(5, 0)));
    TIDE_CHECK(same_cell(entered.rooms.at[0].position, before.rooms.at[0].position));
    world.Workshop.puzzle = before;
    world.Workshop.puzzle.actors.at[1].room = -1;
    world.Workshop.puzzle.actors.at[1].position = tide_i2(-16, -16);
    TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_Move, 0, 2));
    TIDE_CHECK(!same_cell(world.Workshop.puzzle.actors.at[0].position, entered.actors.at[0].position));
    TIDE_CHECK(!same_cell(world.Workshop.puzzle.rooms.at[0].position, before.rooms.at[0].position));
    tide_world_free(&world);
}

TIDE_TEST(inside_out_campaign_cycle_has_one_box_for_each_canonical_room)
{
    inside_out_test_start(2);
    TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 0, 16));
    const Puzzle initial = world.Workshop.puzzle;
    TIDE_CHECK(world.Workshop.layout.roomCount == 2);
    TIDE_CHECK(initial.rooms.at[0].room == 1 && initial.rooms.at[1].room == 0);
    TIDE_REQUIRE(play_witness(16, inside_out_solutions[16], NULL));
    TIDE_CHECK(world.Workshop.puzzle.crates.at[0].room == 1);
    TIDE_CHECK(world.Workshop.puzzle.crates.at[1].room == 0);
    tide_world_free(&world);
}
