// Analytic dry-friction tests of the value ABI; no rows adapter or motor policy.
#include "../runtime.h"
#include "tide_test.h"
#include "tide/math.h"

#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const float friction_no_gravity[3] = {0.0f, 0.0f, 0.0f};
static const float friction_gravity[3] = {0.0f, -9.81f, 0.0f};

static void friction_body(nt_body *body, uint64_t entity, float inertia)
{
    memset(body, 0, sizeof(*body));
    body->entity = entity;
    body->type = NT_DYNAMIC;
    body->mass = 1.0f;
    body->inertia[0] = body->inertia[1] = body->inertia[2] = inertia;
    body->rotation[3] = 1.0f;
}

static void friction_joint(nt_joint *joint, uint64_t entity, uint64_t parent,
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

static int friction_near(float actual, float expected, float tolerance)
{
    if (isfinite(actual) && fabsf(actual - expected) <= tolerance) return 1;
    printf("    actual %.9g, expected %.9g, tolerance %.9g\n",
           (double)actual, (double)expected, (double)tolerance);
    return 0;
}

TIDE_TEST(newton_friction_scalar_static_budget_and_motor_readback)
{
    const float commands[] = {0.006f, -0.006f, 0.00945f, -0.00945f, 0.0f};
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        nt_body body;
        nt_joint joint;
        friction_body(&body, 1, 0.0002f);
        friction_joint(&joint, 10, 0, 1, NT_REVOLUTE);
        joint.armature = 0.0018f;
        joint.friction = 0.00945f;
        joint.damping = 0.005359668f;
        joint.effort = commands[i];
        TIDE_REQUIRE(nt_step(&body, 1, &joint, 1, NULL, 0,
                             friction_no_gravity, 0.005f, 4) == NT_OK);
        TIDE_CHECK(friction_near(joint.velocity, 0.0f, 2.0e-9f));
        TIDE_CHECK(friction_near(joint.position, 0.0f, 2.0e-11f));
        TIDE_CHECK(joint.applied_effort == commands[i]);
        TIDE_CHECK(friction_near(joint.load, 0.0f, 1.0e-8f));
    }
}

TIDE_TEST(newton_friction_scalar_sliding_uses_torque_budget_and_armature)
{
    for (int sign = -1; sign <= 1; sign += 2) {
        nt_body body;
        nt_joint joint;
        friction_body(&body, 1, 0.0002f);
        friction_joint(&joint, 10, 0, 1, NT_REVOLUTE);
        joint.armature = 0.0018f;
        joint.friction = 0.00945f + 0.2668f * 0.04f;
        joint.effort = (float)sign * 0.03f;
        const float h = 0.00125f;
        const float speed = (joint.effort - (float)sign * joint.friction) /
                            (body.inertia[2] + joint.armature) * h;
        TIDE_REQUIRE(nt_step(&body, 1, &joint, 1, NULL, 0,
                             friction_no_gravity, h, 1) == NT_OK);
        TIDE_CHECK(friction_near(joint.velocity, speed, 2.0e-8f));
        TIDE_CHECK(friction_near(joint.position, speed * h, 2.0e-10f));
        TIDE_CHECK(joint.applied_effort == joint.effort);
    }
}

TIDE_TEST(newton_friction_scalar_partial_stop_and_saturated_deceleration)
{
    const float speeds[] = {0.001f, -0.001f, 0.1f, -0.1f};
    for (size_t i = 0; i < sizeof(speeds) / sizeof(speeds[0]); ++i) {
        nt_body body;
        nt_joint joint;
        friction_body(&body, 1, 0.0002f);
        friction_joint(&joint, 10, 0, 1, NT_REVOLUTE);
        joint.armature = 0.0018f;
        joint.friction = 0.00945f;
        joint.velocity = speeds[i];
        const float h = 0.00125f;
        const float decrement = h * joint.friction / (body.inertia[2] + joint.armature);
        const float magnitude = fabsf(speeds[i]) > decrement ? fabsf(speeds[i]) - decrement : 0.0f;
        const float speed = speeds[i] < 0.0f ? -magnitude : magnitude;
        TIDE_REQUIRE(nt_step(&body, 1, &joint, 1, NULL, 0,
                             friction_no_gravity, h, 1) == NT_OK);
        TIDE_CHECK(friction_near(joint.velocity, speed, 2.0e-8f));
        TIDE_CHECK(friction_near(joint.position, speed * h, 2.0e-10f));
        TIDE_CHECK(joint.applied_effort == 0.0f);
    }
}

TIDE_TEST(newton_friction_prismatic_uses_mass_and_normalized_axis)
{
    nt_body body;
    nt_joint joint;
    friction_body(&body, 1, 1.0f);
    body.mass = 2.0f;
    friction_joint(&joint, 10, 0, 1, NT_PRISMATIC);
    joint.axis[0] = 3.0f;
    joint.axis[2] = 0.0f;
    joint.armature = 0.5f;
    joint.friction = 0.4f;
    joint.effort = 1.0f;
    const float dt = 0.01f;
    const float speed = (1.0f - 0.4f) / 2.5f * dt;
    TIDE_REQUIRE(nt_step(&body, 1, &joint, 1, NULL, 0,
                         friction_no_gravity, dt, 1) == NT_OK);
    TIDE_CHECK(friction_near(joint.velocity, speed, 2.0e-8f));
    TIDE_CHECK(friction_near(body.velocity[0], speed, 2.0e-8f));
    TIDE_CHECK(friction_near(body.velocity[1], 0.0f, 2.0e-8f));
}

TIDE_TEST(newton_friction_free_root_conserves_angular_momentum)
{
    nt_body bodies[2];
    nt_joint joint;
    friction_body(&bodies[0], 1, 0.03f);
    friction_body(&bodies[1], 2, 0.01f);
    friction_joint(&joint, 10, 1, 2, NT_REVOLUTE);
    joint.velocity = 0.001f;
    joint.effort = 0.006f;
    joint.armature = 0.002f;
    joint.friction = 0.05f;
    // At the common origin, initial body angular momentum is Ichild*qd.
    // An internal torque may stop the hinge, but cannot stop the whole assembly.
    const float momentum = bodies[1].inertia[2] * joint.velocity;
    const float shared_speed = momentum / (bodies[0].inertia[2] + bodies[1].inertia[2]);
    TIDE_REQUIRE(nt_step(bodies, 2, &joint, 1, NULL, 0,
                         friction_no_gravity, 0.005f, 4) == NT_OK);
    TIDE_CHECK(friction_near(joint.velocity, 0.0f, 2.0e-9f));
    TIDE_CHECK(friction_near(bodies[0].angular_velocity[2], shared_speed, 2.0e-9f));
    TIDE_CHECK(friction_near(bodies[1].angular_velocity[2], shared_speed, 2.0e-9f));
    TIDE_CHECK(friction_near(bodies[0].inertia[2] * bodies[0].angular_velocity[2] +
                             bodies[1].inertia[2] * bodies[1].angular_velocity[2], momentum, 2.0e-10f));
}

static void friction_two_rotors(nt_body bodies[2], nt_joint joints[2])
{
    friction_body(&bodies[0], 1, 2.0f);
    friction_body(&bodies[1], 2, 1.0f);
    friction_joint(&joints[0], 10, 0, 1, NT_REVOLUTE);
    friction_joint(&joints[1], 11, 1, 2, NT_REVOLUTE);
    joints[0].armature = joints[1].armature = 1.0f;
    // Coaxial, coincident, COM-centered rotors: B = [[4,1],[1,2]].
    // The inverse is [[2,-1],[-1,4]]/7, independent of their angles.
}

TIDE_TEST(newton_friction_coupled_joint_uses_full_inverse_response)
{
    for (int first_has_friction = 0; first_has_friction <= 1; ++first_has_friction) {
        nt_body bodies[2];
        nt_joint joints[2];
        friction_two_rotors(bodies, joints);
        joints[0].effort = 1.0f;
        joints[0].friction = first_has_friction ? 0.1f : 0.0f;
        joints[1].friction = 1.0f;
        const float dt = 0.01f;
        // Joint 1 sticks. B00*qdd0=effort0-F0, so its balancing friction
        // is B10*qdd0. Updating only qdd1 would give the wrong qdd0.
        const float speed0 = (1.0f - joints[0].friction) / 4.0f * dt;
        TIDE_REQUIRE(nt_step(bodies, 2, joints, 2, NULL, 0,
                             friction_no_gravity, dt, 1) == NT_OK);
        TIDE_CHECK(friction_near(joints[0].velocity, speed0, 3.0e-8f));
        TIDE_CHECK(friction_near(joints[1].velocity, 0.0f, 3.0e-8f));
        TIDE_CHECK(friction_near(bodies[1].angular_velocity[2], speed0, 3.0e-8f));
    }
}

TIDE_TEST(newton_friction_coupled_static_and_opposed_saturated_budgets)
{
    nt_body bodies[2];
    nt_joint joints[2];
    friction_two_rotors(bodies, joints);
    joints[0].effort = 0.4f;
    joints[1].effort = -0.2f;
    joints[0].friction = joints[1].friction = 0.5f;
    TIDE_REQUIRE(nt_step(bodies, 2, joints, 2, NULL, 0,
                         friction_no_gravity, 0.01f, 1) == NT_OK);
    // Eight fixed sweeps leave a small residual for two simultaneously
    // unsaturated constraints; this well-conditioned case contracts by 1/8.
    TIDE_CHECK(friction_near(joints[0].velocity, 0.0f, 1.0e-8f));
    TIDE_CHECK(friction_near(joints[1].velocity, 0.0f, 1.0e-8f));

    friction_two_rotors(bodies, joints);
    joints[0].effort = 1.0f;
    joints[1].effort = -1.0f;
    joints[0].friction = 0.1f;
    joints[1].friction = 0.2f;
    // B^-1 * [1-.1,-1+.2] = [2.6,-4.1]/7.
    TIDE_REQUIRE(nt_step(bodies, 2, joints, 2, NULL, 0,
                         friction_no_gravity, 0.01f, 1) == NT_OK);
    TIDE_CHECK(friction_near(joints[0].velocity, 2.6f / 7.0f * 0.01f, 3.0e-8f));
    TIDE_CHECK(friction_near(joints[1].velocity, -4.1f / 7.0f * 0.01f, 3.0e-8f));
}

TIDE_TEST(newton_friction_load_excludes_motor_damping_and_friction)
{
    nt_body body;
    nt_joint joint;
    friction_body(&body, 1, 1.0f);
    friction_joint(&joint, 10, 0, 1, NT_REVOLUTE);
    body.torque[2] = -0.2f;
    joint.effort = 0.6f;
    joint.velocity = 0.25f;
    joint.damping = 0.3f;
    joint.friction = 0.05f;
    joint.armature = 0.2f;
    joint.load = 123.0f;
    const float dt = 0.01f;
    const float speed = 0.25f + (0.6f - 0.2f - 0.3f * 0.25f - 0.05f) / 1.2f * dt;
    TIDE_REQUIRE(nt_step(&body, 1, &joint, 1, NULL, 0,
                         friction_no_gravity, dt, 1) == NT_OK);
    TIDE_CHECK(friction_near(joint.velocity, speed, 8.0e-8f));
    TIDE_CHECK(friction_near(joint.load, -0.2f, 8.0e-8f));
    TIDE_CHECK(joint.applied_effort == 0.6f);
}

TIDE_TEST(newton_friction_load_is_last_substep_gravity_bias)
{
    nt_body body;
    nt_joint joint;
    friction_body(&body, 1, 0.6f);
    body.mass = 2.0f;
    body.center_of_mass[0] = 0.5f;
    friction_joint(&joint, 10, 0, 1, NT_REVOLUTE);
    joint.position = 0.3f;
    joint.velocity = 0.2f;
    joint.effort = 0.3f;
    joint.damping = 0.07f;
    joint.armature = 0.2f;
    joint.friction = 0.01f;
    float position = joint.position, velocity = joint.velocity, load = 0.0f;
    const float h = 0.00125f;
    const float inertia = 0.6f + 2.0f * 0.5f * 0.5f + 0.2f;
    for (int i = 0; i < 4; ++i) {
        load = -2.0f * 9.81f * 0.5f * tide_cos_f(position);
        velocity += h * (load + 0.3f - 0.07f * velocity - 0.01f) / inertia;
        TIDE_REQUIRE(velocity > 0.0f); // known sliding branch of the analytic reference
        position += h * velocity;
    }
    TIDE_REQUIRE(nt_step(&body, 1, &joint, 1, NULL, 0,
                         friction_gravity, 0.005f, 4) == NT_OK);
    TIDE_CHECK(friction_near(joint.velocity, velocity, 4.0e-7f));
    TIDE_CHECK(friction_near(joint.position, position, 2.0e-7f));
    TIDE_CHECK(friction_near(joint.load, load, 3.0e-6f));
    TIDE_CHECK(fabsf(joint.load - (-9.81f * tide_cos_f(0.3f))) > 0.0005f);
}

static void friction_contacts(nt_body bodies[2], nt_joint *joint, nt_collider shapes[2])
{
    friction_body(&bodies[0], 1, 1.0f);
    bodies[0].type = NT_STATIC;
    friction_body(&bodies[1], 2, 0.0002f);
    friction_joint(joint, 10, 0, 2, NT_REVOLUTE);
    joint->armature = 0.0018f;
    joint->friction = 0.00945f;
    memset(shapes, 0, sizeof(*shapes) * 2);
    for (int i = 0; i < 2; ++i) {
        shapes[i].entity = (uint64_t)(100 + i);
        shapes[i].body = bodies[i].entity;
        shapes[i].rotation[3] = 1.0f;
        shapes[i].radius = 0.01f;
        shapes[i].stiffness = 10.0f;
    }
    shapes[0].type = NT_PLANE;
    shapes[1].type = NT_SPHERE;
    shapes[1].position[0] = 0.1f;
    shapes[1].position[1] = 0.005f;
}

TIDE_TEST(newton_friction_load_includes_projected_contact_force)
{
    nt_body bodies[2];
    nt_joint joint;
    nt_collider shapes[2];
    friction_contacts(bodies, &joint, shapes);
    TIDE_REQUIRE(nt_step(bodies, 2, &joint, 1, shapes, 2,
                         friction_no_gravity, 0.005f, 4) == NT_OK);
    // 5 mm penetration * stiffness 10 = .05 N; .1 m moment arm = .005 Nm.
    TIDE_CHECK(friction_near(shapes[1].contact_force[1], 0.05f, 1.0e-7f));
    TIDE_CHECK(friction_near(joint.load, 0.005f, 1.0e-8f));
    TIDE_CHECK(friction_near(joint.velocity, 0.0f, 2.0e-9f));
    TIDE_CHECK(joint.applied_effort == 0.0f);
}

TIDE_TEST(newton_friction_prior_load_is_ignored_and_replaced_on_success)
{
    const float stale[] = {NAN, INFINITY, -INFINITY, -FLT_MAX, 456.0f};
    for (size_t i = 0; i < sizeof(stale) / sizeof(stale[0]); ++i) {
        nt_body clean_bodies[2], stale_bodies[2];
        nt_joint clean_joint, stale_joint;
        nt_collider clean_shapes[2], stale_shapes[2];
        friction_contacts(clean_bodies, &clean_joint, clean_shapes);
        memcpy(stale_bodies, clean_bodies, sizeof(stale_bodies));
        memcpy(stale_shapes, clean_shapes, sizeof(stale_shapes));
        memcpy(&stale_joint, &clean_joint, sizeof(stale_joint));
        stale_joint.load = stale[i];
        TIDE_REQUIRE(nt_step(clean_bodies, 2, &clean_joint, 1, clean_shapes, 2,
                             friction_no_gravity, 0.005f, 4) == NT_OK);
        TIDE_REQUIRE(nt_step(stale_bodies, 2, &stale_joint, 1, stale_shapes, 2,
                             friction_no_gravity, 0.005f, 4) == NT_OK);
        TIDE_CHECK(isfinite(stale_joint.load));
        TIDE_CHECK(memcmp(clean_bodies, stale_bodies, sizeof(clean_bodies)) == 0);
        TIDE_CHECK(memcmp(clean_shapes, stale_shapes, sizeof(clean_shapes)) == 0);
        TIDE_CHECK(memcmp(&clean_joint, &stale_joint, sizeof(clean_joint)) == 0);
    }
}

static void friction_expect_atomic(nt_body bodies[2], nt_joint *joint,
                                    nt_collider shapes[2], float dt, int expected)
{
    nt_body before_bodies[2];
    nt_joint before_joint;
    nt_collider before_shapes[2];
    memcpy(before_bodies, bodies, sizeof(before_bodies));
    memcpy(&before_joint, joint, sizeof(before_joint));
    memcpy(before_shapes, shapes, sizeof(before_shapes));
    const int status = nt_step(bodies, 2, joint, 1, shapes, 2, friction_no_gravity, dt, 1);
    if (status != expected) printf("    rejection status %d, expected %d\n", status, expected);
    TIDE_CHECK(status == expected);
    TIDE_CHECK(memcmp(before_bodies, bodies, sizeof(before_bodies)) == 0);
    TIDE_CHECK(memcmp(&before_joint, joint, sizeof(before_joint)) == 0);
    TIDE_CHECK(memcmp(before_shapes, shapes, sizeof(before_shapes)) == 0);
}

TIDE_TEST(newton_friction_malformed_budget_is_atomic)
{
    const float malformed[] = {-1.0f, NAN, INFINITY, -INFINITY};
    for (size_t i = 0; i < sizeof(malformed) / sizeof(malformed[0]); ++i) {
        nt_body bodies[2];
        nt_joint joint;
        nt_collider shapes[2];
        friction_contacts(bodies, &joint, shapes);
        joint.load = NAN;
        joint.applied_effort = 0.125f;
        shapes[1].contact_force[1] = -123.0f;
        joint.friction = malformed[i];
        friction_expect_atomic(bodies, &joint, shapes, 0.005f, NT_INVALID_JOINT);
    }
}

TIDE_TEST(newton_friction_impulse_and_load_overflow_are_atomic)
{
    nt_body bodies[2];
    nt_joint joint;
    nt_collider shapes[2];
    friction_contacts(bodies, &joint, shapes);
    shapes[1].position[1] = 1.0f; // no contact, isolate h*F overflow
    joint.friction = FLT_MAX;
    joint.load = NAN;
    friction_expect_atomic(bodies, &joint, shapes, 2.0f, NT_UNSTABLE);

    friction_contacts(bodies, &joint, shapes);
    shapes[1].position[1] = 1.0f;
    bodies[1].inertia[0] = bodies[1].inertia[1] = bodies[1].inertia[2] = 1.0f;
    joint.axis[0] = joint.axis[1] = joint.axis[2] = 1.0f;
    // Each world-wrench component is finite, but projection onto the unit
    // (1,1,1) axis overflows the generalized nonmotor load.
    for (int axis = 0; axis < 3; ++axis) bodies[1].torque[axis] = 0.8f * FLT_MAX;
    joint.load = -INFINITY;
    friction_expect_atomic(bodies, &joint, shapes, 0.005f, NT_UNSTABLE);

    friction_contacts(bodies, &joint, shapes);
    shapes[1].position[1] = 1.0f;
    bodies[1].inertia[0] = bodies[1].inertia[1] = bodies[1].inertia[2] = 1.0f;
    joint.position = -1.0f;
    joint.velocity = 1.0f;
    joint.limited = 1;
    joint.lower = 0.0f;
    joint.upper = 2.0f;
    joint.limit_stiffness = 0.75f * FLT_MAX;
    joint.limit_damping = 0.0f;
    joint.damping = 0.75f * FLT_MAX;
    bodies[1].torque[2] = 0.75f * FLT_MAX;
    // Limit torque and passive damping cancel in the ordinary force assembly,
    // leaving finite tau=.75*FLT_MAX. Removing passive damping from telemetry
    // restores external+limit=1.5*FLT_MAX: the computed load itself overflows.
    joint.load = NAN;
    friction_expect_atomic(bodies, &joint, shapes, 0.005f, NT_UNSTABLE);
}

typedef struct friction_world {
    nt_body bodies[2];
    nt_joint joints[2];
} friction_world;

static void friction_world_init(friction_world *world)
{
    memset(world, 0, sizeof(*world));
    friction_two_rotors(world->bodies, world->joints);
    world->joints[0].velocity = 0.03f;
    world->joints[1].velocity = -0.02f;
    world->joints[0].damping = 0.03f;
    world->joints[1].damping = 0.02f;
}

static int friction_world_step(friction_world *world, int tick, int variant)
{
    const float sign = variant ? -1.0f : 1.0f;
    for (int i = 0; i < 2; ++i) {
        // Ordinary value state carries previous motor/load, as a BAM controller
        // would. No policy or particular motor fit is being validated here.
        world->joints[i].friction = 0.00945f + 0.2668f * fabsf(world->joints[i].applied_effort) +
                                    0.08078f * fabsf(world->joints[i].load);
        world->joints[i].effort = sign * (float)((tick / (11 + i * 3)) % 3 - 1) * 0.08f;
        world->bodies[i].torque[2] = sign * (float)((tick / (7 + i * 2)) % 3 - 1) * 0.003f;
    }
    return nt_step(world->bodies, 2, world->joints, 2, NULL, 0,
                   friction_no_gravity, 0.005f, 4);
}

static uint64_t friction_hash_float(uint64_t hash, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    for (int i = 0; i < 4; ++i) hash = (hash ^ ((bits >> (i * 8)) & 255u)) * UINT64_C(1099511628211);
    return hash;
}

static uint64_t friction_history_hash(uint64_t hash, const friction_world *world)
{
    // Hash named float fields, excluding native/WASM record padding and sizes.
    for (int i = 0; i < 2; ++i) {
        for (int k = 0; k < 3; ++k) hash = friction_hash_float(hash, world->bodies[i].position[k]);
        for (int k = 0; k < 4; ++k) hash = friction_hash_float(hash, world->bodies[i].rotation[k]);
        for (int k = 0; k < 3; ++k) hash = friction_hash_float(hash, world->bodies[i].velocity[k]);
        for (int k = 0; k < 3; ++k) hash = friction_hash_float(hash, world->bodies[i].angular_velocity[k]);
        hash = friction_hash_float(hash, world->joints[i].position);
        hash = friction_hash_float(hash, world->joints[i].velocity);
        hash = friction_hash_float(hash, world->joints[i].applied_effort);
        hash = friction_hash_float(hash, world->joints[i].load);
    }
    return hash;
}

TIDE_TEST(newton_friction_snapshots_replay_and_interleaved_worlds)
{
    friction_world reference[2], mixed[2], saved, replay;
    uint64_t hashes[2] = {UINT64_C(14695981039346656037), UINT64_C(14695981039346656037)};
    uint64_t mixed_hashes[2] = {UINT64_C(14695981039346656037), UINT64_C(14695981039346656037)};
    for (int variant = 0; variant < 2; ++variant) {
        friction_world_init(&reference[variant]);
        memcpy(&mixed[variant], &reference[variant], sizeof(friction_world));
        for (int tick = 0; tick < 160; ++tick) {
            TIDE_REQUIRE(friction_world_step(&reference[variant], tick, variant) == NT_OK);
            hashes[variant] = friction_history_hash(hashes[variant], &reference[variant]);
            if (variant == 0 && tick == 79) memcpy(&saved, &reference[variant], sizeof(saved));
        }
    }
    for (int tick = 0; tick < 160; ++tick) {
        for (int order = 0; order < 2; ++order) {
            const int variant = (tick + order) & 1;
            TIDE_REQUIRE(friction_world_step(&mixed[variant], tick, variant) == NT_OK);
            mixed_hashes[variant] = friction_history_hash(mixed_hashes[variant], &mixed[variant]);
        }
    }
    TIDE_CHECK(memcmp(reference, mixed, sizeof(reference)) == 0);
    TIDE_CHECK(memcmp(hashes, mixed_hashes, sizeof(hashes)) == 0);
    memcpy(&replay, &saved, sizeof(replay));
    for (int tick = 80; tick < 160; ++tick) TIDE_REQUIRE(friction_world_step(&replay, tick, 0) == NT_OK);
    TIDE_CHECK(memcmp(&replay, &reference[0], sizeof(replay)) == 0);
    printf("    Newton friction history: %016" PRIx64 "\n", hashes[0]);
    // Independently observed on native and WASM before pinning, 2026-10-22.
    TIDE_CHECK(hashes[0] == UINT64_C(0x88d611f84ab7c85a));
}
