# GRAVEWAKE's horde: the six zombie bodies of art/people cut down to a low
# poly count (as a console of the nineties would have them) and their clips
# baked into one texture of numbers, so thousands of the dead draw in a few
# instanced draws with no skeleton: a vert reads where each corner is at a
# frame of a clip (crowd.tide).
#
#   blender -b --factory-startup --python make_crowd.py -- <art/people dir> <out dir> [--tris 900]
#
# Writes <out>/<body>.glb (a static mesh in the body's rest pose: positions,
# normals, colours from its materials, and TEXCOORD_1: x 1 on the eyes, y the
# corner's column in the texture), <out>/anim.png (the texture) and
# <out>/crowd.json (where each body's rows start, each clip's first frame,
# frames and seconds, and the box positions are packed in).
#
# The texture: a column per corner, two rows per frame, the bodies one under
# another. A frame's first row holds the corner's position in the game's axes
# (x, y, z), a byte each across the box (steps of a centimetre and a half: the
# wobble of the era), the second its normal (0 to 255 for -1 to 1). Alpha is
# 255 throughout.
import json
import os
import struct
import sys

import bpy
import bmesh
import numpy as np
from mathutils import Matrix

argv = sys.argv[sys.argv.index("--") + 1:]
PEOPLE, OUT = os.path.abspath(argv[0]), os.path.abspath(argv[1])
TRIS = int(argv[argv.index("--tris") + 1]) if "--tris" in argv else 900

# In the order a glob of the output folder lists their files, so a body's mesh and its rows in the texture share an index
BODIES = [("zombie_farmer", "moves.glb"), ("zombie_king", "moves.glb"), ("zombie_maid", "moves_women.glb"), ("zombie_peasant", "moves_women.glb"),
          ("zombie_wanderer", "moves.glb"), ("zombie_worker", "moves.glb")]

# name: (the catalog's clip, from seconds, to seconds, frames, loops)
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

BOX_LO = np.array([-1.6, -0.4, -2.2])
BOX_HI = np.array([1.6, 2.6, 1.6])
FPS = 24                                  # The catalogs' clips are keyed at 24 a second


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


def gltf(v):
    return np.array([v[0], v[2], -v[1]])


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
    doc = {"asset": {"version": "2.0", "generator": "GRAVEWAKE make_crowd.py"}, "scene": 0, "scenes": [{"nodes": [0]}],
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


def bake(body_name, catalog_name):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    arm, objs, _ = imported(os.path.join(PEOPLE, body_name + ".glb"))
    cat, cobjs, clips = imported(os.path.join(PEOPLE, catalog_name))
    meshes = [o for o in objs if o.type == "MESH" and not o.name.startswith("Icosphere")]
    for o in cobjs:
        if o.type == "MESH":
            o.hide_set(True)
    # Down to the poly count, the skin weights coming along
    total = sum(len(o.data.polygons) for o in meshes)
    ratio = min(1.0, TRIS / max(1, total))
    for o in meshes:
        # The packs' meshes are split at every seam of their uvs, which a decimation tears open: welded first (the
        # horde has no texture, and each material keeps its own corners below)
        bm = bmesh.new()
        bm.from_mesh(o.data)
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0005)
        bm.to_mesh(o.data)
        bm.free()
        bpy.context.view_layer.objects.active = o
        o.select_set(True)
        dec = o.modifiers.new("fewer", "DECIMATE")
        dec.ratio = ratio
        dec.use_collapse_triangulate = True
        while o.modifiers[0] != dec:
            bpy.ops.object.modifier_move_up(modifier=dec.name)
        bpy.ops.object.modifier_apply(modifier=dec.name)
        o.select_set(False)
    # The corners: one for each vertex of each mesh and material it's used with
    corners, key_of = [], {}
    tris = []
    for o in meshes:
        me = o.data
        me.calc_loop_triangles()
        # Eyes: the second uv map's x, as make_bodies.py marked them
        glow = None
        if len(me.uv_layers) > 1:
            glow = me.uv_layers[1].data
        for tri in me.loop_triangles:
            idx = []
            for li, vi in zip(tri.loops, tri.vertices):
                mi = tri.material_index
                lit = 1.0 if glow is not None and glow[li].uv[0] > 0.5 else 0.0
                key = (o.name, vi, mi, lit)
                if key not in key_of:
                    key_of[key] = len(corners)
                    mat = o.material_slots[mi].material if mi < len(o.material_slots) else None
                    corners.append((o, vi, material_color(mat), lit))
                idx.append(key_of[key])
            tris.append(idx)
    count = len(corners)
    # The frames: each clip on the catalog's armature, its pose copied to the body's (the skeletons match), the meshes
    # evaluated
    scene = bpy.context.scene
    scene.render.fps = FPS
    arm.animation_data_create()
    frames_pos, frames_nrm, info = [], [], []
    first = 0
    for name, clip, t0, t1, frames, loops in CLIPS:
        act = clips.get(clip)
        if act is None:
            raise SystemExit(f"{body_name}: no clip {clip} in {catalog_name}")
        arm.animation_data.action = act
        # Blender 4.4's actions have slots: the action plays nothing on the armature until one is chosen
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
        info.append({"name": name, "first": first, "frames": frames, "seconds": (end - start) / FPS, "loops": loops})
        first += frames
    # The rest mesh, as the first frame of the walk has it (the vert moves every corner anyway)
    rest = frames_pos[0]
    positions = [gltf_from_game(p) for p in rest]
    normals = [gltf_from_game(n) for n in frames_nrm[0]]
    colors = [c[2] for c in corners]
    uv2 = [(c[3], 0.0) for c in corners]
    indices = [i for t in tris for i in t]
    return {"name": body_name, "count": count, "positions": positions, "normals": normals, "colors": colors, "uv2": uv2,
            "indices": indices, "frames_pos": frames_pos, "frames_nrm": frames_nrm, "clips": info}


def gltf_from_game(v):
    return [-float(v[0]), float(v[1]), float(v[2])]


os.makedirs(OUT, exist_ok=True)
baked = [bake(b, c) for b, c in BODIES]
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
    # Each corner's column, at the middle of its texel
    uv2 = [(g, (c + 0.5) / width) for c, (g, _) in enumerate(b["uv2"])]
    write_glb(os.path.join(OUT, b["name"].replace("zombie_", "") + ".glb"), b["positions"], b["normals"], b["colors"], uv2, b["indices"])
    print("BODY", b["name"], b["count"], "corners", len(b["indices"]) // 3, "triangles")
# The texture, through Blender's image writer (rows bottom-up there)
img = bpy.data.images.new("anim", width, height, alpha=True)
img.colorspace_settings.name = "Non-Color"
img.pixels.foreach_set((pixels[::-1].astype(np.float32) / 255.0).ravel())
img.filepath_raw = os.path.join(OUT, "anim.png")
img.file_format = "PNG"
img.save()
meta = {"width": width, "height": height, "rowsPerBody": rows_per_body, "frames": frames,
        "boxLo": BOX_LO.tolist(), "boxHi": BOX_HI.tolist(),
        "bodies": [b["name"].replace("zombie_", "") for b in baked], "clips": baked[0]["clips"]}
with open(os.path.join(OUT, "crowd.json"), "w", newline="\n") as f:
    json.dump(meta, f, indent=1)
print("WROTE", width, "x", height)
