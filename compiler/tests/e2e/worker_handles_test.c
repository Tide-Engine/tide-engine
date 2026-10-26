#include <stdlib.h>

#include "game.h"
#include "tide/worker.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_background *pending;
static int entered;

void EnteredWorker(void) { entered++; }

static void defer(void *self, tide_background *job)
{
    (void)self;
    job->next = pending;
    pending = job;
}

static bool help(void *self, const bool helpers_only)
{
    (void)self;
    if (!pending || helpers_only) return false;
    tide_background *job = pending;
    pending = job->next;
    job->work(job->context);
    __atomic_store_n(&job->done, 1u, __ATOMIC_RELEASE);
    return true;
}

static const tide_jobs deferred = {.threads = 1, .background = defer, .help = help};

static void frame(void) { tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui); }

static void init(void)
{
    entered = 0;
    gui.jobs = &deferred;
    tide_world_init(&world, 1.0f);
    tide_local_init(&local);
    frame();
}

static void finish(void)
{
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    TIDE_CHECK(!pending);
}

TIDE_TEST(worker_handle_captures_a_grid_before_a_later_argument_changes_it)
{
    init();
    TIDE_REQUIRE(pending);
    while (help(NULL, false)) { }
    for (int i = 0; i < 4; i++) frame();
    TIDE_CHECK(entered == 1);
    TIDE_CHECK(local.State.one == 5 && local.State.two == 5);
    finish();
}

TIDE_TEST(worker_cancelling_one_waiter_preserves_the_other_consumer)
{
    init();
    local.State.cancelFirst = true;
    local.State.dropBuild = true;
    frame();
    while (help(NULL, false)) { }
    for (int i = 0; i < 4; i++) frame();
    TIDE_CHECK(entered == 1);
    TIDE_CHECK(local.State.one == 0 && local.State.two == 5);
    finish();
}

TIDE_TEST(worker_cancelling_its_handle_ends_both_waiters_and_skips_queued_work)
{
    init();
    local.State.cancelBuild = true;
    frame();
    while (help(NULL, false)) { }
    for (int i = 0; i < 4; i++) frame();
    TIDE_CHECK(entered == 0);
    TIDE_CHECK(local.State.one == 0 && local.State.two == 0);
    finish();
}

TIDE_TEST(worker_abandons_queued_work_after_its_last_consumer_disappears)
{
    init();
    local.State.cancelFirst = true;
    local.State.cancelSecond = true;
    local.State.dropBuild = true;
    frame();
    while (help(NULL, false)) { }
    for (int i = 0; i < 4; i++) frame();
    TIDE_CHECK(entered == 0);
    TIDE_CHECK(local.State.one == 0 && local.State.two == 0);
    finish();
}

TIDE_TEST(worker_handle_restarts_from_its_owned_grid_after_restore)
{
    init();
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    void *bytes = malloc(size);
    TIDE_REQUIRE(bytes);
    TIDE_REQUIRE(tide_local_pack(&local, bytes, size) == size);
    TIDE_REQUIRE(tide_local_unpack(&local, bytes, size));
    free(bytes);
    for (int i = 0; i < 6; i++) {
        frame();
        while (help(NULL, false)) { }
    }
    TIDE_CHECK(entered == 1);
    TIDE_CHECK(local.State.one == 5 && local.State.two == 5);
    finish();
}

TIDE_TEST(worker_handle_reload_keeps_consumers_and_restarts_owned_input)
{
    init();
    const tide_task_handle_value handle = local.State.build;
    TIDE_REQUIRE(pending);
    tide_local_rebind_resources(&local, true);
    TIDE_CHECK(!pending && entered == 0);
    TIDE_CHECK(tide_task_handle_equal(handle, local.State.build));
    for (int i = 0; i < 6; i++) {
        frame();
        while (help(NULL, false)) { }
    }
    TIDE_CHECK(entered == 1);
    TIDE_CHECK(local.State.one == 5 && local.State.two == 5);
    finish();
}
