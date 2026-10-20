# GRAVEWAKE's kinds of the dead: each kind but the ordinary dead its own body,
# grown from one of the dead of art/people as the bosses are (its bones grown,
# shrunk and bent, growths and spikes put on them: art/mutants/tools/
# make_mutants.py), with the horde's clips (art/crowd/tools/make_crowd.py's)
# baked into a texture of numbers, so crowd.tide plays them as it does the
# horde's.
#
#   blender -b --factory-startup --python make_kinds.py -- <art/people dir> <out dir> [--only runner] [--tris 1000]
#
# Writes <out>/<kind>.glb (a static mesh: positions, normals, colours, and
# TEXCOORD_1: x 1 on the eyes, 0.5 on the growths that glow, 0 elsewhere; y the
# corner's column in the texture), <out>/anim.png and <out>/kinds.json, laid
# out as the horde's are, the kinds in their names' order (as a glob of the
# folder lists their files). Each kind's entry in kinds.json also says where
# its head, chest and hips are at the walk's first frame, in the game's axes,
# for zombies.tide's hit spots.
import json
import math
import os
import struct
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Euler, Matrix, Vector

argv = sys.argv[sys.argv.index("--") + 1:]
PEOPLE, OUT = os.path.abspath(argv[0]), os.path.abspath(argv[1])
TRIS = int(argv[argv.index("--tris") + 1]) if "--tris" in argv else 1000
ONLY = argv[argv.index("--only") + 1].split(",") if "--only" in argv else None

MEN, WOMEN = "moves.glb", "moves_women.glb"

# name: the body, its catalog, {bone: ((scale across, along, across), bend degrees)}, growths, colours
#   a growth: (bone, where along it 0-1, offset across it (x, z: z forward) in metres of the grown bone, radius, kind)
#   kind: "glow" (lit in its own colour, brighter as the kind winds up), "lump" (flesh, or iron), "spike" (a cone
#   pointing out the way it's offset)
RECIPES = {
    # Lean and hunched, its limbs long and thin and its claws longer, its head thrust out; bone breaking through its
    # spine and elbows
    "runner": {
        "body": "zombie_worker", "catalog": MEN,
        "bones": {
            "Abdomen": ((0.78, 1.05, 0.72), 14),
            "Torso": ((0.78, 1.0, 0.72), 16),
            "Chest": ((0.85, 1.0, 0.8), 18),
            "Neck": ((0.8, 1.25, 0.8), 4),
            "Head": ((0.88, 0.95, 1.1), -24),
            "Shoulder.L": ((0.85, 1.1, 0.85), 0), "Shoulder.R": ((0.85, 1.1, 0.85), 0),
            "UpperArm.L": ((0.62, 1.35, 0.62), 0), "UpperArm.R": ((0.62, 1.35, 0.62), 0),
            "LowerArm.L": ((0.58, 1.45, 0.58), 0), "LowerArm.R": ((0.58, 1.45, 0.58), 0),
            "Wrist.L*": ((1.25, 1.6, 1.25), 0), "Wrist.R*": ((1.25, 1.6, 1.25), 0),
            "UpperLeg.L": ((0.72, 1.12, 0.72), 0), "UpperLeg.R": ((0.72, 1.12, 0.72), 0),
            "LowerLeg.L": ((0.66, 1.18, 0.66), 0), "LowerLeg.R": ((0.66, 1.18, 0.66), 0),
        },
        "growths": [
            ("Torso", 0.4, (0.0, -0.1), 0.035, "spike"), ("Torso", 0.85, (0.0, -0.1), 0.04, "spike"),
            ("Chest", 0.35, (0.0, -0.11), 0.045, "spike"), ("Chest", 0.75, (0.0, -0.11), 0.042, "spike"),
            ("LowerArm.L", 0.05, (0.0, -0.05), 0.028, "spike"), ("LowerArm.R", 0.05, (0.0, -0.05), 0.028, "spike"),
        ],
        "glow": (1.0, 0.25, 0.15), "lump": (0.3, 0.1, 0.08), "spike": (0.85, 0.8, 0.7),
    },
    # Shoulders and forearms like a bull's, a little head sunk on a thick neck, short heavy legs; lumps of muscle on
    # its back, bone through its shoulders and knuckles
    "brute": {
        "body": "zombie_farmer", "catalog": MEN,
        "bones": {
            "Abdomen": ((1.3, 1.0, 1.25), 6),
            "Torso": ((1.45, 1.0, 1.35), 10),
            "Chest": ((1.75, 1.15, 1.55), 12),
            "Neck": ((1.5, 0.5, 1.5), 16),
            "Head": ((0.72, 0.72, 0.72), -14),
            "Shoulder.L": ((1.6, 1.35, 1.6), 0), "Shoulder.R": ((1.6, 1.35, 1.6), 0),
            "UpperArm.L": ((2.0, 1.15, 2.0), 0), "UpperArm.R": ((2.0, 1.15, 2.0), 0),
            "LowerArm.L": ((2.3, 1.2, 2.3), 0), "LowerArm.R": ((2.3, 1.2, 2.3), 0),
            "Wrist.L*": ((2.0, 1.6, 2.0), 0), "Wrist.R*": ((2.0, 1.6, 2.0), 0),
            "UpperLeg.L": ((1.45, 0.9, 1.45), 0), "UpperLeg.R": ((1.45, 0.9, 1.45), 0),
            "LowerLeg.L": ((1.45, 0.9, 1.45), 0), "LowerLeg.R": ((1.45, 0.9, 1.45), 0),
            "Foot.L*": ((1.3, 1.15, 1.3), 0), "Foot.R*": ((1.3, 1.15, 1.3), 0),
        },
        "growths": [
            ("Chest", 0.7, (0.0, -0.22), 0.13, "lump"), ("Chest", 0.35, (0.15, -0.2), 0.1, "lump"),
            ("Chest", 0.35, (-0.15, -0.2), 0.1, "lump"),
            ("Shoulder.L", 0.6, (0.0, -0.1), 0.06, "spike"), ("Shoulder.R", 0.6, (0.0, -0.1), 0.06, "spike"),
            ("LowerArm.L", 0.85, (0.09, -0.04), 0.04, "spike"), ("LowerArm.R", 0.85, (-0.09, -0.04), 0.04, "spike"),
            ("LowerArm.L", 0.5, (0.0, -0.1), 0.045, "spike"), ("LowerArm.R", 0.5, (0.0, -0.1), 0.045, "spike"),
        ],
        "glow": (1.0, 0.5, 0.1), "lump": (0.36, 0.16, 0.1), "spike": (0.82, 0.77, 0.64),
    },
    # Stretched thin and tall, its back arched and its long neck thrown back, howling at the sky; a frill of spines
    # round its throat, and its throat and lungs glowing through its skin
    "howler": {
        "body": "zombie_peasant", "catalog": WOMEN,
        "bones": {
            "Abdomen": ((0.72, 1.18, 0.68), -4),
            "Torso": ((0.72, 1.15, 0.68), -6),
            "Chest": ((0.82, 1.15, 0.78), -10),
            "Neck": ((0.7, 2.6, 0.7), -14),
            "Head": ((1.0, 1.25, 1.2), -34),
            "UpperArm.L": ((0.6, 1.4, 0.6), 0), "UpperArm.R": ((0.6, 1.4, 0.6), 0),
            "LowerArm.L": ((0.55, 1.45, 0.55), 0), "LowerArm.R": ((0.55, 1.45, 0.55), 0),
            "Wrist.L*": ((1.1, 1.7, 1.1), 0), "Wrist.R*": ((1.1, 1.7, 1.1), 0),
            "UpperLeg.L": ((0.68, 1.22, 0.68), 0), "UpperLeg.R": ((0.68, 1.22, 0.68), 0),
            "LowerLeg.L": ((0.62, 1.28, 0.62), 0), "LowerLeg.R": ((0.62, 1.28, 0.62), 0),
        },
        "growths": [
            ("Neck", 0.85, (0.07, 0.0), 0.035, "spike"), ("Neck", 0.85, (-0.07, 0.0), 0.035, "spike"),
            ("Neck", 0.85, (0.05, -0.05), 0.035, "spike"), ("Neck", 0.85, (-0.05, -0.05), 0.035, "spike"),
            ("Neck", 0.85, (0.0, -0.07), 0.035, "spike"),
            ("Neck", 0.5, (0.0, 0.06), 0.05, "glow"), ("Chest", 0.6, (0.08, 0.13), 0.06, "glow"),
            ("Chest", 0.6, (-0.08, 0.13), 0.06, "glow"),
        ],
        "glow": (1.0, 0.3, 1.0), "lump": (0.3, 0.2, 0.3), "spike": (0.82, 0.78, 0.72),
    },
    # Hunched, its throat a great sac of bile glowing through its skin and its cheeks swollen with it
    "spitter": {
        "body": "zombie_maid", "catalog": WOMEN,
        "bones": {
            "Abdomen": ((0.75, 1.0, 0.7), 6),
            "Torso": ((0.8, 1.0, 0.75), 10),
            "Chest": ((0.85, 1.05, 0.8), 20),
            "Neck": ((1.15, 1.5, 1.2), 22),
            "Head": ((0.95, 0.95, 1.05), -22),
            "UpperArm.L": ((0.7, 1.1, 0.7), 0), "UpperArm.R": ((0.7, 1.1, 0.7), 0),
            "LowerArm.L": ((0.65, 1.1, 0.65), 0), "LowerArm.R": ((0.65, 1.1, 0.65), 0),
        },
        "growths": [
            ("Neck", 0.4, (0.0, 0.1), 0.14, "glow"), ("Neck", 0.75, (0.06, 0.08), 0.075, "glow"),
            ("Neck", 0.75, (-0.06, 0.08), 0.075, "glow"),
            ("Head", 0.25, (0.07, 0.06), 0.045, "lump"), ("Head", 0.25, (-0.07, 0.06), 0.045, "lump"),
        ],
        "glow": (1.0, 0.85, 0.15), "lump": (0.35, 0.32, 0.12), "spike": (0.75, 0.72, 0.6),
    },
    # Its belly vast and hanging (a great sac of flesh out front), pustules glowing on it and its back, its limbs fat
    "bloater": {
        "body": "zombie_king", "catalog": MEN,
        "bones": {
            "Abdomen": ((2.1, 1.25, 2.2), 0),
            "Torso": ((1.8, 1.05, 1.85), 4),
            "Chest": ((1.35, 1.0, 1.35), 8),
            "Neck": ((1.3, 0.6, 1.3), 6),
            "Head": ((0.85, 0.85, 0.85), 0),
            "UpperArm.L": ((1.25, 0.9, 1.25), 0), "UpperArm.R": ((1.25, 0.9, 1.25), 0),
            "LowerArm.L": ((1.2, 0.9, 1.2), 0), "LowerArm.R": ((1.2, 0.9, 1.2), 0),
            "UpperLeg.L": ((1.35, 0.85, 1.35), 0), "UpperLeg.R": ((1.35, 0.85, 1.35), 0),
            "LowerLeg.L": ((1.25, 0.9, 1.25), 0), "LowerLeg.R": ((1.25, 0.9, 1.25), 0),
        },
        "growths": [
            ("Abdomen", 0.35, (0.0, 0.2), 0.3, "lump"), ("Torso", 0.4, (0.0, 0.16), 0.24, "lump"),
            ("Abdomen", 0.4, (0.16, 0.44), 0.07, "glow"), ("Abdomen", 0.6, (-0.18, 0.42), 0.06, "glow"),
            ("Abdomen", 0.15, (-0.05, 0.48), 0.065, "glow"), ("Torso", 0.5, (0.14, 0.36), 0.055, "glow"),
            ("Torso", 0.4, (-0.22, 0.2), 0.05, "glow"), ("Chest", 0.4, (0.12, -0.2), 0.05, "glow"),
            ("Abdomen", 0.5, (0.0, -0.25), 0.12, "lump"), ("Abdomen", 0.8, (0.26, 0.05), 0.09, "lump"),
            ("Abdomen", 0.8, (-0.26, 0.05), 0.09, "lump"),
        ],
        "glow": (0.7, 1.0, 0.15), "lump": (0.28, 0.33, 0.12), "spike": (0.7, 0.68, 0.55),
    },
    # The town's watch: squared up in its mail, iron on its shoulders and a round shield on its back, its head bare (where
    # it's to be shot)
    "warden": {
        "body": "zombie_wanderer", "catalog": MEN,
        "bones": {
            "Chest": ((1.18, 1.05, 1.12), 2),
            "Torso": ((1.12, 1.0, 1.08), 0),
            "Neck": ((1.15, 0.85, 1.15), 0),
            "Shoulder.L": ((1.25, 1.1, 1.25), 0), "Shoulder.R": ((1.25, 1.1, 1.25), 0),
            "UpperArm.L": ((1.12, 1.0, 1.12), 0), "UpperArm.R": ((1.12, 1.0, 1.12), 0),
            "LowerArm.L": ((1.1, 1.0, 1.1), 0), "LowerArm.R": ((1.1, 1.0, 1.1), 0),
        },
        "growths": [
            ("Shoulder.L", 0.85, (0.0, 0.0), 0.11, "lump"), ("Shoulder.R", 0.85, (0.0, 0.0), 0.11, "lump"),
            ("Shoulder.L", 0.85, (0.09, -0.02), 0.03, "spike"), ("Shoulder.R", 0.85, (-0.09, -0.02), 0.03, "spike"),
            ("Chest", 0.55, (0.0, -0.2), 0.15, "lump"),
        ],
        "glow": (0.4, 0.6, 1.0), "lump": (0.12, 0.13, 0.16), "spike": (0.45, 0.47, 0.52),
    },
}

# The horde's clips (make_crowd.py's), so a kind plays what the rest of the dead do
CLIPS = [
    ("walk", "Z_Walk", None, None, 12, True),
    ("run", "Run", None, None, 10, True),
    ("sprint", "Sprint", None, None, 10, True),
    ("idle", "Z_Idle", None, None, 8, True),
    ("claw", "Z_Scratch", 0.0, None, 12, False),
    ("fall", "Z_Knockback", 0.0, 0.833, 8, False),
    ("getup", "Z_GetUp", 0.0, None, 12, False),
    ("death", "Death", 0.0, None, 10, False),
    ("deathB", "Death_B", 0.0, None, 12, False),
    ("rise", "Z_Climb", 0.15, 0.65, 10, False),
]

FPS = 24


def imported(path):
    before = set(bpy.data.objects)
    actions = set(bpy.data.actions)
    bpy.ops.import_scene.gltf(filepath=path)
    objs = [o for o in bpy.data.objects if o not in before]
    arm = next(o for o in objs if o.type == "ARMATURE")
    new = [a for a in bpy.data.actions if a not in actions]
    for a in new:
        a.use_fake_user = True
    if arm.animation_data:
        for t in list(arm.animation_data.nla_tracks):
            arm.animation_data.nla_tracks.remove(t)
        arm.animation_data.action = None
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    return arm, objs, {a.name.split(".")[0]: a for a in new}


def material_color(mat):
    if mat is None:
        return (0.5, 0.5, 0.5, 1.0)
    if mat.use_nodes:
        for node in mat.node_tree.nodes:
            if node.type == "BSDF_PRINCIPLED":
                c = node.inputs["Base Color"].default_value
                return (c[0], c[1], c[2], 1.0)
    c = mat.diffuse_color
    return (c[0], c[1], c[2], 1.0)


def game(v):
    """Blender's axes (z up) to the game's as Tide's importer leaves a glTF's: x mirrored, y up."""
    return np.array([-v[0], v[2], -v[1]])


def gltf_from_game(v):
    return [-float(v[0]), float(v[1]), float(v[2])]


def write_glb(path, positions, normals, colors, uv2, indices):
    n = len(positions)
    blobs = [np.asarray(positions, np.float32).tobytes(), np.asarray(normals, np.float32).tobytes(),
             np.zeros((n, 2), np.float32).tobytes(), np.asarray(uv2, np.float32).tobytes(),
             np.asarray(colors, np.float32).tobytes(), np.asarray(indices, np.uint32).tobytes()]
    views, offset, data = [], 0, b""
    for blob in blobs:
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(blob)})
        data += blob
        offset += len(blob)
        while offset % 4:
            data += b"\0"
            offset += 1
    pos = np.asarray(positions, np.float32)
    accessors = [
        {"bufferView": 0, "componentType": 5126, "count": n, "type": "VEC3", "min": pos.min(0).tolist(), "max": pos.max(0).tolist()},
        {"bufferView": 1, "componentType": 5126, "count": n, "type": "VEC3"},
        {"bufferView": 2, "componentType": 5126, "count": n, "type": "VEC2"},
        {"bufferView": 3, "componentType": 5126, "count": n, "type": "VEC2"},
        {"bufferView": 4, "componentType": 5126, "count": n, "type": "VEC4"},
        {"bufferView": 5, "componentType": 5125, "count": len(indices), "type": "SCALAR"},
    ]
    doc = {"asset": {"version": "2.0", "generator": "GRAVEWAKE make_kinds.py"}, "scene": 0, "scenes": [{"nodes": [0]}],
           "nodes": [{"mesh": 0, "name": "body"}],
           "meshes": [{"name": "body", "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "TEXCOORD_1": 3, "COLOR_0": 4},
                                                       "indices": 5, "mode": 4}]}],
           "accessors": accessors, "bufferViews": views, "buffers": [{"byteLength": len(data)}]}
    js = json.dumps(doc, separators=(",", ":")).encode()
    while len(js) % 4:
        js += b" "
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(data)))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(data), 0x004E4942) + data)


def flat_material(name, rgb):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs["Base Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
    return mat


def select_only(o):
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = o
    o.select_set(True)


def below(pb):
    yield pb
    for child in pb.children:
        yield from below(child)


def reshape(arm, meshes, bones):
    """The bones' new shape made their rest: the pose set, the meshes deformed by it, the pose applied as rest."""
    select_only(arm)
    bpy.ops.object.mode_set(mode="POSE")
    inherit = {b.name: b.inherit_scale for b in arm.data.bones}
    for b in arm.data.bones:
        b.inherit_scale = "NONE"
    for key, (scale, bend) in bones.items():
        name = key.rstrip("*")
        pb = arm.pose.bones.get(name)
        if pb is None:
            raise SystemExit(f"no bone {name}")
        for each in (below(pb) if key.endswith("*") else [pb]):
            each.scale = Vector(scale)
        pb.rotation_quaternion = Euler((math.radians(bend), 0, 0), "XYZ").to_quaternion()
    bpy.ops.object.mode_set(mode="OBJECT")
    bpy.context.view_layer.update()
    for o in meshes:
        mod = next((m for m in o.modifiers if m.type == "ARMATURE"), None)
        if mod is None:
            continue
        select_only(o)
        while o.modifiers[0] != mod:
            bpy.ops.object.modifier_move_up(modifier=mod.name)
        bpy.ops.object.modifier_apply(modifier=mod.name)
    select_only(arm)
    bpy.ops.object.mode_set(mode="POSE")
    bpy.ops.pose.select_all(action="SELECT")
    bpy.ops.pose.armature_apply(selected=False)
    bpy.ops.object.mode_set(mode="OBJECT")
    for o in meshes:
        mod = o.modifiers.new("Armature", "ARMATURE")
        mod.object = arm
    for b in arm.data.bones:
        b.inherit_scale = inherit[b.name]
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)


def growth(arm, bone_name, along, across, radius, kind, mats, index):
    """A sphere (or a cone of bone pointing out from it) on a bone, weighted wholly to it."""
    bone = arm.data.bones[bone_name]
    head = arm.matrix_world @ bone.head_local
    tail = arm.matrix_world @ bone.tail_local
    m = arm.matrix_world.to_3x3() @ bone.matrix_local.to_3x3()
    side, out = (m @ Vector((1, 0, 0))).normalized(), (m @ Vector((0, 0, 1))).normalized()
    at = head + (tail - head) * along + side * across[0] + out * across[1]
    bm = bmesh.new()
    if kind == "spike":
        bmesh.ops.create_cone(bm, cap_ends=True, segments=5, radius1=radius, radius2=0.0, depth=radius * 4.2)
        way = (side * across[0] + out * across[1])
        way = way.normalized() if way.length > 1e-5 else out
        rot = Vector((0, 0, 1)).rotation_difference(way).to_matrix().to_4x4()
        bmesh.ops.transform(bm, matrix=Matrix.Translation(at + way * radius * 1.4) @ rot, verts=bm.verts)
    else:
        bmesh.ops.create_icosphere(bm, subdivisions=1, radius=radius)
        bmesh.ops.transform(bm, matrix=Matrix.Translation(at), verts=bm.verts)
    me = bpy.data.meshes.new(f"growth{index}")
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(f"growth{index}", me)
    bpy.context.scene.collection.objects.link(o)
    me.materials.append(mats[kind])
    uv = me.uv_layers.new(name="UVMap")
    lit = me.uv_layers.new(name="UVMap.001")
    for loop in lit.data:
        loop.uv = (0.5 if kind == "glow" else 0.0, 0.0)
    del uv
    group = o.vertex_groups.new(name=bone_name)
    group.add([v.index for v in me.vertices], 1.0, "REPLACE")
    mod = o.modifiers.new("Armature", "ARMATURE")
    mod.object = arm
    return o


def bake(name, recipe):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    arm, objs, _ = imported(os.path.join(PEOPLE, recipe["body"] + ".glb"))
    cat, cobjs, clips = imported(os.path.join(PEOPLE, recipe["catalog"]))
    meshes = [o for o in objs if o.type == "MESH" and not o.name.startswith("Icosphere")]
    for o in cobjs:
        if o.type == "MESH":
            o.hide_set(True)
    total = sum(len(o.data.polygons) for o in meshes)
    ratio = min(1.0, TRIS / max(1, total))
    for o in meshes:
        bm = bmesh.new()
        bm.from_mesh(o.data)
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0005)
        bm.to_mesh(o.data)
        bm.free()
        select_only(o)
        dec = o.modifiers.new("fewer", "DECIMATE")
        dec.ratio = ratio
        dec.use_collapse_triangulate = True
        while o.modifiers[0] != dec:
            bpy.ops.object.modifier_move_up(modifier=dec.name)
        bpy.ops.object.modifier_apply(modifier=dec.name)
    reshape(arm, meshes, recipe["bones"])
    mats = {k: flat_material(f"{name}_{k}", recipe[k]) for k in ("glow", "lump", "spike")}
    for i, (bone, along, across, radius, kind) in enumerate(recipe["growths"]):
        meshes.append(growth(arm, bone, along, across, radius, kind, mats, i))
    # The corners: one for each vertex of each mesh and material it's used with; the eyes (the second uv map's x, as
    # the people's meshes mark them) 1, the growths that glow 0.5
    corners, key_of, tris = [], {}, []
    for o in meshes:
        me = o.data
        me.calc_loop_triangles()
        glow = me.uv_layers[1].data if len(me.uv_layers) > 1 else None
        for tri in me.loop_triangles:
            idx = []
            for li, vi in zip(tri.loops, tri.vertices):
                mi = tri.material_index
                lit = round(float(glow[li].uv[0]), 2) if glow is not None else 0.0
                lit = 1.0 if lit > 0.75 else 0.5 if lit > 0.3 else 0.0
                key = (o.name, vi, mi, lit)
                if key not in key_of:
                    key_of[key] = len(corners)
                    mat = o.material_slots[mi].material if mi < len(o.material_slots) else None
                    corners.append((o, vi, material_color(mat), lit))
                idx.append(key_of[key])
            tris.append(idx)
    count = len(corners)
    scene = bpy.context.scene
    scene.render.fps = FPS
    arm.animation_data_create()
    frames_pos, frames_nrm, info = [], [], []
    first = 0
    for clip_name, clip, t0, t1, frames, loops in CLIPS:
        act = clips.get(clip)
        if act is None:
            raise SystemExit(f"{name}: no clip {clip} in {recipe['catalog']}")
        arm.animation_data.action = act
        if hasattr(act, "slots") and len(act.slots) > 0:
            arm.animation_data.action_slot = act.slots[0]
        lo, hi = act.frame_range
        start = lo if t0 is None else t0 * FPS
        end = hi if t1 is None else t1 * FPS
        for k in range(frames):
            f = start + (end - start) * (k / frames if loops else k / max(1, frames - 1))
            scene.frame_set(int(f), subframe=f - int(f))
            depsgraph = bpy.context.evaluated_depsgraph_get()
            pos = np.zeros((count, 3))
            nrm = np.zeros((count, 3))
            cache = {}
            for o in meshes:
                ev = o.evaluated_get(depsgraph)
                m = ev.to_mesh()
                world = o.matrix_world
                rot = world.to_3x3()
                cache[o.name] = ([world @ v.co for v in m.vertices], [(rot @ v.normal).normalized() for v in m.vertices])
                ev.to_mesh_clear()
            for c, (o, vi, _, _) in enumerate(corners):
                ps, ns = cache[o.name]
                pos[c] = game(ps[vi])
                nrm[c] = game(ns[vi])
            frames_pos.append(pos)
            frames_nrm.append(nrm)
        info.append({"name": clip_name, "first": first, "frames": frames, "seconds": (end - start) / FPS, "loops": loops})
        first += frames
    # Feet on the ground: grown legs had stood it in it, short ones off it (the walk's lowest corner, at 0)
    walk = info[0]
    low = min(frames_pos[walk["first"] + k][:, 1].min() for k in range(walk["frames"]))
    for f in range(len(frames_pos)):
        frames_pos[f][:, 1] -= low
    # Where the head, chest and hips are (their bones' middles), at the walk's first frame (the walk playing: the last
    # clip baked would be, else)
    arm.animation_data.action = clips["Z_Walk"]
    if hasattr(clips["Z_Walk"], "slots") and len(clips["Z_Walk"].slots) > 0:
        arm.animation_data.action_slot = clips["Z_Walk"].slots[0]
    scene.frame_set(int(clips["Z_Walk"].frame_range[0]))
    marks = {}
    for mark, bone in (("head", "Head"), ("chest", "Chest"), ("hips", "Hips")):
        pb = arm.pose.bones[bone]
        middle = arm.matrix_world @ ((pb.head + pb.tail) * 0.5)
        g = game(middle)
        g[1] -= low
        marks[mark] = [round(float(x), 3) for x in g]
    print("KIND", name, count, "corners", len(tris), "triangles, lifted", round(float(-low), 3), "marks", marks)
    positions = [gltf_from_game(p) for p in frames_pos[0]]
    normals = [gltf_from_game(n) for n in frames_nrm[0]]
    return {"name": name, "count": count, "positions": positions, "normals": normals, "colors": [c[2] for c in corners],
            "uv2": [(c[3], 0.0) for c in corners], "indices": [i for t in tris for i in t], "frames_pos": frames_pos,
            "frames_nrm": frames_nrm, "clips": info, "marks": marks}


os.makedirs(OUT, exist_ok=True)
names = sorted(n for n in RECIPES if ONLY is None or n in ONLY)
baked = [bake(n, RECIPES[n]) for n in names]
# The box positions are packed in: everything any kind does in any frame, a little over
every = np.concatenate([p for b in baked for p in b["frames_pos"]])
BOX_LO = np.floor((every.min(0) - 0.05) * 10) / 10
BOX_HI = np.ceil((every.max(0) + 0.05) * 10) / 10
width = max(b["count"] for b in baked)
frames = len(baked[0]["frames_pos"])
rows_per_body = frames * 2
height = rows_per_body * len(baked)
pixels = np.zeros((height, width, 4), np.uint8)
pixels[..., 3] = 255
span = BOX_HI - BOX_LO
for bi, b in enumerate(baked):
    for f in range(frames):
        q = np.clip(np.round((b["frames_pos"][f] - BOX_LO) / span * 255), 0, 255).astype(np.uint8)
        n = np.clip(np.round((b["frames_nrm"][f] * 0.5 + 0.5) * 255), 0, 255).astype(np.uint8)
        row = bi * rows_per_body + f * 2
        pixels[row, :b["count"], :3] = q
        pixels[row + 1, :b["count"], :3] = n
    uv2 = [(g, (c + 0.5) / width) for c, (g, _) in enumerate(b["uv2"])]
    write_glb(os.path.join(OUT, b["name"] + ".glb"), b["positions"], b["normals"], b["colors"], uv2, b["indices"])
img = bpy.data.images.new("anim", width, height, alpha=True)
img.colorspace_settings.name = "Non-Color"
img.pixels.foreach_set((pixels[::-1].astype(np.float32) / 255.0).ravel())
img.filepath_raw = os.path.join(OUT, "anim.png")
img.file_format = "PNG"
img.save()
meta = {"width": width, "height": height, "rowsPerBody": rows_per_body, "frames": frames, "boxLo": BOX_LO.tolist(), "boxHi": BOX_HI.tolist(),
        "bodies": [b["name"] for b in baked], "marks": [b["marks"] for b in baked], "clips": baked[0]["clips"]}
with open(os.path.join(OUT, "kinds.json"), "w", newline="\n") as f:
    json.dump(meta, f, indent=1)
print("WROTE", width, "x", height, "box", BOX_LO.tolist(), BOX_HI.tolist())
