#include <stdlib.h>

#include "game.h"
#include "tide/text.h"
#include "tide/worker.h"
#include "tide/time.h"
#ifdef TIDE_WORKER_THREADS
#include "tide/platform.h"
#endif
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static tide_background *pending;
static void defer(void *self, tide_background *job)
{
    (void)self;
    job->next = pending;
    pending = job;
}

static void complete(void)
{
    while (pending) {
        tide_background *job = pending;
        pending = job->next;
        job->work(job->context);
        __atomic_store_n(&job->done, 1u, __ATOMIC_RELEASE);
    }
}

static bool deferred_help(void *self, const bool helpers_only)
{
    (void)self;
    if (helpers_only || !pending) return false;
    tide_background *job = pending;
    pending = job->next;
    job->work(job->context);
    __atomic_store_n(&job->done, 1u, __ATOMIC_RELEASE);
    return true;
}

static const tide_jobs deferred = {.threads = 1, .background = defer, .help = deferred_help};

static void init(void)
{
    tide_world_init(&world, 1.0f);
    tide_local_init(&local);
    tide_text_use(&world.heap, &local.heap);
    for (int32_t i = 0; i < 10000; i++) *(int32_t *)tide_list_add(&local.Meshing.source.voxels, sizeof i, TIDE_IN_LOCAL) = i;
    tide_text_set(&local.Meshing.source.name, tide_str_from_cstr("chunk"), TIDE_IN_LOCAL);
}

static void check_result(void)
{
    TIDE_CHECK(local.Meshing.finished);
    tide_text_use(&world.heap, &local.heap);
    TIDE_CHECK(tide_list_count(local.Meshing.result.faces) == 10000);
    for (int32_t i = 0; i < 10000; i++) {
        TIDE_CHECK(*(const int32_t *)tide_list_at(local.Meshing.result.faces, i, sizeof(int32_t)) == i * 3 + 1);
    }
    TIDE_CHECK(tide_str_eq(tide_text_view(local.Meshing.result.name), tide_str_from_cstr("chunk built")));
}

TIDE_TEST(local_workers_owned_result)
{
    init();
    gui.jobs = NULL;
    const uint64_t hash = tide_world_hash(&world);
    for (int i = 0; i < 5 && !local.Meshing.finished; i++) {
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    }
    check_result();
    TIDE_CHECK(tide_world_hash(&world) == hash);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
}

TIDE_TEST(local_workers_capture_survives_source_changes)
{
    init();
    gui.jobs = &deferred;
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(!local.Meshing.finished);
    TIDE_CHECK(pending != NULL);
    tide_text_use(&world.heap, &local.heap);
    tide_list_clear(&local.Meshing.source.voxels, sizeof(int32_t));
    tide_text_set(&local.Meshing.source.name, tide_str_from_cstr("changed"), TIDE_IN_LOCAL);
    tide_scratch_free();
    complete();
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    check_result();
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
}

TIDE_TEST(local_workers_restart_after_serialization)
{
    init();
    gui.jobs = &deferred;
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(!local.Meshing.finished && pending);
    // The nested async call keeps its own arguments across restoration.
    tide_text_use(&world.heap, &local.heap);
    tide_list_clear(&local.Meshing.source.voxels, sizeof(int32_t));
    tide_text_set(&local.Meshing.source.name, tide_str_from_cstr("changed"), TIDE_IN_LOCAL);
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_CHECK(tide_local_pack(&local, bytes, size) == size);
    TIDE_CHECK(((const tide_local *)(const void *)bytes)->tide_workers == NULL);
    TIDE_CHECK(((const tide_local *)(const void *)bytes)->tide_worker_jobs == NULL);
    TIDE_CHECK(tide_local_unpack(&local, bytes, size));
    free(bytes);
    TIDE_CHECK(pending == NULL); // The old provider was drained before disposal.
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(pending != NULL);
    complete();
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    check_result();
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
}

TIDE_TEST(local_workers_reload_drains_old_code_and_restarts)
{
    init();
    gui.jobs = &deferred;
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(pending != NULL);
    tide_text_use(&world.heap, &local.heap);
    tide_list_clear(&local.Meshing.source.voxels, sizeof(int32_t));
    tide_text_set(&local.Meshing.source.name, tide_str_from_cstr("changed"), TIDE_IN_LOCAL);
    tide_local_rebind_resources(&local, true);
    TIDE_CHECK(pending == NULL);
    TIDE_CHECK(local.tide_workers == NULL);
    TIDE_CHECK(!local.Meshing.finished);
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(pending != NULL);
    complete();
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    check_result();
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
}

TIDE_TEST(local_workers_end_with_their_owner)
{
    init();
    gui.jobs = &deferred;
    local.Control.spawn = true;
    for (int i = 0; i < 2; i++) tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.tide_tasks_function_FinishOwner.count == 1);
    local.Control.destroy = true;
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.tide_tasks_function_FinishOwner.count == 0);
    complete(); // An abandoned worker has no binding back to the dead entity.
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(tide_local_entity_count(&local) == 0);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
}

#ifdef TIDE_WORKER_THREADS
TIDE_TEST(local_workers_on_platform_pool)
{
    init();
    gui.jobs = tide_platform_jobs();
    const uint64_t until = tide_time_now_ns() + 10000000000ull;
    while (!local.Meshing.finished && tide_time_now_ns() < until) {
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
        tide_yield();
    }
    check_result();
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
}
#endif
