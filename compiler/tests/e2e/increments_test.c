#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

TIDE_TEST(increments_give_the_value_after_or_before)
{
    tide_world_init(&world, 1.0f);
    const Out *out = &world.Out;
    TIDE_CHECK(out->prefix == 6);
    TIDE_CHECK(out->prefixLeft == 6);
    TIDE_CHECK(out->postfix == 6);
    TIDE_CHECK(out->postfixLeft == 7);
    TIDE_CHECK(out->down == 6);
    TIDE_CHECK(out->downAfter == 6);
    TIDE_CHECK(out->downLeft == 5);
}

TIDE_TEST(increments_keep_their_turn)
{
    tide_world_init(&world, 1.0f);
    const Out *out = &world.Out;
    TIDE_CHECK(out->before == 3);
    TIDE_CHECK(out->after == 4);
    TIDE_CHECK(out->twice == 3);
    TIDE_CHECK(out->packed == 124);
    TIDE_CHECK(out->compound == 134);
    TIDE_CHECK(out->returned == 89);
}

TIDE_TEST(increments_run_only_where_their_part_does)
{
    tide_world_init(&world, 1.0f);
    const Out *out = &world.Out;
    TIDE_CHECK(out->chosen == 7);
    TIDE_CHECK(out->chosenCount == 2);
    TIDE_CHECK(!out->stops);
    TIDE_CHECK(out->stopsCount == 0);
    TIDE_CHECK(out->runs);
    TIDE_CHECK(out->runsCount == 1);
    TIDE_CHECK(out->oneSide == 0);
    TIDE_CHECK(out->oneSideLeft == 1);
    TIDE_CHECK(out->otherSideLeft == 0);
    TIDE_CHECK(out->rounds == 5);
    TIDE_CHECK(out->roundsLeft == 6);
    TIDE_CHECK(out->sum == 10);
}

TIDE_TEST(increments_of_fields_and_elements)
{
    tide_world_init(&world, 1.0f);
    const Out *out = &world.Out;
    TIDE_CHECK(out->field == 3);
    TIDE_CHECK(out->slot.at == 2);
    TIDE_CHECK(out->level == 1.0f);
    TIDE_CHECK(out->slot.level == 1.0f);
    TIDE_CHECK(out->element == 20);
    TIDE_CHECK(out->elementIndex == 31);
    TIDE_CHECK(out->indexLeft == 2);
    TIDE_CHECK(out->score == 5);
    TIDE_CHECK(out->cell == 13);
    TIDE_CHECK(out->cells.at[2] == 7);
    TIDE_CHECK(out->wideWas == 4000000000ll);
    TIDE_CHECK(out->wide == 4000000001ll);
    TIDE_CHECK(out->smallWas == 255);
    TIDE_CHECK(out->small == 0);
    TIDE_CHECK(out->real == 2.75f);
}

TIDE_TEST(increments_in_systems_and_views)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_world_tick(&world);
    TIDE_CHECK(world.Out.ticks == 2);
    TIDE_CHECK(world.Out.ticked == 2);

    tide_local_init(&local);
    for (int frame = 0; frame < 3; frame++) {
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    }
    TIDE_CHECK(local.Seen.frames == 3);
    TIDE_CHECK(local.Seen.odd == 2);
    TIDE_CHECK(local.Seen.even == 1);
}
