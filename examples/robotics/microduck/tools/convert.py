#!/usr/bin/env python3
"""Convert only the pinned MicroDuck walk model; no MuJoCo/NumPy or mesh IO.

P maps a principal-inertia body basis into the original MJCF body basis.
The origin is unchanged: COM' = transpose(P) COM, I' = transpose(P) I P.
For a hinge at j in child-local coordinates, the two Newton frames are
inverse(Pparent) T_parent_child Translate(j), inverse(Pchild) Translate(j).
Thus the original joint-frame axis and generalized coordinate are unchanged.
"""

import argparse
from dataclasses import dataclass
import hashlib
import math
from pathlib import Path
import struct
import sys
import xml.etree.ElementTree as ET


REVISION = "b02fdea4902c2b93b7d34a83a6f05da4ececb7d3"
HASHES = {
    "robot_walk.xml": "80fc4424ed71e430ec4919110dfdb5c714dc1c4e1138e1832a16865cf31eb8c8",
    "scene_walk.xml": "6e23146c1a5025473b55ec94cbe37b08104ca388b7eb005ea277f8230736c8be",
    "LICENSE": "2d3fd9ffffa70a4307498ec630b14a6b4731e010f8ddda92686eaf65733c2dfe",
}
BASE = Path(__file__).resolve().parents[1]
IDENTITY = [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]
ZERO = [0.0, 0.0, 0.0]

# Exact supported XML vocabulary, including deliberately excluded presentation
# and sensor data. Unknown attributes (ref, gear, euler, inertiafromgeom, etc.)
# and nodes (tendons, active equality constraints, additional joints, etc.) fail.
# The byte pins are checked too: this is not a general-purpose MJCF importer.
SCHEMA = {
    "mujoco": ("model", "compiler default sensor worldbody asset actuator equality include visual keyframe"),
    "compiler": ("angle meshdir autolimits", ""),
    "default": ("class", "default joint position geom equality"),
    "joint": ("name type class axis pos range damping frictionloss armature", ""),
    "position": ("name joint class kp kv dampratio forcerange ctrlrange", ""),
    "geom": ("name type class pos quat mesh material contype conaffinity group size", ""),
    "sensor": ("", "framequat gyro velocimeter accelerometer subtreeangmom"),
    "framequat": ("name objtype objname noise", ""),
    "gyro": ("name site noise", ""),
    "velocimeter": ("name site", ""),
    "accelerometer": ("name site", ""),
    "subtreeangmom": ("name body", ""),
    "worldbody": ("", "body light geom"),
    "body": ("name pos quat childclass", "body freejoint joint inertial geom site camera"),
    "freejoint": ("name", ""),
    "inertial": ("mass pos fullinertia", ""),
    "site": ("name group pos quat", ""),
    "camera": ("name pos quat", ""),
    "asset": ("", "mesh material texture"),
    "mesh": ("file", ""),
    "material": ("name rgba texture texuniform texrepeat reflectance", ""),
    "actuator": ("", "position"),
    "equality": ("solref solimp", ""),
    "include": ("file", ""),
    "visual": ("", "headlight rgba global"),
    "headlight": ("diffuse ambient specular", ""),
    "rgba": ("haze", ""),
    "global": ("azimuth elevation", ""),
    "texture": ("type builtin name rgb1 rgb2 width height mark markrgb", ""),
    "light": ("pos dir directional", ""),
    "keyframe": ("", "key"),
    "key": ("name qpos ctrl", ""),
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def numbers(value, count):
    require(value is not None, "missing required numeric attribute")
    result = [float(x) for x in value.split()]
    require(len(result) == count and all(math.isfinite(x) for x in result),
            f"expected {count} finite numbers, got {value!r}")
    return result


def transpose(a):
    return [list(x) for x in zip(*a)]


def dot(a, b):
    # Explicit left-to-right accumulation: Python's built-in float sum changed
    # algorithm in 3.12. Regeneration must not depend on that implementation.
    result = 0.0
    for x, y in zip(a, b):
        result += x * y
    return result


def mul(a, b):
    return [[dot(a[i], (b[k][j] for k in range(3))) for j in range(3)] for i in range(3)]


def rotate(a, v):
    return [dot(row, v) for row in a]


def determinant(a):
    return (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1])
            - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
            + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))


def error(a, b):
    return max(abs(x - y) for x, y in zip(a, b))


def matrix_error(a, b):
    return max(error(x, y) for x, y in zip(a, b))


def normalized(q):
    length = math.sqrt(dot(q, q))
    require(math.isfinite(length) and length > 1e-20, "zero/invalid quaternion")
    return [x / length for x in q]


def quaternion_matrix(q):
    # xyzw, without renormalizing: validation includes float32 storage error.
    x, y, z, w = q
    return [[1 - 2 * (y*y + z*z), 2 * (x*y - z*w), 2 * (x*z + y*w)],
            [2 * (x*y + z*w), 1 - 2 * (x*x + z*z), 2 * (y*z - x*w)],
            [2 * (x*z - y*w), 2 * (y*z + x*w), 1 - 2 * (x*x + y*y)]]


def matrix_quaternion(a):
    # Largest-component branch, deterministic tie order w,x,y,z.
    candidates = [1 + a[0][0] + a[1][1] + a[2][2],
                  1 + a[0][0] - a[1][1] - a[2][2],
                  1 - a[0][0] + a[1][1] - a[2][2],
                  1 - a[0][0] - a[1][1] + a[2][2]]
    k = max(range(4), key=lambda i: candidates[i])
    s = math.sqrt(candidates[k]) * 2
    if k == 0:
        q = [(a[2][1]-a[1][2])/s, (a[0][2]-a[2][0])/s, (a[1][0]-a[0][1])/s, s/4]
    elif k == 1:
        q = [s/4, (a[0][1]+a[1][0])/s, (a[0][2]+a[2][0])/s, (a[2][1]-a[1][2])/s]
    elif k == 2:
        q = [(a[0][1]+a[1][0])/s, s/4, (a[1][2]+a[2][1])/s, (a[0][2]-a[2][0])/s]
    else:
        q = [(a[0][2]+a[2][0])/s, (a[1][2]+a[2][1])/s, s/4, (a[1][0]-a[0][1])/s]
    q = normalized(q)
    # q and -q denote the same rotation; pick a reproducible representative.
    for i in (3, 0, 1, 2):
        if abs(q[i]) > 1e-15:
            return [-x for x in q] if q[i] < 0 else q
    raise ValueError("invalid rotation matrix")


def compose(a, b):
    ar, at = a
    br, bt = b
    return mul(ar, br), [x + y for x, y in zip(at, rotate(ar, bt))]


def inverse(t):
    r = transpose(t[0])
    return r, rotate(r, [-x for x in t[1]])


def translation(v):
    return IDENTITY, v


def xml_frame(element):
    w, x, y, z = numbers(element.get("quat", "1 0 0 0"), 4)
    return quaternion_matrix(normalized([x, y, z, w])), numbers(element.get("pos", "0 0 0"), 3)


def hinge_frame(axis, angle):
    s = math.sin(angle / 2)
    return quaternion_matrix([x * s for x in axis] + [math.cos(angle / 2)]), ZERO


def principal_axes(tensor):
    """Fixed 32 cyclic Jacobi sweeps (01,02,12), ascending eigenvalues.

    Columns are unit eigenvectors, each largest-magnitude entry positive (xyz
    tie order). Flip the last column if necessary to keep determinant +1.
    No platform LAPACK, randomized pivot choice or dependency on mesh geometry.
    """
    a = [row[:] for row in tensor]
    v = [row[:] for row in IDENTITY]
    scale = max(abs(x) for row in a for x in row)
    require(scale > 0, "zero inertia tensor")
    for _ in range(32):
        for p, q in ((0, 1), (0, 2), (1, 2)):
            if abs(a[p][q]) <= scale * 1e-16:
                continue
            tau = (a[q][q] - a[p][p]) / (2 * a[p][q])
            t = math.copysign(1.0, tau) / (abs(tau) + math.sqrt(1 + tau*tau))
            c = 1 / math.sqrt(1 + t*t)
            s = t * c
            app, aqq, apq = a[p][p], a[q][q], a[p][q]
            a[p][p], a[q][q] = app - t*apq, aqq + t*apq
            a[p][q] = a[q][p] = 0.0
            for k in range(3):
                if k not in (p, q):
                    akp, akq = a[k][p], a[k][q]
                    a[k][p] = a[p][k] = c*akp - s*akq
                    a[k][q] = a[q][k] = s*akp + c*akq
                vkp, vkq = v[k][p], v[k][q]
                v[k][p], v[k][q] = c*vkp - s*vkq, s*vkp + c*vkq
    order = sorted(range(3), key=lambda i: (a[i][i], i))
    values = [a[i][i] for i in order]
    v = [[row[i] for i in order] for row in v]
    for j in range(3):
        largest = max(range(3), key=lambda i: abs(v[i][j]))
        if v[largest][j] < 0:
            for row in v:
                row[j] = -row[j]
    if determinant(v) < 0:
        for row in v:
            row[2] = -row[2]
    require(all(x > 0 and math.isfinite(x) for x in values), "nonpositive principal inertia")
    require(abs(determinant(v) - 1) < 1e-12, "principal basis is not a proper rotation")
    require(matrix_error(mul(transpose(v), v), IDENTITY) < 1e-12, "nonorthogonal principal basis")
    diagonal = [[values[i] if i == j else 0 for j in range(3)] for i in range(3)]
    require(matrix_error(mul(mul(v, diagonal), transpose(v)), tensor) < scale * 1e-13,
            "Jacobi tensor reconstruction failed")
    return values, v


def validate_xml(element):
    require(element.tag in SCHEMA, f"unsupported XML element {element.tag}")
    attrs, children = SCHEMA[element.tag]
    unknown = set(element.attrib) - set(attrs.split())
    require(not unknown, f"unsupported {element.tag} attributes: {sorted(unknown)}")
    require(not (element.text or "").strip(), f"unexpected text in {element.tag}")
    for child in element:
        require(child.tag in children.split(), f"unsupported {child.tag} inside {element.tag}")
        require(not (child.tail or "").strip(), f"unexpected text after {child.tag}")
        validate_xml(child)


def exactly_one(element, tag):
    matches = element.findall(tag)
    require(len(matches) == 1, f"expected exactly one {tag} in {element.tag}")
    return matches[0]


def default_classes(robot):
    classes = {}

    def visit(element, inherited):
        values = {k: v.copy() for k, v in inherited.items()}
        for child in element:
            if child.tag != "default":
                values.setdefault(child.tag, {}).update(child.attrib)
        name = element.get("class")
        if name is not None:
            require(name not in classes, f"duplicate default class {name}")
            classes[name] = values
        for child in element.findall("default"):
            require(child.get("class") is not None, "unnamed nested default")
            visit(child, values)
        return values

    main = {}
    for element in robot.findall("default"):
        require(element.get("class") is None, "unexpected top-level default class")
        main = visit(element, main)
    return classes


@dataclass
class SourceBody:
    name: str
    parent: int
    local: tuple
    mass: float
    com: list
    full: list
    tensor: list
    joint_name: str
    joint_pos: list
    axis: list
    lower: float
    upper: float
    home: float
    damping: float
    armature: float
    effort: float
    frames: list


def read_model(source_dir):
    data = {}
    for name, expected in HASHES.items():
        raw = (source_dir / name).read_bytes()
        actual = hashlib.sha256(raw).hexdigest()
        require(actual == expected, f"{name}: SHA-256 {actual}, expected pinned {expected}")
        data[name] = raw
    require(not any(p.suffix.lower() in (".stl", ".obj", ".ply", ".glb", ".gltf")
                    for p in source_dir.rglob("*")), "upstream directory must not contain meshes")
    robot, scene = (ET.fromstring(data[name]) for name in ("robot_walk.xml", "scene_walk.xml"))
    for root, model in ((robot, "microduck"), (scene, "scene")):
        validate_xml(root)
        require(root.tag == "mujoco" and root.attrib == {"model": model}, "unexpected MJCF model")
    require(exactly_one(robot, "compiler").attrib == {
        "angle": "radian", "meshdir": "assets", "autolimits": "true"}, "unexpected compiler settings")
    require(exactly_one(scene, "include").attrib == {"file": "robot_walk.xml"}, "unsupported scene include")
    require(not robot.findall("include"), "unsupported robot include")
    require(len(exactly_one(robot, "equality")) == 0, "active loop/equality constraints are unsupported")
    require(not scene.findall("equality") and not scene.findall("actuator")
            and not scene.findall("default") and not scene.findall("compiler")
            and not scene.findall("sensor"), "unsupported scene physics settings")
    world = exactly_one(robot, "worldbody")
    require(len(world) == 1 and world[0].tag == "body", "expected one robot root only")
    require(all(e.tag in ("light", "geom") for e in exactly_one(scene, "worldbody")),
            "scene may not add bodies")
    classes = default_classes(robot)
    chosen = classes["chosen_actuator"]
    require(chosen["joint"] == {"damping": "0.053", "frictionloss": "0.0048", "armature": "0.0018"},
            "unexpected chosen_actuator joint defaults")
    require(chosen["position"] == {"kp": "0.55", "kv": "0.0", "forcerange": "-0.96 0.96",
                                    "ctrlrange": "-10.0 10.0"}, "unexpected chosen_actuator servo defaults")
    keys = exactly_one(scene, "keyframe").findall("key")
    stand = [key for key in keys if key.get("name") == "STAND"]
    require(len(stand) == 1, "expected exactly one active STAND key")
    qpos = numbers(stand[0].get("qpos"), 21)
    require(error(qpos[:7], [0, 0, 0.12, 1, 0, 0, 0]) == 0, "unexpected STAND free-root pose")
    require(error(numbers(stand[0].get("ctrl"), 14), qpos[7:]) == 0, "STAND controls differ from qpos")
    bodies = []

    def visit(element, parent, inherited_class):
        index = len(bodies)
        root = parent == -1
        childclass = element.get("childclass", inherited_class)
        require(childclass == "microduck", "unexpected body default class")
        name = element.get("name")
        require(name and name not in [b.name for b in bodies], f"missing/duplicate body name {name}")
        inertial = exactly_one(element, "inertial")
        mass = numbers(inertial.get("mass"), 1)[0]
        require(mass > 0, f"nonpositive mass for {name}")
        com = numbers(inertial.get("pos"), 3)
        full = numbers(inertial.get("fullinertia"), 6)
        xx, yy, zz, xy, xz, yz = full
        tensor = [[xx, xy, xz], [xy, yy, yz], [xz, yz, zz]]
        local = xml_frame(element)
        joint_name, jpos, axis = "", ZERO, ZERO
        lower = upper = home = damping = armature = effort = 0.0
        if root:
            require(name == "trunk_base" and len(element.findall("joint")) == 0, "unexpected root")
            require(exactly_one(element, "freejoint").attrib == {"name": "trunk_base_freejoint"},
                    "unexpected root freejoint")
            require(error(local[1], [0, 0, 0.12]) == 0 and matrix_error(local[0], IDENTITY) == 0,
                    "unexpected source root placement")
        else:
            require(not element.findall("freejoint"), "non-root freejoint")
            joint = exactly_one(element, "joint")
            require(joint.get("type") == "hinge" and joint.get("class") == "chosen_actuator",
                    "only chosen_actuator hinges are supported")
            resolved = {**chosen["joint"], **joint.attrib}
            require(resolved["frictionloss"] == "0.0048", "unexpected omitted Coulomb friction")
            damping, armature = (numbers(resolved[key], 1)[0] for key in ("damping", "armature"))
            joint_name = joint.get("name")
            require(joint_name and joint_name not in [b.joint_name for b in bodies], "duplicate joint name")
            jpos = numbers(joint.get("pos", "0 0 0"), 3)
            axis = numbers(joint.get("axis"), 3)
            require(abs(dot(axis, axis) - 1) < 1e-12, "nonunit hinge axis")
            lower, upper = numbers(joint.get("range"), 2)
            home = qpos[7 + index - 1]
            require(lower <= home <= upper, f"STAND outside hinge range for {joint_name}")
        frames = []
        for e in element:
            if e.tag in ("geom", "site", "camera"):
                if e.tag == "geom":
                    require(e.get("type") == "mesh" and e.get("class") in
                            ("visual", "collision", "self_collision_only"), "unexpected robot geom")
                frames.append(xml_frame(e))
        bodies.append(SourceBody(name, parent, local, mass, com, full, tensor, joint_name,
                                 jpos, axis, lower, upper, home, damping, armature, effort, frames))
        for child in element.findall("body"):
            visit(child, index, childclass)

    visit(world[0], -1, "")
    require(len(bodies) == 15, f"expected 15 bodies, got {len(bodies)}")
    actuators = list(exactly_one(robot, "actuator"))
    require(len(actuators) == 14, "expected 14 actuators")
    for body, actuator in zip(bodies[1:], actuators):
        require(actuator.attrib == {"class": "chosen_actuator", "name": body.joint_name,
                                    "joint": body.joint_name}, "unexpected actuator mapping/order")
        lo, hi = numbers(chosen["position"]["forcerange"], 2)
        require(lo == -hi and hi > 0, "expected symmetric positive effort range")
        body.effort = hi
    return bodies


def f32(value):
    result = struct.unpack("<f", struct.pack("<f", value))[0]
    require(math.isfinite(result), "value does not fit float32")
    return 0.0 if result == 0 else result


def rounded(value):
    return [f32(x) for x in value] if isinstance(value, list) else f32(value)


def convert(bodies):
    bases, records = [], []
    for b in bodies:
        values, basis = principal_axes(b.tensor)
        bases.append(basis)
        child = compose((transpose(basis), ZERO), translation(b.joint_pos))
        parent = ((IDENTITY, ZERO) if b.parent == -1 else
                  compose(compose((transpose(bases[b.parent]), ZERO), b.local), translation(b.joint_pos)))
        record = {
            "parent": b.parent, "mass": b.mass, "inertia": values,
            "centerOfMass": rotate(transpose(basis), b.com),
            "principalRotation": matrix_quaternion(basis),
            "parentPosition": parent[1], "parentRotation": matrix_quaternion(parent[0]),
            "childPosition": child[1], "childRotation": matrix_quaternion(child[0]),
            "axis": b.axis, "lower": b.lower, "upper": b.upper, "home": b.home,
            "damping": b.damping, "armature": b.armature, "effortLimit": b.effort,
        }
        records.append({k: v if k == "parent" else rounded(v) for k, v in record.items()})
    return records


def source_fk(bodies, home):
    result = []
    for b in bodies:
        if b.parent == -1:
            result.append((IDENTITY, ZERO))  # Relative to original root, no .12 offset/up conversion.
        else:
            motion = compose(compose(translation(b.joint_pos), hinge_frame(b.axis, b.home if home else 0)),
                             translation([-x for x in b.joint_pos]))
            result.append(compose(compose(result[b.parent], b.local), motion))
    return result


def converted_fk(records, home):
    result = []
    for r in records:
        if r["parent"] == -1:
            result.append((quaternion_matrix(r["principalRotation"]), ZERO))
        else:
            parent = quaternion_matrix(r["parentRotation"]), r["parentPosition"]
            child = quaternion_matrix(r["childRotation"]), r["childPosition"]
            motion = hinge_frame(r["axis"], r["home"] if home else 0)
            result.append(compose(compose(compose(result[r["parent"]], parent), motion), inverse(child)))
    return result


def validate(bodies, records):
    bounds = dict(inertia_absolute=0.0, inertia_relative=0.0, com_local=0.0,
                  fk_position=0.0, fk_rotation=0.0, com_world=0.0,
                  local_frame_position=0.0, local_frame_rotation=0.0)
    for b, r in zip(bodies, records):
        p = quaternion_matrix(r["principalRotation"])
        require(min(r["inertia"]) > 0, "float32 principal inertia lost positivity")
        for key in ("principalRotation", "parentRotation", "childRotation"):
            m = quaternion_matrix(r[key])
            require(abs(determinant(m) - 1) < 1e-6
                    and matrix_error(mul(transpose(m), m), IDENTITY) < 1e-6,
                    f"{b.name}: float32 {key} is not a proper rotation within tolerance")
        d = [[r["inertia"][i] if i == j else 0 for j in range(3)] for i in range(3)]
        absolute = matrix_error(mul(mul(p, d), transpose(p)), b.tensor)
        relative = absolute / max(abs(x) for row in b.tensor for x in row)
        bounds["inertia_absolute"] = max(bounds["inertia_absolute"], absolute)
        bounds["inertia_relative"] = max(bounds["inertia_relative"], relative)
        bounds["com_local"] = max(bounds["com_local"], error(rotate(p, r["centerOfMass"]), b.com))
    for home in (False, True):
        source, converted = source_fk(bodies, home), converted_fk(records, home)
        for b, r, old, new in zip(bodies, records, source, converted):
            p_inverse = transpose(quaternion_matrix(r["principalRotation"])), ZERO
            reconstructed = compose(new, p_inverse)
            bounds["fk_position"] = max(bounds["fk_position"], error(old[1], reconstructed[1]))
            bounds["fk_rotation"] = max(bounds["fk_rotation"], matrix_error(old[0], reconstructed[0]))
            bounds["com_world"] = max(bounds["com_world"], error(
                compose(old, translation(b.com))[1], compose(new, translation(r["centerOfMass"]))[1]))
            # All 83 source geom/site/camera frames are checked, without opening
            # an asset. Games reconstruct these frames with principalRotation.
            for frame in b.frames:
                expected = compose(old, frame)
                actual = compose(new, compose(p_inverse, frame))
                bounds["local_frame_position"] = max(bounds["local_frame_position"], error(expected[1], actual[1]))
                bounds["local_frame_rotation"] = max(bounds["local_frame_rotation"], matrix_error(expected[0], actual[0]))
    require(bounds["inertia_relative"] <= 1e-6, f"full tensor float32 reconstruction error: {bounds}")
    require(all(value <= 2e-6 for key, value in bounds.items() if key != "inertia_relative"),
            f"float32 frame/COM reconstruction error: {bounds}")
    return bounds


FIELDS = [
    ("int", "parent"), ("float", "mass"), ("float3", "inertia"), ("float3", "centerOfMass"),
    ("quaternion", "principalRotation"), ("float3", "parentPosition"), ("quaternion", "parentRotation"),
    ("float3", "childPosition"), ("quaternion", "childRotation"), ("float3", "axis"),
    ("float", "lower"), ("float", "upper"), ("float", "home"), ("float", "damping"),
    ("float", "armature"), ("float", "effortLimit"),
]


def number(value):
    result = format(f32(value), ".9g")
    return result if "." in result or "e" in result else result + ".0"


def model_text(bodies, records):
    lines = ["// Generated by tools/convert.py; do not edit. See UPSTREAM.md.",
             "// Pollen Robotics MJCF data, Apache-2.0; no upstream meshes.",
             "// principalRotation maps principal-body local into original MJCF local.",
             "// Original visual world rotation = body.rotation * Inverse(principalRotation).",
             "// Root incoming-frame fields are unused; the fixture supplies its world anchor.",
             "const int DUCK_BODY_COUNT = 15;", "const int DUCK_JOINT_COUNT = 14;", "",
             "struct DuckBodyDefinition", "{"]
    lines += [f"    {kind} {key};" for kind, key in FIELDS]
    lines += ["}", "", "List<DuckBodyDefinition> DuckModel()", "{", "    return ["]
    for b, record in zip(bodies, records):
        lines += [f"        // {b.name}" + (f" / {b.joint_name}" if b.joint_name else " / root"),
                  "        DuckBodyDefinition {"]
        for i, (kind, key) in enumerate(FIELDS):
            v = record[key]
            value = (str(v) if kind == "int" else
                     kind + "(" + ", ".join(number(x) for x in v) + ")" if isinstance(v, list) else number(v))
            lines.append(f"            {key} = {value}" + ("," if i < len(FIELDS)-1 else ""))
        lines.append("        }" + ("," if b is not bodies[-1] else ""))
    lines += ["    ];", "}", ""]
    for name, argument, names in (("DuckJointName", "jointIndex", [b.joint_name for b in bodies[1:]]),
                                  ("DuckBodyName", "bodyIndex", [b.name for b in bodies])):
        lines += [f"string {name}(int {argument})", "{", f"    switch ({argument})", "    {"]
        lines += [f'        case {i}: return "{value}";' for i, value in enumerate(names)]
        lines += ["    }", '    return "";', "}", ""]
    return "\n".join(lines)


def expected_text(bodies):
    lines = ["/* Generated by tools/convert.py from ORIGINAL MJCF tensors/frames, Apache-2.0.",
             " * Independent of principal-axis model records. Original root-local axes;",
             " * no .12 root height or Tide up-axis conversion. Quaternions are xyzw.",
             " * full_inertia order: xx, yy, zz, xy, xz, yz (about authored COM). */",
             "#ifndef MICRODUCK_MODEL_EXPECTED_H", "#define MICRODUCK_MODEL_EXPECTED_H", "",
             "typedef struct duck_model_expected_body {", "    int parent;", "    float mass;",
             "    float center_of_mass[3];", "    float full_inertia[6];",
             "    float local_position[3];", "    float local_rotation[4];",
             "    float joint_position[3];", "    float axis[3];", "    float home;",
             "    float zero_position[3];", "    float zero_rotation[4];",
             "    float home_position[3];", "    float home_rotation[4];",
             "} duck_model_expected_body;", "", "static const duck_model_expected_body duck_model_expected[15] = {"]
    zero, home = source_fk(bodies, False), source_fk(bodies, True)
    for i, b in enumerate(bodies):
        local = (IDENTITY, ZERO) if b.parent == -1 else b.local
        values = [b.mass, b.com, b.full, local[1], matrix_quaternion(local[0]), b.joint_pos, b.axis, b.home,
                  zero[i][1], matrix_quaternion(zero[i][0]), home[i][1], matrix_quaternion(home[i][0])]
        lines += [f"    /* {i}: {b.name} */", "    {", f"        {b.parent},"]
        for value in values:
            c = ("{" + ", ".join(number(x) + "f" for x in value) + "}"
                 if isinstance(value, list) else number(value) + "f")
            lines.append("        " + c + ",")
        lines.append("    },")
    lines += ["};", "", "#endif", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="validate inputs/math and require byte-identical outputs")
    parser.add_argument("--source-dir", type=Path, default=BASE / "upstream", help="directory of the three pinned input files")
    args = parser.parse_args()
    try:
        bodies = read_model(args.source_dir)
        records = convert(bodies)
        bounds = validate(bodies, records)
        outputs = {BASE / "model.tide": model_text(bodies, records),
                   BASE / "test/model_expected.h": expected_text(bodies)}
        for path, content in outputs.items():
            encoded = content.encode("utf-8")
            if args.check:
                require(path.is_file() and path.read_bytes() == encoded,
                        f"{path.relative_to(BASE)} is stale; run tools/convert.py")
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(encoded)
        print(f"{'Checked' if args.check else 'Generated'} 15 bodies, 14 hinges, "
              f"{sum(len(b.frames) for b in bodies)} local frames; pinned {REVISION}")
        for key, value in bounds.items():
            print(f"  max {key}: {value:.9g}")
    except (ValueError, OSError, ET.ParseError, KeyError, OverflowError) as exc:
        print(f"MicroDuck conversion failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
