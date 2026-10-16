# GRAVEWAKE's village: Quaternius' Medieval Village MegaKit, Fantasy Props
# MegaKit and Stylized Nature MegaKit (CC0, the Standard downloads from
# https://quaternius.com), cut into one GLB per piece and texture.
#
# The engine imports a model's geometry, uvs and material colours but not its
# textures, and a draw gives its frag one texture. So each piece is split by
# the texture its faces use, and the game draws each part with that texture:
#
#   blender -b --factory-startup --python split_kit.py -- <out dir> <piece.gltf>...
#
# Each part is written as <out dir>/<Piece>~<texture>.glb, where <texture> is
# the short name TEXTURES gives the material's base colour image ("flat" for a
# material with none, whose colour stays in the material). The materials'
# base colour factors and the pieces' vertex colours are kept; images are not
# exported; the heaviest pieces (DECIMATE) are cut down first. parts.json lists every part written, in the order a glob of
# <out dir>/*.glb gives them (byte order of their names), with its piece,
# texture and whether its texture cuts holes (alpha MASK) or is see-through.
import sys, os, json
import bpy

argv = sys.argv[sys.argv.index("--") + 1:]
OUT = argv[0]
SOURCES = argv[1:]

# Base colour image -> the texture's short name in art/tex.
TEXTURES = {
    "T_WoodTrim_BaseColor": "woodtrim",
    "T_Plaster_BaseColor": "plaster",
    "T_RockTrim_BaseColor": "rocktrim",
    "T_Brick_BaseColor": "brick",
    "T_RoundTiles_BaseColor": "roundtiles",
    "T_UnevenBrick_BaseColor": "unevenbrick",
    "T_MetalOrnaments_BaseColor": "ornaments",
    "T_RedBrick_BaseColor": "redbrick",
    "T_VineLeaf_png": "vine",
    "T_VineLeaf": "vine",
    "T_Trim_Metal_BaseColor": "metal",
    "T_Trim_Furniture_BaseColor": "furniture",
    "T_Trim_Props_BaseColor": "props",
    "T_Trim_Cloth_BaseColor": "cloth",
    "T_Page_Noise": "page",
    "PathRocks_Diffuse": "pathrocks",
    "Rocks_Diffuse": "rocks",
    "Bark_DeadTree": "deadbark",
    "Bark_TwistedTree": "twistedbark",
    "Bark_NormalTree": "bark",
    "Leaves_TwistedTree_C": "twistedleaves",
    "Leaves_NormalTree_C": "leaves",
    "Leaves": "leaves2",
    "Leaf_Pine_C": "pine",
    "Grass": "grass",
    "Flowers": "flowers",
    "Mushrooms": "mushrooms",
}


# Pieces with more triangles than they need here, and how much of them to keep: the trees stand in the fog.
DECIMATE = {
    "DeadTree_": 0.3,
    "TwistedTree_": 0.3,
    "Chain_Coil": 0.3,
    "Chandelier": 0.5,
    "CandleStick_Stand": 0.5,
}


def texture_of(material, modes):
    """The short name of a material's base colour image, and its glTF alpha mode."""
    if material is None:
        return "flat", "OPAQUE"
    mode = modes.get(material.name.split(".")[0], "OPAQUE")
    if material.use_nodes:
        for node in material.node_tree.nodes:
            if node.type == "TEX_IMAGE" and node.image is not None and node.outputs["Color"].links:
                name = os.path.splitext(node.image.name)[0]
                key = TEXTURES.get(name)
                if key is None:
                    raise SystemExit(f"unknown base colour image {node.image.name} in {material.name}")
                return key, mode
    return "flat", mode


written = []
for src in SOURCES:
    piece = os.path.splitext(os.path.basename(src))[0]
    # The source's own alpha modes, by material name: Blender's import doesn't keep them as they were
    with open(src, encoding="utf-8") as f:
        modes = {m.get("name", ""): m.get("alphaMode", "OPAQUE") for m in json.load(f).get("materials", [])}
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=src)
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        continue
    # One object, its nodes' placements applied
    bpy.ops.object.select_all(action="DESELECT")
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    for o in list(bpy.context.scene.objects):
        if o != obj and o.type != "MESH":
            o.select_set(False)
    bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    for prefix, ratio in DECIMATE.items():
        if piece.startswith(prefix):
            fewer = obj.modifiers.new("fewer", "DECIMATE")
            fewer.ratio = ratio
            fewer.use_collapse_triangulate = True
            bpy.ops.object.modifier_apply(modifier=fewer.name)
    # Group material slots by texture
    groups = {}
    for index, slot in enumerate(obj.material_slots):
        key, blend = texture_of(slot.material, modes)
        groups.setdefault(key, []).append((index, blend))
    for key, slots in groups.items():
        indices = {i for i, _ in slots}
        bpy.ops.object.select_all(action="DESELECT")
        part = obj.copy()
        part.data = obj.data.copy()
        bpy.context.scene.collection.objects.link(part)
        mesh = part.data
        import bmesh
        bm = bmesh.new()
        bm.from_mesh(mesh)
        doomed = [f for f in bm.faces if f.material_index not in indices]
        bmesh.ops.delete(bm, geom=doomed, context="FACES")
        loose = [v for v in bm.verts if not v.link_faces]
        bmesh.ops.delete(bm, geom=loose, context="VERTS")
        bm.to_mesh(mesh)
        bm.free()
        if len(mesh.polygons) == 0:
            bpy.data.objects.remove(part)
            continue
        name = f"{piece}~{key}"
        part.select_set(True)
        bpy.context.view_layer.objects.active = part
        path = os.path.join(OUT, name + ".glb")
        bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_image_format="NONE",
                                  export_texcoords=True, export_normals=True, export_tangents=False,
                                  export_materials="EXPORT", export_animations=False, export_skins=False,
                                  export_morph=False, export_yup=True, export_apply=True)
        blend = slots[0][1]
        written.append({"file": name + ".glb", "piece": piece, "tex": key,
                        "cutout": blend == "MASK", "glass": blend == "BLEND",
                        "tris": sum(len(p.vertices) - 2 for p in mesh.polygons)})
        bpy.data.objects.remove(part)

manifest = os.path.join(OUT, "parts.json")
old = json.load(open(manifest)) if os.path.exists(manifest) else []
byname = {p["file"]: p for p in old}
for p in written:
    byname[p["file"]] = p
parts = [byname[k] for k in sorted(byname)]
json.dump(parts, open(manifest, "w", newline="\n"), indent=1)
print("WROTE", len(written), "parts")
