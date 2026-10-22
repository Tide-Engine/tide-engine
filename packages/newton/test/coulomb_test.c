#include "../runtime.h"
#include "tide/math.h"
#include "tide_test.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const float coulomb_zero[3] = {0, 0, 0};
static const float coulomb_gravity[3] = {0, -9.81f, 0};

static void coulomb_body(nt_body *body, uint64_t id, int type)
{
    memset(body, 0, sizeof(*body));
    body->entity = id;
    body->type = type;
    body->mass = 2;
    body->inertia[0] = body->inertia[1] = body->inertia[2] = .5f;
    body->rotation[3] = 1;
}

static void coulomb_shape(nt_collider *shape, uint64_t id, uint64_t body, int type)
{
    memset(shape, 0, sizeof(*shape));
    shape->entity = id;
    shape->body = body;
    shape->type = type;
    shape->radius = .5f;
    shape->rotation[3] = 1;
    shape->stiffness = 1000;
    shape->friction = .8f;
    shape->friction_damping = 100;
    shape->friction_model = NT_COULOMB;
}

/* One translational DOF makes the tangent effective mass independently known,
 * and deliberately gives a rank-one tangent matrix. */
static void coulomb_slider(nt_body bodies[2], nt_joint *joint, nt_collider shapes[2])
{
    coulomb_body(&bodies[0], 1, NT_STATIC);
    coulomb_body(&bodies[1], 2, NT_DYNAMIC);
    bodies[1].position[1] = .49f;
    memset(joint, 0, sizeof(*joint));
    joint->entity = 3;
    joint->parent = 1;
    joint->child = 2;
    joint->type = NT_PRISMATIC;
    joint->parent_position[1] = .49f;
    joint->parent_rotation[3] = joint->child_rotation[3] = 1;
    joint->axis[0] = 1;
    joint->effort_limit = 100;
    coulomb_shape(&shapes[0], 10, 1, NT_PLANE);
    coulomb_shape(&shapes[1], 11, 2, NT_SPHERE);
}

static void coulomb_close(float actual, float expected, float tolerance)
{
    if (!(fabsf(actual - expected) <= tolerance))
        printf("    actual %.9g, expected %.9g, tolerance %.9g\n",
               (double)actual, (double)expected, (double)tolerance);
    TIDE_CHECK(fabsf(actual - expected) <= tolerance);
}

TIDE_TEST(newton_coulomb_holds_a_loaded_rank_one_slider)
{
    nt_body bodies[2]; nt_joint joint; nt_collider shapes[2];
    coulomb_slider(bodies, &joint, shapes);
    bodies[1].force[0] = .2f;
    TIDE_REQUIRE(nt_step(bodies, 2, &joint, 1, shapes, 2, coulomb_zero, .001f, 1) == NT_OK);
    coulomb_close(joint.velocity, 0, 1e-7f);
    coulomb_close(joint.position, 0, 1e-9f);
    coulomb_close(shapes[1].contact_force[0], -.2f, 2e-5f);
    coulomb_close(shapes[1].contact_force[1], 10, 3e-5f);
    coulomb_close(shapes[0].contact_force[0], .2f, 2e-5f);
    /* Electrical effort is zero; the external body load and contact reaction
     * cancel in the published nonmotor load. */
    coulomb_close(joint.load, 0, 2e-5f);
    TIDE_CHECK(joint.applied_effort == 0);
}

TIDE_TEST(newton_coulomb_sliding_is_bounded_by_the_normal_impulse)
{
    for (int sign = -1; sign <= 1; sign += 2) {
        nt_body bodies[2]; nt_joint joint; nt_collider shapes[2];
        coulomb_slider(bodies, &joint, shapes);
        joint.velocity = (float)sign;
        TIDE_REQUIRE(nt_step(bodies, 2, &joint, 1, shapes, 2, coulomb_zero, .001f, 1) == NT_OK);
        /* N=10 N, mu=.8: maximum tangent impulse=.008 Ns. */
        coulomb_close(joint.velocity, (float)sign * .996f, 2e-7f);
        coulomb_close(shapes[1].contact_force[0], (float)-sign * 8, 3e-5f);
        coulomb_close(joint.load, (float)-sign * 8, 3e-5f);
        TIDE_CHECK(fabsf(shapes[1].contact_force[0]) <= .80001f * shapes[1].contact_force[1]);
    }
}

TIDE_TEST(newton_coulomb_rotated_plane_and_joint_hold_on_an_incline)
{
    nt_body bodies[2]; nt_joint joint; nt_collider shapes[2];
    coulomb_slider(bodies, &joint, shapes);
    const tide_quaternion rotation = tide_axisangle_q(tide_f3(0, 0, 1), TIDE_PI_F / 6);
    const tide_float3 normal = tide_rotate_q(rotation, tide_f3(0, 1, 0));
    const tide_float3 tangent = tide_rotate_q(rotation, tide_f3(1, 0, 0));
    memcpy(bodies[0].rotation, &rotation, sizeof(rotation));
    joint.parent_position[0] = 0;
    joint.parent_position[1] = .49f;
    for (int i = 0; i < 2; ++i) shapes[i].stiffness = 2000;
    TIDE_REQUIRE(nt_step(bodies, 2, &joint, 1, shapes, 2, coulomb_gravity, .001f, 1) == NT_OK);
    coulomb_close(joint.velocity, 0, 2e-6f);
    const tide_float3 force = tide_f3(shapes[1].contact_force[0], shapes[1].contact_force[1], shapes[1].contact_force[2]);
    /* Float32 rotation/FK roundoff is amplified by 2000 N/m stiffness. */
    coulomb_close(tide_dot_f3(force, normal), 20, .002f);
    coulomb_close(tide_dot_f3(force, tangent), 9.81f, 2e-4f);
}

TIDE_TEST(newton_coulomb_requires_both_shapes_and_zero_material_disables_friction)
{
    nt_body bodies[2], legacy[2]; nt_joint joint, legacy_joint;
    nt_collider shapes[2], legacy_shapes[2];
    coulomb_slider(bodies, &joint, shapes);
    joint.velocity = .1f;
    shapes[0].friction_model = NT_REGULARIZED;
    memcpy(legacy, bodies, sizeof(bodies)); legacy_joint = joint;
    memcpy(legacy_shapes, shapes, sizeof(shapes));
    legacy_shapes[1].friction_model = NT_REGULARIZED;
    TIDE_REQUIRE(nt_step(bodies, 2, &joint, 1, shapes, 2, coulomb_zero, .001f, 1) == NT_OK);
    TIDE_REQUIRE(nt_step(legacy, 2, &legacy_joint, 1, legacy_shapes, 2, coulomb_zero, .001f, 1) == NT_OK);
    TIDE_CHECK(memcmp(bodies, legacy, sizeof(bodies)) == 0);
    TIDE_CHECK(memcmp(&joint, &legacy_joint, sizeof(joint)) == 0);
    TIDE_CHECK(memcmp(shapes[1].contact_force, legacy_shapes[1].contact_force, sizeof(shapes[1].contact_force)) == 0);

    coulomb_slider(bodies, &joint, shapes);
    joint.velocity = 1;
    shapes[1].friction = 0;
    TIDE_REQUIRE(nt_step(bodies, 2, &joint, 1, shapes, 2, coulomb_zero, .001f, 1) == NT_OK);
    coulomb_close(joint.velocity, 1, 1e-7f);
    TIDE_CHECK(shapes[1].contact_force[0] == 0);

    coulomb_slider(bodies, &joint, shapes);
    joint.velocity = 1;
    shapes[0].friction = 4;
    shapes[1].friction = .25f;
    TIDE_REQUIRE(nt_step(bodies, 2, &joint, 1, shapes, 2, coulomb_zero, .001f, 1) == NT_OK);
    coulomb_close(shapes[1].contact_force[0], -10, 4e-5f);
    coulomb_close(joint.velocity, .995f, 2e-7f);
}

static tide_float3 coulomb_com(const nt_body *body)
{
    tide_quaternion rotation; tide_float3 origin, com;
    memcpy(&rotation, body->rotation, sizeof(rotation));
    memcpy(&origin, body->position, sizeof(origin));
    memcpy(&com, body->center_of_mass, sizeof(com));
    return tide_add_f3(origin, tide_rotate_q(rotation, com));
}

static tide_float3 coulomb_momentum(const nt_body bodies[2], bool angular)
{
    tide_float3 sum = tide_f3(0, 0, 0);
    for (int i = 0; i < 2; ++i) {
        const nt_body *b = &bodies[i];
        const tide_float3 v = tide_f3(b->velocity[0], b->velocity[1], b->velocity[2]);
        const tide_float3 linear = tide_mul_f3(v, tide_f3_splat(b->mass));
        if (!angular) sum = tide_add_f3(sum, linear);
        else {
            /* These fixtures use isotropic inertia. */
            const tide_float3 spin = tide_mul_f3(tide_f3(b->angular_velocity[0], b->angular_velocity[1], b->angular_velocity[2]), tide_f3_splat(b->inertia[0]));
            sum = tide_add_f3(sum, tide_add_f3(tide_cross_f3(coulomb_com(b), linear), spin));
        }
    }
    return sum;
}

TIDE_TEST(newton_coulomb_dynamic_pair_preserves_momentum_and_canonical_order)
{
    nt_body bodies[2], reversed[2]; nt_collider shapes[2], reverse_shapes[2];
    for (int i = 0; i < 2; ++i) {
        coulomb_body(&bodies[i], (uint64_t)i + 1, NT_DYNAMIC);
        bodies[i].mass = (float)i + 1;
        bodies[i].inertia[0] = bodies[i].inertia[1] = bodies[i].inertia[2] = .03f;
        bodies[i].position[0] = (float)i * .39f;
        bodies[i].velocity[1] = i ? -.5f : 1;
        coulomb_shape(&shapes[i], (uint64_t)i + 10, bodies[i].entity, NT_SPHERE);
        shapes[i].radius = .2f;
    }
    memcpy(reversed, bodies, sizeof(bodies));
    reverse_shapes[0] = shapes[1]; reverse_shapes[1] = shapes[0];
    const tide_float3 linear = coulomb_momentum(bodies, false), angular = coulomb_momentum(bodies, true);
    TIDE_REQUIRE(nt_step(bodies, 2, NULL, 0, shapes, 2, coulomb_zero, .001f, 1) == NT_OK);
    TIDE_REQUIRE(nt_step(reversed, 2, NULL, 0, reverse_shapes, 2, coulomb_zero, .001f, 1) == NT_OK);
    TIDE_CHECK(memcmp(bodies, reversed, sizeof(bodies)) == 0);
    TIDE_CHECK(memcmp(shapes[0].contact_force, reverse_shapes[1].contact_force, sizeof(shapes[0].contact_force)) == 0);
    TIDE_CHECK(shapes[0].contact_force[1] < 0 && shapes[1].contact_force[1] > 0);
    const tide_float3 new_linear = coulomb_momentum(bodies, false), new_angular = coulomb_momentum(bodies, true);
    TIDE_CHECK(tide_distance_f3(linear, new_linear) < 1e-6f);
    TIDE_CHECK(tide_distance_f3(angular, new_angular) < 2e-6f);
}

TIDE_TEST(newton_coulomb_invalid_mode_is_atomic)
{
    const int invalid[] = {-1, 2, 2147483647};
    for (int i = 0; i < 3; ++i) {
        nt_body bodies[2], old_bodies[2]; nt_joint joint, old_joint;
        nt_collider shapes[2], old_shapes[2];
        coulomb_slider(bodies, &joint, shapes);
        shapes[1].friction_model = invalid[i];
        memcpy(old_bodies, bodies, sizeof(bodies)); old_joint = joint;
        memcpy(old_shapes, shapes, sizeof(shapes));
        TIDE_CHECK(nt_step(bodies, 2, &joint, 1, shapes, 2, coulomb_zero, .001f, 1) == NT_INVALID_COLLIDER);
        TIDE_CHECK(memcmp(bodies, old_bodies, sizeof(bodies)) == 0);
        TIDE_CHECK(memcmp(&joint, &old_joint, sizeof(joint)) == 0);
        TIDE_CHECK(memcmp(shapes, old_shapes, sizeof(shapes)) == 0);
    }
}

TIDE_TEST(newton_coulomb_locked_contact_has_no_tangent_response)
{
    nt_body bodies[2]; nt_joint joint; nt_collider shapes[2];
    coulomb_slider(bodies, &joint, shapes);
    joint.type = NT_FIXED;
    TIDE_REQUIRE(nt_step(bodies, 2, &joint, 1, shapes, 2, coulomb_zero, .001f, 1) == NT_OK);
    TIDE_CHECK(bodies[1].velocity[0] == 0 && bodies[1].angular_velocity[2] == 0);
    TIDE_CHECK(shapes[1].contact_force[0] == 0 && shapes[1].contact_force[2] == 0);
    coulomb_close(shapes[1].contact_force[1], 10, 3e-5f);
}

TIDE_TEST(newton_coulomb_fixed_child_transmits_the_contact_impulse)
{
    nt_body bodies[3]; nt_joint joint; nt_collider shapes[2];
    coulomb_body(&bodies[0], 1, NT_STATIC);
    for (int i = 1; i < 3; ++i) {
        coulomb_body(&bodies[i], (uint64_t)i + 1, NT_DYNAMIC);
        bodies[i].mass = 1;
        bodies[i].inertia[0] = bodies[i].inertia[1] = bodies[i].inertia[2] = .1f;
        bodies[i].position[1] = i == 1 ? .99f : .49f;
        bodies[i].velocity[0] = 1;
    }
    memset(&joint, 0, sizeof(joint));
    joint.entity = 4; joint.parent = 2; joint.child = 3; joint.type = NT_FIXED;
    joint.parent_position[1] = -.5f;
    joint.parent_rotation[3] = joint.child_rotation[3] = 1;
    coulomb_shape(&shapes[0], 10, 1, NT_PLANE);
    coulomb_shape(&shapes[1], 11, 3, NT_SPHERE);
    TIDE_REQUIRE(nt_step(bodies, 3, &joint, 1, shapes, 2, coulomb_zero, .001f, 1) == NT_OK);
    coulomb_close(.5f * (bodies[1].velocity[0] + bodies[2].velocity[0]), .996f, 2e-6f);
    /* Assembly Izz=.325 kg m². Common contact midpoint is .745 m below COM. */
    const float angular = -.745f * .008f / .325f;
    coulomb_close(bodies[1].angular_velocity[2], angular, 2e-6f);
    coulomb_close(bodies[2].angular_velocity[2], angular, 2e-6f);
    coulomb_close(shapes[1].contact_force[0], -8, 4e-5f);
}

TIDE_TEST(newton_coulomb_child_contact_preserves_joint_static_friction)
{
    for (int type = NT_REVOLUTE; type <= NT_PRISMATIC; ++type) {
        for (int sign = -1; sign <= 1; sign += 2) {
            nt_body bodies[3]; nt_joint joint; nt_collider shapes[2];
            coulomb_body(&bodies[0], 1, NT_STATIC);
            for (int i = 1; i < 3; ++i) {
                coulomb_body(&bodies[i], (uint64_t)i + 1, NT_DYNAMIC);
                bodies[i].position[1] = .49f;
                bodies[i].velocity[0] = (float)sign * .1f;
            }
            memset(&joint, 0, sizeof(joint));
            joint.entity = 4; joint.parent = 2; joint.child = 3; joint.type = type;
            joint.parent_rotation[3] = joint.child_rotation[3] = 1;
            joint.axis[type == NT_REVOLUTE ? 2 : 0] = 1;
            joint.effort_limit = 100;
            joint.friction = 10;
            coulomb_shape(&shapes[0], 10, 1, NT_PLANE);
            coulomb_shape(&shapes[1], 11, 3, NT_SPHERE);
            TIDE_REQUIRE(nt_step(bodies, 3, &joint, 1, shapes, 2, coulomb_zero, .001f, 1) == NT_OK);
            /* Only the child contacts the plane. Joint friction must share
             * the .008 Ns tangent impulse with the freely moving parent,
             * holding the relative DOF while the whole assembly responds.
             * The required joint effort is below the 10 N/Nm budget. */
            coulomb_close(joint.velocity, 0, 1e-7f);
            coulomb_close(joint.position, 0, 1e-9f);
            coulomb_close(shapes[1].contact_force[0], (float)-sign * 8, 4e-5f);
            coulomb_close(shapes[0].contact_force[0], (float)sign * 8, 4e-5f);
            coulomb_close(shapes[1].contact_force[1], 10, 3e-5f);
            for (int i = 1; i < 3; ++i) {
                coulomb_close(bodies[i].velocity[0], (float)sign * .098f, 2e-7f);
                /* Coincident COMs: total Izz=1, contact arm=.495 m. */
                coulomb_close(bodies[i].angular_velocity[2], (float)-sign * .00396f, 2e-7f);
            }
            TIDE_CHECK(joint.applied_effort == 0);
        }
    }
}

TIDE_TEST(newton_coulomb_weak_positive_tangent_retains_friction)
{
    for (int sign = -1; sign <= 1; sign += 2) {
        nt_body bodies[2]; nt_collider shapes[2];
        coulomb_body(&bodies[0], 1, NT_STATIC);
        coulomb_body(&bodies[1], 2, NT_DYNAMIC);
        bodies[1].mass = 1e7f;
        bodies[1].inertia[0] = bodies[1].inertia[1] = 1e7f;
        bodies[1].inertia[2] = .245025f;
        /* Keep the massive body's origin at its COM to avoid subtracting
         * large spatial-inertia terms from the small axial inertia. */
        bodies[0].position[1] = -.49f;
        bodies[1].velocity[2] = (float)sign;
        coulomb_shape(&shapes[0], 10, 1, NT_PLANE);
        coulomb_shape(&shapes[1], 11, 2, NT_SPHERE);
        /* Contact arm=(0,-.495,0). In world (x,z), the inverse
         * effective tangent mass is diag(1.0000001,1.245025e-7).
         * Both directions respond: a relative rank cutoff must not
         * discard the weak z direction or its .008 Ns friction budget. */
        TIDE_REQUIRE(nt_step(bodies, 2, NULL, 0, shapes, 2, coulomb_zero, .001f, 1) == NT_OK);
        coulomb_close(shapes[1].contact_force[1], 10, 3e-5f);
        coulomb_close(shapes[1].contact_force[2], (float)-sign * 8, 4e-5f);
        coulomb_close(shapes[0].contact_force[2], (float)sign * 8, 4e-5f);
        coulomb_close(shapes[1].contact_force[0], 0, 1e-7f);
        /* The linear decrement rounds away near 1 m/s in float32;
         * the initially zero angular speed still resolves the impulse. */
        coulomb_close(bodies[1].angular_velocity[0], (float)sign * 3.96e-10f, 2e-15f);
        TIDE_CHECK(fabsf(shapes[1].contact_force[2]) <= .80001f * shapes[1].contact_force[1]);
    }
}

TIDE_TEST(newton_coulomb_near_isotropic_tangent_with_tiny_off_diagonal)
{
    nt_body bodies[2]; nt_collider shapes[2];
    coulomb_body(&bodies[0], 1, NT_STATIC);
    coulomb_body(&bodies[1], 2, NT_DYNAMIC);
    bodies[0].position[1] = -.9f;
    bodies[1].mass = 1;
    bodies[1].inertia[0] = bodies[1].inertia[1] = bodies[1].inertia[2] = 1;
    bodies[1].velocity[0] = 1;
    bodies[1].velocity[2] = .5f;
    coulomb_shape(&shapes[0], 10, 1, NT_PLANE);
    coulomb_shape(&shapes[1], 11, 2, NT_SPHERE);
    shapes[1].radius = 1;
    shapes[1].position[0] = shapes[1].position[2] = 1e-12f;
    /* Contact arm=(1e-12,-.95,1e-12): the world (x,z) tangent
     * matrix has diagonal 1.9025 and off-diagonal -1e-24.
     * Squaring an unscaled eigenvector underflows despite this ordinary,
     * positive effective mass. The step must succeed with bounded friction. */
    TIDE_REQUIRE(nt_step(bodies, 2, NULL, 0, shapes, 2, coulomb_zero, .001f, 1) == NT_OK);
    coulomb_close(shapes[1].contact_force[1], 100, .001f);
    const float tangent = sqrtf(shapes[1].contact_force[0] * shapes[1].contact_force[0]
                              + shapes[1].contact_force[2] * shapes[1].contact_force[2]);
    TIDE_CHECK(tangent > 79.99f && tangent <= .80001f * shapes[1].contact_force[1]);
    TIDE_CHECK(shapes[1].contact_force[0] < 0 && shapes[1].contact_force[2] < 0);
    for (int k = 0; k < 3; ++k) {
        TIDE_CHECK(isfinite(bodies[1].position[k]) && isfinite(bodies[1].velocity[k]));
        TIDE_CHECK(isfinite(bodies[1].angular_velocity[k]) && isfinite(shapes[1].contact_force[k]));
        coulomb_close(shapes[0].contact_force[k], -shapes[1].contact_force[k], 1e-5f);
    }
}

TIDE_TEST(newton_coulomb_contact_and_joint_friction_replay_and_interleaving)
{
    nt_body bodies[2], saved_bodies[2], other_bodies[2];
    nt_joint joint, saved_joint, other_joint;
    nt_collider shapes[2], saved_shapes[2], other_shapes[2];
    coulomb_slider(bodies, &joint, shapes);
    joint.friction = .04f;
    memcpy(saved_bodies, bodies, sizeof(bodies)); saved_joint = joint;
    memcpy(saved_shapes, shapes, sizeof(shapes));
    memcpy(other_bodies, bodies, sizeof(bodies)); other_joint = joint;
    memcpy(other_shapes, shapes, sizeof(shapes)); other_joint.velocity = -.3f;
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (int tick = 0; tick < 128; ++tick) {
        joint.effort = saved_joint.effort = tick < 32 ? .25f : tick < 64 ? 20 : tick < 96 ? -20 : 0;
        TIDE_REQUIRE(nt_step(bodies, 2, &joint, 1, shapes, 2, coulomb_zero, .005f, 4) == NT_OK);
        TIDE_REQUIRE(nt_step(other_bodies, 2, &other_joint, 1, other_shapes, 2, coulomb_zero, .005f, 4) == NT_OK);
        TIDE_REQUIRE(nt_step(saved_bodies, 2, &saved_joint, 1, saved_shapes, 2, coulomb_zero, .005f, 4) == NT_OK);
        TIDE_REQUIRE(memcmp(bodies, saved_bodies, sizeof(bodies)) == 0);
        TIDE_REQUIRE(memcmp(&joint, &saved_joint, sizeof(joint)) == 0);
        TIDE_REQUIRE(memcmp(shapes, saved_shapes, sizeof(shapes)) == 0);
        const float fields[] = {joint.position, joint.velocity, joint.load,
            shapes[1].contact_force[0], shapes[1].contact_force[1], shapes[1].contact_force[2]};
        for (int i = 0; i < 6; ++i) {
            const uint32_t bits = tide_f_bits(fields[i]);
            for (int byte = 0; byte < 4; ++byte)
                hash = (hash ^ ((bits >> (8 * byte)) & 255u)) * UINT64_C(0x100000001b3);
        }
    }
    printf("    Coulomb replay candidate: %016llx\n", (unsigned long long)hash);
    /* Observed matching native/WASI outputs before pinning. */
    TIDE_CHECK(hash == UINT64_C(0xf14a6d9b40ed8fb1));
}

static float coulomb_kinetic_energy(const nt_body *body)
{
    tide_quaternion rotation;
    memcpy(&rotation, body->rotation, sizeof(rotation));
    const tide_float3 angular = tide_rotate_q(tide_inverse_q(rotation),
        tide_f3(body->angular_velocity[0], body->angular_velocity[1], body->angular_velocity[2]));
    float energy = 0;
    for (int i = 0; i < 3; ++i) energy += body->mass * body->velocity[i] * body->velocity[i];
    energy += body->inertia[0] * angular.x * angular.x
            + body->inertia[1] * angular.y * angular.y
            + body->inertia[2] * angular.z * angular.z;
    return .5f * energy;
}

TIDE_TEST(newton_coulomb_compound_friction_does_not_add_kinetic_energy)
{
    for (int trial = 0; trial < 24; ++trial) {
        nt_body bodies[2], frictionless[2]; nt_collider shapes[4], no_friction[4];
        coulomb_body(&bodies[0], 1, NT_STATIC);
        coulomb_body(&bodies[1], 2, NT_DYNAMIC);
        bodies[1].mass = .7f;
        bodies[1].inertia[0] = .01f; bodies[1].inertia[1] = .015f; bodies[1].inertia[2] = .02f;
        bodies[1].position[1] = .49f;
        bodies[1].center_of_mass[0] = .12f; bodies[1].center_of_mass[2] = -.07f;
        bodies[1].velocity[0] = .1f * (float)(trial - 10);
        bodies[1].velocity[2] = .13f * (float)(trial % 7 - 3);
        bodies[1].angular_velocity[0] = .3f * (float)(trial % 5 - 2);
        bodies[1].angular_velocity[1] = -.2f * (float)(trial % 3 - 1);
        bodies[1].angular_velocity[2] = .1f * (float)(trial - 8);
        coulomb_shape(&shapes[0], 10, 1, NT_PLANE);
        for (int i = 1; i < 4; ++i) {
            coulomb_shape(&shapes[i], (uint64_t)i + 10, 2, NT_SPHERE);
            shapes[i].position[0] = .12f * (float)(i - 2);
            shapes[i].position[2] = i == 2 ? .1f : -.1f;
        }
        memcpy(frictionless, bodies, sizeof(bodies));
        memcpy(no_friction, shapes, sizeof(shapes));
        no_friction[0].friction = 0;
        TIDE_REQUIRE(nt_step(bodies, 2, NULL, 0, shapes, 4, coulomb_zero, .001f, 1) == NT_OK);
        TIDE_REQUIRE(nt_step(frictionless, 2, NULL, 0, no_friction, 4, coulomb_zero, .001f, 1) == NT_OK);
        const float with = coulomb_kinetic_energy(&bodies[1]), without = coulomb_kinetic_energy(&frictionless[1]);
        if (with > without + 1e-5f)
            printf("    trial %d: friction energy %g, frictionless %g\n", trial, (double)with, (double)without);
        TIDE_CHECK(with <= without + 1e-5f);
    }
}
