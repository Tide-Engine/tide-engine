#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "microduck.h"
#include "tide/net.h"
#include "tide/session.h"
#include "tide_test.h"
#include "../runtime.h"
#include <oracle_expected.h>
#include <model_expected.h>
#ifdef NEWTON_TEST_JOBS
#include "tide/platform.h"
#endif

#define DUCK_DT (1.0f / 200.0f)
#define DUCK_JOINTS 14
#define DUCK_BODIES 15
#define DUCK_HASH UINT64_C(0x5e74bf9e0f8ba7ca)

static tide_entity body_at(const tide_world *world, Duck duck, int index)
{
    const tide_entity *value = tide_list_read(&world->heap, duck.bodies, index, sizeof(*value));
    return value ? *value : (tide_entity){0};
}

static tide_entity joint_at(const tide_world *world, Duck duck, int index)
{
    const tide_entity *value = tide_list_read(&world->heap, duck.joints, index, sizeof(*value));
    return value ? *value : (tide_entity){0};
}

static float number_at(const tide_world *world, tide_list list, int index)
{
    const float *value = tide_list_read(&world->heap, list, index, sizeof(*value));
    return value ? *value : 0.0f;
}

static DuckTransition transition_at(const tide_world *world, Duck duck, int index)
{
    const DuckTransition *value = tide_list_read(&world->heap, duck.transitions, index, sizeof(*value));
    return value ? *value : (DuckTransition){0};
}

static DuckControls duck_action(uint32_t tick, int seat)
{
    return (DuckControls){.select = tick % 18 == 0 ? 1 : 0,
        .drive = tide_sin_f((float)tick * 0.017f + (float)seat) * 0.75f};
}

static void duck_advance(tide_world *world, const tide_jobs *jobs)
{
    for (uint32_t seat = 0; seat < 2; ++seat)
        tide_world_set_input(world, (tide_player_id){seat}, duck_action((uint32_t)world->Time.tick, (int)seat));
    if (jobs) tide_world_tick_on(world, jobs);
    else tide_world_tick(world);
}

static void duck_ticks(tide_world *world, int count)
{
    for (int i = 0; i < count; ++i) duck_advance(world, NULL);
}

static uint64_t duck_identity(tide_entity entity)
{
    return (uint64_t)entity.index | (uint64_t)entity.generation << 32;
}

static nt_joint native_joint(tide_entity entity, Newton_Joint joint)
{
    nt_joint result = {.entity = duck_identity(entity), .parent = duck_identity(joint.parent),
        .child = duck_identity(joint.child), .type = joint.type,
        .position = joint.position, .velocity = joint.velocity,
        .damping = joint.damping, .armature = joint.armature};
    memcpy(result.parent_position, &joint.parentPosition, sizeof(result.parent_position));
    memcpy(result.parent_rotation, &joint.parentRotation, sizeof(result.parent_rotation));
    memcpy(result.child_position, &joint.childPosition, sizeof(result.child_position));
    memcpy(result.child_rotation, &joint.childRotation, sizeof(result.child_rotation));
    memcpy(result.axis, &joint.axis, sizeof(result.axis));
    return result;
}

TIDE_TEST(microduck_preserves_each_authored_mass_com_and_full_inertia_tensor)
{
    static tide_world world;
    tide_world_init(&world, DUCK_DT);
    const Duck duck = tide_get_Duck(&world, world.DuckFixture.first);
    TIDE_REQUIRE(tide_list_read_count(&world.heap, duck.bodies) == DUCK_BODIES);
    for (int i = 0; i < DUCK_BODIES; ++i) {
        const tide_entity entity = body_at(&world, duck, i);
        const Newton_Body body = tide_get_Newton_Body(&world, entity);
        const DuckPart part = tide_get_DuckPart(&world, entity);
        const duck_model_expected_body *source = &duck_model_expected[i];
        TIDE_CHECK(part.index == i);
        TIDE_CHECK(body.mass == source->mass);
        const tide_float3 com = tide_rotate_q(part.principalRotation, body.centerOfMass);
        TIDE_CHECK(fabsf(com.x - source->center_of_mass[0]) < 3e-8f);
        TIDE_CHECK(fabsf(com.y - source->center_of_mass[1]) < 3e-8f);
        TIDE_CHECK(fabsf(com.z - source->center_of_mass[2]) < 3e-8f);
        const tide_float3 x = tide_rotate_q(part.principalRotation, tide_f3(1, 0, 0));
        const tide_float3 y = tide_rotate_q(part.principalRotation, tide_f3(0, 1, 0));
        const tide_float3 z = tide_rotate_q(part.principalRotation, tide_f3(0, 0, 1));
        const float basis[3][3] = {{x.x, x.y, x.z}, {y.x, y.y, y.z}, {z.x, z.y, z.z}};
        const float diagonal[3] = {body.inertia.x, body.inertia.y, body.inertia.z};
        const int row[6] = {0, 1, 2, 0, 0, 1};
        const int col[6] = {0, 1, 2, 1, 2, 2};
        const float scale = fmaxf(source->full_inertia[0],
            fmaxf(source->full_inertia[1], source->full_inertia[2]));
        for (int k = 0; k < 6; ++k) {
            float inertia = 0;
            for (int axis = 0; axis < 3; ++axis)
                inertia += diagonal[axis] * basis[axis][row[k]] * basis[axis][col[k]];
            TIDE_CHECK(fabsf(inertia - source->full_inertia[k]) <= scale * 2e-6f);
        }
    }
    tide_world_free(&world);
}

TIDE_TEST(microduck_converted_model_matches_original_full_tensor_newton_dynamics)
{
    static tide_world world;
    tide_world_init(&world, DUCK_DT);
    const Duck duck = tide_get_Duck(&world, world.DuckFixture.first);
    TIDE_REQUIRE(tide_list_read_count(&world.heap, duck.bodies) == MD_ORACLE_BODY_COUNT);
    TIDE_REQUIRE(tide_list_read_count(&world.heap, duck.joints) == MD_ORACLE_DOF_COUNT);
    const Newton_Joint anchor = tide_get_Newton_Joint(&world, duck.anchor);
    const tide_float3 translation = anchor.parentPosition;
    nt_body bodies[MD_ORACLE_BODY_COUNT] = {0};
    nt_joint joints[MD_ORACLE_DOF_COUNT + 1] = {0};
    for (int i = 0; i < MD_ORACLE_BODY_COUNT; ++i) {
        const tide_entity entity = body_at(&world, duck, i);
        const Newton_Body body = tide_get_Newton_Body(&world, entity);
        bodies[i].entity = duck_identity(entity);
        bodies[i].type = NT_DYNAMIC;
        bodies[i].mass = body.mass;
        memcpy(bodies[i].inertia, &body.inertia, sizeof(bodies[i].inertia));
        memcpy(bodies[i].center_of_mass, &body.centerOfMass, sizeof(bodies[i].center_of_mass));
        memcpy(bodies[i].position, &body.position, sizeof(bodies[i].position));
        memcpy(bodies[i].rotation, &body.rotation, sizeof(bodies[i].rotation));
    }
    joints[0] = native_joint(duck.anchor, anchor);
    for (int i = 0; i < MD_ORACLE_DOF_COUNT; ++i) {
        const tide_entity entity = joint_at(&world, duck, i);
        joints[i + 1] = native_joint(entity, tide_get_Newton_Joint(&world, entity));
        joints[i + 1].position = md_oracle_initial.q[i];
        joints[i + 1].velocity = 0;
        joints[i + 1].damping = MD_ORACLE_DAMPING;
        joints[i + 1].armature = MD_ORACLE_ARMATURE;
        joints[i + 1].effort = md_oracle_effort[i];
        joints[i + 1].effort_limit = MD_ORACLE_EFFORT_LIMIT;
    }
    float maximum = 0;
    int steps = 0;
    for (int sample = 0; sample < MD_ORACLE_SAMPLE_COUNT; ++sample) {
        const md_oracle_sample *expected = &md_oracle_samples[sample];
        while (steps < expected->steps) {
            TIDE_REQUIRE(nt_step(bodies, MD_ORACLE_BODY_COUNT, joints, MD_ORACLE_DOF_COUNT + 1,
                md_oracle_gravity, MD_ORACLE_DT, 1) == NT_OK);
            ++steps;
        }
        for (int i = 0; i < MD_ORACLE_DOF_COUNT; ++i) {
            const float qe = fabsf(joints[i + 1].position - expected->q[i]);
            const float ve = fabsf(joints[i + 1].velocity - expected->qd[i]);
            if (qe > maximum) maximum = qe;
            if (ve > maximum) maximum = ve;
            if (qe > MD_ORACLE_TOLERANCE || ve > MD_ORACLE_TOLERANCE)
                printf("    %s step %d: q error %g, qd error %g\n", md_oracle_joint_names[i], steps,
                    (double)qe, (double)ve);
            TIDE_CHECK(qe <= MD_ORACLE_TOLERANCE && ve <= MD_ORACLE_TOLERANCE);
        }
        for (int i = 0; i < MD_ORACLE_BODY_COUNT; ++i) {
            tide_quaternion rotation;
            tide_float3 com;
            memcpy(&rotation, bodies[i].rotation, sizeof(rotation));
            memcpy(&com, bodies[i].center_of_mass, sizeof(com));
            const tide_float3 local = tide_rotate_q(rotation, com);
            const float actual[3] = {bodies[i].position[0] + local.x - translation.x,
                bodies[i].position[1] + local.y - translation.y,
                bodies[i].position[2] + local.z - translation.z};
            for (int k = 0; k < 3; ++k) {
                const float error = fabsf(actual[k] - expected->body_com_position[i][k]);
                if (error > maximum) maximum = error;
                if (error > MD_ORACLE_TOLERANCE)
                    printf("    %s step %d: COM axis %d error %g\n", md_oracle_body_names[i], steps, k, (double)error);
                TIDE_CHECK(error <= MD_ORACLE_TOLERANCE);
            }
        }
    }
    printf("    original full-tensor Newton oracle maximum absolute error: %g\n", (double)maximum);
    tide_world_free(&world);
}

TIDE_TEST(microduck_all_joints_move_and_record_complete_transitions)
{
    static tide_world world;
    tide_world_init(&world, DUCK_DT);
    duck_ticks(&world, 280);
    TIDE_REQUIRE(world.Newton_Physics.status == Newton_Status_Ok);
    const Duck duck = tide_get_Duck(&world, world.DuckFixture.first);
    TIDE_REQUIRE(tide_list_read_count(&world.heap, duck.bodies) == DUCK_BODIES);
    TIDE_REQUIRE(tide_list_read_count(&world.heap, duck.joints) == DUCK_JOINTS);
    TIDE_REQUIRE(tide_list_read_count(&world.heap, duck.targets) == DUCK_JOINTS);
    const int count = tide_list_read_count(&world.heap, duck.transitions);
    TIDE_REQUIRE(count == 64);
    const DuckTransition latest = transition_at(&world, duck, (duck.next + count - 1) % count);
    const DuckTransition previous = transition_at(&world, duck, (duck.next + count - 2) % count);
    TIDE_REQUIRE(tide_list_read_count(&world.heap, latest.observation) == 2 * DUCK_JOINTS);
    TIDE_REQUIRE(tide_list_read_count(&world.heap, latest.nextObservation) == 2 * DUCK_JOINTS);
    TIDE_REQUIRE(tide_list_read_count(&world.heap, latest.action) == DUCK_JOINTS);
    TIDE_REQUIRE(tide_list_read_count(&world.heap, latest.effort) == DUCK_JOINTS);
    TIDE_CHECK(latest.tick == world.Time.tick - 1);
    TIDE_CHECK(isfinite(latest.reward) && latest.reward <= 0.0f);
    bool moved = false;
    for (int i = 0; i < DUCK_JOINTS; ++i) {
        const tide_entity entity = joint_at(&world, duck, i);
        const Newton_Joint joint = tide_get_Newton_Joint(&world, entity);
        const Newton_Actuator actuator = tide_get_Newton_Actuator(&world, entity);
        TIDE_CHECK(number_at(&world, latest.nextObservation, i) == joint.position);
        TIDE_CHECK(number_at(&world, latest.nextObservation, i + DUCK_JOINTS) == joint.velocity);
        TIDE_CHECK(number_at(&world, latest.action, i) == number_at(&world, duck.targets, i));
        TIDE_CHECK(number_at(&world, latest.effort, i) == actuator.appliedEffort);
        TIDE_CHECK(fabsf(actuator.appliedEffort) <= actuator.effortLimit);
        TIDE_CHECK(isfinite(joint.position) && isfinite(joint.velocity));
        moved = moved || fabsf(joint.velocity) > 0.001f;
        for (int k = 0; k < 2; ++k) {
            const int slot = i + k * DUCK_JOINTS;
            TIDE_CHECK(number_at(&world, previous.nextObservation, slot) == number_at(&world, latest.observation, slot));
        }
    }
    TIDE_CHECK(moved);
    for (int i = 0; i < DUCK_BODIES; ++i) {
        const Newton_Body body = tide_get_Newton_Body(&world, body_at(&world, duck, i));
        TIDE_CHECK(body.mass > 0 && body.inertia.x > 0 && body.inertia.y > 0 && body.inertia.z > 0);
        TIDE_CHECK(isfinite(body.position.x) && isfinite(body.position.y) && isfinite(body.position.z));
    }
    printf("    MicroDuck tick %d: %016llx\n", world.Time.tick, (unsigned long long)tide_world_hash(&world));
    TIDE_CHECK(tide_world_hash(&world) == DUCK_HASH);
    tide_world_free(&world);
}

TIDE_TEST(microduck_snapshot_replay_and_alternating_worlds)
{
    static tide_world original, saved, replay, other;
    tide_world_init(&original, DUCK_DT);
    duck_ticks(&original, 40);
    tide_world_copy(&saved, &original);
    duck_ticks(&original, 100);
    const uint64_t expected = tide_world_hash(&original);
    tide_world_init(&other, DUCK_DT);
    tide_world_copy(&replay, &saved);
    for (int i = 0; i < 100; ++i) {
        duck_advance(&other, NULL);
        duck_advance(&replay, NULL);
    }
    TIDE_CHECK(tide_world_hash(&replay) == expected);
    TIDE_CHECK(tide_world_hash(&saved) != expected);
    tide_world_free(&original);
    tide_world_free(&saved);
    tide_world_free(&replay);
    tide_world_free(&other);
}

TIDE_TEST(microduck_reset_reproduces_the_first_episode_step)
{
    static tide_world world, fresh;
    tide_world_init(&world, DUCK_DT);
    duck_ticks(&world, 110);
    tide_world_init(&fresh, DUCK_DT);
    for (uint32_t seat = 0; seat < 2; ++seat) {
        tide_world_set_input(&world, (tide_player_id){seat}, (DuckControls){.reset = true});
        tide_world_set_input(&fresh, (tide_player_id){seat}, (DuckControls){.reset = true});
    }
    tide_world_tick(&world);
    tide_world_tick(&fresh);
    TIDE_REQUIRE(world.Newton_Physics.status == Newton_Status_Ok);
    const Duck reset_duck = tide_get_Duck(&world, world.DuckFixture.first);
    const Duck fresh_duck = tide_get_Duck(&fresh, fresh.DuckFixture.first);
    TIDE_CHECK(tide_list_read_count(&world.heap, reset_duck.transitions) == 1);
    for (int i = 0; i < DUCK_JOINTS; ++i) {
        const Newton_Joint a = tide_get_Newton_Joint(&world, joint_at(&world, reset_duck, i));
        const Newton_Joint b = tide_get_Newton_Joint(&fresh, joint_at(&fresh, fresh_duck, i));
        TIDE_CHECK(a.position == b.position && a.velocity == b.velocity);
        TIDE_CHECK(number_at(&world, reset_duck.targets, i) == number_at(&fresh, fresh_duck.targets, i));
    }
    for (int i = 0; i < DUCK_BODIES; ++i) {
        const Newton_Body a = tide_get_Newton_Body(&world, body_at(&world, reset_duck, i));
        const Newton_Body b = tide_get_Newton_Body(&fresh, body_at(&fresh, fresh_duck, i));
        TIDE_CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    }
    tide_world_free(&world);
    tide_world_free(&fresh);
}

TIDE_TEST(microduck_canonical_and_delta_transfer_preserve_owned_transition_lists)
{
    static tide_world source, base, packed, delta;
    tide_world_init(&source, DUCK_DT);
    duck_ticks(&source, 70);
    tide_world_copy(&base, &source);
    duck_ticks(&source, 20);
    const uint32_t size = tide_world_pack(&source, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes && tide_world_pack(&source, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&packed, bytes, size));
    free(bytes);
    uint32_t delta_size = 0;
    bytes = tide_world_pack_delta(&source, &base, NULL, 0, &delta_size);
    TIDE_REQUIRE(bytes && tide_world_unpack_delta(&delta, &base, bytes, delta_size));
    free(bytes);
    TIDE_CHECK(tide_world_hash(&packed) == tide_world_hash(&source));
    TIDE_CHECK(tide_world_hash(&delta) == tide_world_hash(&source));
    duck_ticks(&source, 35);
    duck_ticks(&packed, 35);
    duck_ticks(&delta, 35);
    TIDE_CHECK(tide_world_hash(&packed) == tide_world_hash(&source));
    TIDE_CHECK(tide_world_hash(&delta) == tide_world_hash(&source));
    tide_world_free(&source);
    tide_world_free(&base);
    tide_world_free(&packed);
    tide_world_free(&delta);
}

#ifdef NEWTON_TEST_JOBS
TIDE_TEST(microduck_threaded_ticks_match_serial_worlds)
{
    const tide_jobs *jobs = tide_platform_jobs();
    static tide_world serial, threaded;
    tide_world_init(&serial, DUCK_DT);
    tide_world_init(&threaded, DUCK_DT);
    for (int i = 0; i < 90; ++i) {
        duck_advance(&serial, NULL);
        duck_advance(&threaded, jobs);
        TIDE_REQUIRE(tide_world_hash(&serial) == tide_world_hash(&threaded));
    }
    tide_world_free(&serial);
    tide_world_free(&threaded);
}
#endif

static void duck_policy(void *user, uint32_t tick, void *input)
{
    *(DuckControls *)input = duck_action(tick, *(const int *)user);
}

TIDE_TEST(microduck_two_players_use_the_same_physics_through_rollback)
{
    tide_loopback *net = tide_loopback_create(8021);
    TIDE_REQUIRE(net);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.025, .jitter = 0.005, .loss = 0.02});
    tide_server *server = tide_server_create(&(tide_server_desc){
        .game = &tide_game_api, .tick_rate = 200, .transports = {tide_loopback_endpoint(net, 1)}}, 0);
    TIDE_REQUIRE(server);
    int seats[2] = {0, 1};
    tide_client *clients[2];
    for (int i = 0; i < 2; ++i) {
        clients[i] = tide_client_create(&(tide_client_desc){.game = &tide_game_api,
            .transport = tide_loopback_endpoint(net, (uint32_t)i + 2), .server = tide_loopback_address(1),
            .lead = 2, .sample = duck_policy, .user = &seats[i]}, 0);
        TIDE_REQUIRE(clients[i]);
    }
    double now = 0;
    for (int frame = 0; frame < 400; ++frame) {
        now += 1.0 / 200.0;
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
        printf("    duck player %d: %u verified ticks, %u replayed, %u resyncs\n", i,
            status.verified_tick, status.again_ticks, status.resyncs);
        tide_client_destroy(clients[i]);
    }
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}
