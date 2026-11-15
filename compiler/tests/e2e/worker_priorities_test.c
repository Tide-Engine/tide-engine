#include <string.h>

#include "game.h"
#include "tide/worker.h"
#include "tide_test.h"

// One thread that takes a calculation when the test says (help), in the order it was handed them: the order
// calculations start in is the group's alone to say.
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_background *first, *last;
static int ran[128], runs;

void Ran(const int32_t id)
{
    if (runs < 128) ran[runs] = id;
    runs++;
}

static void defer(void *self, tide_background *job)
{
    (void)self;
    job->next = NULL;
    if (last) last->next = job;
    else first = job;
    last = job;
}

static bool help(void *self, const bool helpers_only)
{
    (void)self;
    if (!first || helpers_only) return false;
    tide_background *job = first;
    first = job->next;
    if (!first) last = NULL;
    job->work(job->context);
    __atomic_store_n(&job->done, 1u, __ATOMIC_RELEASE);
    return true;
}

static const tide_jobs one_thread = {.threads = 1, .background = defer, .help = help};

static void frame(void) { tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui); }

static void init(const tide_worker_limits *limits)
{
    runs = 0;
    memset(ran, 0, sizeof ran);
    gui.jobs = &one_thread;
    gui.worker_limits = limits;
    tide_world_init(&world, 1.0f);
    tide_local_init(&local);
    frame();
}

static void finish(void)
{
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    gui.worker_limits = NULL;
    TIDE_CHECK(!first);
}

static void settle(void)
{
    for (int i = 0; i < 12; i++) {
        while (help(NULL, false)) { }
        frame();
    }
}

static bool order_is(const int *want, const int count)
{
    if (runs != count) return false;
    for (int i = 0; i < count; i++) if (ran[i] != want[i]) return false;
    return true;
}

TIDE_TEST(worker_priorities_start_urgent_then_normal_then_background_each_in_the_order_it_came)
{
    init(NULL);
    local.State.phase = 1;
    frame();
    TIDE_CHECK(runs == 0); // Nothing ran on the frame's thread
    while (help(NULL, false)) { }
    static const int want[] = {1, 4, 7, 3, 6, 2, 5};
    TIDE_CHECK(order_is(want, 7));
    settle();
    TIDE_CHECK(local.State.done == 7 && local.State.sum == 28);
    finish();
}

TIDE_TEST(worker_priorities_give_waiting_normal_and_background_calls_their_turn_under_urgent_ones)
{
    init(NULL);
    local.State.phase = 2;
    frame();
    while (help(NULL, false)) { }
    TIDE_REQUIRE(runs == 44);
    // A normal call after four urgent starts, the background one after sixteen starts of the others
    static const int want[] = {100, 200, 201, 202, 203, 102, 204, 205, 206, 207, 103, 208, 209, 210, 211, 212, 213, 101, 214};
    for (int i = 0; i < 19; i++) TIDE_CHECK(ran[i] == want[i]);
    for (int i = 19; i < 44; i++) TIDE_CHECK(ran[i] == 215 + (i - 19));
    settle();
    TIDE_CHECK(local.State.done == 44);
    finish();
}

TIDE_TEST(worker_priorities_are_a_handle_giving_calls_too)
{
    init(NULL);
    local.State.phase = 3;
    frame();
    while (help(NULL, false)) { }
    static const int want[] = {1, 3, 4, 2};
    TIDE_CHECK(order_is(want, 4));
    settle();
    TIDE_CHECK(local.State.done == 4 && local.State.sum == 10);
    finish();
}

TIDE_TEST(worker_priorities_leave_a_cancelled_queued_call_out)
{
    init(NULL);
    local.State.phase = 4;
    local.State.cancelKept = true;
    frame();
    while (help(NULL, false)) { }
    static const int want[] = {1, 3, 4};
    TIDE_CHECK(order_is(want, 3));
    settle();
    TIDE_CHECK(local.State.done == 3 && local.State.sum == 8);
    finish();
}

TIDE_TEST(worker_priorities_take_any_value_of_the_enum)
{
    init(NULL);
    local.State.phase = 5;
    frame();
    while (help(NULL, false)) { }
    static const int want[] = {1, 4, 10, 3};
    TIDE_CHECK(order_is(want, 4));
    settle();
    TIDE_CHECK(local.State.done == 4 && local.State.sum == 18);
    finish();
}

TIDE_TEST(worker_priorities_decide_which_calls_find_room)
{
    const tide_worker_limits limits = {.pending = 4};
    init(&limits);
    local.State.phase = 6;
    frame();
    while (help(NULL, false)) { }
    // What found room at once: the first, the urgent ones, the normal one that came last, and two of the
    // five background calls
    static const int want[] = {1, 20, 21, 22, 30, 10, 11};
    TIDE_CHECK(order_is(want, 7));
    for (int i = 0; i < 8; i++) settle();
    TIDE_CHECK(runs == 10 && local.State.done == 10);
    TIDE_CHECK(local.State.sum == 1 + 10 + 11 + 12 + 13 + 14 + 20 + 21 + 22 + 30);
    finish();
}
