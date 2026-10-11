// Campaign evidence uses the exact generated game commands, rather than a
// second implementation of crate, doorway or latch rules. Every individual
// move must succeed, every final state must satisfy production IsSolved, and
// undo/reset/replay must recover the same puzzle bytes.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"
#include "test_helpers.h"
#include "solutions.h"

#define world inside_out_test_world

static bool equal_cell(const tide_int2 a, const tide_int2 b)
{
    return a.x == b.x && a.y == b.y;
}

static bool play_witness(const int level, const char *const witness, int *const actions)
{
    int used = 0;
    unsigned seats = 0;
    unsigned scales = 0;
    const char *const upper_keys = "NESW";
    const char *const lower_keys = "nesw";
    for (const char *action = witness; *action; action++) {
        const char *const upper = strchr(upper_keys, *action);
        const char *const lower = strchr(lower_keys, *action);
        if (!upper && !lower) continue;
        const int seat = upper ? 0 : 1;
        const int direction = upper ? (int)(upper - upper_keys) : (int)(lower - lower_keys);
        const Puzzle before = world.Workshop.puzzle;
        if (!inside_out_test_act(TIDE_COMMAND_ID_Move, seat, direction) || world.Workshop.puzzle.moves != before.moves + 1) {
            fprintf(stderr, "Level %d witness action %d (%c) blocked: outer (%d,%d), inner room %d (%d,%d).\n",
                level + 1, used + 1, *action, before.outside.x, before.outside.y,
                before.insideRoom, before.inside.x, before.inside.y);
            return false;
        }
        seats |= 1u << seat;
        scales |= 1u << (seat == before.smallSeat ? 1 : 0);
        used++;
    }
    if (actions) *actions = used;
    return world.Workshop.puzzle.solved && seats == 3 && scales == 3;
}

TIDE_TEST(inside_out_every_campaign_puzzle_has_a_production_rules_solution)
{
    for (int level = 0; level < 12; level++) {
        inside_out_test_start(2);
        TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 0, level));
        TIDE_CHECK(world.Workshop.level == level);
        const Puzzle initial = world.Workshop.puzzle;
        const LevelLayout layout = world.Workshop.layout;

        // Necessity of both scales is structural: neither avatar starts on an
        // exit, only an action at its scale can move it, and completion needs
        // both exits. Hatch swaps ownership but does not move either avatar.
        TIDE_CHECK(layout.tiles.at[initial.outside.y * 7 + initial.outside.x] != Tile_Exit);
        TIDE_CHECK(layout.rooms.at[initial.insideRoom].tiles.at[initial.inside.y * 5 + initial.inside.x] != Tile_Exit);
        TIDE_CHECK(!initial.solved);
        TIDE_CHECK(layout.roomCount >= 1 && layout.roomCount <= 3);
        TIDE_CHECK(layout.crateCount <= 2);
        for (int room = layout.roomCount; room < 4; room++) TIDE_CHECK(layout.rooms.at[room].dock.x == -1);

        int actions = 0;
        TIDE_REQUIRE(play_witness(level, inside_out_solutions[level], &actions));
        TIDE_CHECK(!equal_cell(world.Workshop.puzzle.outside, initial.outside));
        TIDE_CHECK(world.Workshop.puzzle.insideRoom != initial.insideRoom || !equal_cell(world.Workshop.puzzle.inside, initial.inside));
        const Puzzle solved = world.Workshop.puzzle;

        // A solved state is reviewable and reversible; no automatic level
        // advance steals the chance to inspect it or undo the final step.
        TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_Undo, 1, 0));
        TIDE_CHECK(!world.Workshop.puzzle.solved);
        TIDE_CHECK(world.Workshop.puzzle.moves == actions - 1);
        TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_Reset, 0, 0));
        TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
        TIDE_REQUIRE(play_witness(level, inside_out_solutions[level], NULL));
        TIDE_CHECK(memcmp(&solved, &world.Workshop.puzzle, sizeof solved) == 0);
        printf("  Workshop %2d: %2d successful production moves, both seats and both scales.\n", level + 1, actions);
        tide_world_free(&world);
    }
}

TIDE_TEST(inside_out_campaign_role_exchange_keeps_every_puzzle_solvable)
{
    // In Return Post the small partner starts on a hatch and the other stands
    // beside the room. Both can try the opposite scale without changing the
    // geometry, including the subsequently imported doorway crate.
    inside_out_test_start(2);
    TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 0, 5));
    TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_Swap, 0, 0));
    TIDE_CHECK(world.Workshop.puzzle.smallSeat == 0);
    char swapped[256];
    const size_t length = strlen(inside_out_solutions[5]);
    TIDE_REQUIRE(length < sizeof swapped);
    for (size_t i = 0; i <= length; i++) {
        const char mark = inside_out_solutions[5][i];
        swapped[i] = mark >= 'A' && mark <= 'Z' ? (char)(mark + ('a' - 'A'))
            : mark >= 'a' && mark <= 'z' ? (char)(mark - ('a' - 'A')) : mark;
    }
    TIDE_REQUIRE(play_witness(5, swapped, NULL));
    tide_world_free(&world);
}
