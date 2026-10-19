# Shared helpers for GRAVEWAKE's people tools (Blender 4.4, run headless).
#
# make_bodies.py and make_moves.py import this module from their own folder. It knows the
# Quaternius CharacterArmature rig (Ultimate Modular Men/Women) and what these tools do to it:
#
# - restructure(): the packs' rig has IK-style feet (Foot.L/R and the pole helpers PT.L/R hang off
#   Root and are placed by translation keys). Every file written here instead hangs Foot.L/R off
#   LowerLeg.L/R with the foot's pivot at the ankle (the lower leg's tail), and drops PT.L/R (they
#   carry no weights). Legs are then plain FK, so a clip needs only rotations below the hips.
# - Target / retarget(): turns per-bone world-space rotation deltas plus a hip position into pose
#   rotations for that FK skeleton, with the Body (hips) bone the only one translated.
# - prune_glb(): rewrites an exported clip catalog to keep exactly the channels wanted.
import json
import math
import os
import struct

import bpy
from mathutils import Matrix, Quaternion, Vector

FPS = 30  # Every source clip used here is keyed at 30 fps.
DROP_BONES = ("PT.L", "PT.R")


def srgb(hexcode):
    """'#rrggbb' (display colour) to the linear RGB Blender and glTF store."""
    h = hexcode.lstrip("#")
    c = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return tuple(x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c)


def load(path):
    """Empty scene at 30 fps (so 30 fps keys land on whole frames), the glTF imported,
    its armature returned with NLA tracks and the active action cleared and the pose at rest."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.fps = FPS
    scene.render.fps_base = 1.0
    bpy.ops.import_scene.gltf(filepath=path)
    arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    clear_animation(arm)
    return arm


def clear_animation(arm):
    ad = arm.animation_data
    if ad:
        for track in list(ad.nla_tracks):
            ad.nla_tracks.remove(track)
        ad.action = None
    for pb in arm.pose.bones:
        pb.rotation_mode = "QUATERNION"
        pb.matrix_basis = Matrix.Identity(4)


def restructure(arm):
    """FK feet: Foot.L/R become children of LowerLeg.L/R, their head moved to the ankle; PT.L/R removed."""
    for obj in bpy.data.objects:
        if obj.type != "MESH":
            continue
        for name in DROP_BONES:
            group = obj.vertex_groups.get(name)
            if not group:
                continue
            for v in obj.data.vertices:
                for g in v.groups:
                    if g.group == group.index and g.weight > 0:
                        raise SystemExit(f"{obj.name}: {name} has weights; it can't be dropped")
            obj.vertex_groups.remove(group)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    bones = arm.data.edit_bones
    for side in ("L", "R"):
        foot, shin = bones[f"Foot.{side}"], bones[f"LowerLeg.{side}"]
        toe = foot.tail.copy()
        foot.use_connect = False
        foot.parent = shin
        foot.head = shin.tail.copy()
        foot.tail = toe
    for name in DROP_BONES:
        bones.remove(bones[name])
    bpy.ops.object.mode_set(mode="OBJECT")


def bone_order(arm):
    """Bone names, parents first."""
    out = []

    def visit(bone):
        out.append(bone.name)
        for child in bone.children:
            visit(child)

    for bone in arm.data.bones:
        if bone.parent is None:
            visit(bone)
    return out


def rotq(m):
    return m.to_3x3().normalized().to_quaternion()


def sample(arm, action, frames):
    """Armature-space pose matrices of every bone at each frame of the action."""
    scene = bpy.context.scene
    ad = arm.animation_data or arm.animation_data_create()
    ad.action = action
    if ad.action_slot is None and len(action.slots):
        ad.action_slot = action.slots[0]
    out = []
    for f in frames:
        scene.frame_set(f)
        out.append({pb.name: (arm.matrix_world @ pb.matrix).copy() for pb in arm.pose.bones})
    ad.action = None
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    return out


def clip_frames(action):
    f0, f1 = action.frame_range
    return list(range(int(round(f0)), int(round(f1)) + 1))


class Target:
    """Rest data of a restructured CharacterArmature and the solve from world deltas to pose keys."""

    def __init__(self, arm):
        self.order = bone_order(arm)
        bones = arm.data.bones
        self.parent = {b.name: (b.parent.name if b.parent else None) for b in bones}
        self.R = {b.name: b.matrix_local.to_quaternion() for b in bones}
        self.h = {b.name: b.head_local.copy() for b in bones}
        self.t = {b.name: b.tail_local.copy() for b in bones}
        self.hipmid = (self.h["UpperLeg.L"] + self.h["UpperLeg.R"]) / 2
        self.ankle = (self.t["LowerLeg.L"].z + self.t["LowerLeg.R"].z) / 2
        self.toe = (self.t["Foot.L"].z + self.t["Foot.R"].z) / 2

    def solve(self, D, hip):
        """D: bone -> world-space rotation delta from rest (bones left out follow their parent).
        hip: wanted world position of the hip-joint midpoint (between the UpperLeg heads).
        Returns (rotation per bone as pose quaternions, Body location, ankle positions, toe positions)."""
        full = {}
        for n in self.order:
            p = self.parent[n]
            # Root never moves: every clip here is in place.
            full[n] = Quaternion() if n == "Root" else D.get(n, full[p] if p else Quaternion())
        body = hip - full["Body"] @ (self.hipmid - self.h["Body"])
        rot = {}
        for n in self.order:
            p = self.parent[n]
            dp = full[p] if p else Quaternion()
            rot[n] = self.R[n].inverted() @ dp.inverted() @ full[n] @ self.R[n]
        pos = {}
        for n in self.order:
            p = self.parent[n]
            if n == "Body":
                pos[n] = body
            elif p is None:
                pos[n] = self.h[n].copy()
            else:
                pos[n] = pos[p] + full[p] @ (self.h[n] - self.h[p])
        ankles = [pos[f"Foot.{s}"] for s in ("L", "R")]  # a foot's head is its lower leg's tail
        toes = [pos[f"Foot.{s}"] + full[f"Foot.{s}"] @ (self.t[f"Foot.{s}"] - self.h[f"Foot.{s}"]) for s in ("L", "R")]
        loc = self.R["Body"].inverted() @ (body - self.h["Body"])
        self.pos = pos
        self.head_top = pos["Head"] + full["Head"] @ (self.t["Head"] - self.h["Head"])
        return rot, loc, ankles, toes

    def contact(self, ankles, toes):
        """Height of the lowest of heels (ankles) and toes above where they stand at rest."""
        return min(min(a.z for a in ankles) - self.ankle, min(t.z for t in toes) - self.toe)


def write_action(arm, name, rots, locs):
    """rots: list over frames of {bone: Quaternion}; locs: list of Body locations. Keys on whole frames."""
    act = bpy.data.actions.new(name)
    act.use_fake_user = True
    n = len(rots)
    for bone in rots[0]:
        prev = None
        series = []
        for r in rots:
            q = r[bone].copy()
            if prev is not None and prev.dot(q) < 0:
                q.negate()
            series.append(q)
            prev = q
        for i in range(4):
            fc = act.fcurves.new(f'pose.bones["{bone}"].rotation_quaternion', index=i, action_group=bone)
            fc.keyframe_points.add(n)
            co = []
            for f, q in enumerate(series):
                co += [f, q[i]]
            fc.keyframe_points.foreach_set("co", co)
            for k in fc.keyframe_points:
                k.interpolation = "LINEAR"
            fc.update()
    for i in range(3):
        fc = act.fcurves.new('pose.bones["Body"].location', index=i, action_group="Body")
        fc.keyframe_points.add(n)
        co = []
        for f, v in enumerate(locs):
            co += [f, v[i]]
        fc.keyframe_points.foreach_set("co", co)
        for k in fc.keyframe_points:
            k.interpolation = "LINEAR"
        fc.update()
    ad = arm.animation_data or arm.animation_data_create()
    ad.action = act  # gives the action an object slot, so the exporter takes it as the armature's
    ad.action = None
    return act


# ---------------------------------------------------------------- GLB reading / rewriting

COMPONENTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
FORMATS = {5126: "f", 5123: "H", 5121: "B", 5125: "I", 5122: "h", 5120: "b"}


def read_glb(path):
    data = open(path, "rb").read()
    magic, version, length = struct.unpack_from("<III", data, 0)
    assert magic == 0x46546C67 and version == 2
    jlen, jtype = struct.unpack_from("<II", data, 12)
    gltf = json.loads(data[20:20 + jlen])
    binary = b""
    off = 20 + jlen
    if off < length:
        blen, btype = struct.unpack_from("<II", data, off)
        binary = data[off + 8:off + 8 + blen]
    return gltf, binary


def accessor_values(gltf, binary, index):
    acc = gltf["accessors"][index]
    view = gltf["bufferViews"][acc["bufferView"]]
    comps = COMPONENTS[acc["type"]]
    fmt = FORMATS[acc["componentType"]]
    size = struct.calcsize(fmt)
    stride = view.get("byteStride", comps * size)
    base = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    out = []
    for i in range(acc["count"]):
        out.append(struct.unpack_from("<" + fmt * comps, binary, base + i * stride))
    return out


def write_glb(path, gltf, binary):
    js = json.dumps(gltf, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    binary += b"\0" * ((4 - len(binary) % 4) % 4)
    total = 12 + 8 + len(js) + (8 + len(binary) if binary else 0)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A))
        f.write(js)
        if binary:
            f.write(struct.pack("<II", len(binary), 0x004E4942))
            f.write(binary)


def prune_glb(path, translated=("Body",)):
    """Keep only rotation channels, plus translation on the bones named in `translated`; drop scale.
    A channel whose keys never change is cut to its first and last key. The file must be a clip
    catalog: nodes, animations and at most a skin (the exporter writes one for the armature), no meshes."""
    gltf, binary = read_glb(path)
    assert not gltf.get("meshes") and not gltf.get("images"), "prune_glb is for clip catalogs"
    names = [n.get("name") for n in gltf["nodes"]]
    blobs = []          # new accessor payloads
    accessors = []
    seen = {}

    def add(kind, values, minmax):
        flat = [x for v in values for x in v]
        raw = struct.pack("<%df" % len(flat), *flat)
        key = (kind, raw)
        if key in seen:
            return seen[key]
        acc = {"bufferView": len(blobs), "componentType": 5126, "count": len(values), "type": kind}
        if minmax:
            acc["min"] = [min(v[i] for v in values) for i in range(len(values[0]))]
            acc["max"] = [max(v[i] for v in values) for i in range(len(values[0]))]
        blobs.append(raw)
        accessors.append(acc)
        seen[key] = len(accessors) - 1
        return seen[key]

    for skin in gltf.get("skins", []):
        if "inverseBindMatrices" in skin:
            skin["inverseBindMatrices"] = add("MAT4", accessor_values(gltf, binary, skin["inverseBindMatrices"]), False)
    for anim in gltf.get("animations", []):
        channels, samplers = [], []
        for ch in anim["channels"]:
            node = names[ch["target"]["node"]]
            path_ = ch["target"]["path"]
            if not (path_ == "rotation" or (path_ == "translation" and node in translated)):
                continue
            s = anim["samplers"][ch["sampler"]]
            times = accessor_values(gltf, binary, s["input"])
            values = accessor_values(gltf, binary, s["output"])
            if all(max(abs(a - b) for a, b in zip(v, values[0])) < 1e-6 for v in values):
                times, values = [times[0], times[-1]], [values[0], values[0]]
            samplers.append({"input": add("SCALAR", times, True),
                             "output": add("VEC4" if path_ == "rotation" else "VEC3", values, False),
                             "interpolation": s.get("interpolation", "LINEAR")})
            channels.append({"sampler": len(samplers) - 1, "target": ch["target"]})
        anim["channels"], anim["samplers"] = channels, samplers
    out = b""
    views = []
    for raw in blobs:
        views.append({"buffer": 0, "byteOffset": len(out), "byteLength": len(raw)})
        out += raw + b"\0" * ((4 - len(raw) % 4) % 4)
    gltf["accessors"] = accessors
    gltf["bufferViews"] = views
    gltf["buffers"] = [{"byteLength": len(out)}]
    write_glb(path, gltf, out)
