#include <stdlib.h>

#include "game.h"
#include "tide/worker.h"
#include "tide/time.h"
#ifdef TIDE_WORKER_THREADS
#include "tide/platform.h"
#endif
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_background *pending;
static int32_t visit_width;
static uint32_t visits, visit_bad;

void BeginVisit(const int32_t width)
{
    visit_width = width;
    __atomic_store_n(&visits, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&visit_bad, 0u, __ATOMIC_RELAXED);
}

void OrderedVisit(const tide_int2 at)
{
    const uint32_t i = __atomic_fetch_add(&visits, 1u, __ATOMIC_RELAXED);
    if (at.x != (int32_t)i % visit_width || at.y != (int32_t)i / visit_width)
        __atomic_store_n(&visit_bad, 1u, __ATOMIC_RELAXED);
}

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

static void init(const tide_jobs *jobs)
{
    TIDE_CHECK(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    gui.jobs = jobs;
}

static void frame(void) { tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui); }

static void finish(void)
{
    const uint64_t hash = tide_world_hash(&world);
    const uint64_t until = tide_time_now_ns() + 10000000000ull;
    while (!local.Terrain.done && tide_time_now_ns() < until) {
        while (help(NULL, false)) { }
        frame();
        tide_yield();
    }
    TIDE_CHECK(local.Terrain.done);
    tide_text_use(&world.heap, &local.heap);
    static const int32_t expected[] = {9, 14, 18, 0, 10, 67, 2130, 36, 3004, 0, 52};
    TIDE_CHECK(tide_list_count(local.Terrain.result) == 11);
    for (int i = 0; i < 11; i++) TIDE_CHECK(*(const int32_t *)tide_list_at(local.Terrain.result, i, sizeof(int32_t)) == expected[i]);
    TIDE_CHECK(local.Terrain.pixels == 42);
    TIDE_CHECK(__atomic_load_n(&visits, __ATOMIC_RELAXED) == (uint32_t)local.Terrain.visits);
    TIDE_CHECK(__atomic_load_n(&visit_bad, __ATOMIC_RELAXED) == 0);
    TIDE_CHECK(local.Terrain.ordered == local.Terrain.orderedExpected);
    TIDE_CHECK(local.Terrain.named == local.Terrain.namedExpected);
    TIDE_CHECK(local.Terrain.saved == local.Terrain.savedExpected);
    TIDE_CHECK(tide_world_hash(&world) == hash);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
}

TIDE_TEST(worker_grid_inputs_are_independent_of_later_source_changes)
{
    init(&deferred);
    frame();
    TIDE_CHECK(pending && !local.Terrain.done);
    local.Terrain.mutate = true;
    frame();
    world.MatchGrid.change = true;
    tide_world_tick(&world);
    tide_scratch_free();
    finish();
}

TIDE_TEST(worker_grid_inputs_restart_after_local_state_restoration)
{
    init(&deferred);
    frame();
    TIDE_CHECK(pending && !local.Terrain.done);
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_CHECK(tide_local_pack(&local, bytes, size) == size);
    TIDE_CHECK(tide_local_unpack(&local, bytes, size));
    free(bytes);
    TIDE_CHECK(pending == NULL);
    finish();
}

TIDE_TEST(worker_grid_inputs_work_on_the_fallback_and_platform_pool)
{
    init(NULL);
    const uint64_t hash = tide_world_hash(&world);
    frame();
    TIDE_CHECK(tide_world_hash(&world) == hash);
    finish();
#ifdef TIDE_WORKER_THREADS
    init(tide_platform_jobs());
    frame();
    TIDE_CHECK(tide_world_hash(&world) == hash);
    finish();
#endif
}

TIDE_TEST(worker_grid_asset_capture_survives_pack_replacement)
{
    init(&deferred);
    frame();
    TIDE_CHECK(help(NULL, false));
    frame(); // The pixel worker has captured the old pack's grid.
    TIDE_CHECK(pending && !local.Terrain.done);
    TIDE_CHECK(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_scratch_free();
    finish();
}

TIDE_TEST(worker_grid_argument_survives_an_await_and_local_state_restoration)
{
    init(&deferred);
    for (int i = 0; i < 20 && !local.Terrain.waiting; i++) {
        while (help(NULL, false)) { }
        frame();
    }
    TIDE_CHECK(local.Terrain.waiting && !local.Terrain.done);
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_CHECK(tide_local_pack(&local, bytes, size) == size);
    TIDE_CHECK(tide_local_unpack(&local, bytes, size));
    free(bytes);
    tide_scratch_free();
    finish();
}

TIDE_TEST(worker_grid_snapshots_progress_with_one_slot_and_oversized_transfers)
{
    init(&deferred);
    const tide_worker_limits limits = {.pending = 1, .transfer_bytes = 1};
    gui.worker_limits = &limits;
    finish();
    gui.worker_limits = NULL;
}
