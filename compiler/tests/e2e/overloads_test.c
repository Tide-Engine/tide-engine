#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void one_tick(void)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
}

static bool text_is(const tide_text field, const char *expected)
{
    return strcmp(tide_str_c(tide_text_read(&world.heap, field)), expected) == 0;
}

TIDE_TEST(overloads_defaults_fill_what_a_call_leaves_out)
{
    one_tick();
    TIDE_CHECK(world.Log.plain == 20.0f);   // multiplier = BASE
    TIDE_CHECK(world.Log.two == 30.0f);     // crit = false
    TIDE_CHECK(text_is(world.Log.label, "none x3")); // Text, and an operator on constants
    TIDE_CHECK(world.Log.offset.x == 2.0f && world.Log.offset.y == 4.0f); // A constructor
    TIDE_CHECK(world.Log.maybe == -1 && world.Log.some == 4); // null, the nothing of a T?
    TIDE_CHECK(world.Log.small == 10);      // An int constant where a byte goes
    TIDE_CHECK(world.Log.sum == 16.0f);     // A list
}

TIDE_TEST(overloads_arguments_go_by_name)
{
    one_tick();
    TIDE_CHECK(world.Log.named == 40.0f);     // crit, past multiplier
    TIDE_CHECK(world.Log.reordered == 30.0f); // In another order than the parameters'
    TIDE_CHECK(world.Log.element == 2.5f);    // The last one, past two
    TIDE_CHECK(text_is(world.Log.named_label, "none x5"));
    TIDE_CHECK(world.Log.sharpened == 6.0f);  // A mut method: Sharpen(), then Sharpen(by: 2)
}

// Pack(ones: Step(n), hundreds: Step(n), tens: Step(n)): each Step runs in its
// turn as written, whatever order the parameters are in and C would pick.
TIDE_TEST(overloads_named_arguments_run_in_the_order_written)
{
    one_tick();
    TIDE_CHECK(world.Log.order == 231);
}

TIDE_TEST(overloads_context_typed_lists_keep_nested_out_locals_in_written_order)
{
    one_tick();
    TIDE_CHECK(world.Log.listedOut == 210);
    TIDE_CHECK(world.Log.namedListedOut == 420);
    TIDE_CHECK(world.Log.nestedListedOut == 660);
    TIDE_CHECK(world.Log.arrayListedOut == 840);
    TIDE_CHECK(world.Log.setListedOut == 250);
    TIDE_CHECK(world.Log.contextualList == 2560);
    TIDE_CHECK(world.Log.listCalls == 5);
}

TIDE_TEST(overloads_a_call_picks_by_its_arguments)
{
    one_tick();
    TIDE_CHECK(world.Log.weapon == 13.0f); // Damage(float, Weapon)
    TIDE_CHECK(world.Log.whole == 110.0f); // Damage(int): its own type over one it converts to
    TIDE_CHECK(world.Log.swings == 75.0f); // Swing(), Swing(float), Swing(int, float = 1) twice
    TIDE_CHECK(world.Log.twice == 6.0f);   // From another of the type's methods
    TIDE_CHECK(world.Log.byte_or_float == 1); // A byte goes to an int rather than a float or a long
    TIDE_CHECK(world.Log.exact == 123);
}

// A block isn't an argument: Repeat's defaults and names are those before it,
// and of two named Each, the call with a block is the one that takes one.
TIDE_TEST(overloads_blocks_and_tasks_take_them_too)
{
    one_tick();
    TIDE_CHECK(world.Log.blocks == 232); // Repeat() twice, Repeat(step: 2, times: 6) three times, Each(2) twice
    TIDE_CHECK(world.Log.counted == 300);
    TIDE_CHECK(world.Log.picked == 13);  // 7 + 1 + 5
    for (int i = 0; i < 3; i++) tide_world_tick(&world);
    TIDE_CHECK(world.Log.later == 43);   // Later(log) and Later(log, add: 40), each a tick later
}

// Two systems named Move: Between runs after the players' and before the
// enemies', which [After] and [Before] name with their parameters.
TIDE_TEST(overloads_systems_share_a_name)
{
    one_tick();
    TIDE_CHECK(world.Log.between == 1.0f); // The player moved by 1; the enemy, which moves by 2, hadn't
    tide_world_tick(&world);
    TIDE_CHECK(world.Log.between == 1.0f); // 1 + (2 - 2)
}

TIDE_TEST(overloads_the_engines_functions_take_names)
{
    one_tick();
    tide_local_init(&local);
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    TIDE_REQUIRE(draw.count == 2);
    const tide_draw_command *circle = &draw.commands[0];
    TIDE_CHECK(circle->kind == TIDE_DRAW_CIRCLE && circle->a.x == 3.0f && circle->a.y == 4.0f && circle->b.x == 2.0f);
    TIDE_CHECK(draw.commands[1].kind == TIDE_DRAW_SHAPE); // Rect with a rotation, picked by its name
}
