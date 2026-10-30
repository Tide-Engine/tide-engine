#include "game.h"
#include "tide_test.h"

static tide_world before;
static tide_world now;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

// A frame a quarter of the way from `before` to `now`, which is confirmed too.
static void frame(void)
{
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(1920.0f, 1080.0f), NULL);
    tide_frame(&now, &before, 0.25f, &now, &before, 0.25f, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

TIDE_TEST(field_snap_draws_the_field_as_it_is_for_the_interval_it_jumped)
{
    tide_local_init(&local);
    tide_world_init(&now, 1.0f / 20.0f);
    tide_world_tick(&now); // Tick 0: everything moves on
    tide_world_copy(&before, &now);
    tide_world_tick(&now); // Tick 1: the stance changes, the pose and the zoom jump

    frame();
    const Seen *seen = &local.Seen;
    TIDE_CHECK(seen->stance == 1 && seen->since == 0.0f); // The new stance, its time from 0: never 0.75 to 0 backwards
    TIDE_CHECK(seen->hand.x == 5.0f);                      // Its sibling blends: 4 then 8
    TIDE_CHECK(seen->angle == 100.0f && seen->reach == 3.0f); // A struct field snaps all of it
    TIDE_CHECK(seen->sway == 2.5f);                        // 2 then 4
    TIDE_CHECK(seen->zoom == 50.0f && seen->drift == 1.25f); // A singleton's field: alone
    TIDE_CHECK(seen->verifiedSince == 0.0f && seen->verifiedZoom == 50.0f); // Confirmed views the same
    TIDE_CHECK(seen->rowSince == 0.0f);                    // A view's query rows the same
    TIDE_CHECK(now.Lens.tide_snaps_zoom == 1 && before.Lens.tide_snaps_zoom == 0);

    tide_world_copy(&before, &now);
    tide_world_tick(&now); // Tick 2: an ordinary one, which blends again
    frame();
    TIDE_CHECK(seen->since == 0.0625f); // 0 then 0.25
    TIDE_CHECK(seen->hand.x == 9.0f);
    TIDE_CHECK(seen->angle == 102.5f && seen->reach == 3.0f);
    TIDE_CHECK(seen->zoom == 50.25f && seen->drift == 2.25f);
    TIDE_CHECK(seen->verifiedSince == 0.0625f && seen->rowSince == 0.0625f);

    // The count is the match's own: in its snapshots and its hash
    tide_world_copy(&before, &now);
    TIDE_CHECK(before.Lens.tide_snaps_zoom == 1 && tide_world_hash(&before) == tide_world_hash(&now));
    before.Lens.tide_snaps_zoom = 2;
    TIDE_CHECK(tide_world_hash(&before) != tide_world_hash(&now));

    tide_local_free(&local);
    tide_world_free(&before);
    tide_world_free(&now);
}
