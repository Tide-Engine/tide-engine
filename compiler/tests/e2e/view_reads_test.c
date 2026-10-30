#include "game.h"
#include "tide_test.h"

static tide_world before;
static tide_world now;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

static void frame(const tide_world *previous, const float alpha)
{
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(640.0f, 360.0f), NULL);
    tide_frame(&now, previous, alpha, NULL, NULL, 1.0f, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

// Each tick moves x by 4, y by 1, dir.z by 8 and pair.b by 2; on the second,
// `since` jumps to 50 alone and the Hops entity to x = 1000 whole.
TIDE_TEST(view_reads_some_lanes_see_what_a_whole_read_sees)
{
    tide_local_init(&local);
    tide_world_init(&now, 1.0f / 20.0f);
    tide_world_tick(&now);
    tide_world_copy(&before, &now);
    tide_world_tick(&now);

    frame(&before, 0.25f);
    const Seen *s = &local.Seen;
    TIDE_CHECK(s->x == 5.0f && s->wholeX == 5.0f);
    TIDE_CHECK(s->dirZ == 10.0f && s->wholeDirZ == 10.0f);
    TIDE_CHECK(s->pairB == 2.5f && s->wholePairB == 2.5f);
    TIDE_CHECK(s->helperY == 2.5f && s->wholeY == 1.25f); // Through a helper handed it whole
    TIDE_CHECK(s->since == 50.0f && s->wholeSince == 50.0f); // Jumped alone: as it is
    TIDE_CHECK(s->radius == 3.0f); // [Snap]: as it is
    TIDE_CHECK(s->queryRows == 1 && s->queryX == 5.0f && s->queryPairB == 2.5f);
    TIDE_CHECK(s->hopX == 1000.0f); // Jumped whole: as it is
    TIDE_CHECK(s->localX == 5.0f && s->marks == 1 && s->spotX == 0.5f);
    TIDE_CHECK(local.Counts.kept == 7 && local.Counts.counted == 1); // The list as the call began, not with what it added

    frame(&before, 0.5f);
    TIDE_CHECK(s->x == 6.0f && s->wholeX == 6.0f && s->queryX == 6.0f && s->localX == 6.0f);
    TIDE_CHECK(s->marks == 2 && s->spotX == 1.0f); // The entity's local fields, kept from the frame before
    TIDE_CHECK(local.Counts.kept == 7 && local.Counts.counted == 2);

    frame(NULL, 1.0f); // No tick before: the latest as it is
    TIDE_CHECK(s->x == 8.0f && s->wholeX == 8.0f && s->queryX == 8.0f && s->pairB == 4.0f && s->helperY == 4.0f);

    tide_local_free(&local);
    tide_world_free(&before);
    tide_world_free(&now);
    tide_draw_free(&draw);
}
