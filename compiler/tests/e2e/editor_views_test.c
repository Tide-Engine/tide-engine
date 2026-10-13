#include "game.h"
#include "tide_test.h"

static int observed;
void Observe(int tag) { observed += tag; }

TIDE_TEST(editor_views_game_selection)
{
    tide_world world = {0}; tide_local local = {0}; tide_draw_list draw = {0}; tide_gui gui = {0};
    observed = 0;
    tide_world_init(&world, 1.0f/60.0f);
    TIDE_CHECK(observed == 35); // Main and both authored entities receive Spawned.
    tide_local_init(&local);
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(observed == 38); // The ordinary view runs in the game.
    TIDE_REQUIRE(draw.count == 2);
    for (uint32_t i = 0; i < draw.count; i++) {
        TIDE_CHECK(draw.commands[i].kind == TIDE_DRAW_CIRCLE);
        TIDE_CHECK(draw.commands[i].a.y == 0); // Neither EditorOnly view executes.
        TIDE_CHECK(draw.commands[i].color.g == 1);
    }
    tide_world_tick(&world);
    TIDE_CHECK(observed == 42); // Both systems run in the game.
    tide_world_free(&world); tide_local_free(&local); tide_draw_free(&draw);
}
