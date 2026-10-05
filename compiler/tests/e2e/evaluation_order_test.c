#include "game.h"
#include "tide_test.h"

static tide_world world;

// The index-th entity Main's Setup spawns: the Main scene itself is the first entity.
static tide_entity slot(const uint32_t index)
{
    return (tide_entity){index + 1, 1};
}

static int32_t thing_id(const uint32_t index)
{
    if (!tide_has_Thing(&world, slot(index))) return -1;
    return tide_get_Thing(&world, slot(index)).id;
}

TIDE_TEST(evaluation_order_siblings_left_to_right)
{
    tide_world_init(&world, 1.0f);
    TIDE_CHECK(thing_id(0) == 1);
    TIDE_CHECK(thing_id(1) == 2);
    const Pair p = tide_get_Pair(&world, slot(2));
    TIDE_REQUIRE(tide_has_Pair(&world, slot(2)));
    TIDE_CHECK(tide_entity_equal(p.a, slot(0)));
    TIDE_CHECK(tide_entity_equal(p.b, slot(1)));
}

TIDE_TEST(evaluation_order_follows_source_not_field_order)
{
    tide_world_init(&world, 1.0f);
    TIDE_CHECK(thing_id(3) == 4);
    TIDE_CHECK(thing_id(4) == 3);
    const Pair p = tide_get_Pair(&world, slot(5));
    TIDE_REQUIRE(tide_has_Pair(&world, slot(5)));
    TIDE_CHECK(tide_entity_equal(p.a, slot(4)));
    TIDE_CHECK(tide_entity_equal(p.b, slot(3)));
}

TIDE_TEST(evaluation_order_method_object_before_arguments)
{
    tide_world_init(&world, 1.0f);
    TIDE_CHECK(thing_id(6) == 5);
    TIDE_CHECK(thing_id(7) == 6);
    const Link l = tide_get_Link(&world, slot(6));
    TIDE_REQUIRE(tide_has_Link(&world, slot(6)));
    TIDE_CHECK(tide_entity_equal(l.target, slot(7)));
}

// Functions and methods that change a mut argument, or the value they're
// called on: two in one expression run left to right.
TIDE_TEST(evaluation_order_changing_calls_left_to_right)
{
    tide_world_init(&world, 1.0f);
    const Order o = world.Order;
    TIDE_CHECK(o.functions == 12);
    TIDE_CHECK(o.methods == 34);
    TIDE_CHECK(o.vector.x == 5 && o.vector.y == 6);
    TIDE_CHECK(o.nested == 7 * 10 + (8 * 10 + 9));
    TIDE_CHECK(o.conditions == 12);
    TIDE_CHECK(o.skipped == 2); // The right side of && never ran
    TIDE_CHECK(o.looped == 3 && o.counter.n == 16);
}

// A Random's methods step it, so they're calls like those: the same numbers,
// in the same places, as rolling one after another.
TIDE_TEST(evaluation_order_rolls_left_to_right)
{
    tide_world_init(&world, 1.0f);
    tide_random r = tide_random_seeded(1);
    const float x = tide_random_next_float(&r);
    const float y = tide_random_next_float(&r);
    const int32_t tens = tide_random_next_int_below(&r, 10);
    const int32_t ones = tide_random_next_int_below(&r, 10);
    TIDE_CHECK(world.Order.rolled.x == x && world.Order.rolled.y == y);
    TIDE_CHECK(world.Order.digits == tens * 10 + ones);
    TIDE_CHECK(world.Order.random.state == r.state);
}
