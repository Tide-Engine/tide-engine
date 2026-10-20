# The kit's chest, shut: its lid comes modelled wide open (the owner, 2026-10-20: "also chests should be closed lol"), so
# this swings the lid's parts shut about the hinge along the box's back top edge and writes the two parts back over
# themselves (Chest_Wood~furniture.glb, Chest_Wood~metal.glb), exported as split_kit.py exports them. Run it once, after
# split_kit.py:
#
#   blender -b --factory-startup --python close_chest.py -- --dir <art/kit>
#
# In Blender's axes as the glTF importer gives them (Z up, the chest's back toward +Y): the box's parts lie within 0.36 m
# of the middle front to back and under 0.43 m; every one of the lid's starts further back than that, or rises above it. The hinge and the
# turn are where the lid's front and back edges are, open, against where they lie shut on the box.
import sys, os, math
import bpy, bmesh
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
DIR = argv[argv.index("--dir") + 1]

HINGE = Vector((0.0, 0.324, 0.383))   # A point on the hinge's axis (along X)
TURN = math.radians(122.3)            # How far the lid swings shut, about +X
LID_BACK = 0.36                       # A part starting further back than this is the lid's...
LID_HIGH = 0.45                       # ...and one reaching higher than this (the box's top is at 0.43)


def close(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=path)
    obs = [o for o in bpy.data.objects if o.type == 'MESH']
    assert len(obs) == 1, path
    ob = obs[0]
    M = ob.matrix_world.copy()
    Mi = M.inverted()
    swing = Matrix.Translation(HINGE) @ Matrix.Rotation(TURN, 4, 'X') @ Matrix.Translation(-HINGE)
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bm.verts.ensure_lookup_table()
    seen = set()
    moved = 0
    for v in bm.verts:
        if v.index in seen:
            continue
        stack, part = [v], []
        seen.add(v.index)
        while stack:
            a = stack.pop()
            part.append(a)
            for e in a.link_edges:
                b = e.other_vert(a)
                if b.index not in seen:
                    seen.add(b.index)
                    stack.append(b)
        if min((M @ p.co).y for p in part) <= LID_BACK and max((M @ p.co).z for p in part) <= LID_HIGH:
            continue
        for p in part:
            p.co = Mi @ (swing @ (M @ p.co))
        moved += len(part)
    bm.to_mesh(ob.data)
    bm.free()
    ob.data.update()
    for o in bpy.context.scene.objects:
        o.select_set(o is ob)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_image_format="NONE",
                              export_texcoords=True, export_normals=True, export_tangents=False,
                              export_materials="EXPORT", export_animations=False, export_skins=False,
                              export_morph=False, export_yup=True, export_apply=True)
    print("closed", os.path.basename(path), moved, "vertices of the lid")


for name in ("Chest_Wood~furniture.glb", "Chest_Wood~metal.glb"):
    close(os.path.join(DIR, name))
