"""Generate the two-link reference with unmodified upstream Newton on the CPU.

From the repository root, using the research environment (Python is not needed
by game builds):
    build/research-newton/venv/Scripts/python.exe packages/newton/test/oracle.py --write
    build/research-newton/venv/Scripts/python.exe packages/newton/test/oracle.py --check

Without either switch, print the C header to stdout. Other source locations can
be supplied with --newton-root and --warp-root. The source revisions and the
installed Warp version must match the package's export pins. This script never
loads Tide's runtime, generated kernels, or math bindings.
"""

import argparse
import contextlib
import math
import pathlib
import subprocess
import sys


NEWTON_REVISION = "ea4899864046dcd5981f8ba0bc3d8c25e65dcb96"
WARP_REVISION = "b346b941a4c0cd19c510db67d6506d781464ed5b"
WARP_VERSION = "1.19.0.dev20261020"
DT = 1.0 / 240.0
SAMPLE_STEPS = (1, 8, 32)
MASSES = (2.0, 1.0)
COMS = ((0.5, 0.0, 0.0), (0.4, 0.0, 0.0))
INERTIAS = ((0.1, 0.3, 0.3), (0.05, 0.12, 0.12))
GRAVITY = (0.0, -9.81, 0.0)
INITIAL_Q = (0.3, -0.2)
INITIAL_QD = (0.1, -0.15)
EFFORT = (1.25, -0.6)
EFFORT_LIMIT = 1.0e6
UNLIMITED = 1.0e10


def check_source(folder, expected):
    """Reject a different revision or modified tracked upstream source."""
    revision = subprocess.check_output(
        ["git", "-C", str(folder), "rev-parse", "HEAD"], text=True
    ).strip()
    if revision != expected:
        raise ValueError(f"{folder.name} must be at {expected}, found {revision}")
    dirty = subprocess.check_output(
        ["git", "-C", str(folder), "status", "--porcelain", "--untracked-files=no"],
        text=True,
    ).strip()
    if dirty:
        raise ValueError(f"{folder.name} has modified tracked upstream files")


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

    # Guard against accidentally importing a different installed Newton package.
    if not pathlib.Path(newton.__file__).resolve().is_relative_to(newton_root):
        raise ValueError("Newton was not imported from --newton-root")

    builder = newton.ModelBuilder(gravity=wp.vec3(*GRAVITY))
    bodies = []
    for mass, com, inertia in zip(MASSES, COMS, INERTIAS):
        ix, iy, iz = inertia
        bodies.append(
            builder.add_link(
                mass=mass,
                com=wp.vec3(*com),
                inertia=wp.mat33(ix, 0.0, 0.0, 0.0, iy, 0.0, 0.0, 0.0, iz),
            )
        )
    joints = []
    for parent, child, anchor in (
        (-1, bodies[0], (0.0, 0.0, 0.0)),
        (bodies[0], bodies[1], (1.0, 0.0, 0.0)),
    ):
        joints.append(
            builder.add_joint_revolute(
                parent,
                child,
                parent_xform=wp.transform(wp.vec3(*anchor), wp.quat_identity()),
                child_xform=wp.transform_identity(),
                axis=wp.vec3(0.0, 0.0, 1.0),
                target_pos=0.0,
                target_vel=0.0,
                target_ke=0.0,
                target_kd=0.0,
                damping=0.0,
                armature=0.0,
                friction=0.0,
                limit_lower=-UNLIMITED,
                limit_upper=UNLIMITED,
                limit_ke=0.0,
                limit_kd=0.0,
                effort_limit=EFFORT_LIMIT,
                velocity_limit=1.0e6,
                actuator_mode=newton.JointTargetMode.EFFORT,
            )
        )
    builder.add_articulation(joints)
    model = builder.finalize(device="cpu")
    model.joint_q.assign(np.asarray(INITIAL_Q, dtype=np.float32))
    model.joint_qd.assign(np.asarray(INITIAL_QD, dtype=np.float32))
    solver = newton.solvers.SolverFeatherstone(
        model, angular_damping=0.0, use_tile_gemm=False, update_mass_matrix_interval=1
    )
    state, next_state = model.state(), model.state()
    control = model.control()
    control.joint_f.assign(np.asarray(EFFORT, dtype=np.float32))
    newton.eval_fk(model, model.joint_q, model.joint_qd, state)

    def capture(steps):
        body_q = state.body_q.numpy().copy()
        body_qd = state.body_qd.numpy().copy()
        com_world = np.asarray(
            [
                wp.transform_point(wp.transform(*pose), wp.vec3(*com))
                for pose, com in zip(body_q, model.body_com.numpy())
            ],
            dtype=np.float32,
        )
        sample = {
            "q": state.joint_q.numpy().copy(),
            "qd": state.joint_qd.numpy().copy(),
            "body_position": body_q[:, :3],
            "body_rotation": body_q[:, 3:],
            "body_com_position": com_world,
            "body_linear_velocity": body_qd[:, :3],
            "body_angular_velocity": body_qd[:, 3:],
        }
        if not all(np.isfinite(value).all() for value in sample.values()):
            raise ValueError(f"nonfinite stock Newton output after {steps} steps")
        return steps, sample

    initial = capture(0)
    samples = []
    for steps in range(1, max(SAMPLE_STEPS) + 1):
        state.clear_forces()
        solver.step(state, next_state, control, None, DT)
        state, next_state = next_state, state
        if steps in SAMPLE_STEPS:
            # step already reconstructs public body poses and COM twists from
            # the updated joint state. No extra integration or FK is applied.
            samples.append(capture(steps))
    return initial, samples


def c_float(value):
    value = float(value)
    if not math.isfinite(value):
        raise ValueError("cannot emit a nonfinite C reference")
    text = format(value, ".9g")
    if "." not in text and "e" not in text:
        text += ".0"
    return text + "f"


def c_array(values):
    return "{ " + ", ".join(c_float(value) for value in values) + " }"


def emit_sample(name, sample):
    steps, fields = sample
    lines = [name + "{", f"    {steps},"]
    for field, values in fields.items():
        if values.ndim == 1:
            lines.append(f"    {c_array(values)}, /* {field} */")
        else:
            lines.append(f"    {{ /* {field} */")
            lines.extend(f"        {c_array(row)}," for row in values)
            lines.append("    },")
    lines.append("}")
    return "\n".join(lines)


def render_header(initial, samples):
    lines = [
        "/* Generated by oracle.py using unmodified upstream Newton on the CPU.",
        f" * Newton commit: {NEWTON_REVISION}",
        f" * Warp source pin: {WARP_REVISION}",
        f" * Installed Warp Python wheel: {WARP_VERSION}",
        " * Stock ModelBuilder + SolverFeatherstone; stock Warp CPU math settings.",
        " * No Tide code, exported kernels, or deterministic math shims are loaded.",
        " * Two links, no shapes/contacts, no free base, one articulation.",
        " * Masses: 2, 1; local COMs: (.5,0,0), (.4,0,0).",
        " * COM inertia diagonals: (.1,.3,.3), (.05,.12,.12).",
        " * Both axes +z; world-to-first joint identity; second parent anchor (1,0,0).",
        " * Both child anchors identity. Initial q=(.3,-.2), qd=(.1,-.15).",
        " * Constant joint_f=(1.25,-.6); effort limit 1e6; actuator mode EFFORT.",
        " * Damping, armature, friction, target gains and limit gains all zero.",
        " * Inactive position limits +/-1e10; velocity limit 1e6; solver angular damping 0.",
        " * Gravity (0,-9.81,0), dt=1/240, substeps=1; mass matrix refreshed every step.",
        " * body_position is the body frame origin, body_rotation is xyzw.",
        " * Linear velocity is WORLD COM velocity; angular velocity is WORLD angular velocity.",
        " * All body outputs are post-step values consistent with the current q/qd.",
        " * Compare short rollouts with absolute tolerances, not bitwise equality:",
        " * Tide disables FMA and uses shared deterministic transcendental math.",
        " * Regenerate with oracle.py --write; verify with oracle.py --check.",
        " */",
        "#ifndef TIDE_NEWTON_ORACLE_EXPECTED_H",
        "#define TIDE_NEWTON_ORACLE_EXPECTED_H",
        "",
        "enum { NT_ORACLE_BODY_COUNT = 2, NT_ORACLE_JOINT_COUNT = 2, NT_ORACLE_SAMPLE_COUNT = 3 };",
        "#define NT_ORACLE_DT (1.0f / 240.0f)",
        "#define NT_ORACLE_TOLERANCE 5.0e-5f",
        "",
        "typedef struct nt_oracle_sample {",
        "    int steps;",
        "    float q[2];",
        "    float qd[2];",
        "    float body_position[2][3];",
        "    float body_rotation[2][4];",
        "    float body_com_position[2][3];",
        "    float body_linear_velocity[2][3];",
        "    float body_angular_velocity[2][3];",
        "} nt_oracle_sample;",
        "",
        emit_sample("static const nt_oracle_sample nt_oracle_initial = ", initial) + ";",
        "",
        "static const nt_oracle_sample nt_oracle_samples[NT_ORACLE_SAMPLE_COUNT] = {",
    ]
    for sample in samples:
        lines.extend("    " + line for line in emit_sample("", sample).splitlines())
        lines[-1] += ","
    lines.extend(["};", "", "#endif", ""])
    return "\n".join(lines)


def main():
    repository = pathlib.Path(__file__).resolve().parents[3]
    research = repository / "build" / "research-newton"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--newton-root", type=pathlib.Path, default=research / "newton")
    parser.add_argument("--warp-root", type=pathlib.Path, default=research / "warp")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--write", action="store_true", help="rebuild oracle_expected.h")
    mode.add_argument("--check", action="store_true", help="check oracle_expected.h is current")
    args = parser.parse_args()
    try:
        # Keep stdout usable as a C header even if Warp reports compilation work.
        with contextlib.redirect_stdout(sys.stderr):
            initial, samples = run_reference(args.newton_root.resolve(), args.warp_root.resolve())
        header = render_header(initial, samples)
        expected = pathlib.Path(__file__).with_name("oracle_expected.h")
        if args.write:
            expected.write_text(header, encoding="utf-8", newline="\n")
            print("Rebuilt oracle_expected.h from stock Newton CPU", file=sys.stderr)
        elif args.check:
            if expected.read_text(encoding="utf-8") != header:
                raise ValueError("oracle_expected.h differs; regenerate with --write")
            print("oracle_expected.h matches stock Newton CPU", file=sys.stderr)
        else:
            sys.stdout.write(header)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"oracle: {error}\n")


if __name__ == "__main__":
    main()
