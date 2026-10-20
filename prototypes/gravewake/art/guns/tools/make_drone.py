# GRAVEWAKE's attack drone, the Engineer's special: modelled from primitives in the guns' colours, written beside them as
# drone.glb, and its line in guns.json put in (or replaced).
#
#   blender -b --factory-startup --python make_drone.py -- --out <guns dir>
#
# Axes as make_guns.py's: +X right, +Y up, +Z forward, metres; its middle at the origin. A squat armoured body, four arms
# out to motor pods (the rotors are drawn turning in the game, held.tide's DrawTurret, at ROTORS below), a gun slung
# under its nose, a red eye that glows, skids under it.
import sys, os, json

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "weapons", "tools"))
from lowpoly import *  # noqa: E402,F401,F403

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[argv.index("--out") + 1] if "--out" in argv else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

PAL = {
    "black": (0.040, 0.040, 0.045), "gunmetal": (0.150, 0.152, 0.160), "metal": (0.270, 0.275, 0.290),
    "olive": (0.220, 0.240, 0.140), "olive_dark": (0.130, 0.145, 0.085), "hazard": (0.700, 0.520, 0.080),
    "eye": (1.000, 0.120, 0.060),
}

ARM = 0.34          # How far out each motor is, along each diagonal (held.tide's ROTOR_OUT)


def build():
    # The body: an armoured box, a darker belly, a hazard stripe across its back
    box((0, 0, 0), (0.30, 0.11, 0.38), "olive", bevel=0.02)
    box((0, -0.065, 0.0), (0.24, 0.04, 0.30), "olive_dark", bevel=0.01)
    box((0, 0.058, -0.08), (0.31, 0.012, 0.06), "hazard")
    # Four arms out to the motors, and a pod on each
    for sx in (-1, 1):
        for sz in (-1, 1):
            cyl((sx * 0.1, 0.01, sz * 0.1), (sx * ARM, 0.02, sz * ARM), 0.022, mat="gunmetal", sides=6)
            cyl((sx * ARM, -0.02, sz * ARM), (sx * ARM, 0.05, sz * ARM), 0.05, mat="metal", sides=8)
            cyl((sx * ARM, 0.05, sz * ARM), (sx * ARM, 0.07, sz * ARM), 0.015, mat="black", sides=6)
    # The gun under its nose: a receiver and a barrel forward
    box((0, -0.11, 0.08), (0.06, 0.06, 0.22), "gunmetal", bevel=0.008)
    cyl((0, -0.11, 0.19), (0, -0.11, 0.36), 0.016, mat="black", sides=6)
    box((0, -0.15, 0.05), (0.03, 0.05, 0.06), "black", bevel=0.004)
    # Its eye, glowing
    ball((0, 0.0, 0.195), 0.035, mat="eye", seg=6, rings=4, glow=True)
    # Skids
    for sx in (-1, 1):
        box((sx * 0.11, -0.13, 0), (0.018, 0.018, 0.36), "black")
        box((sx * 0.11, -0.1, 0.1), (0.014, 0.06, 0.014), "black")
        box((sx * 0.11, -0.1, -0.1), (0.014, 0.06, 0.014), "black")


def main():
    new_scene(PAL)
    build()
    ob, path = export("drone", OUT)
    lo, hi = game_bounds(ob)
    info = {"name": "drone", "file": "drone.glb", "length": round(hi.z - lo.z, 3), "muzzle": [0.0, -0.11, 0.36], "tris": glb_tris(path),
            "bounds": [[round(v, 4) for v in lo], [round(v, 4) for v in hi]]}
    listing = os.path.join(OUT, "guns.json")
    entries = json.load(open(listing)) if os.path.exists(listing) else []
    entries = [e for e in entries if e["name"] != "drone"] + [info]
    with open(listing, "w", newline="\n") as f:
        f.write("[\n" + ",\n".join("  " + json.dumps(e) for e in entries) + "\n]\n")


main()
