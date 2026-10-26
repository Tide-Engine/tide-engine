#include "game.h"
#include "tide_test.h"
#include <stdlib.h>
#ifdef TIDE_TASK_MIGRATION
#include "tide/migrate.h"
extern const tide_layout tide_game_layout;
#endif

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void frame(void)
{
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
}

static void init(void)
{
    tide_world_init(&world, 1.0f);
    tide_local_init(&local);
}

static void finish(void)
{
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
}

TIDE_TEST(task_match_handles_preserve_results_and_cancellation_in_snapshots)
{
    init();
    tide_world_tick(&world);
    static tide_world snapshot;
    tide_world_copy(&snapshot, &world);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    void *bytes = malloc(size);
    TIDE_REQUIRE(bytes);
    TIDE_REQUIRE(tide_world_pack(&world, bytes, size) == size);
    static tide_world packed;
    TIDE_REQUIRE(tide_world_unpack(&packed, bytes, size));
    free(bytes);
    for (int i = 0; i < 8; i++) {
        tide_world_tick(&world);
        tide_world_tick(&snapshot);
        tide_world_tick(&packed);
    }
    TIDE_CHECK(world.MatchResults.answer == 11 && !world.MatchResults.failed);
    TIDE_CHECK(world.MatchResults.polymorphic == 13);
    TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&snapshot));
    TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&packed));
    tide_world_free(&snapshot);
    tide_world_free(&packed);
    finish();
}

TIDE_TEST(task_local_handles_and_cached_results_survive_packing)
{
    init();
    for (int stage = 0; stage < 8; stage++) {
        frame();
        const uint32_t size = tide_local_pack(&local, NULL, 0);
        void *bytes = malloc(size);
        TIDE_REQUIRE(bytes);
        TIDE_REQUIRE(tide_local_pack(&local, bytes, size) == size);
        TIDE_REQUIRE(tide_local_unpack(&local, bytes, size));
        free(bytes);
    }
    TIDE_CHECK(local.Results.valuesRight);
    TIDE_CHECK(local.Results.first == 42 && local.Results.second == 42);
    TIDE_CHECK(local.Results.workerValue == 34);
    finish();
}

TIDE_TEST(task_handles_share_one_operation_and_repeated_results)
{
    init();
    for (int i = 0; i < 8; i++) frame();
    TIDE_CHECK(local.Results.first == 42 && local.Results.second == 42);
    TIDE_CHECK(local.Results.calls == 1);
    TIDE_CHECK(local.Results.workerValue == 34);
    TIDE_CHECK(local.Results.valuesRight);
    TIDE_CHECK(local.Results.order == 12);
    TIDE_CHECK(local.Results.inferred == 13 && local.Results.polymorphic == 26);
    TIDE_CHECK(local.Results.engineWaitsRight);
    TIDE_CHECK(local.Results.afterCancel == 0 && local.Results.independent == 1);
    TIDE_CHECK(local.Results.attached == 18);
    finish();
}

TIDE_TEST(task_completed_results_are_reclaimed_when_handles_leave_state)
{
    init();
    uint32_t baseline = 0;
    for (int burst = 0; burst < 128; burst++) {
        local.Results.started = false;
        for (int i = 0; i < 8; i++) frame();
        TIDE_CHECK(local.Results.valuesRight);
        local.Results.build = (tide_task_handle0){0};
        local.Results.worker = (tide_task_handle0){0};
        local.Results.firstWaiter = (tide_task_handle1){0};
        local.Results.secondWaiter = (tide_task_handle1){0};
        frame();
        TIDE_CHECK(local.tide_tasks_function_tide_completed_function_Calculate.count == 0);
        TIDE_CHECK(local.tide_tasks_function_tide_completed_function_Read.count == 0);
        TIDE_CHECK(local.tide_tasks_function_tide_completed_function_Numbers.count == 0);
        TIDE_CHECK(tide_list_count(local.tide_local_task_slots.slots) <= 32);
        if (burst == 1) baseline = local.heap.used;
        if (burst > 1) TIDE_CHECK(local.heap.used == baseline);
    }
    finish();
}

TIDE_TEST(task_cancelling_one_waiter_preserves_the_other)
{
    init();
    frame();
    local.Results.cancelFirst = true;
    for (int i = 0; i < 8; i++) frame();
    TIDE_CHECK(local.Results.first == 0 && local.Results.second == 42);
    TIDE_CHECK(local.Results.calls == 1);
    finish();
}

TIDE_TEST(task_stale_handles_do_not_alias_reused_storage)
{
    init();
    for (int i = 0; i < 8; i++) frame();
    const tide_task_handle_value stale = local.Results.build;
    local.Results.build = (tide_task_handle_value){0};
    local.Results.firstWaiter = (tide_task_handle_value){0};
    local.Results.secondWaiter = (tide_task_handle_value){0};
    frame();
    local.Results.started = false;
    local.Results.first = local.Results.second = 0;
    frame();
    const tide_task_handle_value fresh = local.Results.build;
    TIDE_CHECK(!tide_task_handle_equal(stale, fresh));
    local.Results.build = stale;
    local.Results.cancelBuild = true;
    local.Results.readStored = true;
    for (int i = 0; i < 8; i++) frame();
    TIDE_CHECK(local.Results.first == 42 && local.Results.second == 42);
    TIDE_CHECK(local.Results.stored == 0);
    finish();
}

TIDE_TEST(task_unreachable_result_cycles_are_reclaimed)
{
    init();
    for (int i = 0; i < 8; i++) frame();
    TIDE_CHECK(local.tide_tasks_function_tide_completed_function_MakeLink.count == 2);
    local.Links.clear = true;
    frame();
    TIDE_CHECK(local.tide_tasks_function_tide_completed_function_MakeLink.count == 0);
    finish();
}

#ifdef TIDE_TASK_MIGRATION
static void migrate_local(const bool drop_tasks)
{
    const tide_layout *layout = &tide_game_layout;
    tide_layout changed = *layout;
    tide_layout_tasks *tasks = malloc(layout->local.task_count * sizeof *tasks);
    TIDE_REQUIRE(tasks);
    for (uint32_t i = 0; i < layout->local.task_count; i++) {
        tasks[i] = layout->local.tasks[i];
        if (drop_tasks) tasks[i].name = "incompatible task code or result schema";
    }
    changed.local.tasks = tasks;
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    void *bytes = malloc(size), *carried = NULL;
    uint32_t carried_size = 0;
    tide_migration migration = {0};
    TIDE_REQUIRE(bytes);
    TIDE_REQUIRE(tide_local_pack(&local, bytes, size) == size);
    TIDE_REQUIRE(tide_migrate_world(&changed, &changed.local, bytes, size,
                                   layout, &layout->local, &carried, &carried_size, &migration));
    TIDE_REQUIRE(tide_local_unpack(&local, carried, carried_size));
    TIDE_CHECK(drop_tasks ? migration.tasks_dropped > 0 : migration.tasks_dropped == 0);
    free(carried); free(bytes); free(tasks);
}

TIDE_TEST(task_reload_preserves_running_and_completed_handles)
{
    init();
    frame();
    const tide_task_handle_value running = local.Results.build;
    migrate_local(false);
    TIDE_CHECK(tide_task_handle_equal(running, local.Results.build));
    for (int i = 0; i < 8; i++) frame();
    migrate_local(false);
    local.Results.readStored = true;
    frame();
    TIDE_CHECK(local.Results.first == 42 && local.Results.second == 42);
    TIDE_CHECK(local.Results.stored == 42);
    finish();
}

TIDE_TEST(task_reload_dropped_operations_leave_harmless_stale_handles)
{
    init();
    for (int i = 0; i < 8; i++) frame();
    const tide_task_handle_value completed = local.Results.build;
    migrate_local(true);
    TIDE_CHECK(tide_task_handle_equal(completed, local.Results.build));
    local.Results.readStored = true;
    local.Results.cancelBuild = true;
    frame();
    TIDE_CHECK(local.Results.stored == 0);
    finish();
}
#endif

TIDE_TEST(task_cancelling_the_operation_ends_all_waiters)
{
    init();
    frame();
    local.Results.cancelBuild = true;
    for (int i = 0; i < 8; i++) frame();
    TIDE_CHECK(local.Results.first == 0 && local.Results.second == 0);
    TIDE_CHECK(local.Results.calls == 1);
    finish();
}
