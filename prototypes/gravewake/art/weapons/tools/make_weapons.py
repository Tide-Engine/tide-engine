# GRAVEWAKE's weapons, projectiles and first-person hands, one .glb each, plus weapons.json.
#
# Pack weapons come from Quaternius' Medieval Weapons pack (CC0,
# https://quaternius.com/packs/medievalweapons.html); the staffs' crystals from his Ultimate RPG
# pack (CC0, https://quaternius.com/packs/ultimaterpg.html). Everything the packs lack (maces,
# polearms, crossbows, the handgonne, staffs, thrown things, the bolt, the hands) is modelled here
# from boxes, cylinders, lathes and extruded outlines in the same chunky, flat-coloured style.
#
# Run headless with the packs' FBX folders:
#
#   blender -b --factory-startup --python make_weapons.py -- --pack <Medieval Weapons FBX dir> \
#       --rpg <Ultimate RPG FBX dir> --out <weapons dir> [--only name,name]
#
# Axes are the game's: +X right, +Y up, +Z forward, in metres (Tide's importer negates glTF's x,
# so the files hold -x). Melee weapons, staffs and thrown things: the right hand's grip centre at
# the origin, the weapon along +Y, its edge or face toward +Z. Bows: grip at the origin, limbs
# along Y, the string (left out of the mesh) on the -Z side, arrows fly +Z. Crossbows and the
# handgonne: the trigger hand at the origin, shooting along +Z. weapons.json gives each file's
# points in these axes. Colours are materials' base colours; nothing is textured. TEXCOORD_1 is
# (1, 0) on what glows (the staffs' crystals and ember) where a file has it. The helpers are in
# lowpoly.py beside this file.
import sys, os, math, json, random
from mathutils import Vector, Matrix

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lowpoly import *  # noqa: E402,F401,F403 (the shared helpers: axes, materials, primitives, pack meshes)

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def arg(name, default=None):
    return argv[argv.index(name) + 1] if name in argv else default


PACK = arg("--pack")
RPG = arg("--rpg")
OUT = arg("--out")
ONLY = set(arg("--only").split(",")) if arg("--only") else None

# Linear colours, as Blender and glTF keep them. The first ones are the pack's own.
PAL = {
    "steel": (0.376, 0.402, 0.440), "steel_light": (0.462, 0.491, 0.539), "steel_dark": (0.266, 0.266, 0.223),
    "wood": (0.238, 0.105, 0.051), "wood_dark": (0.163, 0.073, 0.036),
    "gold": (0.571, 0.468, 0.159), "gold_light": (0.787, 0.644, 0.216),
    "red": (0.440, 0.042, 0.061), "black": (0.031, 0.025, 0.023), "white": (0.80, 0.80, 0.80),
    "iron": (0.105, 0.105, 0.112), "iron_dark": (0.050, 0.050, 0.055),
    "oak": (0.300, 0.165, 0.075), "leather": (0.100, 0.055, 0.032), "leather_light": (0.190, 0.105, 0.055),
    "glove": (0.042, 0.030, 0.024), "sleeve": (0.130, 0.040, 0.030), "sleeve_light": (0.180, 0.060, 0.040),
    "rope": (0.380, 0.280, 0.160), "horn": (0.430, 0.360, 0.250), "fletch": (0.300, 0.250, 0.200),
    "bronze": (0.420, 0.230, 0.070), "bronze_dark": (0.200, 0.105, 0.035), "copper": (0.560, 0.200, 0.075),
    "rust": (0.300, 0.105, 0.040), "rust_light": (0.430, 0.185, 0.075), "rust_dark": (0.135, 0.070, 0.045),
    "worn": (0.125, 0.078, 0.050), "worn_dark": (0.072, 0.046, 0.030),
    "clay": (0.360, 0.125, 0.050), "clay_dark": (0.200, 0.072, 0.032), "rag": (0.440, 0.370, 0.250),
    "ember": (1.000, 0.300, 0.035), "frost": (0.500, 0.820, 1.000), "storm": (0.480, 0.160, 1.000),
    "trouser": (0.070, 0.060, 0.050),
}
def pack_weapon(src, length, roty, hand, grip, offhand=None, head=None, recolour=None, align=False, tip=None):
    """A pack weapon scaled to length (along +Y), turned roty about Y so its edge faces +Z, its grip at the origin.
    grip/offhand: ("mid",) the handle's middle, ("up", m) m above its bottom, ("down", m) m below its top."""
    ob = import_fbx(PACK, src)
    P = game_pts(ob)
    s = length / (max(p.y for p in P) - min(p.y for p in P))
    Hr = game_segs(ob, hand)
    h0, h1 = min(min(a.y, b.y) for a, b in Hr), max(max(a.y, b.y) for a, b in Hr)
    H = game_segs(ob, set(hand) | STEEL_HAND | {"DarkBrown", "Black"})

    def where(spec):
        if spec[0] == "mid":
            return (h0 + h1) / 2
        if spec[0] == "up":
            return h0 + spec[1] / s
        return h1 - spec[1] / s

    g = axis_at(H, where(grip))
    M = R('Y', roty) @ S(s) @ T(-g)
    if align:
        top = M @ axis_at(H, h1 - 0.04 / s)
        M = Vector((0, 1, 0)).rotation_difference(top.normalized()).inverted().to_matrix().to_4x4() @ M
    info = {"offhand": (M @ axis_at(H, where(offhand))) if offhand else None}
    bake_game(ob, M)
    P = game_pts(ob)
    info["tip"] = tip(P) if tip else max(P, key=lambda p: p.y)
    info["head"] = centroid(ob, head) if head else None
    remap(ob, recolour)
    adopt(ob)
    return info


def pack_bow(src, height, grip_mat, stretch=None):
    """A pack bow, its string taken off (its ends reported), the grip at the origin, the string side -Z.
    stretch keeps the grip and the bow's depth at that scale and stretches the limbs to the height."""
    ob = import_fbx(PACK, src)
    Sg = game_pts(ob, {"White"})
    ys = [p.y for p in Sg]

    def end(sel):
        pts = [p for p in Sg if sel(p.y)]
        return sum(pts, Vector()) / len(pts)
    top = end(lambda y: y > max(ys) - 1e-3)
    bot = end(lambda y: y < min(ys) + 1e-3)
    drop_mats(ob, {"White"})
    G = game_pts(ob, {grip_mat})
    g = Vector(((min(p.x for p in G) + max(p.x for p in G)) / 2, 0.0, (min(p.z for p in G) + max(p.z for p in G)) / 2))
    gtop = max(p.y for p in G)
    side = (max(p.z for p in G) - min(p.z for p in G)) / 2  # across the bow: this becomes -X
    rest = Vector((g.x, gtop, g.z + side))
    P = game_pts(ob)
    span = max(p.y for p in P) - min(p.y for p in P)
    M = R('Y', -90)
    if stretch:
        sl = (height / 2 - gtop * stretch) / (span / 2 - gtop)

        def f(p):
            y = p.y * stretch if abs(p.y) <= gtop else math.copysign(gtop * stretch + (abs(p.y) - gtop) * sl, p.y)
            return Vector((p.x * stretch, y, p.z * stretch))
        me = ob.data
        for vtx in me.vertices:
            vtx.co = GB @ f(BG @ vtx.co - g)
        me.normals_split_custom_set([(0.0, 0.0, 0.0)] * len(me.loops))  # plain normals again
        for p in me.polygons:
            p.use_smooth = True
        me.set_sharp_from_angle(angle=math.radians(40))
        bake_game(ob, M)
        st, sb, rest = M @ f(top - g), M @ f(bot - g), M @ f(rest - g)
    else:
        s = height / span
        M = M @ S(s) @ T(-g)
        bake_game(ob, M)
        st, sb, rest = M @ top, M @ bot, M @ rest
    P = game_pts(ob)
    front = max(p.z for p in P if abs(p.y - rest.y) < 0.03)
    remap(ob)
    adopt(ob)
    # The arrow lies along the bow's left (-X), for a right-handed archer, on the hand's top.
    rest = Vector((rest.x - 0.004, rest.y + 0.004, 0.0))
    return {"stringTop": st, "stringBottom": sb, "offhand": rest, "tip": Vector((rest.x, rest.y, front))}


def rpg_part(name, height, centre, mat, M=None, glow=False):
    """An Ultimate RPG item (a crystal) scaled to height, centred at centre, recoloured."""
    ob = import_fbx(RPG, name)
    P = game_pts(ob)
    lo = Vector((min(p.x for p in P), min(p.y for p in P), min(p.z for p in P)))
    hi = Vector((max(p.x for p in P), max(p.y for p in P), max(p.z for p in P)))
    s = height / (hi.y - lo.y)
    bake_game(ob, T(centre) @ (M or Matrix.Identity(4)) @ S(s) @ T(-(lo + hi) / 2))
    for sl in ob.material_slots:
        sl.material = MAT[mat]
    set_uvs(ob.data, glow)
    PARTS.append(ob)
    return ob


# ---------------------------------------------------------------- the pack's weapons

STEEL_HAND = {"LightWood", "DarkWood"}
RUST = {"Steel": "rust", "LightSteel": "rust_light", "DarkSteel": "rust_dark", "LightWood": "worn", "DarkWood": "worn_dark"}


def rusty_sword():
    return pack_weapon("Sword", 0.75, 90, STEEL_HAND, ("mid",), recolour=RUST)


def dagger():
    return pack_weapon("Dagger_2", 0.35, 90, STEEL_HAND, ("mid",))


def arming_sword():
    return pack_weapon("Sword_Golden", 0.95, 90, STEEL_HAND, ("mid",),
                       recolour={"Gold": "steel", "LightGold": "steel_light", "DarkSteel": "gold"})


def falchion():
    return pack_weapon("Sword_2", 0.85, 90, STEEL_HAND, ("mid",))


def longsword():
    return pack_weapon("Sword_Big", 1.15, 90, STEEL_HAND, ("up", 0.06), offhand=("down", 0.05))


def claymore():
    return pack_weapon("Claymore", 1.5, 90, {"DarkBrown", "Black"}, ("up", 0.065), offhand=("down", 0.06),
                       recolour={"LightRed": "steel_light", "Red": "steel", "DarkBrown": "wood", "Black": "wood_dark"})


def hand_axe():
    return pack_weapon("Axe_Small", 0.6, 90, STEEL_HAND, ("up", 0.085), head={"Steel", "LightSteel"}, align=True)


def bearded_axe():
    return pack_weapon("Axe", 0.8, 90, STEEL_HAND, ("up", 0.09), head={"Steel", "LightSteel"}, align=True)


def greataxe():
    return pack_weapon("Axe_Double", 1.3, 90, STEEL_HAND, ("up", 0.11), offhand=("up", 0.55),
                       head={"Steel", "LightSteel"}, align=True)


def war_hammer():
    return pack_weapon("Hammer_Small", 0.8, 90, {"DarkWood"}, ("up", 0.07), head={"Steel", "LightSteel"},
                       align=True)


def maul():
    return pack_weapon("Hammer_Double", 1.2, 90, {"DarkWood"}, ("up", 0.07), offhand=("up", 0.52),
                       head={"Steel", "LightSteel"}, align=True)


def scythe():
    info = pack_weapon("Scythe", 1.8, 0, STEEL_HAND, ("up", 0.40), offhand=("up", 1.0), align=True,
                       head={"Steel", "LightSteel"}, tip=lambda P: min(P, key=lambda p: p.x))
    return info


def spear():
    return pack_weapon("Spear", 2.0, 90, STEEL_HAND, ("up", 0.55), offhand=("up", 1.1), align=True)


def hunting_bow():
    return pack_bow("Bow_Wooden2", 1.1, "LightWood")


def longbow():
    return pack_bow("Bow_Wooden", 1.7, "LightWood", stretch=0.21)


def sun_bow():
    return pack_bow("Bow_Golden", 1.4, "LightWood")


def wraith_bow():
    return pack_bow("Bow_Evil", 1.4, "Black")


def arrow():
    ob = import_fbx(PACK, "Arrow")
    P = game_pts(ob)
    W = game_pts(ob, {"LightWood"})
    z0, z1 = min(p.z for p in P), max(p.z for p in P)
    s = 0.75 / (z1 - z0)
    cx = (min(p.x for p in W) + max(p.x for p in W)) / 2
    cy = (min(p.y for p in W) + max(p.y for p in W)) / 2
    bake_game(ob, S(s) @ T(-cx, -cy, -z0))
    remap(ob)
    adopt(ob)
    return {"tip": Vector((0, 0, 0.75))}


# ---------------------------------------------------------------- modelled melee

def mace():
    lathe([(0, -0.12), (0.016, -0.12), (0.024, -0.105), (0.022, -0.09), (0.017, -0.085)], "steel_dark", sides=8,
          smooth=False)
    grip_wraps(-0.088, 0.06, 0.0155, 5, seed=3)
    cyl((0, 0.055, 0), (0, 0.075, 0), 0.021, mat="steel_dark", sides=8, smooth=False)
    cyl((0, 0.07, 0), (0, 0.50, 0), 0.0135, 0.0125, mat="steel_dark", sides=8)
    cyl((0, 0.47, 0), (0, 0.60, 0), 0.026, 0.024, mat="steel_dark", sides=7, smooth=False, twist=90 / 7)
    for k in range(7):
        a = math.radians(90 + k * 360 / 7)
        u = Vector((math.cos(a), 0, math.sin(a)))
        slab([(0.018, 0.465), (0.040, 0.49), (0.060, 0.545), (0.056, 0.585), (0.034, 0.612), (0.018, 0.618)],
             (0, 0, 0), u, (0, 1, 0), 0.009, "steel", "steel_light", inset=0.003,
             sharp=[False, True, True, True, True, False])
    lathe([(0.024, 0.60), (0.018, 0.615), (0.008, 0.625), (0, 0.63)], "steel_dark", sides=7, smooth=False)
    return {"head": Vector((0, 0.545, 0))}


def spiked_ball(c, r, spike, base, mat="steel_dark", spike_mat="steel", n_up=True):
    ball(c, r, mat, seg=10, rings=7, smooth=True)
    phi = (1 + 5 ** 0.5) / 2
    raw = [(-1, phi, 0), (1, phi, 0), (-1, -phi, 0), (1, -phi, 0), (0, -1, phi), (0, 1, phi), (0, -1, -phi),
           (0, 1, -phi), (phi, 0, -1), (phi, 0, 1), (-phi, 0, -1), (-phi, 0, 1)]
    # Turn the icosahedron so one vertex points up (+Y) and one of its ring points +Z.
    up = Vector(raw[0]).normalized()
    Q = up.rotation_difference(Vector((0, 1, 0)))
    dirs = [Q @ Vector(d).normalized() for d in raw]
    ring = [d for d in dirs if 0.2 < d.y < 0.9]
    a0 = math.atan2(ring[0].x, ring[0].z)
    Q2 = Matrix.Rotation(-a0, 3, 'Y')
    dirs = [Q2 @ d for d in dirs]
    for d in dirs:
        if d.y < -0.9:
            continue
        cyl(Vector(c) + d * r * 0.85, Vector(c) + d * (r + spike), base, 0.0, mat=spike_mat, sides=4, smooth=False,
            twist=45)
    return dirs


def morning_star():
    lathe([(0, -0.125), (0.02, -0.125), (0.025, -0.11), (0.021, -0.095)], "steel_dark", sides=8, smooth=False)
    grip_wraps(-0.10, 0.08, 0.0175, 5, seed=5)
    cyl((0, 0.075, 0), (0, 0.58, 0), 0.018, 0.023, mat="wood_dark", sides=8, smooth=False)
    cyl((0, 0.50, 0), (0, 0.575, 0), 0.026, 0.028, mat="steel_dark", sides=8, smooth=False)
    c = (0, 0.625, 0)
    spiked_ball(c, 0.058, 0.048, 0.012)
    cyl((0, 0.62, 0), (0, 0.63, 0), 0.0605, mat="iron", sides=10, smooth=False)
    return {"head": Vector(c)}


def flail():
    lathe([(0, -0.125), (0.019, -0.125), (0.024, -0.11), (0.02, -0.098)], "steel_dark", sides=8, smooth=False)
    grip_wraps(-0.10, 0.08, 0.017, 5, seed=7)
    cyl((0, 0.075, 0), (0, 0.28, 0), 0.017, 0.02, mat="wood_dark", sides=8, smooth=False)
    cyl((0, 0.255, 0), (0, 0.295, 0), 0.023, 0.02, mat="steel_dark", sides=8, smooth=False)
    # the chain hangs straight on along +Y: an eye, six links (each a quarter turn from the last), an eye
    R_, w, k = 0.016, 0.0045, 1.55
    eye = 0.31
    torus((0, eye, 0), 0.012, w, "iron", seg=8, sides=4, M=R('Z', 90))
    y = eye + 0.012 - 2 * w + R_ * k
    for i in range(6):
        torus((0, y, 0), R_, w, "iron", seg=8, sides=4, M=R('Y', 90 * (i % 2)) @ S(1, k, 1) @ R('X', 90))
        top = y + R_ * k
        y += 2 * R_ * k - 2 * w
    ring = top - 2 * w + 0.012
    torus((0, ring, 0), 0.012, w, "iron", seg=8, sides=4, M=R('X', 90))
    c = Vector((0, ring + 0.012 + 0.045, 0))
    spiked_ball(c, 0.05, 0.036, 0.0105)
    return {"head": c}


def spiked_club():
    prof = [(0, -0.13), (0.017, -0.13), (0.021, -0.115), (0.019, -0.10), (0.020, 0.08), (0.028, 0.25), (0.040, 0.42),
            (0.050, 0.56), (0.052, 0.62), (0.044, 0.655), (0.022, 0.668), (0, 0.67)]
    lathe(prof, "wood_dark", sides=8, smooth=True, phase=0.2, angle=40)
    grip_wraps(-0.105, 0.07, 0.0195, 5, seed=9, core="wood_dark", bulge=1.18)
    cyl((0, 0.38, 0), (0, 0.40, 0), 0.0375, 0.0385, mat="iron", sides=8, smooth=False, twist=11)
    rnd = random.Random(4)

    def r_at(y):
        for (r0, y0), (r1, y1) in zip(prof, prof[1:]):
            if y0 <= y <= y1:
                return r0 + (r1 - r0) * (y - y0) / (y1 - y0)
        return 0.02
    for i, y in enumerate((0.44, 0.50, 0.56, 0.615)):
        for k in range(4):
            a = math.radians(90 + k * 90 + i * 45 + rnd.uniform(-8, 8))
            d = Vector((math.cos(a), 0.25, math.sin(a))).normalized()
            p = Vector((0, y, 0)) + Vector((math.cos(a), 0, math.sin(a))) * r_at(y) * 0.8
            cyl(p, p + d * 0.040, 0.0065, 0.0, mat="steel", sides=4, smooth=False)
    cyl((0, 0.66, 0), (0, 0.70, 0), 0.006, 0.0, mat="steel", sides=4, smooth=False)
    return {"head": Vector((0, 0.50, 0))}


def halberd():
    cyl((0, -0.52, 0), (0, 1.40, 0), 0.0165, mat="wood_dark", sides=8, smooth=False)
    lathe([(0, -0.55), (0.012, -0.535), (0.019, -0.51), (0.0185, -0.47), (0.017, -0.46)], "steel_dark", sides=8,
          smooth=False)
    grip_wraps(-0.065, 0.065, 0.017, 3, seed=11)
    grip_wraps(0.50, 0.62, 0.017, 3, seed=12)
    cyl((0, 1.15, 0), (0, 1.40, 0), 0.022, 0.020, mat="steel_dark", sides=8, smooth=False)
    for z in (-1, 1):
        box((0, 1.06, z * 0.019), (0.012, 0.20, 0.006), "steel_dark", bevel=0.002)
    slab([(0.012, 1.19), (0.06, 1.175), (0.125, 1.11), (0.165, 1.15), (0.192, 1.23), (0.198, 1.30), (0.188, 1.37),
          (0.160, 1.44), (0.10, 1.40), (0.05, 1.375), (0.012, 1.37)],
         (0, 0, 0), (0, 0, 1), (0, 1, 0), 0.010, "steel", "steel_light", inset=0.004,
         sharp=[False, False, True, True, True, True, True, True, False, False, False])
    slab([(-0.012, 1.315), (-0.06, 1.305), (-0.145, 1.25), (-0.06, 1.272), (-0.012, 1.262)],
         (0, 0, 0), (0, 0, 1), (0, 1, 0), 0.012, "steel", "steel_light", inset=0.0035,
         sharp=[False, True, True, True, False])
    cyl((0, 1.38, 0), (0, 1.55, 0), 0.016, 0.0, mat="steel", sides=4, smooth=False, twist=45)
    cyl((0, 1.37, 0), (0, 1.40, 0), 0.021, 0.017, mat="steel_dark", sides=8, smooth=False)
    return {"offhand": Vector((0, 0.56, 0)), "head": Vector((0, 1.30, 0.07)), "tip": Vector((0, 1.55, 0))}


def pitchfork():
    cyl((0, -0.43, 0), (0, 0.90, 0), 0.016, mat="wood_dark", sides=8, smooth=False)
    lathe([(0, -0.45), (0.014, -0.445), (0.0185, -0.42), (0.017, -0.405)], "iron", sides=8, smooth=False)
    grip_wraps(-0.06, 0.06, 0.0165, 3, seed=13)
    grip_wraps(0.44, 0.56, 0.0165, 3, seed=14)
    cyl((0, 0.84, 0), (0, 0.93, 0), 0.020, 0.018, mat="iron", sides=8, smooth=False)
    tube([(-0.082, 0.965, 0), (-0.06, 0.945, 0), (-0.03, 0.932, 0), (0, 0.928, 0), (0.03, 0.932, 0), (0.06, 0.945, 0),
          (0.082, 0.965, 0)], 0.0085, "iron", sides=6, smooth=True)
    for x in (-0.078, 0.0, 0.078):
        tube([(x, 0.93 if x == 0 else 0.955, 0), (x * 1.03, 1.02, 0.004), (x * 1.06, 1.10, 0.016), (x * 1.08, 1.15, 0.03)],
             [0.0075, 0.0065, 0.0045, 0.0], "iron", sides=6, smooth=True)
    return {"offhand": Vector((0, 0.50, 0)), "head": Vector((0, 1.04, 0.01)), "tip": Vector((0, 1.15, 0.03))}


# ---------------------------------------------------------------- staffs

def staff_body(seed):
    rnd = random.Random(seed)
    pts, radii = [], []
    for i in range(11):
        y = -0.88 + i * 0.145
        w = 0.0 if i in (0, 10) else 1.0
        pts.append((rnd.uniform(-0.004, 0.004) * w, y, rnd.uniform(-0.004, 0.004) * w))
        radii.append(0.0165 + 0.003 * i / 10)
    pts[6] = (0.0, pts[6][1], 0.0)
    tube(pts, radii, "wood_dark", sides=7, smooth=False)
    for y in (-0.42, 0.31):
        ball((rnd.uniform(-0.003, 0.003), y, 0.0), 0.021, "wood_dark", seg=7, rings=4, smooth=False, scale=(1, 1.6, 1))
    lathe([(0, -0.92), (0.010, -0.915), (0.0185, -0.89), (0.0185, -0.85), (0.017, -0.845)], "iron", sides=7,
          smooth=False)
    grip_wraps(-0.09, 0.09, 0.0185, 5, mat="leather_light", core="leather", seed=seed)


def staff_fire():
    staff_body(21)
    lathe([(0.021, 0.52), (0.027, 0.53), (0.027, 0.57), (0.032, 0.585), (0.020, 0.60)], "iron_dark", sides=8,
          smooth=False)
    for k in range(4):
        a = math.radians(45 + k * 90)
        d = Vector((math.cos(a), 0, math.sin(a)))
        pts = [Vector((0, y, 0)) + d * r for r, y in
               ((0.022, 0.575), (0.042, 0.61), (0.060, 0.66), (0.058, 0.72), (0.040, 0.765), (0.026, 0.785), (0.030, 0.80))]
        tube(pts, [0.0055, 0.0055, 0.0055, 0.005, 0.0045, 0.004, 0.0], "iron_dark", sides=5, smooth=True)
    lathe([(0, 0.605), (0.030, 0.612), (0.040, 0.63), (0.036, 0.636), (0, 0.628)], "iron_dark", sides=8, smooth=False)
    rpg_part("Crystal4", 0.095, (0, 0.682, 0), "ember", M=R('Y', 20) @ R('X', 15), glow=True)
    return {"muzzle": Vector((0, 0.682, 0)), "offhand": Vector((0, 0.32, 0))}


def staff_frost():
    staff_body(22)
    lathe([(0.021, 0.52), (0.026, 0.53), (0.026, 0.565), (0.031, 0.58), (0.022, 0.595)], "steel_light", sides=8,
          smooth=False)
    for k in range(3):
        a = math.radians(90 + k * 120)
        d = Vector((math.cos(a), 0, math.sin(a)))
        pts = [Vector((0, y, 0)) + d * r for r, y in ((0.020, 0.57), (0.036, 0.60), (0.040, 0.64), (0.030, 0.69), (0.024, 0.71))]
        tube(pts, [0.005, 0.005, 0.0045, 0.004, 0.0], "steel_light", sides=5, smooth=True)
    rpg_part("Crystal2", 0.21, (0, 0.69, 0), "frost", M=R('Y', 30), glow=True)
    rpg_part("Crystal3", 0.075, (0.034, 0.618, 0.008), "frost", M=R('Z', -38), glow=True)
    rpg_part("Crystal3", 0.06, (-0.030, 0.612, -0.016), "frost", M=R('Z', 34) @ R('X', -15), glow=True)
    return {"muzzle": Vector((0, 0.69, 0)), "offhand": Vector((0, 0.32, 0))}


def staff_storm():
    staff_body(23)
    pts = []
    for i in range(4 * 12 + 1):
        t = 2 * math.pi * i / 12
        pts.append((math.cos(t) * 0.0235, 0.40 + 0.115 * i / 48, math.sin(t) * 0.0235))
    tube(pts, 0.0042, "copper", sides=5, smooth=True, caps=True)
    lathe([(0.021, 0.52), (0.027, 0.53), (0.027, 0.565), (0.033, 0.58), (0.022, 0.595)], "copper", sides=8,
          smooth=False)
    for k in range(3):
        a = math.radians(90 + k * 120)
        d = Vector((math.cos(a), 0, math.sin(a)))
        pts = [Vector((0, y, 0)) + d * r for r, y in ((0.020, 0.57), (0.040, 0.61), (0.050, 0.67), (0.044, 0.73))]
        tube(pts, [0.0045, 0.0045, 0.004, 0.0035], "copper", sides=5, smooth=True)
        ball(Vector((0, 0.735, 0)) + d * 0.044, 0.008, "copper", seg=6, rings=4)
    rpg_part("Crystal1", 0.15, (0, 0.665, 0), "storm", M=R('Y', 15), glow=True)
    return {"muzzle": Vector((0, 0.665, 0)), "offhand": Vector((0, 0.32, 0))}


# ---------------------------------------------------------------- thrown

def javelin():
    cyl((0, -0.60, 0), (0, 0.60, 0), 0.0115, 0.012, mat="wood_dark", sides=7, smooth=False)
    cyl((0, -0.62, 0), (0, -0.59, 0), 0.004, 0.012, mat="iron", sides=6, smooth=False)
    grip_wraps(-0.065, 0.065, 0.0122, 4, mat="leather_light", core="leather", seed=31, bulge=1.25)
    cyl((0, 0.58, 0), (0, 0.645, 0), 0.0135, 0.007, mat="iron", sides=6, smooth=False)
    cyl((0, 0.64, 0), (0, 0.715, 0), 0.0045, mat="iron", sides=4, smooth=False, twist=45)
    lathe([(0.003, 0.712), (0.015, 0.735), (0.0, 0.79)], "steel", sides=4, smooth=False, phase=math.pi / 4)
    return {"tip": Vector((0, 0.79, 0))}


def throwing_axe():
    tube([(0, -0.085, -0.004), (0, 0.06, 0.0), (0, 0.20, -0.004), (0, 0.31, -0.012)], [0.0125, 0.0135, 0.014, 0.0145],
         "wood_dark", sides=7, smooth=False)
    grip_wraps(-0.07, 0.05, 0.0135, 4, seed=33)
    box((0, 0.27, -0.008), (0.026, 0.07, 0.042), "steel_dark", bevel=0.004)
    slab([(0.010, 0.245), (0.05, 0.235), (0.095, 0.19), (0.122, 0.215), (0.137, 0.27), (0.132, 0.315), (0.115, 0.338),
          (0.06, 0.305), (0.010, 0.297)],
         (0, 0, 0), (0, 0, 1), (0, 1, 0), 0.012, "steel", "steel_light", inset=0.0045,
         sharp=[False, False, True, True, True, True, True, False, False])
    return {"head": Vector((0, 0.27, 0.06)), "tip": Vector((0, 0.338, 0.115))}


def fire_pot():
    # a round clay pot with two dark bands, a cord round its neck and a rag stuffed in it
    prof = [(0, -0.072, 0), (0.045, -0.072, 0), (0.072, -0.05, 0), (0.085, -0.025, 0), (0.0885, -0.017, 1),
            (0.0895, -0.007, 0), (0.089, 0.012, 0), (0.0855, 0.021, 1), (0.082, 0.030, 0), (0.078, 0.04, 0),
            (0.055, 0.062, 0), (0.034, 0.072, 0), (0.031, 0.082, 0), (0.038, 0.088, 0), (0.036, 0.095, 0),
            (0.0, 0.095, 0)]
    lathe([(r, y) for r, y, _ in prof], sides=10, smooth=True, angle=40,
          mats=(["clay", "clay_dark"], [m for _, _, m in prof[:-1]]))
    torus((0, 0.077, 0), 0.0335, 0.0045, "rope", seg=10, sides=4)
    ico((0.002, 0.104, 0.0), 0.027, "rag", sub=1, scale=(1.0, 0.75, 1.0), jitter=0.004, seed=35)
    tube([(0, 0.11, 0), (0.006, 0.13, 0.003), (0.004, 0.148, -0.002), (0.014, 0.163, 0.0)], [0.014, 0.011, 0.007, 0.0],
         "rag", sides=5, smooth=False)
    for a, ln in ((20, 0.05), (200, 0.035)):
        d = Vector((math.cos(math.radians(a)), 0, math.sin(math.radians(a))))
        side = Vector((0, 1, 0)).cross(d)
        slab([(-0.012, 0.0), (0.012, 0.0), (0.009, -ln), (-0.004, -ln - 0.01)], d * 0.044 + Vector((0, 0.098, 0)),
             side, (0, 1, 0), 0.003, "rag")
    return {"tip": Vector((0.014, 0.163, 0.0))}


def lure_bell():
    lathe([(0, -0.07), (0.012, -0.07), (0.016, -0.062), (0.013, -0.05), (0.0125, 0.03), (0.016, 0.045),
           (0.012, 0.055), (0.009, 0.068)], "wood_dark", sides=8, smooth=True, angle=40)
    lathe([(0.0, 0.064), (0.022, 0.066), (0.034, 0.076), (0.040, 0.094), (0.042, 0.128), (0.047, 0.158),
           (0.057, 0.177), (0.061, 0.183), (0.053, 0.183), (0.044, 0.166), (0.036, 0.133), (0.034, 0.100),
           (0.027, 0.084), (0.0, 0.080)], "bronze", sides=12, smooth=True, angle=40)
    lathe([(0.0405, 0.104), (0.0425, 0.107), (0.0425, 0.113), (0.0405, 0.116)], "bronze_dark", sides=12, smooth=True)
    cyl((0, 0.08, 0), (0, 0.158, 0), 0.0025, mat="iron_dark", sides=4, smooth=False)
    ball((0, 0.163, 0), 0.0115, "iron_dark", seg=8, rings=5)
    cyl((0, 0.060, 0), (0, 0.070, 0), 0.0165, mat="bronze_dark", sides=8, smooth=False)
    return {"head": Vector((0, 0.13, 0)), "tip": Vector((0, 0.183, 0))}


# ---------------------------------------------------------------- crossbows and the handgonne

def stock(outline, width, mat="oak"):
    slab(outline, (0, 0, 0), (0, 0, 1), (0, 1, 0), width, mat, inset=0.006, bevel=True)


def prod(span, zc, zt, y, rc, rt, mat, sides=6, tips="horn"):
    pts, radii = [], []
    n = 9
    for i in range(n):
        x = -span + 2 * span * i / (n - 1)
        f = abs(x) / span
        pts.append((x, y, zc - (zc - zt) * f * f))
        radii.append(rc + (rt - rc) * f)
    tube(pts, radii, mat, sides=sides, smooth=True, caps=True, scale2=(1.0, 0.75))
    for x in (-span, span):
        ball((x, y, zt), rt * 1.5, tips, seg=6, rings=4, smooth=False)
    return Vector((-span, y, zt)), Vector((span, y, zt))


def rope_bind(z0, z1, y, w, h, n):
    for i in range(n):
        z = z0 + (z1 - z0) * (i + 0.5) / n
        box((0, y, z), (w, h, (z1 - z0) / n * 0.8), "rope", bevel=0.002)


def stirrup(z0, y, half, depth, r):
    tube([(-half, y, z0 - 0.01), (-half, y - 0.01, z0 + depth * 0.5), (-half * 0.6, y - 0.015, z0 + depth * 0.95),
          (0, y - 0.016, z0 + depth), (half * 0.6, y - 0.015, z0 + depth * 0.95), (half, y - 0.01, z0 + depth * 0.5),
          (half, y, z0 - 0.01)], r, "iron", sides=6, smooth=True)


def hand_crossbow():
    stock([(-0.08, 0.040), (0.08, 0.044), (0.34, 0.048), (0.37, 0.050), (0.37, 0.074), (0.10, 0.075), (-0.06, 0.075),
           (-0.08, 0.066)], 0.034, "oak")
    slab([(-0.022, -0.06), (0.020, -0.058), (0.026, 0.046), (-0.03, 0.046), (-0.034, 0.02)],
         (0, 0, 0), (0, 0, 1), (0, 1, 0), 0.030, "oak", inset=0.006, bevel=True)
    for y in (-0.03, 0.0, 0.025):
        box((0, y, 0.0), (0.033, 0.016, 0.050), "wood", bevel=0.003)
    box((0, 0.076, 0.21), (0.008, 0.003, 0.32), "wood_dark")
    cyl((-0.016, 0.081, 0.0), (0.016, 0.081, 0.0), 0.011, mat="horn", sides=8, smooth=True)
    tube([(0, 0.044, 0.042), (0, 0.026, 0.046), (0, 0.010, 0.040)], [0.0045, 0.004, 0.0], "iron", sides=5)
    for x in (-0.018, 0.018):
        box((x, 0.058, 0.0), (0.003, 0.03, 0.05), "iron", bevel=0.001)
    sl, sr = prod(0.20, 0.318, 0.285, 0.064, 0.010, 0.006, "wood_dark")
    rope_bind(0.30, 0.335, 0.062, 0.040, 0.030, 3)
    box((0, 0.062, 0.355), (0.040, 0.034, 0.028), "iron", bevel=0.003)
    nock = Vector((0, 0.081, 0.0))
    return {"stringLeft": sl, "stringRight": sr, "nock": nock, "boltTail": nock,
            "boltTip": nock + Vector((0, 0, 0.38)), "tip": Vector((0, 0.081, 0.37))}


def light_crossbow():
    stock([(-0.28, -0.075), (-0.10, -0.045), (-0.03, -0.031), (0.12, -0.028), (0.40, -0.022), (0.47, -0.018),
           (0.47, 0.030), (0.12, 0.030), (-0.06, 0.030), (-0.28, 0.030)], 0.044, "oak")
    box((0, 0.031, 0.29), (0.009, 0.003, 0.36), "wood_dark")
    cyl((-0.017, 0.033, 0.10), (0.017, 0.033, 0.10), 0.014, mat="horn", sides=8, smooth=True)
    for x in (-0.0235, 0.0235):
        box((x, 0.004, 0.08), (0.004, 0.05, 0.11), "iron", bevel=0.0015)
    tube([(0, -0.028, 0.07), (0, -0.05, 0.02), (0, -0.062, -0.06), (0, -0.064, -0.15)], [0.0055, 0.0055, 0.005, 0.0045],
         "iron", sides=5)
    box((0, -0.0225, -0.276), (0.048, 0.108, 0.012), "iron_dark", bevel=0.003)
    sl, sr = prod(0.32, 0.445, 0.395, 0.016, 0.0135, 0.0075, "wood")
    box((0, 0.001, 0.445), (0.050, 0.058, 0.04), "iron", bevel=0.004)
    rope_bind(0.40, 0.43, 0.004, 0.048, 0.056, 2)
    stirrup(0.465, 0.004, 0.045, 0.065, 0.0065)
    nock = Vector((0, 0.037, 0.10))
    return {"stringLeft": sl, "stringRight": sr, "nock": nock, "boltTail": nock,
            "boltTip": nock + Vector((0, 0, 0.38)), "offhand": Vector((0, -0.026, 0.27)),
            "tip": Vector((0, 0.037, 0.53))}


def arbalest():
    stock([(-0.40, -0.090), (-0.14, -0.050), (-0.03, -0.036), (0.14, -0.033), (0.46, -0.026), (0.52, -0.022),
           (0.52, 0.036), (0.14, 0.036), (-0.08, 0.036), (-0.40, 0.036)], 0.052, "oak")
    box((0, 0.037, 0.33), (0.010, 0.003, 0.36), "wood_dark")
    cyl((-0.02, 0.040, 0.14), (0.02, 0.040, 0.14), 0.016, mat="horn", sides=8, smooth=True)
    for x in (-0.0275, 0.0275):
        box((x, 0.004, 0.12), (0.004, 0.058, 0.13), "iron", bevel=0.0015)
    tube([(0, -0.034, 0.10), (0, -0.058, 0.04), (0, -0.07, -0.06), (0, -0.072, -0.17)], [0.006, 0.006, 0.0055, 0.005],
         "iron", sides=5)
    for z in (0.20, 0.42):
        box((0, 0.002, z), (0.058, 0.070, 0.018), "iron", bevel=0.003)
    sl, sr = prod(0.38, 0.505, 0.445, 0.018, 0.017, 0.0095, "steel_dark", sides=6, tips="iron")
    box((0, 0.0, 0.505), (0.064, 0.070, 0.05), "iron", bevel=0.005)
    rope_bind(0.48, 0.53, 0.0, 0.066, 0.072, 2)
    stirrup(0.53, 0.008, 0.055, 0.07, 0.008)
    # the windlass on the butt: a frame, a drum of wound cord, two cranks
    for x in (-0.034, 0.034):
        box((x, -0.012, -0.375), (0.010, 0.07, 0.07), "iron", bevel=0.003)
    cyl((-0.034, -0.012, -0.385), (0.034, -0.012, -0.385), 0.022, mat="rope", sides=8, smooth=True)
    cyl((-0.11, -0.012, -0.385), (0.11, -0.012, -0.385), 0.008, mat="iron_dark", sides=6, smooth=True)
    for sgn in (-1, 1):
        box((sgn * 0.105, -0.05, -0.385), (0.008, 0.085, 0.016), "iron_dark", bevel=0.002)
        cyl((sgn * 0.105, -0.088, -0.385), (sgn * 0.15, -0.088, -0.385), 0.009, mat="wood_dark", sides=6, smooth=True)
    cyl((-0.026, 0.0, -0.42), (0.026, 0.0, -0.42), 0.010, mat="iron", sides=6, smooth=True)
    nock = Vector((0, 0.043, 0.14))
    return {"stringLeft": sl, "stringRight": sr, "nock": nock, "boltTail": nock,
            "boltTip": nock + Vector((0, 0, 0.38)), "offhand": Vector((0, -0.032, 0.31)),
            "tip": Vector((0, 0.043, 0.60))}


def repeater():
    stock([(-0.30, -0.035), (-0.24, -0.045), (-0.05, -0.034), (0.20, -0.030), (0.50, -0.026), (0.52, -0.020),
           (0.52, 0.026), (0.30, 0.026), (-0.26, 0.026), (-0.30, 0.012)], 0.040, "oak")
    # the magazine: a box of bolts that slides on the stock, the lever pulling it back
    box((0, 0.076, 0.255), (0.040, 0.098, 0.42), "oak", bevel=0.005)
    box((0, 0.127, 0.255), (0.028, 0.006, 0.40), "wood_dark", bevel=0.002)
    for z in (0.07, 0.25, 0.44):
        box((0, 0.076, z), (0.044, 0.102, 0.016), "iron", bevel=0.002)
    sl, sr = prod(0.30, 0.475, 0.425, 0.012, 0.012, 0.007, "wood")
    rope_bind(0.45, 0.50, 0.004, 0.046, 0.046, 2)
    for x in (-0.027, 0.027):
        tube([(x, 0.012, 0.50), (x, 0.11, 0.30), (x, 0.17, 0.06), (x, 0.185, -0.02)], 0.0055, "iron", sides=5)
        cyl((x, 0.012, 0.50), (x * 1.5, 0.012, 0.50), 0.007, mat="iron_dark", sides=6)
    cyl((-0.045, 0.185, -0.02), (0.045, 0.185, -0.02), 0.011, mat="wood_dark", sides=7, smooth=True)
    for x in (-0.024, 0.024):
        box((x, 0.150, 0.06), (0.006, 0.05, 0.012), "iron_dark", bevel=0.0015)
    nock = Vector((0, 0.034, 0.06))
    return {"stringLeft": sl, "stringRight": sr, "nock": nock, "boltTail": nock,
            "boltTip": nock + Vector((0, 0, 0.38)), "offhand": Vector((0, 0.185, -0.02)),
            "tip": Vector((0, 0.034, 0.52))}


def handgonne():
    zt = 0.10  # the tube's breech; the hand holds the pole behind it
    cyl((0, 0, -0.62), (0, 0, zt + 0.02), 0.022, 0.025, mat="wood_dark", sides=8, smooth=False)
    lathe([(0, -0.625), (0.016, -0.625), (0.025, -0.61), (0.025, -0.575), (0.023, -0.57)], "iron_dark", sides=8,
          smooth=False, M=R('X', 90))
    # a long iron tube flaring to the muzzle, its bore wide enough for the cannonball
    lathe([(0, zt - 0.005), (0.036, zt - 0.005), (0.048, zt + 0.025), (0.056, zt + 0.06), (0.068, zt + 0.32),
           (0.080, zt + 0.335), (0.084, zt + 0.38), (0.066, zt + 0.38), (0.065, zt + 0.16), (0.0, zt + 0.16)],
          "iron", sides=10, smooth=True, M=R('X', 90), angle=40)
    for dz, r in ((0.05, 0.058), (0.15, 0.0625), (0.25, 0.067)):
        cyl((0, 0, zt + dz - 0.011), (0, 0, zt + dz + 0.011), r, mat="iron_dark", sides=10, smooth=True)
    for sgn in (-1, 1):
        box((0, sgn * 0.024, zt - 0.05), (0.016, 0.006, 0.12), "iron_dark", bevel=0.002)
    for z in (zt - 0.10, zt - 0.03):
        cyl((0, 0, z - 0.006), (0, 0, z + 0.006), 0.0275, mat="iron_dark", sides=8)
    touch = Vector((0, 0.056, zt + 0.09))
    cyl(touch - Vector((0, 0.006, 0)), touch + Vector((0, 0.006, 0)), 0.0085, mat="iron_dark", sides=6)
    box(touch + Vector((0.012, -0.002, 0.0)), (0.016, 0.006, 0.018), "iron_dark", bevel=0.002)
    return {"muzzle": Vector((0, 0, zt + 0.38)), "tip": Vector((0, 0, zt + 0.38)), "touchhole": touch,
            "offhand": Vector((0, -0.068, zt + 0.22))}


# ---------------------------------------------------------------- projectiles

def bolt():
    cyl((0, 0, 0.0), (0, 0, 0.33), 0.0058, mat="wood", sides=6, smooth=False)
    cyl((0, 0, 0.30), (0, 0, 0.335), 0.0062, 0.0085, mat="iron", sides=4, smooth=False, twist=45)
    cyl((0, 0, 0.335), (0, 0, 0.38), 0.0085, 0.0, mat="steel", sides=4, smooth=False, twist=45)
    for a in (0, 180):
        slab([(0.0, 0.004), (0.0, 0.075), (0.016, 0.06), (0.018, 0.012)], (0, 0, 0),
             (R('Z', a) @ Vector((0, 1, 0))), (0, 0, 1), 0.002, "fletch")
    return {"tip": Vector((0, 0, 0.38))}


def cannonball():
    ball((0, 0, 0), 0.06, "iron", seg=12, rings=8, smooth=True)
    return {"tip": Vector((0, 0, 0.06))}


# ---------------------------------------------------------------- hands

RH = 0.0175  # the handle's radius the fist closes on


def fist(mirror=False):
    m = -1.0 if mirror else 1.0

    def P(x, y, z):
        return Vector((x * m, y, z))

    def polar(r, deg, y):
        a = math.radians(deg)
        return P(math.cos(a) * r, y, math.sin(a) * r)

    # back of the hand: the metacarpals along +Z, the back toward +X, the palm on the handle
    box(P(0.034, 0.0, -0.018), (0.044, 0.092, 0.10), "glove", bevel=0.010)
    ball(P(0.022, 0.012, -0.040), 0.024, "glove", seg=8, rings=5, scale=(0.75, 1.15, 1.0))  # heel of the thumb
    # four fingers wrapped round the handle, index on top
    for i, (y, rf) in enumerate(((0.0315, 0.0108), (0.0105, 0.0108), (-0.0105, 0.0104), (-0.031, 0.0095))):
        path = [polar(0.042, 22, y), polar(0.0445, 42, y), polar(0.038, 70, y), polar(0.0315, 100, y),
                polar(0.029, 135, y), polar(0.0285, 168, y), polar(0.028, 198, y), polar(0.027, 222, y)]
        tube(path, [rf, rf * 1.05, rf, rf * 0.98, rf * 0.95, rf * 0.93, rf * 0.9, rf * 0.75], "glove", sides=7,
             smooth=True)
        ball(polar(0.0445, 42, y), rf * 1.08, "glove", seg=7, rings=5)  # knuckle
    # the thumb, from its heel up and over the index finger
    tube([P(0.024, 0.026, -0.042), P(0.010, 0.044, -0.042), P(-0.010, 0.051, -0.030), P(-0.024, 0.050, -0.010),
          P(-0.027, 0.047, 0.008), P(-0.025, 0.044, 0.016)], [0.0125, 0.012, 0.0115, 0.011, 0.010, 0.007], "glove",
         sides=7, smooth=True)
    # plates: the back of the hand and a bar over the knuckles
    box(P(0.058, 0.0, -0.022), (0.008, 0.080, 0.078), "steel", bevel=0.0025, M=R('Y', -8 * m))
    tube([P(0.050, -0.040, 0.030), P(0.054, -0.013, 0.035), P(0.054, 0.013, 0.035), P(0.050, 0.040, 0.030)], 0.0085,
         "steel_dark", sides=6, smooth=True)
    for y in (-0.03, 0.03):
        for z in (-0.054, 0.010):
            ball(P(0.0625, y, z), 0.0035, "steel_dark", seg=6, rings=3, smooth=False)
    # the cuff over the wrist
    c = P(0.034, 0.0, 0.0)
    lathe([(0.0, -0.055), (0.040, -0.055), (0.046, -0.07), (0.052, -0.098), (0.047, -0.10), (0.0, -0.10)],
          "glove", sides=9, smooth=False, M=T(c) @ R('X', 90) @ S(0.88, 1.0, 1.04))
    wrist = P(0.034, 0.0, -0.07)
    return {"tip": P(0.04, 0.0, 0.045), "wrist": wrist, "forearmDir": Vector((0, 0, -1))}


def fist_r():
    return fist(False)


def fist_l():
    return fist(True)


def forearm():
    # the arm along -Y from the wrist; the back of the forearm toward +X, as the fists' backs
    Mx = S(0.9, 1.0, 1.0)
    lathe([(0.0, 0.0), (0.044, 0.0), (0.046, -0.15), (0.050, -0.17), (0.055, -0.28), (0.0, -0.28)], "sleeve", sides=10,
          smooth=True,
          M=Mx, angle=40)
    # the bracer
    lathe([(0.0, 0.004), (0.046, 0.004), (0.049, -0.006), (0.0495, -0.15), (0.055, -0.16), (0.053, -0.168),
           (0.0, -0.168)], "leather", sides=10, smooth=True, M=Mx, angle=40)
    for y in (-0.045, -0.115):
        lathe([(0.0, y - 0.009), (0.0515, y - 0.009), (0.0515, y + 0.009), (0.0, y + 0.009)], "leather_light", sides=10,
              smooth=False, M=Mx)
        box((-0.0475, y, 0.0), (0.008, 0.024, 0.020), "steel_dark", bevel=0.002)
    box((0.0465, -0.08, 0.0), (0.010, 0.13, 0.05), "steel", bevel=0.003)
    for y in (-0.03, -0.13):
        ball((0.0525, y, 0.0), 0.0035, "steel_dark", seg=6, rings=3, smooth=False)
    # the sleeve's rolled cuff above the bracer
    torus((0, -0.18, 0), 0.054, 0.008, "sleeve_light", seg=10, sides=5, M=S(0.9, 1.0, 1.0))
    return {"tip": Vector((0, -0.28, 0)), "elbow": Vector((0, -0.28, 0))}


def kick_leg():
    """A booted shin for the first-person kick: the knee at the origin, the shin down -Y, the toe toward +Z and
    the sole at the bottom, toward -Y."""
    lathe([(0.0, 0.05), (0.064, 0.05), (0.073, 0.0), (0.067, -0.12), (0.058, -0.22), (0.0, -0.22)], "trouser",
          sides=10, smooth=True, angle=40)
    lathe([(0.0, -0.17), (0.063, -0.17), (0.068, -0.19), (0.061, -0.30), (0.056, -0.42), (0.0, -0.42)], "leather",
          sides=10, smooth=True, angle=40)
    torus((0, -0.18, 0), 0.066, 0.013, "leather_light", seg=10, sides=5)
    for y in (-0.25, -0.31, -0.37):
        box((0, y, 0.058), (0.05, 0.008, 0.012), "worn_dark", bevel=0.002)
    # the foot: the instep and toe, the heel, the sole
    box((0, -0.452, 0.045), (0.104, 0.076, 0.23), "leather", bevel=0.03)
    ball((0, -0.462, 0.152), 0.054, "leather", seg=8, rings=5, scale=(1.0, 0.75, 1.1))
    box((0, -0.502, 0.05), (0.112, 0.022, 0.29), "black", bevel=0.006)
    box((0, -0.487, -0.06), (0.10, 0.04, 0.07), "black", bevel=0.006)
    return {"tip": Vector((0, -0.5, 0.2))}


# ---------------------------------------------------------------- the list

WEAPONS = [
    ("rusty_sword", "melee", rusty_sword), ("dagger", "melee", dagger), ("arming_sword", "melee", arming_sword),
    ("falchion", "melee", falchion), ("longsword", "melee", longsword), ("claymore", "melee", claymore),
    ("hand_axe", "melee", hand_axe), ("bearded_axe", "melee", bearded_axe), ("greataxe", "melee", greataxe),
    ("war_hammer", "melee", war_hammer), ("maul", "melee", maul), ("mace", "melee", mace),
    ("morning_star", "melee", morning_star), ("flail", "melee", flail), ("spear", "melee", spear),
    ("halberd", "melee", halberd), ("scythe", "melee", scythe), ("pitchfork", "melee", pitchfork),
    ("spiked_club", "melee", spiked_club),
    ("hunting_bow", "bow", hunting_bow), ("longbow", "bow", longbow), ("sun_bow", "bow", sun_bow),
    ("wraith_bow", "bow", wraith_bow),
    ("hand_crossbow", "crossbow", hand_crossbow), ("light_crossbow", "crossbow", light_crossbow),
    ("arbalest", "crossbow", arbalest), ("repeater", "crossbow", repeater), ("handgonne", "gun", handgonne),
    ("staff_fire", "staff", staff_fire), ("staff_frost", "staff", staff_frost), ("staff_storm", "staff", staff_storm),
    ("javelin", "thrown", javelin), ("throwing_axe", "thrown", throwing_axe), ("fire_pot", "thrown", fire_pot),
    ("lure_bell", "thrown", lure_bell),
    ("arrow", "projectile", arrow), ("bolt", "projectile", bolt), ("cannonball", "projectile", cannonball),
    ("fist_r", "hand", fist_r), ("fist_l", "hand", fist_l), ("forearm", "hand", forearm), ("kick_leg", "hand", kick_leg),
]


def finish(name, kind, info):
    ob, path = export(name, OUT)
    lo, hi = game_bounds(ob)
    size = hi - lo
    length = size.z if kind in ("crossbow", "gun", "projectile") else size.y
    if name == "cannonball":
        length = size.x

    if info.get("tip") is None:  # the far end on the weapon's line
        info["tip"] = Vector((0, 0, hi.z)) if kind in ("crossbow", "gun", "projectile") else Vector((0, hi.y, 0))

    def r(v):
        return None if v is None else [round(float(c), 4) + 0.0 for c in v]
    out = {"name": name, "file": name + ".glb", "kind": kind, "length": round(length, 3),
           "tip": r(info.get("tip")), "offhand": r(info.get("offhand")), "head": r(info.get("head"))}
    for k in ("stringTop", "stringBottom", "stringLeft", "stringRight", "nock", "boltTail", "boltTip", "muzzle",
              "touchhole", "wrist", "forearmDir", "elbow"):
        if k in info:
            out[k] = r(info[k])
    out["tris"] = glb_tris(path)
    out["bounds"] = [r(lo), r(hi)]
    print(f"BUILT {name}: {out['tris']} tris, length {out['length']}")
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    jpath = os.path.join(OUT, "weapons.json")
    old = {}
    if ONLY and os.path.exists(jpath):
        old = {e["name"]: e for e in json.load(open(jpath))}
    entries = []
    for name, kind, fn in WEAPONS:
        if ONLY and name not in ONLY:
            if name in old:
                entries.append(old[name])
            continue
        new_scene(PAL)
        entries.append(finish(name, kind, fn()))
    with open(jpath, "w", newline="\n") as f:
        f.write("[\n" + ",\n".join("  " + json.dumps(e) for e in entries) + "\n]\n")


main()
