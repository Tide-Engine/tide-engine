"""Regenerates FOLDLINE's spider from Quaternius' CC0 "Spider" (Easy Enemy Pack).

Headless only:
    blender -b --factory-startup --python make_spider.py -- [source.glb] [out dir]

Defaults: the source at %TEMP%/foldline-monsters/pp-spider/pp-spider.glb, the output beside this tools folder in
../spider/. Writes spider.glb (mesh, skin, clips Idle/Walk/Attack/Jump/Death; no materials, no textures), spider.png
(the 64x64 palette skin) and preview_*.png. Checks the .glb (common_spider.verify_glb: one skin, unique names, every
channel on a joint, the five clips) and samples every joint at the first, middle and last frame of every clip in the
export and in the scaled source, failing if they differ by a millimetre. The tidec import probe is separate:
    python common_spider.py probe <tidec.exe> <out dir> <scratch dir>

What it changes:
- Reads a copy of the source whose clips state every channel glTF holds at the node's TRS
  (common_spider.hold_channels): Blender's importer would hold those at the bind pose instead, losing the abdomen's
  0.88 scale in every clip and Idle's body roll, whose legs then end 6-10 cm off their feet.
- Drops the importer's empties (FBX2glTF's *_end leaf nodes, the RootNode wrapper) and the two unused IK pole
  bones (PoleTarget.L/R: no weights, no keys). Renames FrontFoot2.R to FrontFoot.R to match its mirror.
- Applies the armature's 100x scale and -90 degree turn and scales the whole spider by SCALE, so it stands on y = 0
  in metres with an identity armature node; translation keys are scaled with it.
- Clips renamed to Idle, Walk, Attack, Jump, Death.
- The two material colours become a palette texture: every face's uvs collapse onto its part's strip of texels,
  and along a strip by how much the corner faces a fixed key light from above, so the chitin reads as glossy at
  64 px with nearest filtering. The eyes have their own 8x8 block (EYE_BLOCK).
"""
import math
import os
import sys
from collections import Counter, defaultdict

import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import common_spider as cs  # noqa: E402

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
TEMP = os.environ.get("TEMP", os.path.dirname(os.path.dirname(os.path.dirname(HERE))))
SRC = ARGS[0] if len(ARGS) > 0 else os.path.join(TEMP, "foldline-monsters", "pp-spider", "pp-spider.glb")
OUT = ARGS[1] if len(ARGS) > 1 else os.path.join(os.path.dirname(HERE), "spider")
SRC_HELD = os.path.join(OUT, "_source_held.glb")  # the source with its held channels stated; removed at the end
DROP_BONES = ["PoleTarget.L", "PoleTarget.R"]

# Source spans 5.94 x 1.95 x 5.19 (span, height, length) at rest. 0.385 gives a 2.29 m leg span, 2.0 m from front
# feet to back feet and 0.75 m height at rest (0.78 m at the idle's highest knee): between the conventions' 2 m span
# and 0.9 m height, which this model's proportions can't both meet.
SCALE = 0.385
CLIP_NAMES = {"Spider_Idle": "Idle", "Spider_Walk": "Walk", "Spider_Attack": "Attack", "Spider_Jump": "Jump",
              "Spider_Death": "Death"}

# ---------------------------------------------------------------------------------------------------------------
# The palette. Pixel rectangles (x0, y0, x1, y1) count from the PNG's top-left, as glTF uvs do:
# u = x / 64, v = y / 64.
TEX = 64
EYE_BLOCK = (56, 0, 64, 8)      # the eyes, and nothing else: u 0.875..1, v 0..0.125
MARK_BLOCK = (0, 0, 8, 8)       # the hourglass under the abdomen, a dull blood red (darker than the eyes)
FANG_TIP_BLOCK = (8, 0, 16, 8)  # the fangs' points, wet bone
STRIPS = {                       # full-width strips; along x the key light's ramp
    "chitin": (0, 16, 64, 24),  # carapace, waist, abdomen
    "leg": (0, 32, 64, 40),     # legs and palps
    "fang": (0, 48, 64, 56),    # the chelicerae
}
# Ramps: (t, sRGB) stops. t is how much a corner faces the key light: 0 away, 0.5 side-on, 1 straight at it.
RAMPS = {
    "chitin": [(0.0, (6, 6, 7)), (0.5, (15, 14, 16)), (0.8, (24, 22, 26)), (0.9, (34, 32, 38)),
               (0.96, (62, 61, 72)), (1.0, (128, 130, 146))],
    "leg": [(0.0, (7, 5, 5)), (0.5, (17, 13, 13)), (0.8, (27, 22, 22)), (0.9, (38, 32, 33)),
            (0.96, (64, 58, 62)), (1.0, (118, 112, 120))],
    "fang": [(0.0, (14, 4, 4)), (0.5, (36, 9, 9)), (0.85, (60, 16, 16)), (1.0, (122, 52, 48))],
    "eye": [(0.0, (150, 0, 0)), (0.6, (215, 10, 4)), (0.85, (255, 36, 16)), (1.0, (255, 140, 100))],
}
MARK_COLOR = (92, 6, 8)
FANG_TIP_COLOR = (176, 162, 132)
BACKGROUND = (8, 8, 8)
KEY = Vector((0.25, -0.35, 1.0)).normalized()  # Blender axes: up +Z, the spider's front -Y


def byte_color(c):
    """Rounds to bytes. (The PSX's 32 levels a channel were tried: they turn these near-blacks into blue and purple
    specks, so the ramps stay at full depth.)"""
    return tuple(int(round(x)) for x in c)


def ramp(stops, t):
    t = min(1.0, max(0.0, t))
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        if t <= t1:
            f = 0 if t1 == t0 else (t - t0) / (t1 - t0)
            return tuple(a + (b - a) * f for a, b in zip(c0, c1))
    return stops[-1][1]


def palette_rows():
    rows = [[byte_color(BACKGROUND)] * TEX for _ in range(TEX)]

    def fill(rect, color_of_x):
        x0, y0, x1, y1 = rect
        for y in range(y0, y1):
            for x in range(x0, x1):
                rows[y][x] = byte_color(color_of_x(x - x0, x1 - x0))

    for name, rect in STRIPS.items():
        fill(rect, lambda i, n, s=RAMPS[name]: ramp(s, (i + 0.5) / n))
    fill(EYE_BLOCK, lambda i, n: ramp(RAMPS["eye"], (i + 0.5) / n))
    fill(MARK_BLOCK, lambda i, n: MARK_COLOR)
    fill(FANG_TIP_BLOCK, lambda i, n: FANG_TIP_COLOR)
    return rows


def strip_uv(rect, t):
    """A Blender uv (origin bottom-left) on a strip's middle row, at t along it."""
    x0, y0, x1, y1 = rect
    n = x1 - x0
    t = min(1.0, max(0.0, t))
    u = (x0 + 0.5 + t * (n - 1)) / TEX
    v = ((y0 + y1) / 2) / TEX
    return (u, 1.0 - v)


# ---------------------------------------------------------------------------------------------------------------
def select_only(*objs):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]


def load_source():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = 24
    bpy.ops.import_scene.gltf(filepath=SRC_HELD, disable_bone_shape=True)
    arm = [o for o in bpy.data.objects if o.type == 'ARMATURE']
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    assert len(arm) == 1 and len(meshes) == 1, (arm, meshes)
    return arm[0], meshes[0]


def flatten(arm, body):
    """Unparents both from the importer's wrappers, applies their transforms at SCALE and scales the keys."""
    view = bpy.context.view_layer
    view.update()
    worlds = {o.name: o.matrix_world.copy() for o in (arm, body)}
    for o in (arm, body):
        o.parent = None
    for o in list(bpy.data.objects):
        if o not in (arm, body):
            bpy.data.objects.remove(o, do_unlink=True)
    s = Matrix.Scale(SCALE, 4)
    for o in (arm, body):
        o.matrix_world = s @ worlds[o.name]
    view.update()
    factor = arm.matrix_world.to_scale()
    assert max(factor) - min(factor) < 1e-4 * max(factor), factor
    factor = factor.x
    select_only(body)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    select_only(arm)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    body.parent = arm
    body.matrix_parent_inverse = Matrix.Identity(4)
    for act in bpy.data.actions:
        for fc in act.fcurves:
            if fc.data_path.endswith(".location"):
                for kp in fc.keyframe_points:
                    kp.co[1] *= factor
                    kp.handle_left[1] *= factor
                    kp.handle_right[1] *= factor
    print("flatten: key translation factor %.4f" % factor)


def fix_bones(arm, body):
    renames = {"FrontFoot2.R": "FrontFoot.R"}
    drop = DROP_BONES
    for act in bpy.data.actions:
        for fc in act.fcurves:
            assert not any('"%s"' % d in fc.data_path for d in drop), fc.data_path
    select_only(arm)
    bpy.ops.object.mode_set(mode='EDIT')
    eb = arm.data.edit_bones
    for d in drop:
        eb.remove(eb[d])
    for old, new in renames.items():
        eb[old].name = new
    bpy.ops.object.mode_set(mode='OBJECT')
    for d in drop:
        vg = body.vertex_groups.get(d)
        if vg:
            body.vertex_groups.remove(vg)
    for old, new in renames.items():
        vg = body.vertex_groups.get(old)
        if vg:
            vg.name = new
        for act in bpy.data.actions:
            for fc in act.fcurves:
                if '"%s"' % old in fc.data_path:
                    fc.data_path = fc.data_path.replace('"%s"' % old, '"%s"' % new)
            for grp in act.groups:
                if grp.name == old:
                    grp.name = new
    bones = {b.name for b in arm.data.bones}
    for act in bpy.data.actions:
        for fc in act.fcurves:
            if fc.data_path.startswith("pose.bones"):
                assert fc.data_path.split('"')[1] in bones, (act.name, fc.data_path)
    for vg in body.vertex_groups:
        assert vg.name in bones, vg.name


def rename_clips(arm):
    for act in bpy.data.actions:
        base = act.name.split("|")[-1]
        act.name = CLIP_NAMES[base]
    ad = arm.animation_data
    for t in ad.nla_tracks:
        if t.strips:
            t.name = t.strips[0].action.name
            t.strips[0].name = t.strips[0].action.name
    ad.action = None
    for pb in arm.pose.bones:
        pb.location = (0, 0, 0)
        pb.rotation_quaternion = (1, 0, 0, 0)
        pb.scale = (1, 1, 1)
    arm.name = "Spider"
    arm.data.name = "Spider"


def hourglass(x, y):
    """The mark under the abdomen, in units of the abdomen's half-length from its middle: two triangles meeting
    at a narrow waist."""
    d = abs(y)
    return d < 0.7 and abs(x) < 0.08 + 0.45 * d


def islands(me):
    parent = list(range(len(me.vertices)))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    seen = {}
    for v in me.vertices:
        k = (round(v.co.x, 5), round(v.co.y, 5), round(v.co.z, 5))
        if k in seen:
            parent[find(v.index)] = find(seen[k])
        else:
            seen[k] = v.index
    for p in me.polygons:
        r = find(p.vertices[0])
        for vi in p.vertices[1:]:
            parent[find(vi)] = r
    return [find(p.vertices[0]) for p in me.polygons]


def paint(body):
    """Classifies every face into a part and writes the uvs that put it on its palette texels."""
    me = body.data
    names = [g.name for g in body.vertex_groups]
    source_mat = [p.material_index for p in me.polygons]
    eye_mat = [i for i, m in enumerate(me.materials) if m and m.name == "Material.001"]
    assert eye_mat, [m.name for m in me.materials]
    eye_mat = eye_mat[0]
    isl = islands(me)

    def dominant(p):
        c = Counter()
        for vi in p.vertices:
            for g in me.vertices[vi].groups:
                c[names[g.group]] += g.weight
        return c.most_common(1)[0][0]

    bone = [dominant(p) for p in me.polygons]
    # Islands: the body (it holds the abdomen), four eyes, eight legs, two palps.
    by_island = defaultdict(list)
    for p in me.polygons:
        by_island[isl[p.index]].append(p.index)
    main = max(by_island, key=lambda k: sum(1 for f in by_island[k] if bone[f] == "Abdomen"))
    abdomen = [p for p in me.polygons if bone[p.index] == "Abdomen"]
    ab_lo = min(me.vertices[v].co.y for p in abdomen for v in p.vertices)
    ab_hi = max(me.vertices[v].co.y for p in abdomen for v in p.vertices)
    ab_mid = (ab_lo + ab_hi) / 2
    ab_half = (ab_hi - ab_lo) / 2
    eye_lo = min(me.vertices[v].co.z for p in me.polygons if source_mat[p.index] == eye_mat for v in p.vertices)

    part = []
    for p in me.polygons:
        c = p.center
        b = bone[p.index]
        if source_mat[p.index] == eye_mat:
            k = "eye"
        elif "Leg" in b or "Foot" in b:
            k = "leg"
        elif b == "Head" and isl[p.index] != main:
            k = "palp"
        elif b == "Head" and c.z < eye_lo - 0.01 * SCALE and c.y < -0.35 * SCALE:
            k = "fang"
        elif b == "Abdomen" and p.normal.z < -0.3 and hourglass(c.x / ab_half, (c.y - ab_mid) / ab_half):
            k = "mark"
        else:
            k = "chitin"
        part.append(k)
    # The fangs' lowest faces are their points.
    fang_faces = [p for p in me.polygons if part[p.index] == "fang"]
    if fang_faces:
        z_lo = min(p.center.z for p in fang_faces)
        z_hi = max(p.center.z for p in fang_faces)
        for p in fang_faces:
            if p.center.z < z_lo + 0.22 * (z_hi - z_lo):
                part[p.index] = "fangtip"
    print("paint: parts", dict(Counter(part)))

    uv = me.uv_layers.new(name="UVMap")
    me.uv_layers.active = uv
    normals = me.corner_normals
    for p in me.polygons:
        k = part[p.index]
        for li in p.loop_indices:
            n = Vector(normals[li].vector)
            t = (n.dot(KEY) + 1) / 2
            if k == "eye":
                u = strip_uv(EYE_BLOCK, t)
            elif k == "mark":
                u = strip_uv(MARK_BLOCK, 0.5)
            elif k == "fangtip":
                u = strip_uv(FANG_TIP_BLOCK, 0.5)
            elif k == "palp":
                u = strip_uv(STRIPS["leg"], t)
            else:
                u = strip_uv(STRIPS[k], t)
            uv.data[li].uv = u
    me.materials.clear()
    body.name = "SpiderBody"
    me.name = "SpiderBody"
    return Counter(part)


def export(arm, body, path):
    select_only(arm, body)
    bpy.ops.export_scene.gltf(
        filepath=path, export_format='GLB', use_selection=True, export_yup=True,
        export_texcoords=True, export_normals=True, export_tangents=False,
        export_materials='NONE', export_vertex_color='NONE', export_attributes=False, export_image_format='NONE',
        export_cameras=False, export_lights=False, export_extras=False,
        export_skins=True, export_influence_nb=4, export_all_influences=False, export_def_bones=True,
        export_morph=False, export_draco_mesh_compression_enable=False, export_apply=False,
        export_animations=True, export_animation_mode='ACTIONS', export_force_sampling=True,
        export_frame_range=False, export_anim_single_armature=True, export_reset_pose_bones=True,
        export_rest_position_armature=True, export_optimize_animation_size=True, export_bake_animation=False,
        export_anim_slide_to_zero=True, export_leaf_bone=False,
        export_copyright="Spider by Quaternius (quaternius.com), CC0 1.0")


# ---------------------------------------------------------------------------------------------------------------
# Previews: the exported file imported again, with the palette nearest-filtered.
PREVIEW_W, PREVIEW_H = 480, 360
PSX_W, PSX_H = 160, 120
FRONT = (0, -1, 0.42)
THREE_QUARTER = (0.75, -0.8, 0.62)


def import_with_skin(glb, png):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.fps = 24
    bpy.ops.import_scene.gltf(filepath=glb, disable_bone_shape=True)
    arm = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
    body = [o for o in bpy.data.objects if o.type == 'MESH'][0]
    img = bpy.data.images.load(png)
    mat = bpy.data.materials.new("skin")
    mat.use_nodes = True
    nt = mat.node_tree
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = img
    tex.interpolation = 'Closest'
    bsdf = nt.nodes["Principled BSDF"]
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 0.35
    nt.nodes.active = tex
    body.data.materials.clear()
    body.data.materials.append(mat)
    sc.view_settings.view_transform = 'Standard'
    sc.world = bpy.data.worlds.new("dark")
    sc.world.color = (0.012, 0.012, 0.016)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    cam.data.lens = 50
    sc.collection.objects.link(cam)
    sc.camera = cam
    for t in arm.animation_data.nla_tracks if arm.animation_data else []:
        t.mute = True
    arm.animation_data_create()
    return sc, arm, body, cam


def clip_of(action_name):
    """The clean clip name for a source or re-imported action's name."""
    base = action_name.split("|")[-1]
    base = CLIP_NAMES.get(base, base)
    return next((c for c in cs.CLIPS if base == c or base.startswith(c + "_") or base.startswith(c + ".")), base)


def clip_actions():
    actions = {clip_of(a.name): a for a in bpy.data.actions}
    assert sorted(actions) == sorted(cs.CLIPS), [a.name for a in bpy.data.actions]
    return actions


def pose_at(sc, arm, action, frame):
    """Poses the armature as glTF does: channels a clip lacks stay at rest (Blender would keep the last clip's)."""
    ad = arm.animation_data
    ad.action = None
    for pb in arm.pose.bones:
        pb.location = (0, 0, 0)
        pb.rotation_quaternion = (1, 0, 0, 0)
        pb.scale = (1, 1, 1)
    ad.action = action
    if action is not None and hasattr(ad, "action_slot") and len(action.slots):
        ad.action_slot = action.slots[0]
    sc.frame_set(frame)
    bpy.context.view_layer.update()


def posed_points(body):
    dg = bpy.context.evaluated_depsgraph_get()
    ev = body.evaluated_get(dg)
    m = ev.to_mesh()
    pts = [body.matrix_world @ v.co for v in m.vertices]
    ev.to_mesh_clear()
    return pts


def frame_on(sc, cam, body, direction, w, h):
    """Aims the camera along `direction` at the posed mesh's bounding box, close enough to fill the frame."""
    pts = posed_points(body)
    lo = Vector([min(p[k] for p in pts) for k in range(3)])
    hi = Vector([max(p[k] for p in pts) for k in range(3)])
    centre = (lo + hi) / 2
    radius = max((p - centre).length for p in pts)
    sc.render.resolution_x = w
    sc.render.resolution_y = h
    half_x = cam.data.angle_x / 2  # sensor fit AUTO: the angle is across the longer side
    half_y = math.atan(math.tan(half_x) * h / w)
    dist = radius / math.sin(min(half_x, half_y)) * 0.87
    d = Vector(direction).normalized()
    cam.location = centre + d * dist
    cam.rotation_euler = (centre - cam.location).to_track_quat('-Z', 'Y').to_euler()


def render(sc, path, w, h):
    sc.render.resolution_x = w
    sc.render.resolution_y = h
    sc.render.resolution_percentage = 100
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)


def shots():
    """(name, clip or None, direction): the rest pose twice, then each clip at its middle frame."""
    out = [("rest_front", None, FRONT), ("rest_threequarter", None, THREE_QUARTER)]
    for clip in cs.CLIPS:
        out.append(("clip_" + clip, clip, THREE_QUARTER))
    return out


def previews(glb, png):
    sc, arm, body, cam = import_with_skin(glb, png)
    actions = clip_actions()
    sc.render.engine = 'BLENDER_WORKBENCH'
    sc.display.shading.light = 'STUDIO'
    sc.display.shading.color_type = 'TEXTURE'
    sc.display.shading.show_specular_highlight = True
    written = []
    for name, clip, direction in shots():
        act = actions[clip] if clip else None
        frame = int(round(sum(act.frame_range) / 2)) if act else 0
        pose_at(sc, arm, act, frame)
        frame_on(sc, cam, body, direction, PREVIEW_W, PREVIEW_H)
        render(sc, os.path.join(OUT, "preview_%s.png" % name), PREVIEW_W, PREVIEW_H)
        psx = os.path.join(OUT, "_psx_%s.png" % name)
        render(sc, psx, PSX_W, PSX_H)
        written.append(psx)
        print("preview", name, "frame", frame)
    contact_sheet(written, os.path.join(OUT, "preview_psx_sheet.png"))
    for p in written:
        os.remove(p)
    torch_sheet(sc, arm, body, cam, actions)


def torch_sheet(sc, arm, body, cam, actions):
    """The same shots at 160x120 in near darkness under one warm torch beside the camera, roughly as FOLDLINE
    lights a passage: how the spider reads in the game. EEVEE; skipped if this Blender can't render it."""
    try:
        sc.render.engine = 'BLENDER_EEVEE_NEXT'
    except TypeError:
        print("torch sheet skipped: no EEVEE")
        return
    sc.world.use_nodes = True
    sc.world.node_tree.nodes["Background"].inputs[0].default_value = (0.003, 0.0032, 0.0035, 1)
    floor_me = bpy.data.meshes.new("floor")
    floor_me.from_pydata([(-40, -40, 0), (40, -40, 0), (40, 40, 0), (-40, 40, 0)], [], [(0, 1, 2, 3)])
    floor = bpy.data.objects.new("floor", floor_me)
    fm = bpy.data.materials.new("floor")
    fm.use_nodes = True
    fm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.09, 0.085, 0.075, 1)
    floor_me.materials.append(fm)
    sc.collection.objects.link(floor)
    light = bpy.data.lights.new("torch", 'SPOT')
    light.energy = 260
    light.spot_size = 1.1
    light.color = (1.0, 0.86, 0.66)
    torch = bpy.data.objects.new("torch", light)
    sc.collection.objects.link(torch)
    written = []
    for name, clip, direction in shots():
        act = actions[clip] if clip else None
        frame = int(round(sum(act.frame_range) / 2)) if act else 0
        pose_at(sc, arm, act, frame)
        frame_on(sc, cam, body, direction, PSX_W, PSX_H)
        torch.location = cam.location + cam.matrix_world.to_3x3() @ Vector((0.35, 0.2, 0))
        torch.rotation_euler = cam.rotation_euler
        path = os.path.join(OUT, "_torch_%s.png" % name)
        try:
            render(sc, path, PSX_W, PSX_H)
        except RuntimeError as e:
            print("torch sheet skipped:", e)
            return
        written.append(path)
    contact_sheet(written, os.path.join(OUT, "preview_psx_torch_sheet.png"))
    for p in written:
        os.remove(p)


def joint_tracks(path, rename=None, scale=1.0):
    """Each clip's joint heads in world space (Blender axes) at its first, middle and last frames."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.fps = 24
    bpy.ops.import_scene.gltf(filepath=path, disable_bone_shape=True)
    arm = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
    for t in arm.animation_data.nla_tracks if arm.animation_data else []:
        t.mute = True
    arm.animation_data_create()
    tracks = {}
    for a in bpy.data.actions:
        clip = clip_of(a.name)
        f0, f1 = (int(round(x)) for x in a.frame_range)
        for f in (f0, (f0 + f1) // 2, f1):
            pose_at(sc, arm, a, f)
            for pb in arm.pose.bones:
                n = (rename or {}).get(pb.name, pb.name)
                tracks[(clip, f - f0, n)] = (arm.matrix_world @ pb.head) * scale
    return tracks


def compare_clips(glb):
    """The exported clips against the source's, scaled: every joint at three frames of every clip. (Both through
    Blender's importer, so the source is read with its held channels stated, as glTF means them.)"""
    src = joint_tracks(SRC_HELD, {"FrontFoot2.R": "FrontFoot.R"}, SCALE)
    out = joint_tracks(glb)
    missing = [k for k in out if k not in src]
    worst = max((v - src[k]).length for k, v in out.items() if k in src)
    print("compare: %d joint samples, worst distance from the scaled source %.6f m, unmatched %d"
          % (len(out), worst, len(missing)))
    assert not missing and worst < 1e-3, (missing[:5], worst)
    return worst


def contact_sheet(paths, out, cols=4, zoom=2):
    import numpy as np
    tiles = []
    for p in paths:
        im = bpy.data.images.load(p)
        w, h = im.size
        a = np.array(im.pixels[:], dtype=np.float32).reshape(h, w, 4)[::-1, :, :3]
        tiles.append(np.repeat(np.repeat(a, zoom, 0), zoom, 1))
        bpy.data.images.remove(im)
    th, tw = tiles[0].shape[:2]
    rows = (len(tiles) + cols - 1) // cols
    sheet = np.zeros((rows * th, cols * tw, 3), dtype=np.float32)
    for i, t in enumerate(tiles):
        r, c = divmod(i, cols)
        sheet[r * th:(r + 1) * th, c * tw:(c + 1) * tw] = t
    rows_out = [[tuple(int(round(min(1, max(0, x)) * 255)) for x in px) for px in row] for row in sheet]
    cs.write_png(out, rows_out)


def main():
    os.makedirs(OUT, exist_ok=True)
    glb = os.path.join(OUT, "spider.glb")
    png = os.path.join(OUT, "spider.png")
    cs.write_png(png, palette_rows())
    print("held channels stated:", cs.hold_channels(SRC, SRC_HELD, skip=DROP_BONES))
    try:
        arm, body = load_source()
        flatten(arm, body)
        fix_bones(arm, body)
        rename_clips(arm)
        parts = paint(body)
        bpy.context.view_layer.update()
        lo = [min((body.matrix_world @ v.co)[k] for v in body.data.vertices) for k in range(3)]
        hi = [max((body.matrix_world @ v.co)[k] for v in body.data.vertices) for k in range(3)]
        print("rest bounds (Blender axes, m): x %.3f..%.3f  y %.3f..%.3f  z %.3f..%.3f" % (lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]))
        export(arm, body, glb)
        summary = cs.verify_glb(glb)
        print("verify:", {k: v for k, v in summary.items() if k != "joint_names"})
        assert not summary["problems"], summary["problems"]
        compare_clips(glb)
    finally:
        if os.path.exists(SRC_HELD):
            os.remove(SRC_HELD)
    previews(glb, png)
    print("parts", dict(parts))
    print("done", glb)


main()
