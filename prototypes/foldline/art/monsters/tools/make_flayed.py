"""Builds FOLDLINE's flayed monster from HorrorGameMaker's CC0 "Horror Game Monster".

Source: https://opengameart.org/content/3d-horror-game-monster (CC0; the README asks, optionally,
for credit to "HorrorGameMaker.com"). Poses.zip has Idle/Walk/Run/Jump.fbx, each the full mesh plus
one clip; Colors.zip has albedo variants.

Run headless only (never opens a window):

  blender -b --factory-startup --python make_flayed.py -- <source x dir> <output dir> [--no-previews]

<source x dir> holds Poses/{Idle,Walk,Run,Jump}.fbx, Poses/UnityTexture/<albedo>.png and
Colors/Black/<albedo>.png. Writes flayed.glb, flayed.png, flayed_dark.png and preview_*.png.

What it does:
- Takes the mesh and the skeleton from Walk.fbx, drops the IK/driver helpers, renames every joint to a
  unique descriptive name (the source repeats names on the left and right sides) by its place in the
  hierarchy and the side it's on, scales the model to 2.0 m tall with its feet on y = 0, applies all
  object transforms and keeps at most 4 influences a vertex.
- Retargets the four source clips onto that one armature: each file's skeleton is renamed the same way,
  and every joint's world location and rotation is copied frame by frame (30 fps), so differing rest
  poses between the files don't matter (each clip's time is stretched under 3% to whole frames).
  Idle, Walk and Run loop and are closed exactly. The source Jump carries its whole leap in the root
  (it starts 1.9 m under the floor); it's kept in place here, rising only enough to end off the ground.
- Authors two clips the source lacks, from the Idle frame whose head faces forward (Idle's own first
  frame looks over the right shoulder): Lunge (0.6 s: coil, spring forward onto
  the left foot with both arms reaching and the jaw open, claws closing; ends in the grab, played once)
  and Twitch (2 s loop: still, then head jerks with a tremor, a jaw drop, a shoulder hitch, a finger flick).
- Exports one .glb (no materials, no images, no compression; Blender -Y forward becomes glTF +Z, the same
  as Kenney's character.fbx), the 128x128 skins (linear-light box filter, a little contrast, 32 levels a
  channel) and previews: rest pose front/three-quarter/side, each clip's middle frame, a contact sheet
  per clip (three-quarter and side, fixed camera, so root motion and the floor show) and the dark skin.
"""
import bpy
import math
import os
import sys

import numpy as np
from mathutils import Matrix, Quaternion, Vector

HEIGHT = 2.0          # metres, top of the head
FPS = 30              # every clip is sampled at this rate
SKIN = 128            # texture size
CONTRAST = 1.2
LEVELS = 32

ALBEDO = "test_StingrayPBS1SG_AlbedoTransparency.png"

# Source joint base names (Blender adds .001 to repeats) -> part name. Sides come from the joint's x.
PARTS = {
    "joint1": "hips", "joint2": "spine_1", "joint3": "spine_2", "joint4": "neck",
    "joint15": "head", "joint16": "head_end",
    "joint19": "mouth_upper_root", "joint20": "mouth_upper", "Jaw": "jaw_root", "joint18": "jaw",
    "Clav_L": "clavicle", "Clav_R": "clavicle",
    "joint14": "upperarm", "joint11": "forearm", "joint12": "hand", "joint13": "palm",
    "joint5": "thigh", "joint64": "thigh", "joint6": "shin", "joint7": "foot", "joint8": "toes", "joint9": "toes_end",
}
CENTRE = {"joint1", "joint2", "joint3", "joint4", "joint15", "joint16", "joint19", "joint20", "Jaw", "joint18"}
FINGER_ROOTS = {"joint21": "index", "joint26": "middle", "joint27": "ring", "joint28": "pinky",
                "Thumb_1": "thumb", "joint30": "thumb"}
FINGER_SEGMENT = {"joint21": 1, "joint26": 1, "joint27": 1, "joint28": 1, "Thumb_1": 1, "joint30": 1,
                  "joint22": 2, "joint23": 3, "joint24": 4, "joint25": 5, "joint29": 2,
                  "Thumb_2": 2, "joint31": 2, "Thumb3": 3, "joint32": 3}


def log(*a):
    print("[flayed]", *a, flush=True)


# ----------------------------------------------------------------------------------------------
# Import and naming

def import_fbx(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path)
    new = [o for o in bpy.data.objects if o not in before]
    scene = bpy.context.scene
    fps = scene.render.fps / scene.render.fps_base
    arm = max((o for o in new if o.type == 'ARMATURE'), key=lambda o: len(o.data.bones))
    return new, arm, fps


def canonical_names(arm):
    """Old bone name -> unique descriptive name, from the base name, finger chain and side."""
    mw = arm.matrix_world
    names = {}
    for b in arm.data.bones:
        base = b.name.split(".")[0]
        x = (mw @ b.head_local).x
        side = "L" if x > 0.005 else "R" if x < -0.005 else ""
        if base in FINGER_SEGMENT:
            root = b
            while root is not None and root.name.split(".")[0] not in FINGER_ROOTS:
                root = root.parent
            if root is None or not side:
                raise RuntimeError("finger joint %s has no finger root or side" % b.name)
            names[b.name] = "%s_%s_%d" % (FINGER_ROOTS[root.name.split(".")[0]], side, FINGER_SEGMENT[base])
        elif base in PARTS:
            if base in CENTRE:
                names[b.name] = PARTS[base]
            elif not side:
                raise RuntimeError("joint %s should be on one side" % b.name)
            else:
                names[b.name] = PARTS[base] + "_" + side
        else:
            raise RuntimeError("unknown source joint %s" % b.name)
    if len(set(names.values())) != len(names):
        dup = sorted(n for n in set(names.values()) if list(names.values()).count(n) > 1)
        raise RuntimeError("names not unique: %s" % dup)
    return names


def rename_bones(arm, names):
    for i, b in enumerate(list(arm.data.bones)):
        b.name = "tmp_rename_%03d" % i
    temp = {"tmp_rename_%03d" % i: old for i, old in enumerate(names)}
    for b in arm.data.bones:
        b.name = names[temp[b.name]]


def delete_objects(objs):
    for o in objs:
        bpy.data.objects.remove(o, do_unlink=True)


def bones_parent_first(arm):
    out = []
    def walk(b):
        out.append(b)
        for c in sorted(b.children, key=lambda c: c.name):
            walk(c)
    for b in sorted((b for b in arm.data.bones if b.parent is None), key=lambda b: b.name):
        walk(b)
    return out


# ----------------------------------------------------------------------------------------------
# Clip data: per bone, per frame location (3) and rotation quaternion (4) of the pose basis.

class Clip:
    def __init__(self, name, frames, bones):
        self.name = name
        self.frames = frames                      # key count; duration = (frames - 1) / FPS
        self.loc = {b: np.zeros((frames, 3)) for b in bones}
        self.rot = {b: np.tile([1.0, 0, 0, 0], (frames, 1)) for b in bones}


def basis_from_armature(arm, order, desired):
    """desired: bone name -> armature-space Matrix (rigid). Returns name -> (loc, quat) basis."""
    out = {}
    for b in order:
        rest = b.matrix_local
        if b.parent is None:
            basis = rest.inverted() @ desired[b.name]
        else:
            prest = b.parent.matrix_local
            basis = (prest.inverted() @ rest).inverted() @ desired[b.parent.name].inverted() @ desired[b.name]
        loc, rot, _ = basis.decompose()
        out[b.name] = (loc, rot)
    return out


def retarget(arm, order, src, src_fps, name, loop):
    """Copy src's joint world transforms onto arm (same canonical names) as a Clip at FPS."""
    scene = bpy.context.scene
    act = src.animation_data.action
    f0, f1 = act.frame_range
    span = (f1 - f0) / src_fps
    frames = int(round(span * FPS)) + 1
    clip = Clip(name, frames, [b.name for b in order])
    pbs = {b.name: src.pose.bones[b.name] for b in order}
    for i in range(frames):
        f = f0 + (f1 - f0) * i / (frames - 1)
        fi = int(math.floor(f))
        scene.frame_set(fi, subframe=f - fi)
        desired = {}
        for b in order:
            m = src.matrix_world @ pbs[b.name].matrix
            loc, rot, _ = m.decompose()
            desired[b.name] = Matrix.Translation(loc) @ rot.to_matrix().to_4x4()
        basis = basis_from_armature(arm, order, desired)
        for n, (loc, rot) in basis.items():
            clip.loc[n][i] = loc
            clip.rot[n][i] = rot
        if i in (0, frames // 2):
            # Check: the posed target's joints sit where the source's are.
            for b in order:
                pb = arm.pose.bones[b.name]
                pb.rotation_mode = 'QUATERNION'
                pb.location, pb.rotation_quaternion = basis[b.name]
                pb.scale = (1, 1, 1)
            bpy.context.view_layer.update()
            worst = 0.0
            for b in order:
                m = arm.matrix_world @ arm.pose.bones[b.name].matrix
                d = desired[b.name]
                ang = m.to_quaternion().rotation_difference(d.to_quaternion()).angle
                worst = max(worst, (m.translation - d.translation).length, min(ang, 2 * math.pi - ang) * 0.1)
            if worst > 1e-3:
                raise RuntimeError("%s frame %d: retargeted joints are off by %.4f" % (name, i, worst))
    log("retargeted %s: %d keys, %.3f s (source %.3f s at %g fps)" % (name, frames, (frames - 1) / FPS, span, src_fps))
    return clip


def fix_quaternions(clip, loop):
    """Keep each bone's quaternions on one hemisphere, and close loops exactly."""
    for n, q in clip.rot.items():
        q /= np.linalg.norm(q, axis=1, keepdims=True)
        for i in range(1, len(q)):
            if np.dot(q[i], q[i - 1]) < 0:
                q[i] = -q[i]
        if loop:
            first = q[0] if np.dot(q[0], q[-2]) >= 0 else -q[0]
            # Spread the closing error over the clip so the last key equals the first.
            err = first - q[-1]
            t = np.linspace(0, 1, len(q))[:, None]
            q += err * t
            q /= np.linalg.norm(q, axis=1, keepdims=True)
            q[-1] = first
            if np.dot(q[-1], q[-2]) < 0:
                raise RuntimeError("loop closing flipped %s" % n)
    if loop:
        for n, l in clip.loc.items():
            err = l[0] - l[-1]
            t = np.linspace(0, 1, len(l))[:, None]
            l += err * t
            l[-1] = l[0]


# ----------------------------------------------------------------------------------------------
# Writing clips as actions in NLA tracks (what the glTF exporter's ACTIONS mode exports)

def action_fcurves(action):
    try:
        return list(action.fcurves)
    except Exception:
        out = []
        for layer in action.layers:
            for strip in layer.strips:
                for slot in action.slots:
                    bag = strip.channelbag(slot)
                    if bag:
                        out.extend(bag.fcurves)
        return out


def write_action(arm, clip):
    ad = arm.animation_data or arm.animation_data_create()
    act = bpy.data.actions.new(clip.name)
    ad.action = act
    for pb in arm.pose.bones:
        pb.rotation_mode = 'QUATERNION'
        pb.location = clip.loc[pb.name][0]
        pb.rotation_quaternion = clip.rot[pb.name][0]
        pb.scale = (1, 1, 1)
        for path in ("location", "rotation_quaternion", "scale"):
            pb.keyframe_insert(path, frame=0, group=pb.name)
    frames = np.arange(clip.frames, dtype=np.float64)
    for fc in action_fcurves(act):
        bone = fc.data_path.split('"')[1]
        prop = fc.data_path.rsplit(".", 1)[1]
        if prop == "location":
            vals = clip.loc[bone][:, fc.array_index]
        elif prop == "rotation_quaternion":
            vals = clip.rot[bone][:, fc.array_index]
        else:
            vals = np.ones(clip.frames)
        kp = fc.keyframe_points
        kp.add(clip.frames - 1)
        co = np.empty(clip.frames * 2)
        co[0::2] = frames
        co[1::2] = vals
        kp.foreach_set("co", co.astype(np.float32))
        kp.foreach_set("interpolation", np.full(clip.frames, 1, dtype=np.int32))  # LINEAR
        fc.update()
    ad.action = None
    track = ad.nla_tracks.new()
    track.name = clip.name
    strip = track.strips.new(clip.name, 0, act)
    strip.name = clip.name
    return act


# ----------------------------------------------------------------------------------------------
# Textures: linear-light box filter down to SKIN, a little contrast, LEVELS levels a channel.

def srgb_to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def make_skin(src_path, out_path, darken=1.0):
    img = bpy.data.images.load(src_path)
    w, h = img.size
    ch = img.channels
    px = np.empty(w * h * ch, np.float32)
    img.pixels.foreach_get(px)
    px = px.reshape(h, w, ch)[:, :, :3].astype(np.float64)
    bpy.data.images.remove(img)
    f = w // SKIN
    small = srgb_to_linear(px).reshape(SKIN, f, SKIN, f, 3).mean(axis=(1, 3)) * darken
    s = linear_to_srgb(small)
    luma = (s * [0.299, 0.587, 0.114]).sum(axis=2).mean()
    s = np.clip((s - luma) * CONTRAST + luma, 0, 1)
    s = np.round(s * (LEVELS - 1)) / (LEVELS - 1)
    out = bpy.data.images.new(os.path.basename(out_path), SKIN, SKIN, alpha=False)
    rgba = np.concatenate([s, np.ones((SKIN, SKIN, 1))], axis=2).astype(np.float32)
    out.pixels.foreach_set(rgba.ravel())
    out.filepath_raw = out_path
    out.file_format = 'PNG'
    out.save()
    log("wrote", out_path)
    return out


# ----------------------------------------------------------------------------------------------
# Posing: forward kinematics over basis dicts (bone -> (loc, quat)), world-space aims and two-bone IK

AX = Vector((1, 0, 0))   # the model's left
AY = Vector((0, 1, 0))   # the model's back (it faces -Y in Blender, which is +Z in glTF)
AZ = Vector((0, 0, 1))   # up


def q(axis, deg):
    return Quaternion(axis, math.radians(deg))


def turn(pitch=0.0, roll=0.0, yaw=0.0):
    """A rotation in rest axes. pitch tips the top forward (about +X); roll tips the top toward the
    model's left (about +Y); yaw turns the front toward the model's left (about +Z)."""
    return q(AZ, yaw) @ q(AY, roll) @ q(AX, pitch)


class Rig:
    def __init__(self, order):
        self.order = order
        self.bones = {b.name: b for b in order}
        self.local = {b.name: (b.parent.matrix_local.inverted() @ b.matrix_local) if b.parent else b.matrix_local.copy()
                      for b in order}

    def basis_of(self, clip, i):
        return {n: (Vector(clip.loc[n][i]), Quaternion(clip.rot[n][i])) for n in clip.loc}

    def fk(self, basis):
        w = {}
        for b in self.order:
            loc, rot = basis[b.name]
            m = Matrix.Translation(loc) @ rot.to_matrix().to_4x4()
            w[b.name] = (w[b.parent.name] if b.parent else Matrix.Identity(4)) @ self.local[b.name] @ m
        return w

    def turn(self, basis, deltas):
        """Rotate bones in rest axes as carried by their parents (so a child follows its parent)."""
        for n, d in deltas.items():
            rq = self.bones[n].matrix_local.to_quaternion()
            loc, rot = basis[n]
            basis[n] = (loc, (rq.inverted() @ d @ rq) @ rot)

    def place_root(self, basis, name, world_pos=None, world_turn=None):
        b = self.bones[name]
        rest = b.matrix_local
        loc, rot = basis[name]
        if world_turn is not None:
            w = self.fk(basis)[name]
            rot = rest.to_quaternion().inverted() @ world_turn @ w.to_quaternion()
        if world_pos is not None:
            loc = rest.to_quaternion().inverted() @ (Vector(world_pos) - rest.translation)
        basis[name] = (loc, rot)

    def set_world_rot(self, basis, w, name, qw):
        b = self.bones[name]
        pw = w[b.parent.name] if b.parent else Matrix.Identity(4)
        frame = (pw @ self.local[name]).to_quaternion()
        loc, _ = basis[name]
        basis[name] = (loc, frame.inverted() @ qw)
        w.update(self.fk(basis))

    def aim(self, basis, w, name, child, direction, twist=0.0):
        cur = (w[child].translation - w[name].translation).normalized()
        d = Vector(direction).normalized()
        qw = cur.rotation_difference(d) @ w[name].to_quaternion()
        if twist:
            qw = Quaternion(d, math.radians(twist)) @ qw
        self.set_world_rot(basis, w, name, qw)

    def leg_ik(self, basis, w, thigh, shin, foot, target, pole, foot_rot=None):
        h = w[thigh].translation
        k = w[shin].translation
        a = (k - h).length
        b = (w[foot].translation - k).length
        to = Vector(target) - h
        d = min(max(to.length, abs(a - b) + 1e-4), a + b - 1e-4)
        u = to.normalized()
        v = (pole - u * pole.dot(u)).normalized()
        cos_a = (a * a + d * d - b * b) / (2 * a * d)
        knee = h + (u * cos_a + v * math.sqrt(max(0.0, 1 - cos_a * cos_a))) * a
        self.aim(basis, w, thigh, shin, knee - h)
        self.aim(basis, w, shin, foot, (h + u * d) - w[shin].translation)
        if foot_rot is not None:
            self.set_world_rot(basis, w, foot, foot_rot)


def curve(keys, t):
    """keys: [(time, value), ...] with values float or Vector; smoothstep between keys. A key may add
    a third item: 'snap' (fast ease out, for jerks) or 'linear'."""
    if t <= keys[0][0]:
        return keys[0][1]
    for k in range(len(keys) - 1):
        ta, va = keys[k][0], keys[k][1]
        tb, vb = keys[k + 1][0], keys[k + 1][1]
        if t <= tb:
            x = (t - ta) / (tb - ta) if tb > ta else 1.0
            ease = keys[k + 1][2] if len(keys[k + 1]) > 2 else "smooth"
            if ease == "snap":
                x = 1 - (1 - x) ** 3
            elif ease == "smooth":
                x = x * x * (3 - 2 * x)
            if isinstance(va, Vector):
                return va.lerp(vb, x)
            return va + (vb - va) * x
    return keys[-1][1]


def store(clip, i, basis):
    for n, (loc, rot) in basis.items():
        clip.loc[n][i] = loc
        clip.rot[n][i] = rot


def fix_jump(rig, jump, idle):
    """The source Jump carries the whole leap in its root: it starts with the body 1.9 m under the floor
    and 1.3 m behind, and ends in the air. Keep it in place: the hips stay where Idle's are, at the height
    that puts the feet on the floor at the first frame, so the game moves the monster along its leap."""
    feet = ("foot_L", "foot_R", "toes_L", "toes_R", "toes_end_L", "toes_end_R")
    wi = rig.fk(rig.basis_of(idle, 0))
    wj = rig.fk(rig.basis_of(jump, 0))
    low_idle = min(wi[n].translation.z for n in feet)
    low_jump = min(wj[n].translation.z for n in feet)
    src_z = [rig.fk(rig.basis_of(jump, i))["hips"].translation.z for i in range(jump.frames)]
    hips = wj["hips"].translation
    target = Vector((wi["hips"].translation.x, wi["hips"].translation.y, hips.z + low_idle - low_jump))
    lows = []
    for i in range(jump.frames):
        basis = rig.basis_of(jump, i)
        rig.place_root(basis, "hips", world_pos=target)
        w = rig.fk(basis)
        lows.append(min(w[n].translation.z for n in feet))
    # The source's own rise, as a 0..1 profile, scaled so the last frame clears the floor by AIRBORNE.
    airborne = 0.22
    end_lift = max(0.0, low_idle + airborne - lows[-1])
    for i in range(jump.frames):
        frac = (src_z[i] - src_z[0]) / (src_z[-1] - src_z[0])
        lift = max(end_lift * frac, low_idle - lows[i])
        basis = rig.basis_of(jump, i)
        rig.place_root(basis, "hips", world_pos=target + Vector((0, 0, lift)))
        store(jump, i, basis)
    log("Jump kept in place: hips at %.3f m, rising %.3f m to end off the ground" % (target.z, end_lift))


# ----------------------------------------------------------------------------------------------
# Previews (Workbench, the 128 px skin nearest-filtered)

def setup_preview(mesh, skin_img):
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.color_type = 'TEXTURE'
    scene.display.shading.show_cavity = False
    scene.render.film_transparent = False
    world = bpy.data.worlds.new("preview") if not scene.world else scene.world
    scene.world = world
    world.color = (0.06, 0.06, 0.07)
    scene.render.resolution_x = 320
    scene.render.resolution_y = 480
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.view_settings.view_transform = 'Standard'
    mat = bpy.data.materials.new("flayed_preview")
    mat.use_nodes = True
    nt = mat.node_tree
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = skin_img
    tex.interpolation = 'Closest'
    bsdf = nt.nodes["Principled BSDF"]
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    nt.nodes.active = tex
    mesh.data.materials.clear()
    mesh.data.materials.append(mat)
    cam_data = bpy.data.cameras.new("preview")
    cam_data.type = 'ORTHO'
    cam_data.sensor_fit = 'VERTICAL'
    cam = bpy.data.objects.new("preview_camera", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    return mat, cam


def frame_camera(cam, mesh, yaw_deg, size=None):
    dg = bpy.context.evaluated_depsgraph_get()
    ev = mesh.evaluated_get(dg)
    pts = [ev.matrix_world @ v.co for v in ev.data.vertices]
    mn = Vector([min(p[i] for p in pts) for i in range(3)])
    mx = Vector([max(p[i] for p in pts) for i in range(3)])
    center = (mn + mx) / 2
    extent = max(mx.z - mn.z, (mx.x - mn.x) * 480 / 320, (mx.y - mn.y) * 480 / 320)
    cam.data.ortho_scale = size or max(extent * 1.12, 2.3)
    yaw = math.radians(yaw_deg)
    # in front is -Y; yaw turns the camera toward the model's left (+X)
    d = Vector((math.sin(yaw), -math.cos(yaw), 0.0))
    cam.location = center + d * 6.0 + Vector((0, 0, 0.6))
    look = (center - cam.location).normalized()
    cam.rotation_euler = look.to_track_quat('-Z', 'Y').to_euler()
    cam.data.clip_end = 50


def fixed_camera(cam, yaw_deg, size, forward=-0.25):
    cam.data.ortho_scale = size
    yaw = math.radians(yaw_deg)
    center = Vector((0, forward, size / 2 - 0.15))
    cam.location = center + Vector((math.sin(yaw), -math.cos(yaw), 0.0)) * 6.0 + Vector((0, 0, 0.6))
    cam.rotation_euler = (center - cam.location).normalized().to_track_quat('-Z', 'Y').to_euler()


def add_floor():
    bpy.ops.mesh.primitive_plane_add(size=6, location=(0, 0, 0))
    floor = bpy.context.active_object
    floor.name = "preview_floor"
    img = bpy.data.images.new("floor", 2, 2, alpha=False)
    img.pixels.foreach_set(np.tile([0.12, 0.11, 0.1, 1.0], 4).astype(np.float32))
    mat = bpy.data.materials.new("floor")
    mat.use_nodes = True
    tex = mat.node_tree.nodes.new("ShaderNodeTexImage")
    tex.image = img
    mat.node_tree.links.new(tex.outputs["Color"], mat.node_tree.nodes["Principled BSDF"].inputs["Base Color"])
    mat.node_tree.nodes.active = tex
    floor.data.materials.append(mat)
    return floor


def render_to(path):
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    log("rendered", path)


def tile_images(paths, cols, out_path):
    imgs = [bpy.data.images.load(p) for p in paths]
    w, h = imgs[0].size
    rows = (len(imgs) + cols - 1) // cols
    sheet = np.zeros((rows * h, cols * w, 4), np.float32)
    for i, img in enumerate(imgs):
        px = np.empty(w * h * 4, np.float32)
        img.pixels.foreach_get(px)
        r = rows - 1 - i // cols   # Blender pixel rows start at the bottom
        c = i % cols
        sheet[r * h:(r + 1) * h, c * w:(c + 1) * w] = px.reshape(h, w, 4)
        bpy.data.images.remove(img)
    out = bpy.data.images.new(os.path.basename(out_path), cols * w, rows * h, alpha=True)
    out.pixels.foreach_set(sheet.ravel())
    out.filepath_raw = out_path
    out.file_format = 'PNG'
    out.save()
    bpy.data.images.remove(out)
    for p in paths:
        os.remove(p)
    log("wrote", out_path)


# ----------------------------------------------------------------------------------------------

def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    src_dir, out_dir = argv[0], argv[1]
    previews = "--no-previews" not in argv
    os.makedirs(out_dir, exist_ok=True)
    poses = os.path.join(src_dir, "Poses")

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene

    # --- Mesh and skeleton from Walk.fbx -----------------------------------------------------
    new, arm, _ = import_fbx(os.path.join(poses, "Walk.fbx"))
    meshes = [o for o in new if o.type == 'MESH' and any(m.type == 'ARMATURE' and m.object == arm for m in o.modifiers)]
    if len(meshes) != 1:
        raise RuntimeError("expected one skinned mesh, found %d" % len(meshes))
    mesh = meshes[0]
    delete_objects([o for o in new if o not in (arm, mesh)])
    arm.animation_data_clear()
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    source_world = arm.matrix_world.copy()

    names = canonical_names(arm)
    rename_bones(arm, names)
    missing = [g.name for g in mesh.vertex_groups if g.name not in arm.data.bones]
    if missing:
        raise RuntimeError("vertex groups not renamed with their bones: %s" % missing)

    # Scale to HEIGHT with the feet on the floor, then apply every object transform.
    mw = mesh.matrix_world.copy()
    zs = [(mw @ v.co).z for v in mesh.data.vertices]
    k = HEIGHT / (max(zs) - min(zs))
    fix = Matrix.Translation((0, 0, -min(zs) * k)) @ Matrix.Scale(k, 4)
    mesh.parent = None
    mesh.matrix_world = fix @ mw
    arm.matrix_world = fix @ source_world
    bpy.ops.object.select_all(action='DESELECT')
    for o in (arm, mesh):
        o.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    mesh.parent = arm
    mesh.matrix_parent_inverse = Matrix.Identity(4)
    arm.name = "flayed_rig"
    arm.data.name = "flayed_rig"
    mesh.name = "flayed_body"
    mesh.data.name = "flayed_body"
    target_fix = fix

    # Mesh cleanup: one uv set, 4 influences, normalized.
    while len(mesh.data.uv_layers) > 1:
        mesh.data.uv_layers.remove(mesh.data.uv_layers[-1])
    mesh.data.uv_layers[0].name = "UVMap"
    bpy.ops.object.select_all(action='DESELECT')
    mesh.select_set(True)
    bpy.context.view_layer.objects.active = mesh
    bpy.ops.object.vertex_group_limit_total(group_select_mode='ALL', limit=4)
    bpy.ops.object.vertex_group_normalize_all(group_select_mode='ALL', lock_active=False)
    for mod in mesh.modifiers:
        if mod.type == 'ARMATURE':
            mod.object = arm
    order = bones_parent_first(arm)

    # --- Clips -------------------------------------------------------------------------------
    clips = []
    for name, loop in (("Idle", True), ("Walk", True), ("Run", True), ("Jump", False)):
        new, src, fps = import_fbx(os.path.join(poses, name + ".fbx"))
        src_names = canonical_names(src)
        rename_bones(src, src_names)
        src.matrix_world = target_fix @ src.matrix_world
        scene.render.fps, scene.render.fps_base = FPS, 1.0
        clip = retarget(arm, order, src, fps, name, loop)
        fix_quaternions(clip, loop)
        clips.append(clip)
        acts = {o.animation_data.action for o in new if o.animation_data and o.animation_data.action}
        delete_objects(new)
        for a in acts:
            bpy.data.actions.remove(a)
    scene.render.fps, scene.render.fps_base = FPS, 1.0

    rig = Rig(order)
    by_name = {c.name: c for c in clips}
    fix_jump(rig, by_name["Jump"], by_name["Idle"])
    start = neutral_frame(rig, by_name["Idle"])
    lunge = make_lunge(rig, by_name["Idle"], start)
    fix_quaternions(lunge, False)
    twitch = make_twitch(rig, by_name["Idle"], start)
    fix_quaternions(twitch, True)
    clips += [lunge, twitch]

    for a in list(bpy.data.actions):
        bpy.data.actions.remove(a)
    for c in clips:
        write_action(arm, c)
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)

    # --- Skins -------------------------------------------------------------------------------
    skin = make_skin(os.path.join(poses, "UnityTexture", ALBEDO), os.path.join(out_dir, "flayed.png"))
    make_skin(os.path.join(src_dir, "Colors", "Black", ALBEDO), os.path.join(out_dir, "flayed_dark.png"))

    # --- Export ------------------------------------------------------------------------------
    for o in list(bpy.data.objects):
        if o not in (arm, mesh):
            bpy.data.objects.remove(o, do_unlink=True)
    bpy.ops.object.select_all(action='DESELECT')
    arm.select_set(True)
    mesh.select_set(True)
    bpy.context.view_layer.objects.active = arm
    glb = os.path.join(out_dir, "flayed.glb")
    bpy.ops.export_scene.gltf(
        filepath=glb, export_format='GLB', use_selection=True,
        export_yup=True, export_apply=False, export_texcoords=True, export_normals=True,
        export_tangents=False, export_materials='NONE', export_image_format='NONE',
        export_vertex_color='NONE', export_attributes=False, export_extras=False,
        export_skins=True, export_influence_nb=4, export_all_influences=False, export_def_bones=False,
        export_morph=False, export_animations=True, export_animation_mode='ACTIONS',
        export_nla_strips=True, export_force_sampling=True, export_frame_step=1,
        export_anim_slide_to_zero=True, export_reset_pose_bones=True, export_rest_position_armature=True,
        export_optimize_animation_size=True, export_anim_single_armature=True,
        export_draco_mesh_compression_enable=False, export_cameras=False, export_lights=False)
    log("wrote", glb)

    # --- Previews ----------------------------------------------------------------------------
    if not previews:
        return
    mesh.data.uv_layers[0].active = True
    mat, cam = setup_preview(mesh, skin)
    add_floor()
    ad = arm.animation_data
    for tr in ad.nla_tracks:
        tr.mute = True
    arm.data.pose_position = 'REST'
    for yaw, tag in ((0, "front"), (40, "threequarter"), (90, "side")):
        frame_camera(cam, mesh, yaw, 2.4)
        render_to(os.path.join(out_dir, "preview_rest_%s.png" % tag))
    arm.data.pose_position = 'POSE'
    dark = bpy.data.images.load(os.path.join(out_dir, "flayed_dark.png"))
    for c in clips:
        act = bpy.data.actions[c.name]
        ad.action = act
        if ad.action_slot is None and len(act.slots):
            ad.action_slot = act.slots[0]
        # how low the mesh goes over the clip (the floor is y = 0 in the game)
        lows = []
        for f in range(c.frames):
            scene.frame_set(f)
            ev = mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
            lows.append(min((ev.matrix_world @ v.co).z for v in ev.data.vertices))
        log("%s: lowest point %.3f m at frame %d (first frame %.3f, last %.3f)" % (
            c.name, min(lows), lows.index(min(lows)), lows[0], lows[-1]))
        mid = (c.frames - 1) // 2
        scene.frame_set(mid)
        size, forward = (3.2, -0.5) if c.name == "Lunge" else (3.0, -0.25) if c.name == "Jump" else (2.6, -0.25)
        fixed_camera(cam, 40, size, forward)
        render_to(os.path.join(out_dir, "preview_%s.png" % c.name))
        # a contact sheet across the clip, from the side and the three-quarter view
        shots = []
        for view in (40, 90):
            for j in range(6):
                f = int(round(j * (c.frames - 1) / 5))
                scene.frame_set(f)
                fixed_camera(cam, view, 3.2 if c.name in ("Lunge", "Jump") else 2.6, -0.5 if c.name == "Lunge" else -0.25)
                p = os.path.join(out_dir, "_sheet_%s_%d_%d.png" % (c.name, view, j))
                render_to(p)
                shots.append(p)
        tile_images(shots, 6, os.path.join(out_dir, "preview_%s_sheet.png" % c.name))
    ad.action = None
    # the dark skin on the idle mid frame
    mat.node_tree.nodes["Image Texture"].image = dark
    ad.action = bpy.data.actions["Idle"]
    if ad.action_slot is None:
        ad.action_slot = ad.action.slots[0]
    scene.frame_set(0)
    frame_camera(cam, mesh, 40, 2.4)
    render_to(os.path.join(out_dir, "preview_dark.png"))


FINGERS = ("index", "middle", "ring", "pinky")


def finger_curl(side, deg, spread=0.0):
    """Curl every finger joint of a hand by deg (fingers point along +-X in the rest T-pose, so the
    hinge is the Y axis), spreading the first joints apart by spread degrees."""
    sign = 1.0 if side == "L" else -1.0
    out = {}
    for fi, f in enumerate(FINGERS):
        segs = 2 if f == "pinky" else 5
        for s in range(1, segs + 1):
            d = q(AY, sign * deg * (0.5 if s == 1 else 1.0))
            if s == 1 and spread:
                d = q(AZ, sign * spread * (fi - 1.5)) @ d
            out["%s_%s_%d" % (f, side, s)] = d
    for s in (1, 2, 3):
        out["thumb_%s_%d" % (side, s)] = q(AY, sign * deg * 0.3)
    return out


def neutral_frame(rig, idle):
    """Idle turns its head from side to side, about 50 degrees each way (its first frame looks over its
    right shoulder); the authored clips start from the frame where the head is turned least from its
    rest pose, so it faces forward."""
    rest = rig.bones["head"].matrix_local.to_quaternion()
    best = None
    for i in range(idle.frames):
        w = rig.fk(rig.basis_of(idle, i))
        turned = w["head"].to_quaternion().rotation_difference(rest).angle
        turned = min(turned, 2 * math.pi - turned)
        if best is None or turned < best[0]:
            best = (turned, i)
    log("authored clips start from Idle's frame %d (%.2f s), the head %.1f degrees from rest" % (
        best[1], best[1] / FPS, math.degrees(best[0])))
    return best[1]


def make_lunge(rig, idle, start):
    """A fast lunge: coil, spring forward onto the left foot with both arms reaching and the jaw open,
    then the claws close and pull in. 0.6 s; it ends in the grab (it doesn't return to Idle)."""
    duration = 0.6
    frames = int(round(duration * FPS)) + 1
    clip = Clip("Lunge", frames, [b.name for b in rig.order])
    base = rig.basis_of(idle, start)
    w0 = rig.fk(base)
    hips0 = w0["hips"].translation.copy()
    ank = {s: w0["foot_" + s].translation.copy() for s in "LR"}
    toe = {s: w0["toes_" + s].translation.copy() for s in "LR"}
    foot_rot = {s: w0["foot_" + s].to_quaternion() for s in "LR"}
    pole = Vector((0, -1, 0))

    hip_off = [(0.0, Vector((0, 0, 0))), (0.12, Vector((0, 0.07, -0.12))), (0.30, Vector((0, -0.46, -0.27))),
               (0.42, Vector((0, -0.52, -0.29))), (0.60, Vector((0, -0.44, -0.25)))]
    hip_pitch = [(0.0, 0.0), (0.12, 10.0), (0.30, 34.0), (0.42, 36.0), (0.60, 30.0)]
    spine = [(0.0, 0.0), (0.12, 12.0), (0.30, 24.0), (0.60, 20.0)]
    neck = [(0.0, 0.0), (0.12, 10.0), (0.30, -26.0), (0.60, -22.0)]
    head = [(0.0, 0.0), (0.12, 8.0), (0.30, -26.0), (0.60, -20.0)]
    jaw = [(0.0, 0.0), (0.12, 4.0), (0.30, 28.0), (0.45, 34.0), (0.60, 24.0)]
    curl = [(0.0, 0.0), (0.12, 10.0), (0.30, -12.0), (0.40, 55.0, "snap"), (0.60, 65.0)]
    spread = [(0.0, 0.0), (0.30, 9.0), (0.40, 2.0), (0.60, 0.0)]
    arm_pull = [(0.0, 0.0), (0.12, 1.0), (0.24, 0.0)]          # draw back during the coil
    arm_reach = [(0.0, 0.0), (0.12, 0.0), (0.28, 1.0), (0.38, 1.0), (0.48, 0.0)]
    arm_grab = [(0.0, 0.0), (0.30, 0.0), (0.40, 1.0), (0.48, 1.0), (0.60, 0.0)]
    arm_hold = [(0.0, 0.0), (0.40, 0.0), (0.48, 1.0), (0.60, 1.0)]
    step = [(0.0, 0.0), (0.10, 0.0), (0.30, 0.58), (0.60, 0.56)]
    lift = [(0.0, 0.0), (0.10, 0.0), (0.20, 0.13), (0.30, 0.0), (0.60, 0.0)]
    heel = [(0.0, 0.0), (0.12, 0.0), (0.30, 32.0), (0.60, 26.0)]
    reach_dirs = {
        # (upper arm, forearm, hand) world directions per phase, for the left side (x mirrored for R)
        "reach": (Vector((0.30, -1.0, 0.12)), Vector((0.12, -1.0, 0.06)), Vector((0.05, -1.0, -0.05))),
        "grab": (Vector((0.16, -1.0, 0.05)), Vector((-0.25, -1.0, -0.05)), Vector((-0.35, -1.0, -0.25))),
        "hold": (Vector((0.18, -0.9, -0.25)), Vector((-0.45, -0.6, 0.05)), Vector((-0.5, -0.5, -0.3))),
    }

    for i in range(frames):
        t = i / FPS
        basis = {n: (l.copy(), r.copy()) for n, (l, r) in base.items()}
        deltas = {
            "spine_1": turn(pitch=curve(spine, t) * 0.6), "spine_2": turn(pitch=curve(spine, t) * 0.4),
            "neck": turn(pitch=curve(neck, t)), "head": turn(pitch=curve(head, t)),
            "jaw_root": turn(pitch=curve(jaw, t)),
        }
        pull = curve(arm_pull, t)
        for s in "LR":
            deltas["upperarm_" + s] = turn(pitch=28 * pull)
            deltas["forearm_" + s] = turn(pitch=-35 * pull)
            deltas.update(finger_curl(s, curve(curl, t), curve(spread, t)))
        rig.turn(basis, deltas)
        rig.place_root(basis, "hips", world_pos=hips0 + curve(hip_off, t), world_turn=turn(pitch=curve(hip_pitch, t)))
        w = rig.fk(basis)
        # arms: blend the idle direction toward the reach / grab / hold directions
        weights = {"reach": curve(arm_reach, t), "grab": curve(arm_grab, t), "hold": curve(arm_hold, t)}
        for s, sign in (("L", 1.0), ("R", -1.0)):
            chain = (("upperarm_" + s, "forearm_" + s), ("forearm_" + s, "hand_" + s), ("hand_" + s, "palm_" + s))
            for j, (bone, child) in enumerate(chain):
                cur = (w[child].translation - w[bone].translation).normalized()
                d = cur.copy()
                for phase, wt in weights.items():
                    if wt > 0:
                        target = reach_dirs[phase][j].copy()
                        target.x *= sign
                        d = d.lerp(target.normalized(), wt).normalized()
                if (d - cur).length > 1e-6:
                    rig.aim(basis, w, bone, child, d)
        # legs: the right foot stays planted and its heel lifts; the left one steps forward
        h = math.radians(curve(heel, t))
        lift_r = Quaternion(AX, h)
        ank_r = toe["R"] + lift_r @ (ank["R"] - toe["R"])
        rig.leg_ik(basis, w, "thigh_R", "shin_R", "foot_R", ank_r, pole, lift_r @ foot_rot["R"])
        rig.set_world_rot(basis, w, "toes_R", w0["toes_R"].to_quaternion())   # toes stay flat
        ank_l = ank["L"] + Vector((0, -curve(step, t), curve(lift, t)))
        rig.leg_ik(basis, w, "thigh_L", "shin_L", "foot_L", ank_l, pole + Vector((0.2, 0, 0)), foot_rot["L"])
        store(clip, i, basis)
    return clip


def make_twitch(rig, idle, start):
    """Idle variant, 2 s loop: still and breathing, then the head jerks to one side, snaps back and up,
    wrenches round with the jaw dropping, a shoulder hitches and the fingers flick, then it settles."""
    duration = 2.0
    frames = int(round(duration * FPS)) + 1
    clip = Clip("Twitch", frames, [b.name for b in rig.order])
    base = rig.basis_of(idle, start)
    # head (pitch, roll, yaw), shared with the neck; snaps take 2-3 frames
    head_roll = [(0.0, 0.0), (0.30, 0.0), (0.37, -34.0, "snap"), (0.68, -30.0), (0.72, 16.0, "snap"),
                 (1.05, 12.0), (1.09, 22.0, "snap"), (1.36, 18.0), (1.42, -6.0, "snap"), (1.75, 0.0), (2.0, 0.0)]
    head_pitch = [(0.0, 0.0), (0.30, 0.0), (0.37, 14.0, "snap"), (0.68, 12.0), (0.72, 34.0, "snap"),
                  (1.05, 30.0), (1.09, 18.0, "snap"), (1.36, 16.0), (1.42, -6.0, "snap"), (1.75, 0.0), (2.0, 0.0)]
    head_yaw = [(0.0, 0.0), (0.30, 0.0), (0.37, -10.0, "snap"), (0.68, -8.0), (0.72, 6.0, "snap"),
                (1.05, 4.0), (1.09, 38.0, "snap"), (1.36, 34.0), (1.42, -4.0, "snap"), (1.75, 0.0), (2.0, 0.0)]
    jaw = [(0.0, 0.0), (1.05, 0.0), (1.09, 26.0, "snap"), (1.36, 20.0), (1.46, 0.0), (2.0, 0.0)]
    shrug = [(0.0, 0.0), (0.70, 0.0), (0.73, 14.0, "snap"), (0.95, 10.0), (1.10, 0.0), (2.0, 0.0)]
    flick = [(0.0, 0.0), (1.38, 0.0), (1.41, 40.0, "snap"), (1.46, -10.0, "snap"), (1.52, 30.0, "snap"),
             (1.70, 0.0), (2.0, 0.0)]
    elbow = [(0.0, 0.0), (0.88, 0.0), (0.91, -22.0, "snap"), (1.10, -15.0), (1.30, 0.0), (2.0, 0.0)]
    holds = [(0.37, 0.68), (0.72, 1.05), (1.09, 1.36)]
    for i in range(frames):
        t = i / FPS
        basis = {n: (l.copy(), r.copy()) for n, (l, r) in base.items()}
        tremor = 0.0
        for a, b in holds:
            if a < t < b:
                tremor = 2.5 * math.sin((t - a) * 2 * math.pi * 9.0) * math.sin(math.pi * (t - a) / (b - a))
        breath = 1.2 * math.sin(2 * math.pi * t / duration)
        p, r, y = curve(head_pitch, t), curve(head_roll, t), curve(head_yaw, t)
        deltas = {
            "spine_2": turn(pitch=breath),
            "neck": turn(pitch=p * 0.35, roll=r * 0.35, yaw=y * 0.35),
            "head": turn(pitch=p * 0.65 + tremor, roll=r * 0.65 - tremor * 0.6, yaw=y * 0.65),
            "jaw_root": turn(pitch=curve(jaw, t)),
            "clavicle_R": turn(roll=curve(shrug, t)),
            "forearm_L": turn(pitch=curve(elbow, t)),
        }
        deltas.update(finger_curl("R", curve(flick, t)))
        rig.turn(basis, deltas)
        store(clip, i, basis)
    return clip


if __name__ == "__main__":
    main()
