#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "robot.h"
#include "tide/net.h"
#include "tide/session.h"
#include "tide_test.h"
#ifdef NEWTON_TEST_JOBS
#include "tide/platform.h"
#endif

#define DT (1.0f / 120.0f)
#define ROBOT_HASH UINT64_C(0xb3a702e67f351594)

static Controls action(uint32_t tick, int seat)
{
    const float phase = (float)tick * 0.013f + (float)seat;
    return (Controls){.target = {tide_sin_f(phase) * 0.8f, tide_cos_f(phase * 0.7f) * 0.6f}};
}

static void advance(tide_world *world, const tide_jobs *jobs)
{
    for (uint32_t seat = 0; seat < 2; ++seat)
        tide_world_set_input(world, (tide_player_id){seat}, action((uint32_t)world->Time.tick, (int)seat));
    if (jobs) tide_world_tick_on(world, jobs);
    else tide_world_tick(world);
}

static void ticks(tide_world *world, int count)
{
    for (int i = 0; i < count; ++i) advance(world, NULL);
}

static Transition sample(const tide_world *world, Arm arm, int index)
{
    const Transition *value = tide_list_read(&world->heap, arm.transitions, index, sizeof(Transition));
    return value ? *value : (Transition){0};
}

TIDE_TEST(newton_robot_observations_actions_and_bounded_transitions)
{
    static tide_world world;
    tide_world_init(&world, DT);
    ticks(&world, 200);
    TIDE_REQUIRE(world.Newton_Physics.status == Newton_Status_Ok);
    const Arm arm = tide_get_Arm(&world, world.Fixture.first);
    TIDE_CHECK(tide_list_count(arm.transitions) == 128);
    const Transition latest = sample(&world, arm, (arm.next + 127) % 128);
    const Newton_Joint shoulder = tide_get_Newton_Joint(&world, arm.shoulder);
    const Newton_Joint elbow = tide_get_Newton_Joint(&world, arm.elbow);
    TIDE_CHECK(latest.tick == world.Time.tick - 1);
    TIDE_CHECK(latest.nextObservation.x == shoulder.position && latest.nextObservation.y == elbow.position);
    TIDE_CHECK(latest.nextObservation.z == shoulder.velocity && latest.nextObservation.w == elbow.velocity);
    TIDE_CHECK(latest.effort.x == tide_get_Newton_Actuator(&world, arm.shoulder).appliedEffort);
    TIDE_CHECK(latest.effort.y == tide_get_Newton_Actuator(&world, arm.elbow).appliedEffort);
    TIDE_CHECK(fabsf(latest.effort.x) <= 30 && fabsf(latest.effort.y) <= 20);
    TIDE_CHECK(isfinite(latest.reward) && latest.reward <= 0);
    const Transition previous = sample(&world, arm, (arm.next + 126) % 128);
    TIDE_CHECK(memcmp(&previous.nextObservation, &latest.observation, sizeof(tide_float4)) == 0);
    printf("    robot at tick %d: %016llx\n", world.Time.tick, (unsigned long long)tide_world_hash(&world));
    TIDE_CHECK(tide_world_hash(&world) == ROBOT_HASH);
    tide_world_free(&world);
}

TIDE_TEST(newton_robot_snapshot_episode_reset_and_alternating_worlds)
{
    static tide_world original, saved, replay, other;
    tide_world_init(&original, DT);
    ticks(&original, 80);
    tide_world_copy(&saved, &original);
    ticks(&original, 160);
    const uint64_t expected = tide_world_hash(&original);
    tide_world_init(&other, DT);
    tide_world_copy(&replay, &saved);
    for (int i = 0; i < 160; ++i) {
        advance(&other, NULL);
        advance(&replay, NULL);
    }
    TIDE_CHECK(tide_world_hash(&replay) == expected);
    TIDE_CHECK(tide_world_hash(&saved) != expected);
    tide_world_free(&replay);
    tide_world_copy(&replay, &saved);
    ticks(&replay, 160);
    TIDE_CHECK(tide_world_hash(&replay) == expected);
    tide_world_free(&original);
    tide_world_free(&saved);
    tide_world_free(&replay);
    tide_world_free(&other);
}

TIDE_TEST(newton_robot_pack_and_delta_continue_the_same_simulation)
{
    static tide_world source, packed, delta, base;
    tide_world_init(&source, DT);
    ticks(&source, 40);
    tide_world_copy(&base, &source);
    ticks(&source, 30);
    const uint64_t expected = tide_world_hash(&source);
    const uint32_t size = tide_world_pack(&source, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes && tide_world_pack(&source, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&packed, bytes, size));
    free(bytes);
    uint32_t delta_size = 0;
    bytes = tide_world_pack_delta(&source, &base, NULL, 0, &delta_size);
    TIDE_REQUIRE(bytes && tide_world_unpack_delta(&delta, &base, bytes, delta_size));
    free(bytes);
    TIDE_CHECK(tide_world_hash(&packed) == expected && tide_world_hash(&delta) == expected);
    ticks(&source, 60);
    ticks(&packed, 60);
    ticks(&delta, 60);
    TIDE_CHECK(tide_world_hash(&packed) == tide_world_hash(&source));
    TIDE_CHECK(tide_world_hash(&delta) == tide_world_hash(&source));
    tide_world_free(&source);
    tide_world_free(&packed);
    tide_world_free(&delta);
    tide_world_free(&base);
}

TIDE_TEST(newton_robot_invalid_reference_does_not_publish_physics)
{
    static tide_world world;
    tide_world_init(&world, DT);
    ticks(&world, 5);
    const Arm arm = tide_get_Arm(&world, world.Fixture.first);
    const Newton_Body before = tide_get_Newton_Body(&world, arm.tip);
    Newton_Joint joint = tide_get_Newton_Joint(&world, arm.elbow);
    joint.parent.generation += 1;
    TIDE_REQUIRE(tide_set_Newton_Joint(&world, arm.elbow, joint));
    ticks(&world, 1);
    TIDE_CHECK(world.Newton_Physics.status == Newton_Status_InvalidJoint);
    const Newton_Body after = tide_get_Newton_Body(&world, arm.tip);
    TIDE_CHECK(memcmp(&before, &after, sizeof(before)) == 0);
    TIDE_CHECK(tide_list_count(tide_get_Arm(&world, world.Fixture.first).transitions) == 5);
    tide_world_free(&world);
}

#ifdef NEWTON_TEST_JOBS
TIDE_TEST(newton_robot_tick_threads_preserve_every_state_bit)
{
    const tide_jobs *jobs = tide_platform_jobs();
    static tide_world serial, threaded;
    tide_world_init(&serial, DT);
    tide_world_init(&threaded, DT);
    for (int i = 0; i < 180; ++i) {
        advance(&serial, NULL);
        advance(&threaded, jobs);
        TIDE_REQUIRE(tide_world_hash(&serial) == tide_world_hash(&threaded));
    }
    tide_world_free(&serial);
    tide_world_free(&threaded);
}
#endif

static void policy(void *user, uint32_t tick, void *input)
{
    const int seat = *(const int *)user;
    *(Controls *)input = action(tick, seat);
}

TIDE_TEST(newton_two_players_predict_and_replay_over_a_lossy_network)
{
    tide_loopback *net = tide_loopback_create(6041);
    TIDE_REQUIRE(net);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.035, .jitter = 0.01, .loss = 0.03});
    tide_server *server = tide_server_create(&(tide_server_desc){
        .game = &tide_game_api, .tick_rate = 120, .transports = {tide_loopback_endpoint(net, 1)}}, 0);
    TIDE_REQUIRE(server);
    int seats[2] = {0, 1};
    tide_client *clients[2];
    for (int i = 0; i < 2; ++i) {
        clients[i] = tide_client_create(&(tide_client_desc){.game = &tide_game_api,
            .transport = tide_loopback_endpoint(net, (uint32_t)i + 2), .server = tide_loopback_address(1),
            .lead = 2, .sample = policy, .user = &seats[i]}, 0);
        TIDE_REQUIRE(clients[i]);
    }
    double now = 0;
    for (int frame = 0; frame < 720; ++frame) {
        now += 1.0 / 120.0;
        tide_loopback_set_time(net, now);
        for (int i = 0; i < 2; ++i) tide_client_update(clients[i], now);
        tide_server_update(server, now);
    }
    const tide_world *authority = tide_server_world(server);
    TIDE_REQUIRE(authority && authority->Newton_Physics.status == Newton_Status_Ok);
    for (int i = 0; i < 2; ++i) {
        const tide_client_status status = tide_client_status_of(clients[i]);
        TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(status.resyncs == 0 && status.again_ticks > 0);
        const tide_world *verified = tide_client_verified_world(clients[i]);
        TIDE_CHECK(verified && verified->Newton_Physics.status == Newton_Status_Ok);
        printf("    player %d: %u verified ticks, %u replayed, %u resyncs\n", i, status.verified_tick,
               status.again_ticks, status.resyncs);
        tide_client_destroy(clients[i]);
    }
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}
