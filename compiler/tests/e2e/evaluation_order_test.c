#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

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

// The cases below hold on every platform and at every optimization level:
// each number is what left to right gives, and C, left to pick an order, gives
// others on some of them (clang goes right to left on Windows).

static void one_tick(void)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
}

TIDE_TEST(evaluation_order_calls_that_write_run_left_to_right)
{
    one_tick();
    const Calls c = world.Calls;
    TIDE_CHECK(c.arguments == 12);
    TIDE_CHECK(c.operators == -5); // 1 - 2 * 3
    TIDE_CHECK(c.vector.x == 1 && c.vector.y == 2 && c.vector.z == 3);
    TIDE_CHECK(c.fields.b == 1 && c.fields.a == 2);
    TIDE_CHECK(c.nested == 154); // Pack(12, 34)
    TIDE_CHECK(c.helper == 154);
    TIDE_CHECK(c.mixed == 24); // 12 + 3 * 4
    TIDE_CHECK(c.list == 123);
    TIDE_CHECK(c.operatorCalls == 12);
    TIDE_CHECK(c.methods == 12);
    TIDE_CHECK(c.external == 12);
}

TIDE_TEST(evaluation_order_reads_keep_their_turn_beside_writes)
{
    one_tick();
    const Reads r = world.Reads;
    TIDE_CHECK(r.before == 1);    // Pack(0, 1)
    TIDE_CHECK(r.after == 11);    // Pack(1, 1)
    TIDE_CHECK(r.between == 112); // Pack3(1, 1, 2)
    TIDE_CHECK(r.operandBefore == 1);
    TIDE_CHECK(r.operandAfter == 11);
    TIDE_CHECK(r.whole == 1);
    TIDE_CHECK(r.returned == 11); // Pack3(0, 1, 1)
    TIDE_CHECK(r.compound == 11); // 5 + 6
    TIDE_CHECK(r.localBefore == 12);
    TIDE_CHECK(r.localAfter == 22);
    TIDE_CHECK(r.localCompound == 3); // 1 + 2
    TIDE_CHECK(r.operatorRead == 1);
    TIDE_CHECK(r.external == 1);
}

TIDE_TEST(evaluation_order_methods_read_their_value_as_they_run)
{
    one_tick();
    const Reads r = world.Reads;
    TIDE_CHECK(r.receiver == 11);   // Bump, then Plus on what it left
    TIDE_CHECK(r.mutReceiver == 2); // Bump, then Add(1)
    TIDE_CHECK(r.ownMethods == 11);
    TIDE_CHECK(r.methodBefore == 1); // Plus(0) on 0, then Bump
}

TIDE_TEST(evaluation_order_failing_calls_keep_their_turn)
{
    one_tick();
    const Reads r = world.Reads;
    TIDE_CHECK(r.orElse == 7);      // Pack(0, 7): the first Even fails
    TIDE_CHECK(r.orValue == 12);    // Pack(1, 2)
    TIDE_CHECK(r.defaulted == 122); // Pack3(1, 2, 2)
}

TIDE_TEST(evaluation_order_parts_run_only_when_they_would)
{
    one_tick();
    const Parts p = world.Parts;
    TIDE_CHECK(!p.andStops && p.andStopsTrace == 1);
    TIDE_CHECK(p.andRuns && p.andRunsTrace == 12);
    TIDE_CHECK(p.orStops && p.orStopsTrace == 1);
    TIDE_CHECK(p.pick == 12);
    TIDE_CHECK(p.pickOne == 1 && p.pickOneCount == 1);
    TIDE_CHECK(p.sides == 43); // Pack(2 + 2, 3)
    TIDE_CHECK(p.converted == 22); // Two calls, and half of the second's 2
    TIDE_CHECK(p.beside == 1 && p.besideTrace == 11);
    TIDE_CHECK(p.inside);
    TIDE_CHECK(p.afterAnd);
}

TIDE_TEST(evaluation_order_conditions_read_before_their_calls)
{
    one_tick();
    const Parts p = world.Parts;
    TIDE_CHECK(p.rounds == 3 && p.roundsCount == 4); // Pack(0, 1), Pack(1, 2), Pack(2, 3), and Pack(3, 4) ends it
    TIDE_CHECK(p.branch == 1);
    TIDE_CHECK(p.section == 1);
}

TIDE_TEST(evaluation_order_lists_maps_and_grids)
{
    one_tick();
    const Bags b = world.Bags;
    TIDE_CHECK(b.listRead == 23);
    TIDE_CHECK(b.listCopy == 9);    // (1 + 2) * 3: the list as it was
    TIDE_CHECK(b.listElement == 7); // The element Push added
    TIDE_CHECK(b.listRemove == 12);
    TIDE_CHECK(b.indexed == 15 && b.indexedCount == 2); // 0, 1, 5: each index ran once
    TIDE_CHECK(b.listCompound == 4);                    // 1 + 3
    TIDE_CHECK(b.ownList == 15);                        // The 1 it was given, then the 5 it wrote
    TIDE_CHECK(b.setAdd == 10);
    TIDE_CHECK(b.setCount == 11);
    TIDE_CHECK(b.mapRead == 59);
    TIDE_CHECK(b.mapRemove == 10);
    TIDE_CHECK(b.gridBefore == 1);
    TIDE_CHECK(b.gridAfter == 22);
}

TIDE_TEST(evaluation_order_text_reads_before_it_changes)
{
    one_tick();
    const Bags b = world.Bags;
    TIDE_CHECK(b.interpolated);
    TIDE_CHECK(b.joined);
    TIDE_CHECK(b.textRead);
}

TIDE_TEST(evaluation_order_a_call_read_twice_runs_once)
{
    one_tick();
    const Bags b = world.Bags;
    TIDE_CHECK(b.once == 12);
    TIDE_CHECK(b.swizzled == 12);
    TIDE_CHECK(b.externalOnce == 12);
}

TIDE_TEST(evaluation_order_components_and_what_spawns_are_given)
{
    one_tick();
    const Walker w = tide_get_Walker(&world, slot(8));
    TIDE_REQUIRE(tide_has_Walker(&world, slot(8)));
    TIDE_CHECK(w.seen == 34 && w.steps == 4);

    const tide_entity marked = world.Marks.marked;
    TIDE_REQUIRE(tide_has_Mark(&world, marked) && tide_has_Extra(&world, marked));
    const Mark m = tide_get_Mark(&world, marked);
    TIDE_CHECK(m.first == 1 && m.second == 1 && m.third == 2);
    const Extra e = tide_get_Extra(&world, marked);
    TIDE_CHECK(e.first == 0 && e.second == 1);
    TIDE_CHECK(world.Marks.heardFirst == 0 && world.Marks.heardSecond == 1);
}

TIDE_TEST(evaluation_order_a_task_gets_its_arguments_in_order)
{
    one_tick();
    TIDE_CHECK(world.Marks.started == 0);
    tide_world_tick(&world);
    tide_world_tick(&world);
    TIDE_CHECK(world.Marks.started == 1); // Pack(0, 1)
}

TIDE_TEST(evaluation_order_local_fields_in_a_view)
{
    tide_world_init(&world, 1.0f);
    tide_local_init(&local);
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    TIDE_REQUIRE(tide_has_Box_local(&local, slot(9)));
    TIDE_CHECK(tide_get_Box_local(&local, slot(9)).seen == 1); // Pack(0, 1)
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    TIDE_CHECK(tide_get_Box_local(&local, slot(9)).seen == 12);
    TIDE_CHECK(tide_get_Box_local(&local, slot(9)).hits == 2);
}

// A Random's methods step it, so they're calls that write: the same numbers,
// in the same places, as rolling one after another, and the state read beside
// a roll is the one before it or after it, as it's written.
TIDE_TEST(evaluation_order_rolls_keep_their_turn)
{
    one_tick();
    tide_random r = tide_random_seeded(1);
    const float x = tide_random_next_float(&r);
    const float y = tide_random_next_float(&r);
    const int32_t tens = tide_random_next_int_below(&r, 10);
    const int32_t ones = tide_random_next_int_below(&r, 10);
    const int64_t before = r.state;
    tide_random_next_int_below(&r, 10);
    tide_random_next_int_below(&r, 10);
    const int64_t after = r.state;
    const Rolls rolls = world.Rolls;
    TIDE_CHECK(rolls.pair.x == x && rolls.pair.y == y);
    TIDE_CHECK(rolls.digits == tens * 10 + ones);
    TIDE_CHECK(rolls.before == before); // State(random.state, random.NextInt(10)): read, then rolled
    TIDE_CHECK(rolls.after == after);   // Rolled(random.NextInt(10), random.state): rolled, then read
    TIDE_CHECK(rolls.random.state == after);
}

// A call that makes a variable through `out` writes it: what's read beside
// the call is read in its turn, before it or after it, as it's written.
TIDE_TEST(evaluation_order_out_arguments_keep_their_turn)
{
    one_tick();
    const Outs outs = world.Outs;
    TIDE_CHECK(outs.after == 11);    // Count(t, out var first) * 10 + first: 1, then its square
    TIDE_CHECK(outs.before == 724);  // seen * 100 + Count(t, out seen) * 10 + seen: 7, then 2, then 4
    TIDE_CHECK(outs.pair == 365);    // Pack3(Count(out var a), Count(out var b), a + b): 3, 4, and 9 + 16
    TIDE_CHECK(outs.same == 5636);   // Pack(Count(out seen), Count(out seen)) * 100 + seen: 5, 6, and 36
    TIDE_CHECK(outs.passed == 3719); // Pack3(seen, Count(out seen), seen): 36, then 7, then 49
    TIDE_CHECK(outs.looked == 1140); // score * 1000 + (TryGet(4, out score) ? 100 : 0) + score: 1, found, 40
}
