"""Pin primitive-contact traces from unmodified upstream Newton on the CPU.

From the repository root, using the pinned research environment:
    build/research-newton/venv/Scripts/python.exe -B packages/newton/test/contact_oracle.py --write
    build/research-newton/venv/Scripts/python.exe -B packages/newton/test/contact_oracle.py --check

Without either switch, print the C header. --newton-root and --warp-root have
the same meaning as in oracle.py. This generator imports its revision guards
and C formatting helpers, never Tide's runtime, exports, or math bindings.
"""

import argparse
import contextlib
import math
import pathlib
import subprocess
import sys

from oracle import NEWTON_REVISION, WARP_REVISION, WARP_VERSION, c_array, c_float, check_source


SAMPLE_STEPS = (1, 8, 32)
FRICTION_SMOOTHING = 0.01
STATE_TOLERANCE = 5.0e-5
FORCE_TOLERANCE = 2.0e-3
BODY_FIELDS = (
    "mass", "inertia", "center_of_mass", "position", "rotation", "velocity",
    "angular_velocity", "force", "torque",
)
SHAPE_FIELDS = (
    "radius", "position", "rotation", "stiffness", "damping", "friction_damping", "friction",
)
SAMPLE_FIELDS = (
    "body_position", "body_rotation", "body_com_position", "body_linear_velocity",
    "body_angular_velocity", "contact_force",
)


def make_cases(np, wp):
    """Author float32 inputs; geometry placement uses stock Warp transforms."""
    def floats(value):
        return np.asarray(value, dtype=np.float32)

    def rotation(axis=(0.0, 0.0, 1.0), angle=0.0):
        axis_length = math.sqrt(sum(x * x for x in axis))
        s = math.sin(angle * 0.5) / axis_length
        q = [x * s for x in axis] + [math.cos(angle * 0.5)]
        return floats(q)

    def body(type=1, mass=1.0, inertia=(0.04, 0.05, 0.06), center_of_mass=(0, 0, 0),
             position=(0, 0, 0), quaternion=None, velocity=(0, 0, 0),
             angular_velocity=(0, 0, 0), force=(0, 0, 0), torque=(0, 0, 0)):
        values = dict(mass=mass, inertia=inertia, center_of_mass=center_of_mass,
                      position=position, rotation=rotation() if quaternion is None else quaternion,
                      velocity=velocity, angular_velocity=angular_velocity, force=force, torque=torque)
        return dict(type=type, **{key: floats(value) for key, value in values.items()})

    def shape(body, type=0, radius=0.2, position=(0, 0, 0), quaternion=None,
              stiffness=1400.0, damping=4.0, friction_damping=12.0, friction=0.45):
        values = dict(radius=radius, position=position,
                      rotation=rotation() if quaternion is None else quaternion,
                      stiffness=stiffness, damping=damping,
                      friction_damping=friction_damping, friction=friction)
        return dict(body=body, type=type, **{key: floats(value) for key, value in values.items()})

    def transform(record):
        return wp.transform(wp.vec3(*record["position"]), wp.quat(*record["rotation"]))

    def place_sphere(record, collider, center):
        offset = wp.quat_rotate(wp.quat(*record["rotation"]), wp.vec3(*collider["position"]))
        record["position"] = floats(center) - floats(offset)

    sphere = body(mass=1.3, inertia=(0.055, 0.08, 0.065), center_of_mass=(-0.025, 0.015, 0.02),
                  quaternion=rotation((0.3, 0.7, -0.2), 0.43), velocity=(0.55, -0.2, 0.28),
                  angular_velocity=(0.4, -0.35, 0.6), force=(0.3, -0.7, 0.2),
                  torque=(0.04, -0.03, 0.02))
    floor = body(type=0, mass=0, inertia=(0, 0, 0), center_of_mass=(0.04, -0.02, 0.01),
                 position=(0.15, -0.1, -0.12), quaternion=rotation((0.2, 0.1, 0.9), 0.17))
    sphere_shape = shape(0, radius=0.22, position=(0.07, -0.03, 0.045),
                         quaternion=rotation((1, 2, 3), 0.37))
    floor_shape = shape(1, type=1, radius=0, position=(0.03, 0.08, 0.02),
                        quaternion=rotation((1, 0, 1), 0.14),
                        stiffness=2200, damping=8, friction_damping=20, friction=0.85)
    plane_world = wp.transform_multiply(transform(floor), transform(floor_shape))
    # +Y is the package plane normal; start 10 mm into it with tangential offset.
    center = wp.transform_point(plane_world, wp.vec3(0.12, 0.21, 0.04))
    place_sphere(sphere, sphere_shape, center)

    first = body(mass=1.2, inertia=(0.045, 0.065, 0.055), center_of_mass=(-0.03, 0.025, 0.015),
                 quaternion=rotation((0.2, 0.3, 0.1), 0.5), velocity=(0.18, 0.42, -0.16),
                 angular_velocity=(0.3, -0.2, 0.7), force=(0.4, -0.15, 0.25),
                 torque=(0.03, -0.02, 0.04))
    second = body(mass=0.8, inertia=(0.025, 0.04, 0.035), center_of_mass=(0.025, -0.02, -0.015),
                  quaternion=rotation((0.1, -0.4, 0.2), -0.4), velocity=(-0.12, -0.21, 0.23),
                  angular_velocity=(-0.4, 0.5, -0.25), force=(-0.2, 0.3, -0.1),
                  torque=(-0.025, 0.035, -0.015))
    first_shape = shape(0, position=(0.045, -0.025, 0.03), quaternion=rotation((0, 1, 0), 0.6),
                        stiffness=1800, damping=5, friction_damping=18, friction=0.5)
    second_shape = shape(1, radius=0.18, position=(-0.035, 0.04, -0.02),
                         quaternion=rotation((1, 0, 0), -0.3),
                         stiffness=2600, damping=9, friction_damping=26, friction=0.9)
    place_sphere(first, first_shape, (-0.181, 0.31, 0.05))
    place_sphere(second, second_shape, (0.181, 0.35, 0.02))

    low_sphere = body(position=(0, 0.194, 0), velocity=(0.003, -0.08, 0.001))
    low_floor = body(type=0, mass=0, inertia=(0, 0, 0))
    # Small mu keeps the Huber-saturated low-slip response stable at this dt.
    # Replacing norm_huber with length then changes both forces and the rollout.
    low_shapes = [shape(0, stiffness=1200, damping=3, friction_damping=60, friction=3.0e-6),
                  shape(1, type=1, radius=0, stiffness=2400, damping=7,
                        friction_damping=100, friction=7.0e-6)]

    cases = []
    for name, bodies, shapes, gravity, substeps in (
        ("offset_sphere_tilted_plane_high_slip", [sphere, floor], [sphere_shape, floor_shape],
         (0, -9.81, 0), 2),
        ("dynamic_sphere_pair_high_slip", [first, second], [first_shape, second_shape],
         (0, 0, 0), 3),
        ("sphere_plane_low_slip_drop", [low_sphere, low_floor], low_shapes, (0, -9.81, 0), 2),
    ):
        cases.append(dict(name=name, body_count=2, shape_count=2, dt=np.float32(1.0 / 240.0),
                          substeps=substeps, gravity=floats(gravity), bodies=bodies, shapes=shapes))
    return cases


def run_case(case_index, case, newton, np, wp):
    builder = newton.ModelBuilder(gravity=wp.vec3(*case["gravity"]))
    for record in case["bodies"]:
        ix, iy, iz = record["inertia"]
        pose = wp.transform(wp.vec3(*record["position"]), wp.quat(*record["rotation"]))
        index = builder.add_link(xform=pose, mass=float(record["mass"]),
                                 com=wp.vec3(*record["center_of_mass"]),
                                 inertia=wp.mat33(ix, 0, 0, 0, iy, 0, 0, 0, iz))
        if record["type"] == 0:
            joint = builder.add_joint_fixed(-1, index, parent_xform=pose)
        else:
            joint = builder.add_joint_free(index)
            start = builder.joint_qd_start[joint]
            builder.joint_qd[start:start + 6] = [*record["velocity"], *record["angular_velocity"]]
        builder.add_articulation([joint])

    z_to_y = wp.quat_from_axis_angle(wp.vec3(1, 0, 0), -math.pi * 0.5)
    for record in case["shapes"]:
        cfg = newton.ModelBuilder.ShapeConfig(
            density=0, ke=float(record["stiffness"]), kd=float(record["damping"]),
            kf=float(record["friction_damping"]), mu=float(record["friction"]),
            ka=0, margin=0, gap=0.01, restitution=0, mu_torsional=0, mu_rolling=0,
        )
        q = wp.quat(*record["rotation"])
        if record["type"] == 1:
            # Stock planes use +Z; this fixed basis conversion implements local +Y.
            q = wp.mul(q, z_to_y)
        pose = wp.transform(wp.vec3(*record["position"]), q)
        if record["type"] == 0:
            builder.add_shape_sphere(record["body"], xform=pose, radius=float(record["radius"]), cfg=cfg)
        else:
            builder.add_shape_plane(body=record["body"], xform=pose, width=0, length=0, cfg=cfg)

    model = builder.finalize(device="cpu")
    # Shape density zero must preserve all authored mass properties, including COM.
    for field, expected in (
        (model.body_mass, [b["mass"] for b in case["bodies"]]),
        (model.body_com, [b["center_of_mass"] for b in case["bodies"]]),
        (model.body_inertia, [np.diag(b["inertia"]) for b in case["bodies"]]),
    ):
        if not np.array_equal(field.numpy(), np.asarray(expected, dtype=np.float32)):
            raise ValueError(f"{case['name']}: stock shapes changed authored mass properties")
    solver = newton.solvers.SolverFeatherstone(
        model, angular_damping=0, friction_smoothing=FRICTION_SMOOTHING,
        use_tile_gemm=False, update_mass_matrix_interval=1,
    )
    pipeline = newton.CollisionPipeline(model)
    contacts = pipeline.contacts()
    state, next_state = model.state(), model.state()
    control = model.control()
    newton.eval_fk(model, model.joint_q, model.joint_qd, state)
    applied = np.asarray([[*b["force"], *b["torque"]] for b in case["bodies"]], dtype=np.float32)
    h = float(np.float32(case["dt"] / case["substeps"]))
    samples = []
    for steps in range(1, max(SAMPLE_STEPS) + 1):
        contact_force = np.zeros((2, 3), dtype=np.float32)
        for _ in range(case["substeps"]):
            state.clear_forces()
            state.body_f.assign(applied)
            velocity_before = state.body_qd.numpy()[:, :3].copy()
            pipeline.collide(state, contacts)
            solver.step(state, next_state, control, contacts, h)
            # eval_rigid_tau shifts and NEGATES each articulated COM wrench.
            # Its linear part therefore recovers the stock force with this sign.
            force = -solver.body_f_ext.numpy()[:, :3] - applied[:, :3]
            if not np.allclose(force[0], -force[1], rtol=0, atol=2.0e-5):
                raise ValueError(f"{case['name']}: stock contact action/reaction mismatch")
            velocity_after = next_state.body_qd.numpy()[:, :3]
            for index, body in enumerate(case["bodies"]):
                if body["type"] == 1:
                    momentum_force = body["mass"] * (velocity_after[index] - velocity_before[index]) / h
                    expected = force[index] + applied[index, :3] + body["mass"] * case["gravity"]
                    if not np.allclose(momentum_force, expected, rtol=2.0e-4, atol=2.0e-3):
                        raise ValueError(f"{case['name']}: stock force readback disagrees with momentum")
            contact_force += force / np.float32(case["substeps"])
            state, next_state = next_state, state
        if steps in SAMPLE_STEPS:
            pose = state.body_q.numpy().copy()
            twist = state.body_qd.numpy().copy()
            com = np.asarray([wp.transform_point(wp.transform(*p), wp.vec3(*b["center_of_mass"]))
                              for p, b in zip(pose, case["bodies"])], dtype=np.float32)
            sample = dict(case_index=case_index, steps=steps, body_position=pose[:, :3],
                          body_rotation=pose[:, 3:], body_com_position=com,
                          body_linear_velocity=twist[:, :3], body_angular_velocity=twist[:, 3:],
                          contact_force=contact_force)
            if not all(np.isfinite(sample[field]).all() for field in SAMPLE_FIELDS):
                raise ValueError(f"{case['name']}: nonfinite stock output at step {steps}")
            samples.append(sample)
    if np.linalg.norm(samples[0]["contact_force"][0]) <= 1.0:
        raise ValueError(f"{case['name']}: missing initial contact force")
    return samples


def run_reference(newton_root, warp_root):
    check_source(newton_root, NEWTON_REVISION)
    check_source(warp_root, WARP_REVISION)
    sys.path.insert(0, str(newton_root))
    import numpy as np
    import warp as wp

    if wp.__version__ != WARP_VERSION:
        raise ValueError(f"install warp-lang=={WARP_VERSION}, found {wp.__version__}")
    if wp.config._git_commit_hash != WARP_REVISION:
        raise ValueError(f"installed Warp wheel must be built from {WARP_REVISION}")
    wp.config.log_level = wp.LOG_WARNING
    wp.init()
    wp.set_device("cpu")
    import newton

    if not pathlib.Path(newton.__file__).resolve().is_relative_to(newton_root):
        raise ValueError("Newton was not imported from --newton-root")
    cases = make_cases(np, wp)
    samples = [sample for i, case in enumerate(cases) for sample in run_case(i, case, newton, np, wp)]
    # Recheck sources after JIT execution as well as before importing them.
    check_source(newton_root, NEWTON_REVISION)
    check_source(warp_root, WARP_REVISION)
    return cases, samples


def c_value(value):
    return c_float(value) if value.ndim == 0 else c_array(value)


def render_header(cases, samples):
    lines = [
        "/* Generated by contact_oracle.py with unmodified stock Newton CPU.",
        f" * Newton commit: {NEWTON_REVISION}",
        f" * Warp source pin: {WARP_REVISION}",
        f" * Installed Warp wheel: {WARP_VERSION}",
        " * Stock ModelBuilder + CollisionPipeline + SolverFeatherstone; stock CPU math.",
        " * No Tide runtime, exported kernels, or deterministic math bindings loaded.",
        " * Each body is a separate articulation: dynamic FREE, static world FIXED.",
        " * Shape density=0 preserves authored mass, diagonal COM inertia and local COM.",
        " * Input enums: body type 0=NT_STATIC, 1=NT_DYNAMIC; shape 0=NT_SPHERE, 1=NT_PLANE.",
        " * Plane normal is local +Y; the oracle converts stock plane +Z with Rx(-pi/2).",
        " * All input floats are float32. Quaternions are xyzw, authored unit rotations.",
        " * Poses refer to body origins; velocities are WORLD COM linear / WORLD angular.",
        " * Collider transforms are body local. Shape order is retained in force outputs.",
        " * Material properties are averaged by upstream eval_body_contact; adhesion=0.",
        " * Solver angular damping=0; friction_smoothing=.01; mass matrix refreshed each substep.",
        " * Collision regenerated before every substep; radius margins=0, detection gap=.01.",
        " * dt is total duration per step; input wrenches persist through all substeps/steps.",
        " * Samples are post-step. contact_force is the last step's mean across substeps,",
        " * including static reaction and excluding authored external forces and gravity.",
        " * It is read from -solver.body_f_ext.linear - external_force after eval_rigid_tau.",
        " * One shape per body makes the per-body force an exact per-shape force readback.",
        " * Force readbacks also checked against action/reaction and dynamic momentum change.",
        " * Compare short rollouts using the separate state/force absolute tolerances below.",
        " * Regenerate with python -B contact_oracle.py --write; verify with --check.",
        " */",
        "#ifndef TIDE_NEWTON_CONTACT_ORACLE_EXPECTED_H",
        "#define TIDE_NEWTON_CONTACT_ORACLE_EXPECTED_H", "",
        "enum { NT_CONTACT_ORACLE_CASE_COUNT = 3, NT_CONTACT_ORACLE_MAX_BODIES = 2,",
        "       NT_CONTACT_ORACLE_MAX_SHAPES = 2, NT_CONTACT_ORACLE_SAMPLE_COUNT = 3,",
        "       NT_CONTACT_ORACLE_TOTAL_SAMPLE_COUNT = 9 };",
        f"#define NT_CONTACT_ORACLE_TOLERANCE {c_float(STATE_TOLERANCE)}",
        f"#define NT_CONTACT_ORACLE_FORCE_TOLERANCE {c_float(FORCE_TOLERANCE)}", "",
        "typedef struct nt_contact_oracle_body {", "    int type;", "    float mass;",
        "    float inertia[3], center_of_mass[3], position[3], rotation[4];",
        "    float velocity[3], angular_velocity[3], force[3], torque[3];",
        "} nt_contact_oracle_body;", "",
        "typedef struct nt_contact_oracle_shape {", "    int body, type;", "    float radius;",
        "    float position[3], rotation[4];",
        "    float stiffness, damping, friction_damping, friction;",
        "} nt_contact_oracle_shape;", "",
        "typedef struct nt_contact_oracle_case {", "    const char *name;",
        "    int body_count, shape_count;", "    float dt;", "    int substeps;", "    float gravity[3];",
        "    nt_contact_oracle_body bodies[NT_CONTACT_ORACLE_MAX_BODIES];",
        "    nt_contact_oracle_shape shapes[NT_CONTACT_ORACLE_MAX_SHAPES];",
        "} nt_contact_oracle_case;", "",
        "typedef struct nt_contact_oracle_sample {", "    int case_index, steps;",
        "    float body_position[2][3], body_rotation[2][4], body_com_position[2][3];",
        "    float body_linear_velocity[2][3], body_angular_velocity[2][3];",
        "    float contact_force[2][3];", "} nt_contact_oracle_sample;", "",
        "static const nt_contact_oracle_case nt_contact_oracle_cases[NT_CONTACT_ORACLE_CASE_COUNT] = {",
    ]
    for case in cases:
        lines.extend(["    {", f"        \"{case['name']}\", {case['body_count']}, {case['shape_count']},",
                      f"        {c_float(case['dt'])}, {case['substeps']}, {c_array(case['gravity'])},",
                      "        { /* bodies */"])
        for body in case["bodies"]:
            lines.append(f"            {{ {body['type']},")
            lines.extend(f"                {c_value(body[field])}, /* {field} */" for field in BODY_FIELDS)
            lines.append("            },")
        lines.extend(["        },", "        { /* shapes */"])
        for shape in case["shapes"]:
            lines.append(f"            {{ {shape['body']}, {shape['type']},")
            lines.extend(f"                {c_value(shape[field])}, /* {field} */" for field in SHAPE_FIELDS)
            lines.append("            },")
        lines.extend(["        },", "    },"])
    lines.extend(["};", "",
                  "static const nt_contact_oracle_sample nt_contact_oracle_samples[NT_CONTACT_ORACLE_TOTAL_SAMPLE_COUNT] = {"])
    for sample in samples:
        lines.extend(["    {", f"        {sample['case_index']}, {sample['steps']},"])
        for field in SAMPLE_FIELDS:
            lines.append(f"        {{ /* {field} */")
            lines.extend(f"            {c_array(row)}," for row in sample[field])
            lines.append("        },")
        lines.append("    },")
    lines.extend(["};", "", "#endif", ""])
    return "\n".join(lines)


def main():
    repository = pathlib.Path(__file__).resolve().parents[3]
    research = repository / "build" / "research-newton"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--newton-root", type=pathlib.Path, default=research / "newton")
    parser.add_argument("--warp-root", type=pathlib.Path, default=research / "warp")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--write", action="store_true", help="rebuild contact_oracle_expected.h")
    mode.add_argument("--check", action="store_true", help="check contact_oracle_expected.h is current")
    args = parser.parse_args()
    try:
        with contextlib.redirect_stdout(sys.stderr):
            cases, samples = run_reference(args.newton_root.resolve(), args.warp_root.resolve())
        header = render_header(cases, samples)
        expected = pathlib.Path(__file__).with_name("contact_oracle_expected.h")
        if args.write:
            expected.write_text(header, encoding="utf-8", newline="\n")
            print("Rebuilt contact_oracle_expected.h from stock Newton CPU", file=sys.stderr)
        elif args.check:
            if expected.read_text(encoding="utf-8") != header:
                raise ValueError("contact_oracle_expected.h differs; regenerate with --write")
            print("contact_oracle_expected.h matches stock Newton CPU", file=sys.stderr)
        else:
            sys.stdout.write(header)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"contact_oracle: {error}\n")


if __name__ == "__main__":
    main()
