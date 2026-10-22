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
#define DUCK_OBSERVATIONS 44
/* Assisted free-root fixture: observed on native and WASI before pinning. */
#define DUCK_HASH UINT64_C(0xd39953ac3eb20462)

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
        .drive = tide_sin_f((float)tick * 0.017f + (float)seat) * 0.75f,
        .walk = seat == 0 ? 1.0f : -1.0f};
}

static void duck_advance(tide_world *world, const tide_jobs *jobs)
{
    for (uint32_t seat = 0; seat < 2; ++seat)
        tide_world_set_input(world, tide_player_from_index((int32_t)seat), duck_action((uint32_t)world->Time.tick, (int)seat));
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

static tide_quaternion original_rotation(const tide_world *world, Duck duck, int index)
{
    const tide_entity entity = body_at(world, duck, index);
    const Newton_Body body = tide_get_Newton_Body(world, entity);
    const DuckPart part = tide_get_DuckPart(world, entity);
    return tide_mul_q(body.rotation, tide_inverse_q(part.principalRotation));
}

/* Independently authored source-frame proxy corners, not DuckSolePoint calls. */
static tide_float3 sole_point(Newton_Body body, tide_quaternion rotation, int corner)
{
    static const float x[4] = {-0.032f, -0.032f, 0.032f, 0.032f};
    static const float z[4] = {-0.0290852f, 0.0009148f, -0.0290852f, 0.0009148f};
    return tide_add_f3(body.position, tide_rotate_q(rotation, tide_f3(x[corner], -0.0238146f, z[corner])));
}

static void check_vector(tide_float3 actual, tide_float3 expected, float tolerance)
{
    TIDE_CHECK(tide_distance_f3(actual, expected) <= tolerance);
}

static void check_observation(const tide_world *world, Duck duck, tide_list observation)
{
    TIDE_REQUIRE(tide_list_read_count(&world->heap, observation) == DUCK_OBSERVATIONS);
    const Newton_Body root = tide_get_Newton_Body(world, body_at(world, duck, 0));
    const tide_float3 gravity = tide_rotate_q(tide_inverse_q(original_rotation(world, duck, 0)), tide_f3(0, -1, 0));
    const float expected[16] = {root.position.y,
        root.velocity.x, root.velocity.y, root.velocity.z,
        root.angularVelocity.x, root.angularVelocity.y, root.angularVelocity.z,
        gravity.x, gravity.y, gravity.z, duck.footLoad.x, duck.footLoad.y, duck.walk,
        tide_sin_f(duck.phase), tide_cos_f(duck.phase), duck.assisted ? 1.0f : 0.0f};
    for (int i = 0; i < 16; ++i)
        TIDE_CHECK(number_at(world, observation, 28 + i) == expected[i]);
}

TIDE_TEST(microduck_spawns_coherent_original_fk_without_a_fixed_root)
{
    static tide_world world;
    tide_world_init(&world, DUCK_DT);
    const tide_quaternion up = tide_axisangle_q(tide_f3(1, 0, 0), -TIDE_PI_F / 2);
    for (int seat = 0; seat < 2; ++seat) {
        const Duck duck = tide_get_Duck(&world, seat == 0 ? world.DuckFixture.first : world.DuckFixture.second);
        TIDE_CHECK(duck.assisted && duck.floorEnabled);
        for (int i = 0; i < DUCK_BODIES; ++i) {
            const duck_model_expected_body *source = &duck_model_expected[i];
            const Newton_Body body = tide_get_Newton_Body(&world, body_at(&world, duck, i));
            const tide_float3 local = tide_f3(source->home_position[0], source->home_position[1], source->home_position[2]);
            const tide_quaternion local_rotation = tide_q(source->home_rotation[0], source->home_rotation[1],
                source->home_rotation[2], source->home_rotation[3]);
            check_vector(body.position, tide_add_f3(duck.spawnPosition, tide_rotate_q(up, local)), 3e-7f);
            TIDE_CHECK(fabsf(tide_dot_q(original_rotation(&world, duck, i), tide_mul_q(up, local_rotation))) > 0.999999f);
            TIDE_CHECK(body.type == Newton_BodyType_Dynamic);
            check_vector(body.velocity, tide_f3(0, 0, 0), 0);
            check_vector(body.angularVelocity, tide_f3(0, 0, 0), 0);
        }
        for (int i = 0; i < DUCK_JOINTS; ++i) {
            const Newton_Joint joint = tide_get_Newton_Joint(&world, joint_at(&world, duck, i));
            TIDE_CHECK(joint.type == Newton_JointType_Revolute && joint.parent.index != 0);
            TIDE_CHECK(joint.child.index != body_at(&world, duck, 0).index);
        }
    }
    tide_world_tick(&world);
    TIDE_CHECK(tide_list_read_count(&world.heap, world.Newton_Physics.joints) == 2 * DUCK_JOINTS);
    tide_world_free(&world);
}

TIDE_TEST(microduck_floor_uses_com_point_velocity_coulomb_friction_and_point_torque)
{
    static tide_world world;
    tide_world_init(&world, DUCK_DT);
    Duck duck = tide_get_Duck(&world, world.DuckFixture.first);
    duck.assisted = false;
    TIDE_REQUIRE(tide_set_Duck(&world, world.DuckFixture.first, duck));
    const tide_entity root_entity = body_at(&world, duck, 0);
    Newton_Body root = tide_get_Newton_Body(&world, root_entity);
    /* Validation rejects before integration, exposing the game's actual applied
     * wrench without FK replacing these deliberately chosen point states. */
    root.mass = -1;
    root.force = tide_f3(21, 22, 23);
    root.torque = tide_f3(24, 25, 26);
    TIDE_REQUIRE(tide_set_Newton_Body(&world, root_entity, root));
    bool saw_angular_effect = false, saw_com_arm_effect = false;
    for (int scenario = 0; scenario < 6; ++scenario) {
        tide_float3 force[2], torque[2];
        float load[2];
        for (int foot = 0; foot < 2; ++foot) {
            const int index = foot == 0 ? 5 : 14;
            const tide_entity entity = body_at(&world, duck, index);
            Newton_Body body = tide_get_Newton_Body(&world, entity);
            const DuckPart part = tide_get_DuckPart(&world, entity);
            const tide_quaternion rotation = tide_euler_q(tide_f3(0.15f, 0.35f, -0.1f));
            body.rotation = tide_mul_q(rotation, part.principalRotation);
            body.position = tide_f3(0.8f + (float)foot, scenario == 0 ? 0.1f : scenario == 5 ? -0.08f : 0.020f, -0.3f);
            body.velocity = scenario == 1 ? tide_f3(0, 0, 0) :
                tide_f3(scenario == 3 ? 10.0f : 0.4f, scenario == 4 ? 10.0f : -0.2f, -0.3f);
            body.angularVelocity = scenario >= 2 && scenario != 4 ? tide_f3(1.7f, -2.3f, 3.1f) : tide_f3(0, 0, 0);
            body.force = tide_f3(31, 32, 33);
            body.torque = tide_f3(41, 42, 43);
            TIDE_REQUIRE(tide_set_Newton_Body(&world, entity, body));
            force[foot] = torque[foot] = tide_f3(0, 0, 0);
            load[foot] = 0;
            const tide_float3 center = tide_add_f3(body.position, tide_rotate_q(body.rotation, body.centerOfMass));
            for (int corner = 0; corner < 4; ++corner) {
                const tide_float3 point = sole_point(body, rotation, corner);
                const tide_float3 arm = tide_sub_f3(point, center);
                const tide_float3 spin = tide_cross_f3(body.angularVelocity, arm);
                const tide_float3 velocity = tide_add_f3(body.velocity, spin);
                if (point.y >= 0) continue;
                const float normal = fminf(12, fmaxf(0, -1000 * point.y - 4 * velocity.y));
                const float speed = tide_sqrt_f(velocity.x * velocity.x + velocity.z * velocity.z);
                const float gain = fminf(20, 0.8f * normal / fmaxf(speed, 1e-6f));
                const tide_float3 f = tide_f3(-gain * velocity.x, normal, -gain * velocity.z);
                const tide_float3 moment = tide_cross_f3(arm, f);
                force[foot] = tide_add_f3(force[foot], f);
                torque[foot] = tide_add_f3(torque[foot], moment);
                load[foot] += normal;
                TIDE_CHECK(f.x * velocity.x + f.z * velocity.z <= 0);
                TIDE_CHECK(tide_sqrt_f(f.x * f.x + f.z * f.z) <= 0.8f * normal + 1e-6f);
                saw_angular_effect = saw_angular_effect || (normal > 0 && fabsf(spin.y) > 0.01f);
                saw_com_arm_effect = saw_com_arm_effect ||
                    tide_distance_f3(moment, tide_cross_f3(tide_sub_f3(point, body.position), f)) > 0.001f;
            }
        }
        for (int repeat = 0; repeat < 2; ++repeat) {
            tide_world_tick(&world);
            TIDE_REQUIRE(world.Newton_Physics.status == Newton_Status_InvalidBody);
            duck = tide_get_Duck(&world, world.DuckFixture.first);
            TIDE_CHECK(tide_list_read_count(&world.heap, duck.transitions) == 0);
            TIDE_CHECK(fabsf(duck.footLoad.x - load[0]) < 2e-4f);
            TIDE_CHECK(fabsf(duck.footLoad.y - load[1]) < 2e-4f);
            for (int foot = 0; foot < 2; ++foot) {
                const Newton_Body body = tide_get_Newton_Body(&world, body_at(&world, duck, foot == 0 ? 5 : 14));
                check_vector(body.force, force[foot], 3e-4f);
                check_vector(body.torque, torque[foot], 2e-5f);
                if (scenario == 0 || scenario == 4) check_vector(body.force, tide_f3(0, 0, 0), 0);
                if (scenario == 5) TIDE_CHECK(load[foot] == 48);
            }
            check_vector(duck.assistanceForce, tide_f3(0, 0, 0), 0);
            check_vector(duck.assistanceTorque, tide_f3(0, 0, 0), 0);
            const Newton_Body cleared = tide_get_Newton_Body(&world, root_entity);
            check_vector(cleared.force, tide_f3(0, 0, 0), 0);
            check_vector(cleared.torque, tide_f3(0, 0, 0), 0);
        }
    }
    TIDE_CHECK(saw_angular_effect && saw_com_arm_effect);
    /* Numeric flat-sole cases independently fix signs, units, friction branches
     * and load-channel assignment, in addition to the rotated model above. */
    for (int active_foot = 0; active_foot < 2; ++active_foot) {
        for (int scenario = 0; scenario < 3; ++scenario) {
            for (int foot = 0; foot < 2; ++foot) {
                const tide_entity entity = body_at(&world, duck, foot == 0 ? 5 : 14);
                Newton_Body body = tide_get_Newton_Body(&world, entity);
                DuckPart part = tide_get_DuckPart(&world, entity);
                part.principalRotation = tide_identity_q();
                body.rotation = tide_identity_q();
                body.position = tide_f3(0, foot == active_foot ? 0.0218146f : 0.1f, 0);
                body.centerOfMass = tide_f3(0.01f, 0.02f, 0.03f);
                body.velocity = scenario == 0 ? tide_f3(0, 0, 0) :
                    scenario == 1 ? tide_f3(0.01f, 0, 0.02f) : tide_f3(3, 0, 4);
                body.angularVelocity = tide_f3(0, 0, 0);
                TIDE_REQUIRE(tide_set_DuckPart(&world, entity, part));
                TIDE_REQUIRE(tide_set_Newton_Body(&world, entity, body));
            }
            tide_world_tick(&world);
            TIDE_REQUIRE(world.Newton_Physics.status == Newton_Status_InvalidBody);
            duck = tide_get_Duck(&world, world.DuckFixture.first);
            TIDE_CHECK(fabsf((active_foot == 0 ? duck.footLoad.x : duck.footLoad.y) - 8) < 1e-4f);
            TIDE_CHECK((active_foot == 0 ? duck.footLoad.y : duck.footLoad.x) == 0);
            const Newton_Body body = tide_get_Newton_Body(&world, body_at(&world, duck, active_foot == 0 ? 5 : 14));
            const tide_float3 expected = scenario == 0 ? tide_f3(0, 8, 0) :
                scenario == 1 ? tide_f3(-0.8f, 8, -1.6f) : tide_f3(-3.84f, 8, -5.12f);
            check_vector(body.force, expected, 1e-4f);
            check_vector(body.torque, tide_cross_f3(tide_f3(-0.01f, -0.0438146f, -0.0440852f), expected), 1e-5f);
        }
    }
    tide_world_free(&world);
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
    const tide_entity root_entity = body_at(&world, duck, 0);
    const Newton_Body root = tide_get_Newton_Body(&world, root_entity);
    const tide_float3 translation = root.position;
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
    /* The game has no anchor. This private value-only oracle adds its own
     * world fixed joint using the actual principal-frame root pose. */
    joints[0] = (nt_joint){.entity = UINT64_MAX, .child = duck_identity(root_entity),
        .type = NT_FIXED, .child_rotation = {0, 0, 0, 1}};
    memcpy(joints[0].parent_position, &root.position, sizeof(joints[0].parent_position));
    memcpy(joints[0].parent_rotation, &root.rotation, sizeof(joints[0].parent_rotation));
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
    TIDE_REQUIRE(tide_list_read_count(&world.heap, latest.observation) == DUCK_OBSERVATIONS);
    TIDE_REQUIRE(tide_list_read_count(&world.heap, latest.nextObservation) == DUCK_OBSERVATIONS);
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
        const float command = 4 * (number_at(&world, latest.action, i) - number_at(&world, latest.observation, i))
            - 0.03f * number_at(&world, latest.observation, i + DUCK_JOINTS);
        TIDE_CHECK(number_at(&world, latest.effort, i) == fminf(actuator.effortLimit, fmaxf(-actuator.effortLimit, command)));
        TIDE_CHECK(fabsf(actuator.appliedEffort) <= actuator.effortLimit);
        TIDE_CHECK(isfinite(joint.position) && isfinite(joint.velocity));
        moved = moved || fabsf(joint.velocity) > 0.001f;
    }
    for (int i = 0; i < DUCK_OBSERVATIONS; ++i) {
        TIDE_CHECK(isfinite(number_at(&world, latest.nextObservation, i)));
        TIDE_CHECK(number_at(&world, previous.nextObservation, i) == number_at(&world, latest.observation, i));
    }
    TIDE_CHECK(moved);
    float error = 0;
    for (int i = 0; i < DUCK_JOINTS; ++i) {
        const float delta = number_at(&world, latest.action, i) - number_at(&world, latest.nextObservation, i);
        error += delta * delta;
    }
    TIDE_CHECK(latest.reward == -error);
    for (int i = 0; i < count; ++i)
        TIDE_CHECK(transition_at(&world, duck, (duck.next + i) % count).tick == world.Time.tick - count + i);
    check_observation(&world, duck, latest.nextObservation);
    check_vector(latest.assistanceForce, duck.assistanceForce, 0);
    check_vector(latest.assistanceTorque, duck.assistanceTorque, 0);
    TIDE_CHECK(latest.assistanceForce.x == 0 && latest.assistanceForce.y == 0);
    for (int i = 0; i < DUCK_BODIES; ++i) {
        const Newton_Body body = tide_get_Newton_Body(&world, body_at(&world, duck, i));
        TIDE_CHECK(body.mass > 0 && body.inertia.x > 0 && body.inertia.y > 0 && body.inertia.z > 0);
        TIDE_CHECK(isfinite(body.position.x) && isfinite(body.position.y) && isfinite(body.position.z));
    }
    printf("    MicroDuck tick %d: %016llx\n", world.Time.tick, (unsigned long long)tide_world_hash(&world));
    TIDE_CHECK(tide_world_hash(&world) == DUCK_HASH);
    tide_world_free(&world);
}

TIDE_TEST(microduck_unanchored_standing_is_supported_by_floor_reactions)
{
    static tide_world supported, falling;
    tide_world_init(&supported, DUCK_DT);
    tide_world_init(&falling, DUCK_DT);
    Duck no_floor = tide_get_Duck(&falling, falling.DuckFixture.first);
    no_floor.floorEnabled = false;
    no_floor.assisted = false;
    TIDE_REQUIRE(tide_set_Duck(&falling, falling.DuckFixture.first, no_floor));
    const Newton_Body initial = tide_get_Newton_Body(&falling, body_at(&falling, no_floor, 0));
    for (int i = 0; i < 40; ++i) tide_world_tick(&falling);
    TIDE_REQUIRE(falling.Newton_Physics.status == Newton_Status_Ok);
    no_floor = tide_get_Duck(&falling, falling.DuckFixture.first);
    const Newton_Body dropped = tide_get_Newton_Body(&falling, body_at(&falling, no_floor, 0));
    TIDE_CHECK(dropped.position.y < initial.position.y - 0.12f);
    TIDE_CHECK(dropped.velocity.y < -1.5f);
    TIDE_CHECK(no_floor.footLoad.x == 0 && no_floor.footLoad.y == 0);
    check_vector(no_floor.assistanceForce, tide_f3(0, 0, 0), 0);
    check_vector(no_floor.assistanceTorque, tide_f3(0, 0, 0), 0);
    tide_world_set_input(&falling, tide_player_from_index(0), (DuckControls){.reset = true});
    tide_world_tick(&falling);
    no_floor = tide_get_Duck(&falling, falling.DuckFixture.first);
    TIDE_CHECK(!no_floor.floorEnabled && no_floor.assisted);
    TIDE_CHECK(tide_list_read_count(&falling.heap, no_floor.transitions) == 1);
    TIDE_CHECK(tide_get_Newton_Body(&falling, body_at(&falling, no_floor, 0)).position.y > 0.119f);
    double total_load = 0;
    float mass = 0;
    for (int tick = 0; tick < 600; ++tick) {
        tide_world_tick(&supported);
        TIDE_REQUIRE(supported.Newton_Physics.status == Newton_Status_Ok);
        const Duck duck = tide_get_Duck(&supported, supported.DuckFixture.first);
        const Newton_Body root = tide_get_Newton_Body(&supported, body_at(&supported, duck, 0));
        if (tick >= 200) {
            TIDE_CHECK(root.position.y > 0.09f && root.position.y < 0.15f);
            TIDE_CHECK(tide_rotate_q(original_rotation(&supported, duck, 0), tide_f3(0, 0, 1)).y > 0.9f);
            total_load += duck.footLoad.x + duck.footLoad.y;
        }
        TIDE_CHECK(duck.assistanceForce.x == 0 && duck.assistanceForce.y == 0);
        if (tick == 0) for (int i = 0; i < DUCK_BODIES; ++i)
            mass += tide_get_Newton_Body(&supported, body_at(&supported, duck, i)).mass;
    }
    printf("    standing mean floor load %.6f N, weight %.6f N; floor-off height %.6f m\n",
        total_load / 400, (double)mass * 9.81, (double)dropped.position.y);
    TIDE_CHECK(fabs(total_load / 400 - (double)mass * 9.81) < 0.15);
    tide_world_free(&supported);
    tide_world_free(&falling);
}

TIDE_TEST(microduck_assistance_toggle_records_actual_wrenches_and_changes_physics)
{
    static tide_world assisted, unassisted;
    tide_world_init(&assisted, DUCK_DT);
    Duck duck = tide_get_Duck(&assisted, assisted.DuckFixture.first);
    const tide_entity root_entity = body_at(&assisted, duck, 0);
    Newton_Body root = tide_get_Newton_Body(&assisted, root_entity);
    const DuckPart part = tide_get_DuckPart(&assisted, root_entity);
    const tide_quaternion original = tide_mul_q(tide_axisangle_q(tide_f3(0, 1, 0), 0.5f),
        original_rotation(&assisted, duck, 0));
    root.rotation = tide_mul_q(original, part.principalRotation);
    root.velocity.z = 0.4f;
    root.angularVelocity = tide_f3(0.1f, 0.2f, -0.3f);
    TIDE_REQUIRE(tide_set_Newton_Body(&assisted, root_entity, root));
    tide_world_copy(&unassisted, &assisted);
    tide_world_set_input(&unassisted, tide_player_from_index(0), (DuckControls){.assistance = true});
    const tide_float3 up = tide_rotate_q(original, tide_f3(0, 0, 1));
    const tide_float3 forward = tide_rotate_q(original, tide_f3(1, 0, 0));
    tide_float3 torque = tide_sub_f3(tide_mul_f3(tide_f3_splat(2), tide_cross_f3(up, tide_f3(0, 1, 0))),
        tide_mul_f3(tide_f3_splat(0.15f), root.angularVelocity));
    torque.y = 0.25f * tide_cross_f3(forward, tide_f3(1, 0, 0)).y - 0.03f * root.angularVelocity.y;
    tide_world_tick(&assisted);
    tide_world_tick(&unassisted);
    TIDE_REQUIRE(assisted.Newton_Physics.status == Newton_Status_Ok && unassisted.Newton_Physics.status == Newton_Status_Ok);
    const Duck on = tide_get_Duck(&assisted, assisted.DuckFixture.first);
    const Duck off = tide_get_Duck(&unassisted, unassisted.DuckFixture.first);
    TIDE_CHECK(on.assisted && !off.assisted);
    check_vector(on.assistanceForce, tide_f3(0, 0, -1.6f), 2e-6f);
    check_vector(on.assistanceTorque, torque, 2e-6f);
    check_vector(off.assistanceForce, tide_f3(0, 0, 0), 0);
    check_vector(off.assistanceTorque, tide_f3(0, 0, 0), 0);
    const DuckTransition on_record = transition_at(&assisted, on, 0);
    const DuckTransition off_record = transition_at(&unassisted, off, 0);
    check_vector(on_record.assistanceForce, on.assistanceForce, 0);
    check_vector(on_record.assistanceTorque, on.assistanceTorque, 0);
    check_vector(off_record.assistanceForce, tide_f3(0, 0, 0), 0);
    check_vector(off_record.assistanceTorque, tide_f3(0, 0, 0), 0);
    TIDE_CHECK(number_at(&unassisted, off_record.observation, 43) == 1);
    TIDE_CHECK(number_at(&unassisted, off_record.nextObservation, 43) == 0);
    const Newton_Body on_root = tide_get_Newton_Body(&assisted, body_at(&assisted, on, 0));
    const Newton_Body off_root = tide_get_Newton_Body(&unassisted, body_at(&unassisted, off, 0));
    TIDE_CHECK(tide_distance_f3(on_root.angularVelocity, off_root.angularVelocity) > 0.01f);
    TIDE_CHECK(tide_distance_f3(on_root.velocity, off_root.velocity) > 0.001f);
    /* A held button has only one edge; release and press toggles back. */
    tide_world_tick(&unassisted);
    TIDE_CHECK(!tide_get_Duck(&unassisted, unassisted.DuckFixture.first).assisted);
    tide_world_set_input(&unassisted, tide_player_from_index(0), (DuckControls){0});
    tide_world_tick(&unassisted);
    tide_world_set_input(&unassisted, tide_player_from_index(0), (DuckControls){.assistance = true});
    tide_world_tick(&unassisted);
    TIDE_CHECK(tide_get_Duck(&unassisted, unassisted.DuckFixture.first).assisted);
    tide_world_free(&assisted);
    tide_world_free(&unassisted);
}

TIDE_TEST(microduck_sustained_assisted_gait_has_physical_clearance_and_contact_dwell)
{
    static tide_world world;
    tide_world_init(&world, DUCK_DT);
    int airborne[2][2] = {{0}}, touchdown[2][2] = {{0}}, steps[2][2] = {{0}};
    bool lifted[2][2] = {{false}};
    float clearance[2][2] = {{0}}, peak_effort[2] = {0}, minimum_height[2] = {1, 1};
    double load[2] = {0};
    for (int tick = 0; tick < 2000; ++tick) {
        for (uint32_t seat = 0; seat < 2; ++seat)
            tide_world_set_input(&world, tide_player_from_index((int32_t)seat), (DuckControls){.walk = seat == 0 ? 1.0f : -1.0f});
        tide_world_tick(&world);
        TIDE_REQUIRE(world.Newton_Physics.status == Newton_Status_Ok);
        for (int seat = 0; seat < 2; ++seat) {
            const Duck duck = tide_get_Duck(&world, seat == 0 ? world.DuckFixture.first : world.DuckFixture.second);
            const Newton_Body root = tide_get_Newton_Body(&world, body_at(&world, duck, 0));
            TIDE_REQUIRE(isfinite(root.position.x) && isfinite(root.position.y) && isfinite(root.position.z));
            TIDE_REQUIRE(root.position.y > 0.055f && root.position.y < 0.18f);
            TIDE_REQUIRE(tide_rotate_q(original_rotation(&world, duck, 0), tide_f3(0, 0, 1)).y > 0.65f);
            if (root.position.y < minimum_height[seat]) minimum_height[seat] = root.position.y;
            TIDE_CHECK(duck.assistanceForce.x == 0 && duck.assistanceForce.y == 0);
            TIDE_CHECK(fabsf(duck.assistanceForce.z) <= 4);
            const DuckTransition latest = transition_at(&world, duck, (duck.next + 63) % 64);
            check_observation(&world, duck, latest.nextObservation);
            check_vector(latest.assistanceForce, duck.assistanceForce, 0);
            check_vector(latest.assistanceTorque, duck.assistanceTorque, 0);
            for (int i = 0; i < DUCK_JOINTS; ++i) {
                const Newton_Actuator actuator = tide_get_Newton_Actuator(&world, joint_at(&world, duck, i));
                TIDE_CHECK(fabsf(actuator.appliedEffort) <= actuator.effortLimit);
                if (fabsf(actuator.appliedEffort) > peak_effort[seat]) peak_effort[seat] = fabsf(actuator.appliedEffort);
            }
            if (tick < 400) continue;
            load[seat] += duck.footLoad.x + duck.footLoad.y;
            for (int foot = 0; foot < 2; ++foot) {
                const int index = foot == 0 ? 5 : 14;
                const Newton_Body body = tide_get_Newton_Body(&world, body_at(&world, duck, index));
                const tide_quaternion rotation = original_rotation(&world, duck, index);
                float lowest = 1;
                for (int corner = 0; corner < 4; ++corner) {
                    const float height = sole_point(body, rotation, corner).y;
                    if (height < lowest) lowest = height;
                }
                TIDE_REQUIRE(lowest > -0.015f);
                if (lowest > clearance[seat][foot]) clearance[seat][foot] = lowest;
                const float own = foot == 0 ? duck.footLoad.x : duck.footLoad.y;
                const float other = foot == 0 ? duck.footLoad.y : duck.footLoad.x;
                if (lowest > 0.003f && own < 0.05f && other > 1) {
                    if (++airborne[seat][foot] >= 4) lifted[seat][foot] = true;
                } else airborne[seat][foot] = 0;
                if (lifted[seat][foot] && own > 0.5f && lowest < 0.001f) {
                    if (++touchdown[seat][foot] >= 6) {
                        ++steps[seat][foot];
                        lifted[seat][foot] = false;
                        touchdown[seat][foot] = 0;
                    }
                } else touchdown[seat][foot] = 0;
            }
        }
    }
    for (int seat = 0; seat < 2; ++seat) {
        printf("    gait %d: dwell steps %d/%d, clearances %.6f/%.6f m, mean load %.6f N, min height %.6f m, peak effort %.6f Nm\n",
            seat, steps[seat][0], steps[seat][1], (double)clearance[seat][0], (double)clearance[seat][1],
            load[seat] / 1600, (double)minimum_height[seat], (double)peak_effort[seat]);
        TIDE_CHECK(steps[seat][0] >= 3 && steps[seat][1] >= 3);
        TIDE_CHECK(clearance[seat][0] > 0.005f && clearance[seat][1] > 0.005f);
        TIDE_CHECK(load[seat] / 1600 > 6.7 && load[seat] / 1600 < 7.8);
        TIDE_CHECK(peak_effort[seat] > 0.05f);
    }
    tide_world_free(&world);
}

TIDE_TEST(microduck_snapshot_replay_and_alternating_worlds)
{
    static tide_world original, saved, replay, other;
    tide_world_init(&original, DUCK_DT);
    duck_ticks(&original, 260);
    tide_world_copy(&saved, &original);
    duck_ticks(&original, 180);
    const uint64_t expected = tide_world_hash(&original);
    tide_world_init(&other, DUCK_DT);
    tide_world_copy(&replay, &saved);
    for (int i = 0; i < 180; ++i) {
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
    duck_ticks(&world, 710);
    tide_world_init(&fresh, DUCK_DT);
    for (uint32_t seat = 0; seat < 2; ++seat) {
        const tide_entity entity = seat == 0 ? world.DuckFixture.first : world.DuckFixture.second;
        Duck changed = tide_get_Duck(&world, entity);
        changed.assisted = false;
        TIDE_REQUIRE(tide_set_Duck(&world, entity, changed));
        tide_world_set_input(&world, tide_player_from_index((int32_t)seat), (DuckControls){.reset = true});
        tide_world_set_input(&fresh, tide_player_from_index((int32_t)seat), (DuckControls){.reset = true});
    }
    tide_world_tick(&world);
    tide_world_tick(&fresh);
    TIDE_REQUIRE(world.Newton_Physics.status == Newton_Status_Ok);
    for (int seat = 0; seat < 2; ++seat) {
        const Duck reset_duck = tide_get_Duck(&world, seat == 0 ? world.DuckFixture.first : world.DuckFixture.second);
        const Duck fresh_duck = tide_get_Duck(&fresh, seat == 0 ? fresh.DuckFixture.first : fresh.DuckFixture.second);
        TIDE_CHECK(tide_list_read_count(&world.heap, reset_duck.transitions) == 1);
        TIDE_CHECK(reset_duck.phase == 0 && reset_duck.walk == 0 && reset_duck.settleTicks == 1 && reset_duck.assisted);
        const DuckTransition reset_record = transition_at(&world, reset_duck, 0);
        const DuckTransition fresh_record = transition_at(&fresh, fresh_duck, 0);
        for (int i = 0; i < DUCK_OBSERVATIONS; ++i) {
            TIDE_CHECK(number_at(&world, reset_record.observation, i) == number_at(&fresh, fresh_record.observation, i));
            TIDE_CHECK(number_at(&world, reset_record.nextObservation, i) == number_at(&fresh, fresh_record.nextObservation, i));
        }
        for (int i = 0; i < DUCK_JOINTS; ++i) {
            const Newton_Joint a = tide_get_Newton_Joint(&world, joint_at(&world, reset_duck, i));
            const Newton_Joint b = tide_get_Newton_Joint(&fresh, joint_at(&fresh, fresh_duck, i));
            TIDE_CHECK(a.position == b.position && a.velocity == b.velocity);
            TIDE_CHECK(number_at(&world, reset_duck.targets, i) == number_at(&fresh, fresh_duck.targets, i));
            TIDE_CHECK(number_at(&world, reset_duck.trim, i) == 0);
        }
        for (int i = 0; i < DUCK_BODIES; ++i) {
            const Newton_Body a = tide_get_Newton_Body(&world, body_at(&world, reset_duck, i));
            const Newton_Body b = tide_get_Newton_Body(&fresh, body_at(&fresh, fresh_duck, i));
            TIDE_CHECK(memcmp(&a, &b, sizeof(a)) == 0);
        }
    }
    tide_world_free(&world);
    tide_world_free(&fresh);
}

TIDE_TEST(microduck_failed_steps_leave_recorded_transitions_immutable_and_resume)
{
    static tide_world world;
    tide_world_init(&world, DUCK_DT);
    duck_ticks(&world, 300);
    const Duck before = tide_get_Duck(&world, world.DuckFixture.first);
    float records[64][DUCK_OBSERVATIONS * 2 + DUCK_JOINTS * 2 + 7];
    int ticks[64];
    for (int slot = 0; slot < 64; ++slot) {
        const DuckTransition sample = transition_at(&world, before, slot);
        ticks[slot] = sample.tick;
        for (int i = 0; i < DUCK_OBSERVATIONS; ++i) {
            records[slot][i] = number_at(&world, sample.observation, i);
            records[slot][DUCK_OBSERVATIONS + i] = number_at(&world, sample.nextObservation, i);
        }
        for (int i = 0; i < DUCK_JOINTS; ++i) {
            records[slot][2 * DUCK_OBSERVATIONS + i] = number_at(&world, sample.action, i);
            records[slot][2 * DUCK_OBSERVATIONS + DUCK_JOINTS + i] = number_at(&world, sample.effort, i);
        }
        const float tail[7] = {sample.reward, sample.assistanceForce.x, sample.assistanceForce.y, sample.assistanceForce.z,
            sample.assistanceTorque.x, sample.assistanceTorque.y, sample.assistanceTorque.z};
        memcpy(&records[slot][2 * DUCK_OBSERVATIONS + 2 * DUCK_JOINTS], tail, sizeof(tail));
    }
    const tide_entity root_entity = body_at(&world, before, 0);
    Newton_Body root = tide_get_Newton_Body(&world, root_entity);
    const float mass = root.mass;
    root.mass = -1;
    TIDE_REQUIRE(tide_set_Newton_Body(&world, root_entity, root));
    duck_ticks(&world, 3);
    TIDE_REQUIRE(world.Newton_Physics.status == Newton_Status_InvalidBody);
    const Duck failed = tide_get_Duck(&world, world.DuckFixture.first);
    TIDE_CHECK(failed.next == before.next && failed.reward == before.reward);
    TIDE_REQUIRE(tide_list_read_count(&world.heap, failed.transitions) == 64);
    const DuckTransition last_good = transition_at(&world, failed, (failed.next + 63) % 64);
    for (int i = 0; i < DUCK_JOINTS; ++i) {
        const tide_entity entity = joint_at(&world, failed, i);
        const Newton_Joint joint = tide_get_Newton_Joint(&world, entity);
        TIDE_CHECK(joint.position == number_at(&world, last_good.nextObservation, i));
        TIDE_CHECK(joint.velocity == number_at(&world, last_good.nextObservation, i + DUCK_JOINTS));
        TIDE_CHECK(tide_get_Newton_Actuator(&world, entity).appliedEffort == number_at(&world, last_good.effort, i));
    }
    for (int slot = 0; slot < 64; ++slot) {
        const DuckTransition sample = transition_at(&world, failed, slot);
        TIDE_CHECK(sample.tick == ticks[slot]);
        for (int i = 0; i < DUCK_OBSERVATIONS; ++i) {
            TIDE_CHECK(number_at(&world, sample.observation, i) == records[slot][i]);
            TIDE_CHECK(number_at(&world, sample.nextObservation, i) == records[slot][DUCK_OBSERVATIONS + i]);
        }
        for (int i = 0; i < DUCK_JOINTS; ++i) {
            TIDE_CHECK(number_at(&world, sample.action, i) == records[slot][2 * DUCK_OBSERVATIONS + i]);
            TIDE_CHECK(number_at(&world, sample.effort, i) == records[slot][2 * DUCK_OBSERVATIONS + DUCK_JOINTS + i]);
        }
        const float tail[7] = {sample.reward, sample.assistanceForce.x, sample.assistanceForce.y, sample.assistanceForce.z,
            sample.assistanceTorque.x, sample.assistanceTorque.y, sample.assistanceTorque.z};
        TIDE_CHECK(memcmp(&records[slot][2 * DUCK_OBSERVATIONS + 2 * DUCK_JOINTS], tail, sizeof(tail)) == 0);
    }
    root = tide_get_Newton_Body(&world, root_entity);
    root.mass = mass;
    TIDE_REQUIRE(tide_set_Newton_Body(&world, root_entity, root));
    duck_ticks(&world, 1);
    TIDE_REQUIRE(world.Newton_Physics.status == Newton_Status_Ok);
    const Duck resumed = tide_get_Duck(&world, world.DuckFixture.first);
    TIDE_CHECK(resumed.next == (before.next + 1) % 64);
    TIDE_CHECK(transition_at(&world, resumed, before.next).tick == world.Time.tick - 1);
    TIDE_CHECK(transition_at(&world, resumed, (before.next + 63) % 64).tick == world.Time.tick - 5);
    tide_world_free(&world);
}

TIDE_TEST(microduck_canonical_and_delta_transfer_preserve_owned_transition_lists)
{
    static tide_world source, base, packed, delta;
    tide_world_init(&source, DUCK_DT);
    duck_ticks(&source, 300);
    tide_world_copy(&base, &source);
    duck_ticks(&source, 70);
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
    duck_ticks(&source, 120);
    duck_ticks(&packed, 120);
    duck_ticks(&delta, 120);
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
    for (int i = 0; i < 400; ++i) {
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
