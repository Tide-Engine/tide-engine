#include "../runtime.h"
#include "tide_test.h"

#include <math.h>
#include <string.h>

static void light_sphere(nt_body *body, uint64_t entity)
{
    memset(body, 0, sizeof(*body));
    body->entity = entity;
    body->type = NT_DYNAMIC;
    body->mass = 0.045f;
    body->inertia[0] = body->inertia[1] = body->inertia[2] = 0.000014112f;
    body->rotation[3] = 1.0f;
}

TIDE_TEST(newton_far_free_sphere_preserves_small_rotational_inertia)
{
    nt_body body;
    light_sphere(&body, 1);
    body.position[0] = 64.0f;
    body.position[2] = 128.0f;
    const float velocity[3] = {0.75f, 0.25f, -0.5f};
    const float force[3] = {2.0f, 3.0f, 4.0f};
    const float torque[3] = {0.00001f, -0.00002f, 0.00003f};
    memcpy(body.velocity, velocity, sizeof(velocity));
    memcpy(body.force, force, sizeof(force));
    memcpy(body.torque, torque, sizeof(torque));
    const float gravity[3] = {0.0f, 0.0f, 0.0f};
    const float dt = 0.005f;

    // With a world-origin free-joint frame, J^T I J loses this sphere's
    // rotational inertia when subtracting its much larger translation terms.
    TIDE_REQUIRE(nt_step(&body, 1, NULL, 0, NULL, 0, gravity, dt, 4) == NT_OK);
    for (int axis = 0; axis < 3; ++axis) {
        const float expected_velocity = velocity[axis] + force[axis] * dt / body.mass;
        const float expected_omega = torque[axis] * dt / body.inertia[axis];
        TIDE_CHECK(fabsf(body.velocity[axis] - expected_velocity) < 1.0e-6f);
        TIDE_CHECK(fabsf(body.angular_velocity[axis] - expected_omega) < 1.0e-6f);
        TIDE_CHECK(isfinite(body.position[axis]));
    }
    TIDE_CHECK(body.position[0] > 64.0f && body.position[2] < 128.0f);
}

TIDE_TEST(newton_far_translated_fixed_child_falls_with_its_free_root)
{
    nt_body bodies[2];
    light_sphere(&bodies[0], 1);
    light_sphere(&bodies[1], 2);
    bodies[0].position[0] = 64.0f;
    bodies[0].position[1] = 1.0f;
    bodies[0].position[2] = 128.0f;
    nt_joint joint;
    memset(&joint, 0, sizeof(joint));
    joint.entity = 3;
    joint.parent = 1;
    joint.child = 2;
    joint.type = NT_FIXED;
    joint.parent_rotation[3] = joint.child_rotation[3] = 1.0f;
    joint.parent_position[0] = 0.06f;
    const float gravity[3] = {0.0f, -9.81f, 0.0f};
    const float dt = 0.005f, h = dt / 4.0f;
    TIDE_REQUIRE(nt_step(bodies, 2, &joint, 1, NULL, 0, gravity, dt, 4) == NT_OK);
    for (int body = 0; body < 2; ++body) {
        TIDE_CHECK(fabsf(bodies[body].velocity[1] - gravity[1] * dt) < 1.0e-6f);
        TIDE_CHECK(fabsf(bodies[body].position[1] - (1.0f + gravity[1] * h * h * 10.0f)) < 1.0e-6f);
        for (int axis = 0; axis < 3; ++axis)
            TIDE_CHECK(fabsf(bodies[body].angular_velocity[axis]) < 1.0e-6f);
    }
    TIDE_CHECK(fabsf(bodies[1].position[0] - bodies[0].position[0] - 0.06f) < 4.0e-6f);
}

TIDE_TEST(newton_interaction_launched_ball_floor_snapshot_remains_solvable)
{
    nt_body bodies[2];
    light_sphere(&bodies[0], 1);
    bodies[0].type = NT_STATIC;
    light_sphere(&bodies[1], 2);
    // Exact two-body reduction of a 353-collider MicroDuck interaction:
    // opposing walking ducks and a repeatedly grabbed ball, tick 1734.
    // Baseline fails Cholesky on this independent ball at substep 3.
    bodies[1].inertia[0] = bodies[1].inertia[1] = bodies[1].inertia[2] = 0x1.d9852cp-17f;
    const float position[3] = {-0x1.bded0ap+3f, 0x1.c98510p-6f, 0x1.f945c2p+4f};
    const float rotation[4] = {0x1.d4ab5cp-1f, 0x1.bd88f6p-15f, 0x1.9a93dep-2f, 0x1.2b22bap-5f};
    const float velocity[3] = {-0x1.9838d8p+0f, 0x1p-11f, 0x1.d075acp+1f};
    const float omega[3] = {0x1.039096p+7f, -0x1.468a88p-3f, 0x1.c8063ap+5f};
    memcpy(bodies[1].position, position, sizeof(position));
    memcpy(bodies[1].rotation, rotation, sizeof(rotation));
    memcpy(bodies[1].velocity, velocity, sizeof(velocity));
    memcpy(bodies[1].angular_velocity, omega, sizeof(omega));
    nt_collider shapes[2];
    memset(shapes, 0, sizeof(shapes));
    for (int shape = 0; shape < 2; ++shape) {
        shapes[shape].entity = (uint64_t)shape + 3;
        shapes[shape].body = (uint64_t)shape + 1;
        shapes[shape].type = shape == 0 ? NT_PLANE : NT_SPHERE;
        shapes[shape].rotation[3] = 1.0f;
        shapes[shape].radius = shape == 0 ? 0.05f : 0.028f;
        shapes[shape].stiffness = 5600.0f;
        shapes[shape].damping = 3.0f;
        shapes[shape].friction_damping = 1.0f;
        shapes[shape].friction = 0.8f;
        shapes[shape].friction_model = NT_COULOMB;
    }
    const float gravity[3] = {0.0f, -9.81f, 0.0f};
    TIDE_REQUIRE(nt_step(bodies, 2, NULL, 0, shapes, 2, gravity, 0.005f, 4) == NT_OK);
    TIDE_CHECK(bodies[1].position[0] < position[0]);
    TIDE_CHECK(bodies[1].position[2] > position[2]);
    TIDE_CHECK(bodies[1].position[1] > 0.027f && bodies[1].position[1] < 0.029f);
    TIDE_CHECK(shapes[1].contact_force[1] > 0.0f);
    // Horizontal momentum and rolling speed must survive the solve. The
    // contact impulse is tiny here; accepting a frozen/reset state fails this.
    for (int axis = 0; axis < 3; ++axis) {
        TIDE_CHECK(isfinite(bodies[1].angular_velocity[axis]));
        TIDE_CHECK(fabsf(bodies[1].velocity[axis] - velocity[axis]) < 0.01f);
    }
}
