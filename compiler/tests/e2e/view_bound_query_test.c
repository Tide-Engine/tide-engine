#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

// The view's query goes through every pane, though the view was handed another parameter after it was checked: one
// instance of the mesh for each.
TIDE_TEST(view_with_a_bound_singleton_runs_its_query)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    uint32_t instances = 0;
    for (uint32_t i = 0; i < draw.count; i++)
        if (draw.commands[i].kind == TIDE_DRAW_SHADED) instances += draw.runs[draw.commands[i].shaded.run].count;
    TIDE_CHECK(instances == 3);
}
