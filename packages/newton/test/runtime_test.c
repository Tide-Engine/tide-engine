// Standalone tests of the public value ABI, independent of the rows adapter.
// Analytic cases use ordinary mechanics; oracle_expected.h comes from stock
// upstream Python Newton, never from this port's exported kernels.
#include "../runtime.h"
#include "oracle_expected.h"
#include "tide_test.h"
#include "tide/math.h"

#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const float no_gravity[3] = {0.0f, 0.0f, 0.0f};
static const float earth_gravity[3] = {0.0f, -9.81f, 0.0f};

static void body_init(nt_body *body, uint64_t entity)
{
    memset(body, 0, sizeof(*body));
    body->entity = entity;
    body->type = NT_DYNAMIC;
    body->mass = 1.0f;
    body->inertia[0] = body->inertia[1] = body->inertia[2] = 1.0f;
    body->rotation[3] = 1.0f;
}

static void joint_init(nt_joint *joint, uint64_t entity, uint64_t parent,
                       uint64_t child, int32_t type)
{
    memset(joint, 0, sizeof(*joint));
    joint->entity = entity;
    joint->parent = parent;
    joint->child = child;
    joint->type = type;
    joint->effort_limit = 100.0f;
    joint->parent_rotation[3] = joint->child_rotation[3] = 1.0f;
    joint->axis[2] = 1.0f;
}

static int near_value(float actual, float expected, float tolerance)
{
    if (isfinite(actual) && fabsf(actual - expected) <= tolerance) return 1;
    printf("    actual %.9g, expected %.9g, tolerance %.9g\n",
           (double)actual, (double)expected, (double)tolerance);
    return 0;
}

static void check_vector(const float *actual, const float *expected, int count, float tolerance)
{
    for (int i = 0; i < count; ++i) TIDE_CHECK(near_value(actual[i], expected[i], tolerance));
}

static tide_float3 vector_of(const float value[3])
{
    return tide_f3(value[0], value[1], value[2]);
}

static tide_quaternion rotation_of(const float value[4])
{
    return tide_q(value[0], value[1], value[2], value[3]);
}

static void set_rotation(float value[4], tide_quaternion rotation)
{
    memcpy(value, &rotation.value, sizeof(rotation.value));
}

static tide_float3 body_com(const nt_body *body)
{
    return tide_add_f3(vector_of(body->position),
                       tide_rotate_q(rotation_of(body->rotation), vector_of(body->center_of_mass)));
}

static void check_float3(tide_float3 actual, tide_float3 expected, float tolerance)
{
    TIDE_CHECK(near_value(actual.x, expected.x, tolerance));
    TIDE_CHECK(near_value(actual.y, expected.y, tolerance));
    TIDE_CHECK(near_value(actual.z, expected.z, tolerance));
}

static void check_rotation(const float actual[4], tide_quaternion expected, float tolerance)
{
    // q and -q describe the same rotation. Compare the same hemisphere.
    const float dot = actual[0] * expected.value.x + actual[1] * expected.value.y +
                      actual[2] * expected.value.z + actual[3] * expected.value.w;
    const float sign = dot < 0.0f ? -1.0f : 1.0f;
    TIDE_CHECK(near_value(sign * actual[0], expected.value.x, tolerance));
    TIDE_CHECK(near_value(sign * actual[1], expected.value.y, tolerance));
    TIDE_CHECK(near_value(sign * actual[2], expected.value.z, tolerance));
    TIDE_CHECK(near_value(sign * actual[3], expected.value.w, tolerance));
}

TIDE_TEST(newton_free_fall_and_substeps)
{
    nt_body body;
    body_init(&body, UINT64_C(0x1234567800000011));
    const float gravity[3] = {0.0f, -10.0f, 0.0f};
    const float initial_position[3] = {1.0f, 2.0f, 3.0f};
    const float initial_velocity[3] = {0.5f, -0.25f, 0.75f};
    memcpy(body.position, initial_position, sizeof(body.position));
    memcpy(body.velocity, initial_velocity, sizeof(body.velocity));
    const float dt = 0.04f;
    const int substeps = 4;
    const float h = dt / (float)substeps;
    for (int tick = 1; tick <= 6; ++tick) {
        TIDE_REQUIRE(nt_step(&body, 1, NULL, 0, gravity, dt, substeps) == NT_OK);
        const float n = (float)(tick * substeps);
        for (int axis = 0; axis < 3; ++axis) {
            const float velocity = initial_velocity[axis] + gravity[axis] * h * n;
            const float position = initial_position[axis] + initial_velocity[axis] * h * n +
                                   gravity[axis] * h * h * (n * (n + 1.0f) * 0.5f);
            TIDE_CHECK(near_value(body.velocity[axis], velocity, 3.0e-5f));
            TIDE_CHECK(near_value(body.position[axis], position, 3.0e-5f));
            TIDE_CHECK(body.angular_velocity[axis] == 0.0f);
        }
        TIDE_CHECK(body.entity == UINT64_C(0x1234567800000011));
    }
}

TIDE_TEST(newton_free_com_force_torque_and_world_inertia)
{
    nt_body body;
    body_init(&body, 1);
    body.mass = 2.0f;
    body.inertia[0] = 0.4f;
    body.inertia[1] = 0.5f;
    body.inertia[2] = 0.8f;
    body.center_of_mass[0] = 0.75f;
    body.position[0] = 3.0f;
    body.force[1] = 6.0f;
    body.torque[2] = 4.0f;
    TIDE_REQUIRE(nt_step(&body, 1, NULL, 0, no_gravity, 0.01f, 1) == NT_OK);
    TIDE_CHECK(near_value(body.velocity[0], 0.0f, 2.0e-6f));
    TIDE_CHECK(near_value(body.velocity[1], 0.03f, 2.0e-6f));
    TIDE_CHECK(near_value(body.angular_velocity[2], 0.05f, 2.0e-6f));
    // Force acts at COM: no extra moment from the offset COM or world origin.
    check_float3(body_com(&body), tide_f3(3.75f, 0.0003f, 0.0f), 3.0e-6f);
    TIDE_CHECK(body.position[1] < -0.00005f);

    body_init(&body, 2);
    body.inertia[0] = 1.0f;
    body.inertia[1] = 2.0f;
    body.inertia[2] = 3.0f;
    set_rotation(body.rotation, tide_axisangle_q(tide_f3(1.0f, 0.0f, 0.0f), TIDE_PI_F * 0.5f));
    body.torque[1] = 6.0f; // world y corresponds to the body's z inertia, 3.
    TIDE_REQUIRE(nt_step(&body, 1, NULL, 0, no_gravity, 0.01f, 1) == NT_OK);
    const float omega[3] = {0.0f, 0.02f, 0.0f};
    check_vector(body.angular_velocity, omega, 3, 2.0e-6f);
}

TIDE_TEST(newton_static_and_free_root_resets)
{
    nt_body bodies[2];
    body_init(&bodies[0], 1);
    bodies[0].type = NT_STATIC;
    bodies[0].mass = 0.0f;
    memset(bodies[0].inertia, 0, sizeof(bodies[0].inertia));
    bodies[0].position[0] = 8.0f;
    bodies[0].force[1] = 1000.0f;
    bodies[0].torque[2] = 1000.0f;
    body_init(&bodies[1], 2);
    TIDE_REQUIRE(nt_step(bodies, 2, NULL, 0, earth_gravity, 0.01f, 2) == NT_OK);
    TIDE_CHECK(bodies[0].position[0] == 8.0f);
    TIDE_CHECK(bodies[0].position[1] == 0.0f);
    TIDE_CHECK(bodies[0].rotation[3] == 1.0f);
    check_vector(bodies[0].velocity, no_gravity, 3, 0.0f);
    check_vector(bodies[0].angular_velocity, no_gravity, 3, 0.0f);

    // Root pose and both public world velocities are the complete reset state.
    body_init(&bodies[1], 2);
    bodies[1].position[0] = -4.0f;
    bodies[1].velocity[0] = 2.0f;
    bodies[1].angular_velocity[2] = 0.5f;
    TIDE_REQUIRE(nt_step(bodies, 2, NULL, 0, no_gravity, 0.1f, 1) == NT_OK);
    TIDE_CHECK(near_value(bodies[1].position[0], -3.8f, 1.0e-5f));
    TIDE_CHECK(near_value(bodies[1].velocity[0], 2.0f, 1.0e-5f));
    TIDE_CHECK(near_value(bodies[1].angular_velocity[2], 0.5f, 1.0e-5f));
    TIDE_CHECK(bodies[1].rotation[2] > 0.02f);
}

TIDE_TEST(newton_fixed_world_link_local_frames)
{
    nt_body body;
    nt_joint joint;
    body_init(&body, 1);
    body.position[0] = -99.0f;
    body.velocity[1] = 7.0f;
    body.force[1] = 30.0f;
    body.torque[0] = 20.0f;
    joint_init(&joint, 10, 0, 1, NT_FIXED);
    joint.parent_position[0] = 3.0f;
    joint.parent_position[1] = 2.0f;
    joint.parent_position[2] = 1.0f;
    joint.child_position[0] = 1.0f;
    const tide_quaternion parent = tide_axisangle_q(tide_f3(0.0f, 0.0f, 1.0f), TIDE_PI_F * 0.5f);
    const tide_quaternion child = tide_axisangle_q(tide_f3(1.0f, 0.0f, 0.0f), TIDE_PI_F * 0.5f);
    set_rotation(joint.parent_rotation, parent);
    set_rotation(joint.child_rotation, child);
    const tide_quaternion rotation = tide_mul_q(parent, tide_conjugate_q(child));
    const tide_float3 position = tide_sub_f3(vector_of(joint.parent_position),
                                            tide_rotate_q(rotation, vector_of(joint.child_position)));
    for (int i = 0; i < 4; ++i) {
        TIDE_REQUIRE(nt_step(&body, 1, &joint, 1, earth_gravity, 0.02f, 3) == NT_OK);
        check_float3(vector_of(body.position), position, 2.0e-6f);
        check_rotation(body.rotation, rotation, 2.0e-6f);
        check_vector(body.velocity, no_gravity, 3, 2.0e-6f);
        check_vector(body.angular_velocity, no_gravity, 3, 2.0e-6f);
    }
}

TIDE_TEST(newton_revolute_torque_inertia_com_and_armature)
{
    nt_body body;
    nt_joint joint;
    body_init(&body, 1);
    body.mass = 2.0f;
    body.inertia[2] = 0.6f;
    body.center_of_mass[0] = 0.75f;
    joint_init(&joint, 10, 0, 1, NT_REVOLUTE);
    joint.armature = 0.2f;
    joint.position = 0.2f;
    joint.effort = 3.0f;
    const float dt = 0.01f;
    const float inertia = 0.6f + 2.0f * 0.75f * 0.75f + 0.2f;
    const float speed = 3.0f / inertia * dt;
    const float position = 0.2f + speed * dt;
    TIDE_REQUIRE(nt_step(&body, 1, &joint, 1, no_gravity, dt, 1) == NT_OK);
    TIDE_CHECK(near_value(joint.velocity, speed, 2.0e-6f));
    TIDE_CHECK(near_value(joint.position, position, 2.0e-6f));
    TIDE_CHECK(joint.applied_effort == 3.0f);
    check_rotation(body.rotation, tide_axisangle_q(tide_f3(0.0f, 0.0f, 1.0f), position), 2.0e-6f);
    const tide_float3 com = body_com(&body);
    const tide_float3 velocity = tide_cross_f3(tide_f3(0.0f, 0.0f, speed), com);
    check_float3(vector_of(body.velocity), velocity, 3.0e-6f);
}

TIDE_TEST(newton_gravity_pendulum)
{
    nt_body body;
    nt_joint joint;
    body_init(&body, 1);
    body.mass = 2.0f;
    body.inertia[2] = 0.6f;
    body.center_of_mass[0] = 0.75f;
    joint_init(&joint, 10, 0, 1, NT_REVOLUTE);
    joint.armature = 0.2f;
    joint.position = 0.3f;
    const float acceleration = 2.0f * -9.81f * 0.75f * tide_cos_f(0.3f) /
                               (0.6f + 2.0f * 0.75f * 0.75f + 0.2f);
    const float dt = 0.002f;
    TIDE_REQUIRE(nt_step(&body, 1, &joint, 1, earth_gravity, dt, 1) == NT_OK);
    TIDE_CHECK(near_value(joint.velocity, acceleration * dt, 3.0e-6f));
    TIDE_CHECK(near_value(joint.position, 0.3f + acceleration * dt * dt, 3.0e-6f));
    TIDE_CHECK(joint.applied_effort == 0.0f);
}

TIDE_TEST(newton_prismatic_effort_rotated_frame)
{
    nt_body body;
    nt_joint joint;
    body_init(&body, 1);
    body.mass = 3.0f;
    body.center_of_mass[0] = 0.2f;
    joint_init(&joint, 10, 0, 1, NT_PRISMATIC);
    joint.axis[0] = 1.0f;
    joint.axis[2] = 0.0f;
    joint.armature = 0.5f;
    joint.effort = 7.0f;
    joint.position = 0.4f;
    joint.velocity = -0.1f;
    joint.parent_position[0] = 2.0f;
    joint.child_position[0] = 0.25f;
    const tide_quaternion rotation = tide_axisangle_q(tide_f3(0.0f, 0.0f, 1.0f), TIDE_PI_F * 0.5f);
    set_rotation(joint.parent_rotation, rotation);
    const float dt = 0.02f;
    const float speed = -0.1f + 7.0f / 3.5f * dt;
    const float q = 0.4f + speed * dt;
    TIDE_REQUIRE(nt_step(&body, 1, &joint, 1, no_gravity, dt, 1) == NT_OK);
    TIDE_CHECK(near_value(joint.velocity, speed, 2.0e-6f));
    TIDE_CHECK(near_value(joint.position, q, 2.0e-6f));
    check_float3(vector_of(body.position), tide_f3(2.0f, q - 0.25f, 0.0f), 3.0e-6f);
    check_float3(vector_of(body.velocity), tide_f3(0.0f, speed, 0.0f), 3.0e-6f);
    check_vector(body.angular_velocity, no_gravity, 3, 3.0e-6f);
    check_rotation(body.rotation, rotation, 3.0e-6f);
}

TIDE_TEST(newton_joint_effort_clamp)
{
    const float commands[] = {50.0f, -50.0f, 0.75f, -0.5f, 0.0f};
    const float applied[] = {2.0f, -2.0f, 0.75f, -0.5f, 0.0f};
    for (int i = 0; i < 5; ++i) {
        nt_body body;
        nt_joint joint;
        body_init(&body, 1);
        body.mass = 2.0f;
        joint_init(&joint, 10, 0, 1, NT_PRISMATIC);
        joint.effort = commands[i];
        joint.effort_limit = 2.0f;
        joint.applied_effort = 123.0f;
        TIDE_REQUIRE(nt_step(&body, 1, &joint, 1, no_gravity, 0.01f, 1) == NT_OK);
        TIDE_CHECK(joint.applied_effort == applied[i]);
        TIDE_CHECK(near_value(joint.velocity, applied[i] / 2.0f * 0.01f, 2.0e-6f));
        TIDE_CHECK(joint.effort == commands[i]);
    }
    nt_body body;
    nt_joint joint;
    body_init(&body, 1);
    joint_init(&joint, 10, 0, 1, NT_REVOLUTE);
    joint.effort = 8.0f;
    joint.effort_limit = 0.0f;
    TIDE_REQUIRE(nt_step(&body, 1, &joint, 1, no_gravity, 0.01f, 1) == NT_OK);
    TIDE_CHECK(joint.applied_effort == 0.0f && joint.velocity == 0.0f);
}

TIDE_TEST(newton_joint_soft_limits_and_passive_damping)
{
    const float positions[] = {-0.4f, -0.1f, 0.4f, 0.2f};
    const float speeds[] = {0.5f, 0.5f, -0.5f, -0.5f};
    for (int i = 0; i < 4; ++i) {
        nt_body body;
        nt_joint joint;
        body_init(&body, 1);
        body.mass = 2.0f;
        joint_init(&joint, 10, 0, 1, NT_PRISMATIC);
        joint.limited = 1;
        joint.lower = -0.2f;
        joint.upper = 0.2f;
        joint.limit_stiffness = 30.0f;
        joint.limit_damping = 3.0f;
        joint.damping = 2.0f;
        joint.position = positions[i];
        joint.velocity = speeds[i];
        float force = -2.0f * speeds[i];
        if (positions[i] < joint.lower) force += 30.0f * (joint.lower - positions[i]) - 3.0f * speeds[i];
        if (positions[i] > joint.upper) force += 30.0f * (joint.upper - positions[i]) - 3.0f * speeds[i];
        const float speed = speeds[i] + force / 2.0f * 0.01f;
        TIDE_REQUIRE(nt_step(&body, 1, &joint, 1, no_gravity, 0.01f, 1) == NT_OK);
        TIDE_CHECK(near_value(joint.velocity, speed, 3.0e-6f));
        TIDE_CHECK(near_value(joint.position, positions[i] + speed * 0.01f, 3.0e-6f));
        TIDE_CHECK(joint.applied_effort == 0.0f); // excludes passive forces
    }
}

static void two_link_init(nt_body bodies[2], nt_joint joints[2])
{
    body_init(&bodies[0], UINT64_C(0x0000000100000001));
    body_init(&bodies[1], UINT64_C(0x0000000200000002));
    bodies[0].mass = 2.0f;
    bodies[0].center_of_mass[0] = 0.5f;
    bodies[0].inertia[0] = 0.1f;
    bodies[0].inertia[1] = bodies[0].inertia[2] = 0.3f;
    bodies[1].center_of_mass[0] = 0.4f;
    bodies[1].inertia[0] = 0.05f;
    bodies[1].inertia[1] = bodies[1].inertia[2] = 0.12f;
    joint_init(&joints[0], 11, 0, bodies[0].entity, NT_REVOLUTE);
    joint_init(&joints[1], 12, bodies[0].entity, bodies[1].entity, NT_REVOLUTE);
    joints[1].parent_position[0] = 1.0f;
    for (int i = 0; i < 2; ++i) {
        joints[i].effort_limit = 1.0e6f;
        joints[i].lower = -1.0e10f;
        joints[i].upper = 1.0e10f;
        joints[i].position = nt_oracle_initial.q[i];
        joints[i].velocity = nt_oracle_initial.qd[i];
        memcpy(bodies[i].position, nt_oracle_initial.body_position[i], sizeof(bodies[i].position));
        memcpy(bodies[i].rotation, nt_oracle_initial.body_rotation[i], sizeof(bodies[i].rotation));
        memcpy(bodies[i].velocity, nt_oracle_initial.body_linear_velocity[i], sizeof(bodies[i].velocity));
        memcpy(bodies[i].angular_velocity, nt_oracle_initial.body_angular_velocity[i], sizeof(bodies[i].angular_velocity));
    }
    joints[0].effort = 1.25f;
    joints[1].effort = -0.6f;
}

TIDE_TEST(newton_two_link_analytic_coupled_acceleration)
{
    nt_body bodies[2];
    nt_joint joints[2];
    two_link_init(bodies, joints);
    joints[0].position = joints[1].position = 0.0f;
    joints[0].velocity = joints[1].velocity = 0.0f;
    joints[0].armature = 0.15f;
    joints[1].armature = 0.07f;
    // At q=0: H11=I1+m1*c1^2+I2+m2*(L+c2)^2+a1,
    // H12=I2+m2*c2*(L+c2), H22=I2+m2*c2^2+a2.
    // The off-diagonal terms distinguish the articulated solve from two hinges.
    const double h11 = 0.3 + 2.0 * 0.5 * 0.5 + 0.12 + 1.4 * 1.4 + 0.15;
    const double h12 = 0.12 + 0.4 * 1.4;
    const double h22 = 0.12 + 0.4 * 0.4 + 0.07;
    const double determinant = h11 * h22 - h12 * h12;
    const float dt = 0.001f;
    const float speed0 = (float)((h22 * 1.25 - h12 * -0.6) / determinant) * dt;
    const float speed1 = (float)((h11 * -0.6 - h12 * 1.25) / determinant) * dt;
    TIDE_REQUIRE(nt_step(bodies, 2, joints, 2, no_gravity, dt, 1) == NT_OK);
    TIDE_CHECK(near_value(joints[0].velocity, speed0, 3.0e-6f));
    TIDE_CHECK(near_value(joints[1].velocity, speed1, 3.0e-6f));
    TIDE_CHECK(near_value(joints[0].position, speed0 * dt, 3.0e-7f));
    TIDE_CHECK(near_value(joints[1].position, speed1 * dt, 3.0e-7f));
    TIDE_CHECK(near_value(bodies[0].angular_velocity[2], speed0, 3.0e-6f));
    TIDE_CHECK(near_value(bodies[1].angular_velocity[2], speed0 + speed1, 3.0e-6f));
    check_float3(vector_of(bodies[1].position),
                 tide_f3(tide_cos_f(joints[0].position), tide_sin_f(joints[0].position), 0.0f), 2.0e-6f);
}

TIDE_TEST(newton_free_parent_fixed_child_transmits_external_wrench)
{
    nt_body bodies[2];
    nt_joint joint;
    body_init(&bodies[0], 1);
    body_init(&bodies[1], 2);
    joint_init(&joint, 10, 1, 2, NT_FIXED);
    joint.parent_position[0] = 1.0f;
    bodies[1].force[1] = 2.0f;
    // The implicit FREE root and its fixed child form one rigid assembly.
    // Total mass=2, COM=(.5,0,0), Izz=1+1+2*.5^2=2.5.
    // Child force gives a COM force 2 and moment 1: aCOM=1, alpha=.4.
    const float dt = 0.01f;
    TIDE_REQUIRE(nt_step(bodies, 2, &joint, 1, no_gravity, dt, 1) == NT_OK);
    TIDE_CHECK(near_value(bodies[0].angular_velocity[2], 0.4f * dt, 3.0e-6f));
    TIDE_CHECK(near_value(bodies[1].angular_velocity[2], 0.4f * dt, 3.0e-6f));
    TIDE_CHECK(near_value(bodies[0].velocity[1], 0.8f * dt, 3.0e-6f));
    TIDE_CHECK(near_value(bodies[1].velocity[1], 1.2f * dt, 3.0e-6f));
    TIDE_CHECK(near_value(bodies[0].velocity[1] + bodies[1].velocity[1], 2.0f * dt, 3.0e-6f));
    const tide_float3 separation = tide_rotate_q(rotation_of(bodies[0].rotation), tide_f3(1.0f, 0.0f, 0.0f));
    check_float3(tide_sub_f3(vector_of(bodies[1].position), vector_of(bodies[0].position)), separation, 3.0e-6f);
}

TIDE_TEST(newton_two_link_stock_upstream_oracle)
{
    nt_body bodies[2];
    nt_joint joints[2];
    two_link_init(bodies, joints);
    int sample = 0;
    for (int step = 1; step <= nt_oracle_samples[NT_ORACLE_SAMPLE_COUNT - 1].steps; ++step) {
        TIDE_REQUIRE(nt_step(bodies, 2, joints, 2, earth_gravity, NT_ORACLE_DT, 1) == NT_OK);
        const nt_oracle_sample *expected = &nt_oracle_samples[sample];
        if (step != expected->steps) continue;
        for (int i = 0; i < 2; ++i) {
            TIDE_CHECK(near_value(joints[i].position, expected->q[i], NT_ORACLE_TOLERANCE));
            TIDE_CHECK(near_value(joints[i].velocity, expected->qd[i], NT_ORACLE_TOLERANCE));
            check_vector(bodies[i].position, expected->body_position[i], 3, NT_ORACLE_TOLERANCE);
            check_rotation(bodies[i].rotation, rotation_of(expected->body_rotation[i]), NT_ORACLE_TOLERANCE);
            check_float3(body_com(&bodies[i]), vector_of(expected->body_com_position[i]), NT_ORACLE_TOLERANCE);
            check_vector(bodies[i].velocity, expected->body_linear_velocity[i], 3, NT_ORACLE_TOLERANCE);
            check_vector(bodies[i].angular_velocity, expected->body_angular_velocity[i], 3, NT_ORACLE_TOLERANCE);
        }
        ++sample;
    }
    TIDE_CHECK(sample == NT_ORACLE_SAMPLE_COUNT);
}

TIDE_TEST(newton_revolute_static_parent_frames_and_coordinate_reset)
{
    nt_body bodies[2];
    nt_joint joint;
    body_init(&bodies[0], 1);
    body_init(&bodies[1], 2);
    bodies[0].type = NT_STATIC;
    bodies[0].position[0] = 3.0f;
    bodies[0].position[1] = 4.0f;
    const tide_quaternion root = tide_axisangle_q(tide_f3(0.0f, 0.0f, 1.0f), TIDE_PI_F * 0.5f);
    const tide_quaternion parent_frame = tide_axisangle_q(tide_f3(1.0f, 0.0f, 0.0f), TIDE_PI_F * 0.5f);
    const tide_quaternion child_frame = tide_axisangle_q(tide_f3(0.0f, 1.0f, 0.0f), TIDE_PI_F * 0.5f);
    set_rotation(bodies[0].rotation, root);
    joint_init(&joint, 10, 1, 2, NT_REVOLUTE);
    joint.parent_position[0] = 2.0f;
    joint.child_position[1] = 0.5f;
    set_rotation(joint.parent_rotation, parent_frame);
    set_rotation(joint.child_rotation, child_frame);
    joint.axis[1] = 1.0f;
    joint.axis[2] = 0.0f;
    const tide_quaternion world_frame = tide_mul_q(root, parent_frame);
    const tide_float3 anchor = tide_add_f3(vector_of(bodies[0].position),
                                          tide_rotate_q(root, vector_of(joint.parent_position)));
    for (int reset = 0; reset < 2; ++reset) {
        joint.position = reset ? -0.4f : 0.25f;
        joint.velocity = reset ? -0.3f : 0.7f;
        const float speed = joint.velocity;
        const float q = joint.position + speed * 0.002f;
        // Body fields cannot override an explicit joint's coordinate reset.
        bodies[1].position[0] = -100.0f;
        bodies[1].velocity[0] = 77.0f;
        TIDE_REQUIRE(nt_step(bodies, 2, &joint, 1, no_gravity, 0.002f, 1) == NT_OK);
        const tide_quaternion rotation = tide_mul_q(
            tide_mul_q(world_frame, tide_axisangle_q(tide_f3(0.0f, 1.0f, 0.0f), q)),
            tide_conjugate_q(child_frame));
        const tide_float3 position = tide_sub_f3(anchor, tide_rotate_q(rotation, vector_of(joint.child_position)));
        check_float3(vector_of(bodies[1].position), position, 6.0e-6f);
        check_rotation(bodies[1].rotation, rotation, 6.0e-6f);
        const tide_float3 omega = tide_mul_f3(tide_rotate_q(world_frame, tide_f3(0.0f, 1.0f, 0.0f)), tide_f3_splat(speed));
        check_float3(vector_of(bodies[1].angular_velocity), omega, 8.0e-6f);
        check_float3(vector_of(bodies[1].velocity), tide_cross_f3(omega, tide_sub_f3(body_com(&bodies[1]), anchor)), 8.0e-6f);
        TIDE_CHECK(near_value(joint.position, q, 4.0e-6f));
        TIDE_CHECK(near_value(joint.velocity, speed, 4.0e-6f));
    }
}

static void check_body_outputs(const nt_body *actual, const nt_body *expected, float tolerance)
{
    TIDE_CHECK(actual->entity == expected->entity);
    check_vector(actual->position, expected->position, 3, tolerance);
    check_rotation(actual->rotation, rotation_of(expected->rotation), tolerance);
    check_vector(actual->velocity, expected->velocity, 3, tolerance);
    check_vector(actual->angular_velocity, expected->angular_velocity, 3, tolerance);
}

TIDE_TEST(newton_topology_order_multiple_roots_and_generation_identity)
{
    nt_body ordered[5], shuffled[5];
    nt_joint joints[3], shuffled_joints[3];
    two_link_init(ordered, joints);
    body_init(&ordered[2], UINT64_C(0x0000000300000001)); // same index, different generation
    ordered[2].position[0] = -5.0f;
    ordered[2].velocity[0] = 0.2f;
    body_init(&ordered[3], 4);
    ordered[3].type = NT_STATIC;
    ordered[3].position[0] = 5.0f;
    body_init(&ordered[4], 5);
    joint_init(&joints[2], 13, ordered[3].entity, ordered[4].entity, NT_FIXED);
    joints[2].parent_position[1] = 1.0f;
    const int permutation[5] = {4, 1, 2, 3, 0};
    const int joint_permutation[3] = {1, 2, 0}; // child joint precedes its parent's
    for (int i = 0; i < 5; ++i) memcpy(&shuffled[i], &ordered[permutation[i]], sizeof(nt_body));
    for (int i = 0; i < 3; ++i) memcpy(&shuffled_joints[i], &joints[joint_permutation[i]], sizeof(nt_joint));
    for (int tick = 0; tick < 8; ++tick) {
        TIDE_REQUIRE(nt_step(ordered, 5, joints, 3, earth_gravity, NT_ORACLE_DT, 2) == NT_OK);
        TIDE_REQUIRE(nt_step(shuffled, 5, shuffled_joints, 3, earth_gravity, NT_ORACLE_DT, 2) == NT_OK);
        for (int i = 0; i < 5; ++i) check_body_outputs(&shuffled[i], &ordered[permutation[i]], 2.0e-5f);
        for (int i = 0; i < 3; ++i) {
            const nt_joint *expected = &joints[joint_permutation[i]];
            TIDE_CHECK(shuffled_joints[i].entity == expected->entity);
            TIDE_CHECK(near_value(shuffled_joints[i].position, expected->position, 2.0e-5f));
            TIDE_CHECK(near_value(shuffled_joints[i].velocity, expected->velocity, 2.0e-5f));
        }
    }
    TIDE_CHECK(ordered[2].position[0] < -4.9f && ordered[2].position[1] < 0.0f);
    TIDE_CHECK(ordered[4].position[0] == 5.0f && ordered[4].position[1] == 1.0f);
}

// Keep all padding initialized, and memcpy snapshots: byte comparisons test the
// actual ABI records, not a projection that could omit persistent solver state.
typedef struct test_world {
    nt_body bodies[3];
    nt_joint joints[2];
} test_world;

static void world_init(test_world *world, int variant)
{
    memset(world, 0, sizeof(*world));
    two_link_init(world->bodies, world->joints);
    body_init(&world->bodies[2], 3);
    world->bodies[2].mass = 1.5f;
    world->bodies[2].position[0] = -2.0f;
    world->bodies[2].center_of_mass[0] = 0.25f;
    if (variant) {
        world->joints[0].position = -0.6f;
        world->joints[1].velocity = 0.4f;
        world->bodies[2].position[1] = 3.0f;
    }
}

static int world_step(test_world *world, int tick, int variant)
{
    world->joints[0].effort = (float)((tick % 9) - 4) * 0.3f + (float)variant;
    world->joints[1].effort = (float)((tick % 5) - 2) * -0.4f;
    world->bodies[2].force[0] = (float)((tick % 7) - 3) * 0.2f;
    world->bodies[2].torque[2] = (float)((tick % 3) - 1) * 0.1f;
    if (tick == 23) {
        world->joints[1].position = -0.15f;
        world->joints[1].velocity = 0.2f;
        world->bodies[2].velocity[1] = 0.5f;
    }
    return nt_step(world->bodies, 3, world->joints, 2, earth_gravity, 1.0f / 120.0f, 3);
}

TIDE_TEST(newton_value_snapshot_replay_and_alternating_worlds)
{
    test_world reference[2], interleaved[2], snapshot, replay;
    world_init(&reference[0], 0);
    world_init(&reference[1], 1);
    memcpy(interleaved, reference, sizeof(reference));
    memset(&snapshot, 0, sizeof(snapshot));
    for (int variant = 0; variant < 2; ++variant) {
        for (int tick = 0; tick < 64; ++tick) {
            TIDE_REQUIRE(world_step(&reference[variant], tick, variant) == NT_OK);
            if (variant == 0 && tick == 15) memcpy(&snapshot, &reference[0], sizeof(snapshot));
        }
    }
    for (int tick = 0; tick < 64; ++tick) {
        const int first = tick & 1;
        TIDE_REQUIRE(world_step(&interleaved[first], tick, first) == NT_OK);
        TIDE_REQUIRE(world_step(&interleaved[1 - first], tick, 1 - first) == NT_OK);
    }
    TIDE_CHECK(memcmp(reference, interleaved, sizeof(reference)) == 0);
    memcpy(&replay, &snapshot, sizeof(replay));
    for (int tick = 16; tick < 64; ++tick) TIDE_REQUIRE(world_step(&replay, tick, 0) == NT_OK);
    TIDE_CHECK(memcmp(&replay, &reference[0], sizeof(replay)) == 0);
    // Editing a detached snapshot cannot affect either live world.
    snapshot.bodies[2].position[0] = 200.0f;
    TIDE_CHECK(memcmp(reference, interleaved, sizeof(reference)) == 0);
}

static uint64_t hash_float(uint64_t hash, float value)
{
    const uint32_t bits = tide_f_bits(value);
    // Explicit byte order and field selection exclude ABI padding/alignment.
    for (unsigned shift = 0; shift < 32; shift += 8) {
        hash ^= (bits >> shift) & UINT32_C(255);
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

TIDE_TEST(newton_crossplatform_state_bits)
{
    test_world world;
    world_init(&world, 0);
    uint64_t hash = UINT64_C(14695981039346656037);
    for (int tick = 0; tick < 64; ++tick) {
        TIDE_REQUIRE(world_step(&world, tick, 0) == NT_OK);
        for (int i = 0; i < 3; ++i) {
            for (int axis = 0; axis < 3; ++axis) hash = hash_float(hash, world.bodies[i].position[axis]);
            for (int axis = 0; axis < 4; ++axis) hash = hash_float(hash, world.bodies[i].rotation[axis]);
            for (int axis = 0; axis < 3; ++axis) hash = hash_float(hash, world.bodies[i].velocity[axis]);
            for (int axis = 0; axis < 3; ++axis) hash = hash_float(hash, world.bodies[i].angular_velocity[axis]);
        }
        for (int i = 0; i < 2; ++i) {
            hash = hash_float(hash, world.joints[i].position);
            hash = hash_float(hash, world.joints[i].velocity);
            hash = hash_float(hash, world.joints[i].applied_effort);
        }
    }
    // Confirmed with this workload in native debug and WASI debug, 2026-10-22.
    const uint64_t expected = UINT64_C(0x92249536c6268981);
    if (hash != expected) printf("    Newton state hash: %016" PRIx64 "\n", hash);
    TIDE_CHECK(hash == expected);
}

static void expect_rejected(nt_body *bodies, uint32_t body_count, nt_joint *joints,
                            uint32_t joint_count, const float gravity[3], float dt,
                            int32_t substeps, int expected_status)
{
    nt_body before_bodies[3];
    nt_joint before_joints[3];
    // Every caller supplies actual arrays of at most three valid C objects.
    // Count-overflow tests are separate so this helper never performs bad reads.
    TIDE_REQUIRE(body_count <= 3 && joint_count <= 3);
    if (body_count) memcpy(before_bodies, bodies, body_count * sizeof(*bodies));
    if (joint_count) memcpy(before_joints, joints, joint_count * sizeof(*joints));
    const int status = nt_step(bodies, body_count, joints, joint_count, gravity, dt, substeps);
    if (expected_status >= 0) {
        if (status != expected_status) printf("    rejection status %d, expected %d\n", status, expected_status);
        TIDE_CHECK(status == expected_status);
    } else TIDE_CHECK(status != NT_OK);
    if (body_count) TIDE_CHECK(memcmp(before_bodies, bodies, body_count * sizeof(*bodies)) == 0);
    if (joint_count) TIDE_CHECK(memcmp(before_joints, joints, joint_count * sizeof(*joints)) == 0);
}

static void set_float_field(void *record, size_t offset, float value)
{
    memcpy((unsigned char *)record + offset, &value, sizeof(value));
}

TIDE_TEST(newton_invalid_body_values_are_atomic)
{
    // Exercise every input float member without aliasing struct fields as an
    // array or doing pointer arithmetic across different C array objects.
    static const struct { size_t offset; int count; } fields[] = {
        {offsetof(nt_body, mass), 1}, {offsetof(nt_body, inertia), 3},
        {offsetof(nt_body, center_of_mass), 3}, {offsetof(nt_body, position), 3},
        {offsetof(nt_body, rotation), 4}, {offsetof(nt_body, velocity), 3},
        {offsetof(nt_body, angular_velocity), 3}, {offsetof(nt_body, force), 3},
        {offsetof(nt_body, torque), 3}
    };
    const float invalid[] = {NAN, INFINITY, -INFINITY};
    for (size_t field = 0; field < sizeof(fields) / sizeof(fields[0]); ++field) {
        for (int element = 0; element < fields[field].count; ++element) {
            for (int bad = 0; bad < 3; ++bad) {
                nt_body bodies[2];
                body_init(&bodies[0], 1);
                body_init(&bodies[1], 2);
                bodies[0].velocity[0] = 1.0f;
                set_float_field(&bodies[1], fields[field].offset + (size_t)element * sizeof(float), invalid[bad]);
                expect_rejected(bodies, 2, NULL, 0, earth_gravity, 0.01f, 1, NT_INVALID_BODY);
            }
        }
    }
    for (int bad = 0; bad < 8; ++bad) {
        nt_body body;
        body_init(&body, 1);
        if (bad == 0) body.entity = 0;
        if (bad == 1) body.type = -1;
        if (bad == 2) body.type = 2;
        if (bad == 3) body.mass = 0.0f;
        if (bad == 4) body.mass = -1.0f;
        if (bad == 5) body.inertia[1] = 0.0f;
        if (bad == 6) body.inertia[2] = -1.0f;
        if (bad == 7) memset(body.rotation, 0, sizeof(body.rotation));
        expect_rejected(&body, 1, NULL, 0, no_gravity, 0.01f, 1, NT_INVALID_BODY);
    }
}

TIDE_TEST(newton_invalid_joint_values_are_atomic)
{
    static const struct { size_t offset; int count; } fields[] = {
        {offsetof(nt_joint, position), 1}, {offsetof(nt_joint, velocity), 1},
        {offsetof(nt_joint, effort), 1}, {offsetof(nt_joint, effort_limit), 1},
        {offsetof(nt_joint, damping), 1}, {offsetof(nt_joint, armature), 1},
        {offsetof(nt_joint, lower), 1}, {offsetof(nt_joint, upper), 1},
        {offsetof(nt_joint, limit_stiffness), 1}, {offsetof(nt_joint, limit_damping), 1},
        {offsetof(nt_joint, parent_position), 3}, {offsetof(nt_joint, parent_rotation), 4},
        {offsetof(nt_joint, child_position), 3}, {offsetof(nt_joint, child_rotation), 4},
        {offsetof(nt_joint, axis), 3}
    };
    const float invalid[] = {NAN, INFINITY, -INFINITY};
    for (size_t field = 0; field < sizeof(fields) / sizeof(fields[0]); ++field) {
        for (int element = 0; element < fields[field].count; ++element) {
            for (int bad = 0; bad < 3; ++bad) {
                nt_body bodies[2];
                nt_joint joints[2];
                two_link_init(bodies, joints);
                set_float_field(&joints[1], fields[field].offset + (size_t)element * sizeof(float), invalid[bad]);
                expect_rejected(bodies, 2, joints, 2, earth_gravity, 0.01f, 1, NT_INVALID_JOINT);
            }
        }
    }
    for (int bad = 0; bad < 13; ++bad) {
        nt_body bodies[2];
        nt_joint joints[2];
        two_link_init(bodies, joints);
        nt_joint *joint = &joints[1];
        if (bad == 0) joint->entity = 0;
        if (bad == 1) joint->type = -1;
        if (bad == 2) joint->type = 3;
        if (bad == 3) joint->effort_limit = -1.0f;
        if (bad == 4) joint->damping = -1.0f;
        if (bad == 5) joint->armature = -1.0f;
        if (bad == 6) joint->limit_stiffness = -1.0f;
        if (bad == 7) joint->limit_damping = -1.0f;
        if (bad == 8) joint->limited = 2;
        if (bad == 9) { joint->limited = 1; joint->lower = 1.0f; joint->upper = -1.0f; }
        if (bad == 10) memset(joint->axis, 0, sizeof(joint->axis));
        if (bad == 11) memset(joint->parent_rotation, 0, sizeof(joint->parent_rotation));
        if (bad == 12) memset(joint->child_rotation, 0, sizeof(joint->child_rotation));
        expect_rejected(bodies, 2, joints, 2, earth_gravity, 0.01f, 1, NT_INVALID_JOINT);
    }
}

TIDE_TEST(newton_invalid_references_duplicates_cycles_and_inbound_conflicts)
{
    for (int bad = 0; bad < 9; ++bad) {
        nt_body bodies[2];
        nt_joint joints[2];
        two_link_init(bodies, joints);
        if (bad == 0) bodies[1].entity = bodies[0].entity;
        if (bad == 1) joints[1].entity = joints[0].entity;
        if (bad == 2) joints[1].parent = 99;
        if (bad == 3) joints[1].child = 99;
        if (bad == 4) joints[1].child = 0;
        if (bad == 5) joints[1].parent = joints[1].child;
        if (bad == 6) joints[0].parent = bodies[1].entity; // two-body cycle
        if (bad == 7) joints[1].child = joints[0].child; // two inbound joints
        if (bad == 8) bodies[1].type = NT_STATIC; // explicit joint needs dynamic child
        expect_rejected(bodies, 2, joints, 2, earth_gravity, 0.01f, 1, -1);
    }
    nt_body bodies[3];
    nt_joint joints[3];
    for (int i = 0; i < 3; ++i) {
        body_init(&bodies[i], (uint64_t)i + 1);
        joint_init(&joints[i], (uint64_t)i + 10, (uint64_t)((i + 1) % 3) + 1,
                   bodies[i].entity, NT_FIXED);
    }
    expect_rejected(bodies, 3, joints, 3, no_gravity, 0.01f, 1, NT_INVALID_TOPOLOGY);
}

TIDE_TEST(newton_invalid_step_arguments_and_empty_world)
{
    TIDE_CHECK(nt_step(NULL, 0, NULL, 0, no_gravity, 0.01f, 1) == NT_OK);
    nt_body body;
    body_init(&body, 1);
    const float bad_dt[] = {0.0f, -0.01f, NAN, INFINITY, -INFINITY};
    for (int i = 0; i < 5; ++i) expect_rejected(&body, 1, NULL, 0, no_gravity, bad_dt[i], 1, -1);
    expect_rejected(&body, 1, NULL, 0, no_gravity, 0.01f, 0, -1);
    expect_rejected(&body, 1, NULL, 0, no_gravity, 0.01f, -1, -1);
    for (int axis = 0; axis < 3; ++axis) {
        float gravity[3] = {0.0f, 0.0f, 0.0f};
        gravity[axis] = INFINITY;
        expect_rejected(&body, 1, NULL, 0, gravity, 0.01f, 1, -1);
        gravity[axis] = NAN;
        expect_rejected(&body, 1, NULL, 0, gravity, 0.01f, 1, -1);
    }
    nt_body before;
    memcpy(&before, &body, sizeof(body));
    TIDE_CHECK(nt_step(NULL, 1, NULL, 0, no_gravity, 0.01f, 1) != NT_OK);
    TIDE_CHECK(nt_step(&body, 1, NULL, 1, no_gravity, 0.01f, 1) != NT_OK);
    TIDE_CHECK(nt_step(&body, 1, NULL, 0, NULL, 0.01f, 1) != NT_OK);
    TIDE_CHECK(memcmp(&before, &body, sizeof(body)) == 0);
}

TIDE_TEST(newton_nonfinite_result_does_not_partially_publish)
{
    nt_body bodies[2];
    nt_joint joint;
    body_init(&bodies[0], 1);
    body_init(&bodies[1], 2);
    bodies[0].velocity[0] = 1.0f;
    joint_init(&joint, 10, 0, 1, NT_REVOLUTE);
    joint.effort = 2.0f;
    joint.applied_effort = -123.0f;
    bodies[1].force[0] = FLT_MAX;
    expect_rejected(bodies, 2, &joint, 1, no_gravity, 2.0f, 1, NT_UNSTABLE);
}

TIDE_TEST(newton_count_guards_precede_array_access)
{
    nt_body body, before_body;
    nt_joint joint, before_joint;
    body_init(&body, 1);
    joint_init(&joint, 10, 0, 1, NT_REVOLUTE);
    memcpy(&before_body, &body, sizeof(body));
    memcpy(&before_joint, &joint, sizeof(joint));
    // nt_step's public count guard must reject these before reading arrays.
    // Pointers still point at real objects; no invalid pointer is fabricated.
    TIDE_CHECK(nt_step(&body, UINT32_MAX, &joint, 1, no_gravity, 0.01f, 1) == NT_TOO_LARGE);
    TIDE_CHECK(nt_step(&body, 1, &joint, UINT32_MAX, no_gravity, 0.01f, 1) == NT_TOO_LARGE);
    TIDE_CHECK(memcmp(&body, &before_body, sizeof(body)) == 0);
    TIDE_CHECK(memcmp(&joint, &before_joint, sizeof(joint)) == 0);
    expect_rejected(&body, 1, &joint, 1, no_gravity, FLT_TRUE_MIN, 2, NT_UNSTABLE);
}
