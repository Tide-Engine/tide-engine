# GRAVEWAKE's flamethrower, the Engineer's gun: modelled from primitives (the pack has none) in the guns' colours,
# written beside them as flamer.glb, and its line in guns.json put in (or replaced).
#
#   blender -b --factory-startup --python make_flamer.py -- --out <guns dir>
#
# Axes and origin as make_guns.py's: +X right, +Y up, +Z forward, metres; the trigger hand (the middle of the pistol
# grip, a hand's width under the bore) at the origin, the nozzle along +Z. A long steel body over a red fuel tank, a
# hose from the tank's back up into the body, a perforated heat shield round the barrel, a flared nozzle with a pilot
# light that glows, a fore grip under the shield and a short stock.
import sys, os, json, math

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "weapons", "tools"))
from lowpoly import *  # noqa: E402,F401,F403

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[argv.index("--out") + 1] if "--out" in argv else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

# make_guns.py's palette, and the tank's red, the fittings' brass and the pilot light
PAL = {
    "black": (0.040, 0.040, 0.045), "black_soft": (0.075, 0.078, 0.085), "gunmetal": (0.150, 0.152, 0.160),
    "metal": (0.270, 0.275, 0.290), "metal_light": (0.420, 0.430, 0.450), "polymer": (0.110, 0.115, 0.105),
    "olive": (0.180, 0.200, 0.120), "red": (0.520, 0.070, 0.045), "red_dark": (0.300, 0.040, 0.030),
    "brass": (0.560, 0.420, 0.150), "pilot": (1.000, 0.550, 0.150),
}

BORE = 0.10      # The bore's height over the trigger hand
LENGTH = 0.98


def build():
    # The body: a squared receiver from the stock to the barrel
    box((0, BORE, -0.04), (0.06, 0.075, 0.42), "gunmetal", bevel=0.008)
    box((0, BORE + 0.045, 0.02), (0.035, 0.02, 0.26), "black_soft", bevel=0.004)          # a rail along its top
    # The barrel, its heat shield (a fatter tube with slots of black), the nozzle flaring out
    cyl((0, BORE, 0.17), (0, BORE, 0.58), 0.022, mat="metal", sides=8)
    cyl((0, BORE, 0.20), (0, BORE, 0.50), 0.038, mat="gunmetal", sides=8)
    for k in range(4):
        z = 0.235 + k * 0.07
        cyl((0, BORE, z), (0, BORE, z + 0.03), 0.0395, mat="black", sides=8)
    cyl((0, BORE, 0.58), (0, BORE, 0.63), 0.024, 0.036, mat="metal_light", sides=8)
    cyl((0, BORE, 0.63), (0, BORE, 0.645), 0.036, 0.03, mat="black", sides=8)
    # The pilot light under the nozzle, burning
    cyl((0, BORE - 0.035, 0.50), (0, BORE - 0.035, 0.60), 0.008, mat="brass", sides=6)
    ball((0, BORE - 0.035, 0.612), 0.014, mat="pilot", seg=6, rings=4, glow=True)
    # The fuel tank under the body, ahead of the grip: red, its caps darker, a band of brass and a valve in front
    TANK = BORE - 0.09
    cyl((0, TANK, 0.10), (0, TANK, 0.33), 0.05, mat="red", sides=10)
    cyl((0, TANK, 0.33), (0, TANK, 0.355), 0.05, 0.03, mat="red_dark", sides=10)
    cyl((0, TANK, 0.075), (0, TANK, 0.10), 0.03, 0.05, mat="red_dark", sides=10)
    cyl((0, TANK, 0.20), (0, TANK, 0.22), 0.053, mat="brass", sides=10)
    cyl((0, TANK, 0.355), (0, TANK, 0.37), 0.014, mat="brass", sides=6)
    # The hose: from the valve, round and up the right side into the barrel's root
    tube([(0, TANK, 0.37), (0.02, TANK + 0.005, 0.395), (0.04, TANK + 0.04, 0.39), (0.045, BORE - 0.01, 0.2), (0.03, BORE, 0.16)],
         [0.01, 0.01, 0.01, 0.01, 0.01], mat="black", sides=6)
    # The pistol grip and its trigger guard
    box((0, -0.005, -0.01), (0.034, 0.13, 0.046), "polymer", bevel=0.007, M=R('X', 14))
    box((0, 0.05, 0.045), (0.012, 0.012, 0.06), "black", bevel=0.002)
    # The fore grip, under the heat shield (the left hand's)
    box((0, BORE - 0.065, 0.44), (0.028, 0.09, 0.032), "polymer", bevel=0.005)
    # A short stock
    box((0, BORE - 0.005, -0.30), (0.04, 0.06, 0.14), "polymer", bevel=0.008)
    box((0, BORE - 0.01, -0.37), (0.045, 0.085, 0.02), "black_soft", bevel=0.004)


def main():
    os.makedirs(OUT, exist_ok=True)
    new_scene(PAL)
    build()
    ob, path = export("flamer", OUT)
    lo, hi = game_bounds(ob)
    info = {"name": "flamer", "file": "flamer.glb", "length": LENGTH, "muzzle": [0.0, BORE, 0.645], "tris": glb_tris(path),
            "bounds": [[round(v, 4) for v in lo], [round(v, 4) for v in hi]]}
    listing = os.path.join(OUT, "guns.json")
    entries = json.load(open(listing)) if os.path.exists(listing) else []
    entries = [e for e in entries if e["name"] != "flamer"] + [info]
    with open(listing, "w", newline="\n") as f:
        f.write("[\n" + ",\n".join("  " + json.dumps(e) for e in entries) + "\n]\n")


main()
