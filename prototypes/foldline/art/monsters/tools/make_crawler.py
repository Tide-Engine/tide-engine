# Builds FOLDLINE's crawler from Teh_Bucket's "Crawlmaster" (OpenGameArt "Nightmare Monsters", CC0):
#   crawler.glb  the mesh, its skin and four authored clips (Idle, Crawl, Rush, Lunge); 1 unit = 1 m, glTF y up,
#                facing +Z (as Kenney's character.fbx does after import), belly over the origin, lowest point y = 0
#   crawler.png  128 x 128 skin flattened from the source's Unity material
# The source stands upright in a T pose and has no clips. This folds it onto all fours, makes that the rest pose,
# scales it to 2.2 m long, and keys the clips procedurally (two-bone IK keeps feet and blade points planted).
#
# blender -b --factory-startup --python make_crawler.py -- <source dir> <output dir> [--look <scratch dir>]
#   <source dir>  the extracted zip: "crawlmaster deluxe.fbx" and textures/crawlmaster/
#   --look        also renders quick stance checks there (the deliverable previews are preview_crawler.py's)
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import common_crawler as cc  # noqa: E402
from common_crawler import quat  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:]
SRC, OUT = argv[0], argv[1]
LOOK = argv[argv.index("--look") + 1] if "--look" in argv else None
os.makedirs(OUT, exist_ok=True)

FPS = 60
LENGTH = 2.2      # metres, front to back in the rest (crawling) pose

RENAME = {
    "Hips": "Hips", "Spine": "Spine", "Chest": "Chest", "Neck": "Neck", "Head": "Head",
    "Left shoulder": "LeftShoulder", "Left arm": "LeftUpperArm", "Left elbow": "LeftLowerArm", "Left wrist": "LeftHand",
    "Right shoulder": "RightShoulder", "Right arm": "RightUpperArm", "Right elbow": "RightLowerArm",
    "Right wrist": "RightHand",
    "Left leg": "LeftUpperLeg", "Left knee": "LeftLowerLeg", "Left ankle": "LeftFoot",
    "Right leg": "RightUpperLeg", "Right knee": "RightLowerLeg", "Right ankle": "RightFoot",
}
SIDES = (("Left", 1.0), ("Right", -1.0))
# the four limbs as IK chains: upper, mid, end bone, and the way the joint between bends (the stance's and every
# clip's, so a clip at rest reproduces the rest pose exactly)
LIMBS = {
    "LH": ("LeftUpperLeg", "LeftLowerLeg", "LeftFoot", Vector((0.8, 0.0, 0.6)), 1.0),
    "RH": ("RightUpperLeg", "RightLowerLeg", "RightFoot", Vector((-0.8, 0.0, 0.6)), -1.0),
    "LF": ("LeftUpperArm", "LeftLowerArm", "LeftHand", Vector((0.7, -0.3, 0.45)), 1.0),
    "RF": ("RightUpperArm", "RightLowerArm", "RightHand", Vector((-0.7, -0.3, 0.45)), -1.0),
}


def set_mode(obj, mode):
    bpy.context.view_layer.objects.active = obj
    for o in bpy.context.view_layer.objects:
        o.select_set(o == obj)
    if obj.mode != mode:
        bpy.ops.object.mode_set(mode=mode)


# --- import and clean ----------------------------------------------------------------------------------------------
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
bpy.ops.import_scene.fbx(filepath=os.path.join(SRC, "crawlmaster deluxe.fbx"))
scene.render.fps = FPS  # the FBX import sets the file's 24
scene.render.fps_base = 1.0
arm = next(o for o in scene.objects if o.type == 'ARMATURE')
mesh = next(o for o in scene.objects if o.type == 'MESH')
arm.name = arm.data.name = "Crawler"
mesh.name = mesh.data.name = "CrawlerMesh"
assert all(abs(arm.matrix_world[i][j] - (i == j)) < 1e-5 for i in range(4) for j in range(4)), arm.matrix_world
arm.matrix_world = Matrix.Identity(4)

# the six finger bones carry no weights
set_mode(arm, 'EDIT')
for eb in list(arm.data.edit_bones):
    if eb.name not in RENAME:
        arm.data.edit_bones.remove(eb)
set_mode(arm, 'OBJECT')
for old, new in RENAME.items():
    arm.data.bones[old].name = new  # renames the mesh's vertex groups too
assert sorted(g.name for g in mesh.vertex_groups) == sorted(RENAME.values())

# vertices into armature space, mesh object at identity, one uv set, no colours
mesh.data.transform(mesh.matrix_world)
mesh.matrix_parent_inverse = Matrix.Identity(4)
mesh.matrix_basis = Matrix.Identity(4)
for poly in mesh.data.polygons:
    poly.use_smooth = True
if mesh.data.has_custom_normals:
    with bpy.context.temp_override(object=mesh, active_object=mesh):
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
while len(mesh.data.color_attributes):
    mesh.data.color_attributes.remove(mesh.data.color_attributes[0])
assert len(mesh.data.uv_layers) == 1
mesh.data.materials.clear()

rig = cc.Rig(arm)


# --- the crawling stance, made the rest pose ---------------------------------------------------------------------
def stance_spec(r):
    """The T-posed source folded onto all fours: the bulbous body tipped forward to lie level, the stalk of a neck
    reaching ahead, the hind legs sprawled out and back, the blade arms arched like a spider's front legs with their
    points stabbed into the floor. Source units (about 1.8 tall)."""
    rest_hips = r.head["Hips"]
    hips = Vector((0.0, rest_hips.y, 0.19))
    spec = {
        "Hips": ('D', quat((1, 0, 0), 91)),
        "Spine": ('A', quat((1, 0, 0), -3)),
        "Chest": ('A', quat((1, 0, 0), -3)),
        "Neck": ('A', quat((1, 0, 0), -4)),
        "Head": ('A', quat((1, 0, 0), -4)),
    }
    for side, sx in SIDES:
        leg, arm_ = LIMBS[side[0] + "H"], LIMBS[side[0] + "F"]
        spec[side + "Shoulder"] = ('A', quat((1, 0, 0), -25))
        spec[leg[0]] = ('ik', Vector((sx * 0.52, hips.y + 0.42, 0.05)), leg[3], leg[1], leg[2], 0.0)
        spec[leg[2]] = ('aim', Vector((sx * 0.15, -0.35, -1.0)), None, 0.0)
        spec[arm_[0]] = ('ik', Vector((sx * 0.44, hips.y - 0.76, 0.18)), arm_[3], arm_[1], arm_[2],
                         math.radians(-75 * sx))
        spec[arm_[2]] = ('aim', Vector((sx * 0.12, -0.35, -1.0)), None, 0.0)
    return spec, hips - rest_hips


spec, offset = stance_spec(rig)
posed, D, H = rig.evaluate(spec, offset)
for k, (up, mid, end, pole, sx) in LIMBS.items():
    want = spec[up][1]
    reach = (rig.head[mid] - rig.head[up]).length + (rig.head[end] - rig.head[mid]).length
    miss = (H[end] - want).length
    print(f"CRAWLER stance {k} reach {reach:.3f} needs {(want - H[up]).length:.3f} misses by {miss:.3f}")
cc.apply_pose(arm, posed)
bpy.context.view_layer.update()

set_mode(mesh, 'OBJECT')
with bpy.context.temp_override(object=mesh, active_object=mesh, selected_objects=[mesh]):
    bpy.ops.object.modifier_apply(modifier=mesh.modifiers[0].name)
set_mode(arm, 'POSE')
bpy.ops.pose.select_all(action='SELECT')
bpy.ops.pose.armature_apply(selected=False)
set_mode(arm, 'OBJECT')

# --- scale to size, lowest point on the floor, centred over the origin ---------------------------------------------
vs = [v.co for v in mesh.data.vertices]
lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
s = LENGTH / (hi.y - lo.y)
t = Vector((-(lo.x + hi.x) / 2 * s, -(lo.y + hi.y) / 2 * s, -lo.z * s))
mesh.data.transform(Matrix.Translation(t) @ Matrix.Scale(s, 4))
set_mode(arm, 'EDIT')
# connected bones share a point, so work every point out before moving any
ends = {eb.name: (eb.head * s + t, eb.tail * s + t, eb.roll) for eb in arm.data.edit_bones}
for eb in arm.data.edit_bones:
    eb.head, eb.tail, eb.roll = ends[eb.name]
set_mode(arm, 'OBJECT')
mod = mesh.modifiers.new("Armature", 'ARMATURE')
mod.object = arm
size = (hi - lo) * s
print(f"CRAWLER scale {s:.4f} width {size.x:.3f} length {size.y:.3f} height {size.z:.3f}")
rig = cc.Rig(arm)

names = {g.index: g.name for g in mesh.vertex_groups}
lows = {}
for v in mesh.data.vertices:
    g = names[max(v.groups, key=lambda e: e.weight).group]
    lows[g] = min(lows.get(g, 9.0), v.co.z)
print("CRAWLER lowest point by bone", sorted(((round(z, 3), n) for n, z in lows.items()))[:6])

# --- skin --------------------------------------------------------------------------------------------------------
tex_dir = os.path.join(SRC, "textures", "crawlmaster")
png = os.path.join(OUT, "crawler.png")
cc.build_skin(os.path.join(tex_dir, "Coffee_Grains_001_BaseColor.jpg"),
              os.path.join(tex_dir, "Organic_Matter_002_COLOR.jpg"),
              os.path.join(tex_dir, "crawlmaster_detailmask.png"), png)
mesh.data.materials.append(cc.skin_material(png))
mesh.data.calc_loop_triangles()
print("CRAWLER triangles", len(mesh.data.loop_triangles), "bones", len(arm.data.bones))


# --- clips ---------------------------------------------------------------------------------------------------------
# A clip is a function of time giving a parameter set: the body's offset and turn, the spine's bends, and for each
# limb where its end (ankle or wrist) is against its rest place and how far its foot or blade is tilted.
def params(**kw):
    p = dict(x=0.0, y=0.0, z=0.0, yaw=0.0, pitch=0.0, roll=0.0)
    for b in ("spine", "chest", "neck", "head"):
        p.update({b + "_yaw": 0.0, b + "_pitch": 0.0, b + "_roll": 0.0})
    for k in LIMBS:
        p.update({k + "_dx": 0.0, k + "_dy": 0.0, k + "_dz": 0.0, k + "_tilt": 0.0})
    for k, v in kw.items():
        assert k in p, k
        p[k] = v
    return p


def mix(a, b, w):
    return {k: a[k] + (b[k] - a[k]) * w for k in a}


def bend(p, b):
    """A spine bone's turn in rest axes: yaw about up, pitch about the side axis (+ dips the front), roll along it."""
    return quat((0, 0, 1), p[b + "_yaw"]) @ quat((1, 0, 0), p[b + "_pitch"]) @ quat((0, 1, 0), p[b + "_roll"])


def build(p):
    body = quat((0, 0, 1), p["yaw"]) @ quat((0, 1, 0), p["roll"]) @ quat((1, 0, 0), p["pitch"])
    spec = {"Hips": ('D', body)}
    spec["Spine"] = ('A', bend(p, "spine"))
    spec["Chest"] = ('A', bend(p, "chest"))
    spec["Neck"] = ('A', bend(p, "neck"))
    spec["Head"] = ('A', bend(p, "head"))
    for k, (up, mid, end, pole, sx) in LIMBS.items():
        target = rig.head[end] + Vector((p[k + "_dx"] * sx, p[k + "_dy"], p[k + "_dz"]))
        spec[up] = ('ik', target, body @ pole, mid, end, 0.0)
        # + tilt swings the point of a foot or blade backwards about the side axis
        spec[end] = ('aim', quat((1, 0, 0), p[k + "_tilt"]) @ (rig.tail[end] - rig.head[end]), None, 0.0)
    return spec, Vector((p["x"], p["y"], p["z"]))


def smooth(u):
    u = min(1.0, max(0.0, u))
    return u * u * (3 - 2 * u)


def pulse(t, start, rise, hold, fall):
    """0 until `start`, eases to 1 over `rise`, holds, eases back to 0 over `fall`: exactly 0 outside."""
    if t < start:
        return 0.0
    if t < start + rise:
        return smooth((t - start) / rise)
    if t < start + rise + hold:
        return 1.0
    return 1.0 - smooth((t - start - rise - hold) / fall)


def step(phase, duty, stride, lift):
    """One foot through a gait cycle, against the body: planted for `duty` of it, sliding back by `stride` (forward is
    -y), then lifted and swung forward. Returns (dy, dz, how far into the swing 0..1..0)."""
    phase %= 1.0
    if phase < duty:
        return stride * (phase / duty - 0.5), 0.0, 0.0
    u = (phase - duty) / (1.0 - duty)
    e = 0.5 - 0.5 * math.cos(math.pi * u)
    return stride * (0.5 - e), lift * math.sin(math.pi * u), math.sin(math.pi * u)


def gait(p, phase0, offsets, duty, stride, lift_hind, lift_front, center_hind, center_front, tilt_front, tilt_hind,
         tuck):
    for k, off in offsets.items():
        dy, dz, sw = step(phase0 - off, duty, stride, lift_hind if k[1] == "H" else lift_front)
        front = k[1] == "F"
        p[k + "_dy"] = dy + (center_front if front else center_hind)
        p[k + "_dz"] = dz
        p[k + "_dx"] = -tuck * sw
        p[k + "_tilt"] = (tilt_front if front else tilt_hind) * sw
    return p


def idle(t):
    """Two seconds: slow breaths through the bulb of the body, the stalk sniffing about, two sharp twitches of the
    head and a blade lifting to tap the floor."""
    w = 2 * math.pi * t / 2.0
    breath = math.sin(w)
    tw1 = pulse(t, 0.50, 0.05, 0.12, 0.30)
    tw2 = pulse(t, 1.22, 0.04, 0.08, 0.25)
    tap = pulse(t, 0.90, 0.16, 0.10, 0.12)
    flick = pulse(t, 1.55, 0.05, 0.03, 0.15)
    return params(
        z=0.012 * breath, y=0.006 * math.sin(w + 0.8), yaw=1.5 * tw1, pitch=-0.8 * breath,
        spine_pitch=-1.5 * breath, chest_pitch=-1.0 * breath,
        neck_yaw=5.0 * math.sin(w + 0.4) - 6.0 * tw1, neck_pitch=2.0 * math.sin(2 * w) - 5.0 * tw1 + 3.0 * tw2,
        head_yaw=4.0 * math.sin(2 * w + 1.0) + 16.0 * tw1 - 12.0 * tw2, head_pitch=-6.0 * tw1, head_roll=14.0 * tw2,
        LF_dz=0.15 * tap, LF_dy=-0.05 * tap, LF_tilt=-25.0 * tap + 10.0 * pulse(t, 1.12, 0.03, 0.0, 0.08),
        RF_dz=0.035 * flick, RF_tilt=12.0 * flick,
    )


def crawl(t):
    """0.9 s: a lateral-sequence walk (left hind, left blade, right hind, right blade), three limbs down at a time,
    the body swaying and bobbing, the stalk held steady ahead."""
    ph = t / 0.9
    w = 2 * math.pi * ph
    p = params(
        z=-0.014 * math.cos(2 * w), x=0.025 * math.sin(w), yaw=5.0 * math.sin(w), roll=2.5 * math.sin(w + 0.5),
        pitch=1.0 * math.sin(2 * w),
        spine_yaw=-3.0 * math.sin(w), chest_yaw=-2.5 * math.sin(w), neck_yaw=1.0 * math.sin(w),
        neck_pitch=-1.5 * math.cos(2 * w), head_yaw=3.0 * math.sin(w + 1.6), head_pitch=2.0 * math.sin(2 * w + 1.0),
    )
    return gait(p, ph, {"LH": 0.0, "LF": 0.25, "RH": 0.5, "RF": 0.75}, duty=0.72, stride=CRAWL_STRIDE,
                lift_hind=0.14, lift_front=0.22, center_hind=-0.08, center_front=0.0, tilt_front=38.0,
                tilt_hind=-20.0, tuck=0.05)


def rush(t):
    """0.5 s: a scuttling trot (diagonal pairs together), body low and head down, rocking hard."""
    ph = t / 0.5
    w = 2 * math.pi * ph
    p = params(
        z=-0.035 - 0.022 * math.cos(2 * w), x=0.025 * math.sin(w), yaw=6.0 * math.sin(w), roll=4.0 * math.sin(w),
        pitch=3.0 + 1.5 * math.sin(2 * w + 0.5),
        spine_yaw=-4.0 * math.sin(w), chest_yaw=-3.0 * math.sin(w), neck_yaw=1.5 * math.sin(w),
        neck_pitch=7.0 + 2.5 * math.sin(2 * w), head_pitch=4.0, head_yaw=-2.0 * math.sin(w),
    )
    return gait(p, ph, {"LH": 0.0, "RF": 0.0, "RH": 0.5, "LF": 0.5}, duty=0.5, stride=RUSH_STRIDE,
                lift_hind=0.14, lift_front=0.20, center_hind=-0.10, center_front=-0.02, tilt_front=40.0,
                tilt_hind=-25.0, tuck=0.06)


LUNGE_KEYS = [
    # (time, pose): coil back and low with the blades raised, points forward; rear up and surge forward with the
    # blades high; slash them down; settle back to the rest pose
    (0.00, params()),
    (0.20, params(y=0.14, z=-0.06, pitch=-4.0, spine_pitch=-3.0, chest_pitch=-3.0, neck_pitch=-18.0, head_pitch=-10.0,
                  LF_dz=0.30, LF_dy=0.12, LF_dx=0.04, LF_tilt=-105.0, RF_dz=0.30, RF_dy=0.12, RF_dx=0.04,
                  RF_tilt=-105.0, LH_dy=-0.04, RH_dy=-0.04)),
    (0.32, params(y=-0.40, z=0.08, pitch=-12.0, spine_pitch=-2.0, chest_pitch=-2.0, neck_pitch=-6.0, head_pitch=-4.0,
                  LF_dy=-0.62, LF_dz=0.30, LF_dx=-0.06, LF_tilt=-70.0, RF_dy=-0.62, RF_dz=0.30, RF_dx=-0.06,
                  RF_tilt=-70.0, LH_dy=-0.16, LH_dz=0.03, RH_dy=-0.16, RH_dz=0.03)),
    (0.40, params(y=-0.42, z=0.05, pitch=-8.0, neck_pitch=2.0, head_pitch=4.0,
                  LF_dy=-0.66, LF_dz=0.04, LF_dx=-0.06, LF_tilt=-10.0, RF_dy=-0.66, RF_dz=0.04, RF_dx=-0.06,
                  RF_tilt=-10.0, LH_dy=-0.16, RH_dy=-0.16)),
    (0.60, params()),
]


def lunge(t):
    """0.6 s: coils, rears up surging forward, slashes both blades down, and pulls back to the rest pose."""
    for (t0, a), (t1, b) in zip(LUNGE_KEYS, LUNGE_KEYS[1:]):
        if t <= t1:
            return mix(a, b, smooth((t - t0) / (t1 - t0)))
    return LUNGE_KEYS[-1][1]


CRAWL_STRIDE = 0.42
RUSH_STRIDE = 0.60
CLIPS = [("Idle", 2.0, idle, True), ("Crawl", 0.9, crawl, True), ("Rush", 0.5, rush, True),
         ("Lunge", 0.6, lunge, False)]
print(f"CRAWLER ground speed Crawl {CRAWL_STRIDE / (0.72 * 0.9):.2f} m/s, Rush {RUSH_STRIDE / (0.5 * 0.5):.2f} m/s")

ad = arm.animation_data_create()
for name, seconds, clip, loop in CLIPS:
    frames = round(seconds * FPS)
    act = bpy.data.actions.new(name)
    act.use_fake_user = True
    ad.action = act
    previous = {}
    for f in range(frames + 1):
        tt = 0.0 if (loop and f == frames) else f / FPS
        posed, _, _ = rig.evaluate(*build(clip(tt)))
        cc.key_pose(arm, posed, f, previous)
    act.use_frame_range = True
    act.frame_start, act.frame_end = 0, frames
    act.use_cyclic = loop
    ad.action = None
    track = ad.nla_tracks.new()
    track.name = name
    track.strips.new(name, 0, act)
    track.mute = True
    print(f"CRAWLER clip {name} {seconds}s {frames + 1} keys")

for pb in arm.pose.bones:
    pb.rotation_quaternion = (1, 0, 0, 0)
    pb.location = (0, 0, 0)
scene.frame_set(0)

# --- export --------------------------------------------------------------------------------------------------------
glb = os.path.join(OUT, "crawler.glb")
bpy.ops.export_scene.gltf(
    filepath=glb, export_format='GLB', use_selection=False,
    export_texcoords=True, export_normals=True, export_tangents=False, export_attributes=False,
    export_vertex_color='NONE', export_materials='NONE', export_image_format='NONE',
    export_draco_mesh_compression_enable=False, export_yup=True, export_apply=False,
    export_skins=True, export_all_influences=False, export_def_bones=True, export_morph=False,
    export_animations=True, export_animation_mode='ACTIONS', export_force_sampling=True, export_frame_step=1,
    export_anim_slide_to_zero=True, export_reset_pose_bones=True, export_rest_position_armature=True,
    export_optimize_animation_size=False, export_cameras=False, export_lights=False, export_extras=False)
print("CRAWLER wrote", glb, png)

# --- quick looks -----------------------------------------------------------------------------------------------------
if LOOK:
    os.makedirs(LOOK, exist_ok=True)
    cc.setup_render(scene, 512)
    cc.ground_plane(scene)
    cam = cc.ortho_camera(scene, 2.6)
    c = Vector((0, 0, 0.4))
    for name, d in {"front": (0, -1, 0.15), "side": (1, 0, 0.05), "top": (0, 0.001, 1), "tq": (-0.8, -0.8, 0.45),
                    "back": (-0.5, 1, 0.4)}.items():
        cc.aim_camera(cam, c, d)
        scene.render.filepath = os.path.join(LOOK, f"look_{name}.png")
        bpy.ops.render.render(write_still=True)
