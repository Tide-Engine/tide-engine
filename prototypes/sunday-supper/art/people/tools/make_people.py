# SUNDAY SUPPER's people: the Hales and the two travellers, from Quaternius'
# Ultimate Modular Men and Ultimate Modular Women packs (CC0,
# https://quaternius.com/packs/ultimatemodularcharacters.html and
# https://quaternius.com/packs/ultimatemodularwomen.html).
#
# Run headless, once per character, with the pack's source file:
#
#   blender -b --factory-startup --python make_people.py -- --who walt --src Farmer.blend --out <dir> [--preview]
#
# What it does to the source:
# - drops props that aren't the person (a sword, a pistol, the packs' icosphere helper);
# - repaints the flat materials for the part each person plays (see PEOPLE);
# - keeps the clips the game plays and renames none of them;
# - writes <who>.glb with the clips as glTF animations, Y up, in metres, modifiers (mirror) applied;
# - with --preview, renders <who>.png from the front for checking the paint.
import sys, os, math
import bpy

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def arg(name, default=None):
    return argv[argv.index(name) + 1] if name in argv else default


WHO = arg("--who")
SRC = arg("--src")
OUT = arg("--out")
PREVIEW = "--preview" in argv

# Linear colours, as Blender and glTF keep them.
DENIM = (0.030, 0.043, 0.075)
DENIM_DARK = (0.018, 0.024, 0.040)
PEOPLE = {
    # Walt Hale: the farmer, older, in his straw hat and overalls
    "walt": {"paint": {"Eyebrows": (0.23, 0.22, 0.20), "LightBlue": (0.040, 0.052, 0.068), "Beige": (0.30, 0.27, 0.17)}},
    # Ruth Hale: the formal dress made a plain house dress, her hair grey
    "ruth": {"paint": {"LimeGreen": (0.075, 0.085, 0.110), "Red": (0.36, 0.35, 0.33), "Gold": (0.26, 0.22, 0.16),
                       "Brown": (0.20, 0.19, 0.18)}},
    # Dale Hale: the son, in a faded work hoodie
    "dale": {"paint": {"Purple": (0.075, 0.080, 0.058), "LightBlue": DENIM_DARK, "Hair": (0.035, 0.022, 0.012)}},
    # The travellers
    "sam": {"paint": {"LightBrown": (0.12, 0.045, 0.030), "LightBlue": DENIM, "Red_Dark": (0.05, 0.05, 0.055)}},
    "jo": {"paint": {"Orange": DENIM, "White": (0.30, 0.31, 0.29)}},
}
DROP = ("Icosphere", "Sword", "Pistol", "Backpack")
CLIPS = ("Idle", "Idle_Neutral", "Walk", "Run", "Interact", "Wave")

spec = PEOPLE[WHO]
if SRC.lower().endswith(".blend"):
    bpy.ops.wm.open_mainfile(filepath=SRC)
else:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=SRC)

for obj in list(bpy.data.objects):
    if any(obj.name.startswith(d) for d in DROP):
        bpy.data.objects.remove(obj, do_unlink=True)

used = set()
for obj in bpy.data.objects:
    if obj.type == "MESH":
        for slot in obj.material_slots:
            if slot.material:
                used.add(slot.material.name)
for name, rgb in spec["paint"].items():
    mat = bpy.data.materials.get(name)
    if not mat:
        raise SystemExit(f"{WHO}: no material {name}")
    if mat.use_nodes and mat.node_tree.nodes.get("Principled BSDF"):
        mat.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*rgb, 1)
    mat.diffuse_color = (*rgb, 1)

for action in list(bpy.data.actions):
    if action.name not in CLIPS:
        bpy.data.actions.remove(action)
missing = [c for c in CLIPS if c not in bpy.data.actions]
if missing:
    raise SystemExit(f"{WHO}: missing clips {missing}")

arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
if arm.animation_data:
    arm.animation_data.action = None
    for track in list(arm.animation_data.nla_tracks):
        arm.animation_data.nla_tracks.remove(track)
for action in bpy.data.actions:
    action.use_fake_user = True

os.makedirs(OUT, exist_ok=True)
glb = os.path.join(OUT, f"{WHO}.glb")
bpy.ops.export_scene.gltf(
    filepath=glb, export_format='GLB', use_selection=False,
    export_texcoords=False, export_normals=True, export_tangents=False,
    export_materials='EXPORT', export_vertex_color='NONE',
    export_skins=True, export_influence_nb=4, export_all_influences=False, export_def_bones=False,
    export_morph=False, export_animations=True, export_animation_mode='ACTIONS', export_force_sampling=False,
    export_frame_range=False, export_anim_slide_to_zero=True, export_optimize_animation_size=True,
    export_reset_pose_bones=True, export_rest_position_armature=True, export_yup=True, export_apply=True,
    export_cameras=False, export_lights=False, export_extras=False, export_draco_mesh_compression_enable=False)
top = max((o.matrix_world @ v.co).z for o in bpy.data.objects if o.type == "MESH" for v in o.data.vertices)
print(f"PEOPLE {WHO}: {glb} materials={sorted(used)} height={top:.3f}")

if PREVIEW:
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "MATERIAL"
    scene.render.resolution_x = 320
    scene.render.resolution_y = 480
    camera = bpy.data.cameras.new("Preview")
    camera.type = "ORTHO"
    camera.ortho_scale = 2.1
    eye = bpy.data.objects.new("Preview", camera)
    scene.collection.objects.link(eye)
    eye.location = (0, -6, 0.9)
    eye.rotation_euler = (math.pi / 2, 0, 0)
    scene.camera = eye
    scene.render.filepath = os.path.join(OUT, f"{WHO}.png")
    bpy.ops.render.render(write_still=True)
