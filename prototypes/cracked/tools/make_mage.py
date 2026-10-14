# Builds prototypes/cracked/art/player/mage.glb: the Magician character as one
# skinned mesh with seven animations retargeted from Mixamo clips.
#
# Run with Blender 4.4, no window:
#   blender -b --factory-startup --python make_mage.py -- <Unity Assets/Art folder> <output .glb>
#
# What it does:
# - Imports "Models/Player/Magician/Magician 24-07.fbx", joins the 'Magician'
#   and 'Fluid' meshes, bakes their transforms (meters, feet at the origin,
#   front = Blender -Y = glTF +Z) and removes the unweighted *_end bones.
# - Replaces the UVs with a code for what each face is, since no materials are
#   exported. In the GLB: u = (slot + 0.5) / 8, with slot 0 Buckle, 1 Bottoms,
#   2 Skin, 3 Fishbowl, 4 Hood, 5 Shirt, 6 Fluid. v = 0.5, except Fluid where
#   v is the rest height inside the fluid (0 at its lowest vertex, 1 at its
#   highest).
# - Retargets the Mixamo clips in "Animations/Player/Anims" onto the metarig
#   bones: each bone takes the source bone's world-space rotation away from
#   its rest pose; the hips also take the source hips' movement, scaled by
#   the ratio of leg lengths. Feet that would go through the ground are
#   flattened. StrafeRight is StrafeLeft mirrored left-right.
# - Exports one glTF animation per clip, sampled at 30 frames per second.
import bpy, sys, os, json, struct, math
import numpy as np
from mathutils import Matrix, Quaternion, Vector

argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
if len(argv) < 2:
    raise SystemExit("usage: blender -b --factory-startup --python make_mage.py -- <Assets/Art> <out.glb>")
ART, OUT = argv[0], argv[1]
MODEL = os.path.join(ART, "Models", "Player", "Magician", "Magician 24-07.fbx")
ANIMS = os.path.join(ART, "Animations", "Player", "Anims")

SLOTS = ['Buckle', 'Bottoms', 'Skin', 'Fishbowl', 'Hood', 'Shirt']  # Fluid is 6
FLUID_SLOT = 6

# metarig bone -> Mixamo bone (without the 'mixamorig:' prefix). Parents come
# before children. Mixamo's Neck has no bone here: Head takes the head's world
# rotation, which includes the neck's. Fingers are dropped.
BONE_MAP = [
    ('spine', 'Hips'), ('spine.001', 'Spine'), ('spine.002', 'Spine1'), ('spine.003', 'Spine2'),
    ('Head', 'Head'),
    ('shoulder.L', 'LeftShoulder'), ('upper_arm.L', 'LeftArm'), ('forearm.L', 'LeftForeArm'), ('hand.L', 'LeftHand'),
    ('shoulder.R', 'RightShoulder'), ('upper_arm.R', 'RightArm'), ('forearm.R', 'RightForeArm'), ('hand.R', 'RightHand'),
    ('thigh.L', 'LeftUpLeg'), ('shin.L', 'LeftLeg'), ('foot.L', 'LeftFoot'), ('toe.L', 'LeftToeBase'),
    ('thigh.R', 'RightUpLeg'), ('shin.R', 'RightLeg'), ('foot.R', 'RightFoot'), ('toe.R', 'RightToeBase'),
]
ROOT = 'spine'

# name, file, loops, keep the hips' horizontal movement
CLIPS = [
    ('Idle', 'X Bot@Idle.fbx', True, True),
    ('JogForward', 'X Bot@Jog Forward.fbx', True, True),
    ('JogBackward', 'X Bot@Jog Backward.fbx', True, True),
    ('StrafeLeft', 'X Bot@Jog Strafe Left.fbx', True, True),
    ('Throw', 'Ch36_nonPBR@Throw.fbx', False, True),
    ('HandsUp', 'Hands up.fbx', False, False),
]
FPS = 30

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.fps = FPS
scene.frame_start = 0
bpy.ops.import_scene.fbx(filepath=MODEL)
arm = bpy.data.objects['metarig']
mag = bpy.data.objects['Magician']
fluid = bpy.data.objects['Fluid']
bpy.context.view_layer.update()

# ---- mesh: bake transforms, write the coded UVs, join ----
def bake_world(o):
    o.data.transform(o.matrix_world)
    o.matrix_parent_inverse = Matrix.Identity(4)
    o.matrix_basis = Matrix.Identity(4)

def one_uv_layer(me):
    while len(me.uv_layers) > 1:
        me.uv_layers.remove(me.uv_layers[-1])
    if not me.uv_layers:
        me.uv_layers.new(name='UVMap')
    me.uv_layers[0].name = 'UVMap'
    return me.uv_layers[0]

assert [s.name for s in mag.material_slots] == SLOTS, [s.name for s in mag.material_slots]
bake_world(mag)
bake_world(fluid)
bpy.context.view_layer.update()

# glTF stores v flipped (v_gltf = 1 - v_blender), so 1 - value is written here.
uv = one_uv_layer(mag.data)
for p in mag.data.polygons:
    u = (p.material_index + 0.5) / 8.0
    for li in p.loop_indices:
        uv.data[li].uv = (u, 1.0 - 0.5)

zs = [v.co.z for v in fluid.data.vertices]
zmin, zmax = min(zs), max(zs)
print("FLUID world z %.4f .. %.4f" % (zmin, zmax))
uv = one_uv_layer(fluid.data)
for p in fluid.data.polygons:
    for li in p.loop_indices:
        h = (fluid.data.vertices[fluid.data.loops[li].vertex_index].co.z - zmin) / (zmax - zmin)
        uv.data[li].uv = ((FLUID_SLOT + 0.5) / 8.0, 1.0 - h)

for o in (mag, fluid):
    for a in list(o.data.color_attributes):
        o.data.color_attributes.remove(a)

bpy.ops.object.select_all(action='DESELECT')
mag.select_set(True)
fluid.select_set(True)
bpy.context.view_layer.objects.active = mag
bpy.ops.object.join()
mesh = mag
mesh.name = 'Mage'
mesh.data.name = 'Mage'
mesh.data.materials.clear()

# ---- armature: remove unweighted *_end bones ----
used = set()
for v in mesh.data.vertices:
    for g in v.groups:
        if g.weight > 0.0:
            used.add(mesh.vertex_groups[g.group].name)
bpy.ops.object.select_all(action='DESELECT')
bpy.context.view_layer.objects.active = arm
arm.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
removed = []
again = True
while again:  # leaves first, so a chain of unweighted ends goes whole
    again = False
    for eb in list(arm.data.edit_bones):
        if eb.name.endswith('_end') and eb.name not in used and not eb.children:
            removed.append(eb.name)
            arm.data.edit_bones.remove(eb)
            again = True
bpy.ops.object.mode_set(mode='OBJECT')
print("REMOVED bones", removed)
for g in list(mesh.vertex_groups):
    if g.name not in arm.data.bones:
        mesh.vertex_groups.remove(g)

# At most four weights a vertex, summing to one.
unweighted = 0
over = 0
dropped = 0.0
for v in mesh.data.vertices:
    gs = sorted(((g.weight, g.group) for g in v.groups if g.weight > 0.0), reverse=True)
    if not gs:
        unweighted += 1
        continue
    if len(gs) > 4:
        over += 1
    keep = gs[:4]
    total = sum(w for w, _ in keep)
    dropped = max(dropped, sum(w for w, _ in gs) - total)
    for g in list(v.groups):
        mesh.vertex_groups[g.group].remove([v.index])
    for w, gi in keep:
        mesh.vertex_groups[gi].add([v.index], w / total, 'REPLACE')
print("WEIGHTS unweighted verts", unweighted, "verts with more than 4", over, "most weight dropped from one vertex %.4f" % dropped)

# The vertices that mostly follow each foot, for keeping the feet above the ground.
FOOT_VERTS = {'L': [], 'R': []}
for v in mesh.data.vertices:
    top = max(v.groups, key=lambda g: g.weight)
    gname = mesh.vertex_groups[top.group].name
    for side in ('L', 'R'):
        if gname in ('foot.' + side, 'toe.' + side, 'toe_end.' + side):
            FOOT_VERTS[side].append(v.index)
FOOT_VERTS = {k: np.array(v) for k, v in FOOT_VERTS.items()}
print("FOOT verts", {k: len(v) for k, v in FOOT_VERTS.items()})

for pb in arm.pose.bones:
    pb.rotation_mode = 'QUATERNION'
bpy.context.view_layer.update()

rest_q = {b.name: (arm.matrix_world @ b.matrix_local).to_quaternion() for b in arm.data.bones}
# Hip joint to ankle: the hips' movement is scaled by the ratio of leg lengths, so a bent knee
# lowers the hips as far as these shorter legs shorten.
def leg_length(a, bones):
    mw = a.matrix_world
    thigh, shin, foot = (a.data.bones[n] for n in bones)
    return ((mw @ shin.head_local) - (mw @ thigh.head_local)).length + ((mw @ foot.head_local) - (mw @ shin.head_local)).length
target_leg = leg_length(arm, ('thigh.L', 'shin.L', 'foot.L'))

# ---- sampling the Mixamo clips ----
def sample_clip(path):
    """A list of frames: ({mixamo bone: world rotation away from rest}, hips movement from rest), and
    the source's leg length."""
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path)
    new = [o for o in bpy.data.objects if o not in before]
    src = [o for o in new if o.type == 'ARMATURE'][0]
    for o in new:
        if o.type == 'MESH':
            me = o.data
            bpy.data.objects.remove(o)
            bpy.data.meshes.remove(me)
    act = src.animation_data.action
    f0, f1 = int(round(act.frame_range[0])), int(round(act.frame_range[1]))
    hips = [b.name for b in src.data.bones if b.name.endswith('Hips')][0]
    prefix = hips[:-4]
    bpy.context.view_layer.update()
    mw = src.matrix_world.copy()
    names = [s for _, s in BONE_MAP]
    rq = {s: (mw @ src.data.bones[prefix + s].matrix_local).decompose()[1] for s in names}
    rest_hips = (mw @ src.data.bones[hips].matrix_local).translation.copy()
    src_leg = leg_length(src, (prefix + 'LeftUpLeg', prefix + 'LeftLeg', prefix + 'LeftFoot'))
    frames = []
    for f in range(f0, f1 + 1):
        scene.frame_set(f)
        mw = src.matrix_world
        d = {}
        for s in names:
            q = (mw @ src.pose.bones[prefix + s].matrix).decompose()[1]
            d[s] = q @ rq[s].inverted()
        disp = (mw @ src.pose.bones[hips].matrix).translation - rest_hips
        frames.append((d, disp.copy()))
    data = src.data
    bpy.data.objects.remove(src)
    bpy.data.armatures.remove(data)
    bpy.data.actions.remove(act)
    print("SAMPLED %s frames %d..%d hips rest height %.4f leg length %.4f" % (os.path.basename(path), f0, f1, rest_hips.z, src_leg))
    return frames, src_leg

def mirror_q(q):  # a world rotation mirrored across the YZ plane
    return Quaternion((q.w, q.x, -q.y, -q.z))

def mirrored(frames):
    def other(s):
        if s.startswith('Left'): return 'Right' + s[4:]
        if s.startswith('Right'): return 'Left' + s[5:]
        return s
    out = []
    for d, disp in frames:
        out.append(({other(s): mirror_q(q) for s, q in d.items()}, Vector((-disp.x, disp.y, disp.z))))
    return out

def make_action(name, frames, src_leg, loops, keep_horizontal):
    scale = target_leg / src_leg
    print("SCALE %s hips movement x %.4f" % (name, scale))
    n = len(frames)
    disps = [disp * scale for _, disp in frames]
    if not keep_horizontal:
        disps = [Vector((0.0, 0.0, d.z)) for d in disps]
    if loops:
        # Take out any straight-line travel so the loop stays in place, then close it exactly.
        drift = disps[-1] - disps[0]
        print("LOOP %s hips travel first to last (%.5f %.5f %.5f)" % (name, drift.x, drift.y, drift.z))
        disps = [d - Vector((drift.x, drift.y, 0.0)) * (i / (n - 1)) for i, d in enumerate(disps)]
        worst = max(frames[0][0][s].rotation_difference(frames[-1][0][s]).angle for _, s in BONE_MAP)
        print("LOOP %s largest first/last rotation difference %.5f rad" % (name, worst))
        frames = frames[:-1] + [(frames[0][0], frames[0][1])]
        disps[-1] = disps[0].copy()
    if n == 1:  # a single pose: held for one second
        frames = [frames[0], frames[0]]
        disps = [disps[0], disps[0]]
        times = [0, FPS]
    else:
        times = list(range(n))

    act = bpy.data.actions.new(name)
    arm.animation_data_create()
    arm.animation_data.action = act
    prev = {}
    root_rest_inv = rest_q[ROOT].inverted()
    for t, (d, _), disp in zip(times, frames, disps):
        for tname, sname in BONE_MAP:
            b = arm.data.bones[tname]
            pb = arm.pose.bones[tname]
            r = rest_q[tname]
            if b.parent:
                # world = parent's world * (parent rest^-1 * rest) * basis
                parent_delta = d[dict(BONE_MAP)[b.parent.name]]
                q = r.inverted() @ parent_delta.inverted() @ d[sname] @ r
            else:
                q = r.inverted() @ d[sname] @ r
            q.normalize()
            if tname in prev and prev[tname].dot(q) < 0.0:
                q.negate()
            prev[tname] = q.copy()
            pb.rotation_quaternion = q
            pb.keyframe_insert('rotation_quaternion', frame=t, group=tname)
            if tname == ROOT:
                pb.location = root_rest_inv @ disp
                pb.keyframe_insert('location', frame=t, group=tname)
    for fc in (act.fcurves if hasattr(act, 'fcurves') else []):
        for k in fc.keyframe_points:
            k.interpolation = 'LINEAR'

    # The boots are much longer and taller than the source feet, so a foot that pitches the way
    # the source foot does pushes its toe or heel through the ground. Such a foot is blended
    # toward lying flat (keeping its heading) just far enough to bring its lowest vertex back to
    # the ground. Whatever is left (an ankle that is itself too low) lifts the hips for that key.
    def evaluated():
        ev = mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
        me = ev.to_mesh()
        co = np.empty(len(me.vertices) * 3, dtype=np.float32)
        me.vertices.foreach_get('co', co)
        ev.to_mesh_clear()
        return co.reshape(-1, 3)
    smap = dict(BONE_MAP)
    before, blended, lifted, after = [], 0.0, 0.0, []
    for i, t in enumerate(times):
        scene.frame_set(t)
        before.append(float(evaluated()[:, 2].min()))
        d = frames[i][0]
        for side in ('L', 'R'):
            tname = 'foot.' + side
            if float(evaluated()[FOOT_VERTS[side], 2].min()) > -0.0005:
                continue
            pb = arm.pose.bones[tname]
            r = rest_q[tname]
            delta = d[smap[tname]]
            heading = Quaternion((delta.w, 0.0, 0.0, delta.z))
            heading = heading.normalized() if heading.magnitude > 1e-6 else Quaternion()
            world = delta @ r
            flat = heading @ r
            to_local = r.inverted() @ d[smap[arm.data.bones[tname].parent.name]].inverted()
            old = pb.rotation_quaternion.copy()
            toe = arm.pose.bones['toe.' + side]
            toe_old = toe.rotation_quaternion.copy()
            def put(amount):
                q = (to_local @ world.slerp(flat, amount)).normalized()
                if q.dot(old) < 0.0:
                    q.negate()
                pb.rotation_quaternion = q
                pb.keyframe_insert('rotation_quaternion', frame=t, group=tname)
                # the toe's bend goes with the foot's pitch
                toe.rotation_quaternion = toe_old.slerp(Quaternion() if toe_old.w >= 0.0 else -Quaternion(), amount)
                toe.keyframe_insert('rotation_quaternion', frame=t, group=toe.name)
                scene.frame_set(t)
                return float(evaluated()[FOOT_VERTS[side], 2].min())
            lo, hi = 0.0, 1.0
            if put(1.0) > -0.0005:
                for _ in range(7):
                    mid = (lo + hi) * 0.5
                    if put(mid) > -0.0005:
                        hi = mid
                    else:
                        lo = mid
                put(hi)
            blended = max(blended, hi)
        low = float(evaluated()[:, 2].min())
        if low < 0.0:
            lifted = max(lifted, -low)
            disps[i] = disps[i] + Vector((0.0, 0.0, -low))
            root = arm.pose.bones[ROOT]
            root.location = root_rest_inv @ disps[i]
            root.keyframe_insert('location', frame=t, group=ROOT)
            scene.frame_set(t)
            low = float(evaluated()[:, 2].min())
        after.append(low)
    # A clip that never reaches the ground (shorter legs shorten less) is lowered onto it.
    gap = min(after)
    if gap > 0.0005:
        root = arm.pose.bones[ROOT]
        for i, t in enumerate(times):
            root.location = root_rest_inv @ (disps[i] - Vector((0.0, 0.0, gap)))
            root.keyframe_insert('location', frame=t, group=ROOT)
        after = [z - gap for z in after]
        print("GROUND %s lowered by %.4f" % (name, gap))
    print("GROUND %s lowest vertex: before %.4f .. %.4f, after %.4f .. %.4f, most a foot was flattened %.2f, most the hips were lifted %.4f" % (name, min(before), max(before), min(after), max(after), blended, lifted))

    track = arm.animation_data.nla_tracks.new()
    track.name = name
    strip = track.strips.new(name, 0, act)
    strip.name = name
    if hasattr(strip, 'action_slot') and strip.action_slot is None and len(act.slots):
        strip.action_slot = act.slots[0]
    arm.animation_data.action = None
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    print("ACTION %s keys %d..%d = %.4f s" % (name, times[0], times[-1], (times[-1] - times[0]) / FPS))

sampled = {}
for name, fname, loops, keep in CLIPS:
    sampled[name] = sample_clip(os.path.join(ANIMS, fname))
order = ['Idle', 'JogForward', 'JogBackward', 'StrafeLeft', 'StrafeRight', 'Throw', 'HandsUp']
info = {n: (l, k) for n, _, l, k in CLIPS}
info['StrafeRight'] = info['StrafeLeft']
sampled['StrafeRight'] = (mirrored(sampled['StrafeLeft'][0]), sampled['StrafeLeft'][1])
for name in order:
    frames, h = sampled[name]
    make_action(name, frames, h, *info[name])

# ---- export ----
scene.frame_set(0)
bpy.ops.object.select_all(action='DESELECT')
arm.select_set(True)
mesh.select_set(True)
bpy.context.view_layer.objects.active = arm
os.makedirs(os.path.dirname(OUT), exist_ok=True)
bpy.ops.export_scene.gltf(
    filepath=OUT, export_format='GLB', use_selection=True,
    export_yup=True, export_apply=False,
    export_texcoords=True, export_normals=True, export_tangents=False,
    export_vertex_color='NONE', export_materials='NONE',
    export_skins=True, export_all_influences=False, export_influence_nb=4,
    export_def_bones=False, export_leaf_bone=False, export_rest_position_armature=True,
    export_morph=False, export_cameras=False, export_lights=False, export_extras=False,
    export_animations=True, export_animation_mode='ACTIONS', export_nla_strips=True,
    export_force_sampling=True, export_frame_range=False, export_anim_slide_to_zero=True,
    export_optimize_animation_size=True,
    export_optimize_animation_keep_anim_armature=False,
    export_optimize_animation_keep_anim_object=False,
    export_reset_pose_bones=True, export_bake_animation=False,
)

# The exporter marks a clip whose keys never change (HandsUp) as STEP; make every sampler LINEAR.
data = open(OUT, 'rb').read()
jlen = struct.unpack_from('<I', data, 12)[0]
js = json.loads(data[20:20 + jlen])
rest = data[20 + jlen:]
for a in js.get('animations', []):
    for sm in a['samplers']:
        sm['interpolation'] = 'LINEAR'
jbytes = json.dumps(js, separators=(',', ':')).encode('utf-8')
jbytes += b' ' * (-len(jbytes) % 4)
with open(OUT, 'wb') as f:
    f.write(struct.pack('<4sII', b'glTF', 2, 12 + 8 + len(jbytes) + len(rest)))
    f.write(struct.pack('<I4s', len(jbytes), b'JSON'))
    f.write(jbytes)
    f.write(rest)
print("WROTE", OUT, os.path.getsize(OUT), "bytes")
