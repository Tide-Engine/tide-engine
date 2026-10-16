# GRAVEWAKE's textures: the kits' base colour images, made small enough for a
# web page, darkened in their creases by the ambient occlusion of the kits' ORM
# images. They stay opaque, but for the cut-out ones (vines, grass), whose
# alpha is their mask.
#
#   python -P make_textures.py <Medieval Village MegaKit dir> <Fantasy Props MegaKit dir> <Stylized Nature MegaKit dir> <out dir>
#
# The dirs are the Standard downloads, unzipped. Writes <out dir>/<name>.png;
# the names are split_kit.py's texture names.
import os, sys
from PIL import Image

village, props, nature, out = sys.argv[1:5]
V = os.path.join(village, "Textures")
P = os.path.join(props, "Textures")
N = os.path.join(nature, "Textures")

# name: (base colour, occlusion from an ORM's red channel or None, size, cut-out)
TEXTURES = {
    "woodtrim": (os.path.join(V, "T_WoodTrim_BaseColor.png"), os.path.join(V, "T_WoodTrim_ORM.png"), 512, False),
    "plaster": (os.path.join(V, "T_Plaster_BaseColor.png"), os.path.join(V, "T_Plaster_ORM.png"), 512, False),
    "rocktrim": (os.path.join(V, "T_RockTrim_BaseColor.png"), os.path.join(V, "T_RockTrim_ORM.png"), 512, False),
    "brick": (os.path.join(V, "T_Brick_BaseColor.png"), None, 512, False),
    "roundtiles": (os.path.join(V, "T_RoundTiles_BaseColor.png"), None, 512, False),
    "unevenbrick": (os.path.join(V, "T_UnevenBrick_BaseColor.png"), None, 512, False),
    "ornaments": (os.path.join(village, "glTF", "T_MetalOrnaments_BaseColor.png"), None, 256, False),
    "metal": (os.path.join(P, "T_Trim_Metal_BaseColor.png"), os.path.join(P, "T_Trim_Metal_ORM.png"), 256, False),
    "furniture": (os.path.join(P, "T_Trim_Furniture_BaseColor.png"), os.path.join(P, "T_Trim_Furniture_ORM.png"), 256, False),
    "props": (os.path.join(P, "T_Trim_Props_BaseColor.png"), os.path.join(P, "T_Trim_Props_ORM.png"), 256, False),
    "cloth": (os.path.join(P, "T_Trim_Cloth_BaseColor.png"), os.path.join(P, "T_Trim_Cloth_ORM.png"), 256, False),
    "deadbark": (os.path.join(N, "Bark_DeadTree.png"), None, 256, False),
    "twistedbark": (os.path.join(N, "Bark_TwistedTree.png"), None, 256, False),
    "twistedleaves": (os.path.join(N, "Leaves_TwistedTree_C.png"), None, 256, True),
}

only = set(sys.argv[5:])
os.makedirs(out, exist_ok=True)
for name, (base, orm, size, cutout) in TEXTURES.items():
    if only and name not in only:
        continue
    image = Image.open(base).convert("RGBA").resize((size, size), Image.LANCZOS)
    if not cutout:
        image = image.convert("RGB")
        if orm is not None:
            occlusion = Image.open(orm).convert("RGB").split()[0].resize((size, size), Image.LANCZOS)
            # Most of the occlusion, not all of it: the game's light has its own
            soft = occlusion.point(lambda v: int(255 - (255 - v) * 0.75))
            image = Image.composite(image, Image.new("RGB", (size, size), (0, 0, 0)), soft)
    image.save(os.path.join(out, name + ".png"), optimize=True)
    print(name, size, os.path.getsize(os.path.join(out, name + ".png")) // 1024, "KB")
