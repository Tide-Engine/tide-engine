# Shared modelling helpers for GRAVEWAKE's art scripts (make_weapons.py beside this file and
# ../../items/tools/make_items.py), run inside Blender. Shapes are built in the game's axes (+X right,
# +Y up, +Z forward, metres) and kept in Blender as (x, y, z) -> (-x, -z, y), which its glTF
# exporter writes back Y up and Tide's importer (negating glTF's x) reads as built. A model is the
# parts made since new_scene(), joined by export(); colours are its materials' base colours, UV 0 is
# zeros and UV 1 ('glow', TEXCOORD_1) is (1, 0) on glowing faces and dropped where nothing glows.
import os, math, json, struct, random
import bpy, bmesh
from mathutils import Vector, Matrix

# The packs' material names, onto the palette.
PACK_MATS = {"Steel": "steel", "LightSteel": "steel_light", "DarkSteel": "steel_dark", "LightWood": "wood",
             "DarkWood": "wood_dark", "Gold": "gold", "LightGold": "gold_light", "Red": "red", "Black": "black",
             "White": "white", "LightRed": "red", "DarkBrown": "wood_dark"}

# Game axes to Blender's: the same shape, (x, y, z) -> (-x, -z, y). The exporter writes Y up.
GB = Matrix(((-1, 0, 0, 0), (0, 0, -1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
BG = GB.inverted()


def V(p):
    return Vector((-p[0], -p[2], p[1]))


def R(axis, deg):
    return Matrix.Rotation(math.radians(deg), 4, axis)


def T(x, y=0.0, z=0.0):
    return Matrix.Translation(Vector(x) if not isinstance(x, (int, float)) else Vector((x, y, z)))


def S(x, y=None, z=None):
    y = x if y is None else y
    z = x if z is None else z
    return Matrix.Diagonal((x, y, z, 1.0))


MAT = {}
PARTS = []


def new_scene(pal):
    """An empty scene with a material for each palette colour."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    MAT.clear()
    PARTS.clear()
    for name, rgb in pal.items():
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        b = m.node_tree.nodes["Principled BSDF"]
        b.inputs["Base Color"].default_value = (*rgb, 1.0)
        b.inputs["Roughness"].default_value = 0.8
        b.inputs["Metallic"].default_value = 0.0
        m.diffuse_color = (*rgb, 1.0)
        m.use_backface_culling = True
        MAT[name] = m


def set_uvs(me, glow=False):
    """UV 0 is (0, 0); UV 1 ('glow', TEXCOORD_1) is (1, 0) where glow(face) says and (0, 0) elsewhere.
    The exporter writes glTF's v as 1 - Blender's, so Blender holds v = 1."""
    while me.uv_layers:
        me.uv_layers.remove(me.uv_layers[0])
    me.uv_layers.new(name="UVMap")
    me.uv_layers.new(name="glow")
    n = len(me.loops)
    me.uv_layers["UVMap"].data.foreach_set("uv", [0.0, 1.0] * n)
    vals = [0.0, 1.0] * n
    for p in me.polygons:
        if glow is True or (callable(glow) and glow(p)):
            for li in p.loop_indices:
                vals[2 * li] = 1.0
    me.uv_layers["glow"].data.foreach_set("uv", vals)


def emit(bm, mats, smooth=False, glow=False, angle=50.0):
    """Turns a bmesh built in game axes into a part. Faces' material_index picks from mats."""
    if isinstance(mats, str):
        mats = [mats]
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    for v in bm.verts:
        v.co = V(v.co)
    bmesh.ops.reverse_faces(bm, faces=bm.faces[:])  # the axis swap mirrors, so turn the faces back out
    me = bpy.data.meshes.new("part")
    bm.to_mesh(me)
    bm.free()
    for m in mats:
        me.materials.append(MAT[m])
    for p in me.polygons:
        p.use_smooth = smooth
    if smooth:
        me.set_sharp_from_angle(angle=math.radians(angle))
    set_uvs(me, glow)
    ob = bpy.data.objects.new("part", me)
    bpy.context.scene.collection.objects.link(ob)
    PARTS.append(ob)
    return ob


# ---------------------------------------------------------------- primitives, in game axes

def place(bm, M):
    bmesh.ops.transform(bm, matrix=M, verts=bm.verts[:])


def rot_to(d):
    return Vector((0, 0, 1)).rotation_difference(Vector(d).normalized()).to_matrix().to_4x4()


def cyl(p0, p1, r0, r1=None, mat="steel", sides=8, smooth=True, caps=True, twist=0.0, glow=False):
    """A cylinder or a cone from p0 (radius r0) to p1 (radius r1)."""
    p0, p1 = Vector(p0), Vector(p1)
    r1 = r0 if r1 is None else r1
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=caps, cap_tris=False, segments=sides, radius1=r0, radius2=r1,
                          depth=(p1 - p0).length)
    place(bm, T((p0 + p1) / 2) @ rot_to(p1 - p0) @ R('Z', twist))
    return emit(bm, mat, smooth, glow)


def box(c, s, mat="steel", bevel=0.0, M=None, smooth=False):
    """A box of size s centred at c, its edges chamfered by bevel, turned by M about its centre."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=Vector(s), verts=bm.verts[:])
    if bevel > 0:
        bmesh.ops.bevel(bm, geom=bm.edges[:], offset=bevel, offset_type='OFFSET', segments=1, profile=0.5,
                        affect='EDGES', clamp_overlap=True)
    place(bm, T(c) @ (M or Matrix.Identity(4)))
    return emit(bm, mat, smooth)


def ball(c, r, mat="steel", seg=8, rings=6, smooth=True, scale=(1, 1, 1), M=None, glow=False):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=rings, radius=r)
    bmesh.ops.scale(bm, vec=Vector(scale), verts=bm.verts[:])
    place(bm, T(c) @ (M or Matrix.Identity(4)))
    return emit(bm, mat, smooth, glow)


def ico(c, r, mat="steel", sub=1, smooth=False, scale=(1, 1, 1), M=None, glow=False, jitter=0.0, seed=0):
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=sub, radius=r)
    rnd = random.Random(seed)
    for v in bm.verts:
        v.co += Vector((rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(-1, 1))) * jitter
    bmesh.ops.scale(bm, vec=Vector(scale), verts=bm.verts[:])
    place(bm, T(c) @ (M or Matrix.Identity(4)))
    return emit(bm, mat, smooth, glow)


def tube(pts, radii, mat="steel", sides=6, smooth=True, caps=True, glow=False, phase=0.0, scale2=None):
    """A tube along a polyline, radius per point; a zero radius ends it in a point.
    scale2 squashes the cross-section: (along the frame's normal, along its binormal)."""
    pts = [Vector(p) for p in pts]
    n = len(pts)
    if isinstance(radii, (int, float)):
        radii = [radii] * n
    tang = []
    for i in range(n):
        if i == 0:
            t = pts[1] - pts[0]
        elif i == n - 1:
            t = pts[-1] - pts[-2]
        else:
            t = (pts[i + 1] - pts[i]).normalized() + (pts[i] - pts[i - 1]).normalized()
        tang.append(t.normalized())
    a = Vector((0, 1, 0)) if abs(tang[0].y) < 0.9 else Vector((1, 0, 0))
    N = (a - tang[0] * a.dot(tang[0])).normalized()
    bm = bmesh.new()
    rings = []
    sx, sy = scale2 or (1.0, 1.0)
    for i in range(n):
        if i > 0:
            N = tang[i - 1].rotation_difference(tang[i]) @ N
            N = (N - tang[i] * N.dot(tang[i])).normalized()
        B = tang[i].cross(N)
        if radii[i] < 1e-7:
            rings.append([bm.verts.new(pts[i])])
            continue
        ring = []
        for k in range(sides):
            ang = 2 * math.pi * k / sides + phase
            ring.append(bm.verts.new(pts[i] + (N * math.cos(ang) * sx + B * math.sin(ang) * sy) * radii[i]))
        rings.append(ring)
    for i in range(n - 1):
        A, Bn = rings[i], rings[i + 1]
        for k in range(sides):
            if len(A) == 1:
                bm.faces.new((A[0], Bn[k], Bn[(k + 1) % sides]))
            elif len(Bn) == 1:
                bm.faces.new((A[k], A[(k + 1) % sides], Bn[0]))
            else:
                bm.faces.new((A[k], A[(k + 1) % sides], Bn[(k + 1) % sides], Bn[k]))
    if caps and len(rings[0]) > 1:
        bm.faces.new(rings[0][::-1])
    if caps and len(rings[-1]) > 1:
        bm.faces.new(rings[-1])
    return emit(bm, mat, smooth, glow)


def lathe(profile, mat="steel", sides=8, smooth=True, M=None, phase=0.0, glow=False, mats=None, angle=50.0,
          closed=False):
    """A surface of revolution about +Y from [(radius, y)], bottom to top; zero radii close it.
    mats gives each profile segment its own material (an index into the list); closed joins the last
    point back to the first (a ring's cross-section) instead of capping the ends."""
    bm = bmesh.new()
    rings = []
    for r, y in profile:
        if r < 1e-7:
            rings.append([bm.verts.new((0.0, y, 0.0))])
        else:
            rings.append([bm.verts.new((r * math.cos(2 * math.pi * k / sides + phase), y,
                                        r * math.sin(2 * math.pi * k / sides + phase))) for k in range(sides)])
    names = [mat] if mats is None else mats[0]
    seg_mat = [0] * (len(profile) - 1) if mats is None else mats[1]
    if closed:
        rings.append(rings[0])
        seg_mat = list(seg_mat) + [seg_mat[-1]] * (len(rings) - 1 - len(seg_mat))
    for i in range(len(rings) - 1):
        A, B = rings[i], rings[i + 1]
        for k in range(sides):
            if len(A) == 1 and len(B) == 1:
                continue
            if len(A) == 1:
                f = bm.faces.new((A[0], B[(k + 1) % sides], B[k]))
            elif len(B) == 1:
                f = bm.faces.new((A[k], A[(k + 1) % sides], B[0]))
            else:
                f = bm.faces.new((A[k], A[(k + 1) % sides], B[(k + 1) % sides], B[k]))
            f.material_index = seg_mat[i]
    if len(rings[0]) > 1 and not closed:
        f = bm.faces.new(rings[0][::-1])
        f.material_index = seg_mat[0]
    if len(rings[-1]) > 1 and not closed:
        f = bm.faces.new(rings[-1])
        f.material_index = seg_mat[-1]
    if M is not None:
        place(bm, M)
    return emit(bm, names, smooth, glow, angle)


def area2(pts):
    return sum(pts[i - 1][0] * pts[i][1] - pts[i][0] * pts[i - 1][1] for i in range(len(pts))) / 2


def slab(outline, o, u, v, t, mat="steel", edge_mat=None, inset=0.0, sharp=None, bevel=False, glow=False):
    """An outline [(a, b)] in the plane o + a*u + b*v, made t thick along u x v.
    sharp[i] makes point i a blade's edge (the faces inset, the outline at the middle), in edge_mat;
    bevel chamfers every point by inset instead (a bevelled board)."""
    pts = [Vector(p) for p in outline]
    flags = list(sharp) if sharp is not None else [bevel] * len(pts)
    if area2(pts) < 0:
        pts.reverse()
        flags.reverse()
    o, u, v = Vector(o), Vector(u), Vector(v)
    nrm = u.cross(v).normalized()
    m = len(pts)
    ins = []
    for i in range(m):
        p0, p1, p2 = pts[i - 1], pts[i], pts[(i + 1) % m]
        e1, e2 = (p1 - p0).normalized(), (p2 - p1).normalized()
        n1, n2 = Vector((-e1.y, e1.x)), Vector((-e2.y, e2.x))
        b = n1 + n2
        b = n1 if b.length < 1e-6 else b.normalized()
        c = max(b.dot(n1), 0.4)
        ins.append(p1 + b * (inset / c) if flags[i] else p1)

    def P(q, h):
        return o + u * q.x + v * q.y + nrm * h

    bm = bmesh.new()
    rows = []  # per point: front, front edge, back edge, back
    for i in range(m):
        if flags[i]:
            eh = (t / 2 - inset) if bevel else 0.0
            rows.append([P(ins[i], t / 2), P(pts[i], eh), P(pts[i], -eh), P(ins[i], -t / 2)])
        else:
            rows.append([P(pts[i], t / 2), P(pts[i], t / 2), P(pts[i], -t / 2), P(pts[i], -t / 2)])
    vs = [[bm.verts.new(c) for c in r] for r in rows]
    names = [mat] if edge_mat is None else [mat, edge_mat]
    bm.faces.new([vs[i][0] for i in range(m)])
    bm.faces.new([vs[i][3] for i in reversed(range(m))])
    for i in range(m):
        j = (i + 1) % m
        edge = 1 if (edge_mat is not None and flags[i] and flags[j] and not bevel) else 0
        for a, b in ((0, 1), (1, 2), (2, 3)):
            q = [vs[i][a], vs[j][a], vs[j][b], vs[i][b]]
            if (q[0].co - q[3].co).length < 1e-7 and (q[1].co - q[2].co).length < 1e-7:
                continue
            if (q[0].co - q[3].co).length < 1e-7:
                q = [q[0], q[1], q[2]]
            elif (q[1].co - q[2].co).length < 1e-7:
                q = [q[0], q[1], q[3]]
            f = bm.faces.new(q)
            f.material_index = edge
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=1e-7)
    return emit(bm, names, False, glow)


def torus(c, R_, r, mat="steel", seg=10, sides=5, M=None, smooth=True):
    """A ring about the local Y axis (in the XZ plane), turned (and stretched) by M."""
    bm = bmesh.new()
    rings = []
    for i in range(seg):
        a = 2 * math.pi * i / seg
        ring = []
        for k in range(sides):
            b = 2 * math.pi * k / sides
            rr = R_ + math.cos(b) * r
            ring.append(bm.verts.new((math.cos(a) * rr, math.sin(b) * r, math.sin(a) * rr)))
        rings.append(ring)
    for i in range(seg):
        A, B = rings[i], rings[(i + 1) % seg]
        for k in range(sides):
            bm.faces.new((A[k], B[k], B[(k + 1) % sides], A[(k + 1) % sides]))
    place(bm, T(c) @ (M or Matrix.Identity(4)))
    return emit(bm, mat, smooth)


def grip_wraps(y0, y1, r, n, mat="wood", core="wood_dark", sides=7, seed=1, x=0.0, z=0.0, bulge=1.22):
    """The pack's handle look: a core with n bulging leather wraps."""
    rnd = random.Random(seed)
    cyl((x, y0, z), (x, y1, z), r, mat=core, sides=sides, smooth=False)
    L = (y1 - y0) / n
    for i in range(n):
        a = y0 + L * i + L * 0.06
        b = y0 + L * (i + 1) - L * 0.06
        w = r * bulge * rnd.uniform(0.96, 1.04)
        lathe([(r * 1.02, a), (w, a + (b - a) * 0.3), (w * 0.98, a + (b - a) * 0.75), (r * 1.02, b)], mat=mat,
              sides=sides, smooth=False, phase=rnd.uniform(0, 1),
              M=T(x, (a + b) / 2, z) @ R('Z', rnd.uniform(-3, 3)) @ T(0, -(a + b) / 2, 0))


# ---------------------------------------------------------------- pack meshes

def base_name(n):
    return n.split(".")[0]


def bake(ob, M):
    """Moves a mesh by M in Blender's axes, carrying its custom normals."""
    me = ob.data
    nrm = [Vector(c.vector) for c in me.corner_normals]
    me.transform(M)
    N = M.to_3x3().inverted().transposed()
    me.normals_split_custom_set([tuple((N @ n).normalized()) for n in nrm])
    me.update()


def bake_game(ob, M):
    bake(ob, GB @ M @ BG)


def import_fbx(folder, name):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=os.path.join(folder, name + ".fbx"))
    new = [o for o in bpy.data.objects if o not in before]
    meshes = [o for o in new if o.type == 'MESH']
    if len(meshes) != 1:
        raise SystemExit(f"{name}: expected one mesh, got {[o.name for o in new]}")
    ob = meshes[0]
    M = ob.matrix_world.copy()
    ob.parent = None
    for o in new:
        if o is not ob:
            bpy.data.objects.remove(o, do_unlink=True)
    ob.matrix_world = Matrix.Identity(4)
    bake(ob, M)
    return ob


def slots_named(ob, names):
    return {i for i, s in enumerate(ob.material_slots) if s.material and base_name(s.material.name) in names}


def game_pts(ob, mats=None):
    me = ob.data
    if mats is None:
        return [BG @ v.co for v in me.vertices]
    idx = slots_named(ob, mats)
    vs = set()
    for p in me.polygons:
        if p.material_index in idx:
            vs.update(p.vertices)
    return [BG @ me.vertices[i].co for i in sorted(vs)]


def centroid(ob, mats=None):
    """Area-weighted centre of the faces in mats, in game axes."""
    idx = slots_named(ob, mats) if mats else None
    tot, A = Vector(), 0.0
    for p in ob.data.polygons:
        if idx is None or p.material_index in idx:
            tot += (BG @ p.center) * p.area
            A += p.area
    return tot / A


def drop_mats(ob, names):
    idx = slots_named(ob, names)
    me = ob.data
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index in idx], context='FACES')
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    bm.to_mesh(me)
    bm.free()


def remap(ob, recolour=None):
    for s in ob.material_slots:
        n = base_name(s.material.name)
        s.material = MAT[(recolour or {}).get(n) or PACK_MATS[n]]


def adopt(ob, glow_mats=()):
    """Makes an imported mesh a part (UV sets; glow on faces of glow_mats)."""
    idx = {i for i, s in enumerate(ob.material_slots) if s.material and s.material.name in glow_mats}
    set_uvs(ob.data, (lambda p: p.material_index in idx) if idx else False)
    PARTS.append(ob)


def game_segs(ob, mats):
    """The edges of the faces in mats, in game axes."""
    me = ob.data
    idx = slots_named(ob, mats)
    es = set()
    for p in me.polygons:
        if p.material_index in idx:
            vs = list(p.vertices)
            for i in range(len(vs)):
                es.add((min(vs[i], vs[i - 1]), max(vs[i], vs[i - 1])))
    co = [BG @ v.co for v in me.vertices]
    return [(co[a], co[b]) for a, b in es]


def axis_at(segs, y):
    """The middle of where the plane at height y cuts these edges (a handle's axis there)."""
    for dy in (0.0, 0.01, -0.01, 0.02, -0.02, 0.04, -0.04, 0.08, -0.08):
        yy = y + dy
        pts = [a.lerp(b, (yy - a.y) / (b.y - a.y)) for a, b in segs if (a.y - yy) * (b.y - yy) <= 0 and a.y != b.y]
        if pts:
            xs, zs = [p.x for p in pts], [p.z for p in pts]
            return Vector(((min(xs) + max(xs)) / 2, y, (min(zs) + max(zs)) / 2))
    raise SystemExit(f"no handle at {y}")


def glb_tris(path):
    d = open(path, "rb").read()
    n = struct.unpack_from("<I", d, 12)[0]
    js = json.loads(d[20:20 + n])
    return sum(js["accessors"][p["indices"]]["count"] // 3 for m in js["meshes"] for p in m["primitives"])


def export(name, out):
    """Joins the parts into one mesh and writes <out>/<name>.glb. Returns the object and the path."""
    obs = [o for o in PARTS if o.name in bpy.data.objects]
    if len(obs) > 1:
        with bpy.context.temp_override(active_object=obs[0], object=obs[0], selected_objects=obs,
                                       selected_editable_objects=obs):
            bpy.ops.object.join()
    ob = obs[0]
    ob.name = ob.data.name = name
    g = ob.data.uv_layers.get("glow")
    if g is not None and not any(l.uv[0] > 0.5 for l in g.data):
        ob.data.uv_layers.remove(g)
    for o in bpy.context.scene.objects:
        o.select_set(o is ob)
    bpy.context.view_layer.objects.active = ob
    path = os.path.join(out, name + ".glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_yup=True,
                              export_apply=True, export_texcoords=True, export_normals=True, export_tangents=False,
                              export_materials='EXPORT', export_vertex_color='NONE', export_animations=False,
                              export_skins=False, export_morph=False, export_cameras=False, export_lights=False,
                              export_extras=False)
    canonical_triangles(path)
    return ob, path


def canonical_triangles(path):
    """The exporter writes the same vertices but its triangles in an order that changes from run to run;
    this sorts each primitive's triangles (each turned to start at its smallest index, its winding kept)
    so that building again writes the same bytes."""
    d = bytearray(open(path, "rb").read())
    n = struct.unpack_from("<I", d, 12)[0]
    js = json.loads(bytes(d[20:20 + n]))
    base = 20 + n + 8
    for m in js["meshes"]:
        for pr in m["primitives"]:
            acc = js["accessors"][pr["indices"]]
            view = js["bufferViews"][acc["bufferView"]]
            fmt = {5121: "B", 5123: "H", 5125: "I"}[acc["componentType"]]
            off = base + view.get("byteOffset", 0) + acc.get("byteOffset", 0)
            cnt = acc["count"]
            idx = struct.unpack_from(f"<{cnt}{fmt}", d, off)
            tris = []
            for i in range(0, cnt, 3):
                t = idx[i:i + 3]
                k = t.index(min(t))
                tris.append(t[k:] + t[:k])
            tris.sort()
            struct.pack_into(f"<{cnt}{fmt}", d, off, *[i for t in tris for i in t])
    open(path, "wb").write(bytes(d))


def game_bounds(ob):
    P = [BG @ v.co for v in ob.data.vertices]
    lo = Vector((min(p.x for p in P), min(p.y for p in P), min(p.z for p in P)))
    hi = Vector((max(p.x for p in P), max(p.y for p in P), max(p.z for p in P)))
    return lo, hi
