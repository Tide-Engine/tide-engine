#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "microduck.h"
#include "tide_test.h"

/* Real generated-game acceptance: the released actor, its default hardware
 * filters, and Newton contacts/motors run without balance or root assistance.
 * Walking requests .4 m/s; held Shift requests .5 m/s and 1.25x yaw through
 * the same actor. The physical run acceptance below exercises forward travel.
 * No test-side controller, contact, limit, gravity, or learned-target overrides
 * are applied. */
#define LOC_DT (1.0f / 200.0f)
#define LOC_PI 3.14159265358979323846f
#define LOC_BODIES 15
#define LOC_JOINTS 14
#define LOC_SETTLE 400
#define LOC_COMMAND 1400
#define LOC_RELEASE 200
#define LOC_EPISODE (LOC_SETTLE + LOC_COMMAND + LOC_RELEASE)
#define LOC_RECOVERY_EPISODE 4000
#define LOC_RECOVERY_DEADLINE 1600
#define LOC_UPRIGHT_DWELL 200
#define LOC_LATE_RECOVERY 400
#define LOC_CONTACT_DWELL 4

/* Unlike TIDE_REQUIRE, this helper guard is restricted to bool functions.
 * Callers record the failure and free their initialized worlds. */
#define LOC_GUARD(expr) do { \
    if (!(expr)) { \
        printf("    locomotion guard %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return false; \
    } \
} while (0)

typedef struct loc_foot_cycle {
    int airborne;
    int touchdown;
    int cycles;
    bool lifted;
    float maximum_clearance;
} loc_foot_cycle;

static tide_entity loc_entity(const tide_world *world, tide_list list, int index)
{
    const tide_entity *value = tide_list_read(&world->heap, list, index, sizeof(*value));
    return value ? *value : (tide_entity){0};
}

static float loc_number(const tide_world *world, tide_list list, int index)
{
    const float *value = tide_list_read(&world->heap, list, index, sizeof(*value));
    return value ? *value : NAN;
}

static bool loc_zero(tide_float3 value)
{
    return value.x == 0 && value.y == 0 && value.z == 0;
}

static bool loc_finite(tide_float3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static tide_entity loc_shape_body(tide_entity entity, Newton_Collider collider)
{
    return tide_entity_is_null(collider.body) ? entity : collider.body;
}

static tide_quaternion loc_original(const tide_world *world, Duck duck)
{
    const tide_entity entity = loc_entity(world, duck.bodies, 0);
    const Newton_Body body = tide_get_Newton_Body(world, entity);
    const DuckPart part = tide_get_DuckPart(world, entity);
    return tide_mul_q(body.rotation, tide_inverse_q(part.principalRotation));
}

static float loc_up(const tide_world *world, Duck duck)
{
    return tide_rotate_q(loc_original(world, duck), tide_f3(0, 0, 1)).y;
}

static float loc_yaw(const tide_world *world, Duck duck)
{
    const tide_float3 forward = tide_rotate_q(loc_original(world, duck), tide_f3(1, 0, 0));
    return atan2f(-forward.z, forward.x);
}

static float loc_project(tide_float3 value, tide_float3 forward)
{
    return value.x * forward.x + value.z * forward.z;
}

static float loc_bottom(const tide_world *world, tide_entity entity)
{
    const Newton_Collider collider = tide_get_Newton_Collider(world, entity);
    const Newton_Body body = tide_get_Newton_Body(world, loc_shape_body(entity, collider));
    const tide_float3 center = tide_add_f3(body.position, tide_rotate_q(body.rotation, collider.position));
    return center.y - collider.radius;
}

static bool loc_clearances(const tide_world *world, Duck duck, float lowest[2])
{
    int counts[2] = {0, 0};
    lowest[0] = lowest[1] = INFINITY;
    for (int i = 0; i < tide_list_read_count(&world->heap, duck.soles); ++i) {
        const tide_entity entity = loc_entity(world, duck.soles, i);
        LOC_GUARD(tide_has_DuckSole(world, entity));
        const int foot = tide_get_DuckSole(world, entity).foot;
        LOC_GUARD(foot == 0 || foot == 1);
        const Newton_Collider collider = tide_get_Newton_Collider(world, entity);
        LOC_GUARD(collider.enabled && collider.type == Newton_ColliderType_Sphere);
        const float height = loc_bottom(world, entity);
        LOC_GUARD(isfinite(height));
        if (height < lowest[foot]) lowest[foot] = height;
        ++counts[foot];
    }
    LOC_GUARD(counts[0] == 6 && counts[1] == 6);
    return true;
}

static void loc_cycle(loc_foot_cycle *cycle, float height, float own, float opposite)
{
    if (height > cycle->maximum_clearance) cycle->maximum_clearance = height;
    if (height > .005f && own < .05f && opposite > .05f) {
        if (++cycle->airborne >= LOC_CONTACT_DWELL) cycle->lifted = true;
    } else {
        cycle->airborne = 0;
    }
    /* Loaded dwell plus a near-floor sole prevents a transient force spike or
     * an airborne body contact from counting as the end of a physical step. */
    if (cycle->lifted && own > .05f && height < .002f) {
        if (++cycle->touchdown >= LOC_CONTACT_DWELL) {
            ++cycle->cycles;
            cycle->lifted = false;
            cycle->airborne = cycle->touchdown = 0;
        }
    } else {
        cycle->touchdown = 0;
    }
}

static bool loc_prepare(tide_world *world)
{
    tide_world_init(world, LOC_DT);
    const tide_entity learned = world->DuckFixture.first;
    const tide_entity companion = world->DuckFixture.second;
    const Duck duck = tide_get_Duck(world, learned);
    Duck other = tide_get_Duck(world, companion);
    LOC_GUARD(tide_list_read_count(&world->heap, duck.bodies) == LOC_BODIES);
    LOC_GUARD(tide_list_read_count(&world->heap, duck.joints) == LOC_JOINTS);

    DuckPolicyState policy = tide_get_DuckPolicyState(world, learned);
    LOC_GUARD(policy.enabled && policy.actionScale == .9f &&
        policy.legAlpha == .7f && policy.headAlpha == .5f &&
        policy.forwardSpeed == .4f && policy.runSpeed == .5f);

    /* A static articulation root is valid; making its jointed children static
     * is not. Its disabled colliders eliminate all interaction with the duck. */
    const tide_entity other_root = loc_entity(world, other.bodies, 0);
    Newton_Body body = tide_get_Newton_Body(world, other_root);
    body.type = Newton_BodyType_Static;
    body.velocity = body.angularVelocity = body.force = body.torque = tide_f3(0, 0, 0);
    LOC_GUARD(tide_set_Newton_Body(world, other_root, body));
    DuckPolicyState other_policy = tide_get_DuckPolicyState(world, companion);
    other_policy.enabled = false;
    LOC_GUARD(tide_set_DuckPolicyState(world, companion, other_policy));
    other.assisted = false;
    other.assistanceForce = other.assistanceTorque = tide_f3(0, 0, 0);
    LOC_GUARD(tide_set_Duck(world, companion, other));
    for (int i = 0; i < tide_list_read_count(&world->heap, other.colliders); ++i) {
        const tide_entity entity = loc_entity(world, other.colliders, i);
        Newton_Collider collider = tide_get_Newton_Collider(world, entity);
        collider.enabled = false;
        LOC_GUARD(tide_set_Newton_Collider(world, entity, collider));
    }
    for (int i = 0; i < tide_list_read_count(&world->heap, world->DuckSandbox.balls); ++i) {
        const tide_entity entity = loc_entity(world, world->DuckSandbox.balls, i);
        body = tide_get_Newton_Body(world, entity);
        body.type = Newton_BodyType_Static;
        body.velocity = body.angularVelocity = body.force = body.torque = tide_f3(0, 0, 0);
        LOC_GUARD(tide_set_Newton_Body(world, entity, body));
        Newton_Collider collider = tide_get_Newton_Collider(world, entity);
        collider.enabled = false;
        LOC_GUARD(tide_set_Newton_Collider(world, entity, collider));
    }

    /* Assert the actual game's new contact/stop wiring. Do not silently supply
     * it in the fixture and thereby let a missing integration pass. */
    const Newton_Collider floor = tide_get_Newton_Collider(world, world->DuckSandbox.floor);
    LOC_GUARD(floor.enabled && floor.type == Newton_ColliderType_Plane &&
        floor.frictionModel == Newton_ContactFriction_Coulomb && floor.friction > 0);
    for (int i = 0; i < tide_list_read_count(&world->heap, duck.colliders); ++i) {
        const tide_entity entity = loc_entity(world, duck.colliders, i);
        const Newton_Collider collider = tide_get_Newton_Collider(world, entity);
        LOC_GUARD(collider.enabled && collider.type == Newton_ColliderType_Sphere &&
            collider.frictionModel == Newton_ContactFriction_Coulomb);
        if (tide_has_DuckSole(world, entity))
            LOC_GUARD(collider.friction == .8f || collider.friction == 1.f);
        else
            LOC_GUARD(collider.friction == 0);
    }
    for (int i = 0; i < LOC_JOINTS; ++i) {
        const Newton_Joint joint = tide_get_Newton_Joint(world, loc_entity(world, duck.joints, i));
        LOC_GUARD(joint.limited && joint.limitStiffness == 55.f && joint.limitDamping == .6f);
    }
    LOC_GUARD(world->Newton_Physics.substeps == 4 &&
        world->Newton_Physics.gravity.x == 0 && world->Newton_Physics.gravity.y == -9.81f &&
        world->Newton_Physics.gravity.z == 0);
    return true;
}

static bool loc_advance(tide_world *world, DuckControls input)
{
    tide_world_set_input(world, tide_player_from_index(0), input);
    tide_world_set_input(world, tide_player_from_index(1), (DuckControls){0});
    tide_world_tick(world);
    if (world->Newton_Physics.status != Newton_Status_Ok)
        printf("    locomotion failed physics: tick %d status %d\n",
            (int)world->Time.tick, (int)world->Newton_Physics.status);
    LOC_GUARD(world->Newton_Physics.status == Newton_Status_Ok);
    const Duck duck = tide_get_Duck(world, world->DuckFixture.first);
    const DuckPolicyState policy = tide_get_DuckPolicyState(world, world->DuckFixture.first);
    LOC_GUARD(policy.enabled && policy.status == 1 && policy.actionScale == .9f &&
        policy.legAlpha == .7f && policy.headAlpha == .5f &&
        policy.forwardSpeed == .4f && policy.runSpeed == .5f);
    LOC_GUARD(policy.inferences == (world->Time.tick + 3) / 4);
    LOC_GUARD(tide_list_read_count(&world->heap, policy.observation) == 61 &&
        tide_list_read_count(&world->heap, policy.action) == LOC_JOINTS);
    const float speed = input.run ? .5f : .4f;
    const float yaw = (input.run ? -1.25f : -1.f) * input.turn;
    LOC_GUARD(policy.command.x == speed * input.walk && policy.command.y == 0 &&
        policy.command.z == yaw);
    if ((world->Time.tick - 1) % 4 == 0) {
        LOC_GUARD(loc_number(world, policy.observation, 48) == speed * input.walk &&
            loc_number(world, policy.observation, 49) == 0 &&
            loc_number(world, policy.observation, 50) == yaw);
    }
    for (int i = 51; i < 61; ++i) LOC_GUARD(loc_number(world, policy.observation, i) == 0);
    LOC_GUARD(!duck.assisted && loc_zero(duck.assistanceForce) && loc_zero(duck.assistanceTorque));

    /* Published Body wrenches have already been cleared. Collected BodyState
     * inputs retain the forces actually submitted to the Newton solve. */
    for (int i = 0; i < LOC_BODIES; ++i) {
        const tide_entity entity = loc_entity(world, duck.bodies, i);
        const Newton_Body body = tide_get_Newton_Body(world, entity);
        LOC_GUARD(body.type == Newton_BodyType_Dynamic && loc_finite(body.position) &&
            loc_finite(body.velocity) && loc_finite(body.angularVelocity) &&
            loc_zero(body.force) && loc_zero(body.torque));
        bool found = false;
        for (int j = 0; j < tide_list_read_count(&world->heap, world->Newton_Physics.bodies); ++j) {
            const Newton_BodyState *state = tide_list_read(&world->heap,
                world->Newton_Physics.bodies, j, sizeof(*state));
            if (!state || !tide_entity_equal(state->entity, entity)) continue;
            LOC_GUARD(loc_zero(state->force) && loc_zero(state->torque));
            found = true;
        }
        LOC_GUARD(found);
    }
    const tide_entity players[2] = {world->DuckFixture.first, world->DuckFixture.second};
    for (int i = 0; i < 2; ++i) {
        const DuckGrab grab = tide_get_DuckGrab(world, players[i]);
        LOC_GUARD(!grab.requested && tide_entity_is_null(grab.body) &&
            loc_zero(grab.force) && loc_zero(grab.torque));
    }
    const Duck other = tide_get_Duck(world, world->DuckFixture.second);
    LOC_GUARD(!tide_get_DuckPolicyState(world, world->DuckFixture.second).enabled &&
        !other.assisted && loc_zero(other.assistanceForce) && loc_zero(other.assistanceTorque));
    LOC_GUARD(tide_get_Newton_Body(world, loc_entity(world, other.bodies, 0)).type == Newton_BodyType_Static);
    for (int i = 0; i < tide_list_read_count(&world->heap, other.colliders); ++i)
        LOC_GUARD(!tide_get_Newton_Collider(world, loc_entity(world, other.colliders, i)).enabled);
    for (int i = 0; i < tide_list_read_count(&world->heap, world->DuckSandbox.balls); ++i) {
        const tide_entity entity = loc_entity(world, world->DuckSandbox.balls, i);
        LOC_GUARD(!tide_get_Newton_Collider(world, entity).enabled &&
            tide_get_Newton_Body(world, entity).type == Newton_BodyType_Static);
    }
    LOC_GUARD(tide_list_read_count(&world->heap, duck.transitions) > 0);
    const DuckTransition *record = tide_list_read(&world->heap, duck.transitions,
        (duck.next + 63) % 64, sizeof(*record));
    LOC_GUARD(record && record->tick == world->Time.tick - 1 &&
        loc_zero(record->assistanceForce) && loc_zero(record->assistanceTorque) &&
        !record->grabRequested && tide_entity_is_null(record->grabBody) &&
        loc_zero(record->grabForce) && loc_zero(record->grabTorque));
    return true;
}

static void loc_walk_case(float sign)
{
    static tide_world driven, idle;
    const bool driven_ready = loc_prepare(&driven);
    const bool idle_ready = loc_prepare(&idle);
    TIDE_CHECK(driven_ready && idle_ready);
    if (!(driven_ready && idle_ready)) goto cleanup;
    loc_foot_cycle feet[2] = {{0}};
    tide_float3 start = tide_f3(0, 0, 0), idle_start = start, forward = tide_f3(1, 0, 0);
    float travel = 0, drift = 0, min_up = INFINITY, min_height = INFINITY;
    float stop_mean = 0, stop_speed = INFINITY, late_speed = 0;
    int stop_count = 0, late_count = 0;
    for (int tick = 0; tick < LOC_EPISODE; ++tick) {
        if (tick == LOC_SETTLE) {
            const Duck duck = tide_get_Duck(&driven, driven.DuckFixture.first);
            const Duck control = tide_get_Duck(&idle, idle.DuckFixture.first);
            start = tide_get_Newton_Body(&driven, loc_entity(&driven, duck.bodies, 0)).position;
            idle_start = tide_get_Newton_Body(&idle, loc_entity(&idle, control.bodies, 0)).position;
            forward = tide_rotate_q(loc_original(&driven, duck), tide_f3(1, 0, 0));
            const float length = sqrtf(forward.x * forward.x + forward.z * forward.z);
            TIDE_CHECK(isfinite(length) && length > .9f);
            if (!(isfinite(length) && length > .9f)) goto cleanup;
            forward = tide_f3(forward.x / length, 0, forward.z / length);
        }
        const bool moving = tick >= LOC_SETTLE && tick < LOC_SETTLE + LOC_COMMAND;
        const bool active_ok = loc_advance(&driven, (DuckControls){.walk = moving ? sign : 0});
        const bool idle_ok = loc_advance(&idle, (DuckControls){0});
        TIDE_CHECK(active_ok && idle_ok);
        if (!(active_ok && idle_ok)) goto cleanup;
        const Duck duck = tide_get_Duck(&driven, driven.DuckFixture.first);
        const Duck control = tide_get_Duck(&idle, idle.DuckFixture.first);
        const Newton_Body root = tide_get_Newton_Body(&driven, loc_entity(&driven, duck.bodies, 0));
        const Newton_Body control_root = tide_get_Newton_Body(&idle, loc_entity(&idle, control.bodies, 0));
        /* Stopping must remain upright too: collapsing after key release must
         * not satisfy the low-speed acceptance checks. Keep gait counts below
         * restricted to the commanded seven seconds. */
        if (tick >= LOC_SETTLE) {
            const float up = loc_up(&driven, duck);
            if (up < min_up) min_up = up;
            if (root.position.y < min_height) min_height = root.position.y;
        }
        if (moving) {
            float clearance[2];
            const bool valid_feet = loc_clearances(&driven, duck, clearance);
            TIDE_CHECK(valid_feet);
            if (!valid_feet) goto cleanup;
            loc_cycle(&feet[0], clearance[0], duck.footLoad.x, duck.footLoad.y);
            loc_cycle(&feet[1], clearance[1], duck.footLoad.y, duck.footLoad.x);
            if (tick >= LOC_SETTLE + LOC_COMMAND - 200) {
                const tide_float3 local = tide_rotate_q(tide_inverse_q(loc_original(&driven, duck)), root.velocity);
                late_speed += sign * local.x;
                ++late_count;
            }
        }
        if (tick == LOC_SETTLE + LOC_COMMAND - 1) {
            travel = sign * loc_project(tide_sub_f3(root.position, start), forward);
            drift = sign * loc_project(tide_sub_f3(control_root.position, idle_start), forward);
        }
        if (tick >= LOC_EPISODE - 50) {
            stop_mean += fabsf(loc_project(root.velocity, forward));
            ++stop_count;
        }
        if (tick == LOC_EPISODE - 1)
            stop_speed = sqrtf(root.velocity.x * root.velocity.x + root.velocity.z * root.velocity.z);
    }
    printf("    learned %s: signed travel %.6f m, zero-command drift %.6f m, excess %.6f m, "
        "cycles %d/%d, sole lift %.6f/%.6f m, min up %.6f, min height %.6f m, "
        "late signed body vx %.6f m/s, release mean |vx| %.6f m/s, final horizontal speed %.6f m/s\n",
        sign > 0 ? "W" : "S", (double)travel, (double)drift, (double)(travel - drift),
        feet[0].cycles, feet[1].cycles, (double)feet[0].maximum_clearance,
        (double)feet[1].maximum_clearance, (double)min_up, (double)min_height,
        (double)(late_speed / (float)late_count), (double)(stop_mean / (float)stop_count), (double)stop_speed);
    printf("    learned %s final world: %016llx\n", sign > 0 ? "W" : "S",
        (unsigned long long)tide_world_hash(&driven));
    TIDE_CHECK(travel >= .3f && travel - drift >= .3f);
    TIDE_CHECK(fabsf(drift) < .05f);
    TIDE_CHECK(min_up > .95f && min_height > .08f);
    TIDE_CHECK(feet[0].cycles >= 5 && feet[1].cycles >= 5);
    TIDE_CHECK(stop_mean / (float)stop_count < .05f && stop_speed < .08f);
cleanup:
    tide_world_free(&driven);
    tide_world_free(&idle);
}

static void loc_run_case(float sign)
{
    static tide_world worlds[2];
    const bool walk_ready = loc_prepare(&worlds[0]);
    const bool run_ready = loc_prepare(&worlds[1]);
    TIDE_CHECK(walk_ready && run_ready);
    if (!(walk_ready && run_ready)) goto cleanup;
    loc_foot_cycle feet[2][2] = {{{0}}};
    tide_float3 start[2] = {{0}}, forward[2] = {{0}};
    float travel[2] = {0}, min_up[2] = {INFINITY, INFINITY};
    float min_height[2] = {INFINITY, INFINITY}, stop_mean[2] = {0}, stop_speed[2] = {0};
    int stop_count = 0;
    for (int tick = 0; tick < LOC_EPISODE; ++tick) {
        const bool moving = tick >= LOC_SETTLE && tick < LOC_SETTLE + LOC_COMMAND;
        for (int run = 0; run < 2; ++run) {
            tide_world *world = &worlds[run];
            if (tick == LOC_SETTLE) {
                const Duck duck = tide_get_Duck(world, world->DuckFixture.first);
                start[run] = tide_get_Newton_Body(world, loc_entity(world, duck.bodies, 0)).position;
                forward[run] = tide_rotate_q(loc_original(world, duck), tide_f3(1, 0, 0));
                const float length = sqrtf(forward[run].x * forward[run].x +
                    forward[run].z * forward[run].z);
                TIDE_CHECK(isfinite(length) && length > .9f);
                if (!(isfinite(length) && length > .9f)) goto cleanup;
                forward[run] = tide_f3(forward[run].x / length, 0, forward[run].z / length);
            }
            const bool healthy = loc_advance(world,
                (DuckControls){.walk = moving ? sign : 0, .run = moving && run != 0});
            TIDE_CHECK(healthy);
            if (!healthy) goto cleanup;
            const Duck duck = tide_get_Duck(world, world->DuckFixture.first);
            const Newton_Body root = tide_get_Newton_Body(world, loc_entity(world, duck.bodies, 0));
            if (tick >= LOC_SETTLE) {
                const float up = loc_up(world, duck);
                if (up < min_up[run]) min_up[run] = up;
                if (root.position.y < min_height[run]) min_height[run] = root.position.y;
            }
            if (moving) {
                float clearance[2];
                const bool valid_feet = loc_clearances(world, duck, clearance);
                TIDE_CHECK(valid_feet);
                if (!valid_feet) goto cleanup;
                loc_cycle(&feet[run][0], clearance[0], duck.footLoad.x, duck.footLoad.y);
                loc_cycle(&feet[run][1], clearance[1], duck.footLoad.y, duck.footLoad.x);
            }
            if (tick == LOC_SETTLE + LOC_COMMAND - 1)
                travel[run] = sign * loc_project(tide_sub_f3(root.position, start[run]), forward[run]);
            if (tick >= LOC_EPISODE - 50)
                stop_mean[run] += fabsf(loc_project(root.velocity, forward[run]));
            if (tick == LOC_EPISODE - 1)
                stop_speed[run] = sqrtf(root.velocity.x * root.velocity.x + root.velocity.z * root.velocity.z);
        }
        if (tick >= LOC_EPISODE - 50) ++stop_count;
    }
    for (int run = 0; run < 2; ++run) {
        printf("    learned %s%s: signed travel %.6f m, cycles %d/%d, min up %.6f, "
            "min height %.6f m, release mean |vx| %.6f m/s, final horizontal speed %.6f m/s\n",
            run ? "Shift+" : "", sign > 0 ? "W" : "S", (double)travel[run],
            feet[run][0].cycles, feet[run][1].cycles, (double)min_up[run], (double)min_height[run],
            (double)(stop_mean[run] / (float)stop_count), (double)stop_speed[run]);
        printf("    learned %s%s final world: %016llx\n", run ? "Shift+" : "",
            sign > 0 ? "W" : "S", (unsigned long long)tide_world_hash(&worlds[run]));
        TIDE_CHECK(travel[run] >= .3f);
        TIDE_CHECK(min_up[run] > .95f && min_height[run] > .08f);
        TIDE_CHECK(feet[run][0].cycles >= 5 && feet[run][1].cycles >= 5);
        TIDE_CHECK(stop_mean[run] / (float)stop_count < .05f && stop_speed[run] < .08f);
    }
    printf("    learned Shift+%s extra travel over walking: %.6f m\n",
        sign > 0 ? "W" : "S", (double)(travel[1] - travel[0]));
    TIDE_CHECK(travel[1] - travel[0] >= .1f);
cleanup:
    tide_world_free(&worlds[0]);
    tide_world_free(&worlds[1]);
}

static bool loc_seed_fall(tide_world *world, tide_float3 axis, float radians)
{
    const Duck duck = tide_get_Duck(world, world->DuckFixture.first);
    const Newton_Body root = tide_get_Newton_Body(world, loc_entity(world, duck.bodies, 0));
    const DuckPolicyState before = tide_get_DuckPolicyState(world, world->DuckFixture.first);
    const tide_list old_lists[4] = {before.observation, before.action, before.filtered, before.motor};
    float history[4][61] = {{0}};
    for (int list = 0; list < 4; ++list) {
        const int count = list == 0 ? 61 : LOC_JOINTS;
        LOC_GUARD(tide_list_read_count(&world->heap, old_lists[list]) == count);
        for (int i = 0; i < count; ++i) history[list][i] = loc_number(world, old_lists[list], i);
    }
    float positions[LOC_JOINTS];
    for (int i = 0; i < LOC_JOINTS; ++i)
        positions[i] = tide_get_Newton_Joint(world, loc_entity(world, duck.joints, i)).position;
    const tide_quaternion turn = tide_axisangle_q(axis, radians);
    for (int i = 0; i < LOC_BODIES; ++i) {
        const tide_entity entity = loc_entity(world, duck.bodies, i);
        Newton_Body body = tide_get_Newton_Body(world, entity);
        body.position = tide_add_f3(root.position, tide_rotate_q(turn, tide_sub_f3(body.position, root.position)));
        body.rotation = tide_mul_q(turn, body.rotation);
        body.velocity = body.angularVelocity = body.force = body.torque = tide_f3(0, 0, 0);
        LOC_GUARD(tide_set_Newton_Body(world, entity, body));
    }
    float lowest = INFINITY;
    for (int i = 0; i < tide_list_read_count(&world->heap, duck.colliders); ++i) {
        const tide_entity entity = loc_entity(world, duck.colliders, i);
        const float height = loc_bottom(world, entity);
        LOC_GUARD(isfinite(height));
        if (height < lowest) lowest = height;
    }
    LOC_GUARD(isfinite(lowest));
    const float translation = .001f - lowest;
    for (int i = 0; i < LOC_BODIES; ++i) {
        const tide_entity entity = loc_entity(world, duck.bodies, i);
        Newton_Body body = tide_get_Newton_Body(world, entity);
        body.position.y += translation;
        LOC_GUARD(tide_set_Newton_Body(world, entity, body));
    }
    for (int i = 0; i < LOC_JOINTS; ++i) {
        const tide_entity entity = loc_entity(world, duck.joints, i);
        Newton_Joint joint = tide_get_Newton_Joint(world, entity);
        LOC_GUARD(joint.position == positions[i]);
        joint.velocity = 0;
        LOC_GUARD(tide_set_Newton_Joint(world, entity, joint));
    }
    lowest = INFINITY;
    for (int i = 0; i < tide_list_read_count(&world->heap, duck.colliders); ++i) {
        const float height = loc_bottom(world, loc_entity(world, duck.colliders, i));
        if (height < lowest) lowest = height;
    }
    LOC_GUARD(fabsf(lowest - .001f) < 2e-6f && fabsf(loc_up(world, duck)) < .2f);
    const DuckPolicyState after = tide_get_DuckPolicyState(world, world->DuckFixture.first);
    LOC_GUARD(after.inferences == before.inferences && after.cadence == before.cadence &&
        after.status == before.status && after.previousMotorLoad == before.previousMotorLoad &&
        loc_zero(after.command));
    const tide_list new_lists[4] = {after.observation, after.action, after.filtered, after.motor};
    for (int list = 0; list < 4; ++list) {
        const int count = list == 0 ? 61 : LOC_JOINTS;
        LOC_GUARD(tide_list_read_count(&world->heap, new_lists[list]) == count);
        for (int i = 0; i < count; ++i)
            LOC_GUARD(history[list][i] == loc_number(world, new_lists[list], i));
    }
    for (int i = 48; i < 61; ++i) LOC_GUARD(loc_number(world, after.observation, i) == 0);
    return true;
}

static void loc_recovery_case(const char *label, tide_float3 axis, float radians)
{
    static tide_world world;
    const bool ready = loc_prepare(&world);
    TIDE_CHECK(ready);
    if (!ready) goto cleanup;
    int streak = 0, confirmed = 0, late_upright = 0;
    float final_up = 0, final_height = 0;
    for (int tick = 0; tick < LOC_RECOVERY_EPISODE; ++tick) {
        if (tick == LOC_SETTLE) {
            const bool seeded = loc_seed_fall(&world, axis, radians);
            TIDE_CHECK(seeded);
            if (!seeded) goto cleanup;
        }
        const bool healthy = loc_advance(&world, (DuckControls){0});
        TIDE_CHECK(healthy);
        if (!healthy) goto cleanup;
        if (tick < LOC_SETTLE) continue;
        const Duck duck = tide_get_Duck(&world, world.DuckFixture.first);
        const Newton_Body root = tide_get_Newton_Body(&world, loc_entity(&world, duck.bodies, 0));
        final_up = loc_up(&world, duck);
        final_height = root.position.y;
        const bool upright = final_up > .9f && final_height > .09f;
        streak = upright ? streak + 1 : 0;
        if (!confirmed && streak >= LOC_UPRIGHT_DWELL) confirmed = tick - LOC_SETTLE + 1;
        if (tick >= LOC_RECOVERY_EPISODE - LOC_LATE_RECOVERY && upright) ++late_upright;
    }
    printf("    learned recovery %s: first sustained upright start %.3f s, confirmed %.3f s "
        "after fall, late-2s upright %.6f, final up %.6f, height %.6f m\n", label,
        confirmed ? (double)((float)(confirmed - LOC_UPRIGHT_DWELL) * LOC_DT) : -1.,
        confirmed ? (double)((float)confirmed * LOC_DT) : -1.,
        (double)late_upright / LOC_LATE_RECOVERY, (double)final_up, (double)final_height);
    printf("    learned recovery %s final world: %016llx\n", label,
        (unsigned long long)tide_world_hash(&world));
    TIDE_CHECK(confirmed > 0 && confirmed <= LOC_RECOVERY_DEADLINE);
    TIDE_CHECK(late_upright >= (LOC_LATE_RECOVERY * 8) / 10);
cleanup:
    tide_world_free(&world);
}

static void loc_turn_case(float turn)
{
    static tide_world world;
    const bool ready = loc_prepare(&world);
    TIDE_CHECK(ready);
    if (!ready) goto cleanup;
    float previous_yaw = 0, yaw = 0, min_up = INFINITY, min_height = INFINITY;
    for (int tick = 0; tick < LOC_EPISODE; ++tick) {
        if (tick == LOC_SETTLE)
            previous_yaw = loc_yaw(&world, tide_get_Duck(&world, world.DuckFixture.first));
        const bool moving = tick >= LOC_SETTLE && tick < LOC_SETTLE + LOC_COMMAND;
        const bool healthy = loc_advance(&world, (DuckControls){.turn = moving ? turn : 0});
        TIDE_CHECK(healthy);
        if (!healthy) goto cleanup;
        const Duck duck = tide_get_Duck(&world, world.DuckFixture.first);
        const Newton_Body root = tide_get_Newton_Body(&world, loc_entity(&world, duck.bodies, 0));
        if (tick >= LOC_SETTLE) {
            const float up = loc_up(&world, duck);
            if (up < min_up) min_up = up;
            if (root.position.y < min_height) min_height = root.position.y;
        }
        if (!moving) continue;
        const float current = loc_yaw(&world, duck);
        float delta = current - previous_yaw;
        while (delta > LOC_PI) delta -= 2 * LOC_PI;
        while (delta < -LOC_PI) delta += 2 * LOC_PI;
        yaw += delta;
        previous_yaw = current;
    }
    printf("    learned turn input %+.2f, desired yaw %+.2f rad/s: unwrapped yaw %+.6f rad, "
        "min up %.6f, min height %.6f m\n", (double)turn, (double)-turn,
        (double)yaw, (double)min_up, (double)min_height);
    printf("    learned turn %+.2f final world: %016llx\n", (double)turn,
        (unsigned long long)tide_world_hash(&world));
    TIDE_CHECK((turn > 0 ? -yaw : yaw) >= .25f);
    TIDE_CHECK(min_up > .95f && min_height > .08f);
cleanup:
    tide_world_free(&world);
}

TIDE_TEST(microduck_learned_forward_has_unassisted_signed_travel_and_loaded_steps)
{
    loc_walk_case(1.f);
}

TIDE_TEST(microduck_learned_backward_has_unassisted_signed_travel_and_loaded_steps)
{
    loc_walk_case(-1.f);
}

TIDE_TEST(microduck_learned_run_forward_exceeds_walk_with_loaded_steps_and_release)
{
    loc_run_case(1.f);
}

TIDE_TEST(microduck_learned_prone_recovers_without_resetting_policy_history)
{
    loc_recovery_case("prone", tide_f3(0, 0, 1), -LOC_PI / 2);
}

TIDE_TEST(microduck_learned_supine_recovers_without_resetting_policy_history)
{
    loc_recovery_case("supine", tide_f3(0, 0, 1), LOC_PI / 2);
}

TIDE_TEST(microduck_learned_side_recovers_without_resetting_policy_history)
{
    loc_recovery_case("side", tide_f3(1, 0, 0), LOC_PI / 2);
}

TIDE_TEST(microduck_learned_turn_left_tracks_the_actual_signed_input)
{
    loc_turn_case(1.f);
}

TIDE_TEST(microduck_learned_turn_right_tracks_the_actual_signed_input)
{
    loc_turn_case(-1.f);
}
