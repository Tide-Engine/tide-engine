// Private, value-only boundary between Tide's rows adapter and Newton's
// statically compiled CPU kernels. No pointer is retained after nt_step.
#ifndef TIDE_NEWTON_RUNTIME_H
#define TIDE_NEWTON_RUNTIME_H

#include <stdint.h>

enum nt_status {
    NT_OK,
    NT_INVALID_BODY,
    NT_INVALID_JOINT,
    NT_INVALID_TOPOLOGY,
    NT_TOO_LARGE,
    NT_UNSTABLE,
    NT_INVALID_COLLIDER
};

enum nt_body_type { NT_STATIC, NT_DYNAMIC };
enum nt_joint_type { NT_FIXED, NT_REVOLUTE, NT_PRISMATIC };
enum nt_collider_type { NT_SPHERE, NT_PLANE };

typedef struct nt_body {
    uint64_t entity;
    int32_t type;
    float mass;
    float inertia[3];
    float center_of_mass[3];
    float position[3];
    float rotation[4];
    float velocity[3];
    float angular_velocity[3];
    float force[3];
    float torque[3];
} nt_body;

typedef struct nt_joint {
    uint64_t entity;
    uint64_t parent;
    uint64_t child;
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
    float parent_position[3];
    float parent_rotation[4];
    float child_position[3];
    float child_rotation[4];
    float axis[3];
    float applied_effort;
} nt_joint;

// Compound colliders are separate records referencing a body. Position and
// rotation are relative to its origin. A plane's normal is local +Y and its
// body must be static. Matching nonzero exclusion groups suppress collisions.
typedef struct nt_collider {
    uint64_t entity;
    uint64_t body;
    int32_t type;
    int32_t exclusion_group;
    float radius;
    float position[3];
    float rotation[4];
    float stiffness;
    float damping;
    float friction_damping;
    float friction;
    // Successful tick's mean world contact force, including friction.
    float contact_force[3];
} nt_collider;

#ifdef __cplusplus
extern "C" {
#endif

// All input/model values are checked before the upstream kernels run. Outputs
// are published only after the entire step succeeds. Scratch is reconstructed
// on every call; these value records are the complete persistent state.
int nt_step(nt_body *bodies, uint32_t body_count, nt_joint *joints, uint32_t joint_count,
            nt_collider *colliders, uint32_t collider_count,
            const float gravity[3], float dt, int32_t substeps);

#ifdef __cplusplus
}
#endif

#endif
