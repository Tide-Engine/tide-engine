# GRAVEWAKE's first-person arms: CoinCoin's first-person kit (OpenGameArt, CC-BY 4.0: "Animated Bow",
# "Animated Tomahawk" and "Animated Knife and Firesteel", https://opengameart.org/content/animated-bow,
# /content/animated-tomahawk, /content/animated-knife-and-firesteel) on one skeleton, the arms recoloured as a
# survivor's (gloves, bracers, sleeves), the kit's bow and arrow in flat colours, the kit's clips, and clips
# made here for what the kit has none for (two-handed weapons, polearms, crossbows, staffs, throwing, drinking).
#
#   blender -b --factory-startup --python make_fp.py -- --bow <arc_copie.fbx> --axe <FPS_KIT.fbx>
#       --knife <firesteel_copie.fbx> --out <fp dir> [--preview <dir> [clip,clip]]
#
# Writes into --out:
#   arms.glb   the two arms, skinned
#   bow.glb    the kit's bow and its string, one skinned mesh on the same skeleton (the limbs flex, the string draws)
#   arrow.glb  the nocked arrow, skinned to the same skeleton (drawn only while one is on the string)
#   moves.glb  the clips, on the same skeleton, no meshes
# The skeleton is the bow file's armature with two bones added: `weapon` (the tomahawk file's weapon bone) and
# `knife` (the knife file's). The three files' 42 shared bones have identical rests. A weapon rides `weapon`: its
# grip at the bone, its blade (or a crossbow's barrel) along the bone's local +Z in Blender, its edge toward +X.
#
# The clips made here are a weapon's path, set by hand: where its grip is, where its blade points and where its
# edge faces, in the eye's space (x right, y up, z ahead, metres from the eye, as fp.tide's FpEye places the arms).
# The right hand takes the grip as the kit's hand holds its tomahawk; on a two-handed weapon the left takes it
# further along, mirrored; arms reach by IK (elbows toward poles below and outside), the rest of the body as the
# kit's one-handed idle has it.
#
# Axes: the kit faces -Y in Blender with Z up, so after glTF (Y up) and Tide's importer (x negated) the arms face
# +Z with +Y up and +X to the right: the game's eye space.
import sys, os, math
import bpy
from mathutils import Vector, Matrix, Quaternion

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(HERE, "..", "..", "people", "tools"))
import gw_rig  # noqa: E402  (its prune_glb trims the clip catalog)

argv = sys.argv[sys.argv.index("--") + 1:]


def arg(name, default=None):
    return argv[argv.index(name) + 1] if name in argv else default


BOW, AXE, KNIFE, OUT = arg("--bow"), arg("--axe"), arg("--knife"), arg("--out")
PREVIEW = arg("--preview")
PREVIEW_ONLY = set(argv[argv.index("--preview") + 2].split(",")) if PREVIEW and len(argv) > argv.index("--preview") + 2 else None

# The kit's clips kept, by their name in the kit, and the name each has in the catalog.
KEEP = {
    "Bow_IDLE": "Bow_Idle", "Bow_IDLE(no arrow)": "Bow_Empty", "Bow_AIM": "Bow_Draw", "Bow_AIM_IDLE": "Bow_Hold",
    "Bow_FIRE": "Bow_Loose", "Bow_RELOAD": "Bow_Nock", "Bow_WALK": "Bow_Walk", "Bow_RUN": "Bow_Run",
    "Bow_DISARMED": "Bow_Lower",
    "Tomahawk_IDLE": "One_Idle", "Tomahawk_ATK1(hit)": "One_Swing1", "Tomahawk_ATK2(hit)": "One_Swing2",
    "Tomahawk_WALK": "One_Walk", "Tomahawk_RUN": "One_Run",
    "Knife_ATK": "Stab",
}

# Linear colours: a survivor's gloves, bracers and sleeves, the bow's wood and grip, its string, the arrow.
PAL = {
    "glove": (0.150, 0.090, 0.050), "bracer": (0.230, 0.140, 0.075), "strap": (0.330, 0.210, 0.110),
    "sleeve": (0.300, 0.095, 0.060), "oak": (0.300, 0.165, 0.075), "grip": (0.100, 0.055, 0.032),
    "string": (0.380, 0.280, 0.160), "shaft": (0.238, 0.105, 0.051), "head": (0.376, 0.402, 0.440),
    "fletch": (0.440, 0.042, 0.061),
}

STEP = 2          # The kit's 60 frames a second, kept every other: 30 a second
FPS = 60 // STEP


def material(name):
    m = bpy.data.materials.get(name)
    if m:
        return m
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*PAL[name], 1.0)
    b.inputs["Roughness"].default_value = 0.8
    m.diffuse_color = (*PAL[name], 1.0)
    return m


def short(action_name):
    parts = action_name.split("|")
    return parts[1] if len(parts) > 1 else action_name


def import_fbx(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path)
    return [o for o in bpy.data.objects if o not in before]


def take_bone(path, source, target):
    """A kit file's held-thing bone: its rest (head, tail, roll, parent), and the file's clips with that bone renamed."""
    objects = import_fbx(path)
    kit = [o for o in objects if o.type == "ARMATURE"][0]
    b = kit.data.bones[source]
    rest = (b.head_local.copy(), b.tail_local.copy(), b.parent.name if b.parent else None)
    bpy.context.view_layer.objects.active = kit
    bpy.ops.object.mode_set(mode="EDIT")
    roll = kit.data.edit_bones[source].roll
    bpy.ops.object.mode_set(mode="OBJECT")
    for a in bpy.data.actions:
        if short(a.name) not in KEEP:
            continue
        a.use_fake_user = True
        for fc in a.fcurves:
            if '"%s"' % source in fc.data_path:
                fc.data_path = fc.data_path.replace('"%s"' % source, '"%s"' % target)
        for g in a.groups:
            if g.name == source:
                g.name = target
    for o in objects:
        bpy.data.objects.remove(o, do_unlink=True)
    return rest + (roll,)


bpy.ops.wm.read_factory_settings(use_empty=True)
weapon_rest = take_bone(AXE, "Tomahawk_Bone", "weapon")
knife_rest = take_bone(KNIFE, "Knife_Bone", "knife")

# ---- The bow file: the skeleton, the arms, the bow; the two held-thing bones added as the kits have them
bow_objects = import_fbx(BOW)
arm = [o for o in bow_objects if o.type == "ARMATURE"][0]
for o in bow_objects:
    if o.type == "CAMERA":
        bpy.data.objects.remove(o, do_unlink=True)
bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode="EDIT")
for name, (head, tail, parent, roll) in (("weapon", weapon_rest), ("knife", knife_rest)):
    eb = arm.data.edit_bones.new(name)
    eb.head, eb.tail, eb.roll = head, tail, roll
    if parent:
        eb.parent = arm.data.edit_bones[parent]
bpy.ops.object.mode_set(mode="OBJECT")
scene = bpy.context.scene
if arm.animation_data is None:
    arm.animation_data_create()
BONES = [b.name for b in arm.pose.bones]


def assign(action):
    arm.animation_data.action = action
    if hasattr(action, "slots") and len(action.slots):
        arm.animation_data.action_slot = action.slots[0]


# ---- The kit's clips, sampled onto this skeleton (their actions are bound to the armatures they came in)
CLIPS = {}      # name -> list of {bone: (location, rotation)}, a pose a frame at FPS
for src in [a for a in bpy.data.actions if short(a.name) in KEEP]:
    assign(src)
    f0, f1 = int(src.frame_range[0]), int(src.frame_range[1])
    frames = list(range(f0, f1 + 1, STEP))
    if frames[-1] != f1:
        frames.append(f1)
    poses = []
    for f in frames:
        scene.frame_set(f)
        poses.append({b.name: b.matrix_basis.decompose()[:2] for b in arm.pose.bones})
    CLIPS[KEEP[short(src.name)]] = poses
arm.animation_data.action = None
for a in list(bpy.data.actions):
    bpy.data.actions.remove(a)


def set_pose(pose):
    for b in arm.pose.bones:
        loc, rot = pose[b.name]
        b.matrix_basis = Matrix.Translation(loc) @ rot.to_matrix().to_4x4()
    bpy.context.view_layer.update()


def unscaled(m):
    loc, rot, _ = m.decompose()
    return Matrix.Translation(loc) @ rot.to_matrix().to_4x4()


# ---- Clips made here
def ev(v):
    """The eye's space (x right, y up, z ahead) to Blender's world."""
    return Vector((-v[0], -v[2], v[1]))


# Where the eye is, in the kit's space (fp.tide's FpEye): for what's held in one hand as the kit holds its tomahawk,
# and higher above the arms for the rest, so an arm reaching across to a two-handed grip passes below the eye
EYE = ev((-0.02, 0.52, -0.04))
EYE_LOW = ev((-0.02, 0.67, -0.04))


def weapon_frame(p, blade, edge, eye):
    """The weapon bone's place in the world: its grip at p (from the eye), its blade toward `blade`, its edge `edge`."""
    z = ev(blade).normalized()
    x = ev(edge)
    x = (x - z * x.dot(z)).normalized()
    y = z.cross(x)
    m = Matrix((x, y, z)).transposed().to_4x4()
    m.translation = eye + ev(p)
    return m


# How the kit's right hand holds its weapon, and the mirror that turns it into the left's
set_pose(CLIPS["One_Idle"][0])
hand_r = unscaled(arm.matrix_world @ arm.pose.bones["Hand_R"].matrix)
held = unscaled(arm.matrix_world @ arm.pose.bones["weapon"].matrix)
HAND_TO_WEAPON = hand_r.inverted() @ held



def eye_of(world_point):
    """A point of Blender's world in the eye's space (x right, y up, z ahead, from the eye)."""
    d = world_point - EYE
    return Vector((-d.x, d.z, -d.y))


if PREVIEW:
    for clip, frames in (("One_Idle", (0,)), ("One_Swing1", (0, 8, 15, 20, 30)), ("Bow_Hold", (0,))):
        for f in frames:
            set_pose(CLIPS[clip][f])
            w = arm.matrix_world @ arm.pose.bones["weapon"].matrix
            hl = arm.matrix_world @ arm.pose.bones["Hand_L"].matrix
            sh = arm.matrix_world @ arm.pose.bones["UpperArm_R"].matrix
            print("REFERENCE", clip, f, "grip", [round(v, 2) for v in eye_of(w.translation)], "blade",
                  [round(v, 2) for v in (lambda z: Vector((-z.x, z.z, -z.y)))(w.to_3x3().normalized().col[2])],
                  "left hand", [round(v, 2) for v in eye_of(hl.translation)], "right shoulder", [round(v, 2) for v in eye_of(sh.translation)])
for b in arm.pose.bones:
    b.matrix_basis.identity()
bpy.context.view_layer.update()
MIRROR = Matrix.Diagonal((-1, 1, 1, 1))
rest_r = unscaled(arm.matrix_world @ arm.data.bones["Hand_R"].matrix_local)
rest_l = unscaled(arm.matrix_world @ arm.data.bones["Hand_L"].matrix_local)
LEFT_FROM_MIRROR = (MIRROR @ rest_r @ MIRROR).inverted() @ rest_l
TURN_LEFT = Matrix.Rotation(math.pi, 4, "Z")       # The left hand round the handle from the right's side


ARM_INV = arm.matrix_world.inverted()
ARM_TURN = arm.matrix_world.to_quaternion().to_matrix()
# Which way each elbow goes: down, out and a little back from its shoulder
POLE = {"R": ev((0.6, -0.8, -0.2)), "L": ev((-0.6, -0.8, -0.2))}


def turn_about(pb, pivot, turn):
    """A pose bone turned about a point (the armature's space), its children with it."""
    pb.matrix = Matrix.Translation(pivot) @ turn.to_4x4() @ Matrix.Translation(-pivot) @ pb.matrix
    bpy.context.view_layer.update()


def reach_hand(side, hand_world):
    """Two-bone IK: the elbow placed from the shoulder, the wrist's target and the pole, the upper and lower arm
    turned onto it, the hand then turned as the target is. (The kit's bones don't lie along the limbs, so the
    joints are the bones' heads: shoulder, elbow, wrist.)"""
    upper = arm.pose.bones["UpperArm_" + side]
    lower = arm.pose.bones["LowerArm_" + side]
    hand = arm.pose.bones["Hand_" + side]
    s, e0, w0 = upper.head.copy(), lower.head.copy(), hand.head.copy()
    a, b = (e0 - s).length, (w0 - e0).length
    t = ARM_INV @ hand_world.translation
    to = t - s
    d = min(max(to.length, abs(a - b) + 0.01), a + b - 0.01)
    way = to.normalized()
    pole = ARM_TURN.inverted() @ POLE[side]
    out = (pole - way * pole.dot(way)).normalized()
    cos_a = (a * a + d * d - b * b) / (2 * a * d)
    elbow = s + way * (a * cos_a) + out * (a * math.sqrt(max(0.0, 1 - cos_a * cos_a)))
    turn_about(upper, s, (e0 - s).rotation_difference(elbow - s).to_matrix())
    e1, w1 = lower.head.copy(), hand.head.copy()
    turn_about(lower, e1, (w1 - e1).rotation_difference(t - e1).to_matrix())
    hand.matrix = Matrix.Translation(hand.head.copy()) @ (ARM_TURN.inverted() @ hand_world.to_3x3().normalized()).to_4x4()
    bpy.context.view_layer.update()


def basis_of(pb):
    """A bone's own transform (its matrix_basis) from where it ended up, constraints and all."""
    rest = pb.bone.matrix_local
    if pb.parent:
        return (pb.parent.matrix @ pb.parent.bone.matrix_local.inverted() @ rest).inverted() @ pb.matrix
    return rest.inverted() @ pb.matrix


def mirrored_fingers(pose):
    """The right hand's finger curls on the left hand (the rig mirrors across X)."""
    out = dict(pose)
    for name in BONES:
        if name.endswith("_R") and name.split("_")[0] in ("Thumb", "Index", "Middle", "Ring", "Little"):
            loc, q = pose[name]
            out[name[:-2] + "_L"] = (Vector((-loc.x, loc.y, loc.z)), Quaternion((q.w, q.x, -q.y, -q.z)))
    return out


def catmull(points, times, t):
    """A Catmull-Rom curve through `points` at `times`, at t (vectors or floats)."""
    if t <= times[0]:
        return points[0].copy()
    if t >= times[-1]:
        return points[-1].copy()
    i = max(k for k in range(len(times) - 1) if times[k] <= t)
    p0 = points[max(i - 1, 0)]
    p1, p2 = points[i], points[i + 1]
    p3 = points[min(i + 2, len(points) - 1)]
    u = (t - times[i]) / (times[i + 1] - times[i])
    u2, u3 = u * u, u * u * u
    return 0.5 * ((2 * p1) + (-p0 + p2) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u2 + (-p0 + 3 * p1 - 3 * p2 + p3) * u3)


# How far each arm reaches: shoulder joint to wrist, with a little kept back so the elbow stays bent; the shoulders
# where the kit's one-handed idle has them
def reach_of(side):
    set_pose(CLIPS["One_Idle"][0])
    bones = arm.pose.bones
    a = arm.matrix_world @ bones["UpperArm_" + side].head
    b = arm.matrix_world @ bones["LowerArm_" + side].head
    c = arm.matrix_world @ bones["Hand_" + side].head
    return ((b - a).length + (c - b).length) * 0.95, a


REACH_R, SHOULDER_R = reach_of("R")
REACH_L, SHOULDER_L = reach_of("L")
# The kit's arms are short (its eye is at its shoulders), too short for the left hand to reach a grip across the
# body: holding with both hands, the left shoulder comes forward and in (out of the eye's view: under it and behind
# the near plane's edge), as a shoulder does when a body turns into a two-handed hold
LEFT_FORWARD = ev((0.10, 0.0, 0.12))
SHOULDER_L2 = SHOULDER_L + LEFT_FORWARD


def shoulder_forward():
    pb = arm.pose.bones["UpperArm_L"]
    pb.matrix = Matrix.Translation(ARM_INV.to_3x3() @ LEFT_FORWARD) @ pb.matrix
    bpy.context.view_layer.update()


def left_grip(w, along):
    """Where the left hand takes the weapon: `along` (metres up a handle, or a vector in the weapon's own axes: +Z up
    a handle, +X ahead along a crossbow's barrel), round it from the right hand's other side."""
    offset = Vector((0, 0, along)) if isinstance(along, (int, float)) else Vector(along)
    return w @ Matrix.Translation(offset) @ TURN_LEFT


def within_reach(w, both, along):
    """The weapon's place moved in toward the shoulders until the hands' wrists can get there."""
    for _ in range(12):
        wrist_r = (w @ HAND_TO_WEAPON.inverted()).translation
        moves = []
        over = (wrist_r - SHOULDER_R).length - REACH_R
        if over > 0:
            moves.append((SHOULDER_R - wrist_r).normalized() * over)
        if both:
            grip_l = left_grip(w, along)
            wrist_l = (grip_l @ (MIRROR @ HAND_TO_WEAPON @ MIRROR).inverted() @ LEFT_FROM_MIRROR).translation
            over = (wrist_l - SHOULDER_L2).length - REACH_L
            if over > 0:
                moves.append((SHOULDER_L2 - wrist_l).normalized() * over)
        if not moves:
            break
        w = Matrix.Translation(sum(moves, Vector()) * 0.6) @ w
    return w


def made(keys, length, both=False, reach=0.17, eye=EYE_LOW):
    """A clip from keys (t, grip, blade, edge): poses a frame, the weapon's path between keys a smooth curve."""
    times = [k[0] for k in keys]
    grips = [Vector(k[1]) for k in keys]
    blades = [Vector(k[2]).normalized() for k in keys]
    edges = [Vector(k[3]).normalized() for k in keys]
    rest = CLIPS["One_Idle"][0]
    if both:
        rest = mirrored_fingers(rest)
    poses = []
    paths = []
    n = int(round(length * FPS))
    for f in range(n + 1):
        t = f / FPS
        w = within_reach(weapon_frame(catmull(grips, times, t), catmull(blades, times, t), catmull(edges, times, t), eye), both, reach)
        set_pose(rest)
        if both:
            shoulder_forward()
        want_r = w @ HAND_TO_WEAPON.inverted()
        reach_hand("R", want_r)
        want_l = None
        if both:
            # The other hand further along the handle (toward the blade), or under a crossbow's barrel
            grip_l = left_grip(w, reach)
            want_l = grip_l @ (MIRROR @ HAND_TO_WEAPON @ MIRROR).inverted() @ LEFT_FROM_MIRROR
            reach_hand("L", want_l)
        if f == 0 and PREVIEW:
            got_r = (arm.matrix_world @ arm.pose.bones["Hand_R"].matrix).translation
            miss_l = ((arm.matrix_world @ arm.pose.bones["Hand_L"].matrix).translation - want_l.translation).length if both else 0
            print("MISS right %.3f left %.3f" % ((got_r - want_r.translation).length, miss_l))
        poses.append({pb.name: basis_of(pb).decompose()[:2] for pb in arm.pose.bones})
        paths.append(w)
    return poses, paths


def two_handed(source, along, shift=(-0.12, 0.02, 0.02)):
    """A kit clip with both hands on the weapon: the weapon's path as the kit has it, moved by `shift` (in toward
    the middle, so the other arm needn't cross the eye), the right hand on its grip, the left reaching the handle
    `along` metres further up it."""
    poses, paths = [], []
    for pose in CLIPS[source]:
        set_pose(mirrored_fingers(pose))
        w = Matrix.Translation(ev(shift)) @ unscaled(arm.matrix_world @ arm.pose.bones["weapon"].matrix)
        shoulder_forward()
        reach_hand("R", w @ HAND_TO_WEAPON.inverted())
        grip_l = w @ Matrix.Translation((0, 0, along)) @ TURN_LEFT
        reach_hand("L", grip_l @ (MIRROR @ HAND_TO_WEAPON @ MIRROR).inverted() @ LEFT_FROM_MIRROR)
        poses.append({pb.name: basis_of(pb).decompose()[:2] for pb in arm.pose.bones})
        paths.append(w)
    return poses, paths


# Two-handed blades, axes and hammers: the kit's one-handed swings with the other hand on the handle
MADE = {
    "Two_Idle": two_handed("One_Idle", 0.17),
    "Two_Swing1": two_handed("One_Swing1", 0.17),
    "Two_Swing2": two_handed("One_Swing2", 0.17),
}
# Polearms: held low by the hip and pointed ahead across the body, a thrust; a sweep as the kit's backhand with the
# hands further apart
IDLEP = ((0.16, -0.24, 0.10), (-0.30, 0.14, 0.94), (0.0, 1.0, 0.0))
MADE.update({
    "Pole_Idle": made([(0.0,) + IDLEP, (1.0, (0.16, -0.25, 0.10), (-0.30, 0.15, 0.94), (0.0, 1.0, 0.0)), (2.0,) + IDLEP],
                      2.0, both=True, reach=0.26),
    "Pole_Thrust": made([(0.0,) + IDLEP,
                         (0.15, (0.18, -0.23, 0.00), (-0.28, 0.16, 0.95), (0.0, 1.0, 0.0)),
                         (0.30, (0.10, -0.20, 0.30), (-0.12, 0.06, 0.99), (0.0, 1.0, 0.0)),
                         (0.70,) + IDLEP], 0.7, both=True, reach=0.26),
    "Pole_Sweep": two_handed("One_Swing2", 0.3, (-0.16, -0.04, 0.0)),
})
# Crossbows and the handgonne, held by the stock as a pistol is: the right fist round the grip behind the trigger
# (the weapon bone's +Z up the grip, +X ahead along the barrel), the left hand under the barrel further on; a kick;
# a reload with the nose down
UP = (0.0, 0.97, -0.25)
AHEAD = (0.0, 0.25, 0.97)
IDLEX = ((0.13, -0.15, 0.26), UP, (-0.08, 0.25, 0.96))
UNDER = (0.13, 0.0, -0.02)
MADE.update({
    "X_Idle": made([(0.0,) + IDLEX, (1.0, (0.13, -0.16, 0.26), UP, (-0.08, 0.26, 0.96)), (2.0,) + IDLEX],
                   2.0, both=True, reach=UNDER),
    "X_Fire": made([(0.0,) + IDLEX, (0.05, (0.13, -0.13, 0.21), (0.0, 0.9, -0.43), (-0.08, 0.43, 0.9)), (0.40,) + IDLEX],
                   0.4, both=True, reach=UNDER),
    "X_Reload": made([(0.0,) + IDLEX,
                      (0.30, (0.12, -0.24, 0.26), (0.0, 0.8, 0.6), (0.0, -0.6, 0.8)),
                      (0.75, (0.12, -0.25, 0.25), (0.05, 0.78, 0.62), (0.0, -0.62, 0.78)),
                      (1.20, (0.12, -0.24, 0.26), (0.0, 0.8, 0.6), (0.0, -0.6, 0.8)),
                      (1.50,) + IDLEX], 1.5, both=True, reach=UNDER),
})
# The hand crossbow, in one hand, the same way
IDLEH = ((0.19, -0.17, 0.30), UP, AHEAD)
MADE.update({
    "H_Idle": made([(0.0,) + IDLEH, (1.0, (0.19, -0.18, 0.30), UP, (0.0, 0.26, 0.97)), (2.0,) + IDLEH], 2.0),
    "H_Fire": made([(0.0,) + IDLEH, (0.05, (0.19, -0.15, 0.25), (0.0, 0.9, -0.43), (0.0, 0.43, 0.9)), (0.35,) + IDLEH], 0.35),
    "H_Reload": made([(0.0,) + IDLEH, (0.30, (0.14, -0.14, 0.26), (0.0, 0.7, -0.7), (0.0, 0.7, 0.7)),
                      (0.75, (0.14, -0.14, 0.26), (0.0, 0.7, -0.7), (0.0, 0.7, 0.7)), (1.0,) + IDLEH], 1.0),
})
# Staffs, javelins, a pot or a bell tossed, a brew drunk
IDLES = ((0.22, -0.20, 0.32), (-0.12, 0.97, 0.15), (0.0, 0.0, 1.0))
IDLEJ = ((0.24, 0.04, 0.10), (-0.05, 0.22, 1.0), (0.0, 1.0, 0.0))
MADE.update({
    "Staff_Idle": made([(0.0,) + IDLES, (1.0, (0.22, -0.21, 0.32), (-0.12, 0.97, 0.16), (0.0, 0.0, 1.0)), (2.0,) + IDLES], 2.0),
    "Staff_Cast": made([(0.0,) + IDLES, (0.25, (0.12, -0.10, 0.45), (-0.10, 0.55, 0.83), (0.0, -0.83, 0.55)), (0.60,) + IDLES], 0.6),
    "Jav_Idle": made([(0.0,) + IDLEJ, (1.0, (0.24, 0.03, 0.10), (-0.05, 0.23, 1.0), (0.0, 1.0, 0.0)), (2.0,) + IDLEJ], 2.0),
    "Jav_Throw": made([(0.0,) + IDLEJ,
                       (0.25, (0.30, 0.04, -0.08), (0.0, 0.25, 1.0), (0.0, 1.0, 0.0)),
                       (0.40, (0.12, -0.02, 0.42), (0.0, 0.0, 1.0), (0.0, 1.0, 0.0)),
                       (0.60, (0.02, -0.20, 0.38), (0.0, -0.3, 1.0), (0.0, 1.0, 0.3)),
                       (0.90,) + IDLEJ], 0.9),
    "Toss": made([(0.0, (0.22, -0.30, 0.28), (0.0, 1.0, 0.2), (0.0, 0.0, 1.0)),
                  (0.18, (0.30, 0.06, 0.02), (0.1, 0.6, -0.8), (0.0, 0.8, 0.6)),
                  (0.30, (0.14, 0.00, 0.40), (0.0, 0.5, 0.85), (0.0, 0.85, -0.5)),
                  (0.65, (0.22, -0.40, 0.28), (0.0, 1.0, 0.2), (0.0, 0.0, 1.0))], 0.65, eye=EYE),
    "Drink": made([(0.0, (0.20, -0.32, 0.30), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)),
                   (0.35, (0.05, -0.12, 0.17), (0.0, 1.0, 0.1), (0.0, -0.1, 1.0)),
                   (0.50, (0.04, -0.08, 0.15), (0.0, 0.30, -0.95), (0.0, 0.95, 0.30)),
                   (0.90, (0.04, -0.08, 0.15), (0.0, 0.30, -0.95), (0.0, 0.95, 0.30)),
                   (1.20, (0.20, -0.32, 0.30), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))], 1.2, eye=EYE),
})
for name, (poses, paths) in MADE.items():
    CLIPS[name] = poses
# The stab's clip came without the weapon bone: it stays in the right hand as the one-handed idle holds it
held_basis = CLIPS["One_Idle"][0]["weapon"]
for pose in CLIPS["Stab"]:
    pose["weapon"] = held_basis

# ---- Previews of the made clips from the game's eye, a stick for the weapon
if PREVIEW:
    os.makedirs(PREVIEW, exist_ok=True)
    cam_data = bpy.data.cameras.new("eye")
    cam = bpy.data.objects.new("eye", cam_data)
    scene.collection.objects.link(cam)
    cam.matrix_world = Matrix.Translation(EYE_LOW) @ Matrix.Rotation(math.pi, 4, "Z") @ Matrix.Rotation(math.radians(90), 4, "X")
    cam_data.sensor_fit = "VERTICAL"
    cam_data.angle_y = math.radians(74)
    cam_data.clip_start = 0.01
    scene.camera = cam
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.render.resolution_x, scene.render.resolution_y = 480, 270
    for o in bpy.data.objects:
        if o.type == "MESH" and o.name in ("arc", "corde", "fleche"):
            o.hide_render = True
    bpy.ops.mesh.primitive_cube_add(size=1)
    stick = bpy.context.active_object
    for name, (poses, paths) in MADE.items():
        if PREVIEW_ONLY and name not in PREVIEW_ONLY:
            continue
        for k in range(6):
            i = round(k * (len(poses) - 1) / 5)
            set_pose(poses[i])
            stick.matrix_world = paths[i] @ Matrix.Translation((0, 0, 0.45)) @ Matrix.Diagonal((0.025, 0.06, 0.9, 1))
            scene.render.filepath = os.path.join(PREVIEW, "%s_%d.png" % (name, k))
            bpy.ops.render.render(write_still=True)
        print("PREVIEW", name)
    bpy.data.objects.remove(stick, do_unlink=True)
    bpy.data.objects.remove(cam, do_unlink=True)


# ---- Every clip written as an action of this armature
for name, poses in CLIPS.items():
    act = bpy.data.actions.new(name)
    act.use_fake_user = True
    for bone in BONES:
        prev = None
        quats = []
        for p in poses:
            q = p[bone][1].copy()
            if prev is not None and prev.dot(q) < 0:
                q.negate()
            quats.append(q)
            prev = q
        for i in range(4):
            fc = act.fcurves.new('pose.bones["%s"].rotation_quaternion' % bone, index=i, action_group=bone)
            fc.keyframe_points.add(len(poses))
            fc.keyframe_points.foreach_set("co", [x for k, q in enumerate(quats) for x in (k, q[i])])
            for kp in fc.keyframe_points:
                kp.interpolation = "LINEAR"
            fc.update()
        for i in range(3):
            fc = act.fcurves.new('pose.bones["%s"].location' % bone, index=i, action_group=bone)
            fc.keyframe_points.add(len(poses))
            fc.keyframe_points.foreach_set("co", [x for k, p in enumerate(poses) for x in (k, p[bone][0][i])])
            for kp in fc.keyframe_points:
                kp.interpolation = "LINEAR"
            fc.update()
    # Assigned once, so it's this armature's action for the exporter
    assign(act)
    print("CLIP %-12s %3d keys, %.3f s" % (name, len(poses), (len(poses) - 1) / FPS))
arm.animation_data.action = None
scene.render.fps = FPS
for b in arm.pose.bones:
    b.matrix_basis.identity()

meshes = {o.name: o for o in bpy.data.objects if o.type == "MESH"}


def dominant_bone(ob, vertex):
    best, weight = None, 0.0
    for g in vertex.groups:
        if g.weight > weight:
            weight = g.weight
            best = ob.vertex_groups[g.group].name
    return best


def recolour(ob, choose):
    """Every face takes the material choose(face's vertices) names."""
    ob.data.materials.clear()
    names = []
    for poly in ob.data.polygons:
        name = choose([ob.data.vertices[i] for i in poly.vertices])
        if name not in names:
            names.append(name)
            ob.data.materials.append(material(name))
        poly.material_index = names.index(name)


# The arms: gloved hands, a bracer over each forearm (a strap at the wrist), sleeves above
def arm_part(verts):
    bones = [dominant_bone(meshes["Arms"], v) or "" for v in verts]
    if any(b.startswith(("Thumb", "Index", "Middle", "Ring", "Little", "Hand")) for b in bones):
        return "glove"
    if all(b.startswith(("LowerArm", "twist")) for b in bones):
        return "bracer"
    if any(b.startswith(("LowerArm", "twist")) for b in bones):
        return "strap"
    return "sleeve"


recolour(meshes["Arms"], arm_part)


# The bow: oak limbs, a leather grip where the riser is; the string; the arrow's head, shaft and fletching
def bow_part(verts):
    bones = [dominant_bone(meshes["arc"], v) or "" for v in verts]
    return "grip" if all(b == "arc" for b in bones) else "oak"


recolour(meshes["arc"], bow_part)
recolour(meshes["corde"], lambda verts: "string")
arrow = meshes["fleche"]
ys = [v.co.y for v in arrow.data.vertices]
lo, hi = min(ys), max(ys)
# Along the arrow (its local y): the fletching toward the nock, the head at the other end
recolour(arrow, lambda verts: (lambda t: "fletch" if t > 0.82 else "head" if t < 0.07 else "shaft")(
    (sum(v.co.y for v in verts) / len(verts) - lo) / (hi - lo)))


def export(path, objects):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objects:
        o.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.export_scene.gltf(
        filepath=path, export_format="GLB", use_selection=True,
        export_texcoords=True, export_normals=True, export_tangents=False,
        export_materials="EXPORT", export_vertex_color="NONE",
        export_skins=True, export_influence_nb=4, export_all_influences=False, export_def_bones=False,
        export_morph=False, export_animations=False, export_rest_position_armature=True, export_yup=True,
        export_apply=True, export_cameras=False, export_lights=False, export_extras=False)
    print("WROTE", path, os.path.getsize(path), "bytes")


os.makedirs(OUT, exist_ok=True)
# The meshes named apart from the bones (the bow and the arrow share their bones' names, and a pose binds by name)
meshes["arc"].name = "BowMesh"
meshes["fleche"].name = "ArrowMesh"
meshes["Arms"].name = "ArmsMesh"
export(os.path.join(OUT, "arms.glb"), [arm, meshes["Arms"]])
# The bow and its string as one mesh (one skinned mesh to a file)
bpy.ops.object.select_all(action="DESELECT")
meshes["corde"].select_set(True)
meshes["arc"].select_set(True)
bpy.context.view_layer.objects.active = meshes["arc"]
bpy.ops.object.join()
export(os.path.join(OUT, "bow.glb"), [arm, meshes["arc"]])
export(os.path.join(OUT, "arrow.glb"), [arm, meshes["fleche"]])
# The clips: the armature alone in the scene, with every action (its animation data kept: without it the exporter
# finds none)
for o in [o for o in bpy.data.objects if o.type == "MESH"]:
    bpy.data.objects.remove(o, do_unlink=True)
bpy.ops.export_scene.gltf(
    filepath=os.path.join(OUT, "moves.glb"), export_format="GLB", use_selection=False,
    export_animations=True, export_animation_mode="ACTIONS", export_force_sampling=True,
    export_frame_range=False, export_anim_slide_to_zero=True, export_optimize_animation_size=False,
    export_reset_pose_bones=True, export_rest_position_armature=True, export_yup=True,
    export_def_bones=False, export_skins=True, export_materials="NONE", export_morph=False,
    export_cameras=False, export_lights=False, export_extras=False)
# Rotations and positions only, constant channels cut to their ends
gw_rig.prune_glb(os.path.join(OUT, "moves.glb"), translated=tuple(BONES))
print("WROTE", os.path.join(OUT, "moves.glb"), os.path.getsize(os.path.join(OUT, "moves.glb")), "bytes")
