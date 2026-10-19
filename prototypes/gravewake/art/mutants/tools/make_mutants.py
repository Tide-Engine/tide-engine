# GRAVEWAKE's bosses: three mutants made from the dead of art/people, their
# bones grown and shrunk and bent into new shapes, growths and spikes on
# them, and the catalog's clips baked into a texture of numbers, as the
# horde's are (art/crowd/tools/make_crowd.py; crowd.tide draws both).
#
#   blender -b --factory-startup --python make_mutants.py -- <art/people dir> <out dir> [--only mauler] [--tris 1600]
#
# Writes <out>/<mutant>.glb (a static mesh in its rest pose: positions,
# normals, colours, and TEXCOORD_1: x 1 on the eyes, 0.5 on the growths that
# glow, the weak spots, 0 elsewhere; y the corner's column in the texture),
# <out>/anim.png and <out>/mutants.json, laid out as the horde's are; the
# bodies in their names' order, as a glob of the folder lists their files.
# Each body's entry in mutants.json also says where its head, chest and hips
# are and the middle of its glowing growths (the weak spot), at the walk's
# first frame in the game's axes, for boss.tide's hit spheres.
#
# A mutant: one of the dead's bodies, its pose set from RECIPES (each bone's
# own scale along and across it, not passed on to the bones below it; a name
# ending in * scales the bones below it alike, a hand with its fingers; and a
# bend about its own x), then the meshes deformed by that pose and the pose
# made the rest, so the clips (keyed on the bones' rotations from rest) play
# on the new shape; growths (spheres) and spikes (cones) put on bones and
# weighted to them.
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
TRIS = int(argv[argv.index("--tris") + 1]) if "--tris" in argv else 1600
ONLY = argv[argv.index("--only") + 1].split(",") if "--only" in argv else None

# name: (the body, its catalog, {bone: ((scale across, along, across), bend degrees)}, growths, colours)
#   a growth: (bone, where along it 0-1, offset across it (x, z) in metres of the grown bone, radius, kind)
#   kind: "glow" (a weak spot: lit, its own colour), "lump" (flesh), "spike" (a cone of bone pointing out)
RECIPES = {
    "mauler": {
        "body": "zombie_king", "catalog": "moves.glb",
        "bones": {
            "Abdomen": ((1.35, 1.0, 1.35), 10),
            "Torso": ((1.7, 1.05, 1.7), 16),
            "Chest": ((1.95, 1.2, 1.9), 12),
            "Neck": ((1.3, 0.45, 1.3), 18),
            "Head": ((0.5, 0.5, 0.5), -16),
            "Shoulder.L": ((1.8, 1.5, 1.8), 0), "Shoulder.R": ((1.8, 1.5, 1.8), 0),
            "UpperArm.L": ((2.4, 1.3, 2.4), 0), "UpperArm.R": ((2.4, 1.3, 2.4), 0),
            "LowerArm.L": ((2.6, 1.35, 2.6), 0), "LowerArm.R": ((2.6, 1.35, 2.6), 0),
            "Wrist.L*": ((2.2, 1.9, 2.2), 0), "Wrist.R*": ((2.2, 1.9, 2.2), 0),
            "UpperLeg.L": ((1.6, 0.8, 1.6), 0), "UpperLeg.R": ((1.6, 0.8, 1.6), 0),
            "LowerLeg.L": ((1.6, 0.85, 1.6), 0), "LowerLeg.R": ((1.6, 0.85, 1.6), 0),
            "Foot.L*": ((1.5, 1.3, 1.5), 0), "Foot.R*": ((1.5, 1.3, 1.5), 0),
        },
        "growths": [
            ("Chest", 0.3, (0.0, -0.27), 0.11, "glow"), ("Chest", 0.6, (0.15, -0.25), 0.095, "glow"),
            ("Chest", 0.55, (-0.16, -0.24), 0.1, "glow"), ("Chest", 0.85, (0.03, -0.24), 0.085, "glow"),
            ("Torso", 0.5, (0.11, -0.22), 0.085, "glow"), ("Torso", 0.6, (-0.12, -0.21), 0.08, "glow"),
            ("Chest", 0.95, (0.22, -0.12), 0.07, "spike"), ("Chest", 0.95, (-0.22, -0.12), 0.07, "spike"),
            ("Chest", 0.7, (0.0, -0.3), 0.06, "spike"),
            ("UpperArm.L", 0.3, (0.0, -0.12), 0.065, "spike"), ("UpperArm.R", 0.3, (0.0, -0.12), 0.065, "spike"),
            ("LowerArm.L", 0.5, (0.1, -0.08), 0.08, "lump"), ("LowerArm.R", 0.5, (-0.1, -0.08), 0.08, "lump"),
            ("Shoulder.L", 0.7, (0.0, -0.1), 0.09, "lump"), ("Shoulder.R", 0.7, (0.0, -0.1), 0.09, "lump"),
        ],
        "glow": (1.0, 0.55, 0.12), "lump": (0.32, 0.12, 0.09), "spike": (0.78, 0.74, 0.62),
    },
    "brood": {
        "body": "zombie_maid", "catalog": "moves_women.glb",
        "bones": {
            "Abdomen": ((2.5, 1.5, 2.6), 4),
            "Torso": ((1.7, 1.0, 1.8), 6),
            "Chest": ((1.0, 1.0, 1.0), 14),
            "Neck": ((0.9, 1.3, 0.9), 26),
            "Head": ((0.75, 0.75, 0.75), 0),
            "UpperArm.L": ((0.7, 1.45, 0.7), 0), "UpperArm.R": ((0.7, 1.45, 0.7), 0),
            "LowerArm.L": ((0.65, 1.4, 0.65), 0), "LowerArm.R": ((0.65, 1.4, 0.65), 0),
            "Wrist.L*": ((1.3, 1.4, 1.3), 0), "Wrist.R*": ((1.3, 1.4, 1.3), 0),
            "UpperLeg.L": ((0.85, 1.1, 0.85), 0), "UpperLeg.R": ((0.85, 1.1, 0.85), 0),
            "LowerLeg.L": ((0.75, 1.1, 0.75), 0), "LowerLeg.R": ((0.75, 1.1, 0.75), 0),
        },
        "growths": [
            # The sac: a great lump hanging out of the belly, the eggs glowing through its skin
            ("Abdomen", 0.3, (0.0, 0.2), 0.34, "lump"), ("Abdomen", 0.9, (0.0, 0.12), 0.24, "lump"),
            ("Abdomen", 0.35, (0.0, 0.52), 0.11, "glow"), ("Abdomen", 0.15, (0.2, 0.46), 0.09, "glow"),
            ("Abdomen", 0.55, (-0.19, 0.46), 0.095, "glow"), ("Abdomen", 0.0, (-0.08, 0.48), 0.08, "glow"),
            ("Abdomen", 0.6, (0.17, 0.44), 0.08, "glow"), ("Abdomen", 0.2, (0.29, 0.3), 0.07, "glow"),
            ("Abdomen", 0.3, (-0.3, 0.3), 0.07, "glow"),
            ("Abdomen", 0.5, (0.0, -0.27), 0.08, "lump"), ("Torso", 0.6, (0.0, -0.22), 0.05, "spike"),
            ("Chest", 0.6, (0.12, -0.15), 0.045, "spike"), ("Chest", 0.6, (-0.12, -0.15), 0.045, "spike"),
        ],
        "glow": (0.75, 1.0, 0.2), "lump": (0.3, 0.32, 0.12), "spike": (0.7, 0.68, 0.55),
    },
    "stalker": {
        "body": "zombie_wanderer", "catalog": "moves.glb",
        "bones": {
            "Abdomen": ((0.8, 1.25, 0.75), 8),
            "Torso": ((0.8, 1.2, 0.75), 14),
            "Chest": ((0.85, 1.1, 0.8), 18),
            "Neck": ((0.8, 2.2, 0.8), 22),
            "Head": ((0.85, 0.95, 1.05), -26),
            "UpperArm.L": ((0.75, 1.5, 0.75), 0), "UpperArm.R": ((0.75, 1.5, 0.75), 0),
            "LowerArm.L": ((0.7, 1.5, 0.7), 0), "LowerArm.R": ((0.7, 1.5, 0.7), 0),
            "Wrist.L*": ((1.3, 1.6, 1.3), 0), "Wrist.R*": ((1.3, 1.6, 1.3), 0),
            "UpperLeg.L": ((0.8, 1.5, 0.8), 0), "UpperLeg.R": ((0.8, 1.5, 0.8), 0),
            "LowerLeg.L": ((0.75, 1.45, 0.75), 0), "LowerLeg.R": ((0.75, 1.45, 0.75), 0),
            "Foot.L*": ((1.0, 1.3, 1.0), 0), "Foot.R*": ((1.0, 1.3, 1.0), 0),
        },
        "growths": [
            ("Neck", 0.4, (0.0, 0.075), 0.06, "glow"), ("Neck", 0.65, (0.0, 0.07), 0.05, "glow"),
            ("Abdomen", 0.6, (0.0, -0.11), 0.05, "spike"), ("Torso", 0.3, (0.0, -0.12), 0.055, "spike"),
            ("Torso", 0.8, (0.0, -0.12), 0.06, "spike"), ("Chest", 0.4, (0.0, -0.13), 0.065, "spike"),
            ("Chest", 0.85, (0.0, -0.12), 0.06, "spike"), ("Neck", 0.25, (0.0, -0.065), 0.05, "spike"),
            ("Head", 0.6, (0.0, -0.08), 0.045, "spike"),
        ],
        "glow": (1.0, 0.35, 0.3), "lump": (0.25, 0.25, 0.28), "spike": (0.82, 0.8, 0.72),
    },
}

# name: (the catalog's clip, from seconds, to seconds, frames, loops)
CLIPS = [
    ("walk", "Z_Walk", None, None, 12, True),
    ("run", "Sprint", None, None, 10, True),
    ("idle", "Z_Idle", None, None, 8, True),
    ("claw", "Z_Scratch", 0.0, None, 12, False),
    ("slam", "Sword_Slash", 0.0, None, 12, False),
    ("jump", "Jump_Loop", None, None, 8, True),
    ("land", "Jump_Land", 0.0, 0.6, 6, False),
    ("hit", "HitRecieve", 0.0, None, 8, False),
    ("crouch", "Crouch_Idle", None, None, 8, True),
    ("death", "Death", 0.0, None, 10, False),
    ("rise", "Z_Climb", 0.15, 0.65, 10, False),
]

BOX_LO = np.array([-3.0, -1.4, -2.8])
BOX_HI = np.array([3.0, 3.2, 2.4])
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
    doc = {"asset": {"version": "2.0", "generator": "GRAVEWAKE make_mutants.py"}, "scene": 0, "scenes": [{"nodes": [0]}],
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
    # Each bone's scale its own: what a bone's grown by doesn't pass to the ones below it
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
        # (as a quaternion: the clips key quaternions, which a bone set to Euler angles would ignore)
        pb.rotation_quaternion = Euler((math.radians(bend), 0, 0), "XYZ").to_quaternion()
    bpy.ops.object.mode_set(mode="OBJECT")
    bpy.context.view_layer.update()
    for o in meshes:
        mod = next((m for m in o.modifiers if m.type == "ARMATURE"), None)
        if mod is None:
            continue
        select_only(o)
        # (to the top of the stack, so applying it bakes the pose into the mesh)
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
    axis = (tail - head).normalized()
    m = arm.matrix_world.to_3x3() @ bone.matrix_local.to_3x3()
    side, out = (m @ Vector((1, 0, 0))).normalized(), (m @ Vector((0, 0, 1))).normalized()
    at = head + (tail - head) * along + side * across[0] + out * across[1]
    bm = bmesh.new()
    if kind == "spike":
        bmesh.ops.create_cone(bm, cap_ends=True, segments=5, radius1=radius, radius2=0.0, depth=radius * 4.2)
        # Pointing out from the bone, the way its offset goes
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
    # Down to the poly count (welded first, as make_crowd.py does: the packs' meshes are split at every seam)
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
    # The corners: one for each vertex of each mesh and material it's used with
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
    # Where the head, chest and hips are (their bones' middles) and the glowing growths' middle, at the walk's first frame
    walk_frame = frames_pos[walk["first"]]
    scene.frame_set(int(clips["Z_Walk"].frame_range[0]))
    marks = {}
    for mark, bone in (("head", "Head"), ("chest", "Chest"), ("hips", "Hips")):
        pb = arm.pose.bones[bone]
        middle = arm.matrix_world @ ((pb.head + pb.tail) * 0.5)
        g = game(middle)
        g[1] -= low
        marks[mark] = [round(float(x), 3) for x in g]
    glows = [c for c, (_, _, _, lit) in enumerate(corners) if 0.3 < lit < 0.8]
    marks["weak"] = [round(float(x), 3) for x in walk_frame[glows].mean(0)] if glows else [0, 0, 0]
    marks["weakSpread"] = round(float(np.linalg.norm(walk_frame[glows] - walk_frame[glows].mean(0), axis=1).max()), 3) if glows else 0
    spread_lo = np.min([p.min(0) for p in frames_pos], axis=0)
    spread_hi = np.max([p.max(0) for p in frames_pos], axis=0)
    print("MUTANT", name, count, "corners", len(tris), "triangles, lifted", round(float(-low), 3), "spread", spread_lo.round(2).tolist(), spread_hi.round(2).tolist(), "marks", marks)
    positions = [gltf_from_game(p) for p in frames_pos[0]]
    normals = [gltf_from_game(n) for n in frames_nrm[0]]
    return {"name": name, "count": count, "positions": positions, "normals": normals, "colors": [c[2] for c in corners],
            "uv2": [(c[3], 0.0) for c in corners], "indices": [i for t in tris for i in t], "frames_pos": frames_pos,
            "frames_nrm": frames_nrm, "clips": info, "marks": marks}


os.makedirs(OUT, exist_ok=True)
names = sorted(n for n in RECIPES if ONLY is None or n in ONLY)
baked = [bake(n, RECIPES[n]) for n in names]
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
with open(os.path.join(OUT, "mutants.json"), "w", newline="\n") as f:
    json.dump(meta, f, indent=1)
print("WROTE", width, "x", height)
