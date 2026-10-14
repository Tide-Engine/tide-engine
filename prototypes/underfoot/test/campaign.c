// What is claimed of the puzzles, checked against the game itself. The search
// runs on test/model.h's statement of the rules, so first the two are held
// together: thousands of moves on every puzzle and on boards crowded with
// every kind of ground, each made in both and compared. Then every puzzle is
// searched from the board the game loads, its shortest way must be the number
// the game tells players, and that way is played through the real commands.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"
#include "test_helpers.h"
#include "search.h"

#define world underfoot_test_world
#define yard world.Yard
#define LEVELS 18

static uint32_t dice;
static uint32_t cast(const uint32_t below)
{
    dice = dice * 1664525u + 1013904223u;
    return (dice >> 8) % below;
}

// The same moves in the game and in the model, compared after each.
static bool wander(const int steps, const bool start_over)
{
    uf_layout layout;
    uf_state model;
    underfoot_test_to_model(&layout, &model);
    const uf_state first = model;
    for (int step = 0; step < steps; step++) {
        const int paver = (int)cast(2);
        // Walking is what gets a paver anywhere; hands are tried half as often.
        const int action = cast(3) ? (int)cast(4) : 4 + (int)cast(4);
        const int moves = yard.board.moves;
        const bool expected = uf_act(&layout, &model, paver, action);
        underfoot_test_act(0, paver, action);
        if ((yard.board.moves == moves + 1) != expected || !underfoot_test_same(&layout, &model)) {
            fprintf(stderr, "  step %d: paver %d action %d: the model %s, the game %s\n", step, paver, action,
                expected ? "moved" : "refused", yard.board.moves == moves + 1 ? "moved" : "refused");
            return false;
        }
        if (yard.board.solved || (start_over && cast(400) == 0)) {
            if (!start_over) return true;
            underfoot_test_do(TIDE_COMMAND_ID_Reset, 0, 0);
            model = first;
            if (!underfoot_test_same(&layout, &model)) return false;
        }
        else if (cast(40) == 0 && yard.historyCount > 0) {
            // Undo is the game's alone: the model steps back by replaying nothing,
            // so take the game's board as the model's from here.
            underfoot_test_do(TIDE_COMMAND_ID_Undo, 0, 0);
            underfoot_test_to_model(&layout, &model);
        }
    }
    return true;
}

TIDE_TEST(underfoot_the_game_and_the_search_model_agree_move_for_move)
{
    dice = 20261011u;
    for (int level = 0; level < LEVELS; level++) {
        underfoot_test_start(1);
        TIDE_REQUIRE(underfoot_test_do(TIDE_COMMAND_ID_ChooseLevel, 0, level));
        TIDE_REQUIRE(yard.level == level);
        TIDE_REQUIRE(wander(2500, true));
    }
    // Boards with everything close together, where the rarer rules meet often.
    static const char *const labs[][7] = {
        {"# # # # # # # # #", "# .@o I i ~ .xo #", "# o . I ~ o . . #", "# ~ i .xI . ~ o #", "# . o ~ I o I .&#", "# G . . ~ . . G #", "# # # # # # # # #"},
        {"# # # # # # # # #", "# .@p . ~ o .xq #", "# o . ~ I . ~ . #", "# . .xI I ~ o . #", "# ~ . ~ . . . .&#", "# G o . . ~ . G #", "# # # # # # # # #"},
        {"# # # # # # # # #", "# .@a 1 . ~ .xb #", "# o . . 2 o . . #", "# ~ .x. 1 ~ . o #", "# . o ~ . 2 . .&#", "# G . A ~ B . G #", "# # # # # # # # #"},
        {"# # # # # # # # #", "# .@a I p ~ .xo #", "# o .xI 1 o . q #", "# ~ i . I ~ .x. #", "# . o ~ 1 . i .&#", "# G . . ~ . . G #", "# # # # # # # # #"},
        {"# # # # # # # # #", "# .@P I I I Q .&#", "# .x. I .xI . . #", "# I I I I I I I #", "# . .x. ~ . . . #", "# G . ~ ~ ~ . G #", "# # # # # # # # #"},
    };
    for (size_t lab = 0; lab < sizeof labs / sizeof labs[0]; lab++)
        for (int round = 0; round < 12; round++) {
            underfoot_test_lab(labs[lab]);
            TIDE_REQUIRE(wander(1500, false));
        }
    tide_world_free(&world);
}

TIDE_TEST(underfoot_every_puzzle_is_solved_in_exactly_the_moves_it_says)
{
    node_limit = 3000000;
    table_mask = (1u << 23) - 1;
    nodes = malloc(sizeof(node) * node_limit);
    table = malloc(sizeof(uint32_t) * ((size_t)table_mask + 1));
    TIDE_REQUIRE(nodes != NULL && table != NULL);
    for (int level = 0; level < LEVELS; level++) {
        underfoot_test_start(1);
        TIDE_REQUIRE(underfoot_test_do(TIDE_COMMAND_ID_ChooseLevel, 0, level));
        const Board initial = yard.board;
        uf_layout layout;
        uf_state state;
        underfoot_test_to_model(&layout, &state);
        TIDE_CHECK(!yard.board.solved && yard.board.paver.at[0] >= 0 && yard.board.paver.at[1] >= 0);
        int lanterns = 0;
        for (int cell = 0; cell < UF_CELLS; cell++) lanterns += layout.goal[cell];
        TIDE_CHECK(lanterns == 2);

        const result found = solve(&layout, &state, false);
        TIDE_REQUIRE(found.solved);
        // The number players are told is the fewest moves there are.
        if (found.moves != yard.layout.par) fprintf(stderr, "  Puzzle %d says %d, and the search finds %d.\n", level + 1, yard.layout.par, found.moves);
        TIDE_CHECK(found.moves == yard.layout.par);

        // That way, through the commands a player sends.
        int hands[2] = {0, 0};
        for (const char *mark = found.path; *mark; mark++) {
            const int paver = *mark >= 'a';
            const int action = (int)(strchr(marks[paver], *mark) - marks[paver]);
            const int moves = yard.board.moves;
            underfoot_test_act(0, paver, action);
            TIDE_REQUIRE(yard.board.moves == moves + 1);
            hands[paver] += action >= 4;
        }
        TIDE_CHECK(yard.board.solved && yard.board.moves == found.moves && (yard.completed & (1 << level)) != 0);
        TIDE_CHECK(yard.best.at[level] == found.moves);
        TIDE_REQUIRE(underfoot_test_do(TIDE_COMMAND_ID_Undo, 0, 0));
        TIDE_CHECK(!yard.board.solved);
        TIDE_REQUIRE(underfoot_test_do(TIDE_COMMAND_ID_Reset, 0, 0));
        TIDE_CHECK(memcmp(&initial, &yard.board, sizeof initial) == 0);
        printf("  Puzzle %2d: %3d moves at the least, %2d and %2d of them hands; %d positions searched.\n",
            level + 1, found.moves, hands[0], hands[1], found.states);
    }
    free(nodes);
    free(table);
    nodes = NULL;
    table = NULL;
    tide_world_free(&world);
}
