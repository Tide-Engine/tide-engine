#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static bool said(const tide_text t, const char *want)
{
    const tide_str s = tide_text_read(&world.heap, t);
    return (size_t)s.bytes == strlen(want) && memcmp(s.ptr, want, (size_t)s.bytes) == 0;
}

static int32_t int_at(const tide_list l, const int i)
{
    const int32_t *v = tide_list_read(&world.heap, l, i, sizeof(int32_t));
    return v ? *v : -999;
}

// The world after its first tick: every case ran once.
static void start(void)
{
    tide_world_free(&world);
    world = (tide_world){0};
    tide_world_init(&world, 0.1f);
    tide_world_tick(&world);
}

TIDE_TEST(out_var_in_an_if_is_the_enclosing_blocks)
{
    start();
    const Calls *c = &world.Calls;
    TIDE_CHECK(c->found == 1);
    TIDE_CHECK(c->foundX == 20.0f);
    TIDE_CHECK(c->afterX == 20.0f); // Read after the if that declared it
    TIDE_CHECK(c->missedX == 1.0f); // The else's: nothing found, so the default
}

TIDE_TEST(out_arguments_declare_assign_and_discard)
{
    start();
    const Calls *c = &world.Calls;
    TIDE_CHECK(c->whole == 3);
    TIDE_CHECK(c->rest == 0.25f);
    TIDE_CHECK(c->typedWhole == 7);
    TIDE_CHECK(c->existingRest == 0.75f);
    TIDE_CHECK(c->changed == 109); // `out mut var`, changed after its call
    TIDE_CHECK(c->namedWhole == 6);
    TIDE_CHECK(c->namedRest == 0.25f);
    TIDE_CHECK(TIDE_AT(&world, arch0_Body, Body, 0).hits == 4); // A field of a mut component
}

TIDE_TEST(out_parameters_of_methods)
{
    start();
    const Calls *c = &world.Calls;
    TIDE_CHECK(c->enough);
    TIDE_CHECK(c->power == 5);
    TIDE_CHECK(c->spent);
    TIDE_CHECK(c->left == 3);
    TIDE_CHECK(c->stats.power == 3); // The mut method changed its value too
}

TIDE_TEST(out_text_lists_and_structs_with_text)
{
    start();
    const Calls *c = &world.Calls;
    TIDE_CHECK(said(c->label, "n7"));
    TIDE_CHECK(c->labelLength == 3);
    TIDE_CHECK(tide_list_read_count(&world.heap, c->made) == 4);
    TIDE_CHECK(int_at(c->made, 3) == 9);
    TIDE_CHECK(c->squares == 7);
    TIDE_CHECK(c->named.power == 6);
    TIDE_CHECK(said(c->named.name, "p6"));
    TIDE_CHECK(c->namedLength == 14);
}

TIDE_TEST(out_parameters_passed_on_and_through_conditions)
{
    start();
    const Calls *c = &world.Calls;
    TIDE_CHECK(c->lookedThere);
    TIDE_CHECK(c->looked == 2.5f);
    TIDE_CHECK(c->halved);
    TIDE_CHECK(c->half == 5);
    TIDE_CHECK(c->both);
    TIDE_CHECK(c->halfA == 4);
    TIDE_CHECK(c->halfB == 3);
    TIDE_CHECK(c->chained == 265); // 1.5 found at 3, then 2.5 at 8
    TIDE_CHECK(c->either == 9);
}

TIDE_TEST(out_parameters_through_switches_loops_and_blocks)
{
    start();
    const Calls *c = &world.Calls;
    TIDE_CHECK(c->grade == 43);
    TIDE_CHECK(said(c->gradeName, "B"));
    TIDE_CHECK(c->index == 1);
    TIDE_CHECK(c->looped == 3); // 40, 20 and 10 halve to more than 4
    TIDE_CHECK(c->picked == 40);
    TIDE_CHECK(c->pickedText == 4);
    TIDE_CHECK(c->ran == 2);
    TIDE_CHECK(c->blocks == 2);
}

TIDE_TEST(out_after_a_call_that_fails)
{
    start();
    const Fails *f = &world.Fails;
    TIDE_CHECK(f->parsed == 104);
    TIDE_CHECK(f->used == 2);
    TIDE_CHECK(f->emptyValue == 0);
    TIDE_CHECK(f->emptyUsed == 0); // It failed before assigning: the default, not the 99 it held
    TIDE_CHECK(f->longUsed == 6);  // What it assigned before it failed
    TIDE_CHECK(f->tried == 12);
    TIDE_CHECK(f->triedUsed == 3);
    TIDE_CHECK(f->isValue == 6);
    TIDE_CHECK(f->isUsed == 3);
}

TIDE_TEST(out_map_try_get)
{
    start();
    const Maps *m = &world.Maps;
    TIDE_CHECK(m->has);
    TIDE_CHECK(m->there == 1.5f);
    TIDE_CHECK(!m->hasMissing);
    TIDE_CHECK(m->missing == 0.0f); // No such key: the default
    TIDE_CHECK(m->power == 9);
    TIDE_CHECK(said(m->statName, "Sir"));
    TIDE_CHECK(m->missingPower == 3); // A struct's declared default
    TIDE_CHECK(m->named);
    TIDE_CHECK(said(m->name, "one"));
    TIDE_CHECK(said(m->noName, ""));
    TIDE_CHECK(m->total == 4.0f);
    TIDE_CHECK(m->count == 2);
}

TIDE_TEST(out_parameters_of_c_functions)
{
    start();
    const FromC *f = &world.FromC;
    TIDE_CHECK(f->halved);
    TIDE_CHECK(f->half == 42);
    TIDE_CHECK(!f->oddHalved);
    TIDE_CHECK(f->odd == 0);
    TIDE_CHECK(f->pair.a == 6);
    TIDE_CHECK(f->pair.b == 3.5f);
    TIDE_CHECK(f->a == 3.0f);
    TIDE_CHECK(f->b.x == 1.5f && f->b.y == 2.5f && f->b.z == 3.5f);
    TIDE_CHECK(f->count == 5);
    TIDE_CHECK(tide_list_read_count(&world.heap, f->squares) == 5);
    TIDE_CHECK(int_at(f->squares, 4) == 16);
    TIDE_CHECK(f->localCount == 4);
    TIDE_CHECK(f->localLast == 9);
}

TIDE_TEST(out_in_parallel_steps_and_comparisons)
{
    start();
    const Loops *l = &world.Loops;
    TIDE_CHECK(int_at(l->values, 0) == 4);
    TIDE_CHECK(int_at(l->values, 1) == 3); // Odd: left as it was
    TIDE_CHECK(int_at(l->values, 2) == 5);
    TIDE_CHECK(int_at(l->values, 3) == 3);
    // By their halves, the odd ones (-1) first, in the order they were
    TIDE_CHECK(int_at(l->sorted, 0) == 9);
    TIDE_CHECK(int_at(l->sorted, 1) == 7);
    TIDE_CHECK(int_at(l->sorted, 2) == 2);
    TIDE_CHECK(int_at(l->sorted, 3) == 4);
    TIDE_CHECK(l->halves == 2);
}

TIDE_TEST(out_locals_wait_with_their_task)
{
    start();
    TIDE_CHECK(world.Later.whole == -1); // Started, and waiting
    tide_world_tick(&world);
    tide_world_tick(&world);
    TIDE_CHECK(world.Later.waited == 1);
    TIDE_CHECK(world.Later.whole == 5);
    TIDE_CHECK(world.Later.rest == 0.75f);
}

TIDE_TEST(out_in_handlers_and_views)
{
    start();
    TIDE_CHECK(world.Poked.halved);
    TIDE_CHECK(world.Poked.half == 21);
    tide_local_init(&local);
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    TIDE_CHECK(local.Seen.frames == 1);
    TIDE_CHECK(local.Seen.whole == 8);
    TIDE_CHECK(local.Seen.rest == 0.5f);
    TIDE_CHECK(local.Seen.has);
    TIDE_CHECK(local.Seen.score == 2.5f);
}

TIDE_TEST(out_the_same_world_every_run)
{
    start();
    const uint64_t first = tide_world_hash(&world);
    tide_world_tick(&world);
    const uint64_t second = tide_world_hash(&world);
    start();
    TIDE_CHECK(tide_world_hash(&world) == first);
    tide_world_tick(&world);
    TIDE_CHECK(tide_world_hash(&world) == second);
}

TIDE_TEST(out_declared_wherever_a_call_is)
{
    start();
    const Places *p = &world.Places;
    TIDE_CHECK(p->returned == 7);  // return Half(value, out var half) ? half : -1
    TIDE_CHECK(p->listed == 1409); // A foreach's list: 1 + 4 + 9, and the last
    TIDE_CHECK(p->switched == 6);
    TIDE_CHECK(p->stepped == 4);   // A for's step halves its own variable: 16, 8, 4, 2
    TIDE_CHECK(p->indexed == 42);  // The index's call, then the value that reads what it made
    TIDE_CHECK(p->inlined == 2);   // In a function copied into its call: 2 and 4 halve to more than 0
}
