"""Underfoot's pictures: a tiled courtyard over dark water, cut from flat colour.

The look has four rules, and every sprite keeps them:
  - flat colour only: no gradients, no gloss, no blur, no outlines;
  - whatever is loose or stands up casts one hard shadow, straight down;
  - what is set in the floor is plain, and what can be carried is decorated:
    a loose stone is a glazed tile with an eight-pointed star;
  - a short palette: sand, cream, cobalt, mustard, jade, coral, teal, plum.

Nothing is drawn by hand and nothing is borrowed: each sprite is shapes given
as distances and filled. Run from any directory; it writes atlas.png and
lattice.png beside itself. Needs numpy and Pillow.

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

SAND = (0.80, 0.72, 0.60)
SAND_B = (0.77, 0.69, 0.57)
CREAM = (0.97, 0.94, 0.87)
CREAM_SIDE = (0.70, 0.66, 0.60)
COBALT = (0.17, 0.33, 0.68)
MUSTARD = (0.93, 0.69, 0.25)
AQUA = (0.74, 0.91, 0.95)
AQUA_SET = (0.60, 0.83, 0.90)
AQUA_SIDE = (0.38, 0.64, 0.75)
VIOLET = (0.47, 0.35, 0.78)
VIOLET_SET = (0.40, 0.30, 0.66)
VIOLET_SIDE = (0.27, 0.20, 0.50)
INK = (0.13, 0.11, 0.24)
WOOD = (0.80, 0.55, 0.30)
WOOD_DARK = (0.56, 0.35, 0.18)
WOOD_SUNK = (0.46, 0.36, 0.30)
BRASS = (0.93, 0.69, 0.25)
SKIN = (0.99, 0.87, 0.74)
STRAW = (0.95, 0.83, 0.52)
CORAL = (0.94, 0.40, 0.33)
TEAL = (0.16, 0.64, 0.66)
SHADOW = 0.36


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


def star(cx, cy, r):
    """An eight-pointed star: two squares, one turned an eighth."""
    x, y = np.abs(U - cx), np.abs(V - cy)
    a = r * 0.74
    return np.minimum(np.maximum(x, y) - a, (x + y) / 1.41421 - a)


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

    def shadow(self, d, drop=0.06):
        """The one shadow there is: the same shape, a little lower, flat."""
        shifted = np.roll(d, int(round(drop / PX)), axis=0)
        self.fill((0.02, 0.03, 0.08), shifted, SHADOW)

    def image(self):
        a = self.a[..., None]
        straight = np.where(a > 1e-6, self.rgb / np.maximum(a, 1e-6), 0)
        out = np.concatenate([straight, a], axis=2)
        img = Image.fromarray((np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA")
        return img.resize((CELL, CELL), Image.LANCZOS)


HALF = 0.465


def paving(color):
    """Ground set in the floor: a plain square, flush. The grout is the game's."""
    c = Canvas()
    c.fill(color, rbox(0, 0, HALF, HALF, 0.03))
    return c


def tile(top, side):
    """Loose ground: a thick glazed tile, with its side and its shadow."""
    c = Canvas()
    rise = -0.035
    c.shadow(rbox(0, rise + 0.07, HALF, HALF, 0.05), 0.075)
    c.fill(side, rbox(0, rise + 0.07, HALF, HALF, 0.05))
    c.fill(top, rbox(0, rise, HALF, HALF, 0.05))
    return c, rise


def star_motif(c, y, line, heart):
    c.fill(line, np.abs(rbox(0, y, HALF - 0.065, HALF - 0.065, 0.02)) - 0.011)
    c.fill(line, star(0, y, 0.30))
    c.fill(CREAM, star(0, y, 0.19))
    c.fill(heart, star(0, y, 0.115))
    for sx in (-1, 1):
        for sy in (-1, 1):
            c.fill(line, (np.abs(U - sx * 0.315) + np.abs(V - y - sy * 0.315)) - 0.035)


def frost_motif(c, y, line):
    c.fill(line, np.abs(rbox(0, y, HALF - 0.065, HALF - 0.065, 0.02)) - 0.011)
    for k in range(3):
        angle = np.pi * k / 3 + np.pi / 6
        dx, dy = np.cos(angle) * 0.27, np.sin(angle) * 0.27
        c.fill(line, segment(-dx, y - dy, dx, y + dy, 0.02))
        for end in (-1, 1):
            ex, ey = end * dx * 0.62, end * dy * 0.62
            for turn in (-1, 1):
                bx = ex + np.cos(angle + turn * 0.9) * 0.085 * end
                by = ey + np.sin(angle + turn * 0.9) * 0.085 * end
                c.fill(line, segment(ex, y + ey, bx, y + by, 0.015))
    c.fill(line, ellipse(0, y, 0.045, 0.045))


def well_motif(c, y, ring):
    c.fill(ring, np.abs(rbox(0, y, HALF - 0.065, HALF - 0.065, 0.02)) - 0.011)
    c.fill(INK, ellipse(0, y, 0.31, 0.31))


def stone_set(color):
    return paving(color).image()


def stone_loose():
    c, y = tile(CREAM, CREAM_SIDE)
    star_motif(c, y, COBALT, MUSTARD)
    return c.image()


def tile_plain():
    """A loose tile with only its border: what a loose plate's disc sits on."""
    c, y = tile(CREAM, CREAM_SIDE)
    c.fill(COBALT, np.abs(rbox(0, y, HALF - 0.065, HALF - 0.065, 0.02)) - 0.011)
    return c.image()


def ice(loose):
    if loose:
        c, y = tile(AQUA, AQUA_SIDE)
    else:
        c, y = paving(AQUA_SET), 0.0
    frost_motif(c, y, (1, 1, 1))
    return c.image()


def portal(loose):
    if loose:
        c, y = tile(VIOLET, VIOLET_SIDE)
    else:
        c, y = paving(VIOLET_SET), 0.0
    well_motif(c, y, CREAM)
    return c.image()


def portal_light():
    """What turns in a portal, in white: the game colours and spins it."""
    c = Canvas()
    angle = np.arctan2(V, U)
    ring = np.abs(ellipse(0, 0, 0.215, 0.215)) - 0.026
    gaps = np.abs(np.cos(angle)) - 0.80
    c.fill((1, 1, 1), np.maximum(ring, gaps))
    inner = np.abs(ellipse(0, 0, 0.115, 0.115)) - 0.022
    c.fill((1, 1, 1), np.maximum(inner, np.abs(np.sin(angle)) - 0.80))
    c.fill((1, 1, 1), ellipse(0, 0, 0.04, 0.04))
    return c.image()


def crate_lid(c, y, half, top, frame):
    c.fill(top, rbox(0, y, half, half, 0.035))
    c.fill(frame, np.maximum(rbox(0, y, half, half, 0.035), -rbox(0, y, half - 0.062, half - 0.062, 0.01)))
    brace = np.maximum(segment(-half + 0.05, y - half + 0.05, half - 0.05, y + half - 0.05, 0.034), rbox(0, y, half - 0.05, half - 0.05))
    c.fill(frame, brace)


def crate():
    c = Canvas()
    half, y = 0.30, -0.11
    c.shadow(rbox(0, y + 0.15, half, half, 0.035), 0.075)
    c.fill(WOOD_DARK, rbox(0, y + 0.15, half, half, 0.035))
    c.fill((0.44, 0.27, 0.13), np.maximum(rbox(0, y + 0.15, half, half, 0.035), np.abs(np.abs(U) - 0.15) - 0.011))
    crate_lid(c, y, half, WOOD, (0.66, 0.43, 0.21))
    return c.image()


def rubble():
    """A crate that sank: its lid in the floor, the colour of wet wood."""
    c = Canvas()
    crate_lid(c, 0.0, HALF, WOOD_SUNK, (0.36, 0.28, 0.24))
    return c.image()


def button(down):
    """A plate's disc, in white and grey: the game colours it."""
    c = Canvas()
    if down:
        c.fill((0.42, 0.42, 0.42), ellipse(0, 0.0, 0.30, 0.30))
        c.fill((1, 1, 1), ellipse(0, 0.0, 0.25, 0.25))
        c.fill((0.42, 0.42, 0.42), np.abs(ellipse(0, 0.0, 0.14, 0.14)) - 0.02)
    else:
        c.fill((0.42, 0.42, 0.42), ellipse(0, 0.035, 0.30, 0.30))
        c.fill((0.90, 0.90, 0.90), ellipse(0, -0.02, 0.30, 0.30))
        c.fill((0.50, 0.50, 0.50), np.abs(ellipse(0, -0.02, 0.17, 0.17)) - 0.02)
    return c.image()


def gate(shut):
    """Bars across a cell, or drawn down into it. White: the game colours them."""
    c = Canvas()
    bars = (-0.27, -0.09, 0.09, 0.27)
    if shut:
        shape = rbox(0, -0.25, 0.37, 0.04, 0.02)
        shape = np.minimum(shape, rbox(0, 0.12, 0.37, 0.04, 0.02))
        for x in bars:
            shape = np.minimum(shape, rbox(x, -0.03, 0.04, 0.34, 0.04))
        for x in (-0.42, 0.42):
            shape = np.minimum(shape, rbox(x, -0.03, 0.055, 0.37, 0.05))
        c.shadow(shape, 0.07)
        c.fill((1, 1, 1), shape)
    else:
        for x in bars:
            c.fill((0.25, 0.25, 0.25), ellipse(x, 0.30, 0.055, 0.04))
            c.fill((1, 1, 1), ellipse(x, 0.285, 0.036, 0.026))
        for x in (-0.42, 0.42):
            post = rbox(x, 0.20, 0.055, 0.13, 0.05)
            c.shadow(post, 0.05)
            c.fill((1, 1, 1), post)
    return c.image()


def lantern(lit):
    c = Canvas()
    if lit:
        c.fill(MUSTARD, ellipse(0, 0.02, 0.42, 0.42), 0.16)
        c.fill(MUSTARD, ellipse(0, 0.02, 0.29, 0.29), 0.22)
    body = np.minimum(rbox(0, 0.03, 0.12, 0.20, 0.04), rbox(0, 0.225, 0.16, 0.035, 0.02))
    c.shadow(np.minimum(body, rbox(0, -0.175, 0.14, 0.035, 0.02)), 0.045)
    c.fill(BRASS, np.maximum(np.abs(ellipse(0, -0.235, 0.075, 0.075)) - 0.016, V + 0.215))
    c.fill(BRASS, rbox(0, 0.03, 0.12, 0.20, 0.04))
    c.fill((1.0, 0.93, 0.62) if lit else INK, rbox(0, 0.03, 0.082, 0.15, 0.03))
    if lit:
        c.fill((1, 1, 1), ellipse(0, 0.05, 0.035, 0.07))
    else:
        c.fill(MUSTARD, ellipse(0, 0.09, 0.022, 0.036))
    c.fill(BRASS, rbox(0, -0.175, 0.14, 0.035, 0.02))
    c.fill(BRASS, rbox(0, 0.225, 0.16, 0.035, 0.02))
    return c.image()


def paver(cloth, view):
    """A tiler in a straw hat, three ways round: 'front', 'back' or 'side'."""
    cloth = np.asarray(cloth)
    dark = cloth * 0.72
    c = Canvas()
    lean = 0.03 if view == "side" else 0.0
    c.fill((0.02, 0.03, 0.08), ellipse(0, 0.285, 0.23, 0.07), SHADOW)
    for sx in (-1, 1):
        c.fill(INK, ellipse(sx * 0.09 + lean, 0.265, 0.07, 0.04))
    if view != "side":
        for sx in (-1, 1):
            c.fill(dark, ellipse(sx * 0.215, 0.10, 0.06, 0.085))
    c.fill(cloth, rbox(0, 0.09, 0.195, 0.185, 0.16))
    c.fill(dark, np.maximum(rbox(0, 0.09, 0.195, 0.185, 0.16), np.abs(V - 0.17) - 0.02))
    if view == "side":
        c.fill(dark, ellipse(0.03, 0.10, 0.058, 0.09))
    if view == "back":
        c.fill(WOOD, rbox(0, 0.07, 0.13, 0.115, 0.04))
        c.fill(WOOD_DARK, rbox(0, 0.03, 0.13, 0.016))
    c.fill(SKIN, ellipse(lean, -0.14, 0.165, 0.16))
    if view == "back":
        c.fill(INK, np.maximum(ellipse(0, -0.14, 0.165, 0.16), V + 0.05))
    if view == "front":
        for sx in (-1, 1):
            c.fill(INK, ellipse(sx * 0.062, -0.12, 0.022, 0.03))
    if view == "side":
        c.fill(INK, ellipse(lean + 0.095, -0.12, 0.02, 0.03))
        c.fill(SKIN, ellipse(lean + 0.165, -0.10, 0.025, 0.022))
    hat_x = lean * 1.5
    c.fill(np.asarray(STRAW) * 0.80, ellipse(hat_x, -0.225, 0.285, 0.09))
    c.fill(STRAW, ellipse(hat_x, -0.245, 0.285, 0.09))
    c.fill(cloth, ellipse(hat_x, -0.285, 0.15, 0.062))
    c.fill(STRAW, ellipse(hat_x, -0.31, 0.135, 0.062))
    return c.image()


SPRITES = [
    ("stone_set_a", lambda: stone_set(SAND)),
    ("stone_set_b", lambda: stone_set(SAND_B)),
    ("stone_set_c", lambda: stone_set(SAND)),
    ("stone_loose", stone_loose),
    ("ice_set", lambda: ice(False)),
    ("ice_loose", lambda: ice(True)),
    ("portal_set", lambda: portal(False)),
    ("portal_loose", lambda: portal(True)),
    ("rubble", rubble),
    ("crate", crate),
    ("button_up", lambda: button(False)),
    ("button_down", lambda: button(True)),
    ("gate_shut", lambda: gate(True)),
    ("gate_open", lambda: gate(False)),
    ("lantern", lambda: lantern(False)),
    ("lantern_lit", lambda: lantern(True)),
    ("portal_light", portal_light),
    ("ember_front", lambda: paver(CORAL, "front")),
    ("ember_back", lambda: paver(CORAL, "back")),
    ("ember_side", lambda: paver(CORAL, "side")),
    ("tide_front", lambda: paver(TEAL, "front")),
    ("tide_back", lambda: paver(TEAL, "back")),
    ("tide_side", lambda: paver(TEAL, "side")),
    ("tile_plain", tile_plain),
]
COLUMNS, ROWS = 6, 4


def lattice():
    """The backdrop's pattern: eight-pointed stars in line, white on nothing.
    It repeats, and the game lays it faintly over the night."""
    size, ss = 240, 3
    n = size * ss
    y, x = np.mgrid[0:n, 0:n]
    u = ((x + 0.5) / n + 0.5) % 1 - 0.5
    v = ((y + 0.5) / n + 0.5) % 1 - 0.5
    ax, ay = np.abs(u), np.abs(v)
    d = np.minimum(np.maximum(ax, ay) - 0.30, (ax + ay) / 1.41421 - 0.30)
    line = np.abs(d) - 0.012
    cu, cv = np.abs(ax - 0.5), np.abs(ay - 0.5)
    diamond = np.abs((cu + cv) - 0.12) - 0.012
    alpha = np.clip(0.5 - np.minimum(line, diamond) * n, 0, 1)
    out = np.stack([np.ones((n, n))] * 3 + [alpha], axis=2)
    Image.fromarray((out * 255 + 0.5).astype(np.uint8), "RGBA").resize((size, size), Image.LANCZOS).save(DEST / "lattice.png", optimize=True)


if __name__ == "__main__":
    atlas = Image.new("RGBA", (COLUMNS * CELL, ROWS * CELL), (0, 0, 0, 0))
    for index, (name, make) in enumerate(SPRITES):
        atlas.paste(make(), ((index % COLUMNS) * CELL, (index // COLUMNS) * CELL))
    atlas.save(DEST / "atlas.png", optimize=True)
    lattice()
    print("Wrote atlas.png", atlas.size, "and lattice.png")
