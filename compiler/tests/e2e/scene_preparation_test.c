#include "game.h"
#include "tide_test.h"
#include "tide/preparation.h"
#include "tide/worker.h"
#include <stdlib.h>
#ifdef TIDE_WORKER_THREADS
#include "tide/platform.h"
#include "tide/time.h"
#endif

typedef struct deferred_queue { tide_background *head; } deferred_queue;

static void defer(void *self, tide_background *job)
{
    deferred_queue *queue = self;
    job->next = queue->head;
    queue->head = job;
}

static bool complete_one(void *self, bool helpers_only)
{
    deferred_queue *queue = self;
    if (helpers_only || !queue->head) return false;
    tide_background *job = queue->head;
    queue->head = job->next;
    job->work(job->context);
    __atomic_store_n(&job->done, 1u, __ATOMIC_RELEASE);
    return true;
}

TIDE_TEST(scene_preparation_runs_before_spawned)
{
    tide_world world = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    TIDE_CHECK(world.Generation.value == 42);
    TIDE_CHECK(world.Generation.started == 1);
    TIDE_CHECK(tide_world_entity_count(&world) == 2);
    tide_world_tick(&world);
    TIDE_CHECK(world.Generation.started == 1);
    tide_world_free(&world);
}

TIDE_TEST(scene_preparation_loads_run_in_recorded_order)
{
    tide_world world = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    world.Generation.travel = world.Generation.multi = true;
    for (int tick = 0; tick < 12; tick++) tide_world_tick(&world);
    TIDE_CHECK(world.Generation.started == 4 && world.Generation.order == 571329);
    TIDE_CHECK(!tide_world_prepare_pending(&world));
    tide_world_free(&world);
}

TIDE_TEST(scene_preparation_background_work_owns_its_world)
{
    tide_world world = {0};
    tide_game_api.start(&world, 1.0f / 60.0f, NULL, 0);
    TIDE_REQUIRE(tide_world_prepare_pending(&world));
    TIDE_CHECK(world.Generation.started == 0);
    deferred_queue queue = {0};
    const tide_jobs jobs = {.self = &queue, .threads = 1, .background = defer, .help = complete_one};
    tide_preparation *state = NULL;
    tide_event_log events = {0};
    TIDE_CHECK(!tide_preparation_update(&state, &tide_game_api, &world, &jobs, &events));
    TIDE_REQUIRE(queue.head != NULL);
    TIDE_CHECK(world.Generation.started == 0 && world.Time.tick == 0);
    TIDE_REQUIRE(complete_one(&queue, false));
    TIDE_CHECK(world.Generation.started == 0);
    TIDE_REQUIRE(tide_preparation_update(&state, &tide_game_api, &world, &jobs, &events));
    TIDE_CHECK(state == NULL && world.Generation.started == 1 && world.Time.tick == 0);
    TIDE_CHECK(tide_world_scene_prepared(&world, tide_entity_in_slot(&world.entities, 0), (tide_player_id){0}));
    TIDE_CHECK(events.count == 2);
    tide_local local = {0};
    tide_local_init(&local);
    tide_local_events(&local, &events);
    tide_frame(&world, NULL, 1.0f, &world, NULL, 1.0f, &local, NULL, NULL);
    TIDE_CHECK(local.Heard.roots == 1 && local.Heard.sites == 1 && local.Heard.verifiedRoots == 1);
    tide_local_free(&local);
    tide_event_log_free(&events);
    tide_world_free(&world);
}

TIDE_TEST(scene_preparation_readiness_survives_values_snapshots_and_packing)
{
    tide_world world = {0}, restored = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    const tide_entity scene = tide_entity_in_slot(&world.entities, 0);
    const tide_player_id player = tide_player_from_index(64);
    uint8_t bytes[TIDE_COMMAND_BYTES];
    const uint32_t size = tide_game_api.prepared_command(scene, bytes, sizeof bytes);
    TIDE_REQUIRE(size && tide_game_api.command(&world, player, 0, 1, bytes, size));
    TIDE_CHECK(tide_world_player_scene_prepared(&world, scene, player));
    TIDE_CHECK(!tide_world_player_scene_prepared(&world, scene, tide_player_from_index(0)));
    TIDE_CHECK(!tide_world_player_scene_prepared(&world, scene, (tide_player_id){0}));
    TIDE_REQUIRE(tide_set_Main(&world, scene, (Main){.seed = 99}));
    TIDE_CHECK(!tide_world_prepare_pending(&world));
    TIDE_CHECK(tide_world_player_scene_prepared(&world, scene, player));
    const uint32_t packed_size = tide_world_pack(&world, NULL, 0);
    uint8_t *packed = malloc(packed_size);
    TIDE_REQUIRE(packed != NULL);
    TIDE_REQUIRE(tide_world_pack(&world, packed, packed_size) == packed_size);
    TIDE_REQUIRE(tide_world_unpack(&restored, packed, packed_size));
    free(packed);
    TIDE_CHECK(tide_world_hash(&restored) == tide_world_hash(&world));
    TIDE_CHECK(tide_world_player_scene_prepared(&restored, scene, player));
    tide_world_player_left(&restored, player);
    TIDE_CHECK(!tide_world_player_scene_prepared(&restored, scene, player));
    tide_world_free(&restored);
    tide_world_free(&world);
}

TIDE_TEST(scene_preparation_abandons_queued_work_on_leave)
{
    tide_world world = {0};
    tide_game_api.start(&world, 1.0f / 60.0f, NULL, 0);
    deferred_queue queue = {0};
    const tide_jobs jobs = {.self = &queue, .threads = 1, .background = defer, .help = complete_one};
    tide_preparation *state = NULL;
    TIDE_CHECK(!tide_preparation_update(&state, &tide_game_api, &world, &jobs, NULL));
    tide_preparation_free(state);
    TIDE_CHECK(queue.head == NULL && world.Generation.started == 0);
    tide_world_free(&world);
}

static void transition_start(void *world, float dt, const void *start, uint32_t seed)
{
    tide_game_api.start(world, dt, start, seed);
    ((tide_world *)world)->Generation.travel = true;
}

#ifdef TIDE_WORKER_THREADS
static void cancellable_start(void *world, float dt, const void *start, uint32_t seed)
{
    tide_game_api.start(world, dt, start, seed);
    ((tide_world *)world)->Generation.cancelTest = true;
}

TIDE_TEST(scene_preparation_loading_views_can_read_progress)
{
    tide_game game = tide_game_api;
    game.start = cancellable_start;
    deferred_queue queue = {0};
    const tide_jobs server_jobs = {.self = &queue, .threads = 1, .background = defer, .help = complete_one};
    tide_loopback *net = tide_loopback_create(8431);
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &game, .tick_rate = 60,
        .transports = {tide_loopback_endpoint(net, 1)}, .jobs = &server_jobs}, 0);
    tide_session *session = tide_session_create(&(tide_session_desc){.game = &game,
        .tick_rate = 60, .jobs = tide_platform_jobs()});
    TIDE_REQUIRE(server && session);
    tide_session_join(session, tide_loopback_endpoint(net, 2), tide_loopback_address(1), 0);
    const uint64_t began = tide_time_now_ns();
    bool observed = false;
    do {
        const double now = (double)(tide_time_now_ns() - began) / 1e9;
        tide_loopback_set_time(net, now);
        tide_server_update(server, now);
        tide_session_update(session, now);
        const tide_view_worlds view = tide_session_view(session);
        if (view.current && ((const tide_world *)view.current)->Generation.started == 1) {
            observed = true;
            TIDE_CHECK(!view.verified || ((const tide_world *)view.verified)->Generation.started == 0);
            const tide_world *committed = tide_session_world(session);
            TIDE_CHECK(!committed || committed->Generation.started == 0);
            break;
        }
        tide_yield();
    } while (tide_time_now_ns() - began < UINT64_C(3000000000));
    TIDE_CHECK(observed && tide_server_tick(server) == 0);
    tide_session_destroy(session);
    tide_server_destroy(server);
    TIDE_CHECK(queue.head == NULL);
    tide_loopback_destroy(net);
}

TIDE_TEST(scene_preparation_publishes_progress_and_cancels_running_helpers)
{
    tide_world world = {0};
    tide_game_api.start(&world, 1.0f / 60.0f, NULL, 0);
    world.Generation.cancelTest = true;
    tide_preparation *state = NULL;
    const tide_jobs *jobs = tide_platform_jobs();
    TIDE_REQUIRE(jobs && jobs->background);
    bool observed = false;
    const uint64_t until = tide_time_now_ns() + UINT64_C(3000000000);
    do {
        TIDE_CHECK(!tide_preparation_update(&state, &tide_game_api, &world, jobs, NULL));
        const tide_world *view = tide_preparation_view(state);
        if (view && view->Generation.started == 1) {
            TIDE_CHECK(view->entities.page[0] != world.entities.page[0]);
            observed = true;
            break;
        }
        tide_yield();
    } while (tide_time_now_ns() < until);
    TIDE_CHECK(observed);
    TIDE_CHECK(world.Generation.started == 0 && world.Time.tick == 0);
    tide_preparation_free(state);
    TIDE_CHECK(world.Generation.started == 0);
    tide_world_free(&world);
}
#endif

static void network_preparation(bool transition)
{
    tide_game game = tide_game_api;
    if (transition) game.start = transition_start;
    deferred_queue server_queue = {0}, client_queue = {0};
    const tide_jobs server_jobs = {.self = &server_queue, .threads = 1, .background = defer, .help = complete_one};
    const tide_jobs client_jobs = {.self = &client_queue, .threads = 1, .background = defer, .help = complete_one};
    tide_loopback *net = tide_loopback_create(7331);
    TIDE_REQUIRE(net != NULL);
    tide_transport host = tide_loopback_endpoint(net, 1), peer = tide_loopback_endpoint(net, 2);
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &game, .tick_rate = 60,
        .transports = {host}, .jobs = &server_jobs}, 0);
    TIDE_REQUIRE(server != NULL);
    tide_client *client = tide_client_create(&(tide_client_desc){.game = &game, .transport = peer,
        .server = tide_loopback_address(1), .jobs = &client_jobs}, 0);
    TIDE_REQUIRE(client != NULL);
    bool server_ran_while_client_prepared = false;
    for (int frame = 0; frame < 600; frame++) {
        const double now = (double)frame / 120.0;
        tide_loopback_set_time(net, now);
        tide_server_update(server, now);
        tide_client_update(client, now);
        while (complete_one(&server_queue, false)) {}
        if (frame > 100) while (complete_one(&client_queue, false)) {}
        if (client_queue.head && tide_server_tick(server) > 10) server_ran_while_client_prepared = true;
    }
    const tide_client_status status = tide_client_status_of(client);
    const tide_world *authoritative = tide_server_world(server);
    const tide_world *verified = tide_client_verified_world(client);
    TIDE_REQUIRE(verified != NULL);
    TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED && status.resyncs == 0);
    TIDE_CHECK(server_ran_while_client_prepared);
    TIDE_CHECK(authoritative->Generation.started == (transition ? 2 : 1));
    TIDE_CHECK(verified->Generation.started == authoritative->Generation.started);
    TIDE_CHECK(verified->Generation.value == (transition ? 57 : 42));
    const tide_entity scene = transition ? authoritative->Generation.destination : tide_entity_in_slot(&authoritative->entities, 0);
    TIDE_CHECK(tide_world_scene_prepared(authoritative, scene, status.player));
    tide_client_destroy(client);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}

TIDE_TEST(scene_preparation_peers_generate_and_report_readiness) { network_preparation(false); }
TIDE_TEST(scene_preparation_later_transition_is_generated_once) { network_preparation(true); }
