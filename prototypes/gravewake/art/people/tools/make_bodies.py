# GRAVEWAKE's bodies: six zombies and four heroes, from Quaternius' Ultimate Modular Men and
# Ultimate Modular Women packs (CC0 1.0, https://quaternius.com/packs/ultimatemodularcharacters.html
# and https://quaternius.com/packs/ultimatemodularwomen.html), glTF versions.
#
# Run headless; it writes every body (or those named by --only) into --out:
#
#   blender -b --factory-startup --python make_bodies.py -- --men <dir> --women <dir> [--women <dir2>] --out <dir> [--only zombie_king,hero_smith]
#
# --men/--women are folders holding the packs' .gltf files (Farmer.gltf, King.gltf, ...); several
# --women folders are searched in order. For each body it:
# - drops what isn't the person (held swords/pistols, the packs' icosphere helper, a backpack
#   unless the body keeps it);
# - restructures the rig as gw_rig.restructure() says (FK feet, no pole helpers), the skeleton
#   moves.glb / moves_women.glb are keyed on;
# - repaints the flat materials (BODIES below; "Part:Material" repaints that material on the
#   <name>_<Part> mesh only);
# - zombies: the eye faces get their own material "Eye", a warm amber;
# - adds a second UV map, glTF TEXCOORD_1: (1, 0) on zombie eye vertices, (0, 0) everywhere else
#   (TEXCOORD_0 stays the pack's UVs);
# - writes <name>.glb with the mesh, skin and skeleton and no clips, Y up, metres.
import os
import sys

import bpy

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gw_rig  # noqa: E402
from gw_rig import srgb  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def args(name):
    return [argv[i + 1] for i, a in enumerate(argv) if a == name]


MEN = args("--men")
WOMEN = args("--women")
OUT = args("--out")[0]
ONLY = set(args("--only")[0].split(",")) if args("--only") else None

AMBER = "#ffa526"
DROP = ("Icosphere", "Sword", "Pistol", "Backpack")

BODIES = {
    # ---- zombies: dead skin, desaturated grimy clothes, dried blood, greyed hair, amber eyes
    "zombie_farmer": {"src": ("men", "Farmer.gltf"), "eye": "Eye", "paint": {
        "Skin": "#7b8a6a",              # grey-green
        "Eyebrows": "#3a3a33",
        "Head:Beige": "#5f5843",        # straw hat, filthy
        "Red": "#3b1712",               # hat band, dried blood
        "Body:Brown": "#4d2c24",        # shirt, blood-soaked
        "Feet:Brown": "#2f251c",        # boots
        "Brown2": "#1c1712",
        "LightBlue": "#343a40",         # overalls, faded to slate
        "Body:Beige": "#4e4535"}},
    "zombie_worker": {"src": ("men", "Worker.gltf"), "eye": "Eye", "paint": {
        "Skin": "#6e7a80",              # bluish grey
        "Eyebrows": "#2e2e2c",
        "Moustache": "#3c3934",
        "Head:Worker_Yellow": "#3d362e",  # the hard hat becomes a dull iron cap
        "Body:Worker_Yellow": "#4e4434",
        "Worker_Vest": "#4a221c",       # tunic, bloodied
        "LightBrown": "#575247",
        "Brown": "#3a3128",
        "Brown2": "#29221b",
        "Grey": "#2a2826",
        "Black": "#121110"}},
    "zombie_king": {"src": ("men", "King.gltf"), "eye": "Eye", "paint": {   # the rare plague king
        "Skin": "#8f8d5a",              # jaundiced
        "Hair_White": "#7d7a70",
        "Gold": "#6b5524",              # tarnished crown
        "Blue": "#2a2d36",
        "Metal": "#4a4b49",
        "Metal_Dark": "#2b2c2b",
        "Beige": "#4f2a22",             # bloodied trim
        "DarkBrown": "#261d17"}},
    "zombie_peasant": {"src": ("women", "Worker.gltf"), "eye": "Brown", "paint": {
        "Skin": "#8a957e",              # pale grey-green
        "DarkBrown": "#4b453d",         # hair, greyed
        "Head:Worker_Yellow": "#6a6352",  # the hard hat becomes a linen cap
        "Body:Worker_Yellow": "#5c5340",
        "Worker_Vest": "#55271f",       # apron, bloodied
        "White": "#6f6b62",
        "Brown_02": "#3d3329",
        "Brown2": "#2b231c",
        "Black": "#151311"}},
    "zombie_maid": {"src": ("women", "Formal.gltf"), "eye": "Brown", "paint": {
        "Skin": "#76808a",              # bluish grey
        "Head:Red": "#4d3b34",          # hair, dulled
        "Feet:Red": "#2b1d19",          # shoes
        "Body:LimeGreen": "#4b2520",    # bodice, blood-soaked
        "Legs:LimeGreen": "#3b4036",    # skirt, grimy
        "Gold": "#4f4330"}},
    "zombie_wanderer": {"src": ("men", "Adventurer.gltf"), "eye": "Eye", "paint": {
        "Skin": "#7f8862",              # sallow green
        "Eyebrows": "#2e2a26",
        "Hair": "#3d3833",
        "Green": "#33372b",
        "LightGreen": "#45443a",
        "Brown": "#3c3128",
        "Brown2": "#4a221c",            # belt and patches, bloodied
        "Grey": "#2a2927",
        "Black": "#121212"}},
    # ---- heroes: one grim medieval palette, each with an accent colour on a garment
    "hero_ranger": {"src": ("men", "Adventurer.gltf"), "keep": ("Backpack",), "paint": {   # accent: green
        "Skin": "#b5876a",
        "Eyebrows": "#2e1f15",
        "Hair": "#3a2617",
        "Green": "#2e4a26",             # tunic: the green accent
        "LightGreen": "#5a4632",        # leather
        "Brown": "#3d3228",
        "Brown2": "#2b221a",
        "Grey": "#3b2f25",
        "Black": "#151210",
        "Backpack:Brown": "#4b3726",
        "Backpack:LightGreen": "#585240",
        "Backpack:Gold": "#8c6c34",
        "Backpack:Green": "#2e4a26"}},
    "hero_huntress": {"src": ("women", "Medieval.gltf"), "paint": {   # accent: red
        "Skin": "#cfa087",
        "White": "#b5b0a6",             # grey hair
        "Head:DarkBrown": "#7a1d18",    # hood: the red accent
        "Body:DarkBrown": "#3a2a1f",
        "Feet:DarkBrown": "#3a2a1f",
        "Body:LightBrown": "#4f3726",   # bodice
        "Feet:LightBrown": "#4a3626",   # boots
        "Gold": "#8a6a32",
        "Black": "#1f1d1c",
        "Metal": "#5b5d5f"}},
    "hero_smith": {"src": ("men", "Worker.gltf"), "paint": {   # accent: blue
        "Skin": "#a8735a",
        "Eyebrows": "#2a1a10",
        "Moustache": "#3b2516",
        "Head:Worker_Yellow": "#3b2c20",  # leather cap
        "Body:Worker_Yellow": "#2e2219",  # apron straps
        "Worker_Vest": "#4a3322",       # leather apron
        "LightBrown": "#31507e",        # shirt: the blue accent
        "Brown": "#3a3029",
        "Brown2": "#29221b",
        "Grey": "#2f2924",
        "Black": "#121110"}},
    "hero_witch": {"src": ("women", "Witch.gltf"), "paint": {   # accent: gold
        "Skin": "#d2a893",
        "Hair_Black": "#1c1918",
        "Purple": "#2b2830",            # dress and hat, ink-dark
        "Gold": "#b88d36",              # trim and hat band: the gold accent
        "Brown2": "#3a2a20",
        "Legs:Brown": "#2a2420"}},
}


def find_source(pack, filename):
    for folder in (MEN if pack == "men" else WOMEN):
        path = os.path.join(folder, filename)
        if os.path.exists(path):
            return path
    raise SystemExit(f"no {pack} {filename} in {MEN if pack == 'men' else WOMEN}")


def set_colour(mat, hexcode):
    rgb = srgb(hexcode)
    if mat.use_nodes and mat.node_tree.nodes.get("Principled BSDF"):
        mat.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*rgb, 1)
    mat.diffuse_color = (*rgb, 1)


def meshes():
    return [o for o in bpy.data.objects if o.type == "MESH"]


def part_of(obj, part):
    return obj.name == part or obj.name.endswith("_" + part)


def paint(name, spec):
    for key, hexcode in spec.items():
        if ":" not in key:
            continue
        part, matname = key.split(":")
        copy = None
        for obj in meshes():
            if not part_of(obj, part):
                continue
            for slot in obj.material_slots:
                if slot.material and slot.material.name == matname:
                    if copy is None:
                        copy = slot.material.copy()
                        copy.name = f"{matname}_{part}"
                    slot.material = copy
        if copy is None:
            raise SystemExit(f"{name}: no {matname} on a {part} mesh")
        set_colour(copy, hexcode)
    for key, hexcode in spec.items():
        if ":" in key:
            continue
        mat = bpy.data.materials.get(key)
        if not mat or not any(s.material == mat for o in meshes() for s in o.material_slots):
            raise SystemExit(f"{name}: no material {key} in use")
        set_colour(mat, hexcode)


def mark_eyes(name, eye_material):
    """Zombie eyes: their faces on the head mesh get material "Eye" (amber); returns the eye faces per mesh."""
    head = next(o for o in meshes() if o.name.endswith("_Head"))
    slots = [i for i, s in enumerate(head.material_slots) if s.material and s.material.name == eye_material]
    if not slots:
        raise SystemExit(f"{name}: no {eye_material} on {head.name}")
    if eye_material == "Eye":
        eye = head.material_slots[slots[0]].material
        index = slots[0]
    else:
        eye = head.material_slots[slots[0]].material.copy()
        eye.name = "Eye"
        head.data.materials.append(eye)
        index = len(head.material_slots) - 1
    set_colour(eye, AMBER)
    faces = [p.index for p in head.data.polygons if p.material_index in slots]
    for i in faces:
        head.data.polygons[i].material_index = index
    return {head.name: set(faces)}


def add_uv2(eye_faces):
    for obj in meshes():
        me = obj.data
        if not me.uv_layers:  # the backpack has none; every primitive gets both sets
            me.uv_layers.new(name="UVMap")
        first = me.uv_layers[0]
        layer = me.uv_layers.new(name="EyeMask")
        marked = eye_faces.get(obj.name, set())
        uv = [0.0, 1.0] * len(me.loops)            # Blender (0, 1) is glTF (0, 0)
        for p in me.polygons:
            if p.index in marked:
                for li in p.loop_indices:
                    uv[2 * li] = 1.0               # Blender (1, 1) is glTF (1, 0)
        layer.uv.foreach_set("vector", uv)
        me.uv_layers.active = first
        first.active_render = True


def build(name, spec):
    src = find_source(*spec["src"])
    arm = gw_rig.load(src)
    keep = spec.get("keep", ())
    for obj in list(bpy.data.objects):
        if any(obj.name.startswith(d) for d in DROP) and not any(obj.name.startswith(k) for k in keep):
            bpy.data.objects.remove(obj, do_unlink=True)
    for action in list(bpy.data.actions):
        bpy.data.actions.remove(action)
    arm.animation_data_clear()
    gw_rig.restructure(arm)
    paint(name, spec["paint"])
    eye_faces = mark_eyes(name, spec["eye"]) if "eye" in spec else {}
    add_uv2(eye_faces)
    used = {o.material_slots[p.material_index].material.name for o in meshes() for p in o.data.polygons}
    unpainted = sorted(used - {m for m in spec["paint"] if ":" not in m}
                       - {f"{k.split(':')[1]}_{k.split(':')[0]}" for k in spec["paint"] if ":" in k} - {"Eye"})
    for obj in meshes():
        obj.data.name = obj.name
    os.makedirs(OUT, exist_ok=True)
    glb = os.path.join(OUT, f"{name}.glb")
    bpy.ops.export_scene.gltf(
        filepath=glb, export_format='GLB', use_selection=False,
        export_texcoords=True, export_normals=True, export_tangents=False,
        export_materials='EXPORT', export_vertex_color='NONE',
        export_skins=True, export_influence_nb=4, export_all_influences=False, export_def_bones=False,
        export_morph=False, export_animations=False, export_rest_position_armature=True,
        export_yup=True, export_apply=True, export_cameras=False, export_lights=False, export_extras=False,
        export_draco_mesh_compression_enable=False)
    t = gw_rig.Target(arm)
    top = max((o.matrix_world @ v.co).z for o in meshes() for v in o.data.vertices)
    print(f"BODY {name}: {glb} {os.path.getsize(glb)} bytes, from {src}; height {top:.3f} m; "
          f"Body (hips) head y {t.h['Body'].z:.4f} m; hip joints y {t.hipmid.z:.4f} m; ankles y {t.ankle:.4f} m; "
          f"eyes {sum(len(v) for v in eye_faces.values())} faces; unpainted {unpainted}")


for name, spec in BODIES.items():
    if ONLY is None or name in ONLY:
        build(name, spec)
