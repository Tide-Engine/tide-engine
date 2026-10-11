#include <string.h>

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

enum { A, B, C, PROBE, HEALER, NOBODY, ROSTER, MEDIC, COPY, FIRST, SECOND, CALLER };

static bool said(const tide_text t, const char *want)
{
    const tide_str s = tide_text_read(&world.heap, t);
    return (size_t)s.bytes == strlen(want) && memcmp(s.ptr, want, (size_t)s.bytes) == 0;
}

// The world after its first tick: every system ran once.
static void start(void)
{
    tide_world_free(&world);
    world = (tide_world){0};
    tide_world_init(&world, 0.1f);
    tide_world_tick(&world);
}

TIDE_TEST(world_functions_read_through_their_callers_world)
{
    start();
    const Healer healer = tide_get_Healer(&world, slot(HEALER));
    TIDE_CHECK(healer.has);
    TIDE_CHECK(healer.found == 7);
    TIDE_CHECK(healer.seen == 7); // Through a function that passes it on
    const Healer nobody = tide_get_Healer(&world, slot(NOBODY));
    TIDE_CHECK(!nobody.has);
    TIDE_CHECK(nobody.found == 10); // None to find: the default
    TIDE_CHECK(nobody.seen == -1);
    TIDE_CHECK(world.Stats.total == 6);
    TIDE_CHECK(world.Stats.picked == 1);  // A method's World
    TIDE_CHECK(world.Stats.inlined == 1); // A function copied into its call
}

TIDE_TEST(world_functions_set_through_a_mut_world)
{
    start();
    const Health b = tide_get_Health(&world, slot(B));
    TIDE_CHECK(b.value == 22);
    TIDE_CHECK(said(b.name, "healed to 22"));
    TIDE_CHECK(tide_get_Health(&world, slot(A)).value == 7);
}

TIDE_TEST(world_functions_compare_through_it)
{
    start();
    const Roster roster = tide_get_Roster(&world, slot(ROSTER));
    TIDE_REQUIRE(tide_list_read_count(&world.heap, roster.all) == 3);
    const tide_entity *lowest = tide_list_read(&world.heap, roster.all, 0, sizeof(tide_entity));
    const tide_entity *middle = tide_list_read(&world.heap, roster.all, 1, sizeof(tide_entity));
    const tide_entity *highest = tide_list_read(&world.heap, roster.all, 2, sizeof(tide_entity));
    TIDE_CHECK(lowest->index == slot(A).index && middle->index == slot(C).index && highest->index == slot(B).index);
    TIDE_CHECK(roster.lowest == 7);
    TIDE_CHECK(roster.highest == 22);
}

// A system that changes a component and reads it through a function runs as
// on one thread: the second entity sees the first's as the system left it.
TIDE_TEST(world_functions_count_as_the_systems_own_reads)
{
    start();
    const Score first = tide_get_Score(&world, slot(FIRST));
    const Score second = tide_get_Score(&world, slot(SECOND));
    TIDE_CHECK(first.value == 6 && first.seen == 0);
    TIDE_CHECK(second.value == 9 && second.seen == 6);
}

TIDE_TEST(world_functions_in_parallel_steps_and_in_their_turn)
{
    start();
    for (int i = 0; i < 4; i++) {
        const int32_t *slot_value = tide_list_read(&world.heap, world.Board.slots, i, sizeof(int32_t));
        TIDE_CHECK(slot_value && *slot_value == 7 + i);
    }
    TIDE_CHECK(world.Stats.order == 100101); // Peek(probe) * 1000 + Bump(probe): read, then set
    TIDE_CHECK(world.Stats.after == 102102); // Bump(probe) * 1000 + Peek(probe): set, then read
    TIDE_CHECK(tide_get_Health(&world, slot(PROBE)).value == 102);
}

TIDE_TEST(world_try_get)
{
    start();
    TIDE_CHECK(world.Stats.tried == 7);
    TIDE_CHECK(world.Stats.missing == 10); // No such entity: the component's default
    const Health copy = tide_get_Health(&world, slot(COPY)); // Into the system's own component, text and all
    TIDE_CHECK(copy.value == 22);
    TIDE_CHECK(said(copy.name, "healed to 22"));
}

TIDE_TEST(world_functions_by_name_overloaded_and_with_defaults)
{
    start();
    const Caller caller = tide_get_Caller(&world, slot(CALLER)); // Of c, which is at 12 once its Hit landed
    TIDE_CHECK(caller.named == 12); // Its arguments by name, the World among them
    TIDE_CHECK(caller.over == 1202); // The one that takes the World, and the one that doesn't
    TIDE_CHECK(caller.least == 1250); // A default after the World, left out and given
    TIDE_CHECK(caller.deep == 15); // Passed on to itself, three deep
}

TIDE_TEST(world_functions_in_handlers_tasks_and_views)
{
    start();
    TIDE_CHECK(world.Stats.hitSeen == 13);
    TIDE_CHECK(tide_get_Health(&world, slot(C)).value == 12);
    tide_world_tick(&world);
    tide_world_tick(&world);
    TIDE_CHECK(world.Stats.later == 1212); // Before its wait and after it
    tide_local_free(&local);
    local = (tide_local){0};
    tide_local_init(&local);
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Seen.value == 6); // A view reads the match through a function: 7, and -1 for nobody
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Seen.frames == 2);
    TIDE_CHECK(local.Seen.life == 2); // ...and sets this machine's own through a mut World
}

TIDE_TEST(world_functions_the_same_world_every_run)
{
    start();
    tide_world_tick(&world);
    const uint64_t first = tide_world_hash(&world);
    start();
    tide_world_tick(&world);
    TIDE_CHECK(tide_world_hash(&world) == first);
}
