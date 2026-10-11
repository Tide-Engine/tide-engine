#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

// What the tasks of no world of their own were started with, in the order they went on
static int32_t marks[16];
static int marked;

void task_mark(const int32_t n)
{
    if (marked < 16) marks[marked++] = n;
}

static bool was_marked(const int32_t n)
{
    for (int i = 0; i < marked; i++) {
        if (marks[i] == n) return true;
    }
    return false;
}

static int32_t went(const tide_world *w, const int32_t at)
{
    return *(const int32_t *)tide_list_read(&w->heap, w->Tally.order, (uint32_t)at, sizeof(int32_t));
}

static void frame(const tide_world *w, const float seconds)
{
    tide_local_frame_time(&local, seconds);
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(640, 480), NULL);
    tide_frame(w, NULL, 1, w, NULL, 1.0f, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

TIDE_TEST(function_tasks_start_in_the_match_in_the_order_written)
{
    marked = 0;
    tide_world w = {0};
    tide_world_init(&w, 1.0f);
    tide_world_tick(&w);
    TIDE_CHECK(w.Tally.sum == 14);                           // 2 + 3 * 4, each call in its turn
    TIDE_CHECK(w.tide_tasks_function_Count.count == 9);      // Each waits in the match's table
    TIDE_CHECK(w.tide_tasks_function_Later.count == 2);
    TIDE_CHECK(w.Tally.counted == 0);
    tide_world_tick(&w);
    // A function's, two through another's, a sum's left to right, a method's, a block's function's and the block's
    static const int32_t first[] = {1, 10, 100, 2, 3, 4, 5, 7, 8};
    TIDE_REQUIRE(tide_list_count(w.Tally.order) == 9);
    for (int32_t i = 0; i < 9; i++) TIDE_CHECK(went(&w, i) == first[i]);
    TIDE_CHECK(w.Tally.counted == 140);
    TIDE_CHECK(w.Tally.rolled != 0);                         // It rolled with a sequence the function handed on
    TIDE_REQUIRE(marked == 2);                               // The match's seconds: the tasks nothing decides ran there
    TIDE_CHECK(marks[0] == 1 && marks[1] == 41);
    tide_world_tick(&w);
    tide_world_tick(&w);
    TIDE_REQUIRE(tide_list_count(w.Tally.order) == 10);      // A task's own call of the function started one more
    TIDE_CHECK(went(&w, 9) == 1000);
    TIDE_CHECK(w.Tally.counted == 1140);
    TIDE_CHECK(w.tide_tasks_function_Count.count == 0);
    // The same again in another world: a task's rolls are the match's
    tide_world again = {0};
    tide_world_init(&again, 1.0f);
    tide_world_tick(&again);
    tide_world_tick(&again);
    TIDE_CHECK(again.Tally.rolled == w.Tally.rolled);
    tide_world_free(&again);
    tide_world_free(&w);
}

TIDE_TEST(function_tasks_belong_to_the_entity_their_caller_runs_for)
{
    tide_world w = {0};
    tide_world_init(&w, 1.0f);
    tide_world_tick(&w); // Burn lights each fuse: a task a fuse, started by a function
    TIDE_CHECK(w.tide_tasks_function_Blow.count == 2);
    tide_world_tick(&w); // The first fuse is destroyed, and its task goes with it
    tide_world_tick(&w);
    TIDE_CHECK(w.tide_tasks_function_Blow.count == 1);
    TIDE_CHECK(w.Tally.booms == 0);
    // The arena's handler's function started a task, whose crate joined the arena
    TIDE_CHECK(tide_world_entity_count(&w) == 4); // Main, the arena, its crate, the second fuse
    tide_world_tick(&w);
    TIDE_CHECK(w.Tally.booms == 2);               // Only the second fuse's
    TIDE_CHECK(w.tide_tasks_function_Blow.count == 0);
    tide_world_tick(&w);
    tide_world_tick(&w);
    TIDE_CHECK(tide_world_entity_count(&w) == 2); // The arena unloaded, and the crate went with it
    tide_world_free(&w);
}

TIDE_TEST(function_tasks_start_on_this_machine_from_views_and_local_handlers)
{
    marked = 0;
    tide_world w = {0};
    tide_world_init(&w, 1.0f);
    tide_local_init(&local);
    tide_world_tick(&w);
    marked = 0; // The match's own are another test's
    frame(&w, 0.25f);
    // Menu's two, through one function and two; its handler's one, started with no match to hand on
    TIDE_CHECK(local.tide_tasks_function_Flow.count == 3);
    TIDE_CHECK(local.tide_tasks_function_Watch.count == 2); // One a fuse, each its fuse's
    TIDE_CHECK(local.tide_tasks_function_Later.count == 2); // The task nothing decides, in the local world this time
    TIDE_CHECK(w.tide_tasks_function_Later.count == 2);     // ...beside the match's own two
    TIDE_CHECK(local.Desk.heard == 3);
    const tide_str note = tide_text_read(&local.heap, local.Desk.note);
    TIDE_CHECK(note.bytes == 9 && memcmp(note.ptr, "asked ana", 9) == 0);
    TIDE_CHECK(local.Desk.flows == 0);
    // The first fuse goes: the task its view's function started ends with it
    tide_world_tick(&w);
    tide_world_tick(&w);
    marked = 0;
    frame(&w, 0.25f);
    TIDE_CHECK(local.Desk.watched == 2);
    TIDE_CHECK(local.tide_tasks_function_Watch.count == 0);
    frame(&w, 0.25f);
    TIDE_CHECK(local.Desk.flows == 111);
    TIDE_CHECK(marked == 0);
    frame(&w, 0.25f);
    frame(&w, 0.25f);
    TIDE_CHECK(marked == 2 && was_marked(2) && was_marked(3)); // This machine's seconds
    TIDE_CHECK(local.tide_tasks_function_Later.count == 0);
    tide_local_free(&local);
    tide_world_free(&w);
}
