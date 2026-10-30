// Value-only rows adapter for newton.tide. No native state survives this call.
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tide/list.h"
#include "tide/map.h"
#include "tide/math.h"
#include "tide/rows.h"

#include "runtime.h"

// Mirrors of newton.tide's private records, in declaration order. Entity has
// alignment 4, whereas runtime.h's uint64_t has alignment 8: BodyState is 116
// bytes and nt_body is 120. Copy individual records, never an array by stride.
typedef struct body_state {
    tide_entity entity;
    int32_t type;
    float mass;
    tide_float3 inertia;
    tide_float3 center_of_mass;
    tide_float3 position;
    tide_quaternion rotation;
    tide_float3 velocity;
    tide_float3 angular_velocity;
    tide_float3 force;
    tide_float3 torque;
} body_state;

typedef struct joint_state {
    tide_entity entity;
    tide_entity parent;
    tide_entity child;
    int32_t type;
    float position;
    float velocity;
    float effort;
    float effort_limit;
    float damping;
    float armature;
    int32_t limited;
    float lower;
    float upper;
    float limit_stiffness;
    float limit_damping;
    tide_float3 parent_position;
    tide_quaternion parent_rotation;
    tide_float3 child_position;
    tide_quaternion child_rotation;
    tide_float3 axis;
    float applied_effort;
    float friction;
    float load;
} joint_state;

typedef struct collider_state {
    tide_entity entity;
    tide_entity body;
    int32_t type;
    int32_t exclusion_group;
    float radius;
    tide_float3 position;
    tide_quaternion rotation;
    float stiffness;
    float damping;
    float friction_damping;
    float friction;
    tide_float3 contact_force;
    int32_t friction_model;
} collider_state;

typedef struct physics {
    tide_float3 gravity;
    int32_t substeps;
    int32_t status;
    uint8_t padding[4];
    tide_list bodies;
    tide_list joints;
    tide_list colliders;
    tide_map body_indices;
    tide_map joint_indices;
    tide_map collider_indices;
} physics;

// Time's invariant prefix, checked against check.c:add_builtins and generated
// C. Optional maxRewind/seed fields follow it in games that use those fields;
// neither their presence nor Time's total size is assumed by this adapter.
typedef struct simulation_time {
    float dt;
    int32_t tick;
    float t;
    int32_t tick_rate;
} simulation_time;

_Static_assert(sizeof(tide_entity) == 8 && offsetof(tide_entity, index) == 0 &&
                   offsetof(tide_entity, generation) == 4,
               "Entity's full identity layout changed");
_Static_assert(sizeof(tide_float3) == 12 && sizeof(tide_quaternion) == 16,
               "Newton's vector/quaternion layout changed");
_Static_assert(sizeof(body_state) == 116 && sizeof(nt_body) == 120 &&
                   sizeof(joint_state) == 152 && sizeof(nt_joint) == 152 &&
                   sizeof(collider_state) == 88 && sizeof(nt_collider) == 88,
               "Newton's Tide/native record sizes changed");
_Static_assert(sizeof(physics) == 144 && offsetof(physics, gravity) == 0 &&
                   offsetof(physics, substeps) == 12 && offsetof(physics, status) == 16 &&
                   offsetof(physics, bodies) == 24 && offsetof(physics, joints) == 32 &&
                   offsetof(physics, colliders) == 40 && offsetof(physics, body_indices) == 48 &&
                   offsetof(physics, joint_indices) == 80 && offsetof(physics, collider_indices) == 112,
               "newton.tide's Physics layout changed");
_Static_assert(sizeof(simulation_time) == 16 && offsetof(simulation_time, dt) == 0 &&
                   offsetof(simulation_time, tick) == 4 && offsetof(simulation_time, t) == 8 &&
                   offsetof(simulation_time, tick_rate) == 12,
               "Time's invariant prefix changed");
_Static_assert(NT_OK == 0 && NT_INVALID_BODY == 1 && NT_INVALID_JOINT == 2 &&
                   NT_INVALID_TOPOLOGY == 3 && NT_TOO_LARGE == 4 && NT_UNSTABLE == 5 &&
                   NT_INVALID_COLLIDER == 6 && NT_SPHERE == 0 && NT_PLANE == 1 &&
                   NT_REGULARIZED == 0 && NT_COULOMB == 1 &&
                   NT_STATIC == 0 && NT_DYNAMIC == 1 && NT_FIXED == 0 && NT_REVOLUTE == 1 && NT_PRISMATIC == 2,
               "newton.tide's enums must mirror runtime.h");

#define BODY_FIELD(field, offset) \
    _Static_assert(offsetof(body_state, field) == (offset) && offsetof(nt_body, field) == (offset), \
                   "BodyState/nt_body field layout changed: " #field)
BODY_FIELD(entity, 0);
BODY_FIELD(type, 8);
BODY_FIELD(mass, 12);
BODY_FIELD(inertia, 16);
BODY_FIELD(center_of_mass, 28);
BODY_FIELD(position, 40);
BODY_FIELD(rotation, 52);
BODY_FIELD(velocity, 68);
BODY_FIELD(angular_velocity, 80);
BODY_FIELD(force, 92);
BODY_FIELD(torque, 104);
#undef BODY_FIELD

#define JOINT_FIELD(field, offset) \
    _Static_assert(offsetof(joint_state, field) == (offset) && offsetof(nt_joint, field) == (offset), \
                   "JointState/nt_joint field layout changed: " #field)
JOINT_FIELD(entity, 0);
JOINT_FIELD(parent, 8);
JOINT_FIELD(child, 16);
JOINT_FIELD(type, 24);
JOINT_FIELD(position, 28);
JOINT_FIELD(velocity, 32);
JOINT_FIELD(effort, 36);
JOINT_FIELD(effort_limit, 40);
JOINT_FIELD(damping, 44);
JOINT_FIELD(armature, 48);
JOINT_FIELD(limited, 52);
JOINT_FIELD(lower, 56);
JOINT_FIELD(upper, 60);
JOINT_FIELD(limit_stiffness, 64);
JOINT_FIELD(limit_damping, 68);
JOINT_FIELD(parent_position, 72);
JOINT_FIELD(parent_rotation, 84);
JOINT_FIELD(child_position, 100);
JOINT_FIELD(child_rotation, 112);
JOINT_FIELD(axis, 128);
JOINT_FIELD(applied_effort, 140);
JOINT_FIELD(friction, 144);
JOINT_FIELD(load, 148);
#undef JOINT_FIELD

#define COLLIDER_FIELD(field, offset) \
    _Static_assert(offsetof(collider_state, field) == (offset) && offsetof(nt_collider, field) == (offset), \
                   "ColliderState/nt_collider field layout changed: " #field)
COLLIDER_FIELD(entity, 0);
COLLIDER_FIELD(body, 8);
COLLIDER_FIELD(type, 16);
COLLIDER_FIELD(exclusion_group, 20);
COLLIDER_FIELD(radius, 24);
COLLIDER_FIELD(position, 28);
COLLIDER_FIELD(rotation, 40);
COLLIDER_FIELD(stiffness, 56);
COLLIDER_FIELD(damping, 60);
COLLIDER_FIELD(friction_damping, 64);
COLLIDER_FIELD(friction, 68);
COLLIDER_FIELD(contact_force, 72);
COLLIDER_FIELD(friction_model, 84);
#undef COLLIDER_FIELD

static uint64_t identity(const tide_entity entity)
{
    return (uint64_t)entity.index | (uint64_t)entity.generation << 32;
}

static tide_entity entity_of(const uint64_t value)
{
    return (tide_entity){(uint32_t)value, (uint32_t)(value >> 32)};
}

// Flatten read-only, then copy to owned native arrays before any mutation.
// Checked actual list counts bound every read; no Tide caller supplies lengths.
static void collect_bodies(const tide_list list, nt_body *bodies, const int32_t count)
{
    bool copied;
    const body_state *source = tide_list_flatten(list, sizeof(body_state), false, &copied);
    for (int32_t i = 0; i < count; ++i) {
        memcpy(&bodies[i], &source[i], sizeof(body_state));
        bodies[i].entity = identity(source[i].entity);
    }
    tide_list_unflatten(list, (void *)source, sizeof(body_state), false, copied);
}

static void collect_joints(const tide_list list, nt_joint *joints, const int32_t count)
{
    bool copied;
    const joint_state *source = tide_list_flatten(list, sizeof(joint_state), false, &copied);
    for (int32_t i = 0; i < count; ++i) {
        memcpy(&joints[i], &source[i], sizeof(joint_state));
        joints[i].entity = identity(source[i].entity);
        joints[i].parent = identity(source[i].parent);
        joints[i].child = identity(source[i].child);
    }
    tide_list_unflatten(list, (void *)source, sizeof(joint_state), false, copied);
}

static void collect_colliders(const tide_list list, nt_collider *colliders, const int32_t count)
{
    bool copied;
    const collider_state *source = tide_list_flatten(list, sizeof(collider_state), false, &copied);
    for (int32_t i = 0; i < count; ++i) {
        memcpy(&colliders[i], &source[i], sizeof(collider_state));
        colliders[i].entity = identity(source[i].entity);
        colliders[i].body = identity(source[i].body);
    }
    tide_list_unflatten(list, (void *)source, sizeof(collider_state), false, copied);
}

void nt_simulate(tide_rows *rows, physics *state, const simulation_time *time)
{
    (void)rows; // No component/filter parameters: called exactly once per tick.
    const int32_t body_count = tide_list_count(state->bodies);
    const int32_t joint_count = tide_list_count(state->joints);
    const int32_t collider_count = tide_list_count(state->colliders);
    if (body_count < 0 || joint_count < 0 || collider_count < 0 ||
        (uint64_t)(uint32_t)body_count * sizeof(nt_body) > SIZE_MAX ||
        (uint64_t)(uint32_t)joint_count * sizeof(nt_joint) > SIZE_MAX ||
        (uint64_t)(uint32_t)collider_count * sizeof(nt_collider) > SIZE_MAX) {
        state->status = NT_TOO_LARGE;
        return;
    }

    // calloc also clears nt_body's native-only tail padding. These are transient
    // owned values, not pointers into a world, a snapshot or flattened input.
    nt_body *bodies = body_count ? calloc((size_t)body_count, sizeof(nt_body)) : NULL;
    nt_joint *joints = joint_count ? calloc((size_t)joint_count, sizeof(nt_joint)) : NULL;
    nt_collider *colliders = collider_count ? calloc((size_t)collider_count, sizeof(nt_collider)) : NULL;
    if ((body_count && !bodies) || (joint_count && !joints) || (collider_count && !colliders)) {
        free(colliders);
        free(joints);
        free(bodies);
        state->status = NT_TOO_LARGE;
        return;
    }
    collect_bodies(state->bodies, bodies, body_count);
    collect_joints(state->joints, joints, joint_count);
    collect_colliders(state->colliders, colliders, collider_count);

    state->status = NT_OK;
    for (int32_t i = 0; i < joint_count; ++i) {
        // Validate before clamping, so NaN/infinity and negative limits are
        // rejected rather than hidden by forgiving min/max semantics.
        if (!isfinite(joints[i].effort) || !isfinite(joints[i].effort_limit) || joints[i].effort_limit < 0) {
            state->status = NT_INVALID_JOINT;
            break;
        }
        const float limit = joints[i].effort_limit;
        if (joints[i].effort > limit) joints[i].effort = limit;
        if (joints[i].effort < -limit) joints[i].effort = -limit;
    }
    if (state->status == NT_OK) {
        const float gravity[3] = {state->gravity.x, state->gravity.y, state->gravity.z};
        state->status = nt_step(bodies, (uint32_t)body_count, joints, (uint32_t)joint_count,
                                colliders, (uint32_t)collider_count,
                                gravity, time->dt, state->substeps);
    }
    if (state->status == NT_OK) {
        // Inputs have all been released before COW writes can move any pages.
        // Both buffers publish only after the entire native step succeeded.
        tide_list_prepare(&state->bodies, sizeof(body_state), TIDE_IN_MATCH);
        tide_list_prepare(&state->joints, sizeof(joint_state), TIDE_IN_MATCH);
        tide_list_prepare(&state->colliders, sizeof(collider_state), TIDE_IN_MATCH);
        for (int32_t i = 0; i < body_count; ++i) {
            body_state result;
            memcpy(&result, &bodies[i], sizeof(result));
            result.entity = entity_of(bodies[i].entity);
            memcpy(tide_list_at_mut(state->bodies, i, sizeof(result)), &result, sizeof(result));
        }
        for (int32_t i = 0; i < joint_count; ++i) {
            joint_state result;
            memcpy(&result, &joints[i], sizeof(result));
            result.entity = entity_of(joints[i].entity);
            result.parent = entity_of(joints[i].parent);
            result.child = entity_of(joints[i].child);
            memcpy(tide_list_at_mut(state->joints, i, sizeof(result)), &result, sizeof(result));
        }
        for (int32_t i = 0; i < collider_count; ++i) {
            collider_state result;
            memcpy(&result, &colliders[i], sizeof(result));
            result.entity = entity_of(colliders[i].entity);
            result.body = entity_of(colliders[i].body);
            memcpy(tide_list_at_mut(state->colliders, i, sizeof(result)), &result, sizeof(result));
        }
    }
    free(colliders);
    free(joints);
    free(bodies);
}
