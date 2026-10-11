"""Underfoot's pictures: a crypt floor over a pit, cut from flat colour.

The look has four rules, and every sprite keeps them:
  - flat colour only: no gradients, no gloss, no blur, no outlines;
  - everything has height. The view is from straight above, tipped a little to
    the south, so whatever stands up shows its south face under its top, and
    casts one hard shadow down and to the right, away from a light in the
    north-west;
  - what is set in the floor is plain grey stone, and what can be carried is
    marked: a loose slab is bone white with a red eye carved in it;
  - a short, cold palette (stone, bone, iron, pit black) with three warm or
    sick accents: blood red, candle orange, a portal's green.

Nothing is drawn by hand and nothing is borrowed: each sprite is shapes given
as distances and filled. Run from any directory; it writes atlas.png beside
itself. Needs numpy and Pillow.

A sprite is 288 pixels square around a cell of 192. The atlas is six across
and four down, in the order of SPRITES; paint.tide's `Art` enum is that order.
"""
from pathlib import Path

import numpy as np
from PIL import Image

DEST = Path(__file__).resolve().parent
TILE = 192
CELL = 288
SS = 3
N = CELL * SS
PX = 1.0 / (TILE * SS)
ys, xs = np.mgrid[0:N, 0:N]
U = (xs + 0.5) * PX - CELL / (2 * TILE)
V = (ys + 0.5) * PX - CELL / (2 * TILE)

STONE = (0.46, 0.49, 0.50)
STONE_B = (0.43, 0.46, 0.48)
STONE_CRACK = (0.27, 0.29, 0.31)
BONE = (0.89, 0.86, 0.77)
BONE_SIDE = (0.55, 0.52, 0.47)
BLOOD = (0.66, 0.12, 0.14)
FROST = (0.74, 0.86, 0.90)
FROST_SET = (0.58, 0.72, 0.78)
FROST_SIDE = (0.38, 0.52, 0.60)
SLATE = (0.25, 0.21, 0.32)
SLATE_SET = (0.21, 0.18, 0.27)
SLATE_SIDE = (0.13, 0.11, 0.18)
INK = (0.05, 0.04, 0.08)
WOOD = (0.44, 0.29, 0.19)
WOOD_SIDE = (0.27, 0.17, 0.11)
WOOD_SUNK = (0.30, 0.23, 0.19)
PALE = (0.78, 0.72, 0.55)
IRON = (0.30, 0.30, 0.36)
CANDLE = (1.0, 0.70, 0.28)
EMBER = (0.74, 0.22, 0.20)
TIDE = (0.15, 0.52, 0.58)
SHADOW = 0.50
CAST = (0.06, 0.075)


def rbox(cx, cy, hw, hh, r=0.0):
    qx = np.abs(U - cx) - hw + r
    qy = np.abs(V - cy) - hh + r
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r


def ellipse(cx, cy, rx, ry):
    return (np.hypot((U - cx) / rx, (V - cy) / ry) - 1) * min(rx, ry)


def segment(ax, ay, bx, by, r):
    px, py, dx, dy = U - ax, V - ay, bx - ax, by - ay
    h = np.clip((px * dx + py * dy) / (dx * dx + dy * dy), 0, 1)
    return np.hypot(px - dx * h, py - dy * h) - r


def cover(d):
    return np.clip(0.5 - d / PX, 0, 1)


class Canvas:
    def __init__(self):
        self.rgb = np.zeros((N, N, 3))
        self.a = np.zeros((N, N))

    def fill(self, color, d, alpha=1.0):
        a = cover(d) * alpha
        self.rgb = self.rgb * (1 - a[..., None]) + np.asarray(color, dtype=np.float64) * a[..., None]
        self.a = self.a * (1 - a) + a

    def shadow(self, d, reach=1.0):
        """The one shadow there is: the same shape, down and to the right, flat."""
        shifted = np.roll(d, (int(round(CAST[1] * reach / PX)), int(round(CAST[0] * reach / PX))), axis=(0, 1))
        self.fill((0, 0, 0), shifted, SHADOW)

    def image(self):
        a = self.a[..., None]
        straight = np.where(a > 1e-6, self.rgb / np.maximum(a, 1e-6), 0)
        out = np.concatenate([straight, a], axis=2)
        img = Image.fromarray((np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA")
        return img.resize((CELL, CELL), Image.LANCZOS)


HALF = 0.465
THICK = 0.14


def paving(color, cracked=False):
    """Ground set in the floor: a plain flagstone, flush. The joints are the game's."""
    c = Canvas()
    c.fill(color, rbox(0, 0, HALF, HALF, 0.03))
    if cracked:
        inside = rbox(0, 0, HALF, HALF, 0.03)
        for a, b in (((-0.47, -0.18), (-0.12, -0.05)), ((-0.12, -0.05), (0.02, 0.20)), ((0.02, 0.20), (0.30, 0.47)), ((-0.12, -0.05), (0.10, -0.22))):
            c.fill(STONE_CRACK, np.maximum(segment(a[0], a[1], b[0], b[1], 0.011), inside))
    return c


def slab(top, side):
    """Loose ground: a thick slab standing proud of the pit, with its south
    face and its shadow. Its top is the cell's own square."""
    c = Canvas()
    body = rbox(0, THICK / 2, HALF, HALF + THICK / 2, 0.04)
    c.shadow(body, 0.8)
    c.fill(side, body)
    c.fill(top, rbox(0, 0, HALF, HALF, 0.04))
    return c


def eye_motif(c, line):
    """An eye cut in the slab: what marks a slab as one that lifts."""
    c.fill(line, np.abs(rbox(0, 0, HALF - 0.065, HALF - 0.065, 0.02)) - 0.011)
    almond = np.maximum(ellipse(0, 0.20, 0.36, 0.36), ellipse(0, -0.20, 0.36, 0.36))
    c.fill(line, almond)
    c.fill(BONE, almond + 0.03)
    c.fill(line, ellipse(0, 0, 0.115, 0.115))
    c.fill(INK, ellipse(0, 0, 0.05, 0.05))
    for x in (-0.20, 0.0, 0.20):
        c.fill(line, segment(x, -0.24, x * 1.25, -0.31, 0.012))


def frost_motif(c, line):
    c.fill(line, np.abs(rbox(0, 0, HALF - 0.065, HALF - 0.065, 0.02)) - 0.011)
    for k in range(3):
        angle = np.pi * k / 3 + np.pi / 6
        dx, dy = np.cos(angle) * 0.27, np.sin(angle) * 0.27
        c.fill(line, segment(-dx, -dy, dx, dy, 0.02))
        for end in (-1, 1):
            ex, ey = end * dx * 0.62, end * dy * 0.62
            for turn in (-1, 1):
                bx = ex + np.cos(angle + turn * 0.9) * 0.085 * end
                by = ey + np.sin(angle + turn * 0.9) * 0.085 * end
                c.fill(line, segment(ex, ey, bx, by, 0.015))
    c.fill(line, ellipse(0, 0, 0.045, 0.045))


def circle_motif(c):
    """A portal's ring cut in dark slate. What burns in it is the game's."""
    c.fill(INK, ellipse(0, 0, 0.36, 0.36))
    c.fill((0.10, 0.08, 0.14), ellipse(0, 0, 0.30, 0.30))


def stone_loose():
    c = slab(BONE, BONE_SIDE)
    eye_motif(c, BLOOD)
    return c.image()


def slab_plain():
    """A loose slab with only its border: what a loose plate's disc sits on."""
    c = slab(BONE, BONE_SIDE)
    c.fill(BLOOD, np.abs(rbox(0, 0, HALF - 0.065, HALF - 0.065, 0.02)) - 0.011)
    return c.image()


def ice(loose):
    c = slab(FROST, FROST_SIDE) if loose else paving(FROST_SET)
    frost_motif(c, (1, 1, 1))
    return c.image()


def portal(loose):
    c = slab(SLATE, SLATE_SIDE) if loose else paving(SLATE_SET)
    circle_motif(c)
    return c.image()


def portal_light():
    """What burns in a portal: a ring and a five-pointed star, in white. The
    game colours and turns it."""
    c = Canvas()
    c.fill((1, 1, 1), np.abs(ellipse(0, 0, 0.25, 0.25)) - 0.018)
    points = [(np.sin(k * 2 * np.pi / 5) * 0.235, -np.cos(k * 2 * np.pi / 5) * 0.235) for k in range(5)]
    for k in range(5):
        a, b = points[k], points[(k + 2) % 5]
        c.fill((1, 1, 1), segment(a[0], a[1], b[0], b[1], 0.012))
    return c.image()


def lid(c, y, hw, hh, top, trim):
    c.fill(top, rbox(0, y, hw, hh, 0.03))
    c.fill(trim, np.maximum(rbox(0, y, hw, hh, 0.03), -rbox(0, y, hw - 0.045, hh - 0.045, 0.01)))
    c.fill(PALE, rbox(0, y, 0.028, hh * 0.62))
    c.fill(PALE, rbox(0, y - hh * 0.22, hw * 0.42, 0.028))


def coffin():
    """What gets pushed: a coffin, taller than anything else on the floor."""
    c = Canvas()
    hw, hh, tall, y = 0.30, 0.30, 0.22, -0.16
    body = rbox(0, y + tall / 2, hw, hh + tall / 2, 0.03)
    c.shadow(body, 1.5)
    c.fill(WOOD_SIDE, body)
    c.fill((0.19, 0.12, 0.08), np.maximum(body, np.abs(V - (y + hh + tall * 0.55)) - 0.012))
    lid(c, y, hw, hh, WOOD, (0.34, 0.22, 0.14))
    return c.image()


def rubble():
    """A coffin that went down and wedged: its lid, level with the floor."""
    c = Canvas()
    lid(c, 0.0, HALF, HALF, WOOD_SUNK, (0.21, 0.16, 0.14))
    return c.image()


def button(down):
    """A plate's disc, in white and grey: the game colours it."""
    c = Canvas()
    if down:
        c.fill((0.30, 0.30, 0.30), ellipse(0, 0.0, 0.31, 0.31))
        c.fill((1, 1, 1), ellipse(0, 0.0, 0.26, 0.26))
        c.fill((0.40, 0.40, 0.40), np.abs(ellipse(0, 0.0, 0.14, 0.14)) - 0.02)
    else:
        c.fill((0.34, 0.34, 0.34), ellipse(0, 0.02, 0.31, 0.31))
        c.fill((0.86, 0.86, 0.86), ellipse(0, -0.04, 0.31, 0.31))
        c.fill((0.46, 0.46, 0.46), np.abs(ellipse(0, -0.04, 0.17, 0.17)) - 0.02)
    return c.image()


def gate(shut):
    """Bars across a cell, standing tall, or drawn down into it. White: the
    game colours them."""
    c = Canvas()
    bars = (-0.27, -0.09, 0.09, 0.27)
    if shut:
        shape = rbox(0, -0.34, 0.37, 0.04, 0.02)
        shape = np.minimum(shape, rbox(0, 0.10, 0.37, 0.04, 0.02))
        for x in bars:
            shape = np.minimum(shape, rbox(x, -0.10, 0.04, 0.40, 0.02))
            shape = np.minimum(shape, (np.abs(U - x) * 1.6 + np.abs(V + 0.52)) - 0.07)
        for x in (-0.42, 0.42):
            shape = np.minimum(shape, rbox(x, -0.08, 0.055, 0.42, 0.03))
        c.shadow(shape, 1.4)
        c.fill((1, 1, 1), shape)
        c.fill((0.62, 0.62, 0.62), np.maximum(shape, -(V - 0.12)))
    else:
        for x in bars:
            c.fill((0.10, 0.10, 0.10), ellipse(x, 0.28, 0.055, 0.04))
            c.fill((0.85, 0.85, 0.85), (np.abs(U - x) * 1.6 + np.abs(V - 0.25)) - 0.05)
        for x in (-0.42, 0.42):
            post = rbox(x, 0.18, 0.055, 0.14, 0.03)
            c.shadow(post, 0.7)
            c.fill((1, 1, 1), post)
            c.fill((0.62, 0.62, 0.62), np.maximum(post, -(V - 0.22)))
    return c.image()


def lantern(lit):
    c = Canvas()
    if lit:
        c.fill(CANDLE, ellipse(0, 0.02, 0.44, 0.44), 0.14)
        c.fill(CANDLE, ellipse(0, 0.02, 0.30, 0.30), 0.20)
    body = np.minimum(rbox(0, 0.03, 0.12, 0.20, 0.03), rbox(0, 0.225, 0.16, 0.035, 0.02))
    c.shadow(np.minimum(body, rbox(0, -0.175, 0.14, 0.035, 0.02)), 0.9)
    c.fill(IRON, np.maximum(np.abs(ellipse(0, -0.235, 0.075, 0.075)) - 0.016, V + 0.215))
    c.fill(IRON, rbox(0, 0.03, 0.12, 0.20, 0.03))
    c.fill((1.0, 0.86, 0.48) if lit else INK, rbox(0, 0.03, 0.082, 0.15, 0.02))
    c.fill(BONE, rbox(0, 0.13, 0.026, 0.05, 0.01))
    if lit:
        c.fill((1, 1, 0.92), ellipse(0, 0.04, 0.032, 0.06))
    c.fill(IRON, rbox(0, -0.175, 0.14, 0.035, 0.02))
    c.fill(IRON, rbox(0, 0.225, 0.16, 0.035, 0.02))
    return c.image()


def keeper(cloth, view):
    """A small hooded keeper: a cloak, a hood, and two lit eyes in the dark of
    it. Three ways round: 'front', 'back' or 'side'."""
    cloth = np.asarray(cloth)
    dark = cloth * 0.66
    c = Canvas()
    lean = 0.035 if view == "side" else 0.0
    cloak = np.maximum(ellipse(0, 0.30, 0.25, 0.50), V - 0.27)
    hood = ellipse(lean * 0.6, -0.17, 0.19, 0.20)
    c.fill((0, 0, 0), ellipse(0.04, 0.285, 0.25, 0.075), SHADOW)
    c.fill(dark, np.maximum(ellipse(0, 0.30, 0.25, 0.50), np.maximum(V - 0.27, -(V - 0.20))))
    c.fill(cloth, np.maximum(cloak, V - 0.20))
    # The cloak's fold, down the front or along the side.
    if view == "front":
        c.fill(dark, np.maximum(rbox(0, 0.13, 0.014, 0.16), cloak))
    if view == "side":
        c.fill(dark, np.maximum(rbox(-0.04, 0.13, 0.014, 0.16), cloak))
    c.fill(cloth, hood)
    c.fill(dark, np.maximum(hood, -(ellipse(lean * 0.6 - 0.02, -0.19, 0.19, 0.20))))
    if view == "front":
        c.fill(INK, ellipse(0, -0.14, 0.125, 0.13))
        for sx in (-1, 1):
            c.fill((1.0, 0.93, 0.70), ellipse(sx * 0.052, -0.13, 0.024, 0.03))
    if view == "side":
        c.fill(INK, ellipse(lean + 0.085, -0.14, 0.085, 0.125))
        c.fill((1.0, 0.93, 0.70), ellipse(lean + 0.115, -0.13, 0.021, 0.03))
    if view == "back":
        c.fill(dark, np.maximum(rbox(0, -0.16, 0.012, 0.16), hood))
    # The hood's point.
    tip = (np.abs(U - lean * 0.6) * 1.5 + np.abs(V + 0.37)) - 0.05
    c.fill(cloth, np.maximum(tip, V + 0.33))
    return c.image()


SPRITES = [
    ("stone_set_a", lambda: paving(STONE).image()),
    ("stone_set_b", lambda: paving(STONE_B).image()),
    ("stone_set_c", lambda: paving(STONE, cracked=True).image()),
    ("stone_loose", stone_loose),
    ("ice_set", lambda: ice(False)),
    ("ice_loose", lambda: ice(True)),
    ("portal_set", lambda: portal(False)),
    ("portal_loose", lambda: portal(True)),
    ("rubble", rubble),
    ("coffin", coffin),
    ("button_up", lambda: button(False)),
    ("button_down", lambda: button(True)),
    ("gate_shut", lambda: gate(True)),
    ("gate_open", lambda: gate(False)),
    ("lantern", lambda: lantern(False)),
    ("lantern_lit", lambda: lantern(True)),
    ("portal_light", portal_light),
    ("ember_front", lambda: keeper(EMBER, "front")),
    ("ember_back", lambda: keeper(EMBER, "back")),
    ("ember_side", lambda: keeper(EMBER, "side")),
    ("tide_front", lambda: keeper(TIDE, "front")),
    ("tide_back", lambda: keeper(TIDE, "back")),
    ("tide_side", lambda: keeper(TIDE, "side")),
    ("slab_plain", slab_plain),
]
COLUMNS, ROWS = 6, 4


if __name__ == "__main__":
    atlas = Image.new("RGBA", (COLUMNS * CELL, ROWS * CELL), (0, 0, 0, 0))
    for index, (name, make) in enumerate(SPRITES):
        atlas.paste(make(), ((index % COLUMNS) * CELL, (index // COLUMNS) * CELL))
    atlas.save(DEST / "atlas.png", optimize=True)
    print("Wrote atlas.png", atlas.size)
