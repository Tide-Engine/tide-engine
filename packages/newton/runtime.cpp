// Value-only adapter for the pinned, statically exported Newton CPU kernels.
#include "runtime.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernels/featherstone.inl"
#undef float
#undef int

namespace {

using Vec3 = wp::vec_t<3, float>;
using Twist = wp::vec_t<6, float>;
using Quat = wp::quat_t<float>;
using Pose = wp::transform_t<float>;
using Inertia = wp::mat_t<3, 3, float>;
using SpatialInertia = wp::mat_t<6, 6, float>;

// Warp uses signed 32-bit element offsets, including within dense matrices.
// The total allocation ceiling also leaves address space for the caller on wasm32.
constexpr uint64_t scratch_limit = INT_MAX;
enum { newton_prismatic = 0, newton_revolute = 1, newton_fixed = 3, newton_free = 4 };

template <class T> struct Buffer {
    T *data = nullptr;
    int count = 0;

    Buffer() = default;
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;
    ~Buffer() { free(data); }

    bool allocate(uint64_t n, uint64_t &budget)
    {
        if (n > INT_MAX || n > SIZE_MAX / sizeof(T) ||
            n > (scratch_limit - budget) / sizeof(T)) return false;
        const uint64_t bytes = n * sizeof(T);
        // A non-null zero-length view is useful to kernels with zero-DOF articulations.
        data = static_cast<T *>(calloc(n ? static_cast<size_t>(n) : 1, sizeof(T)));
        if (!data) return false;
        count = static_cast<int>(n);
        budget += bytes ? bytes : sizeof(T);
        return budget <= scratch_limit;
    }

    T &operator[](int i) { return data[i]; }
    const T &operator[](int i) const { return data[i]; }
    operator wp::array_t<T>() const { return wp::array_t<T>(data, count); }
    wp::array_t<T> matrix(int rows, int cols) const { return wp::array_t<T>(data, rows, cols); }
    void clear() { memset(data, 0, static_cast<size_t>(count) * sizeof(T)); }
    void swap(Buffer &other)
    {
        T *saved = data;
        data = other.data;
        other.data = saved;
    }
};

bool finite(float x)
{
    uint32_t bits;
    memcpy(&bits, &x, sizeof(bits));
    return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000);
}

bool finite_values(const float *v, int n)
{
    for (int i = 0; i < n; ++i) if (!finite(v[i])) return false;
    return true;
}

template <class T> bool finite_buffer(const Buffer<T> &buffer)
{
    static_assert(sizeof(T) % sizeof(float) == 0);
    // Copy words instead of aliasing Warp aggregates as float arrays.
    const unsigned char *bytes = reinterpret_cast<const unsigned char *>(buffer.data);
    const size_t count = static_cast<size_t>(buffer.count) * sizeof(T) / sizeof(float);
    for (size_t i = 0; i < count; ++i) {
        float x;
        memcpy(&x, bytes + i * sizeof(float), sizeof(x));
        if (!finite(x)) return false;
    }
    return true;
}

bool unit_quaternion(const float *q)
{
    if (!finite_values(q, 4)) return false;
    // Double only validates: all simulation arithmetic remains upstream binary32.
    double norm = 0.0;
    for (int i = 0; i < 4; ++i) norm += static_cast<double>(q[i]) * q[i];
    return norm >= 0.9999 && norm <= 1.0001;
}

bool unit_poses(const Buffer<Pose> &buffer)
{
    for (int i = 0; i < buffer.count; ++i) {
        float values[4];
        for (int k = 0; k < 4; ++k) values[k] = buffer[i].q[k];
        if (!unit_quaternion(values)) return false;
    }
    return true;
}

Vec3 vector3(const float *v) { return Vec3(v[0], v[1], v[2]); }

Pose pose(const float *p, const float *q)
{
    return Pose(vector3(p), wp::normalize(Quat(q[0], q[1], q[2], q[3])));
}

bool axis_valid(const float *v)
{
    return finite_values(v, 3) && (v[0] != 0.0f || v[1] != 0.0f || v[2] != 0.0f);
}

Vec3 unit_axis(const float *v)
{
    float scale = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float magnitude = v[i] < 0.0f ? -v[i] : v[i];
        if (magnitude > scale) scale = magnitude;
    }
    // Scale first, so finite nonzero axes are safe even at float's extremes.
    const Vec3 scaled(v[0] / scale, v[1] / scale, v[2] / scale);
    return wp::normalize(scaled);
}

struct EntitySlot { uint64_t entity; int index; };

struct EntityMap {
    Buffer<EntitySlot> slots;
    uint64_t mask = 0;

    bool allocate(uint32_t count, uint64_t &budget)
    {
        uint64_t capacity = 2;
        while (capacity < static_cast<uint64_t>(count) * 2) capacity *= 2;
        mask = capacity - 1;
        return slots.allocate(capacity, budget);
    }

    uint64_t hash(uint64_t id) const
    {
        id ^= id >> 30;
        id *= UINT64_C(0xbf58476d1ce4e5b9);
        id ^= id >> 27;
        id *= UINT64_C(0x94d049bb133111eb);
        return (id ^ (id >> 31)) & mask;
    }

    bool insert(uint64_t id, int index)
    {
        uint64_t slot = hash(id);
        while (slots.data[slot].entity) {
            if (slots.data[slot].entity == id) return false;
            slot = (slot + 1) & mask;
        }
        slots.data[slot] = EntitySlot{id, index};
        return true;
    }

    int find(uint64_t id) const
    {
        uint64_t slot = hash(id);
        while (slots.data[slot].entity) {
            if (slots.data[slot].entity == id) return slots.data[slot].index;
            slot = (slot + 1) & mask;
        }
        return -1;
    }
};

bool valid_body(const nt_body &b)
{
    if (!b.entity || (b.type != NT_STATIC && b.type != NT_DYNAMIC) ||
        !finite(b.mass) || !finite_values(b.inertia, 3) ||
        !finite_values(b.center_of_mass, 3) || !finite_values(b.position, 3) ||
        !unit_quaternion(b.rotation) || !finite_values(b.velocity, 3) ||
        !finite_values(b.angular_velocity, 3) || !finite_values(b.force, 3) ||
        !finite_values(b.torque, 3)) return false;
    if (b.type == NT_DYNAMIC) {
        if (b.mass <= 0.0f) return false;
        for (int i = 0; i < 3; ++i) if (b.inertia[i] <= 0.0f) return false;
    }
    return true;
}

bool valid_joint(const nt_joint &j)
{
    if (!j.entity || j.type < NT_FIXED || j.type > NT_PRISMATIC ||
        (j.limited != 0 && j.limited != 1) ||
        !finite(j.position) || !finite(j.velocity) || !finite(j.effort) ||
        !finite(j.effort_limit) || j.effort_limit < 0.0f ||
        !finite(j.damping) || j.damping < 0.0f ||
        !finite(j.armature) || j.armature < 0.0f ||
        !finite(j.lower) || !finite(j.upper) || (j.limited && j.lower > j.upper) ||
        !finite(j.limit_stiffness) || j.limit_stiffness < 0.0f ||
        !finite(j.limit_damping) || j.limit_damping < 0.0f ||
        !finite_values(j.parent_position, 3) || !unit_quaternion(j.parent_rotation) ||
        !finite_values(j.child_position, 3) || !unit_quaternion(j.child_rotation) ||
        !finite_values(j.axis, 3)) return false;
    return j.type == NT_FIXED || axis_valid(j.axis);
}

wp::launch_bounds_t<1> launch_bounds(int n)
{
    return wp::launch_bounds_t<1>{{static_cast<uint32_t>(n)}, static_cast<size_t>(n), 1};
}

struct Model {
    Buffer<int> incoming, parent, first_child, next_child, order, body_joint;
    Buffer<int> art_start, art_end, art_J, art_M, art_H, art_rows, art_dofs, art_dof_start;
    Buffer<int> type, joint_parent, child, ancestor, q_start, qd_start, dof_dim, world;
    Buffer<Vec3> com, axis, solve_origin;
    Buffer<Pose> body_q, body_q_com, X_com, X_p, X_c;
    Buffer<Inertia> inertia;
    Buffer<SpatialInertia> I_m, I_s, I_joint;
    Buffer<Twist> body_qd, S, v_s, f_s, a_s, ft_s, body_force;
    Buffer<float> mass, q, q_new, qd, qd_internal, qd_new, effort, effort_internal;
    Buffer<float> damping, armature, lower, upper, limit_ke, limit_kd, zero;
    Buffer<float> tau, qdd, solve_tmp, J, M, P, H, L;

    int articulations = 0;

    int build(const nt_body *bodies, int n, const nt_joint *joints, int joint_count,
              uint64_t &budget)
    {
        EntityMap body_ids, joint_ids;
        if (!body_ids.allocate(static_cast<uint32_t>(n), budget) ||
            !joint_ids.allocate(static_cast<uint32_t>(joint_count), budget)) return NT_TOO_LARGE;
#define NT_ALLOC(buffer, count) if (!(buffer).allocate((count), budget)) return NT_TOO_LARGE
        NT_ALLOC(incoming, n);
        NT_ALLOC(parent, n);
        NT_ALLOC(first_child, n);
        NT_ALLOC(next_child, n);
        NT_ALLOC(order, n);
        NT_ALLOC(body_joint, n);
        NT_ALLOC(art_start, n);
        NT_ALLOC(art_end, n);
        for (int i = 0; i < n; ++i) {
            if (!valid_body(bodies[i]) || !body_ids.insert(bodies[i].entity, i)) return NT_INVALID_BODY;
            incoming[i] = parent[i] = first_child[i] = next_child[i] = -1;
        }
        for (int i = 0; i < joint_count; ++i) {
            const nt_joint &j = joints[i];
            if (!valid_joint(j) || !joint_ids.insert(j.entity, i)) return NT_INVALID_JOINT;
            const int c = body_ids.find(j.child);
            const int p = j.parent ? body_ids.find(j.parent) : -1;
            if (c < 0 || (j.parent && p < 0)) return NT_INVALID_JOINT;
            if (bodies[c].type == NT_STATIC) return NT_INVALID_JOINT;
            if (incoming[c] >= 0 || c == p) return NT_INVALID_TOPOLOGY;
            incoming[c] = i;
            parent[c] = p;
        }
        // Reverse insertion gives each body's children in original body-input order.
        for (int i = n; i-- > 0;) {
            const int p = parent[i];
            if (p >= 0) {
                next_child[i] = first_child[p];
                first_child[p] = i;
            }
        }
        int placed = 0;
        for (int root = 0; root < n; ++root) {
            if (parent[root] >= 0) continue;
            const int start = placed;
            order[placed++] = root;
            // Each articulation is contiguous, breadth first, with stable sibling order.
            for (int cursor = start; cursor < placed; ++cursor) {
                for (int c = first_child[order[cursor]]; c >= 0; c = next_child[c]) {
                    if (placed == n) return NT_INVALID_TOPOLOGY;
                    order[placed++] = c;
                }
            }
            art_start[articulations] = start;
            art_end[articulations++] = placed;
        }
        // Every acyclic, one-inbound graph is reachable from exactly one root.
        if (placed != n) return NT_INVALID_TOPOLOGY;
        for (int i = 0; i < n; ++i) body_joint[order[i]] = i;

        NT_ALLOC(type, n);
        NT_ALLOC(joint_parent, n);
        NT_ALLOC(child, n);
        NT_ALLOC(ancestor, n);
        NT_ALLOC(q_start, static_cast<uint64_t>(n) + 1);
        NT_ALLOC(qd_start, static_cast<uint64_t>(n) + 1);
        NT_ALLOC(dof_dim, static_cast<uint64_t>(n) * 2);
        int nq = 0, nd = 0;
        for (int i = 0; i < n; ++i) {
            const int b = order[i];
            const int j = incoming[b];
            const int t = j >= 0 ? (joints[j].type == NT_FIXED ? newton_fixed :
                joints[j].type == NT_REVOLUTE ? newton_revolute : newton_prismatic) :
                (bodies[b].type == NT_STATIC ? newton_fixed : newton_free);
            type[i] = t;
            child[i] = b;
            joint_parent[i] = parent[b];
            ancestor[i] = parent[b] >= 0 ? body_joint[parent[b]] : -1;
            q_start[i] = nq;
            qd_start[i] = nd;
            nq += t == newton_free ? 7 : t == newton_fixed ? 0 : 1;
            nd += t == newton_free ? 6 : t == newton_fixed ? 0 : 1;
            dof_dim[2 * i] = t == newton_free ? 3 : t == newton_prismatic ? 1 : 0;
            dof_dim[2 * i + 1] = t == newton_free ? 3 : t == newton_revolute ? 1 : 0;
        }
        q_start[n] = nq;
        qd_start[n] = nd;
        NT_ALLOC(art_J, articulations);
        NT_ALLOC(art_M, articulations);
        NT_ALLOC(art_H, articulations);
        NT_ALLOC(art_rows, articulations);
        NT_ALLOC(art_dofs, articulations);
        NT_ALLOC(art_dof_start, articulations);
        uint64_t nj = 0, nm = 0, nh = 0;
        for (int a = 0; a < articulations; ++a) {
            const uint64_t links = static_cast<uint64_t>(art_end[a] - art_start[a]);
            const uint64_t dofs = static_cast<uint64_t>(qd_start[art_end[a]] - qd_start[art_start[a]]);
            art_J[a] = static_cast<int>(nj);
            art_M[a] = static_cast<int>(nm);
            art_H[a] = static_cast<int>(nh);
            nj += 6 * links * dofs;
            nm += 36 * links * links;
            nh += dofs * dofs;
            if (nj > INT_MAX || nm > INT_MAX || nh > INT_MAX) return NT_TOO_LARGE;
            art_rows[a] = static_cast<int>(6 * links);
            art_dofs[a] = static_cast<int>(dofs);
            art_dof_start[a] = qd_start[art_start[a]];
        }
        NT_ALLOC(com, n); NT_ALLOC(axis, nd); NT_ALLOC(solve_origin, n);
        NT_ALLOC(body_q, n); NT_ALLOC(body_q_com, n); NT_ALLOC(X_com, n);
        NT_ALLOC(X_p, n); NT_ALLOC(X_c, n); NT_ALLOC(inertia, n);
        NT_ALLOC(I_m, n); NT_ALLOC(I_s, n); NT_ALLOC(I_joint, n);
        NT_ALLOC(body_qd, n); NT_ALLOC(S, nd); NT_ALLOC(v_s, n); NT_ALLOC(f_s, n);
        NT_ALLOC(a_s, n); NT_ALLOC(ft_s, n); NT_ALLOC(body_force, n); NT_ALLOC(world, n);
        NT_ALLOC(mass, n); NT_ALLOC(q, nq); NT_ALLOC(q_new, nq); NT_ALLOC(qd, nd);
        NT_ALLOC(qd_internal, nd); NT_ALLOC(qd_new, nd); NT_ALLOC(effort, nd);
        NT_ALLOC(effort_internal, nd); NT_ALLOC(damping, nd); NT_ALLOC(armature, nd);
        NT_ALLOC(lower, nd); NT_ALLOC(upper, nd); NT_ALLOC(limit_ke, nd); NT_ALLOC(limit_kd, nd);
        NT_ALLOC(zero, nd); NT_ALLOC(tau, nd); NT_ALLOC(qdd, nd); NT_ALLOC(solve_tmp, nd);
        NT_ALLOC(J, nj); NT_ALLOC(M, nm); NT_ALLOC(P, nj); NT_ALLOC(H, nh); NT_ALLOC(L, nh);
#undef NT_ALLOC
        for (int b = 0; b < n; ++b) {
            const nt_body &input = bodies[b];
            body_q[b] = pose(input.position, input.rotation);
            com[b] = vector3(input.center_of_mass);
            // Static roots contribute no inertia, wrench or motion.
            if (input.type == NT_DYNAMIC) {
                mass[b] = input.mass;
                for (int k = 0; k < 3; ++k) inertia[b].data[k][k] = input.inertia[k];
            }
        }
        for (int i = 0; i < n; ++i) {
            const int b = child[i];
            const int j = incoming[b];
            const int qc = q_start[i], dc = qd_start[i];
            X_p[i] = wp::transform_identity<float>();
            X_c[i] = wp::transform_identity<float>();
            if (j < 0) {
                if (type[i] == newton_fixed) X_p[i] = body_q[b];
                else {
                    for (int k = 0; k < 3; ++k) {
                        q[qc + k] = body_q[b].p[k];
                        qd[dc + k] = bodies[b].velocity[k];
                        qd[dc + k + 3] = bodies[b].angular_velocity[k];
                    }
                    for (int k = 0; k < 4; ++k) q[qc + k + 3] = body_q[b].q[k];
                }
                continue;
            }
            const nt_joint &input = joints[j];
            X_p[i] = pose(input.parent_position, input.parent_rotation);
            X_c[i] = pose(input.child_position, input.child_rotation);
            if (type[i] == newton_fixed) continue;
            q[qc] = input.position;
            qd[dc] = input.velocity;
            axis[dc] = unit_axis(input.axis);
            float applied = input.effort;
            if (applied > input.effort_limit) applied = input.effort_limit;
            if (applied < -input.effort_limit) applied = -input.effort_limit;
            effort[dc] = applied;
            damping[dc] = input.damping;
            armature[dc] = input.armature;
            if (input.limited) {
                lower[dc] = input.lower;
                upper[dc] = input.upper;
                limit_ke[dc] = input.limit_stiffness;
                limit_kd[dc] = input.limit_damping;
            }
        }
        return NT_OK;
    }

    void forward_poses(wp::launch_bounds_t<1> &bounds)
    {
        wp_args_eval_rigid_fk args = {
            art_start, art_end, type, joint_parent, child, q_start, qd_start, q,
            X_p, X_c, X_com, axis, dof_dim.matrix(type.count, 2), body_q, body_q_com
        };
        eval_rigid_fk_cpu_forward(&bounds, &args);
    }

    bool step(const nt_body *bodies, const float *gravity, float dt)
    {
        auto body_bounds = launch_bounds(body_q.count);
        auto joint_bounds = launch_bounds(type.count);
        auto art_bounds = launch_bounds(articulations);
        wp_args_compute_spatial_inertia inertia_args = {inertia, mass, I_m};
        compute_spatial_inertia_cpu_forward(&body_bounds, &inertia_args);
        wp_args_compute_com_transforms com_args = {com, X_com};
        compute_com_transforms_cpu_forward(&body_bounds, &com_args);
        forward_poses(art_bounds);
        if (!finite_buffer(body_q) || !finite_buffer(body_q_com) || !unit_poses(body_q)) return false;
        wp_args_convert_free_distance_joint_qd_public_to_internal speed_args = {
            type, joint_parent, child, qd_start, X_p, body_q, com, qd, qd_internal
        };
        convert_free_distance_joint_qd_public_to_internal_cpu_forward(&joint_bounds, &speed_args);
        wp_args_convert_free_distance_joint_f_public_to_internal effort_args = {
            type, qd_start, effort, effort_internal
        };
        convert_free_distance_joint_f_public_to_internal_cpu_forward(&joint_bounds, &effort_args);
        if (!finite_buffer(qd_internal)) return false;
        Vec3 acceleration = vector3(gravity);
        f_s.clear();
        wp_args_eval_rigid_id id_args = {
            {}, art_start, art_end, type, joint_parent, child, q_start, qd_start,
            q, qd_internal, {}, axis, dof_dim.matrix(type.count, 2), I_m, body_q,
            body_q_com, X_p, world, wp::array_t<Vec3>(&acceleration, 1),
            body_qd, S, solve_origin, I_s, v_s, f_s, a_s
        };
        eval_rigid_id_cpu_forward(&art_bounds, &id_args);
        if (!finite_buffer(body_qd) || !finite_buffer(S) || !finite_buffer(I_s) ||
            !finite_buffer(v_s) || !finite_buffer(f_s) || !finite_buffer(a_s)) return false;
        // eval_rigid_tau overwrites the wrench array. Restore held public COM/world
        // inputs for every substep rather than reusing its internal shifted values.
        for (int b = 0; b < body_q.count; ++b) {
            for (int k = 0; k < 3; ++k) {
                body_force[b][k] = bodies[b].type == NT_DYNAMIC ? bodies[b].force[k] : 0.0f;
                body_force[b][k + 3] = bodies[b].type == NT_DYNAMIC ? bodies[b].torque[k] : 0.0f;
            }
        }
        ft_s.clear();
        wp_args_eval_rigid_tau tau_args = {
            {}, art_start, art_end, type, joint_parent, child, q_start, qd_start,
            qd_start, dof_dim.matrix(type.count, 2), zero, zero, q, qd_internal,
            effort_internal, zero, zero, lower, upper, limit_ke, limit_kd, damping,
            S, body_q_com, solve_origin, f_s, body_force, ft_s, tau
        };
        eval_rigid_tau_cpu_forward(&art_bounds, &tau_args);
        if (!finite_buffer(body_force) || !finite_buffer(ft_s) || !finite_buffer(tau)) return false;
        wp_args_eval_rigid_jacobian jacobian_args = {
            art_start, art_end, art_J, ancestor, qd_start, S, J
        };
        eval_rigid_jacobian_cpu_forward(&art_bounds, &jacobian_args);
        // Upstream spatial_mass indexes its input by joint, while inverse dynamics
        // indexes I_s by body. Gather explicitly: input body order is unrestricted.
        for (int i = 0; i < type.count; ++i) I_joint[i] = I_s[child[i]];
        wp_args_eval_rigid_mass mass_args = {art_start, art_end, art_M, I_joint, M};
        eval_rigid_mass_cpu_forward(&art_bounds, &mass_args);
        wp_args_eval_dense_gemm_batched mj_args = {
            art_rows, art_dofs, art_rows, false, false, art_M, art_J, art_J, M, J, P
        };
        eval_dense_gemm_batched_cpu_forward(&art_bounds, &mj_args);
        wp_args_eval_dense_gemm_batched jp_args = {
            art_dofs, art_dofs, art_rows, true, false, art_J, art_J, art_H, J, P, H
        };
        eval_dense_gemm_batched_cpu_forward(&art_bounds, &jp_args);
        if (!finite_buffer(J) || !finite_buffer(M) || !finite_buffer(P) || !finite_buffer(H)) return false;
        wp_args_eval_dense_cholesky_batched factor_args = {art_H, art_dofs, art_dof_start, H, armature, L};
        eval_dense_cholesky_batched_cpu_forward(&art_bounds, &factor_args);
        if (!finite_buffer(L)) return false;
        for (int a = 0; a < articulations; ++a) {
            const int dim = art_dofs[a];
            for (int i = 0; i < dim; ++i) if (L[art_H[a] + i * dim + i] <= 0.0f) return false;
        }
        qdd.clear();
        wp_args_eval_dense_solve_batched solve_args = {art_H, art_dofs, art_dof_start, H, L, tau, qdd, solve_tmp};
        eval_dense_solve_batched_cpu_forward(&art_bounds, &solve_args);
        if (!finite_buffer(qdd)) return false;
        wp_args_integrate_generalized_joints integrate_args = {
            type, joint_parent, child, q_start, qd_start, dof_dim.matrix(type.count, 2),
            X_c, com, q, qd_internal, qdd, dt, q_new, qd_new
        };
        integrate_generalized_joints_cpu_forward(&joint_bounds, &integrate_args);
        if (!finite_buffer(q_new) || !finite_buffer(qd_new)) return false;
        q.swap(q_new);
        wp_args_eval_articulation_fk_with_velocity_conversion fk_args = {
            art_start, art_end, articulations, {}, {}, q, qd_new, q_start, qd_start,
            type, joint_parent, child, X_p, X_c, axis, dof_dim.matrix(type.count, 2), com, body_q, body_qd
        };
        eval_articulation_fk_with_velocity_conversion_cpu_forward(&art_bounds, &fk_args);
        if (!finite_buffer(body_q) || !finite_buffer(body_qd) || !unit_poses(body_q)) return false;
        wp_args_convert_free_distance_joint_qd_internal_to_public public_args = {
            type, joint_parent, child, qd_start, X_p, body_q, com, qd_new, qd
        };
        convert_free_distance_joint_qd_internal_to_public_cpu_forward(&joint_bounds, &public_args);
        return finite_buffer(qd);
    }

    void publish(nt_body *bodies, nt_joint *joints)
    {
        for (int b = 0; b < body_q.count; ++b) {
            if (bodies[b].type == NT_STATIC) continue;
            for (int k = 0; k < 3; ++k) {
                bodies[b].position[k] = body_q[b].p[k];
                bodies[b].velocity[k] = body_qd[b][k];
                bodies[b].angular_velocity[k] = body_qd[b][k + 3];
            }
            for (int k = 0; k < 4; ++k) bodies[b].rotation[k] = body_q[b].q[k];
        }
        for (int i = 0; i < type.count; ++i) {
            const int j = incoming[child[i]];
            if (j < 0) continue;
            joints[j].position = type[i] == newton_fixed ? 0.0f : q[q_start[i]];
            joints[j].velocity = type[i] == newton_fixed ? 0.0f : qd[qd_start[i]];
            joints[j].applied_effort = type[i] == newton_fixed ? 0.0f : effort[qd_start[i]];
        }
    }
};

} // namespace

extern "C" int nt_step(nt_body *bodies, uint32_t body_count, nt_joint *joints,
                       uint32_t joint_count, const float gravity[3], float dt, int32_t substeps)
{
    if (!gravity || !finite_values(gravity, 3) || !finite(dt) || dt <= 0.0f || substeps <= 0)
        return NT_UNSTABLE;
    // nq <= 7*n, nd <= 6*n; n+1 sentinels and all kernel int arithmetic must fit.
    if (body_count > static_cast<uint32_t>((INT_MAX - 1) / 7) || joint_count > INT_MAX)
        return NT_TOO_LARGE;
    if (body_count && !bodies) return NT_INVALID_BODY;
    if (joint_count && !joints) return NT_INVALID_JOINT;
    if (joint_count > body_count) return NT_INVALID_TOPOLOGY;
    const float substep_dt = dt / static_cast<float>(substeps);
    if (!finite(substep_dt) || substep_dt <= 0.0f) return NT_UNSTABLE;
    if (!body_count) return NT_OK;
    uint64_t budget = 0;
    Model model;
    const int status = model.build(bodies, static_cast<int>(body_count), joints,
                                   static_cast<int>(joint_count), budget);
    if (status != NT_OK) return status;
    for (int i = 0; i < substeps; ++i) {
        if (!model.step(bodies, gravity, substep_dt)) return NT_UNSTABLE;
    }
    model.publish(bodies, joints);
    return NT_OK;
}
