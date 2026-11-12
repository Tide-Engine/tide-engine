// A view's guard turns rows away before they're copied, and the view does
// what it does without one (view_guard.tide).

#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

TIDE_TEST(view_guard_gives_what_the_code_alone_gives)
{
    static tide_world world, before;
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    static tide_devices devices;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    int32_t chosen = 0, nested = 0;
    for (int frame = 1; frame <= 12; frame++) {
        tide_world_copy(&before, &world);
        tide_world_tick(&world);
        const int32_t guarded_before = local.Show.guardedRows, nested_before = local.Show.nestedRows;
        const int32_t fresh_before = local.Show.freshRows;
        tide_draw_reset(&draw);
        tide_gui_begin(&gui, &devices, tide_f2(640.0f, 360.0f), NULL);
        tide_frame(&world, &before, 0.5f, NULL, NULL, 1.0f, &local, &draw, &gui);
        tide_gui_end(&gui, &draw);
        TIDE_CHECK(local.Show.frame == frame);
        // Every man's default passes, whether anything was written for him yet or not
        TIDE_CHECK(local.Show.freshRows - fresh_before == 600);
        // Each frame's rows and sum, bit for bit, as the view with no guard has them
        TIDE_CHECK(local.Show.guardedRows == local.Show.plainRows);
        TIDE_CHECK(memcmp(&local.Show.guarded, &local.Show.plain, sizeof(float)) == 0);
        // Off every third frame: the singleton's test turns every row away
        if (frame % 3 == 0) TIDE_CHECK(local.Show.guardedRows == guarded_before);
        if (frame % 2 == 1) TIDE_CHECK(local.Show.nestedRows == nested_before);
        chosen = local.Show.guardedRows;
        nested = local.Show.nestedRows;
    }
    TIDE_CHECK(chosen > 100 && chosen < 3000); // Some rows, far from all 7,200
    TIDE_CHECK(nested > 50);
    TIDE_CHECK(local.Show.drawn > 0); // The guarded view's own writes to the rows it let through
    if (chosen <= 100 || nested <= 50) printf("    %d chosen, %d nested\n", (int)chosen, (int)nested);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&before);
    tide_world_free(&world);
}
