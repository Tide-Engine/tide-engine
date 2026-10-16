# Preview renders for checking GRAVEWAKE's people (Blender 4.4 Workbench, headless).
#
#   blender -b --factory-startup --python preview.py -- --out <dir> --pair <body.glb>[=<catalog.glb>] [--pair ...]
#           [--clips Z_Walk,Sprint|all] [--frames 5] [--views front,side] [--tag name] [--head]
#
# Each --pair is a body and the clip catalog it plays (none: the body at rest). A body takes the
# catalog's pose as the engine's in-place sampling does: every bone's local transform from the
# catalog's skeleton, so a body paired with the other family's catalog shows what that does to it.
# For each clip it writes <tag>_<clip>.png, a sheet with a row per body and view and a column per
# frame (evenly spaced, the last frame included unless the clip loops). Without a catalog, or
# with --clips rest, it writes <tag>_rest.png. --head adds a close-up of each face at rest.
# Floor: a grey plane at height 0 with 0.5 m lines.
import os
import sys

import bpy
import numpy as np
from mathutils import Matrix

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def args(name):
    return [argv[i + 1] for i, a in enumerate(argv) if a == name]


OUT = args("--out")[0]
PAIRS = [p.split("=") for p in args("--pair")]
CLIPS = args("--clips")[0].split(",") if args("--clips") else ["rest"]
FRAMES = int(args("--frames")[0]) if args("--frames") else 5
VIEWS = args("--views")[0].split(",") if args("--views") else ["front", "side"]
TAG = args("--tag")[0] if args("--tag") else "preview"
HEAD = "--head" in argv
SIZE = 300

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.fps = 30
scene.render.engine = "BLENDER_WORKBENCH"
scene.display.shading.light = "STUDIO"
scene.display.shading.color_type = "MATERIAL"
scene.display.shading.show_shadows = True
scene.display.shading.show_cavity = False
scene.render.resolution_x = SIZE
scene.render.resolution_y = SIZE
scene.render.film_transparent = False
scene.view_settings.view_transform = "Standard"
scene.world = bpy.data.worlds.new("Backdrop")
scene.world.color = (0.42, 0.44, 0.47)

floor_mat = bpy.data.materials.new("Floor")
floor_mat.diffuse_color = (0.30, 0.30, 0.30, 1)
line_mat = bpy.data.materials.new("Line")
line_mat.diffuse_color = (0.12, 0.12, 0.12, 1)
bpy.ops.mesh.primitive_plane_add(size=8, location=(0, 0, 0))
bpy.context.object.data.materials.append(floor_mat)
for i in range(-8, 9):
    for axis in (0, 1):
        loc = (i * 0.5, 0, 0.001) if axis == 0 else (0, i * 0.5, 0.001)
        bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
        bar = bpy.context.object
        bar.scale = (0.006, 4, 0.0005) if axis == 0 else (4, 0.006, 0.0005)
        bar.data.materials.append(line_mat)

cam_data = bpy.data.cameras.new("Cam")
cam_data.type = "ORTHO"
cam = bpy.data.objects.new("Cam", cam_data)
scene.collection.objects.link(cam)
scene.camera = cam
VIEW = {  # location, rotation, ortho scale
    "front": ((0, -8, 0.95), (1.5708, 0, 0), 2.6),
    "side": ((8, 0.45, 0.95), (1.5708, 0, 1.5708), 2.6),
    "back": ((0, 8, 0.95), (1.5708, 0, 3.14159), 2.6),
    "head": ((0, -8, 1.62), (1.5708, 0, 0), 0.5),
}


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


people = []
for pair in PAIRS:
    body, bobjs, _ = imported(pair[0])
    cat = None
    clips = {}
    if len(pair) > 1:
        cat, cobjs, clips = imported(pair[1])
        for o in cobjs:
            o.hide_render = True
    people.append({"name": os.path.splitext(os.path.basename(pair[0]))[0] + ("+" + os.path.splitext(os.path.basename(pair[1]))[0] if cat else ""),
                   "body": body, "objs": bobjs, "cat": cat, "clips": clips})


def copy_pose(body, cat):
    """The body's bones take the catalog skeleton's posed armature-space matrices."""
    rest_b = {b.name: b.matrix_local for b in body.data.bones}
    pose_c = {pb.name: pb.matrix for pb in cat.pose.bones}
    for pb in body.pose.bones:
        b = pb.bone
        if b.name not in pose_c:
            continue
        if b.parent:
            p = b.parent.name
            pb.matrix_basis = rest_b[b.name].inverted() @ rest_b[p] @ pose_c[p].inverted() @ pose_c[b.name]
        else:
            pb.matrix_basis = rest_b[b.name].inverted() @ pose_c[b.name]


def show_only(person):
    for q in people:
        for o in q["objs"]:
            o.hide_render = q is not person


def render(path, view):
    loc, rot, scale = VIEW[view]
    cam.location, cam.rotation_euler = loc, rot
    cam_data.ortho_scale = scale
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    img = bpy.data.images.load(path)
    px = np.array(img.pixels[:], dtype=np.float32).reshape(SIZE, SIZE, 4)
    bpy.data.images.remove(img)
    os.remove(path)
    return px


def sheet(rows, path):
    """rows: list of lists of pixel arrays (bottom-up rows, as Blender keeps them)."""
    h, w = len(rows), max(len(r) for r in rows)
    out = np.zeros((h * SIZE, w * SIZE, 4), dtype=np.float32)
    out[..., 3] = 1
    for i, r in enumerate(rows):
        for j, px in enumerate(r):
            y = (h - 1 - i) * SIZE
            out[y:y + SIZE, j * SIZE:(j + 1) * SIZE] = px
    img = bpy.data.images.new("sheet", w * SIZE, h * SIZE)
    img.pixels.foreach_set(out.ravel())
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()
    bpy.data.images.remove(img)
    print("PREVIEW", path)


os.makedirs(OUT, exist_ok=True)
tmp = os.path.join(OUT, "_frame.png")
for clip in CLIPS:
    rows = []
    for person in people:
        show_only(person)
        act = person["clips"].get(clip)
        if clip != "rest" and not act:
            if person["cat"]:
                raise SystemExit(f"{person['name']}: no clip {clip}")
            continue
        if act:
            person["cat"].animation_data_create()
            person["cat"].animation_data.action = act
            if person["cat"].animation_data.action_slot is None and len(act.slots):
                person["cat"].animation_data.action_slot = act.slots[0]
            f0, f1 = (int(round(x)) for x in act.frame_range)
            loops = clip in ("Idle", "Walk", "Run", "Idle_Sword", "Idle_Gun", "Z_Idle", "Z_Walk", "Sprint", "Crouch_Idle", "Crouch_Walk")
            n = FRAMES
            frames = [round(f0 + (f1 - f0) * i / (n if loops else n - 1)) for i in range(n)]
        else:
            frames = [0]
        for view in VIEWS:
            row = []
            for f in frames:
                scene.frame_set(f)
                if act:
                    copy_pose(person["body"], person["cat"])
                    bpy.context.view_layer.update()
                else:
                    for pb in person["body"].pose.bones:
                        pb.matrix_basis = Matrix.Identity(4)
                row.append(render(tmp, view))
            rows.append(row)
        if act:
            print(f"FRAMES {person['name']} {clip}: {frames}")
    if rows:
        sheet(rows, os.path.join(OUT, f"{TAG}_{clip}.png"))

if HEAD:
    row = []
    for person in people:
        show_only(person)
        for pb in person["body"].pose.bones:
            pb.matrix_basis = Matrix.Identity(4)
        bpy.context.view_layer.update()
        row.append(render(tmp, "head"))
    sheet([row], os.path.join(OUT, f"{TAG}_heads.png"))
