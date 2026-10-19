"""Build FOLDLINE's drowned zombie from Quaternius' "Animated Zombie" (CC0).

Run headless only:
  blender -b --factory-startup --python make_zombie.py -- [--src SOURCE.glb] [--out DIR]

Writes DIR/zombie.glb (mesh, skin and the clips Idle, Walk, Run, Crawl, Bite; no
materials or images) and DIR/zombie.png (64x64 skin for the glb's one uv set).

What it does to the source:
- Drops the importer's helper objects (RootNode, *_end leaf empties, bone-shape sphere).
- Scales the rest (T-pose) height to 1.75 m, feet on z = 0 (glTF y = 0).
- The source's rest pose faces +X (Blender) while every clip faces -Y; the rest is turned
  -90 degrees about the vertical so both face Blender -Y, which is glTF +Z, the way
  Kenney's character.fbx faces after import. Every clip is re-solved frame by frame
  against the new rest so the animated result is the source's, unchanged. Bones a clip
  doesn't key (Idle keys no finger) hold the source's glTF node rest, as glTF says.
- Clips get clean names, keep 24 fps keys, and get a closing frame equal to their
  first one where the source left it out, so they loop without a pop. The exporter
  writes them in name order: Bite, Crawl, Idle, Run, Walk.
- The body is swollen (per-bone push along the smoothed normal, more on the belly);
  the crawl, whose belly then sank into the floor, is lifted to lie on it.
- The model is unwrapped (smart project, head islands given more texels, the tiny
  teeth and gums collapsed onto two solid swatches) and packed onto the 64x64 texel
  grid so no texel is shared by two islands. Rest-pose position, normal, material
  and ambient occlusion are baked at 512 px with Cycles; the colours are painted
  from those with 3D noise (grey-blue marbled skin with livor, dark sockets with
  clouded eyes, bruised mouth, soaked grey-brown shirt, silted dark trousers with
  no blue, grimy bone, washed-out wounds), averaged down to 64 px, given a little
  more contrast and quantized to 32 levels a channel.
"""
import json
import math
import os
import shutil
import struct
import sys
import tempfile

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Quaternion, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_SRC = "C:/Users/blenm/AppData/Local/Temp/foldline-monsters/pp-animated-zombie/pp-animated-zombie.glb"
DEFAULT_OUT = os.path.normpath(os.path.join(HERE, "..", "zombie"))

HEIGHT = 1.75          # rest (T-pose) height in metres
TEX = 64               # final texture size
BAKE = 512             # bake size before downscaling (multiple of TEX)
FPS = 24

CLIPS = {"ZombieIdle": "Idle", "ZombieWalk": "Walk", "ZombieRun": "Run",
         "ZombieCrawl": "Crawl", "ZombieBite": "Bite"}
CLIP_ORDER = ["Idle", "Walk", "Run", "Crawl", "Bite"]

# Source palette (32x32 png in the glb): sRGB colour -> class
PALETTE = {
    (228, 238, 176): 0,  # skin
    (236, 239, 203): 1,  # teeth
    (209, 27, 54): 2,    # gums
    (41, 41, 41): 3,     # dark (unused by any face)
    (220, 189, 116): 4,  # exposed bone
    (203, 189, 171): 5,  # shirt
    (93, 112, 173): 6,   # jeans
    (174, 0, 13): 7,     # wound
}
SKIN, TEETH, GUMS, DARK, BONE, SHIRT, PANTS, WOUND = range(8)
PART_BODY, PART_HEAD, PART_HAND, PART_FOOT = range(4)

# Swelling along the smoothed normal, metres, by the bone a vertex follows
SWELL = {
    "Hips": 0.024, "Spine": 0.034, "Spine1": 0.038, "Spine2": 0.024, "Neck": 0.022,
    "Head": 0.008, "LeftShoulder": 0.012, "RightShoulder": 0.012,
    "LeftArm": 0.012, "RightArm": 0.012, "LeftForeArm": 0.008, "RightForeArm": 0.008,
    "LeftHand": 0.004, "RightHand": 0.004, "LeftUpLeg": 0.016, "RightUpLeg": 0.016,
    "LeftLeg": 0.008, "RightLeg": 0.008,
}
BELLY = 0.050          # extra push forward on the front of the belly

ANGLE_LIMIT = 80       # smart uv project angle
HEAD_TEXELS = 1.9      # head islands get this much more texel density
HAND_TEXELS = 1.2

# Face features in the source's rest coordinates (Blender, units, facing +X)
SRC_EYES = [Vector((0.30, 0.14, 7.72)), Vector((0.30, -0.14, 7.72))]
SRC_MOUTH = Vector((0.33, 0.0, 7.32))


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    src, out = DEFAULT_SRC, DEFAULT_OUT
    i = 0
    while i < len(argv):
        if argv[i] == "--src":
            src = argv[i + 1]; i += 2
        elif argv[i] == "--out":
            out = argv[i + 1]; i += 2
        else:
            raise SystemExit("unknown argument " + argv[i])
    return src, out


def log(*a):
    print("[zombie]", *a, flush=True)


def set_active(ob):
    bpy.context.view_layer.update()
    for o in bpy.context.scene.objects:
        if o is not None:
            o.select_set(False)
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)


# ---------------------------------------------------------------- source

def node_rest_bases(src):
    """Every bone's basis in the source's glTF node rest (the nodes' own TRS), which is what
    a bone a clip doesn't key holds. Blender's importer leaves its first action (Bite) at
    frame 1 in every pose bone instead, so Idle, which keys no finger and not RightToeBase,
    would keep Bite's fingers. An import of the same file without its animations leaves
    the node TRS in the pose bones: read them from that."""
    data = open(src, "rb").read()
    magic, _version, _length = struct.unpack_from("<4sII", data, 0)
    jlen, jtype = struct.unpack_from("<II", data, 12)
    assert magic == b"glTF" and jtype == 0x4E4F534A, "not a glb with a JSON chunk first"
    g = json.loads(data[20:20 + jlen].decode("utf-8"))
    g.pop("animations", None)
    js = json.dumps(g, separators=(",", ":")).encode("utf-8")
    js += b" " * (-len(js) % 4)
    body = struct.pack("<II", len(js), 0x4E4F534A) + js + data[20 + jlen:]
    tmp = tempfile.mkdtemp()
    try:
        path = os.path.join(tmp, "rest.glb")
        with open(path, "wb") as fh:
            fh.write(struct.pack("<4sII", b"glTF", 2, 12 + len(body)) + body)
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.gltf(filepath=path)
        arms = [o for o in bpy.data.objects if o.type == 'ARMATURE']
        assert len(arms) == 1, arms
        bases = {pb.name: (pb.bone.matrix_local.copy(), pb.matrix_basis.copy()) for pb in arms[0].pose.bones}
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return bases


def load_source(src):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = FPS
    bpy.ops.import_scene.gltf(filepath=src)
    arms = [o for o in bpy.data.objects if o.type == 'ARMATURE']
    meshes = [o for o in bpy.data.objects if o.type == 'MESH' and any(m.type == 'ARMATURE' for m in o.modifiers)]
    assert len(arms) == 1 and len(meshes) == 1, (arms, meshes)
    images = [im for im in bpy.data.images if im.size[0] > 0]
    assert len(images) == 1, images
    return arms[0], meshes[0], images[0]


def classify_faces(mesh_ob, image):
    """Class of each face from the source palette texel under its uv centroid."""
    w, h = image.size
    px = np.array(image.pixels[:], dtype=np.float32).reshape(h, w, 4)[:, :, :3]
    keys = np.array(list(PALETTE.keys()), dtype=np.float32) / 255.0
    vals = list(PALETTE.values())
    me = mesh_ob.data
    uv = me.uv_layers.active.data
    out = []
    for p in me.polygons:
        u = sum((uv[i].uv[0] for i in p.loop_indices)) / p.loop_total
        v = sum((uv[i].uv[1] for i in p.loop_indices)) / p.loop_total
        x = min(max(int(u * w), 0), w - 1)
        y = min(max(int(v * h), 0), h - 1)
        c = px[y, x]
        out.append(vals[int(np.argmin(((keys - c) ** 2).sum(1)))])
    return out


def face_parts(mesh_ob):
    """Head, hand, foot or body for each face, from the bone its vertices follow most."""
    me = mesh_ob.data
    names = [g.name for g in mesh_ob.vertex_groups]
    dom = []
    for v in me.vertices:
        best = max(v.groups, key=lambda g: g.weight, default=None)
        dom.append(names[best.group] if best else "")
    parts = []
    for p in me.polygons:
        votes = {}
        for vi in p.vertices:
            n = dom[vi]
            k = (PART_HEAD if n in ("Head", "HeadTop_End") else
                 PART_HAND if "Hand" in n else
                 PART_FOOT if ("Foot" in n or "Toe" in n) else PART_BODY)
            votes[k] = votes.get(k, 0) + 1
        parts.append(max(votes, key=votes.get))
    return parts


# ---------------------------------------------------------------- rig

def rest_world_vertices(arm, mesh_ob):
    arm.data.pose_position = 'REST'
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    ev = mesh_ob.evaluated_get(dg)
    m = ev.to_mesh()
    vs = np.array([tuple(mesh_ob.matrix_world @ v.co) for v in m.vertices])
    ev.to_mesh_clear()
    arm.data.pose_position = 'POSE'
    return vs


def build_rig(old_arm, old_mesh, face_cls, face_part):
    vs = rest_world_vertices(old_arm, old_mesh)
    h = vs[:, 2].max() - vs[:, 2].min()
    s = HEIGHT / h
    dz = -vs[:, 2].min() * s
    log("source rest height %.3f units, scale %.5f" % (h, s))
    lift = Matrix.Translation((0, 0, dz))
    turn = Matrix.Rotation(-math.pi / 2, 4, 'Z')
    scale = Matrix.Diagonal((s, s, s, 1))
    f_rest = lift @ turn @ scale        # source world -> new rest (turned)
    g_clip = lift @ scale               # source world -> new world for clips (already facing -Y)

    w_arm = old_arm.matrix_world.copy()
    w_mesh = old_mesh.matrix_world.copy()

    data = old_arm.data.copy()
    data.name = "ZombieRig"
    arm = bpy.data.objects.new("ZombieRig", data)
    bpy.context.scene.collection.objects.link(arm)
    set_active(arm)
    bpy.ops.object.mode_set(mode='EDIT')
    for eb in data.edit_bones:
        eb.transform(f_rest @ w_arm, scale=True, roll=True)
    bpy.ops.object.mode_set(mode='OBJECT')
    data.pose_position = 'POSE'

    me = old_mesh.data.copy()
    me.name = "ZombieMesh"
    me.transform(f_rest @ w_mesh)
    ob = old_mesh.copy()
    ob.data = me
    ob.name = "ZombieMesh"
    ob.animation_data_clear()
    bpy.context.scene.collection.objects.link(ob)
    ob.parent = arm
    ob.matrix_parent_inverse = Matrix()
    ob.matrix_basis = Matrix()
    for m in ob.modifiers:
        if m.type == 'ARMATURE':
            m.object = arm
    a = me.attributes.new("zcls", 'INT', 'FACE')
    a.data.foreach_set("value", face_cls)
    a = me.attributes.new("zpart", 'INT', 'FACE')
    a.data.foreach_set("value", face_part)
    return arm, ob, g_clip @ w_arm, f_rest, s


def sample_targets(old_arm, action, to_new, rest_bases):
    """Armature-space (new) target matrices of every bone, each frame of the action.
    Bones the action doesn't key hold the source's node rest, as glTF has them."""
    sc = bpy.context.scene
    old_arm.animation_data.action = None
    for pb in old_arm.pose.bones:
        pb.matrix_basis = rest_bases[pb.name][1]
    old_arm.animation_data.action = action
    f0, f1 = (int(round(x)) for x in action.frame_range)
    frames = []
    for f in range(f0, f1 + 1):
        sc.frame_set(f)
        pose = {}
        for pb in old_arm.pose.bones:
            m = to_new @ pb.matrix
            loc, rot, _scale = m.decompose()
            pose[pb.name] = Matrix.Translation(loc) @ rot.to_matrix().to_4x4()
        frames.append(pose)
    return frames


def pose_gap_degrees(a, b):
    worst = 0.0
    for n in a:
        qa = a[n].to_quaternion()
        qb = b[n].to_quaternion()
        d = qa.rotation_difference(qb).angle
        worst = max(worst, math.degrees(min(d, 2 * math.pi - d)))
    return worst


def solve_bases(arm, frames):
    """Local (basis) loc/quaternion per bone per frame so the new rig reproduces the targets."""
    bones = arm.data.bones
    rel = {}
    for b in bones:
        rel[b.name] = (b.parent.matrix_local.inverted() @ b.matrix_local) if b.parent else b.matrix_local.copy()
    out = {b.name: ([], []) for b in bones}
    for pose in frames:
        for b in bones:
            m = pose[b.name]
            basis = rel[b.name].inverted() @ (pose[b.parent.name].inverted() @ m if b.parent else m)
            loc, rot, _ = basis.decompose()
            locs, rots = out[b.name]
            if rots and rots[-1].dot(rot) < 0:
                rot = -rot
            locs.append(loc)
            rots.append(rot)
    return out


def clip_floor(arm, ob, act):
    """Lowest point of the skinned mesh on each frame of a clip."""
    sc = bpy.context.scene
    arm.animation_data.action = act
    f0, f1 = (int(round(x)) for x in act.frame_range)
    lows = []
    for f in range(f0, f1 + 1):
        sc.frame_set(f)
        dg = bpy.context.evaluated_depsgraph_get()
        ev = ob.evaluated_get(dg)
        m = ev.to_mesh()
        v = np.empty(len(m.vertices) * 3)
        m.vertices.foreach_get("co", v)
        ev.to_mesh_clear()
        lows.append(v.reshape(-1, 3)[:, 2].min())
    arm.animation_data.action = None
    return np.array(lows)


def ground_clips(arm, ob):
    """The swelling pushes the belly through the floor in the crawl: lift a clip whose
    body sinks below the floor so it lies on it (by its usual lowest point, not its worst)."""
    hips = arm.data.bones["Hips"]
    to_local = hips.matrix_local.to_3x3().inverted()
    for name in CLIP_ORDER:
        act = bpy.data.actions[name]
        lows = clip_floor(arm, ob, act)
        lift = -float(np.percentile(lows, 25))
        log("clip %-5s lowest point: min %.3f, 25%% %.3f, median %.3f, max %.3f m" % (name, lows.min(), -lift, np.median(lows), lows.max()))
        if lift < 0.02:
            continue
        d = to_local @ Vector((0, 0, lift))
        for fc in act.fcurves:
            if fc.data_path == 'pose.bones["Hips"].location':
                for k in fc.keyframe_points:
                    k.co.y += d[fc.array_index]
                    k.handle_left.y += d[fc.array_index]
                    k.handle_right.y += d[fc.array_index]
                fc.update()
        lows = clip_floor(arm, ob, act)
        log("clip %-5s lifted %.3f m: lowest point now min %.3f, median %.3f m" % (name, lift, lows.min(), np.median(lows)))


def write_action(arm, name, bases, nframes):
    act = bpy.data.actions.new(name)
    act.use_fake_user = True
    if arm.animation_data is None:
        arm.animation_data_create()
    arm.animation_data.action = act
    for pb in arm.pose.bones:
        pb.rotation_mode = 'QUATERNION'
    # create the channels through keyframe_insert so the action gets a slot for the rig
    for pb in arm.pose.bones:
        pb.keyframe_insert("location", frame=0)
        pb.keyframe_insert("rotation_quaternion", frame=0)
    frames = np.arange(nframes, dtype=np.float32)
    for fc in act.fcurves:
        bone = fc.data_path.split('"')[1]
        locs, rots = bases[bone]
        if fc.data_path.endswith("location"):
            vals = np.array([l[fc.array_index] for l in locs], dtype=np.float32)
        else:
            vals = np.array([q[fc.array_index] for q in rots], dtype=np.float32)
        fc.keyframe_points.clear()
        fc.keyframe_points.add(nframes)
        co = np.empty(nframes * 2, dtype=np.float32)
        co[0::2] = frames
        co[1::2] = vals
        fc.keyframe_points.foreach_set("co", co)
        fc.keyframe_points.foreach_set("interpolation", [bpy.types.Keyframe.bl_rna.properties["interpolation"].enum_items["LINEAR"].value] * nframes)
        fc.update()
    arm.animation_data.action = None
    return act


def transfer_clips(old_arm, arm, to_new, rest_bases):
    for pb in old_arm.pose.bones:
        d = pb.bone.matrix_local - rest_bases[pb.name][0]
        assert max(abs(x) for row in d for x in row) < 1e-5, "the two imports' rests differ at " + pb.name
    sources = {}
    for act in bpy.data.actions:
        short = act.name.split("|")[-1]
        if short in CLIPS:
            sources[CLIPS[short]] = act
    assert sorted(sources) == sorted(CLIP_ORDER), sorted(sources)
    made = {}
    for name in CLIP_ORDER:
        src = sources[name]
        frames = sample_targets(old_arm, src, to_new, rest_bases)
        gap = pose_gap_degrees(frames[0], frames[-1])
        closed = gap > 0.5
        if closed:
            frames.append(frames[0])
        bases = solve_bases(arm, frames)
        act = write_action(arm, name, bases, len(frames))
        made[name] = (act, frames)
        log("clip %-5s from %-24s %3d frames, %.3f s, first/last gap %.1f deg%s"
            % (name, src.name, len(frames), (len(frames) - 1) / FPS, gap, ", closing frame added" if closed else ""))
    old_arm.animation_data.action = None
    return made


def verify_clips(arm, made):
    sc = bpy.context.scene
    worst = 0.0
    for name, (act, frames) in made.items():
        arm.animation_data.action = act
        for f in range(0, len(frames), 7):
            sc.frame_set(f)
            for pb in arm.pose.bones:
                d = (pb.matrix.translation - frames[f][pb.name].translation).length
                worst = max(worst, d)
    arm.animation_data.action = None
    sc.frame_set(0)
    log("clip transfer: worst bone position error %.6f m" % worst)
    assert worst < 1e-3


# ---------------------------------------------------------------- mesh

def merge_and_swell(ob, arm):
    me = ob.data
    bm = bmesh.new()
    bm.from_mesh(me)
    nf = len(bm.faces)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    assert len(bm.faces) == nf
    bm.to_mesh(me)
    bm.free()
    me.update()
    log("merged to %d vertices, %d faces" % (len(me.vertices), len(me.polygons)))

    # smoothed normals of the merged surface
    n = len(me.vertices)
    co = np.empty(n * 3, dtype=np.float64)
    me.vertices.foreach_get("co", co)
    co = co.reshape(n, 3)
    acc = np.zeros((n, 3))
    for p in me.polygons:
        vs = list(p.vertices)
        a = co[vs[1]] - co[vs[0]]
        b = co[vs[2]] - co[vs[0]]
        fn = np.cross(a, b)  # area weighted
        for vi in vs:
            acc[vi] += fn
    nrm = acc / np.maximum(np.linalg.norm(acc, axis=1, keepdims=True), 1e-12)

    cls = np.empty(len(me.polygons), dtype=np.int32)
    me.attributes["zcls"].data.foreach_get("value", cls)
    bone_vert = np.zeros(n, dtype=bool)
    mouth_vert = np.zeros(n, dtype=bool)
    for p in me.polygons:
        if cls[p.index] == BONE:
            bone_vert[list(p.vertices)] = True
        if cls[p.index] in (TEETH, GUMS):
            mouth_vert[list(p.vertices)] = True

    names = [g.name for g in ob.vertex_groups]
    swell = np.zeros(n)
    belly_w = np.zeros(n)
    for v in me.vertices:
        tot = sum(g.weight for g in v.groups) or 1.0
        for g in v.groups:
            nm = names[g.group]
            swell[v.index] += SWELL.get(nm, 0.0) * g.weight / tot
            if nm in ("Spine", "Spine1", "Hips"):
                belly_w[v.index] += g.weight / tot
    hips = arm.data.bones["Hips"].head_local
    spine1 = arm.data.bones["Spine1"].head_local
    belly_c = np.array(((hips.x + spine1.x) / 2, min(hips.y, spine1.y), (hips.z + spine1.z) / 2))
    fwd = np.array((0.0, -1.0, 0.0))
    front = np.clip(nrm @ fwd, 0, 1)
    d = np.linalg.norm((co - belly_c) * np.array((1.0, 1.0, 0.8)), axis=1)
    bulge = BELLY * belly_w * front * np.exp(-(d / 0.16) ** 2)
    swell = swell * np.where(bone_vert, 0.15, 1.0) * np.where(mouth_vert, 0.4, 1.0)
    co2 = co + nrm * swell[:, None] + fwd[None, :] * bulge[:, None]
    me.vertices.foreach_set("co", co2.reshape(-1))
    me.update()
    log("swelling: max %.3f m, belly bulge max %.3f m" % ((swell).max(), bulge.max()))


def uv_islands(bm, uvl, faces):
    """Faces grouped into uv islands (connected through shared vertices with equal uvs)."""
    parent = {i: i for i in faces}

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    seen = {}
    for fi in faces:
        for l in bm.faces[fi].loops:
            key = (l.vert.index, round(l[uvl].uv.x, 6), round(l[uvl].uv.y, 6))
            if key in seen:
                a, b = find(seen[key]), find(fi)
                if a != b:
                    parent[a] = b
            else:
                seen[key] = fi
    islands = {}
    for fi in faces:
        islands.setdefault(find(fi), []).append(fi)
    return list(islands.values())


# Solid swatches for the tiny mouth parts, outside the packed area: texel blocks (x0, y0)
# in Blender's image orientation (y up), 4x4 texels each
SWATCHES = {TEETH: (TEX - 4, TEX - 4), GUMS: (TEX - 8, TEX - 4)}
SWATCH_COLOURS = {TEETH: np.array((104, 96, 76)) / 255.0, GUMS: np.array((48, 16, 24)) / 255.0}


def select_faces(ob, mask):
    """In edit mode: select exactly the faces in mask (and their uvs)."""
    bm = bmesh.from_edit_mesh(ob.data)
    bm.faces.ensure_lookup_table()
    for v in bm.verts:
        v.select_set(False)
    for e in bm.edges:
        e.select_set(False)
    for f in bm.faces:
        f.select_set(False)
    for f in bm.faces:
        if mask[f.index]:
            f.select_set(True)
    bm.select_mode = {'FACE'}
    bm.select_flush_mode()
    uvl = bm.loops.layers.uv.active
    for f in bm.faces:
        for l in f.loops:
            l[uvl].select = bool(mask[f.index])
            l[uvl].select_edge = bool(mask[f.index])
    bmesh.update_edit_mesh(ob.data)
    log("selected faces: %d" % sum(1 for f in bm.faces if f.select))


def min_area_rotation(pts):
    """Angle that turns the points to their smallest axis-aligned box (tried at hull edges)."""
    pts = np.asarray(pts)
    if len(pts) < 3:
        return 0.0
    # convex hull (monotone chain)
    P = sorted(set(map(tuple, pts.round(9))))
    if len(P) < 3:
        return 0.0

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for q in P:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], q) <= 0:
            lower.pop()
        lower.append(q)
    for q in reversed(P):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], q) <= 0:
            upper.pop()
        upper.append(q)
    hull = np.array(lower[:-1] + upper[:-1])
    best, best_a = None, 0.0
    for i in range(len(hull)):
        d = hull[(i + 1) % len(hull)] - hull[i]
        ang = -math.atan2(d[1], d[0])
        c, s_ = math.cos(ang), math.sin(ang)
        r = hull @ np.array(((c, s_), (-s_, c)))
        ext = r.max(0) - r.min(0)
        area = ext[0] * ext[1]
        if best is None or area < best - 1e-12:
            best, best_a = area, ang
    return best_a


def island_mask(pts, tris):
    """Texels (cells of 1) an island's triangles touch; pts in texel units with min >= 0."""
    w = int(math.floor(pts[:, 0].max())) + 1
    h = int(math.floor(pts[:, 1].max())) + 1
    mask = np.zeros((h, w), dtype=bool)
    for t in tris:
        a, b, c = pts[t[0]], pts[t[1]], pts[t[2]]
        n = max(2, int(math.ceil(max(np.linalg.norm(b - a), np.linalg.norm(c - a), np.linalg.norm(c - b)) * 6)))
        i, j = np.meshgrid(np.arange(n + 1), np.arange(n + 1))
        keep = (i + j) <= n
        u = (i[keep] / n)[:, None]
        v = (j[keep] / n)[:, None]
        q = a + (b - a) * u + (c - a) * v
        x = np.clip(np.floor(q[:, 0]).astype(int), 0, w - 1)
        y = np.clip(np.floor(q[:, 1]).astype(int), 0, h - 1)
        mask[y, x] = True
    return mask


def turned(pts, quarter):
    for _ in range(quarter):
        pts = np.stack((-pts[:, 1], pts[:, 0]), axis=1)
    return pts - pts.min(0)


def try_pack(shapes, k, reserved):
    """Place every island (texel grid, no two islands sharing a texel) at scale k, or None."""
    occ = reserved.copy()
    H, W = occ.shape
    placed = []
    order = sorted(range(len(shapes)), key=lambda i: -shapes[i][1].max(0).prod())
    for idx in order:
        _loops, pts, tris = shapes[idx]
        best = None
        for quarter in range(4):
            base = turned(pts * k, quarter)
            # the sub-texel offset that touches the fewest texels
            cand = None
            for off in ((0.02, 0.02), (0.5, 0.02), (0.02, 0.5), (0.5, 0.5)):
                q = base + np.array(off)
                m = island_mask(q, tris)
                if cand is None or m.sum() < cand[1].sum():
                    cand = (q, m)
            q, m = cand
            h, w = m.shape
            if h > H or w > W:
                continue
            win = np.lib.stride_tricks.sliding_window_view(occ, (h, w))
            hit = np.einsum("yxhw,hw->yx", win.astype(np.uint8), m.astype(np.uint8))
            free = np.argwhere(hit == 0)
            if len(free) == 0:
                continue
            tops = free[:, 0] + h
            j = np.lexsort((free[:, 1], tops))[0]
            y0, x0 = free[j]
            key = (tops[j], x0, m.sum())
            if best is None or key < best[0]:
                best = (key, q, m, y0, x0)
        if best is None:
            return None
        _key, q, m, y0, x0 = best
        occ[y0:y0 + m.shape[0], x0:x0 + m.shape[1]] |= m
        placed.append((idx, q + np.array((x0, y0))))
    return placed, occ


def pack_islands(ob, islands):
    """Pack the uv islands onto the texture's texel grid as big as they fit, no texel shared
    by two islands (so nearest sampling never picks up a neighbour's colour)."""
    me = ob.data
    bm = bmesh.new()
    bm.from_mesh(me)
    uvl = bm.loops.layers.uv.active
    bm.faces.ensure_lookup_table()
    shapes = []
    for faces in islands:
        loops = [l for i in faces for l in bm.faces[i].loops]
        pts = np.array([tuple(l[uvl].uv) for l in loops])
        ang = min_area_rotation(pts)
        c, s_ = math.cos(ang), math.sin(ang)
        r = pts @ np.array(((c, s_), (-s_, c)))
        r -= r.min(0)
        tris, n = [], 0
        for i in faces:
            fl = len(bm.faces[i].loops)
            for t in range(1, fl - 1):
                tris.append((n, n + t, n + t + 1))
            n += fl
        shapes.append((loops, r, tris))
    reserved = np.zeros((TEX, TEX), dtype=bool)
    for (x0, y0) in SWATCHES.values():
        reserved[y0:y0 + 4, x0:x0 + 4] = True
    area = sum(0.5 * abs(np.cross(sh[1][t[1]] - sh[1][t[0]], sh[1][t[2]] - sh[1][t[0]])) for sh in shapes for t in sh[2])
    lo, hi = 0.3 * TEX / math.sqrt(area), 1.0 * TEX / math.sqrt(area)
    best = None
    for _ in range(9):
        k = (lo + hi) / 2
        res = try_pack(shapes, k, reserved)
        if res is None:
            hi = k
        else:
            lo, best = k, (k, res)
    if best is None:
        best = (lo, try_pack(shapes, lo, reserved))
    k, (placed, occ) = best
    for idx, q in placed:
        for l, t in zip(shapes[idx][0], q):
            l[uvl].uv = (float(t[0]) / TEX, float(t[1]) / TEX)
    bm.to_mesh(me)
    bm.free()
    log("packed %d islands at %.1f texels per uv unit: texels used %d of %d, triangles cover %.1f%%"
        % (len(shapes), k, occ.sum() - reserved.sum(), TEX * TEX, 100 * area * k * k / (TEX * TEX)))


def unwrap(ob):
    me = ob.data
    while me.uv_layers:
        me.uv_layers.remove(me.uv_layers[0])
    me.uv_layers.new(name="UVMap")
    part = np.empty(len(me.polygons), dtype=np.int32)
    me.attributes["zpart"].data.foreach_get("value", part)
    cls = np.empty(len(me.polygons), dtype=np.int32)
    me.attributes["zcls"].data.foreach_get("value", cls)
    mouth = np.isin(cls, list(SWATCHES))
    set_active(ob)
    bpy.context.scene.tool_settings.use_uv_select_sync = True
    bpy.ops.object.mode_set(mode='EDIT')
    select_faces(ob, ~mouth)
    r = bpy.ops.uv.smart_project(angle_limit=math.radians(ANGLE_LIMIT), island_margin=0.0, area_weight=0.0,
                             correct_aspect=True, scale_to_bounds=False)
    log("smart project:", r)
    bpy.ops.object.mode_set(mode='OBJECT')

    # give the head (the face is what's looked at) more texels: scale its islands up first
    bm = bmesh.new()
    bm.from_mesh(me)
    uvl = bm.loops.layers.uv.active
    bm.faces.ensure_lookup_table()
    islands = uv_islands(bm, uvl, [i for i in range(len(bm.faces)) if not mouth[i]])
    boosted = 0
    for faces in islands:
        if sum(1 for i in faces if part[i] == PART_HEAD) * 2 > len(faces):
            k = HEAD_TEXELS
        elif sum(1 for i in faces if part[i] == PART_HAND) * 2 > len(faces):
            k = HAND_TEXELS
        else:
            continue
        boosted += 1
        uvs = [l[uvl].uv for i in faces for l in bm.faces[i].loops]
        cx = sum(u.x for u in uvs) / len(uvs)
        cy = sum(u.y for u in uvs) / len(uvs)
        for u in uvs:
            u.x = cx + (u.x - cx) * k
            u.y = cy + (u.y - cy) * k
    bm.to_mesh(me)
    bm.free()
    sizes = sorted((len(f) for f in islands), reverse=True)
    log("uv islands: %d (%d rescaled for head/hands); largest %s; single faces %d"
        % (len(islands), boosted, sizes[:8], sum(1 for x in sizes if x == 1)))

    pack_islands(ob, islands)

    # collapse the mouth parts onto their swatches
    uv = np.empty(len(me.loops) * 2)
    me.uv_layers.active.data.foreach_get("uv", uv)
    uv = uv.reshape(-1, 2)
    loop_face = np.repeat(np.arange(len(me.polygons)), [p.loop_total for p in me.polygons])
    for c, (x0, y0) in SWATCHES.items():
        m = cls[loop_face] == c
        uv[m] = ((x0 + 2.0) / TEX, (y0 + 2.0) / TEX)
    me.uv_layers.active.data.foreach_set("uv", uv.reshape(-1))
    me.update()
    assert uv.min() >= 0 and uv.max() <= 1, (uv.min(), uv.max())
    used = 0.0
    for p in me.polygons:
        if mouth[p.index]:
            continue
        t = [Vector(uv[li]) for li in p.loop_indices]
        used += abs((t[1] - t[0]).cross(t[2] - t[0])) / 2
    log("uv coverage %.1f%% of the texture" % (100 * used))


# ---------------------------------------------------------------- texture

def bake_maps(ob):
    """Bake rest-pose position, true normal, class/part and ambient occlusion into float images."""
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    sc.render.bake.margin_type = 'EXTEND'
    sc.render.bake.use_clear = True
    me = ob.data
    # per-corner class/part colour for the bake
    cls = np.empty(len(me.polygons), dtype=np.int32)
    me.attributes["zcls"].data.foreach_get("value", cls)
    part = np.empty(len(me.polygons), dtype=np.int32)
    me.attributes["zpart"].data.foreach_get("value", part)
    info = me.color_attributes.new("zinfo", 'FLOAT_COLOR', 'CORNER')
    col = np.zeros((len(me.loops), 4), dtype=np.float32)
    for p in me.polygons:
        for li in p.loop_indices:
            col[li] = ((cls[p.index] + 0.5) / 8.0, (part[p.index] + 0.5) / 4.0, 0, 1)
    info.data.foreach_set("color", col.reshape(-1))

    vs = np.empty(len(me.vertices) * 3)
    me.vertices.foreach_get("co", vs)
    vs = vs.reshape(-1, 3)
    lo = vs.min(0) - 0.05
    hi = vs.max(0) + 0.05

    mat = bpy.data.materials.new("bake")
    mat.use_nodes = True
    me.materials.clear()
    me.materials.append(mat)
    nt = mat.node_tree
    nt.nodes.clear()
    outn = nt.nodes.new("ShaderNodeOutputMaterial")
    emit = nt.nodes.new("ShaderNodeEmission")
    emit.inputs["Strength"].default_value = 1.0
    nt.links.new(emit.outputs["Emission"], outn.inputs["Surface"])
    tex = nt.nodes.new("ShaderNodeTexImage")
    nt.nodes.active = tex

    geo = nt.nodes.new("ShaderNodeNewGeometry")
    attr = nt.nodes.new("ShaderNodeAttribute")
    attr.attribute_name = "zinfo"
    ao = nt.nodes.new("ShaderNodeAmbientOcclusion")
    ao.samples = 16
    ao.only_local = True
    ao.inputs["Distance"].default_value = 0.25
    # position mapped into 0..1 by the bounds
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    comb = nt.nodes.new("ShaderNodeCombineXYZ")
    nt.links.new(geo.outputs["Position"], sep.inputs[0])
    for i, ax in enumerate("XYZ"):
        mr = nt.nodes.new("ShaderNodeMapRange")
        mr.clamp = False
        mr.inputs["From Min"].default_value = lo[i]
        mr.inputs["From Max"].default_value = hi[i]
        nt.links.new(sep.outputs[ax], mr.inputs["Value"])
        nt.links.new(mr.outputs["Result"], comb.inputs[ax])
    vm = nt.nodes.new("ShaderNodeVectorMath")
    vm.operation = 'MULTIPLY_ADD'
    vm.inputs[1].default_value = (0.5, 0.5, 0.5)
    vm.inputs[2].default_value = (0.5, 0.5, 0.5)
    nt.links.new(geo.outputs["True Normal"], vm.inputs[0])

    set_active(ob)
    maps = {}
    margin = BAKE // TEX * 2
    for key, socket, samples, mg in (("pos", comb.outputs[0], 1, margin), ("nrm", vm.outputs[0], 1, margin),
                                     ("info", attr.outputs["Color"], 1, margin), ("ao", ao.outputs["AO"], 32, margin),
                                     ("cover", None, 1, 0)):
        img = bpy.data.images.new("bake_" + key, BAKE, BAKE, alpha=True, float_buffer=True)
        img.colorspace_settings.name = 'Non-Color'
        img.generated_color = (0, 0, 0, 0)
        tex.image = img
        for l in list(emit.inputs["Color"].links):
            nt.links.remove(l)
        if socket is not None:
            nt.links.new(socket, emit.inputs["Color"])
        else:
            emit.inputs["Color"].default_value = (1, 1, 1, 1)
        sc.cycles.samples = samples
        bpy.ops.object.bake(type='EMIT', margin=mg, use_clear=True)
        a = np.array(img.pixels[:], dtype=np.float32).reshape(BAKE, BAKE, 4)
        maps[key] = a
        bpy.data.images.remove(img)
    pos = maps["pos"][:, :, :3] * (hi - lo) + lo
    nrm = maps["nrm"][:, :, :3] * 2 - 1
    cls_map = np.clip(np.floor(maps["info"][:, :, 0] * 8), 0, 7).astype(np.int32)
    part_map = np.clip(np.floor(maps["info"][:, :, 1] * 4), 0, 3).astype(np.int32)
    covered = maps["cover"][:, :, 0] > 0.5
    ao_map = maps["ao"][:, :, 0]
    me.materials.clear()
    bpy.data.materials.remove(mat)
    me.color_attributes.remove(me.color_attributes["zinfo"])
    log("baked maps %dx%d, %.1f%% of the pixels inside triangles" % (BAKE, BAKE, 100 * covered.mean()))
    return pos, nrm, cls_map, part_map, ao_map, covered


_PERM = np.random.default_rng(1919).permutation(256)
_PERM = np.concatenate([_PERM, _PERM])
_GRAD = np.random.default_rng(7).random(256)


def value_noise(p):
    """Smooth 3D value noise in 0..1 for points p (..., 3)."""
    pi = np.floor(p).astype(np.int64)
    pf = p - pi
    w = pf * pf * (3 - 2 * pf)
    xi, yi, zi = (pi[..., 0] & 255), (pi[..., 1] & 255), (pi[..., 2] & 255)

    def h(dx, dy, dz):
        return _GRAD[_PERM[_PERM[_PERM[(xi + dx) & 255] + ((yi + dy) & 255)] + ((zi + dz) & 255)]]
    x00 = h(0, 0, 0) * (1 - w[..., 0]) + h(1, 0, 0) * w[..., 0]
    x10 = h(0, 1, 0) * (1 - w[..., 0]) + h(1, 1, 0) * w[..., 0]
    x01 = h(0, 0, 1) * (1 - w[..., 0]) + h(1, 0, 1) * w[..., 0]
    x11 = h(0, 1, 1) * (1 - w[..., 0]) + h(1, 1, 1) * w[..., 0]
    y0 = x00 * (1 - w[..., 1]) + x10 * w[..., 1]
    y1 = x01 * (1 - w[..., 1]) + x11 * w[..., 1]
    return y0 * (1 - w[..., 2]) + y1 * w[..., 2]


def fbm(p, octaves=4):
    tot, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        tot = tot + amp * value_noise(p * (2.0 ** o) + o * 17.31)
        norm += amp
        amp *= 0.5
    return tot / norm


def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def mix(c, d, t):
    return c * (1 - t[..., None]) + np.asarray(d, dtype=np.float64) * t[..., None]


def srgb(*c):
    return np.array(c, dtype=np.float64) / 255.0


def face_anchors(ob, f_rest):
    """Eye and mouth centres on the (swollen, rest) face surface: rays cast backwards from the front."""
    from mathutils.bvhtree import BVHTree
    me = ob.data
    tree = BVHTree.FromPolygons([v.co.copy() for v in me.vertices], [tuple(p.vertices) for p in me.polygons])
    out = {}
    for key, src in (("eyeL", SRC_EYES[0]), ("eyeR", SRC_EYES[1]), ("mouth", SRC_MOUTH)):
        q = f_rest @ src
        hit, _n, _i, _d = tree.ray_cast(Vector((q.x, -2.0, q.z)), Vector((0, 1, 0)))
        assert hit is not None, key
        out[key] = np.array(hit)
    log("face anchors (m): " + ", ".join("%s %s" % (k, v.round(3)) for k, v in out.items()))
    return out


def texel_density(ob):
    me = ob.data
    uv = me.uv_layers.active.data
    part = np.empty(len(me.polygons), dtype=np.int32)
    me.attributes["zpart"].data.foreach_get("value", part)
    acc = {}
    for p in me.polygons:
        if p.area < 1e-8:
            continue
        t = [uv[i].uv for i in p.loop_indices]
        a = 0.0
        for k in range(1, len(t) - 1):
            a += abs((t[k] - t[0]).cross(t[k + 1] - t[0])) / 2
        if a == 0:
            continue
        d = acc.setdefault(int(part[p.index]), [0.0, 0.0])
        d[0] += a * TEX * TEX
        d[1] += p.area
    names = {PART_BODY: "body", PART_HEAD: "head", PART_HAND: "hands", PART_FOOT: "feet"}
    log("texel size: " + ", ".join("%s %.1f cm" % (names[k], 100 / math.sqrt(v[0] / v[1])) for k, v in sorted(acc.items())))


def paint(pos, nrm, cls, part, ao, face):
    """Colour every baked texel (sRGB 0..1)."""
    H, W = cls.shape
    col = np.zeros((H, W, 3))
    p = pos
    z = p[..., 2]
    up = nrm[..., 2]
    front = -nrm[..., 1]               # the model faces -Y
    head = (part == PART_HEAD)
    hands = (part == PART_HAND)
    feet = (part == PART_FOOT)

    # ---- skin: pale grey-blue, marbled; livor in purple-blue blotches, rot green in places
    skin = np.broadcast_to(srgb(140, 150, 154), (H, W, 3)).copy()
    n1 = fbm(p * 9.0)
    n2 = fbm(p * 6.0 + 31.0)
    n3 = fbm(p * 11.0 + 5.0, 3)
    skin = mix(skin, srgb(80, 82, 110), smooth(0.50, 0.68, n1) * 0.8)        # livor blotches
    skin = mix(skin, srgb(112, 128, 98), smooth(0.58, 0.74, n2) * 0.65)      # rot green
    vein = 1 - smooth(0.0, 0.045, np.abs(n3 - 0.5))
    skin = mix(skin, srgb(50, 60, 78), vein * 0.65 * (1 - head * 0.5))      # marbled veins
    skin = mix(skin, srgb(70, 66, 96), smooth(0.60, 0.05, z) * 0.7)         # blood pooled low
    skin[hands] = mix(skin[hands], srgb(156, 162, 160), np.full(hands.sum(), 0.4))   # washerwoman hands
    skin[feet] = mix(skin[feet], srgb(58, 54, 52), np.full(feet.sum(), 0.55))        # silt on the feet
    # the face: bloodless, dark sunken sockets each with a clouded eye, bruised mouth
    skin[head] = mix(skin[head], srgb(168, 176, 178), np.full(head.sum(), 0.35))
    facing = smooth(0.05, 0.35, front) * head
    for key in ("eyeL", "eyeR"):
        e = face[key]
        dx = (p[..., 0] - e[0]) / 0.024
        dz = (p[..., 2] - e[2]) / 0.019
        near = smooth(e[1] + 0.05, e[1] + 0.02, p[..., 1])          # on the face, not behind it
        r = np.sqrt(dx * dx + dz * dz)
        skin = mix(skin, srgb(84, 80, 104), smooth(1.9, 1.2, r) * facing * near * 0.8)  # bruised ring
        skin = mix(skin, srgb(14, 14, 20), smooth(1.15, 0.85, r) * facing * near)       # the socket
        skin = mix(skin, srgb(176, 184, 168), smooth(0.42, 0.30, r) * facing * near)    # clouded eye
        # dark runs down the cheek from each socket
        run = (smooth(0.016, 0.006, np.abs(p[..., 0] - e[0] - 0.004)) * smooth(e[2] - 0.016, e[2] - 0.03, z)
               * smooth(e[2] - 0.10, e[2] - 0.06, z))
        skin = mix(skin, srgb(56, 60, 82), run * facing * near * 0.75)
    m = face["mouth"]
    dm = np.sqrt(((p[..., 0] - m[0]) / 0.040) ** 2 + ((p[..., 2] - m[2]) / 0.024) ** 2)
    nearm = smooth(m[1] + 0.05, m[1] + 0.02, p[..., 1])
    skin = mix(skin, srgb(46, 40, 62), smooth(1.4, 0.9, dm) * facing * nearm)
    col[:] = skin

    # ---- shirt: soaked undershirt gone grey-brown, water stains, silt, a little weed, tears
    shirt = np.broadcast_to(srgb(122, 116, 104), (H, W, 3)).copy()
    s1 = fbm(p * 7.0 + 3.0)
    s2 = fbm(p * np.array((10.0, 10.0, 2.5)) + 11.0)
    s3 = fbm(p * 12.0 + 41.0, 3)
    shirt = mix(shirt, srgb(66, 62, 54), smooth(0.46, 0.62, s1) * 0.85)    # wet stains
    shirt = mix(shirt, srgb(74, 86, 64), smooth(0.60, 0.76, s2) * 0.6)     # weed streaks
    shirt = mix(shirt, srgb(16, 16, 16), smooth(0.68, 0.73, s3))          # tears
    shirt = mix(shirt, srgb(54, 50, 44), smooth(1.12, 0.96, z) * 0.7)     # soaked hem
    m_ = cls == SHIRT
    col[m_] = shirt[m_]

    # ---- trousers: dark sodden cloth caked in silt, no blue left
    pants = np.broadcast_to(srgb(58, 56, 54), (H, W, 3)).copy()
    t1 = fbm(p * 6.0 + 71.0)
    t2 = fbm(p * 12.0 + 13.0, 3)
    pants = mix(pants, srgb(92, 82, 64), smooth(0.52, 0.70, t1) * 0.8)    # silt
    pants = mix(pants, srgb(34, 34, 34), smooth(0.44, 0.28, t1) * 0.7)    # wet
    pants = mix(pants, srgb(12, 12, 12), smooth(0.70, 0.75, t2))          # tears
    pants = mix(pants, srgb(96, 86, 68), smooth(0.40, 0.12, z) * 0.6)     # mud at the cuffs
    m_ = cls == PANTS
    col[m_] = pants[m_]

    # ---- bone: dull yellow-grey, grimy
    bone = np.broadcast_to(srgb(136, 128, 102), (H, W, 3)).copy()
    bone = mix(bone, srgb(84, 76, 58), smooth(0.40, 0.66, fbm(p * 16.0 + 2.0, 3)) * 0.85)
    m_ = cls == BONE
    col[m_] = bone[m_]

    # ---- wounds: washed-out dark meat with pale ragged rims
    wound = np.broadcast_to(srgb(92, 34, 40), (H, W, 3)).copy()
    wound = mix(wound, srgb(34, 10, 16), smooth(0.42, 0.64, fbm(p * 16.0 + 9.0, 3)))
    wound = mix(wound, srgb(140, 112, 116), smooth(0.68, 0.82, fbm(p * 12.0 + 23.0, 3)) * 0.55)
    m_ = cls == WOUND
    col[m_] = wound[m_]
    col[cls == DARK] = srgb(16, 16, 20)

    # ---- light: ambient occlusion, a little darker facing down
    shade = (0.45 + 0.55 * np.clip(ao, 0, 1)) * (0.88 + 0.12 * np.clip(up + 1, 0, 1))
    col *= shade[..., None]
    return np.clip(col, 0, 1)


def finish_texture(col, covered):
    # each texel: the mean of its baked pixels inside triangles (no two islands share a texel),
    # or of its margin pixels where no triangle covers a pixel centre
    k = BAKE // TEX
    w = covered.astype(np.float64).reshape(TEX, k, TEX, k)
    n = w.sum(axis=(1, 3))
    inside = (col.reshape(TEX, k, TEX, k, 3) * w[..., None]).sum(axis=(1, 3)) / np.maximum(n, 1)[..., None]
    plain = col.reshape(TEX, k, TEX, k, 3).mean(axis=(1, 3))
    small = np.where((n > 0)[..., None], inside, plain)
    # raise contrast a little around the mean, then quantize to 32 levels a channel
    mean = small.mean(axis=(0, 1), keepdims=True)
    small = np.clip((small - mean) * 1.12 + mean, 0, 1)
    for c, (x0, y0) in SWATCHES.items():
        small[y0:y0 + 4, x0:x0 + 4] = SWATCH_COLOURS[c]
    small = np.round(small * 31) / 31
    return small


def save_png(small, path):
    img = bpy.data.images.new("zombie_skin", TEX, TEX, alpha=False, float_buffer=False)
    img.colorspace_settings.name = 'sRGB'
    rgba = np.ones((TEX, TEX, 4), dtype=np.float32)
    rgba[:, :, :3] = small
    img.pixels.foreach_set(rgba.reshape(-1))
    img.filepath_raw = path
    img.file_format = 'PNG'
    img.save()
    bpy.data.images.remove(img)


# ---------------------------------------------------------------- export

def cleanup(keep):
    for o in list(bpy.data.objects):
        if o not in keep:
            bpy.data.objects.remove(o, do_unlink=True)
    for a in list(bpy.data.actions):
        if a.name not in CLIP_ORDER:
            bpy.data.actions.remove(a)
    for m in list(bpy.data.materials):
        bpy.data.materials.remove(m)
    for im in list(bpy.data.images):
        bpy.data.images.remove(im)
    for me in list(bpy.data.meshes):
        if me.users == 0:
            bpy.data.meshes.remove(me)
    for ar in list(bpy.data.armatures):
        if ar.users == 0:
            bpy.data.armatures.remove(ar)


def export(arm, ob, path):
    me = ob.data
    for name in ("zcls", "zpart"):
        if name in me.attributes:
            me.attributes.remove(me.attributes[name])
    for p in me.polygons:
        p.use_smooth = False
    me.materials.clear()
    # every clip is an action of the rig's; the exporter's ACTIONS mode writes each one
    ad = arm.animation_data or arm.animation_data_create()
    ad.action = None
    for t in list(ad.nla_tracks):
        ad.nla_tracks.remove(t)
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix()
    set_active(arm)
    ob.select_set(True)
    bpy.ops.export_scene.gltf(
        filepath=path, export_format='GLB', use_selection=True, export_yup=True,
        export_apply=False, export_texcoords=True, export_normals=True, export_tangents=False,
        export_materials='NONE', export_vertex_color='NONE', export_attributes=False,
        export_skins=True, export_influence_nb=4, export_all_influences=False, export_def_bones=False,
        export_morph=False, export_animations=True, export_animation_mode='ACTIONS',
        export_force_sampling=True, export_frame_range=False, export_anim_slide_to_zero=True,
        export_optimize_animation_size=True, export_reset_pose_bones=True,
        export_draco_mesh_compression_enable=False, export_cameras=False, export_lights=False,
        export_extras=False)


def main():
    src, out = parse_args()
    os.makedirs(out, exist_ok=True)
    rest_bases = node_rest_bases(src)
    old_arm, old_mesh, palette = load_source(src)
    face_cls = classify_faces(old_mesh, palette)
    face_part = face_parts(old_mesh)
    log("faces by class:", {k: face_cls.count(v) for k, v in
                            (("skin", SKIN), ("teeth", TEETH), ("gums", GUMS), ("bone", BONE),
                             ("shirt", SHIRT), ("jeans", PANTS), ("wound", WOUND))})
    arm, ob, to_new, f_rest, s = build_rig(old_arm, old_mesh, face_cls, face_part)
    made = transfer_clips(old_arm, arm, to_new, rest_bases)
    verify_clips(arm, made)
    cleanup([arm, ob])
    merge_and_swell(ob, arm)
    ground_clips(arm, ob)
    unwrap(ob)
    texel_density(ob)
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix()
    arm.data.pose_position = 'REST'
    face = face_anchors(ob, f_rest)
    pos, nrm, cls, part, ao, covered = bake_maps(ob)
    col = paint(pos, nrm, cls, part, ao, face)
    small = finish_texture(col, covered)
    arm.data.pose_position = 'POSE'
    save_png(small, os.path.join(out, "zombie.png"))
    export(arm, ob, os.path.join(out, "zombie.glb"))
    log("wrote", os.path.join(out, "zombie.glb"), "and zombie.png")


main()
