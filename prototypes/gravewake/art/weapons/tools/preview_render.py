# Checking renders of GRAVEWAKE's .glb models (weapons, hands, items), in the game's axes.
#
#   blender -b --factory-startup --python preview_render.py -- --dir <glb dir> --out <png dir> \
#       [--json weapons.json] [--only name,name] [--sheet <pixels per metre> [--view side|front|back]]
#
# For each model it writes three views: <name>_side.png from +X (+Z to the right, +Y up), see-through,
# with the origin's axes (red +X, green +Y, blue +Z) and the points weapons.json gives marked;
# <name>_back.png from behind (-Z, +X to the right, as the player sees a held weapon); and
# <name>_34.png, a three-quarter view with back faces culled, where anything inside out is a hole.
# With --sheet it writes sheet_<name>.png instead: one view (from +X unless --view says the front, +Z,
# or behind) on a clear background at a fixed scale, for preview_sheet.py to lay out side by side.
# Workbench, flat material colours.
import sys, os, json, math
import bpy, bmesh
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def arg(name, default=None):
    return argv[argv.index(name) + 1] if name in argv else default


DIR, OUT, JS = arg("--dir"), arg("--out"), arg("--json")
ONLY = set(arg("--only").split(",")) if arg("--only") else None
PPM = float(arg("--sheet")) if arg("--sheet") else None
VIEW = arg("--view", "side")
INFO = {e["name"]: e for e in json.load(open(JS))} if JS else {}
# what each point is drawn as
POINTS = {"tip": (1.0, 0.85, 0.0), "offhand": (0.0, 0.9, 0.9), "head": (0.9, 0.0, 0.9), "stringTop": (1, 1, 1),
          "stringBottom": (1, 1, 1), "stringLeft": (1, 1, 1), "stringRight": (1, 1, 1), "nock": (1.0, 0.45, 0.0),
          "boltTail": (1.0, 0.45, 0.0), "boltTip": (1.0, 0.45, 0.0), "muzzle": (1.0, 0.1, 0.1),
          "touchhole": (1.0, 0.1, 0.1), "wrist": (0.0, 0.9, 0.9), "elbow": (0.0, 0.9, 0.9)}
SHEET_VIEW = {"scythe": "back"}


def V(p):
    return Vector((-p[0], -p[2], p[1]))


def flat_mat(name, rgb):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*rgb, 1.0)
    return m


def add_mesh(bm, mat):
    me = bpy.data.meshes.new("marker")
    bm.to_mesh(me)
    bm.free()
    me.materials.append(mat)
    ob = bpy.data.objects.new("marker", me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def sphere(p, r, rgb):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=10, v_segments=6, radius=r)
    bmesh.ops.translate(bm, vec=V(p), verts=bm.verts[:])
    return add_mesh(bm, flat_mat("m", rgb))


def stick(a, b, r, rgb):
    a, b = V(a), V(b)
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=6, radius1=r, radius2=r, depth=(b - a).length)
    q = Vector((0, 0, 1)).rotation_difference((b - a).normalized())
    bmesh.ops.rotate(bm, cent=(0, 0, 0), matrix=q.to_matrix(), verts=bm.verts[:])
    bmesh.ops.translate(bm, vec=(a + b) / 2, verts=bm.verts[:])
    return add_mesh(bm, flat_mat("m", rgb))


def bounds(obs):
    lo, hi = Vector((1e9,) * 3), Vector((-1e9,) * 3)
    for o in obs:
        for v in o.data.vertices:
            w = o.matrix_world @ v.co
            lo = Vector(map(min, lo, w))
            hi = Vector(map(max, hi, w))
    return lo, hi


def camera(direction, centre, extent, ortho=True, res=(640, 640)):
    cam = bpy.data.objects.get("cam") or bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    if cam.name not in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.link(cam)
    d = Vector(direction).normalized()
    cam.data.type = 'ORTHO' if ortho else 'PERSP'
    cam.data.ortho_scale = extent
    cam.data.lens = 50
    cam.data.clip_start = 0.001
    cam.data.clip_end = 100
    dist = extent * 1.45 if not ortho else 10
    cam.location = centre - d * dist
    cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    s = bpy.context.scene
    s.camera = cam
    s.render.resolution_x, s.render.resolution_y = res


def render(path):
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


def setup(transparent=False):
    s = bpy.context.scene
    s.render.engine = 'BLENDER_WORKBENCH'
    sh = s.display.shading
    sh.light = 'STUDIO'
    sh.color_type = 'MATERIAL'
    sh.show_cavity = True
    sh.cavity_type = 'WORLD'
    s.render.film_transparent = transparent
    w = bpy.data.worlds.new("w")
    w.color = (0.22, 0.23, 0.25)
    s.world = w
    s.display_settings.display_device = 'sRGB'
    s.view_settings.view_transform = 'Standard'


def load(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    obs = [o for o in bpy.data.objects if o not in before and o.type == 'MESH']
    for o in obs:
        for sl in o.material_slots:
            m = sl.material
            b = m.node_tree.nodes.get("Principled BSDF") if m and m.use_nodes else None
            if b:
                c = b.inputs["Base Color"].default_value
                m.diffuse_color = (c[0], c[1], c[2], 1.0)
    return obs


def one(name):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    setup(transparent=PPM is not None)
    obs = load(os.path.join(DIR, name + ".glb"))
    lo, hi = bounds(obs)
    size = max(hi - lo)
    c = (lo + hi) / 2
    sh = bpy.context.scene.display.shading
    if PPM:
        view = SHEET_VIEW.get(name, VIEW)
        d = {"side": Vector((1, 0, 0)), "back": Vector((0, -1, 0)), "front": Vector((0, 1, 0))}[view]
        w = (hi.y - lo.y) if view == "side" else (hi.x - lo.x)
        h = hi.z - lo.z
        ext = max(w, h) + 0.04
        camera(d, c, ext, True, (max(8, int(ext * PPM)), max(8, int(ext * PPM))))
        sh.show_backface_culling = True
        render(os.path.join(OUT, f"sheet_{name}.png"))
        return
    info = INFO.get(name, {})
    L = max(0.06, size * 0.18)
    r = max(0.0025, size * 0.006)
    marks = [sphere((0, 0, 0), r * 1.6, (1, 1, 1)),
             stick((0, 0, 0), (L, 0, 0), r * 0.7, (1, 0.1, 0.1)),
             stick((0, 0, 0), (0, L, 0), r * 0.7, (0.1, 1, 0.1)),
             stick((0, 0, 0), (0, 0, L), r * 0.7, (0.2, 0.4, 1))]
    for k, rgb in POINTS.items():
        if info.get(k):
            marks.append(sphere(info[k], r * 1.3, rgb))
    if info.get("boltTail") and info.get("boltTip"):
        marks.append(stick(info["boltTail"], info["boltTip"], r * 0.5, (1.0, 0.45, 0.0)))
    if info.get("nock") and info.get("stringLeft"):
        for k in ("stringLeft", "stringRight"):
            marks.append(stick(info[k], info["nock"], r * 0.25, (1, 1, 1)))
    lo2, hi2 = bounds(obs + marks)
    c2 = (lo2 + hi2) / 2
    ext = max(hi2 - lo2) * 1.12
    sh.show_xray = True
    sh.xray_alpha = 0.55
    sh.show_backface_culling = False
    camera((1, 0, 0), c2, ext)
    render(os.path.join(OUT, f"{name}_side.png"))
    camera((0, -1, 0), c2, ext)
    render(os.path.join(OUT, f"{name}_back.png"))
    sh.show_xray = False
    sh.show_backface_culling = True
    for m in marks:
        bpy.data.objects.remove(m, do_unlink=True)
    # from front-right-above in game axes: the camera at game (+X, +Y, +Z), looking back at the model
    d = -V((0.9, 0.45, 0.75))
    camera(d, c, size * 1.05, False)
    render(os.path.join(OUT, f"{name}_34.png"))


def main():
    os.makedirs(OUT, exist_ok=True)
    names = [f[:-4] for f in sorted(os.listdir(DIR)) if f.endswith(".glb")]
    if JS:
        order = [e["name"] for e in json.load(open(JS))]
        names = [n for n in order if n in names] + [n for n in names if n not in order]
    for n in names:
        if ONLY and n not in ONLY:
            continue
        one(n)


main()
