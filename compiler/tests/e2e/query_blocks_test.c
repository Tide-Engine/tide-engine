#include "game.h"
#include "tide_test.h"

// A `Sort` with a comparison block, and a parallel loop, in a system or a view
// that also takes a query (`query (Racer racer) racers`). The block and the
// loop's step are functions of their own, given the parameters of the code
// around them: a query among those is no C value, and the generated C named
// it all the same, so such a game didn't build.

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

// Main occupies slot 0; these are Setup's spawn handles.
static const tide_entity THREE = {1, 1};
static const tide_entity NINE = {2, 1};
static const tide_entity ONE = {3, 1};
static const tide_entity FIVE = {4, 1};

TIDE_TEST(query_blocks_sort_and_loop_beside_a_query)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world); // Setup's spawns come in at the end of this tick
    tide_world_tick(&world);
    TIDE_CHECK(world.Board.racers == 4 && world.Board.first == 9 && world.Board.last == 1);
    TIDE_CHECK(world.Board.doubledSum == 36);

    TIDE_CHECK(tide_get_Racer(&world, NINE).place == 0 && tide_get_Racer(&world, NINE).nearest == 0);
    TIDE_CHECK(tide_get_Racer(&world, FIVE).place == 1 && tide_get_Racer(&world, FIVE).nearest == 9);
    TIDE_CHECK(tide_get_Racer(&world, THREE).place == 2 && tide_get_Racer(&world, THREE).nearest == 5);
    TIDE_CHECK(tide_get_Racer(&world, ONE).place == 3 && tide_get_Racer(&world, ONE).nearest == 3);

    tide_local_init(&local);
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Shown.count == 4 && local.Shown.lowest == 1 && local.Shown.highest == 9);

    tide_local_free(&local);
    tide_world_free(&world);
}
