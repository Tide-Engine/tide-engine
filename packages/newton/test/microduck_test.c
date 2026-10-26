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
/* Manual-controller world, including the released actor schema and fixed
 * 174-sphere model and centered free-root coordinates. Native/WASI agree. */
#define DUCK_HASH UINT64_C(0x90ed0aaf9bb442c8)

/* These are the original manual-controller/contact regressions. The released
 * actor has its own policy_game_test host; keep its default out of these
 * analytic and assisted-gait cases. */
static void manual_world_init(tide_world *world, float dt)
{
    tide_world_init(world, dt);
    const tide_entity ducks[2] = {world->DuckFixture.first, world->DuckFixture.second};
    for (int i = 0; i < 2; ++i) {
        DuckPolicyState policy = tide_get_DuckPolicyState(world, ducks[i]);
        policy.enabled = false;
        TIDE_REQUIRE(tide_set_DuckPolicyState(world, ducks[i], policy));
    }
}

static tide_entity entity_at(const tide_world *world, tide_list list, int index)
{
    const tide_entity *value = tide_list_read(&world->heap, list, index, sizeof(*value));
    return value ? *value : (tide_entity){0};
}

static tide_entity body_at(const tide_world *world, Duck duck, int index)
{
    return entity_at(world, duck.bodies, index);
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

static tide_entity collider_body(tide_entity entity, Newton_Collider collider)
{
    return tide_entity_is_null(collider.body) ? entity : collider.body;
}

static tide_float3 sphere_center(const tide_world *world, tide_entity entity)
{
    const Newton_Collider collider = tide_get_Newton_Collider(world, entity);
    const Newton_Body body = tide_get_Newton_Body(world, collider_body(entity, collider));
    return tide_add_f3(body.position, tide_rotate_q(body.rotation, collider.position));
}

static float sole_clearance(const tide_world *world, Duck duck, int foot)
{
    float lowest = INFINITY;
    for (int i = 0; i < tide_list_read_count(&world->heap, duck.soles); ++i) {
        const tide_entity entity = entity_at(world, duck.soles, i);
        if (tide_get_DuckSole(world, entity).foot != foot) continue;
        const Newton_Collider collider = tide_get_Newton_Collider(world, entity);
        const float height = sphere_center(world, entity).y - collider.radius;
        if (height < lowest) lowest = height;
    }
    return lowest;
}

static void check_vector(tide_float3 actual, tide_float3 expected, float tolerance)
{
    if (tide_distance_f3(actual, expected) > tolerance)
        printf("    vector actual (%g,%g,%g), expected (%g,%g,%g), tolerance %g\n",
            (double)actual.x, (double)actual.y, (double)actual.z,
            (double)expected.x, (double)expected.y, (double)expected.z, (double)tolerance);
    TIDE_CHECK(tide_distance_f3(actual, expected) <= tolerance);
}

static tide_float3 body_com(Newton_Body body)
{
    return tide_add_f3(body.position, tide_rotate_q(body.rotation, body.centerOfMass));
}

static void disable_ball_colliders(tide_world *world)
{
    for (int i = 0; i < tide_list_read_count(&world->heap, world->DuckSandbox.balls); ++i) {
        const tide_entity entity = entity_at(world, world->DuckSandbox.balls, i);
        Newton_Collider collider = tide_get_Newton_Collider(world, entity);
        collider.enabled = false;
        TIDE_REQUIRE(tide_set_Newton_Collider(world, entity, collider));
    }
}

static void place_duck(tide_world *world, tide_entity entity, tide_float3 origin, tide_quaternion turn)
{
    Duck duck = tide_get_Duck(world, entity);
    const tide_float3 previous = tide_get_Newton_Body(world, body_at(world, duck, 0)).position;
    duck.assisted = false;
    TIDE_REQUIRE(tide_set_Duck(world, entity, duck));
    for (int i = 0; i < DUCK_BODIES; ++i) {
        const tide_entity part = body_at(world, duck, i);
        Newton_Body body = tide_get_Newton_Body(world, part);
        body.position = tide_add_f3(origin, tide_rotate_q(turn, tide_sub_f3(body.position, previous)));
        body.rotation = tide_mul_q(turn, body.rotation);
        body.velocity = body.angularVelocity = tide_f3(0, 0, 0);
        TIDE_REQUIRE(tide_set_Newton_Body(world, part, body));
    }
}

static tide_entity child_collider(const tide_world *world, Duck duck, int child)
{
    const tide_entity body = body_at(world, duck, child);
    for (int i = 0; i < tide_list_read_count(&world->heap, duck.colliders); ++i) {
        const tide_entity entity = entity_at(world, duck.colliders, i);
        if (tide_entity_equal(tide_get_Newton_Collider(world, entity).body, body)) return entity;
    }
    return (tide_entity){0};
}

static DuckControls grab_action(const tide_world *world, tide_entity entity, tide_float3 offset, int serial)
{
    const Newton_Collider collider = tide_get_Newton_Collider(world, entity);
    const tide_entity selected = collider_body(entity, collider);
    const Newton_Body body = tide_get_Newton_Body(world, selected);
    const tide_float3 anchor = tide_add_f3(collider.position, tide_f3(collider.radius, 0, 0));
    const tide_float3 point = tide_add_f3(body.position, tide_rotate_q(body.rotation, anchor));
    return (DuckControls){.grab = true, .grabCollider = entity, .grabAnchor = anchor,
        .grabTarget = tide_add_f3(point, offset),
        .grabRevision = tide_get_SandboxBody(world, selected).revision, .grabSerial = serial};
}

static tide_float3 duck_momentum(const tide_world *world, Duck duck)
{
    tide_float3 result = {0};
    for (int i = 0; i < DUCK_BODIES; ++i) {
        const Newton_Body body = tide_get_Newton_Body(world, body_at(world, duck, i));
        result = tide_add_f3(result, tide_mul_f3(tide_f3_splat(body.mass), body.velocity));
    }
    return result;
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
    manual_world_init(&world, DUCK_DT);
    const tide_quaternion up = tide_axisangle_q(tide_f3(1, 0, 0), -TIDE_PI_F / 2);
    for (int seat = 0; seat < 2; ++seat) {
        const Duck duck = tide_get_Duck(&world, seat == 0 ? world.DuckFixture.first : world.DuckFixture.second);
        TIDE_CHECK(duck.assisted && tide_get_Newton_Collider(&world, world.DuckSandbox.floor).enabled);
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

TIDE_TEST(microduck_preserves_each_authored_mass_com_and_full_inertia_tensor)
{
    static tide_world world;
    manual_world_init(&world, DUCK_DT);
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

TIDE_TEST(microduck_visible_lower_trunk_has_pick_and_prop_collision_coverage)
{
    static tide_world world;
    manual_world_init(&world, DUCK_DT);
    const Duck duck = tide_get_Duck(&world, world.DuckFixture.first);
    const tide_entity root = entity_at(&world, duck.bodies, 0);
    const Newton_Body body = tide_get_Newton_Body(&world, root);
    const DuckPart part = tide_get_DuckPart(&world, root);
    /* An independently chosen point inside the drawn black base casing.
     * This region previously lay 24.33 mm outside every trunk sphere, letting
     * the 22 mm ball and the camera ray pass through the visible casing. */
    const tide_float3 local = tide_rotate_q(tide_inverse_q(part.principalRotation),
        tide_f3(.004f, 0, -.015f));
    const tide_float3 point = tide_add_f3(body.position, tide_rotate_q(body.rotation, local));
    const tide_float3 eye = tide_f3(.42f, .43f, -.95f);
    const tide_float3 direction = tide_normalize_f3(tide_sub_f3(point, eye));
    float clearance = INFINITY;
    bool picked = false;
    for (int i = 0; i < tide_list_read_count(&world.heap, duck.colliders); ++i) {
        const tide_entity entity = entity_at(&world, duck.colliders, i);
        const Newton_Collider collider = tide_get_Newton_Collider(&world, entity);
        if (!tide_entity_equal(collider.body, root)) continue;
        const tide_float3 delta = tide_sub_f3(local, collider.position);
        const float gap = sqrtf(tide_dot_f3(delta, delta)) - collider.radius;
        if (gap < clearance) clearance = gap;
        const tide_float3 center = tide_add_f3(body.position, tide_rotate_q(body.rotation, collider.position));
        const tide_float3 offset = tide_sub_f3(center, eye);
        const float along = tide_dot_f3(offset, direction);
        const float perpendicular_squared = tide_dot_f3(offset, offset) - along * along;
        if (along > 0 && perpendicular_squared <= collider.radius * collider.radius)
            picked = true;
    }
    printf("    visible lower trunk clearance %.6f m, camera ray hit %s\n",
        (double)clearance, picked ? "yes" : "no");
    TIDE_CHECK(clearance <= 0);
    TIDE_CHECK(clearance < .022f);
    TIDE_CHECK(picked);
    tide_world_free(&world);
}

TIDE_TEST(microduck_package_colliders_cover_every_body_and_report_floor_loads)
{
    static tide_world world;
    manual_world_init(&world, DUCK_DT);
    for (int seat = 0; seat < 2; ++seat) {
        const Duck duck = tide_get_Duck(&world, seat ? world.DuckFixture.second : world.DuckFixture.first);
        int coverage[DUCK_BODIES] = {0};
        TIDE_REQUIRE(tide_list_read_count(&world.heap, duck.soles) == 12);
        for (int i = 0; i < tide_list_read_count(&world.heap, duck.colliders); ++i) {
            const tide_entity entity = entity_at(&world, duck.colliders, i);
            const Newton_Collider collider = tide_get_Newton_Collider(&world, entity);
            TIDE_CHECK(collider.enabled && collider.type == Newton_ColliderType_Sphere && collider.radius > 0);
            TIDE_CHECK(collider.exclusionGroup == seat + 1);
            TIDE_CHECK(tide_has_Grabbable(&world, entity));
            bool found = false;
            for (int body = 0; body < DUCK_BODIES; ++body) {
                if (tide_entity_equal(collider.body, body_at(&world, duck, body))) { ++coverage[body]; found = true; }
            }
            TIDE_CHECK(found);
        }
        for (int i = 0; i < DUCK_BODIES; ++i) TIDE_CHECK(coverage[i] > 0);
    }
    for (int tick = 0; tick < 80; ++tick) tide_world_tick(&world);
    TIDE_REQUIRE(world.Newton_Physics.status == Newton_Status_Ok);
    tide_float3 reactions = tide_get_Newton_Collider(&world, world.DuckSandbox.floor).contactForce;
    TIDE_CHECK(reactions.y < -1.0f);
    for (int seat = 0; seat < 2; ++seat) {
        const Duck duck = tide_get_Duck(&world, seat ? world.DuckFixture.second : world.DuckFixture.first);
        float loads[2] = {0};
        for (int i = 0; i < tide_list_read_count(&world.heap, duck.colliders); ++i) {
            const tide_entity entity = entity_at(&world, duck.colliders, i);
            const Newton_Collider collider = tide_get_Newton_Collider(&world, entity);
            reactions = tide_add_f3(reactions, collider.contactForce);
            if (tide_has_DuckSole(&world, entity)) loads[tide_get_DuckSole(&world, entity).foot] += collider.contactForce.y;
        }
        TIDE_CHECK(duck.footLoad.x == loads[0] && duck.footLoad.y == loads[1]);
    }
    for (int i = 0; i < 4; ++i)
        reactions = tide_add_f3(reactions, tide_get_Newton_Collider(&world, entity_at(&world, world.DuckSandbox.balls, i)).contactForce);
    check_vector(reactions, tide_f3(0, 0, 0), 0.001f);
    tide_world_free(&world);
}

TIDE_TEST(microduck_all_four_loose_balls_fall_and_rest_on_the_floor)
{
    static tide_world world;
    manual_world_init(&world, DUCK_DT);
    TIDE_REQUIRE(tide_list_read_count(&world.heap, world.DuckSandbox.balls) == 4);
    tide_entity balls[4];
    float minimum[4] = {1, 1, 1, 1};
    for (int i = 0; i < 4; ++i) {
        balls[i] = entity_at(&world, world.DuckSandbox.balls, i);
        const Newton_Collider collider = tide_get_Newton_Collider(&world, balls[i]);
        const LooseBall ball = tide_get_LooseBall(&world, balls[i]);
        TIDE_CHECK(collider.enabled && collider.type == Newton_ColliderType_Sphere && collider.radius == ball.radius);
        TIDE_CHECK(tide_entity_equal(collider_body(balls[i], collider), balls[i]));
        for (int other = 0; other < i; ++other) TIDE_CHECK(!tide_entity_equal(balls[i], balls[other]));
        Newton_Body body = tide_get_Newton_Body(&world, balls[i]);
        body.position = tide_f3(0.7f + (float)i * 0.15f, 0.12f + (float)i * 0.01f, -0.7f);
        TIDE_REQUIRE(tide_set_Newton_Body(&world, balls[i], body));
    }
    for (int tick = 0; tick < 800; ++tick) {
        tide_world_tick(&world);
        TIDE_REQUIRE(world.Newton_Physics.status == Newton_Status_Ok);
        for (int i = 0; i < 4; ++i) {
            const Newton_Collider collider = tide_get_Newton_Collider(&world, balls[i]);
            const float bottom = sphere_center(&world, balls[i]).y - collider.radius;
            if (bottom < minimum[i]) minimum[i] = bottom;
        }
    }
    for (int i = 0; i < 4; ++i) {
        const Newton_Body body = tide_get_Newton_Body(&world, balls[i]);
        const Newton_Collider collider = tide_get_Newton_Collider(&world, balls[i]);
        printf("    ball %d: minimum %.6f m, settled %.6f m, support %.6f N\n", i,
            (double)minimum[i], (double)(body.position.y - collider.radius), (double)collider.contactForce.y);
        TIDE_CHECK(minimum[i] > -0.02f);
        TIDE_CHECK(fabsf(body.velocity.y) < 0.001f);
        TIDE_CHECK(fabsf(collider.contactForce.y - body.mass * 9.81f) < 0.002f);
        TIDE_CHECK(fabsf(body.position.y - (collider.radius - body.mass * 9.81f / collider.stiffness)) < 0.0001f);
    }
    tide_world_free(&world);
}

TIDE_TEST(microduck_failed_physics_replaces_game_wrenches_without_accumulating_grabs)
{
    static tide_world world;
    manual_world_init(&world, DUCK_DT);
    Duck duck = tide_get_Duck(&world, world.DuckFixture.first);
    duck.assisted = false;
    TIDE_REQUIRE(tide_set_Duck(&world, world.DuckFixture.first, duck));
    const tide_entity root_entity = body_at(&world, duck, 0);
    Newton_Body root = tide_get_Newton_Body(&world, root_entity);
    const float mass = root.mass;
    root.mass = -1;
    root.force = tide_f3(100, 200, 300);
    root.torque = tide_f3(400, 500, 600);
    TIDE_REQUIRE(tide_set_Newton_Body(&world, root_entity, root));
    const tide_entity ball = entity_at(&world, world.DuckSandbox.balls, 0);
    Newton_Body body = tide_get_Newton_Body(&world, ball);
    body.position = tide_f3(0, 1, 0);
    body.force = tide_f3(100, 200, 300);
    body.torque = tide_f3(400, 500, 600);
    TIDE_REQUIRE(tide_set_Newton_Body(&world, ball, body));
    Newton_Collider collider = tide_get_Newton_Collider(&world, ball);
    collider.contactForce = tide_f3(1, 2, 3);
    TIDE_REQUIRE(tide_set_Newton_Collider(&world, ball, collider));
    for (int repeat = 0; repeat < 3; ++repeat) {
        tide_world_set_input(&world, tide_player_from_index(0), grab_action(&world, ball, tide_f3(0, 0.02f, 0), repeat + 1));
        tide_world_tick(&world);
        TIDE_REQUIRE(world.Newton_Physics.status == Newton_Status_InvalidBody);
        const DuckGrab grab = tide_get_DuckGrab(&world, world.DuckFixture.first);
        TIDE_CHECK(tide_entity_equal(grab.body, ball));
        check_vector(grab.force, tide_f3(0, 0.7f, 0), 2e-6f);
        body = tide_get_Newton_Body(&world, ball);
        check_vector(body.force, grab.force, 0);
        check_vector(body.torque, grab.torque, 0);
        check_vector(tide_get_Newton_Collider(&world, ball).contactForce, tide_f3(1, 2, 3), 0);
        check_vector(tide_get_Newton_Body(&world, root_entity).force, tide_f3(0, 0, 0), 0);
        check_vector(tide_get_Newton_Body(&world, root_entity).torque, tide_f3(0, 0, 0), 0);
        TIDE_CHECK(tide_list_read_count(&world.heap, tide_get_Duck(&world, world.DuckFixture.first).transitions) == 0);
    }
    tide_world_set_input(&world, tide_player_from_index(0), (DuckControls){0});
    tide_world_tick(&world);
    check_vector(tide_get_Newton_Body(&world, ball).force, tide_f3(0, 0, 0), 0);
    root = tide_get_Newton_Body(&world, root_entity);
    root.mass = mass;
    TIDE_REQUIRE(tide_set_Newton_Body(&world, root_entity, root));
    tide_world_tick(&world);
    TIDE_REQUIRE(world.Newton_Physics.status == Newton_Status_Ok);
    check_vector(tide_get_Newton_Collider(&world, ball).contactForce, tide_f3(0, 0, 0), 0);
    tide_world_free(&world);
}

TIDE_TEST(microduck_full_body_topples_onto_package_floor_without_assistance)
{
    static tide_world world;
    manual_world_init(&world, DUCK_DT);
    disable_ball_colliders(&world);
    place_duck(&world, world.DuckFixture.first, tide_f3(-0.6f, 0.35f, -0.4f),
        tide_axisangle_q(tide_f3(0, 0, 1), TIDE_PI_F * 0.5f));
    const Duck duck = tide_get_Duck(&world, world.DuckFixture.first);
    bool nonsole_contact = false;
    float minimum = 1.0f, final_minimum = 1.0f;
    for (int tick = 0; tick < 400; ++tick) {
        tide_world_tick(&world);
        TIDE_REQUIRE(world.Newton_Physics.status == Newton_Status_Ok);
        for (int i = 0; i < tide_list_read_count(&world.heap, duck.colliders); ++i) {
            const tide_entity entity = entity_at(&world, duck.colliders, i);
            const Newton_Collider collider = tide_get_Newton_Collider(&world, entity);
            const float bottom = sphere_center(&world, entity).y - collider.radius;
            TIDE_REQUIRE(isfinite(bottom));
            if (bottom < minimum) minimum = bottom;
            if (tick >= 340 && bottom < final_minimum) final_minimum = bottom;
            if (!tide_has_DuckSole(&world, entity) && collider.contactForce.y > 0.05f) nonsole_contact = true;
        }
    }
    const Newton_Body root = tide_get_Newton_Body(&world, body_at(&world, duck, 0));
    printf("    toppled duck: minimum sphere bottom %.6f m, settled %.6f m, root speed %.6f m/s\n",
        (double)minimum, (double)final_minimum, (double)tide_length_f3(root.velocity));
    TIDE_CHECK(nonsole_contact);
    TIDE_CHECK(minimum > -0.02f && final_minimum > -0.01f);
    TIDE_CHECK(tide_length_f3(root.velocity) < 0.25f);
    TIDE_CHECK(!tide_get_Duck(&world, world.DuckFixture.first).assisted);
    tide_world_free(&world);
}

TIDE_TEST(microduck_loose_ball_transfers_contact_momentum_to_articulation)
{
    static tide_world world, without;
    manual_world_init(&world, DUCK_DT);
    place_duck(&world, world.DuckFixture.first, tide_f3(-0.4f, 0.6f, -0.4f), tide_identity_q());
    disable_ball_colliders(&world);
    Newton_Collider floor = tide_get_Newton_Collider(&world, world.DuckSandbox.floor);
    floor.enabled = false;
    TIDE_REQUIRE(tide_set_Newton_Collider(&world, world.DuckSandbox.floor, floor));
    const Duck duck = tide_get_Duck(&world, world.DuckFixture.first);
    const tide_entity target = child_collider(&world, duck, 0);
    const Newton_Collider proxy = tide_get_Newton_Collider(&world, target);
    // Isolate one surface witness rather than starting the ball deeply inside
    // several overlapping trunk proxies. Full compound coverage is separate.
    for (int i = 0; i < tide_list_read_count(&world.heap, duck.colliders); ++i) {
        const tide_entity entity = entity_at(&world, duck.colliders, i);
        Newton_Collider shape = tide_get_Newton_Collider(&world, entity);
        shape.enabled = tide_entity_equal(entity, target);
        TIDE_REQUIRE(tide_set_Newton_Collider(&world, entity, shape));
    }
    const tide_entity ball = entity_at(&world, world.DuckSandbox.balls, 0);
    Newton_Collider collider = tide_get_Newton_Collider(&world, ball);
    collider.enabled = true;
    TIDE_REQUIRE(tide_set_Newton_Collider(&world, ball, collider));
    Newton_Body body = tide_get_Newton_Body(&world, ball);
    body.position = tide_add_f3(sphere_center(&world, target), tide_f3(proxy.radius + collider.radius - 0.001f, 0, 0));
    body.velocity = tide_f3(-0.2f, 0, 0);
    TIDE_REQUIRE(tide_set_Newton_Body(&world, ball, body));
    tide_world_copy(&without, &world);
    collider.enabled = false;
    TIDE_REQUIRE(tide_set_Newton_Collider(&without, ball, collider));
    tide_world_tick(&world);
    tide_world_tick(&without);
    TIDE_REQUIRE(world.Newton_Physics.status == Newton_Status_Ok && without.Newton_Physics.status == Newton_Status_Ok);
    const tide_float3 reaction = tide_get_Newton_Collider(&world, ball).contactForce;
    TIDE_CHECK(tide_length_f3(reaction) > 0.1f);
    const tide_float3 difference = tide_sub_f3(duck_momentum(&world, duck), duck_momentum(&without, tide_get_Duck(&without, without.DuckFixture.first)));
    check_vector(difference, tide_mul_f3(tide_f3_splat(-DUCK_DT), reaction), 5e-5f);
    const Newton_Body hit = tide_get_Newton_Body(&world, ball);
    const Newton_Body missed = tide_get_Newton_Body(&without, ball);
    check_vector(tide_mul_f3(tide_f3_splat(body.mass), tide_sub_f3(hit.velocity, missed.velocity)),
        tide_mul_f3(tide_f3_splat(DUCK_DT), reaction), 5e-6f);
    tide_world_free(&world);
    tide_world_free(&without);
}

TIDE_TEST(microduck_input_grab_applies_com_wrench_without_teleporting)
{
    static tide_world world, twice;
    manual_world_init(&world, DUCK_DT);
    const tide_entity ball = entity_at(&world, world.DuckSandbox.balls, 0);
    Newton_Body before = tide_get_Newton_Body(&world, ball);
    before.position = tide_f3(0, 1, 0);
    before.rotation = tide_axisangle_q(tide_f3(0, 0, 1), TIDE_PI_F * 0.5f);
    before.centerOfMass = tide_f3(0.005f, 0, 0);
    TIDE_REQUIRE(tide_set_Newton_Body(&world, ball, before));
    const DuckControls request = grab_action(&world, ball, tide_f3(0.004f, 0.002f, 0), 1);
    tide_world_copy(&twice, &world);
    tide_world_set_input(&world, tide_player_from_index(0), request);
    tide_world_set_input(&twice, tide_player_from_index(0), request);
    tide_world_set_input(&twice, tide_player_from_index(1), request);
    tide_world_tick(&world);
    tide_world_tick(&twice);
    TIDE_REQUIRE(world.Newton_Physics.status == Newton_Status_Ok && twice.Newton_Physics.status == Newton_Status_Ok);
    const DuckGrab grab = tide_get_DuckGrab(&world, world.DuckFixture.first);
    TIDE_CHECK(tide_entity_equal(grab.body, ball) && grab.requested);
    check_vector(grab.force, tide_f3(0.14f, 0.07f, 0), 3e-6f);
    const tide_float3 point = tide_add_f3(before.position, tide_rotate_q(before.rotation, request.grabAnchor));
    check_vector(grab.torque, tide_cross_f3(tide_sub_f3(point, body_com(before)), grab.force), 2e-7f);
    const Newton_Body after = tide_get_Newton_Body(&world, ball);
    const tide_float3 acceleration = tide_add_f3(tide_mul_f3(tide_f3_splat(1.0f / before.mass), grab.force), world.Newton_Physics.gravity);
    check_vector(after.velocity, tide_mul_f3(tide_f3_splat(DUCK_DT), acceleration), 2e-6f);
    check_vector(body_com(after), tide_add_f3(body_com(before),
        tide_mul_f3(tide_f3_splat(DUCK_DT * DUCK_DT * 0.625f), acceleration)), 2e-6f);
    TIDE_CHECK(tide_distance_f3(after.position, request.grabTarget) > 0.02f);
    printf("    grabbed ball omega.z %.9g, constant COM torque expectation %.9g\n",
        (double)after.angularVelocity.z, (double)(grab.torque.z / before.inertia.z * DUCK_DT));
    TIDE_CHECK(fabsf(after.angularVelocity.z - grab.torque.z / before.inertia.z * DUCK_DT) < 2e-4f);
    const DuckGrab first = tide_get_DuckGrab(&twice, twice.DuckFixture.first);
    const DuckGrab second = tide_get_DuckGrab(&twice, twice.DuckFixture.second);
    check_vector(first.force, grab.force, 0);
    check_vector(second.force, grab.force, 0);
    const Newton_Body combined = tide_get_Newton_Body(&twice, ball);
    check_vector(tide_sub_f3(combined.velocity, after.velocity),
        tide_mul_f3(tide_f3_splat(DUCK_DT / before.mass), grab.force), 2e-6f);
    const Duck duck = tide_get_Duck(&world, world.DuckFixture.first);
    const DuckTransition sample = transition_at(&world, duck, 0);
    TIDE_CHECK(sample.grabRequested && tide_entity_equal(sample.grabBody, ball));
    check_vector(sample.grabForce, grab.force, 0);
    check_vector(sample.grabTorque, grab.torque, 0);
    check_vector(sample.grabAnchor, request.grabAnchor, 0);
    check_vector(sample.grabTarget, request.grabTarget, 0);
    tide_world_free(&world);
    tide_world_free(&twice);
}

TIDE_TEST(microduck_input_grab_child_couples_to_whole_articulation)
{
    static tide_world world, without;
    manual_world_init(&world, DUCK_DT);
    place_duck(&world, world.DuckFixture.first, tide_f3(-0.4f, 0.7f, -0.4f), tide_identity_q());
    const Duck duck = tide_get_Duck(&world, world.DuckFixture.first);
    const tide_entity collider = child_collider(&world, duck, 6);
    TIDE_REQUIRE(!tide_entity_is_null(collider));
    const DuckControls request = grab_action(&world, collider, tide_f3(0.004f, 0.008f, 0), 1);
    const Newton_Body before = tide_get_Newton_Body(&world, collider_body(collider, tide_get_Newton_Collider(&world, collider)));
    tide_world_copy(&without, &world);
    tide_world_set_input(&world, tide_player_from_index(0), request);
    tide_world_tick(&world);
    tide_world_tick(&without);
    TIDE_REQUIRE(world.Newton_Physics.status == Newton_Status_Ok && without.Newton_Physics.status == Newton_Status_Ok);
    const DuckGrab grab = tide_get_DuckGrab(&world, world.DuckFixture.first);
    TIDE_CHECK(!tide_entity_is_null(grab.body) && !tide_entity_equal(grab.body, body_at(&world, duck, 0)));
    TIDE_CHECK(tide_length_f3(grab.force) > 0.1f && tide_length_f3(grab.force) <= 12.00001f);
    const tide_float3 point = tide_add_f3(before.position, tide_rotate_q(before.rotation, request.grabAnchor));
    check_vector(grab.torque, tide_cross_f3(tide_sub_f3(point, body_com(before)), grab.force), 2e-7f);
    check_vector(tide_sub_f3(duck_momentum(&world, duck), duck_momentum(&without, tide_get_Duck(&without, without.DuckFixture.first))),
        tide_mul_f3(tide_f3_splat(DUCK_DT), grab.force), 2e-5f);
    TIDE_CHECK(tide_distance_f3(tide_get_Newton_Body(&world, body_at(&world, duck, 0)).velocity,
                              tide_get_Newton_Body(&without, body_at(&without, tide_get_Duck(&without, without.DuckFixture.first), 0)).velocity) > 1e-5f);
    tide_world_free(&world);
    tide_world_free(&without);
}

TIDE_TEST(microduck_grab_rejects_invalid_requests_and_records_requested_metadata)
{
    static tide_world world;
    for (int scenario = 0; scenario < 6; ++scenario) {
        manual_world_init(&world, DUCK_DT);
        const tide_entity ball = entity_at(&world, world.DuckSandbox.balls, 0);
        DuckControls request = grab_action(&world, ball, tide_f3(0, 0.01f, 0), 1);
        if (scenario == 0) request.grabAnchor = tide_f3(0, 0, 0); // not on the sphere
        if (scenario == 1) ++request.grabCollider.generation;
        if (scenario == 2) ++request.grabRevision;
        if (scenario == 3) request.grabCollider = world.DuckSandbox.floor;
        if (scenario == 4) {
            Newton_Collider collider = tide_get_Newton_Collider(&world, ball);
            collider.enabled = false;
            TIDE_REQUIRE(tide_set_Newton_Collider(&world, ball, collider));
        }
        if (scenario == 5) request.grabAnchor = tide_f3(0.25f, 0.25f, 0.25f);
        tide_world_set_input(&world, tide_player_from_index(0), request);
        tide_world_tick(&world);
        TIDE_REQUIRE(world.Newton_Physics.status == Newton_Status_Ok);
        const DuckGrab grab = tide_get_DuckGrab(&world, world.DuckFixture.first);
        TIDE_CHECK(grab.requested && tide_entity_is_null(grab.body) && tide_entity_is_null(grab.collider));
        check_vector(grab.force, tide_f3(0, 0, 0), 0);
        check_vector(grab.torque, tide_f3(0, 0, 0), 0);
        const Duck duck = tide_get_Duck(&world, world.DuckFixture.first);
        const DuckTransition sample = transition_at(&world, duck, 0);
        TIDE_CHECK(sample.grabRequested && tide_entity_is_null(sample.grabBody));
        TIDE_CHECK(tide_entity_equal(sample.grabCollider, request.grabCollider));
        check_vector(sample.grabAnchor, request.grabAnchor, 0);
        check_vector(sample.grabTarget, request.grabTarget, 0);
        check_vector(sample.grabForce, tide_f3(0, 0, 0), 0);
        tide_world_free(&world);
    }
}

TIDE_TEST(microduck_grab_caps_force_expires_and_releases_without_extra_impulse)
{
    static tide_world world;
    manual_world_init(&world, DUCK_DT);
    world.Newton_Physics.gravity = tide_f3(0, 0, 0);
    const tide_entity ball = entity_at(&world, world.DuckSandbox.balls, 0);
    Newton_Body body = tide_get_Newton_Body(&world, ball);
    body.position = tide_f3(0, 1, 0);
    TIDE_REQUIRE(tide_set_Newton_Body(&world, ball, body));
    DuckControls request = grab_action(&world, ball, tide_f3(0, 0.002f, 0), 17);
    tide_world_set_input(&world, tide_player_from_index(0), request);
    tide_world_tick(&world);
    TIDE_CHECK(tide_entity_equal(tide_get_DuckGrab(&world, world.DuckFixture.first).body, ball));
    for (int tick = 0; tick < 49; ++tick) tide_world_tick(&world);
    DuckGrab before_expiry = tide_get_DuckGrab(&world, world.DuckFixture.first);
    TIDE_CHECK(tide_entity_equal(before_expiry.body, ball) && before_expiry.lease > 0);
    TIDE_CHECK(tide_length_f3(before_expiry.force) > 0.00001f);
    for (int tick = 0; tick < 2; ++tick) tide_world_tick(&world);
    DuckGrab grab = tide_get_DuckGrab(&world, world.DuckFixture.first);
    TIDE_CHECK(grab.requested && grab.lease == 0 && tide_entity_is_null(grab.body));
    const Newton_Body coast = tide_get_Newton_Body(&world, ball);
    tide_world_tick(&world);
    check_vector(tide_get_Newton_Body(&world, ball).velocity, coast.velocity, 2e-6f);
    request = grab_action(&world, ball, tide_f3(1, 0.4f, 1), 18);
    const Newton_Body before_cap = tide_get_Newton_Body(&world, ball);
    tide_world_set_input(&world, tide_player_from_index(0), request);
    tide_world_tick(&world);
    TIDE_REQUIRE(world.Newton_Physics.status == Newton_Status_Ok);
    grab = tide_get_DuckGrab(&world, world.DuckFixture.first);
    TIDE_CHECK(tide_entity_equal(grab.body, ball));
    TIDE_CHECK(fabsf(tide_length_f3(grab.force) - 12.0f) < 2e-6f);
    const tide_float3 cap_point = tide_add_f3(before_cap.position, tide_rotate_q(before_cap.rotation, request.grabAnchor));
    const tide_float3 cap_arm = tide_sub_f3(cap_point, body_com(before_cap));
    check_vector(grab.torque, tide_cross_f3(cap_arm, grab.force), 2e-7f);
    TIDE_CHECK(tide_length_f3(grab.torque) <= tide_length_f3(cap_arm) * 12.0f + 1e-6f);
    const Newton_Body before_release = tide_get_Newton_Body(&world, ball);
    tide_world_set_input(&world, tide_player_from_index(0), (DuckControls){0});
    tide_world_tick(&world);
    grab = tide_get_DuckGrab(&world, world.DuckFixture.first);
    TIDE_CHECK(!grab.requested && tide_entity_is_null(grab.body));
    check_vector(grab.force, tide_f3(0, 0, 0), 0);
    check_vector(grab.torque, tide_f3(0, 0, 0), 0);
    check_vector(tide_get_Newton_Body(&world, ball).velocity, before_release.velocity, 2e-6f);
    tide_world_free(&world);
}

TIDE_TEST(microduck_active_grab_survives_snapshot_raw_delta_threads_and_lease_replay)
{
    static tide_world source, base, saved, replay, packed, delta, other;
    manual_world_init(&source, DUCK_DT);
    source.Newton_Physics.gravity = tide_f3(0, 0, 0);
    const tide_entity ball = entity_at(&source, source.DuckSandbox.balls, 0);
    Newton_Body body = tide_get_Newton_Body(&source, ball);
    body.position = tide_f3(0, 1, 0);
    TIDE_REQUIRE(tide_set_Newton_Body(&source, ball, body));
    const DuckControls request = grab_action(&source, ball, tide_f3(0.003f, 0.002f, 0), 1);
    tide_world_set_input(&source, tide_player_from_index(0), request);
    for (int tick = 0; tick < 20; ++tick) {
        tide_world_tick(&source);
        if (tick == 9) tide_world_copy(&base, &source);
    }
    const DuckGrab active = tide_get_DuckGrab(&source, source.DuckFixture.first);
    TIDE_REQUIRE(tide_entity_equal(active.body, ball) && active.lease > 0.1f && tide_length_f3(active.force) > 0.001f);
    tide_world_copy(&saved, &source);
    tide_world_copy(&replay, &saved);
    const uint32_t size = tide_world_pack(&source, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes && tide_world_pack(&source, bytes, size) == size && tide_world_unpack(&packed, bytes, size));
    free(bytes);
    uint32_t delta_size = 0;
    bytes = tide_world_pack_delta(&source, &base, NULL, 0, &delta_size);
    TIDE_REQUIRE(bytes && tide_world_unpack_delta(&delta, &base, bytes, delta_size));
    free(bytes);
    manual_world_init(&other, DUCK_DT);
#ifdef NEWTON_TEST_JOBS
    const tide_jobs *jobs = tide_platform_jobs();
#endif
    for (int tick = 20; tick < 70; ++tick) {
        tide_world_tick(&source);
        tide_world_tick(&other);
#ifdef NEWTON_TEST_JOBS
        tide_world_tick_on(&replay, jobs);
#else
        tide_world_tick(&replay);
#endif
        tide_world_tick(&packed);
        tide_world_tick(&delta);
        TIDE_REQUIRE(source.Newton_Physics.status == Newton_Status_Ok);
        const uint64_t hash = tide_world_hash(&source);
        TIDE_REQUIRE(tide_world_hash(&replay) == hash && tide_world_hash(&packed) == hash && tide_world_hash(&delta) == hash);
        const DuckGrab grab = tide_get_DuckGrab(&source, source.DuckFixture.first);
        if (tick == 49) TIDE_CHECK(tide_entity_equal(grab.body, ball) && grab.lease > 0);
        if (tick >= 51) TIDE_CHECK(grab.requested && grab.lease == 0 && tide_entity_is_null(grab.body));
    }
    TIDE_CHECK(tide_world_hash(&saved) != tide_world_hash(&source));
    tide_world_free(&source); tide_world_free(&base); tide_world_free(&saved);
    tide_world_free(&replay); tide_world_free(&packed); tide_world_free(&delta); tide_world_free(&other);
}

TIDE_TEST(microduck_reset_only_own_duck_and_shared_balls_rejects_old_grab_revisions)
{
    static tide_world world;
    manual_world_init(&world, DUCK_DT);
    Duck first = tide_get_Duck(&world, world.DuckFixture.first);
    Duck second = tide_get_Duck(&world, world.DuckFixture.second);
    second.selected = 9;
    second.settleTicks = 200;
    TIDE_REQUIRE(tide_set_Duck(&world, world.DuckFixture.second, second));
    const tide_entity own_collider = child_collider(&world, first, 6);
    DuckControls old_child = grab_action(&world, own_collider, tide_f3(0, 0.005f, 0), 1);
    const tide_entity ball = entity_at(&world, world.DuckSandbox.balls, 0);
    const DuckControls old_ball = grab_action(&world, ball, tide_f3(0, 0.005f, 0), 2);
    tide_world_set_input(&world, tide_player_from_index(0), (DuckControls){.reset = true});
    tide_world_set_input(&world, tide_player_from_index(1), old_child);
    tide_world_tick(&world);
    TIDE_REQUIRE(world.Newton_Physics.status == Newton_Status_Ok);
    first = tide_get_Duck(&world, world.DuckFixture.first);
    second = tide_get_Duck(&world, world.DuckFixture.second);
    TIDE_CHECK(first.settleTicks == 1 && first.phase == 0 && first.next == 1);
    TIDE_CHECK(second.settleTicks == 200 && second.selected == 9);
    for (int i = 0; i < DUCK_BODIES; ++i) {
        TIDE_CHECK(tide_get_SandboxBody(&world, body_at(&world, first, i)).revision == 1);
        TIDE_CHECK(tide_get_SandboxBody(&world, body_at(&world, second, i)).revision == 0);
    }
    for (int i = 0; i < 4; ++i)
        TIDE_CHECK(tide_get_SandboxBody(&world, entity_at(&world, world.DuckSandbox.balls, i)).revision == 1);
    TIDE_CHECK(tide_entity_is_null(tide_get_DuckGrab(&world, world.DuckFixture.second).body));
    // A held reset has no second edge. The old shared-ball revision also fails.
    tide_world_set_input(&world, tide_player_from_index(1), old_ball);
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_SandboxBody(&world, ball).revision == 1);
    TIDE_CHECK(tide_entity_is_null(tide_get_DuckGrab(&world, world.DuckFixture.second).body));
    tide_world_set_input(&world, tide_player_from_index(0), (DuckControls){0});
    tide_world_set_input(&world, tide_player_from_index(1), grab_action(&world, ball, tide_f3(0, 0.005f, 0), 3));
    tide_world_tick(&world);
    TIDE_CHECK(tide_entity_equal(tide_get_DuckGrab(&world, world.DuckFixture.second).body, ball));
    tide_world_free(&world);
}

TIDE_TEST(microduck_converted_model_matches_original_full_tensor_newton_dynamics)
{
    static tide_world world;
    manual_world_init(&world, DUCK_DT);
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
                NULL, 0, md_oracle_gravity, MD_ORACLE_DT, 1) == NT_OK);
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
    manual_world_init(&world, DUCK_DT);
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
    manual_world_init(&supported, DUCK_DT);
    manual_world_init(&falling, DUCK_DT);
    Duck no_floor = tide_get_Duck(&falling, falling.DuckFixture.first);
    Newton_Collider floor = tide_get_Newton_Collider(&falling, falling.DuckSandbox.floor);
    floor.enabled = false;
    TIDE_REQUIRE(tide_set_Newton_Collider(&falling, falling.DuckSandbox.floor, floor));
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
    TIDE_CHECK(!tide_get_Newton_Collider(&falling, falling.DuckSandbox.floor).enabled && no_floor.assisted);
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
    manual_world_init(&assisted, DUCK_DT);
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
    manual_world_init(&world, DUCK_DT);
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
                const float lowest = sole_clearance(&world, duck, foot);
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
    manual_world_init(&original, DUCK_DT);
    duck_ticks(&original, 260);
    tide_world_copy(&saved, &original);
    duck_ticks(&original, 180);
    const uint64_t expected = tide_world_hash(&original);
    manual_world_init(&other, DUCK_DT);
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
    manual_world_init(&world, DUCK_DT);
    duck_ticks(&world, 710);
    manual_world_init(&fresh, DUCK_DT);
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
    manual_world_init(&world, DUCK_DT);
    duck_ticks(&world, 300);
    const Duck before = tide_get_Duck(&world, world.DuckFixture.first);
    float records[64][DUCK_OBSERVATIONS * 2 + DUCK_JOINTS * 2 + 7];
    int ticks[64];
    DuckTransition metadata[64];
    for (int slot = 0; slot < 64; ++slot) {
        const DuckTransition sample = transition_at(&world, before, slot);
        metadata[slot] = sample;
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
        TIDE_CHECK(memcmp(&sample, &metadata[slot], sizeof(sample)) == 0);
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
    manual_world_init(&source, DUCK_DT);
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
    manual_world_init(&serial, DUCK_DT);
    manual_world_init(&threaded, DUCK_DT);
    for (int i = 0; i < 400; ++i) {
        duck_advance(&serial, NULL);
        duck_advance(&threaded, jobs);
        TIDE_REQUIRE(tide_world_hash(&serial) == tide_world_hash(&threaded));
    }
    tide_world_free(&serial);
    tide_world_free(&threaded);
}
#endif

typedef struct duck_network_policy {
    int seat;
    DuckControls grab;
} duck_network_policy;

static void duck_policy(void *user, uint32_t tick, void *input)
{
    const duck_network_policy *policy = user;
    DuckControls controls = duck_action(tick, policy->seat);
    if (tick >= 100 && tick < 360) {
        controls.grab = true;
        controls.grabCollider = policy->grab.grabCollider;
        controls.grabAnchor = policy->grab.grabAnchor;
        controls.grabTarget = policy->grab.grabTarget;
        controls.grabRevision = policy->grab.grabRevision;
        // Held packets expire, then a later serial starts another lease.
        controls.grabSerial = tick < 220 ? 1 : tick < 345 ? 2 : 3;
    }
    *(DuckControls *)input = controls;
}

TIDE_TEST(microduck_two_players_use_the_same_physics_through_rollback)
{
    tide_loopback *net = tide_loopback_create(8021);
    TIDE_REQUIRE(net);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.025, .jitter = 0.005, .loss = 0.02});
    tide_server *server = tide_server_create(&(tide_server_desc){
        .game = &tide_game_api, .tick_rate = 200, .transports = {tide_loopback_endpoint(net, 1)}}, 0);
    TIDE_REQUIRE(server);
    const tide_world *initial = tide_server_world(server);
    TIDE_REQUIRE(initial);
    duck_network_policy policies[2];
    bool grabbed[2] = {false}, recorded[2] = {false}, expired[2] = {false};
    bool late_active[2] = {false}, released[2] = {false};
    tide_client *clients[2];
    for (int i = 0; i < 2; ++i) {
        policies[i].seat = i;
        const Duck owned = tide_get_Duck(initial, initial->DuckFixture.second);
        const tide_entity collider = i == 0 ? entity_at(initial, initial->DuckSandbox.balls, 0)
                                           : child_collider(initial, owned, 8);
        TIDE_REQUIRE(!tide_entity_is_null(collider));
        policies[i].grab = grab_action(initial, collider, tide_f3(0, 0.005f, 0), 1);
        clients[i] = tide_client_create(&(tide_client_desc){.game = &tide_game_api,
            .transport = tide_loopback_endpoint(net, (uint32_t)i + 2), .server = tide_loopback_address(1),
            .lead = 2, .sample = duck_policy, .user = &policies[i]}, 0);
        TIDE_REQUIRE(clients[i]);
    }
    double now = 0;
    for (int frame = 0; frame < 400; ++frame) {
        now += 1.0 / 200.0;
        tide_loopback_set_time(net, now);
        for (int i = 0; i < 2; ++i) tide_client_update(clients[i], now);
        tide_server_update(server, now);
        const tide_world *current = tide_server_world(server);
        for (int seat = 0; seat < 2; ++seat) {
            const tide_entity owner = seat ? current->DuckFixture.second : current->DuckFixture.first;
            const Duck duck = tide_get_Duck(current, owner);
            const DuckGrab grab = tide_get_DuckGrab(current, owner);
            const bool active = !tide_entity_is_null(grab.body) && tide_length_f3(grab.force) > 0.001f;
            grabbed[seat] = grabbed[seat] || active;
            if (current->Time.tick >= 351 && current->Time.tick <= 360) late_active[seat] = late_active[seat] || active;
            if (active) {
                const DuckTransition latest = transition_at(current, duck, (duck.next + 63) % 64);
                // Jitter can reverse handshake order. Policy index belongs to
                // the client, while this Duck belongs to its assigned player.
                const tide_player_id player = tide_get_Owner(current, owner).player;
                int policy_index = -1;
                for (int client = 0; client < 2; ++client)
                    if (tide_player_equal(tide_client_status_of(clients[client]).player, player)) policy_index = client;
                TIDE_REQUIRE(policy_index >= 0);
                const tide_entity requested = policies[policy_index].grab.grabCollider;
                const tide_entity expected_body = collider_body(requested, tide_get_Newton_Collider(current, requested));
                TIDE_CHECK(tide_entity_equal(grab.body, expected_body));
                TIDE_CHECK(latest.grabRequested && tide_entity_equal(latest.grabBody, expected_body));
                TIDE_CHECK(tide_entity_equal(latest.grabCollider, requested));
                check_vector(latest.grabForce, grab.force, 0);
                check_vector(latest.grabTorque, grab.torque, 0);
                check_vector(latest.grabAnchor, grab.requestedAnchor, 0);
                check_vector(latest.grabTarget, grab.requestedTarget, 0);
                TIDE_CHECK(tide_length_f3(latest.grabForce) <= 12.00001f);
                recorded[seat] = true;
            }
            expired[seat] = expired[seat] || (grab.requested && grab.lease == 0 && tide_entity_is_null(grab.body));
            if (current->Time.tick >= 361 && !grab.requested) {
                TIDE_CHECK(tide_entity_is_null(grab.body));
                check_vector(grab.force, tide_f3(0, 0, 0), 0);
                check_vector(grab.torque, tide_f3(0, 0, 0), 0);
                released[seat] = true;
            }
        }
    }
    const tide_world *authority = tide_server_world(server);
    TIDE_REQUIRE(authority && authority->Newton_Physics.status == Newton_Status_Ok);
    for (int i = 0; i < 2; ++i) {
        TIDE_CHECK(grabbed[i] && recorded[i] && expired[i] && late_active[i] && released[i]);
        const tide_client_status status = tide_client_status_of(clients[i]);
        TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(status.resyncs == 0 && status.again_ticks > 0);
        const tide_world *verified = tide_client_verified_world(clients[i]);
        TIDE_CHECK(verified && verified->Newton_Physics.status == Newton_Status_Ok);
        TIDE_REQUIRE(verified && verified->Time.tick >= 361);
        for (int seat = 0; seat < 2; ++seat) {
            const DuckGrab grab = tide_get_DuckGrab(verified, seat ? verified->DuckFixture.second : verified->DuckFixture.first);
            TIDE_CHECK(!grab.requested && tide_entity_is_null(grab.body));
            check_vector(grab.force, tide_f3(0, 0, 0), 0);
            check_vector(grab.torque, tide_f3(0, 0, 0), 0);
        }
        printf("    duck player %d: %u verified ticks, %u replayed, %u resyncs\n", i,
            status.verified_tick, status.again_ticks, status.resyncs);
        tide_client_destroy(clients[i]);
    }
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}
