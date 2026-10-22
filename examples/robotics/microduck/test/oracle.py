"""Generate an independent full-tensor MicroDuck reference with stock Newton CPU.

From the repository root, in the optional research Python environment:
    build/research-newton/venv/Scripts/python.exe examples/robotics/microduck/test/oracle.py --write
    build/research-newton/venv/Scripts/python.exe examples/robotics/microduck/test/oracle.py --check

This reads the ORIGINAL upstream MJCF, never model.tide, the principal-axis
converter, Tide's runtime, exported kernels, or Tide math bindings. Python,
NumPy, Newton and Warp are reference-regeneration dependencies only. The
checked-in C header is sufficient for native and web game tests.

The fixture fixes the original dynamic trunk to the world instead of using the
source free joint. Its original origin is at zero, with source Z-up rotated
-90 degrees about X to Tide Y-up. Compare COM positions after subtracting the
runtime's trunk anchor translation; principal-axis body rotations differ by
design. This is an articulated-dynamics/conversion check, not a contact,
walking, MuJoCo-equivalence, or physical-robot fidelity claim.
"""

import argparse
import contextlib
import hashlib
import math
import pathlib
import subprocess
import sys
import xml.etree.ElementTree as ET


MICRODUCK_REVISION = "b02fdea4902c2b93b7d34a83a6f05da4ececb7d3"
XML_SHA256 = {
    "robot_walk.xml": "80fc4424ed71e430ec4919110dfdb5c714dc1c4e1138e1832a16865cf31eb8c8",
    "scene_walk.xml": "6e23146c1a5025473b55ec94cbe37b08104ca388b7eb005ea277f8230736c8be",
}
NEWTON_REVISION = "ea4899864046dcd5981f8ba0bc3d8c25e65dcb96"
WARP_REVISION = "b346b941a4c0cd19c510db67d6506d781464ed5b"
WARP_VERSION = "1.19.0.dev20261020"
BODY_COUNT = 15
DOF_COUNT = 14
DT = 1.0 / 200.0
SAMPLE_STEPS = (1, 8, 32)
GRAVITY = (0.0, -9.81, 0.0)
DAMPING = 0.053
ARMATURE = 0.0018
EFFORT_LIMIT = 0.96
EFFORT = tuple(0.02 if index % 2 == 0 else -0.015 for index in range(DOF_COUNT))
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


def numbers(element, attribute, count, default=None):
    text = element.get(attribute, default)
    if text is None:
        raise ValueError(f"{element.tag} is missing {attribute}")
    values = tuple(float(value) for value in text.split())
    if len(values) != count or not all(math.isfinite(value) for value in values):
        raise ValueError(f"invalid {element.tag} {attribute}")
    return values


def load_source(xml_root):
    """Parse only this pinned original model, retaining full inertia tensors."""
    roots = {}
    for name, expected in XML_SHA256.items():
        data = (xml_root / name).read_bytes()
        # Git may check text out as CRLF on Windows. Pin identical XML content
        # while accepting that line-ending-only checkout difference.
        actual = hashlib.sha256(data.replace(b"\r\n", b"\n")).hexdigest()
        if actual != expected:
            raise ValueError(f"{name} differs from the pinned upstream XML ({actual})")
        roots[name] = ET.fromstring(data)
    robot, scene = roots["robot_walk.xml"], roots["scene_walk.xml"]
    if robot.findall("./equality/*"):
        raise ValueError("the selected source has active equality constraints")
    world = robot.find("worldbody")
    if world is None or len(world.findall("body")) != 1:
        raise ValueError("expected one original trunk tree")
    trunk = world.find("body")
    if trunk.get("name") != "trunk_base" or len(trunk.findall("freejoint")) != 1:
        raise ValueError("expected the original free trunk")
    if numbers(trunk, "quat", 4, "1 0 0 0") != (1.0, 0.0, 0.0, 0.0):
        raise ValueError("unexpected original trunk rotation")

    chosen = robot.find("./default/default[@class='chosen_actuator']")
    if chosen is None:
        raise ValueError("missing chosen_actuator source defaults")
    joint_defaults, actuator_defaults = chosen.find("joint"), chosen.find("position")
    if joint_defaults is None or actuator_defaults is None:
        raise ValueError("missing chosen actuator settings")
    if numbers(joint_defaults, "damping", 1) != (DAMPING,):
        raise ValueError("unexpected source damping")
    if numbers(joint_defaults, "armature", 1) != (ARMATURE,):
        raise ValueError("unexpected source armature")
    if numbers(actuator_defaults, "forcerange", 2) != (-EFFORT_LIMIT, EFFORT_LIMIT):
        raise ValueError("unexpected source actuator effort range")

    bodies, hinges = [], []

    def visit(body, parent):
        index = len(bodies)
        inertial = body.find("inertial")
        if inertial is None or "diaginertia" in inertial.attrib or "quat" in inertial.attrib:
            raise ValueError("expected full inertia in the original body frame")
        mass = numbers(inertial, "mass", 1)[0]
        if mass <= 0.0:
            raise ValueError("expected positive dynamic mass")
        bodies.append({
            "name": body.get("name"),
            "parent": parent,
            "mass": mass,
            "com": numbers(inertial, "pos", 3, "0 0 0"),
            "fullinertia": numbers(inertial, "fullinertia", 6),
            "pos": numbers(body, "pos", 3, "0 0 0"),
            "quat_wxyz": numbers(body, "quat", 4, "1 0 0 0"),
        })
        joints = body.findall("joint")
        if parent < 0:
            if joints:
                raise ValueError("unexpected trunk hinge")
        else:
            if len(joints) != 1 or body.findall("freejoint"):
                raise ValueError("expected one hinge per non-root body")
            joint = joints[0]
            if joint.get("type") != "hinge" or joint.get("class") != "chosen_actuator":
                raise ValueError("unexpected source hinge configuration")
            if float(joint.get("ref", "0")) != 0.0:
                raise ValueError("nonzero source joint reference is not supported")
            hinges.append({
                "name": joint.get("name"),
                "parent": parent,
                "child": index,
                "pos": numbers(joint, "pos", 3, "0 0 0"),
                "axis": numbers(joint, "axis", 3),
            })
        for child in body.findall("body"):
            visit(child, index)

    visit(trunk, -1)
    if len(bodies) != BODY_COUNT or len(hinges) != DOF_COUNT:
        raise ValueError("expected 15 original bodies and 14 hinges")
    if len({body["name"] for body in bodies}) != BODY_COUNT:
        raise ValueError("body names must be unique")
    if len({joint["name"] for joint in hinges}) != DOF_COUNT:
        raise ValueError("joint names must be unique")
    actuated = [element.get("joint") for element in robot.findall("./actuator/position")]
    if actuated != [joint["name"] for joint in hinges]:
        raise ValueError("source actuator order differs from DFS hinge order")
    key = scene.find("./keyframe/key[@name='STAND']")
    if key is None:
        raise ValueError("missing original STAND keyframe")
    initial_q = numbers(key, "qpos", 7 + DOF_COUNT)[7:]
    if initial_q != numbers(key, "ctrl", DOF_COUNT):
        raise ValueError("STAND controls differ from joint coordinates")
    return bodies, hinges, initial_q


def run_reference(newton_root, warp_root, xml_root):
    check_source(newton_root, NEWTON_REVISION)
    check_source(warp_root, WARP_REVISION)
    bodies, hinges, initial_q = load_source(xml_root)
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

    def source_transform(body):
        # MJCF stores wxyz, Warp uses xyzw. Normalize rounded CAD quaternions
        # as MJCF does; do not diagonalize or rotate the source inertia tensor.
        w, x, y, z = body["quat_wxyz"]
        norm = math.sqrt(w * w + x * x + y * y + z * z)
        if norm == 0.0:
            raise ValueError("zero source body quaternion")
        return wp.transform(wp.vec3(*body["pos"]), wp.quat(x / norm, y / norm, z / norm, w / norm))

    builder = newton.ModelBuilder(gravity=wp.vec3(*GRAVITY))
    original_inertias = []
    for body in bodies:
        # MJCF ordering: Ixx Iyy Izz Ixy Ixz Iyz, all about the authored COM.
        xx, yy, zz, xy, xz, yz = body["fullinertia"]
        matrix = (xx, xy, xz, xy, yy, yz, xz, yz, zz)
        if np.linalg.eigvalsh(np.asarray(matrix).reshape(3, 3)).min() <= 0.0:
            raise ValueError(f"nonpositive original tensor for {body['name']}")
        original_inertias.append(matrix)
        builder.add_link(
            mass=body["mass"],
            com=wp.vec3(*body["com"]),
            inertia=wp.mat33(*matrix),
            label=body["name"],
            is_kinematic=False,
        )
    # Source trunk placement (0,0,.12) is replaced by the fixture anchor at
    # zero. Keeping its real mass/full tensor and a fixed joint matches the
    # runtime's dynamic principal-axis trunk, rather than making it kinematic.
    root_rotation = wp.quat_from_axis_angle(wp.vec3(1.0, 0.0, 0.0), -math.pi / 2.0)
    joints = [builder.add_joint_fixed(
        -1, 0,
        parent_xform=wp.transform(wp.vec3(0.0, 0.0, 0.0), root_rotation),
        child_xform=wp.transform_identity(),
        label="fixture_anchor",
    )]
    for joint in hinges:
        anchor = wp.transform(wp.vec3(*joint["pos"]), wp.quat_identity())
        # MJCF joint pos/axis are in the child body frame at q=0. The same
        # anchor appears on both sides of the joint so nonzero pos is kept.
        parent_anchor = wp.transform_multiply(source_transform(bodies[joint["child"]]), anchor)
        joints.append(builder.add_joint_revolute(
            joint["parent"], joint["child"],
            parent_xform=parent_anchor,
            child_xform=anchor,
            axis=wp.vec3(*joint["axis"]),
            target_pos=0.0, target_vel=0.0, target_ke=0.0, target_kd=0.0,
            damping=DAMPING, armature=ARMATURE, friction=0.0,
            limit_lower=-UNLIMITED, limit_upper=UNLIMITED,
            limit_ke=0.0, limit_kd=0.0,
            effort_limit=EFFORT_LIMIT, velocity_limit=1.0e6,
            actuator_mode=newton.JointTargetMode.EFFORT,
            label=joint["name"],
        ))
    builder.add_articulation(joints)
    model = builder.finalize(device="cpu")
    if (model.body_count, model.joint_count, model.joint_coord_count, model.joint_dof_count) != (15, 15, 14, 14):
        raise ValueError("unexpected stock Newton articulation layout")
    if model.shape_count != 0 or not model.device.is_cpu:
        raise ValueError("reference must use CPU and contain no shapes")
    if not np.all(model.body_flags.numpy() == int(newton.BodyFlags.DYNAMIC)):
        raise ValueError("all original links, including the anchored trunk, must be dynamic")
    expected_inertias = np.asarray(original_inertias, dtype=np.float32).reshape(BODY_COUNT, 3, 3)
    if not np.array_equal(model.body_inertia.numpy(), expected_inertias):
        raise ValueError("stock model did not retain the original full inertia tensors")
    model.joint_q.assign(np.asarray(initial_q, dtype=np.float32))
    model.joint_qd.assign(np.zeros(DOF_COUNT, dtype=np.float32))
    solver = newton.solvers.SolverFeatherstone(
        model, angular_damping=0.0, use_tile_gemm=False, update_mass_matrix_interval=1
    )
    state, next_state = model.state(), model.state()
    control = model.control()
    # Featherstone does not enforce its effort_limit field. Mirror the Tide
    # package's explicit actuator clamp before supplying generalized efforts.
    applied_effort = np.clip(np.asarray(EFFORT, dtype=np.float32), -EFFORT_LIMIT, EFFORT_LIMIT)
    control.joint_f.assign(applied_effort)
    newton.eval_fk(model, model.joint_q, model.joint_qd, state)

    def capture(steps):
        poses = state.body_q.numpy().copy()
        com_world = np.asarray([
            wp.transform_point(wp.transform(*pose), wp.vec3(*body["com"]))
            for pose, body in zip(poses, bodies)
        ], dtype=np.float32)
        fields = {
            "q": state.joint_q.numpy().copy(),
            "qd": state.joint_qd.numpy().copy(),
            "body_com_position": com_world,
        }
        if not all(np.isfinite(value).all() for value in fields.values()):
            raise ValueError(f"nonfinite stock Newton output after {steps} steps")
        return steps, fields

    initial = capture(0)
    samples = []
    for steps in range(1, max(SAMPLE_STEPS) + 1):
        state.clear_forces()
        solver.step(state, next_state, control, None, DT)
        state, next_state = next_state, state
        if steps in SAMPLE_STEPS:
            # SolverFeatherstone already reconstructs post-step FK. Repeating
            # integration or supplying a separate pose update would be wrong.
            samples.append(capture(steps))
    if not np.any(samples[-1][1]["q"] != initial[1]["q"]):
        raise ValueError("reference did not exercise articulated dynamics")
    return bodies, hinges, initial, samples


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


def render_header(bodies, hinges, initial, samples):
    lines = [
        "/* Generated by oracle.py using unmodified upstream Newton on the CPU.",
        f" * MicroDuck source: https://github.com/pollen-robotics/microduck_rl/tree/{MICRODUCK_REVISION}",
        " * Original robot_walk.xml + scene_walk.xml, pinned SHA256 (LF text):",
        *(f" * {name}: {digest}" for name, digest in XML_SHA256.items()),
        f" * Newton commit: {NEWTON_REVISION}",
        f" * Warp source pin: {WARP_REVISION}",
        f" * Installed Warp Python wheel: {WARP_VERSION}",
        " * Stock ModelBuilder + SolverFeatherstone; stock Warp CPU math settings.",
        " * No Tide code, converted principal data, exported kernels, or math shims.",
        " * Original 15 dynamic links, full COM tensors including products of inertia.",
        " * Original body/hinge frames and axes; source quaternion inputs normalized.",
        " * Source free trunk replaced by FIXED joint to world at zero, rotation -90deg X.",
        " * This maps source Z-up to Tide Y-up: (x,y,z) -> (x,z,-y).",
        " * Initial hinge q is original scene_walk STAND (source DFS order), qd=0.",
        " * Constant effort alternates +.02/-.015 Nm, explicitly clamped .96 Nm, EFFORT mode.",
        " * Chosen actuator damping .053 and armature .0018; target gains zero.",
        " * No shapes/contacts/friction; inactive limits +/-1e10 and limit gains zero.",
        " * No equality constraints in the original selected robot; no velocity constraint.",
        " * Gravity (0,-9.81,0), dt=1/200, substeps=1; angular damping zero.",
        " * Mass matrix refreshed every step; samples are post-step q/qd and COMs.",
        " * body_com_position is WORLD COM position relative to the zero trunk anchor.",
        " * Subtract the runtime's anchor translation before comparing COM positions.",
        " * Principal-frame body origins/rotations are intentionally not compared.",
        " * Absolute tolerance, not bitwise equality: Tide math/FMA settings differ.",
        " * This checks anchored articulated dynamics, not walking or sim-to-real fidelity.",
        " * Regenerate with oracle.py --write; verify with oracle.py --check.",
        " */",
        "#ifndef TIDE_MICRODUCK_ORACLE_EXPECTED_H",
        "#define TIDE_MICRODUCK_ORACLE_EXPECTED_H",
        "",
        "enum { MD_ORACLE_BODY_COUNT = 15, MD_ORACLE_DOF_COUNT = 14, MD_ORACLE_SAMPLE_COUNT = 3 };",
        "#define MD_ORACLE_DT (1.0f / 200.0f)",
        "#define MD_ORACLE_TOLERANCE 5.0e-5f",
        f"#define MD_ORACLE_DAMPING {c_float(DAMPING)}",
        f"#define MD_ORACLE_ARMATURE {c_float(ARMATURE)}",
        f"#define MD_ORACLE_EFFORT_LIMIT {c_float(EFFORT_LIMIT)}",
        "",
        "static const char *const md_oracle_body_names[MD_ORACLE_BODY_COUNT] = {",
        *(f'    "{body["name"]}",' for body in bodies),
        "};",
        "static const char *const md_oracle_joint_names[MD_ORACLE_DOF_COUNT] = {",
        *(f'    "{joint["name"]}",' for joint in hinges),
        "};",
        f"static const float md_oracle_gravity[3] = {c_array(GRAVITY)};",
        f"static const float md_oracle_effort[MD_ORACLE_DOF_COUNT] = {c_array(EFFORT)};",
        "",
        "typedef struct md_oracle_sample {",
        "    int steps;",
        "    float q[MD_ORACLE_DOF_COUNT];",
        "    float qd[MD_ORACLE_DOF_COUNT];",
        "    float body_com_position[MD_ORACLE_BODY_COUNT][3];",
        "} md_oracle_sample;",
        "",
        emit_sample("static const md_oracle_sample md_oracle_initial = ", initial) + ";",
        "",
        "static const md_oracle_sample md_oracle_samples[MD_ORACLE_SAMPLE_COUNT] = {",
    ]
    for sample in samples:
        lines.extend("    " + line for line in emit_sample("", sample).splitlines())
        lines[-1] += ","
    lines.extend(["};", "", "#endif", ""])
    return "\n".join(lines)


def main():
    repository = pathlib.Path(__file__).resolve().parents[4]
    research = repository / "build" / "research-newton"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--newton-root", type=pathlib.Path, default=research / "newton")
    parser.add_argument("--warp-root", type=pathlib.Path, default=research / "warp")
    parser.add_argument("--xml-root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1] / "upstream")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--write", action="store_true", help="rebuild oracle_expected.h")
    mode.add_argument("--check", action="store_true", help="check oracle_expected.h is current")
    args = parser.parse_args()
    try:
        with contextlib.redirect_stdout(sys.stderr):
            reference = run_reference(args.newton_root.resolve(), args.warp_root.resolve(), args.xml_root.resolve())
        header = render_header(*reference)
        expected = pathlib.Path(__file__).with_name("oracle_expected.h")
        if args.write:
            expected.write_text(header, encoding="utf-8", newline="\n")
            print("Rebuilt oracle_expected.h from original full-tensor MJCF / stock Newton CPU", file=sys.stderr)
        elif args.check:
            if expected.read_text(encoding="utf-8") != header:
                raise ValueError("oracle_expected.h differs; regenerate with --write")
            print("oracle_expected.h matches original full-tensor MJCF / stock Newton CPU", file=sys.stderr)
        else:
            sys.stdout.write(header)
    except (OSError, ValueError, subprocess.CalledProcessError, ET.ParseError) as error:
        parser.exit(1, f"oracle: {error}\n")


if __name__ == "__main__":
    main()
