#include "game.h"
#include "tide_test.h"

static tide_world world;

static Checks checked(void)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world); // Main's Spawned fills the grid as the first tick ends, if it hasn't already
    tide_world_tick(&world);
    return world.Checks;
}

TIDE_TEST(vectors_compare_whole)
{
    const Checks c = checked();
    TIDE_CHECK(c.ints);
    TIDE_CHECK(c.intsDiffer);
    TIDE_CHECK(c.floats);
    TIDE_CHECK(c.floatsDiffer);
    TIDE_CHECK(c.fours);
    TIDE_CHECK(c.doubles);
    TIDE_CHECK(c.doublesDiffer);
    TIDE_CHECK(c.defaults);
}

TIDE_TEST(vectors_widen_as_arithmetic_does)
{
    const Checks c = checked();
    TIDE_CHECK(c.widened);
    TIDE_CHECK(c.widenedDiffer);
    TIDE_CHECK(c.widenedDouble);
}

TIDE_TEST(vector_components_compare_as_c_compares_floats)
{
    const Checks c = checked();
    TIDE_CHECK(c.signedZero);
    TIDE_CHECK(!c.nanEqual);
    TIDE_CHECK(c.nanDiffer);
}

TIDE_TEST(colors_compare_every_channel)
{
    const Checks c = checked();
    TIDE_CHECK(c.colors);
    TIDE_CHECK(c.alphaDiffers);
}

TIDE_TEST(vector_equality_in_constants_and_defaults)
{
    tide_world_init(&world, 1.0f);
    TIDE_CHECK(world.Checks.constants);
    TIDE_CHECK(world.Checks.fieldDefault);
}

TIDE_TEST(vector_equality_works_each_side_once)
{
    const Checks c = checked();
    TIDE_CHECK(c.calls == 2); // int2(1, 0) == int2(2, 0): two calls, and false
}

TIDE_TEST(lists_find_vectors_and_colors)
{
    const Checks c = checked();
    TIDE_CHECK(c.found == 1);
    TIDE_CHECK(c.index == 2);
    TIDE_CHECK(c.removed);
    TIDE_CHECK(c.colorFound);
}

TIDE_TEST(vector_guards_skip_the_same_rows_indexed)
{
    const Checks c = checked();
    TIDE_CHECK(c.matched == 2);
    TIDE_CHECK(c.unmatched == 63);
}
