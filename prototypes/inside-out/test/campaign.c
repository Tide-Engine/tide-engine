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
#define PUZZLES 9
static bool same_cell(tide_int2 a, tide_int2 b) { return a.x == b.x && a.y == b.y; }
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
TIDE_TEST(inside_out_every_campaign_puzzle_has_a_production_rules_solution)
{
    for (int level = 0; level < PUZZLES; level++) {
        inside_out_test_start(2);
        TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 0, level));
        const Puzzle initial = world.Workshop.puzzle;
        const LevelLayout layout = world.Workshop.layout;
        TIDE_CHECK(initial.actors.at[0].room >= 0 && initial.actors.at[1].room >= 0);
        TIDE_CHECK(!initial.solved);
        TIDE_CHECK(layout.roomCount >= 2 && layout.roomCount <= 4 && layout.crateCount <= 8);
        for (int room = 0; room < layout.roomCount; room++) {
            // Every box is a square, so it fills the cell it stands in.
            TIDE_CHECK(layout.rooms.at[room].width >= 3 && layout.rooms.at[room].width <= 7);
            TIDE_CHECK(layout.rooms.at[room].height == layout.rooms.at[room].width);
        }
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

// Every position either bot can reach acting alone, with the other bot taken
// off the board: more room than merely leaving it standing. Returns how many
// there are, and whether any of them has the small box on its outline with
// its battery seated, which is all of the puzzle but the robots' goals.
static int alone(int level, int seat, bool *done)
{
    enum { LIMIT = 600000, BUCKETS = 1 << 21 };
    inside_out_test_start(2);
    if (!inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 0, level)) return -1;
    Puzzle *const states = malloc((size_t)LIMIT * sizeof *states);
    int *const table = malloc((size_t)BUCKETS * sizeof *table);
    if (!states || !table) return -1;
    memset(table, 0xff, (size_t)BUCKETS * sizeof *table);
    const LevelLayout layout = world.Workshop.layout;
    states[0] = world.Workshop.puzzle;
    states[0].actors.at[1 - seat].room = -1;
    states[0].actors.at[1 - seat].position = tide_i2(0, 0);
    int count = 1;
    *done = false;
    for (int index = 0; index < count && count < LIMIT; index++) {
        const Puzzle current = states[index];
        const RoomShell box = current.rooms.at[2];
        const Cargo battery = current.crates.at[0];
        const RoomLayout *inside = &layout.rooms.at[2];
        if (box.room == layout.rooms.at[2].dockRoom && same_cell(box.position, layout.rooms.at[2].dock) && battery.room == 2
            && inside->tiles.at[battery.position.y * inside->width + battery.position.x] == Tile_Target) *done = true;
        for (int direction = 0; direction < 4; direction++) {
            world.Workshop.puzzle = current;
            world.Workshop.historyCount = 0;
            if (!inside_out_test_act(TIDE_COMMAND_ID_Move, seat, direction)) continue;
            Puzzle next = world.Workshop.puzzle;
            if (next.moves == current.moves) continue;
            next.moves = 0;
            next.solved = false;
            uint32_t hash = 2166136261u;
            const unsigned char *bytes = (const unsigned char *)&next;
            for (size_t i = 0; i < sizeof next; i++) hash = (hash ^ bytes[i]) * 16777619u;
            uint32_t slot = hash & (uint32_t)(BUCKETS - 1);
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
    // "Hold it steady" and "Posted": the small box has to end on its outline
    // with its battery seated inside. A bot gets into a box only while the
    // box cannot move, and here the only thing that can stop it where it
    // has to stand is the other bot. Alone, neither bot can ever do both.
    for (int level = 5; level <= 6; level++)
        for (int seat = 0; seat < 2; seat++) {
            bool done = true;
            const int count = alone(level, seat, &done);
            TIDE_CHECK(count > 0 && count < 600000);
            TIDE_CHECK(!done);
            printf("  Puzzle %d, seat %d alone: %d positions, the box never parked with its battery seated.\n", level + 1, seat, count);
        }
}
