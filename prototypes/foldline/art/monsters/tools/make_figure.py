# FOLDLINE "figure": a faceless mannequin / tall black figure from Quaternius' "Man"
# (Animated Men Pack, CC0, https://quaternius.com/packs/animatedmen.html, poly.pizza/m/HMnuH5geEG).
#
# Regenerates figure.glb, figure_mannequin.png, figure_shadow.png, the preview renders and stats.json
# from the source .glb. Run headless only:
#
#   blender -b --factory-startup --python make_figure.py -- --src <pp-man-1216.glb> --out <dir> [--no-previews]
#
# What it does to the source:
# - drops the eyeballs, ears and eyebrows (separate shells) and rounds the head onto a smooth egg (faceless);
# - drops the IK pole-target bones (unweighted helpers), the importer's leaf "_end" empties and bone-shape mesh;
# - scales it to 1.8 m standing on y = 0 (it was 4.85 units, under a x100 armature node);
# - replaces the six material colours with one UV set into a 64x64 palette skin: four tones a face picks by its
#   rest normal (lit top / plain / underside) or by a seeded "grime" roll, each with a plain block and a seam strip;
#   faces that straddle a mannequin joint (neck, shoulder, elbow, wrist, waist, hip, knee, ankle) sample the strip
#   by their corners' weight between the two parts, so a thin seam line runs where the parts meet. The layout is
#   mirrored top to bottom, so a flipped V convention samples the same colours;
# - renames the clips (HumanArmature|Man_Walk -> Walk; Man_Standing, which stands up from sitting, -> StandUp) and
#   adds two: Stand (static mannequin pose) and Creep (slow, stiff, lurching walk, 1.625 s loop);
# - keys every bone in every source clip: a property a source clip leaves unkeyed holds the source's own default
#   pose (its nodes' TRS, an idle stance), which is what glTF and Tide give an unkeyed property. Blender's rest is the
#   bind (T) pose instead, so without this the source Idle (and Stand, made from it) would put the feet ~5 cm from
#   the shins and turn the head, hands and torso away from the source's pose.
import sys, os, math, json, random, struct, tempfile, zlib
import bpy, bmesh
from mathutils import Vector, Matrix, Quaternion

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def arg(name, default=None):
    return argv[argv.index(name) + 1] if name in argv else default


SRC = arg("--src")
OUT = arg("--out")
PREVIEWS = "--no-previews" not in argv
HEIGHT = float(arg("--height", "1.8"))
TEX = 64
FPS = 24
os.makedirs(OUT, exist_ok=True)

# ---------------------------------------------------------------- import and clean up


def source_default_pose(src):
    """The source's authored default pose (its nodes' own TRS, which a clip's unkeyed properties keep in glTF and in
    Tide) as Blender pose-bone values: import a copy with no animations, so no clip is evaluated over it."""
    with open(src, 'rb') as fh:
        data = fh.read()
    n = struct.unpack_from('<I', data, 12)[0]
    assert data[16:20] == b'JSON'
    g = json.loads(data[20:20 + n].decode('utf-8'))
    g.pop('animations', None)
    js = json.dumps(g, separators=(',', ':')).encode('utf-8')
    js += b' ' * (-len(js) % 4)
    tail = data[20 + n:]
    glb = b'glTF' + struct.pack('<II', 2, 20 + len(js) + len(tail)) + struct.pack('<I', len(js)) + b'JSON' + js + tail
    d = tempfile.mkdtemp()
    path = os.path.join(d, 'default_pose.glb')
    with open(path, 'wb') as fh:
        fh.write(glb)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=path)
    a = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    pose = {pb.name: (tuple(pb.rotation_quaternion), tuple(pb.location)) for pb in a.pose.bones}
    os.remove(path)
    os.rmdir(d)
    return pose


DEFAULT_POSE = source_default_pose(SRC)
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.fps = FPS
scene.render.fps_base = 1.0
bpy.ops.import_scene.gltf(filepath=SRC)

arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
body = next(o for o in bpy.data.objects if o.type == 'MESH' and o.parent == arm)
for pb in arm.pose.bones:
    pb.custom_shape = None
arm_world = arm.matrix_world.copy()
arm.parent = None
arm.matrix_world = arm_world
for o in list(bpy.data.objects):
    if o not in (arm, body):
        bpy.data.objects.remove(o, do_unlink=True)
for m in list(bpy.data.meshes):
    if m.users == 0:
        bpy.data.meshes.remove(m)

# every source clip keys every bone: what a clip leaves unkeyed holds the source's default pose, as it did there
FILLED = {}
for a in bpy.data.actions:
    f0, f1 = a.frame_range
    have = {(fc.data_path, fc.array_index) for fc in a.fcurves}
    filled = set()
    for b in arm.data.bones:
        q, l = DEFAULT_POSE[b.name]
        for prop, vals in (('rotation_quaternion', q), ('location', l)):
            path = 'pose.bones["%s"].%s' % (b.name, prop)
            for i, v in enumerate(vals):
                if (path, i) in have:
                    continue
                fc = a.fcurves.new(path, index=i, action_group=b.name)
                fc.keyframe_points.add(2)
                for k, f in zip(fc.keyframe_points, (f0, f1)):
                    k.co = (f, v)
                    k.interpolation = 'LINEAR'
                fc.update()
                filled.add((b.name, prop))
    FILLED[a.name] = filled
arm.name = 'FigureArmature'
arm.data.name = 'FigureArmature'
body.name = 'Figure'
body.data.name = 'Figure'


def activate(objs, active):
    bpy.context.view_layer.update()
    for o in bpy.data.objects:
        if o.name in bpy.context.view_layer.objects:
            o.select_set(False)
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = active


def apply_scale(factor):
    """Bake the armature's scale (times factor) into its rest data and the mesh's whole world matrix into its
    vertices, so both objects end at identity and mesh coordinates are world coordinates."""
    mw = body.matrix_world.copy()
    body.parent = None
    arm.scale = arm.scale * factor
    activate([arm], arm)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    body.data.transform(Matrix.Scale(factor, 4) @ mw)
    body.matrix_world = Matrix.Identity(4)
    body.parent = arm
    body.matrix_parent_inverse = Matrix.Identity(4)


def scale_actions(factor):
    for a in bpy.data.actions:
        for fc in a.fcurves:
            if fc.data_path.endswith('.location'):
                for k in fc.keyframe_points:
                    k.co[1] *= factor
                    k.handle_left[1] *= factor
                    k.handle_right[1] *= factor


# the armature node carries x100: bake it so 1 unit of the rest data is 1 world unit
s0 = arm.scale[0]
apply_scale(1.0)
scale_actions(s0)
print('after unit bake: armature', arm.matrix_world, 'mesh', body.matrix_world, 'mesh z', min(v.co.z for v in body.data.vertices), max(v.co.z for v in body.data.vertices))

# IK pole targets: unweighted helpers nothing in the clips needs once they are baked
DROP_BONES = [b.name for b in arm.data.bones if b.name.startswith('PoleTarget')]
gidx = {g.name: g.index for g in body.vertex_groups}
for name in DROP_BONES:
    if name in gidx:
        gi = gidx[name]
        assert all(g.weight < 1e-6 for v in body.data.vertices for g in v.groups if g.group == gi), name
        body.vertex_groups.remove(body.vertex_groups[name])
activate([arm], arm)
bpy.ops.object.mode_set(mode='EDIT')
for name in DROP_BONES:
    arm.data.edit_bones.remove(arm.data.edit_bones[name])
bpy.ops.object.mode_set(mode='OBJECT')
for a in bpy.data.actions:
    for fc in list(a.fcurves):
        if any('"%s"' % n in fc.data_path for n in DROP_BONES):
            a.fcurves.remove(fc)

# ---------------------------------------------------------------- mesh: one shell, faceless head

me = body.data
bm = bmesh.new()
bm.from_mesh(me)
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
bm.faces.ensure_lookup_table()
# keep the largest connected shell: the eyeballs, ears and eyebrows are shells of their own
seen = set()
islands = []
for f in bm.faces:
    if f.index in seen:
        continue
    stack = [f]
    seen.add(f.index)
    isl = []
    while stack:
        g = stack.pop()
        isl.append(g)
        for e in g.edges:
            for h in e.link_faces:
                if h.index not in seen:
                    seen.add(h.index)
                    stack.append(h)
    islands.append(isl)
islands.sort(key=len, reverse=True)
dropped = [len(i) for i in islands[1:]]
bmesh.ops.delete(bm, geom=[f for isl in islands[1:] for f in isl], context='FACES')
bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
bm.verts.ensure_lookup_table()
bm.faces.ensure_lookup_table()

deform = bm.verts.layers.deform.active
names = {g.index: g.name for g in body.vertex_groups}


def weights(v):
    return {names[i]: w for i, w in v[deform].items() if w > 0}


def smoothstep(a, b, x):
    t = min(1.0, max(0.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


# fit a piecewise ellipsoid (an egg) to the skull and pull the head onto it
headv = [v for v in bm.verts if weights(v).get('Head', 0) >= 0.5]
zt = max(v.co.z for v in headv)
crown = [v for v in headv if v.co.z > zt - 0.4]
rx = max(abs(v.co.x) for v in crown if v.co.z > zt - 0.36) * 0.97
y_back = max(v.co.y for v in crown)
y_front = min(v.co.y for v in headv if zt - 0.37 < v.co.z < zt - 0.12)  # forehead, above the nose
z_chin = min(v.co.z for v in headv if v.co.y < -0.15)
cy = 0.5 * (y_front + y_back) + 0.02
cz = z_chin + 0.47 * (zt - z_chin)
r_up, r_down = zt - cz, cz - z_chin + 0.02
r_front, r_back = cy - y_front, y_back - cy
# the neck column under the jaw, measured on the source (units of the source's rest pose): x +-0.125,
# y from -0.04 (throat) to 0.29 (nape)
nx, ncy, nry = 0.125, 0.125, 0.165
print('HEAD', dict(zt=zt, rx=rx, y_back=y_back, y_front=y_front, z_chin=z_chin, cy=cy, cz=cz))


def egg(p):
    d = p - Vector((0.0, cy, cz))
    ex = d.x / rx
    ey = d.y / (r_front if d.y < 0 else r_back)
    ez = d.z / (r_up if d.z > 0 else r_down)
    n = math.sqrt(ex * ex + ey * ey + ez * ez)
    return Vector((0.0, cy, cz)) + d / max(n, 1e-6)


moved = 0
on_egg = []
for v in bm.verts:
    w = weights(v)
    hw = w.get('Head', 0.0) + w.get('Neck', 0.0)
    if hw < 0.3:
        continue
    # the neck column below the jaw stays as it is
    r = math.hypot(v.co.x / nx, (v.co.y - ncy) / nry)
    neckness = (1.0 - smoothstep(1.05, 1.6, r)) * (1.0 - smoothstep(z_chin + 0.03, z_chin + 0.13, v.co.z))
    f = smoothstep(0.3, 0.7, hw) * (1.0 - neckness)
    if f <= 0:
        continue
    v.co = v.co.lerp(egg(v.co), f)
    moved += 1
    if f > 0.999:
        on_egg.append(v)
# relax the vertices that are wholly on the egg along its surface, so the old nose, mouth and brow leave no
# folded or crowded triangles behind
for _ in range(6):
    to = [egg(v.co.lerp(sum((e.other_vert(v).co for e in v.link_edges), Vector()) / len(v.link_edges), 0.5))
          for v in on_egg]
    for v, p in zip(on_egg, to):
        v.co = p
head_info = dict(center=(0.0, cy, cz), rx=rx, r_up=r_up, r_down=r_down, r_front=r_front, r_back=r_back, moved=moved)

# ---------------------------------------------------------------- uvs into the palette skin

GROUP = {'Head': 'head', 'Neck': 'head', 'Torso': 'torso', 'Abdomen': 'torso', 'Shoulder.L': 'torso',
         'Shoulder.R': 'torso', 'Hips': 'pelvis', 'Body': 'pelvis', 'Bone': 'pelvis'}
for s in ('L', 'R'):
    GROUP.update({'UpperArm.' + s: 'uarm.' + s, 'LowerArm.' + s: 'farm.' + s, 'Palm.' + s: 'hand.' + s,
                  'MiddleHand.' + s: 'hand.' + s, 'Fingers.' + s: 'hand.' + s, 'Thumb1.' + s: 'hand.' + s,
                  'Thumb2.' + s: 'hand.' + s, 'UpperLeg.' + s: 'thigh.' + s, 'LowerLeg.' + s: 'shin.' + s,
                  'Foot.' + s: 'foot.' + s})
SEAMS = {frozenset(('head', 'torso')), frozenset(('torso', 'pelvis'))}
for s in ('L', 'R'):
    for a, b in (('torso', 'uarm.'), ('uarm.', 'farm.'), ('farm.', 'hand.'), ('pelvis', 'thigh.'),
                 ('thigh.', 'shin.'), ('shin.', 'foot.')):
        SEAMS.add(frozenset((a if a == 'torso' or a == 'pelvis' else a + s, b + s)))
GRIME = {'foot': 0.45, 'shin': 0.18, 'hand': 0.2, 'farm': 0.06, 'thigh': 0.05, 'pelvis': 0.02, 'torso': 0.01}


def group_weights(v):
    gw = {}
    for n, w in weights(v).items():
        g = GROUP.get(n)
        if g:
            gw[g] = gw.get(g, 0.0) + w
    return gw


def dominant(gw):
    return max(gw.items(), key=lambda kv: kv[1])[0] if gw else 'pelvis'


bm.normal_update()
uv = bm.loops.layers.uv.new('UVMap')
rng = random.Random(1216)
LINE0, LINE1 = 28, 35          # texel rows of the seam line: symmetric about the middle of the 64 rows
tone_count = [0, 0, 0, 0]
seam_faces = 0
for f in bm.faces:
    gws = [group_weights(l.vert) for l in f.loops]
    doms = [dominant(g) for g in gws]
    groups = sorted(set(doms))
    # tone: lit top, plain, underside, grime
    n = f.normal
    tone = 1 if n.z > 0.6 else (2 if n.z < -0.55 else 0)
    part = max(sorted(set(doms)), key=doms.count).split('.')[0]
    if rng.random() < GRIME.get(part, 0.0):
        tone = 3
    tone_count[tone] += 1
    if len(groups) == 2 and frozenset(groups) in SEAMS:
        a, b = groups
        seam_faces += 1
        for l, g in zip(f.loops, gws):
            wa, wb = g.get(a, 0.0), g.get(b, 0.0)
            s = wb / (wa + wb) if wa + wb > 1e-6 else (1.0 if dominant(g) == b else 0.0)
            l[uv].uv = ((16 * tone + 12) / TEX, (2.0 + s * (TEX - 4.0)) / TEX)
    else:
        for l in f.loops:
            l[uv].uv = ((16 * tone + 4) / TEX, 16.5 / TEX)

bm.to_mesh(me)
bm.free()
me.update()
with bpy.context.temp_override(object=body, active_object=body, selected_objects=[body]):
    if me.has_custom_normals:
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
me.shade_flat()

# ---------------------------------------------------------------- 1.8 m tall


def top_z():
    return max((body.matrix_world @ v.co).z for v in me.vertices)


raw_top = top_z()
factor = HEIGHT / raw_top
apply_scale(factor)
scale_actions(factor)
for k in ('rx', 'r_up', 'r_down', 'r_front', 'r_back'):
    head_info[k] *= factor
head_info['center'] = tuple(c * factor for c in head_info['center'])

# ---------------------------------------------------------------- the skins


def q8(c):
    """32 levels a channel."""
    return tuple(min(248, max(0, int(math.floor(x / 8.0 + 0.5)) * 8)) for x in c)


def write_png(path, rows):
    raw = b''.join(b'\x00' + bytes(r) for r in rows)

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    data = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', TEX, TEX, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))
    with open(path, 'wb') as fh:
        fh.write(data)


def skin(tones, line_dark, edge_dark):
    """64x64: for tone k, columns 16k..16k+7 are plain, 16k+8..16k+15 are its seam strip (line in rows 28..35)."""
    rows = []
    for y in range(TEX):
        row = []
        for x in range(TEX):
            k, c = divmod(x, 16)
            col = tones[k]
            if c >= 8:
                if LINE0 + 1 <= y <= LINE1 - 1:
                    col = tuple(v * line_dark for v in col)
                elif y in (LINE0, LINE1):
                    col = tuple(v * edge_dark for v in col)
            row.extend(q8(col))
        rows.append(row)
    return rows


# pale glazed ceramic: plain, glazed top, underside, scuffed
MANNEQUIN = [(216, 208, 200), (240, 232, 224), (176, 168, 160), (152, 136, 120)]
# near-black with a faint cold sheen on top-facing faces
SHADOW = [(16, 16, 24), (48, 48, 64), (8, 8, 8), (24, 24, 24)]
tex_mannequin = os.path.join(OUT, 'figure_mannequin.png')
tex_shadow = os.path.join(OUT, 'figure_shadow.png')
write_png(tex_mannequin, skin(MANNEQUIN, 0.42, 0.7))
write_png(tex_shadow, skin(SHADOW, 0.35, 0.6))

mat = bpy.data.materials.new('figure')
mat.use_nodes = True
nodes = mat.node_tree.nodes
bsdf = nodes.get('Principled BSDF')
img = bpy.data.images.load(tex_mannequin)
tex = nodes.new('ShaderNodeTexImage')
tex.image = img
tex.interpolation = 'Closest'
mat.node_tree.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
nodes.active = tex
me.materials.clear()
me.materials.append(mat)
for p in me.polygons:
    p.material_index = 0

# ---------------------------------------------------------------- clips

RENAME = {'Standing': 'StandUp'}
for a in list(bpy.data.actions):
    n = a.name.split('|')[-1]
    n = n[4:] if n.startswith('Man_') else n
    FILLED[RENAME.get(n, n)] = FILLED.pop(a.name, set())
    a.name = RENAME.get(n, n)
    a.use_fake_user = True
actions = {a.name: a for a in bpy.data.actions}
bones = [b.name for b in arm.data.bones]


def channel(action, bone, prop, n, frame, default, skip=()):
    out = list(default)
    if (bone, prop) in skip:
        return out
    for i in range(n):
        fc = action.fcurves.find('pose.bones["%s"].%s' % (bone, prop), index=i)
        if fc:
            out[i] = fc.evaluate(frame)
    return out


def sample(action, frame, skip=()):
    """A pose of the clip; properties in skip (bone, prop) stay at Blender's rest instead."""
    pose = {}
    for b in bones:
        q = Quaternion(channel(action, b, 'rotation_quaternion', 4, frame, (1, 0, 0, 0), skip)).normalized()
        l = Vector(channel(action, b, 'location', 3, frame, (0, 0, 0), skip))
        pose[b] = [q, l]
    return pose


def local(bone, G):
    """An armature-axes rotation about the bone's head, as a rotation in the bone's rest frame."""
    R = arm.data.bones[bone].matrix_local.to_3x3()
    return (R.inverted() @ G.to_matrix() @ R).to_quaternion()


def rot(axis, deg):
    return Quaternion(Vector(axis), math.radians(deg))


def write_action(name, frames):
    """frames: list of pose dicts, one per frame from 0."""
    act = bpy.data.actions.new(name)
    act.use_fake_user = True
    for b in bones:
        prev = None
        qs = []
        for p in frames:
            q = p[b][0].copy()
            if prev is not None and prev.dot(q) < 0:
                q.negate()
            qs.append(q)
            prev = q
        for prop, n, vals in (('rotation_quaternion', 4, [tuple(q) for q in qs]),
                              ('location', 3, [tuple(p[b][1]) for p in frames])):
            for i in range(n):
                fc = act.fcurves.new('pose.bones["%s"].%s' % (b, prop), index=i, action_group=b)
                fc.keyframe_points.add(len(frames))
                for fi, v in enumerate(vals):
                    k = fc.keyframe_points[fi]
                    k.co = (fi, v[i])
                    k.interpolation = 'LINEAR'
                fc.update()
    return act


idle0 = sample(actions['Idle'], 0)
# Creep was posed over the idle's keyed bones only, with the rest at Blender's rest: keep it that way
creep_base = sample(actions['Idle'], 0, FILLED.get('Idle', ()))

# Stand: the idle's first frame held still, one second
stand = write_action('Stand', [idle0] * 25)

# Creep: a stiff, slow, wrong walk built bone by bone. Near-straight legs swing from the hip like a wooden
# doll's (a compass gait: the hips ride up over the planted leg and drop between steps), feet are set down flat
# with no heel-toe roll and drag their toes through the swing, the timing lurches (each step snaps through,
# then the planted leg drags), the torso leans forward and rocks with the hips, the arms hang dead straight
# and hardly swing, and the head is lolled onto the right shoulder with one twitch a loop.
CN = 39            # frames: 1.625 s at 24 fps, two steps
A = 15.0           # hip swing, degrees each way
K_SWING = 32.0     # knee bend at mid swing
K_STANCE = 2.0     # the planted knee stays nearly locked
DROOP = 7.0        # toes hanging down at mid swing
B = 0.5            # lurch: pace runs from 0.5x (feet planted) to 1.5x (mid swing) of the mean
LEAN = 9.0
rest = {bn.name: bn.matrix_local.copy() for bn in arm.data.bones}
rhead = {bn.name: bn.head_local.copy() for bn in arm.data.bones}


def T(v):
    return Matrix.Translation(v)


def about(h, q):
    return T(h) @ q.to_matrix().to_4x4() @ T(-h)


def part_of(v):
    gw = {}
    for g in v.groups:
        n = GROUP.get(body.vertex_groups[g.group].name)
        if n:
            gw[n] = gw.get(n, 0.0) + g.weight
    return max(gw.items(), key=lambda kv: kv[1])[0] if gw else None


parts = [part_of(v) for v in me.vertices]
ANKLE = {}
for side in ('L', 'R'):
    pts = set()
    for poly in me.polygons:
        ps = {parts[i] for i in poly.vertices}
        if ps == {'shin.' + side, 'foot.' + side}:
            pts.update(poly.vertices)
    ANKLE[side] = sum((me.vertices[i].co for i in pts), Vector()) / len(pts)


def leg_angles(phi):
    phi %= 1.0
    if phi < 0.5:      # planted: +A to -A, the ankle moving back at a steady pace
        return math.degrees(math.asin(math.sin(math.radians(A)) * (1.0 - 4.0 * phi))), K_STANCE, 0.0, True
    w = (phi - 0.5) / 0.5
    knee = K_STANCE + (K_SWING - K_STANCE) * math.sin(math.pi * w)
    return -A * math.cos(math.pi * w), knee, DROOP * math.sin(math.pi * w), False


def leg(side, alpha, knee, D_body):
    Gh = rot((1, 0, 0), -alpha)
    Gk = rot((1, 0, 0), knee)
    D_shin = D_body @ about(rhead['UpperLeg.' + side], Gh) @ about(rhead['LowerLeg.' + side], Gk)
    return Gh, Gk, D_shin @ ANKLE[side]


def creep_pose(t):
    u = t / CN - B / (4.0 * math.pi) * math.sin(4.0 * math.pi * t / CN)
    p = {bn: [Quaternion(), Vector()] for bn in bones}
    for bn in bones:
        if bn.split('.')[0] in ('UpperArm', 'LowerArm', 'Palm', 'MiddleHand', 'Fingers', 'Thumb1', 'Thumb2', 'Shoulder'):
            p[bn] = [creep_base[bn][0].copy(), creep_base[bn][1].copy()]
    cyc = 2.0 * math.pi * u
    G_body = rot((0, 0, 1), -4.0 * math.cos(cyc)) @ rot((0, 1, 0), 3.0 * math.sin(cyc))
    D0 = about(rhead['Body'], G_body)
    legs = {}
    for side, phi in (('L', u), ('R', u + 0.5)):
        alpha, knee, droop, planted = leg_angles(phi)
        legs[side] = (alpha, knee, droop, planted) + leg(side, alpha, knee, D0)
    stance = 'L' if legs['L'][3] else 'R'
    dz = ANKLE[stance].z - legs[stance][6].z
    D_body = T(Vector((0, 0, dz))) @ D0
    Rb = rest['Body'].to_3x3()
    p['Body'] = [local('Body', G_body), Rb.inverted() @ Vector((0, 0, dz))]
    clear = []
    for side in ('L', 'R'):
        alpha, knee, droop, planted, Gh, Gk, _ = legs[side]
        Gh, Gk, ank = leg(side, alpha, knee, D_body)
        p['UpperLeg.' + side][0] = local('UpperLeg.' + side, Gh)
        p['LowerLeg.' + side][0] = local('LowerLeg.' + side, Gk)
        M = T(ank) @ rot((1, 0, 0), droop).to_matrix().to_4x4() @ T(-ANKLE[side]) @ rest['Foot.' + side]
        basis = rest['Foot.' + side].inverted() @ M
        p['Foot.' + side] = [basis.to_quaternion(), basis.to_translation()]
        clear.append(ank.z - ANKLE[side].z)
    p['Torso'][0] = local('Torso', rot((1, 0, 0), LEAN))
    for side in ('L', 'R'):
        swing = 3.0 * math.sin(cyc + (0.0 if side == 'L' else math.pi))
        p['UpperArm.' + side][0] = local('UpperArm.' + side, rot((1, 0, 0), -LEAN + swing)) @ p['UpperArm.' + side][0]
    tw = max(0.0, 1.0 - abs(t - 26.0) / 2.5)
    roll = -24.0 + 15.0 * tw
    p['Head'][0] = local('Head', rot((1, 0, 0), 12.0) @ rot((0, 1, 0), roll) @ rot((0, 0, 1), 7.0 * tw)) @ creep_base['Head'][0]
    p['Neck'][0] = local('Neck', rot((1, 0, 0), 6.0)) @ creep_base['Neck'][0]
    return p, dz, clear, legs


creep_frames = []
creep_lift = []
for t in range(CN + 1):
    pose, dz, clear, legs = creep_pose(t % CN)
    creep_frames.append(pose)
    creep_lift.append(max(clear))
creep = write_action('Creep', creep_frames)
# its pace: how far the planted ankle travels in one step, two steps a loop
step = abs(leg('L', A, K_STANCE, Matrix())[2].y - leg('L', -A, K_STANCE, Matrix())[2].y)
creep_speed = 2.0 * step / (CN / FPS)


walk = actions['Walk']
WN = int(round(walk.frame_range[1] - walk.frame_range[0]))
# every clip in an NLA track of its own, so the exporter writes each as one animation
ad = arm.animation_data or arm.animation_data_create()
ad.action = None
for tr in list(ad.nla_tracks):
    ad.nla_tracks.remove(tr)
for a in sorted(bpy.data.actions, key=lambda a: a.name):
    tr = ad.nla_tracks.new()
    tr.name = a.name
    tr.strips.new(a.name, int(a.frame_range[0]), a)
for pb in arm.pose.bones:
    pb.rotation_quaternion = (1, 0, 0, 0)
    pb.location = (0, 0, 0)
    pb.scale = (1, 1, 1)
scene.frame_set(0)

# ---------------------------------------------------------------- export

glb = os.path.join(OUT, 'figure.glb')
bpy.ops.export_scene.gltf(
    filepath=glb, export_format='GLB', use_selection=False,
    export_texcoords=True, export_normals=True, export_tangents=False,
    export_materials='NONE', export_vertex_color='NONE',
    export_skins=True, export_influence_nb=4, export_all_influences=False, export_def_bones=False,
    export_morph=False, export_animations=True, export_animation_mode='ACTIONS', export_force_sampling=False,
    export_frame_range=False, export_anim_slide_to_zero=True, export_optimize_animation_size=False,
    export_reset_pose_bones=True, export_rest_position_armature=True, export_yup=True, export_apply=False,
    export_cameras=False, export_lights=False, export_extras=False, export_draco_mesh_compression_enable=False)

stats = dict(
    triangles=sum(len(p.vertices) - 2 for p in me.polygons), vertices=len(me.vertices),
    bones=len(arm.data.bones), bone_names=[b.name for b in arm.data.bones],
    dropped_shells=dropped, raw_height=raw_top, scale=factor, head=head_info,
    seam_faces=seam_faces, tones=tone_count,
    clips={a.name: (a.frame_range[1] - a.frame_range[0]) / FPS for a in sorted(bpy.data.actions, key=lambda a: a.name)},
    bounds=dict(min=[min((body.matrix_world @ v.co)[i] for v in me.vertices) for i in range(3)],
                max=[max((body.matrix_world @ v.co)[i] for v in me.vertices) for i in range(3)]),
    creep_step=step, creep_speed=creep_speed, creep_max_swing_lift=max(creep_lift),
    filled_properties={k: len(v) for k, v in sorted(FILLED.items())})


# the walk's pace: how fast its planted foot moves back over its longest run of planted frames, in m/s
def foot_speed(action, n):
    for tr in ad.nla_tracks:
        tr.mute = True
    ad.action = action
    xs = []
    for f in range(int(action.frame_range[0]), int(action.frame_range[0]) + n):
        scene.frame_set(f)
        hl = arm.matrix_world @ arm.pose.bones['Foot.L'].head
        xs.append((f, hl.y, hl.z))
    zmin = min(z for _, _, z in xs)
    runs, cur = [], []
    for f, y, z in xs:
        if z < zmin + 0.01:
            cur.append((f, y))
        elif cur:
            runs.append(cur)
            cur = []
    if cur:
        runs.append(cur)
    run = max(runs, key=len)
    (fa, ya), (fb, yb) = run[0], run[-1]
    return abs(yb - ya) / ((fb - fa) / FPS) if fb > fa else None


stats['walk_speed'] = foot_speed(walk, WN)
arm.animation_data.action = None
with open(os.path.join(OUT, 'stats.json'), 'w') as fh:
    json.dump(stats, fh, indent=1)
print('STATS', json.dumps(stats))

# ---------------------------------------------------------------- previews, rendered from the exported file

STRIPS = arg("--strip-dir")   # optional: several frames of every clip, for looking at the motion

if PREVIEWS:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.fps = FPS
    bpy.ops.import_scene.gltf(filepath=glb)
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    fig = next(o for o in bpy.data.objects if o.type == 'MESH' and o.parent == arm)
    for o in list(bpy.data.objects):
        if o not in (arm, fig):
            bpy.data.objects.remove(o, do_unlink=True)
    mat = bpy.data.materials.new('preview')
    mat.use_nodes = True
    img = bpy.data.images.load(tex_mannequin)
    tex = mat.node_tree.nodes.new('ShaderNodeTexImage')
    tex.image = img
    tex.interpolation = 'Closest'
    mat.node_tree.links.new(tex.outputs['Color'], mat.node_tree.nodes['Principled BSDF'].inputs['Base Color'])
    mat.node_tree.nodes.active = tex
    fig.data.materials.clear()
    fig.data.materials.append(mat)
    ad = arm.animation_data
    for tr in ad.nla_tracks:
        tr.mute = True
    clips = {a.name: a for a in bpy.data.actions}

    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.color_type = 'TEXTURE'
    scene.display.shading.show_shadows = True
    scene.display.shading.shadow_intensity = 0.35
    scene.display.render_aa = 'OFF'
    scene.view_settings.view_transform = 'Standard'
    scene.world = bpy.data.worlds.new('bg')
    scene.world.color = (0.035, 0.04, 0.05)
    scene.render.resolution_x, scene.render.resolution_y = 240, 320
    scene.render.resolution_percentage = 100
    floor_me = bpy.data.meshes.new('floor')
    floor_me.from_pydata([(-3, -3, 0), (3, -3, 0), (3, 3, 0), (-3, 3, 0)], [], [(0, 1, 2, 3)])
    floor = bpy.data.objects.new('floor', floor_me)
    scene.collection.objects.link(floor)
    fmat = bpy.data.materials.new('floor')
    fmat.diffuse_color = (0.09, 0.09, 0.1, 1)
    floor_me.materials.append(fmat)
    cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
    scene.collection.objects.link(cam)
    scene.camera = cam
    cam.data.lens = 50

    def shot(path, yaw_deg, height=HEIGHT, dist=None, look=None, lift=0.25):
        dist = dist or height * 1.9 + 0.6
        look = look if look is not None else height * 0.52
        a = math.radians(yaw_deg)
        cam.location = (dist * math.sin(a), -dist * math.cos(a), look + lift)
        cam.rotation_euler = (Vector((0, 0, look)) - cam.location).to_track_quat('-Z', 'Y').to_euler()
        scene.render.filepath = path
        bpy.ops.render.render(write_still=True)

    def pose(clip, frame=None):
        """A clip's pose the way the engine samples it: bones the clip doesn't key stay at rest."""
        ad.action = None
        for pb in arm.pose.bones:
            pb.rotation_quaternion = (1, 0, 0, 0)
            pb.location = (0, 0, 0)
            pb.scale = (1, 1, 1)
        if clip is None:
            scene.frame_set(0)
            return
        a = clips[clip]
        ad.action = a
        if frame is None:
            frame = 0.5 * (a.frame_range[0] + a.frame_range[1])
        scene.frame_set(int(frame), subframe=frame - int(frame))

    P = lambda n: os.path.join(OUT, n)
    pose(None)
    shot(P('preview_rest_front.png'), 0)
    shot(P('preview_rest_threequarter.png'), 35)
    pose('Stand')
    shot(P('preview_head.png'), 25, dist=1.1, look=HEIGHT * 0.9, lift=0.05)
    for name in sorted(clips):
        pose(name)
        shot(P('preview_clip_%s.png' % name), 35)
    if STRIPS:
        os.makedirs(STRIPS, exist_ok=True)
        for name in sorted(clips):
            a = clips[name]
            f0, f1 = a.frame_range
            for i in range(8):
                pose(name, f0 + (f1 - f0) * i / 8.0)
                shot(os.path.join(STRIPS, '%s_%d.png' % (name, i)), 90 if name in ('Creep', 'Walk') else 35)
    # the tall black figure: the same model with the shadow skin, stretched the way the game will
    img.filepath = tex_shadow
    img.reload()
    arm.scale = (0.8, 0.8, 1.45)
    for name, clip, yaw in (('preview_shadow_front.png', 'Stand', 0), ('preview_shadow_threequarter.png', 'Creep', 35)):
        pose(clip)
        shot(P(name), yaw, height=HEIGHT * 1.45)
print('DONE')
