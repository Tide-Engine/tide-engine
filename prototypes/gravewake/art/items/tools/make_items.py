# GRAVEWAKE's items: the mystery chest (closed, and its open base and lid apart), the power-up icons,
# the six perk potions, the power lever, the brazier, a board for the windows, gravestones and a
# grave mound. One .glb each, flat-coloured through material base colours.
#
# The skull, coins, chalice and potions are Quaternius' Ultimate RPG pack (CC0,
# https://quaternius.com/packs/ultimaterpg.html); the quiver's arrows and the carpenter hammer his
# Medieval Weapons pack (CC0, https://quaternius.com/packs/medievalweapons.html). The rest is
# modelled here with the shared helpers in ../../weapons/tools/lowpoly.py.
#
# Run headless with the packs' FBX folders:
#
#   blender -b --factory-startup --python make_items.py -- --pack <Medieval Weapons FBX dir> \
#       --rpg <Ultimate RPG FBX dir> --out <items dir> [--only name,name]
#
# Axes are the game's: +X right, +Y up, +Z forward (an item's front), metres. The origin is the
# bottom centre except where README.md says otherwise. TEXCOORD_1 is (1, 0) on the potions' liquid
# and the brazier's coals, (0, 0) on the rest of those files; the other files have none.
import sys, os, math, random
from mathutils import Vector, Matrix

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(HERE, "..", "..", "weapons", "tools"))
from lowpoly import *  # noqa: E402,F401,F403

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def arg(name, default=None):
    return argv[argv.index(name) + 1] if name in argv else default


PACK = arg("--pack")
RPG = arg("--rpg")
OUT = arg("--out")
ONLY = set(arg("--only").split(",")) if arg("--only") else None

# Linear colours.
PAL = {
    "steel": (0.376, 0.402, 0.440), "steel_light": (0.462, 0.491, 0.539), "steel_dark": (0.266, 0.266, 0.223),
    "wood": (0.238, 0.105, 0.051), "wood_dark": (0.163, 0.073, 0.036), "gold": (0.571, 0.468, 0.159),
    "gold_light": (0.787, 0.644, 0.216), "red": (0.440, 0.042, 0.061), "black": (0.031, 0.025, 0.023),
    "white": (0.80, 0.80, 0.80),
    "chest_wood": (0.205, 0.098, 0.046), "chest_plank": (0.250, 0.125, 0.058), "chest_inside": (0.050, 0.026, 0.016),
    "iron": (0.070, 0.070, 0.076), "iron_dark": (0.035, 0.035, 0.040), "bone": (0.342, 0.300, 0.211),
    "rpg_gold": (0.604, 0.470, 0.098), "leather": (0.120, 0.062, 0.034), "leather_light": (0.210, 0.115, 0.060),
    "glass": (0.380, 0.450, 0.470), "cork": (0.300, 0.180, 0.090),
    "liquid_red": (0.850, 0.045, 0.040), "liquid_blue": (0.080, 0.250, 1.000), "liquid_green": (0.150, 0.850, 0.120),
    "liquid_amber": (1.000, 0.500, 0.050), "liquid_violet": (0.550, 0.100, 0.950), "liquid_white": (0.920, 0.920, 0.880),
    "coal": (0.450, 0.070, 0.015), "ember": (1.000, 0.300, 0.035),
    "board": (0.200, 0.125, 0.075), "board_dark": (0.110, 0.068, 0.040),
    "stone": (0.300, 0.300, 0.290), "stone_dark": (0.150, 0.150, 0.150), "moss": (0.090, 0.130, 0.045),
    "dirt": (0.100, 0.060, 0.035), "dirt_dark": (0.060, 0.036, 0.022),
}
RPG_MATS = {"Bone": "bone", "Gold": "rpg_gold"}


def place_parts(parts, M):
    """Moves parts already made by M (in game axes)."""
    for ob in parts:
        bake_game(ob, M)


def centre_parts(bottom=False, only_y=False):
    """Moves every part so the model's bounding box is centred on the origin (or sits on it)."""
    lo, hi = Vector((1e9,) * 3), Vector((-1e9,) * 3)
    for ob in PARTS:
        for v in ob.data.vertices:
            p = BG @ v.co
            lo = Vector(map(min, lo, p))
            hi = Vector(map(max, hi, p))
    c = (lo + hi) / 2
    if bottom:
        c.y = lo.y
    if only_y:
        c.x = c.z = 0.0
    place_parts(PARTS, T(-c))


def pack_item(folder, name, height, M=None, recolour=None, axis=1):
    """A pack item scaled so its extent along axis (1 = Y) is height, centred on the origin, turned by M."""
    ob = import_fbx(folder, name)
    P = game_pts(ob)
    lo = Vector((min(p.x for p in P), min(p.y for p in P), min(p.z for p in P)))
    hi = Vector((max(p.x for p in P), max(p.y for p in P), max(p.z for p in P)))
    s = height / (hi - lo)[axis]
    bake_game(ob, (M or Matrix.Identity(4)) @ S(s) @ T(-(lo + hi) / 2))
    for sl in ob.material_slots:
        n = base_name(sl.material.name)
        sl.material = MAT[(recolour or {}).get(n) or RPG_MATS.get(n) or PACK_MATS[n]]
    adopt(ob)
    return ob


# ---------------------------------------------------------------- the mystery chest

L, D, HB = 1.10, 0.60, 0.40  # length (X), depth (Z), the base's height; the lid adds 0.20


def chest_base():
    x, z = L / 2, D / 2
    for sx in (-1, 1):
        for sz in (-1, 1):
            box((sx * (x - 0.05), 0.015, sz * (z - 0.05)), (0.09, 0.03, 0.09), "iron_dark", bevel=0.006)
    # a hollow box: floor and four walls, dark inside
    box((0, 0.05, 0), (L - 0.04, 0.04, D - 0.04), "chest_inside")
    box((0, 0.215, z - 0.02), (L, 0.37, 0.04), "chest_wood")
    box((0, 0.215, -z + 0.02), (L, 0.37, 0.04), "chest_wood")
    for sx in (-1, 1):
        box((sx * (x - 0.02), 0.215, 0), (0.04, 0.37, D - 0.08), "chest_wood")
        box((sx * (x - 0.042), 0.235, 0), (0.004, 0.33, D - 0.09), "chest_inside")
    for sz in (-1, 1):
        box((0, 0.235, sz * (z - 0.042)), (L - 0.09, 0.33, 0.004), "chest_inside")
    # planks
    for y in (0.095, 0.215, 0.335):
        for sz in (-1, 1):
            box((0, y, sz * (z + 0.005)), (L - 0.03, 0.112, 0.012), "chest_plank", bevel=0.004)
        for sx in (-1, 1):
            box((sx * (x + 0.005), y, 0), (0.012, 0.112, D - 0.03), "chest_plank", bevel=0.004)
    # iron: corner guards, straps, rims, side handles
    for sx in (-1, 1):
        for sz in (-1, 1):
            box((sx * (x - 0.012), 0.215, sz * (z - 0.012)), (0.062, 0.37, 0.062), "iron", bevel=0.006)
    for sx in (-1, 1):
        for sz in (-1, 1):
            box((sx * 0.30, 0.215, sz * (z + 0.012)), (0.07, 0.37, 0.012), "iron", bevel=0.003)
    for y in (0.045, HB - 0.0175):
        for sz in (-1, 1):
            box((0, y, sz * (z + 0.013)), (L + 0.025, 0.035, 0.014), "iron", bevel=0.003)
        for sx in (-1, 1):
            box((sx * (x + 0.013), y, 0), (0.014, 0.035, D + 0.025), "iron", bevel=0.003)
    for sx in (-1, 1):
        torus((sx * (x + 0.02), 0.27, 0), 0.05, 0.009, "iron_dark", seg=10, sides=5, M=R('Z', 90))
        box((sx * (x + 0.016), 0.315, 0), (0.012, 0.03, 0.06), "iron_dark", bevel=0.003)
    # the lock plate with a skull, and the hinges' lower straps at the back
    box((0, 0.24, z + 0.018), (0.22, 0.22, 0.014), "iron_dark", bevel=0.005)
    for sx in (-1, 1):
        for sy in (-1, 1):
            ball((sx * 0.085, 0.24 + sy * 0.085, z + 0.026), 0.008, "iron", seg=6, rings=4, smooth=False)
    pack_item(RPG, "Skull", 0.15, M=T(0, 0.235, z + 0.035))
    for sx in (-1, 1):
        box((sx * 0.30, 0.33, -z - 0.018), (0.06, 0.13, 0.012), "iron", bevel=0.003)


def lid_profile(n=12):
    """The lid's side outline in (z, y) about the hinge: up the front, over the arch, down the back."""
    pts = [(D, 0.0), (D, 0.05)]
    for i in range(1, n):
        a = math.pi * i / n
        pts.append((D / 2 + D / 2 * math.cos(a), 0.05 + 0.15 * math.sin(a)))
    pts += [(0.0, 0.05), (0.0, 0.0)]
    return pts


def chest_lid():
    """The lid with its hinge axis along X through the origin; closed, it spans z 0..0.6 and y 0..0.2."""
    prof = lid_profile()
    slab(prof, (0, 0, 0), (0, 0, 1), (0, 1, 0), L, "chest_wood", inset=0.012, bevel=True)
    box((0, -0.002, D / 2), (L - 0.06, 0.004, D - 0.06), "chest_inside")
    # iron bands over the arch, at the base's straps and at both ends
    c = Vector((D / 2, 0.0))
    for xb, w in ((-0.30, 0.07), (0.30, 0.07), (-L / 2 + 0.03, 0.062), (L / 2 - 0.03, 0.062)):
        outer = []
        for zz, yy in prof:
            q = Vector((zz, yy))
            d = (q - c)
            d = Vector((d.x, d.y * 1.6)).normalized() if d.length > 1e-6 else Vector((0, 1))
            outer.append(q + d * 0.013)
        band = [(p.x, p.y) for p in outer] + list(reversed(prof))
        band = [p for i, p in enumerate(band) if i == 0 or (Vector(p) - Vector(band[i - 1])).length > 1e-5]
        slab(band, (xb, 0, 0), (0, 0, 1), (0, 1, 0), w, "iron")
    for sz, zz in ((1, D + 0.013), (-1, -0.013)):
        box((0, 0.02, zz), (L + 0.025, 0.035, 0.014), "iron", bevel=0.003)
    # the hasp over the lock plate, the hinges at the back
    box((0, -0.005, D + 0.026), (0.07, 0.12, 0.012), "iron_dark", bevel=0.004)
    ball((0, -0.04, D + 0.034), 0.012, "iron", seg=6, rings=4, smooth=False)
    for sx in (-1, 1):
        cyl((sx * 0.30 - 0.045, 0, 0), (sx * 0.30 + 0.045, 0, 0), 0.016, mat="iron_dark", sides=8)
        box((sx * 0.30, 0.04, -0.016), (0.06, 0.08, 0.012), "iron", bevel=0.003)


def chest_closed():
    chest_base()
    n = len(PARTS)
    chest_lid()
    place_parts(PARTS[n:], T(0, HB, -D / 2))


def chest_open_base():
    chest_base()


# ---------------------------------------------------------------- power-ups (centred on the origin)

def pu_max_ammo():
    lathe([(0, -0.25), (0.058, -0.25), (0.066, -0.235), (0.074, 0.04), (0.080, 0.05), (0.080, 0.062),
           (0.070, 0.062), (0.066, 0.02), (0.0, 0.02)], "leather", sides=10, smooth=True, angle=40)
    for y in (-0.19, -0.06):
        cyl((0, y - 0.012, 0), (0, y + 0.012, 0), 0.0735 + 0.003 * (y + 0.25) / 0.3, mat="leather_light", sides=10)
    tube([(0.07, -0.20, -0.02), (0.10, -0.06, -0.05), (0.09, 0.04, -0.06), (0.07, 0.05, -0.02)], 0.008,
         "leather_light", sides=4, smooth=False)
    rnd = random.Random(3)
    for k in range(7):
        a = 2 * math.pi * k / 7
        r = 0.0 if k == 0 else 0.042
        x, z = math.cos(a) * r, math.sin(a) * r
        tilt = rnd.uniform(-7, 7)
        ob = import_fbx(PACK, "Arrow")
        P = game_pts(ob)
        z0, z1 = min(p.z for p in P), max(p.z for p in P)
        s = 0.44 / (z1 - z0)
        # point down into the quiver, fletching up
        bake_game(ob, T(x, 0.03 + rnd.uniform(-0.02, 0.01), z) @ R('Y', math.degrees(a) + rnd.uniform(0, 120))
                  @ R('Z', tilt) @ R('X', 90 + x * 120) @ S(s) @ T(0, 0, -(z0 + z1) / 2 - 0.0))
        remap(ob)
        adopt(ob)
    centre_parts()


def pu_instakill():
    pack_item(RPG, "Skull", 0.5)


def pu_double():
    rnd = random.Random(5)
    for i in range(3):
        pack_item(RPG, "Coin", 0.26, M=T(rnd.uniform(-0.012, 0.012), 0.0 + i * 0.066, rnd.uniform(-0.012, 0.012))
                  @ R('Y', rnd.uniform(0, 90)) @ R('X', 90), axis=0)
    pack_item(RPG, "Coin_Star", 0.30, M=T(0, 0.066 * 2 + 0.033 + 0.15, 0), axis=0)
    centre_parts()


def pu_nuke():
    pack_item(RPG, "Chalice", 0.5)


def pu_carpenter():
    pack_item(PACK, "Hammer_Small", 0.5, M=R('Y', 90))


def pu_firesale():
    pack_item(RPG, "Coin_Skull", 0.45, axis=0)


# ---------------------------------------------------------------- potions (bottom centre)

def potion(src, liquid, height):
    """An RPG potion: the glass below the liquid's level takes the liquid's colour (and the glow mark),
    since nothing is see-through; the liquid inside goes."""
    ob = import_fbx(RPG, src)
    me = ob.data
    me.normals_split_custom_set([(0.0, 0.0, 0.0)] * len(me.loops))
    names = [base_name(s.material.name) for s in ob.material_slots]
    liq = {i for i, n in enumerate(names) if n.startswith("Liquid")}
    glass = names.index("Glass")
    bm = bmesh.new()
    bm.from_mesh(me)
    level = max(v.co.z for f in bm.faces if f.material_index in liq for v in f.verts)
    used = sorted({f.material_index for f in bm.faces if f.material_index in liq})
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index in liq], context='FACES')
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    gf = [f for f in bm.faces if f.material_index == glass]
    geom = list({e for f in gf for e in f.edges}) + list({v for f in gf for v in f.verts}) + gf
    bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-6, plane_co=(0, 0, level), plane_no=(0, 0, 1))
    for f in bm.faces:
        if f.material_index == glass and f.calc_center_median().z < level:
            f.material_index = used[0]
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = True
    me.set_sharp_from_angle(angle=math.radians(50))
    P = game_pts(ob)
    lo = Vector((min(p.x for p in P), min(p.y for p in P), min(p.z for p in P)))
    hi = Vector((max(p.x for p in P), max(p.y for p in P), max(p.z for p in P)))
    s = height / (hi.y - lo.y)
    bake_game(ob, S(s) @ T(-(lo.x + hi.x) / 2, -lo.y, -(lo.z + hi.z) / 2))
    for i, sl in enumerate(ob.material_slots):
        sl.material = MAT[liquid if i in liq else "glass" if i == glass else "cork"]
    set_uvs(me, lambda p: p.material_index in liq)
    PARTS.append(ob)


def potion_red():
    potion("Potion1_Filled", "liquid_red", 0.22)


def potion_blue():
    potion("Potion2_Filled", "liquid_blue", 0.24)


def potion_green():
    potion("Potion3_Filled", "liquid_green", 0.24)


def potion_amber():
    potion("Potion8_Filled", "liquid_amber", 0.20)


def potion_violet():
    potion("Potion4_Filled", "liquid_violet", 0.24)


def potion_white():
    potion("Potion5_Filled", "liquid_white", 0.24)


# ---------------------------------------------------------------- the power lever and the brazier

def lever():
    """The moving arm: its pivot (an axle along X) at the origin, the handle up +Y."""
    cyl((-0.04, 0, 0), (0.04, 0, 0), 0.048, mat="iron", sides=10, smooth=True)
    cyl((-0.042, 0, 0), (0.042, 0, 0), 0.02, mat="iron_dark", sides=8)
    slab([(-0.03, -0.075), (0.03, -0.075), (0.026, 0.60), (0.016, 0.64), (-0.016, 0.64), (-0.026, 0.60)],
         (0, 0, 0), (1, 0, 0), (0, 1, 0), 0.032, "iron", inset=0.005, bevel=True)
    box((0, -0.065, 0), (0.07, 0.035, 0.05), "iron_dark", bevel=0.006)
    grip_wraps(0.62, 0.74, 0.024, 3, mat="leather_light", core="wood_dark", sides=8, seed=41, bulge=1.15)
    ball((0, 0.765, 0), 0.034, "iron_dark", seg=8, rings=6, smooth=True)
    for y in (0.12, 0.40):
        box((0, y, 0), (0.07, 0.022, 0.042), "iron_dark", bevel=0.004)


def lever_base():
    """The wall mount the lever turns in: same origin (the pivot), the wall behind it at z = -0.075."""
    slab([(-0.15, -0.24), (0.15, -0.24), (0.17, -0.2), (0.17, 0.22), (0.13, 0.26), (-0.13, 0.26), (-0.17, 0.22),
          (-0.17, -0.2)], (0, 0, -0.06), (1, 0, 0), (0, 1, 0), 0.03, "iron", inset=0.006, bevel=True)
    for sx in (-1, 1):
        box((sx * 0.062, 0, -0.02), (0.022, 0.13, 0.11), "iron_dark", bevel=0.006)
        for sy in (-1, 1):
            ball((sx * 0.13, sy * 0.19, -0.042), 0.014, "iron_dark", seg=6, rings=4, smooth=False)
    cyl((-0.08, 0, 0), (0.08, 0, 0), 0.018, mat="iron_dark", sides=8)
    for sx in (-1, 1):
        cyl((sx * 0.075, 0, 0), (sx * 0.09, 0, 0), 0.028, mat="iron", sides=6)


def brazier():
    # three wrought legs with curled feet, a ring between them, a wide bowl of coals
    for k in range(3):
        a = math.radians(90 + k * 120)
        d = Vector((math.cos(a), 0, math.sin(a)))
        pts = [d * r + Vector((0, y, 0)) for r, y in
               ((0.47, 0.06), (0.50, 0.02), (0.45, 0.0), (0.40, 0.03), (0.36, 0.20), (0.30, 0.55), (0.25, 0.85),
                (0.20, 1.03))]
        tube(pts, 0.022, "iron", sides=4, smooth=False, phase=math.pi / 4)
        tube([d * 0.47 + Vector((0, 0.06, 0)), d * 0.52 + Vector((0, 0.10, 0)), d * 0.54 + Vector((0, 0.06, 0)),
              d * 0.51 + Vector((0, 0.035, 0))], 0.016, "iron", sides=4, smooth=False, phase=math.pi / 4)
    torus((0, 0.42, 0), 0.325, 0.014, "iron", seg=12, sides=4)
    torus((0, 0.88, 0), 0.235, 0.014, "iron", seg=12, sides=4)
    lathe([(0, 0.98), (0.17, 0.98), (0.30, 1.04), (0.42, 1.14), (0.50, 1.26), (0.52, 1.29), (0.49, 1.29),
           (0.40, 1.16), (0.28, 1.07), (0.15, 1.02), (0, 1.02)], "iron", sides=14, smooth=True, angle=35)
    torus((0, 1.20, 0), 0.465, 0.012, "iron_dark", seg=14, sides=4)
    for k in range(7):
        a = math.radians(k * 360 / 7 + 13)
        d = Vector((math.cos(a), 0, math.sin(a)))
        cyl(d * 0.51 + Vector((0, 1.28, 0)), d * 0.53 + Vector((0, 1.40, 0)), 0.016, 0.0, mat="iron", sides=4,
            smooth=False)
    # coals: a glowing heap and lumps on it
    lathe([(0.0, 1.03), (0.26, 1.05), (0.355, 1.13), (0.34, 1.18), (0.22, 1.22), (0.0, 1.24)], "coal", sides=12,
          smooth=False, glow=True)
    rnd = random.Random(7)
    for i in range(16):
        a = rnd.uniform(0, 2 * math.pi)
        r = 0.36 * math.sqrt(rnd.uniform(0.02, 1.0))
        y = 1.23 - 0.10 * (r / 0.36) ** 2
        ico((math.cos(a) * r, y, math.sin(a) * r), rnd.uniform(0.04, 0.07), "ember" if i % 3 else "coal", sub=1,
            scale=(1, 0.7, 1), jitter=0.01, seed=i, glow=True)
    centre_parts(bottom=True, only_y=True)


# ---------------------------------------------------------------- boards, graves (bottom centre, the board centred)

def plank():
    """A rough board 1.2 x 0.18 x 0.04, centred on the origin, long along X, its face toward +Z."""
    out = [(-0.60, -0.082), (-0.42, -0.090), (0.0, -0.088), (0.45, -0.090), (0.595, -0.086), (0.60, 0.02),
           (0.585, 0.090), (0.20, 0.089), (-0.30, 0.090), (-0.585, 0.087), (-0.60, 0.03)]
    slab(out, (0, 0, 0), (1, 0, 0), (0, 1, 0), 0.04, "board", inset=0.006, bevel=True)
    for x, y, w in ((-0.25, 0.035, 0.42), (0.18, -0.03, 0.55), (0.40, 0.045, 0.22)):
        box((x, y, 0.0195), (w, 0.006, 0.003), "board_dark")
    for x in (-0.54, 0.54):
        for y in (-0.045, 0.045):
            cyl((x, y, 0.018), (x, y, 0.026), 0.008, mat="iron_dark", sides=6, smooth=False)


def stone_slab(outline, t, mat="stone"):
    slab(outline, (0, 0, 0), (1, 0, 0), (0, 1, 0), t, mat, inset=0.012, bevel=True)


def gravestone_a():
    """A rounded headstone on a plinth, a cross cut in its face."""
    box((0, 0.05, 0), (0.78, 0.10, 0.28), "stone", bevel=0.015)
    top = [(0.30 * math.cos(math.radians(a)), 0.80 + 0.30 * math.sin(math.radians(a))) for a in range(0, 181, 15)]
    stone_slab([(-0.30, 0.09), (0.30, 0.09), (0.30, 0.80)] + top[1:-1] + [(-0.30, 0.80)], 0.13)
    for c, s in (((0, 0.70, 0.066), (0.06, 0.34, 0.006)), ((0, 0.78, 0.066), (0.24, 0.06, 0.006))):
        box(c, s, "stone_dark")
    box((0, 0.38, 0.066), (0.34, 0.03, 0.006), "stone_dark")
    box((0, 0.31, 0.066), (0.26, 0.03, 0.006), "stone_dark")
    box((0.0, 0.11, 0.10), (0.5, 0.02, 0.08), "moss", bevel=0.008)


def gravestone_b():
    """A celtic cross on a stepped base."""
    box((0, 0.08, 0), (0.56, 0.16, 0.36), "stone", bevel=0.02)
    box((0, 0.19, 0), (0.40, 0.08, 0.26), "stone", bevel=0.015)
    box((0, 0.66, 0), (0.17, 0.90, 0.14), "stone", bevel=0.015)
    box((0, 0.98, 0), (0.62, 0.16, 0.14), "stone", bevel=0.015)
    box((0, 1.21, 0), (0.17, 0.18, 0.14), "stone", bevel=0.015)
    lathe([(0.17, -0.055), (0.235, -0.055), (0.235, 0.055), (0.17, 0.055)], "stone", sides=16, smooth=False,
          closed=True, M=T(0, 0.98, 0) @ R('X', 90))
    box((0, 0.98, 0.072), (0.09, 0.09, 0.006), "stone_dark", M=R('Z', 45))
    box((0, 0.166, 0.15), (0.50, 0.012, 0.06), "moss", bevel=0.005)


def gravestone_c():
    """An old pointed slab leaning back, a corner broken off."""
    out = [(-0.27, 0.0), (0.27, 0.0), (0.27, 0.58), (0.22, 0.66), (0.17, 0.63), (0.12, 0.74), (0.0, 0.90),
           (-0.16, 0.80), (-0.27, 0.62)]
    n = len(PARTS)
    stone_slab(out, 0.12)
    box((0, 0.55, 0.0615), (0.05, 0.22, 0.006), "stone_dark")
    box((0, 0.60, 0.0615), (0.16, 0.05, 0.006), "stone_dark")
    box((-0.05, 0.30, 0.0615), (0.012, 0.18, 0.006), "stone_dark", M=R('Z', 25))
    box((0.0, 0.06, 0.062), (0.46, 0.10, 0.012), "moss", bevel=0.006)
    place_parts(PARTS[n:], R('X', -8) @ R('Z', 3))
    box((0, 0.02, 0), (0.62, 0.06, 0.22), "dirt", bevel=0.02)
    centre_parts(bottom=True)


def grave_mound():
    """A low heap of dirt, 1 x 2 m, long along Z."""
    bm = bmesh.new()
    prof = [(0.0, 0.30), (0.25, 0.285), (0.50, 0.24), (0.72, 0.16), (0.90, 0.07), (1.0, 0.0)]
    sides = 14
    rnd = random.Random(11)
    rings = []
    for i, (r, y) in enumerate(prof):
        if r == 0:
            rings.append([bm.verts.new((0, y, 0))])
            continue
        ring = []
        for k in range(sides):
            a = 2 * math.pi * k / sides
            j = 0.0 if i == len(prof) - 1 else 1.0
            ring.append(bm.verts.new((math.cos(a) * r * 0.5 + rnd.uniform(-0.02, 0.02) * j,
                                      y + rnd.uniform(-0.02, 0.02) * j,
                                      math.sin(a) * r * 1.0 + rnd.uniform(-0.03, 0.03) * j)))
        rings.append(ring)
    for i in range(len(rings) - 1):
        A, B = rings[i], rings[i + 1]
        for k in range(sides):
            if len(A) == 1:
                bm.faces.new((A[0], B[(k + 1) % sides], B[k]))
            else:
                bm.faces.new((A[k], A[(k + 1) % sides], B[(k + 1) % sides], B[k]))
    bm.faces.new(rings[-1][::-1])
    emit(bm, "dirt", smooth=False)
    for i in range(9):
        a = rnd.uniform(0, 2 * math.pi)
        f = rnd.uniform(0.3, 0.95)
        x, z = math.cos(a) * 0.5 * f, math.sin(a) * 1.0 * f
        yy = 0.30 * (1 - f ** 2) ** 0.8
        ico((x, yy, z), rnd.uniform(0.04, 0.08), "dirt_dark", sub=1, scale=(1, 0.6, 1), jitter=0.01, seed=20 + i)


ITEMS = [
    ("chest_closed", chest_closed), ("chest_open_base", chest_open_base), ("chest_lid", chest_lid),
    ("pu_max_ammo", pu_max_ammo), ("pu_instakill", pu_instakill), ("pu_double", pu_double), ("pu_nuke", pu_nuke),
    ("pu_carpenter", pu_carpenter), ("pu_firesale", pu_firesale),
    ("potion_red", potion_red), ("potion_blue", potion_blue), ("potion_green", potion_green),
    ("potion_amber", potion_amber), ("potion_violet", potion_violet), ("potion_white", potion_white),
    ("lever", lever), ("lever_base", lever_base), ("brazier", brazier), ("plank", plank),
    ("gravestone_a", gravestone_a), ("gravestone_b", gravestone_b), ("gravestone_c", gravestone_c),
    ("grave_mound", grave_mound),
]


def main():
    os.makedirs(OUT, exist_ok=True)
    rows = []
    for name, fn in ITEMS:
        if ONLY and name not in ONLY:
            continue
        new_scene(PAL)
        fn()
        ob, path = export(name, OUT)
        lo, hi = game_bounds(ob)
        size = hi - lo
        rows.append(f"{name}\t{glb_tris(path)}\t{size.x:.3f} x {size.y:.3f} x {size.z:.3f}\t"
                    f"[{lo.x:.3f}, {lo.y:.3f}, {lo.z:.3f}]..[{hi.x:.3f}, {hi.y:.3f}, {hi.z:.3f}]")
        print("BUILT " + rows[-1])


main()
