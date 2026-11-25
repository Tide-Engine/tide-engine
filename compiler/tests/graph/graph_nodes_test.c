// A game written as a graph (graph_nodes.tidegraph), with the nodes a .tide file made a graph doesn't have: a
// statement whose value is kept, text and code nodes with inputs, a value wired to two inputs, an else-if chain,
// reroutes (knots on wires) and nodes nothing reaches (notes/visual-scripting.md).
#include "game.h"
#include "tide_test.h"

static tide_world world;

TIDE_TEST(graph_nodes_run_as_written)
{
    tide_world_init(&world, 1.0f);
    TIDE_CHECK(world.Results.summed == 0);

    tide_world_tick(&world);
    TIDE_CHECK(world.Results.counted == 1); // Bump ran once...
    TIDE_CHECK(world.Results.kept == 2);    // ...and its value was read twice
    TIDE_CHECK(world.Results.text == 13);   // a * b + 1, a 3 and b 4
    TIDE_CHECK(world.Results.code == 10);   // Twice(n), n 5
    TIDE_CHECK(world.Results.summed == 5);
    TIDE_CHECK(world.Results.fanned == 14);
    TIDE_CHECK(world.Results.chain == 1);
    TIDE_CHECK(world.Results.knotted == 64); // -(-3) + 7 * 7 + Twice(6), each through a knot

    tide_world_tick(&world);
    TIDE_CHECK(world.Results.counted == 2);
    TIDE_CHECK(world.Results.kept == 4);
    TIDE_CHECK(world.Results.summed == 10);
    TIDE_CHECK(world.Results.chain == 2);

    tide_world_tick(&world);
    TIDE_CHECK(world.Results.chain == 3);
}
