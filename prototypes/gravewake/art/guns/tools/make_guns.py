# GRAVEWAKE's guns, one .glb each, plus guns.json: Quaternius' Ultimate Gun Pack (CC0,
# https://opengameart.org/content/low-poly-guns-pack), recoloured to the game's palette, scaled to
# size and turned into the game's axes; and the Engineer's turret, a gun on the pack's tripod.
#
# Run headless with the pack's FBX folder:
#
#   blender -b --factory-startup --python make_guns.py -- --pack <Ultimate Gun Pack FBX dir> --out <guns dir>
#
# Axes are the game's: +X right, +Y up, +Z forward, in metres (Tide's importer negates glTF's x, so
# the files hold -x). A gun's trigger hand (the middle of its pistol grip, a hand's width under the
# bore) is at the origin, its barrel along +Z, its top up. guns.json gives each one's length, its
# muzzle (the bore's end) and its triangles. The helpers are in ../../weapons/tools/lowpoly.py.
import sys, os, json
from mathutils import Vector

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "weapons", "tools"))
from lowpoly import *  # noqa: E402,F401,F403

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def arg(name, default=None):
    return argv[argv.index(name) + 1] if name in argv else default


PACK, OUT = arg("--pack"), arg("--out")

# The pack's colours are very dark as its files have them; these are the preview's, as the game's palette.
PAL = {
    "black": (0.040, 0.040, 0.045), "black_soft": (0.075, 0.078, 0.085), "gunmetal": (0.150, 0.152, 0.160),
    "metal": (0.270, 0.275, 0.290), "metal_light": (0.420, 0.430, 0.450), "grey": (0.200, 0.200, 0.195),
    "wood": (0.400, 0.205, 0.095), "wood_dark": (0.260, 0.135, 0.065), "polymer": (0.110, 0.115, 0.105),
    "olive": (0.180, 0.200, 0.120), "tan": (0.420, 0.330, 0.200),
}
GUN_MATS = {"Main": "polymer", "MainDark": "black_soft", "MainLight": "gunmetal", "Metal": "metal", "DarkMetal": "gunmetal",
            "LightMetal": "metal_light", "Black": "black", "Black2": "black", "Grey": "grey", "Wood": "wood", "DarkWood": "wood_dark"}

# name: (pack file, length in metres, the trigger hand in the pack's own units (x along the gun, toward the stock;
# y up), a recolouring)
GUNS = {
    "rifle": ("AssaultRifle2_1", 0.86, (-0.08, 0.0), {}),
    "shotgun": ("Shotgun_1", 0.98, (0.24, -0.06), {"LightMetal": "metal"}),
    "smg": ("SubmachineGun_2", 0.62, (0.07, -0.07), {}),
    "pistol": ("Pistol_1", 0.21, (0.02, 0.08), {}),
}


def import_gun(src):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=os.path.join(PACK, src + ".fbx"))
    new = [o for o in bpy.data.objects if o not in before]
    meshes = [o for o in new if o.type == 'MESH']
    if len(meshes) != 1:
        raise SystemExit(f"{src}: expected one mesh, got {[o.name for o in new]}")
    ob = meshes[0]
    M = ob.matrix_world.copy()
    ob.parent = None
    for o in new:
        if o is not ob:
            bpy.data.objects.remove(o, do_unlink=True)
    ob.matrix_world = Matrix.Identity(4)
    bake(ob, M)
    for s in ob.material_slots:
        s.material = MAT[GUN_MATS[base_name(s.material.name)]]
    return ob


def gun(name, src, length, hand, recolour):
    """A pack gun: its muzzle (the pack's -x) turned to +Z, scaled to length, its trigger hand at the origin."""
    ob = import_gun(src)
    if recolour:
        for s in ob.material_slots:
            for k, v in recolour.items():
                if s.material.name == GUN_MATS.get(k):
                    s.material = MAT[v]
    P = game_pts(ob)
    s = length / (max(p.x for p in P) - min(p.x for p in P))
    grip = Vector((hand[0], hand[1], 0.0))
    M = R('Y', 90) @ S(s) @ T(-grip)
    bake_game(ob, M)
    P = game_pts(ob)
    front = max(p.z for p in P)
    tip = [p for p in P if p.z > front - 0.004]
    bore = sum(tip, Vector()) / len(tip)
    info = {"name": name, "file": name + ".glb", "length": round(length, 3),
            "muzzle": [0.0, round(bore.y, 4), round(front, 4)]}
    set_uvs(ob.data, False)
    PARTS.append(ob)
    return info


def finish(info):
    ob, path = export(info["name"], OUT)
    lo, hi = game_bounds(ob)
    info["tris"] = glb_tris(path)
    info["bounds"] = [[round(v, 4) for v in lo], [round(v, 4) for v in hi]]
    return info


def tripod(height):
    """The pack's tripod, standing: its feet on the ground round the origin, its mount `height` up."""
    ob = import_gun("Accessories/Tripod")
    P = game_pts(ob)
    s = height / (max(p.y for p in P) - min(p.y for p in P))
    foot = Vector(((max(p.x for p in P) + min(p.x for p in P)) / 2, min(p.y for p in P), (max(p.z for p in P) + min(p.z for p in P)) / 2))
    bake_game(ob, S(s) @ T(-foot))
    set_uvs(ob.data, False)
    PARTS.append(ob)
    return {"name": "tripod", "file": "tripod.glb", "length": round(height, 3), "muzzle": [0.0, round(height, 4), 0.0]}


def main():
    os.makedirs(OUT, exist_ok=True)
    entries = []
    for name, (src, length, hand, recolour) in GUNS.items():
        new_scene(PAL)
        entries.append(finish(gun(name, src, length, hand, recolour)))
    # The Engineer's turret stands on this, its gun turning on the mount
    new_scene(PAL)
    entries.append(finish(tripod(0.85)))
    with open(os.path.join(OUT, "guns.json"), "w", newline="\n") as f:
        f.write("[\n" + ",\n".join("  " + json.dumps(e) for e in entries) + "\n]\n")


main()
