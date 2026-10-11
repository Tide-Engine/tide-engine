"""Underfoot's pictures: every sprite on the board and the sky behind it.

Nothing is drawn by hand and nothing is borrowed. Each sprite is shapes given
as distances, filled, lit from the top left, bevelled where they turn away
from the light, and given a little grain. Run from any directory; it writes
atlas.png sky.jpg and rock.png beside itself. Needs numpy and Pillow.

A sprite is 288 pixels square around a cell of 192, so things taller or wider
than their cell have room. The atlas is six of them across and five down, in
the order of SPRITES; paint.tide's `Art` enum is the same order.
"""
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

DEST = Path(__file__).resolve().parent
TILE = 192
CELL = 288
SS = 2
N = CELL * SS
PX = 1.0 / (TILE * SS)
ys, xs = np.mgrid[0:N, 0:N]
U = (xs + 0.5) * PX - CELL / (2 * TILE)
V = (ys + 0.5) * PX - CELL / (2 * TILE)
LIGHT = np.array([-0.56, -0.83])


def rbox(cx, cy, hw, hh, r):
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


def blur(a, radius):
    """Gaussian blur of a 0..1 array, radius in cells."""
    img = Image.fromarray(np.clip(a * 255, 0, 255).astype(np.uint8))
    return np.asarray(img.filter(ImageFilter.GaussianBlur(radius / PX))).astype(np.float64) / 255


def noise(seed, cells, octaves=3):
    rng = np.random.default_rng(seed)
    total = np.zeros((N, N))
    amp, norm = 1.0, 0.0
    for o in range(octaves):
        n = max(2, int(cells * 2 ** o))
        small = (rng.random((n, n)) * 255).astype(np.uint8)
        big = np.asarray(Image.fromarray(small).resize((N, N), Image.BICUBIC)).astype(np.float64) / 255
        total += (big * 2 - 1) * amp
        norm += amp
        amp *= 0.5
    return total / norm


def facing(d):
    """How much each point of a shape's edge faces the light, -1..1."""
    gy, gx = np.gradient(d)
    length = np.hypot(gx, gy) + 1e-9
    return (gx * LIGHT[0] + gy * LIGHT[1]) / length


class Canvas:
    def __init__(self):
        self.rgb = np.zeros((N, N, 3))
        self.a = np.zeros((N, N))

    def paint(self, color, alpha):
        color = np.asarray(color, dtype=np.float64)
        if color.ndim == 1:
            color = np.broadcast_to(color, (N, N, 3))
        alpha = np.clip(alpha, 0, 1)
        self.rgb = self.rgb * (1 - alpha[..., None]) + np.clip(color, 0, 1) * alpha[..., None]
        self.a = self.a * (1 - alpha) + alpha

    def shadow(self, d, radius, strength, dy=0.0):
        shifted = np.roll(cover(d), int(dy / PX), axis=0) if dy else cover(d)
        self.paint((0, 0, 0), blur(shifted, radius) * strength)

    def image(self):
        a = self.a[..., None]
        straight = np.where(a > 1e-6, self.rgb / np.maximum(a, 1e-6), 0)
        out = np.concatenate([straight, a], axis=2)
        img = Image.fromarray((np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA")
        return img.resize((CELL, CELL), Image.LANCZOS)


def solid(base, d, bevel=0.03, hi=0.20, lo=0.22, grain=None, slope=0.07, round_in=0.0):
    """A lit surface: a slope of light across it, a bevel at its edge, grain."""
    color = np.broadcast_to(np.asarray(base, dtype=np.float64), (N, N, 3)).copy()
    tilt = (U * LIGHT[0] + V * LIGHT[1]) * slope / 0.5
    edge = np.clip(1 + d / bevel, 0, 1) ** 1.5
    lit = facing(d)
    shade = 1 + tilt + edge * (hi * np.maximum(lit, 0) - lo * np.maximum(-lit, 0))
    if round_in:
        shade -= round_in * np.clip(1 + d / 0.12, 0, 1) ** 2
    if grain is not None:
        shade += grain
    return color * shade[..., None]


def speckle(seed, amount=0.035):
    fine = noise(seed, 48, 2)
    blotch = noise(seed + 1, 3, 3)
    dots = np.clip(noise(seed + 2, 90, 1) - 0.45, 0, 1) * 1.6
    return fine * amount + blotch * amount * 1.4 - dots * amount * 2.2


def planks(seed, rows=4, vertical=False):
    """Wood: boards with dark joints and grain that runs along them."""
    across, along = (U, V) if vertical else (V, U)
    board = np.floor((across + 0.75) * rows / 0.62)
    joint = np.abs(((across + 0.75) * rows / 0.62) % 1 - 0.5)
    rng = np.random.default_rng(seed)
    tones = rng.random(64) * 0.14 - 0.07
    tone = tones[(board.astype(int)) % 64]
    grain_img = noise(seed + 5, 6, 3)
    stretch = np.asarray(Image.fromarray(((grain_img * 0.5 + 0.5) * 255).astype(np.uint8)).resize((N // 6, N), Image.BICUBIC).resize((N, N), Image.BICUBIC)) / 255 - 0.5
    grain = stretch.T if not vertical else stretch
    return tone + grain * 0.10 - np.clip(joint - 0.44, 0, 1) * 4.5


# ---- ground ----------------------------------------------------------------

STONE_SET = (0.52, 0.56, 0.71)
STONE_LOOSE = (0.86, 0.87, 0.95)
STONE_SIDE = (0.40, 0.43, 0.62)
HALF = 0.455
ROUND = 0.10


def bedded(top, seed, grain_amount=0.035, gloss=None, extra=None):
    """Ground set in rock: flush, a groove round it, barely any side."""
    c = Canvas()
    d = rbox(0, 0, HALF, HALF, ROUND)
    c.paint((0.02, 0.02, 0.06), blur(cover(d - 0.02), 0.022) * 0.85)
    c.paint(np.asarray(top) * 0.42, cover(rbox(0, 0.016, HALF, HALF, ROUND)))
    surface = solid(top, d, bevel=0.035, hi=0.16, lo=0.20, grain=speckle(seed, grain_amount))
    c.paint(surface, cover(rbox(0, -0.006, HALF, HALF, ROUND)))
    if gloss:
        gloss(c, 0.0)
    if extra:
        extra(c, 0.0)
    return c.image()


def slab(top, side, seed, grain_amount=0.03, gloss=None, extra=None):
    """Loose ground: a thick stone with nothing under it but its own shadow."""
    c = Canvas()
    thick, rise = 0.13, -0.045
    c.shadow(rbox(0, rise + thick + 0.03, HALF - 0.02, HALF - 0.01, ROUND), 0.035, 0.55)
    body = rbox(0, rise + thick / 2, HALF, HALF + thick / 2, ROUND)
    face = np.broadcast_to(np.asarray(side, dtype=np.float64), (N, N, 3)) * (1.06 - np.clip((V - rise - HALF) / thick, 0, 1) * 0.34)[..., None]
    face = face * (1 + speckle(seed + 9, 0.03))[..., None]
    c.paint(face, cover(body))
    d = rbox(0, rise, HALF, HALF, ROUND)
    c.paint(solid(top, d, bevel=0.04, hi=0.10, lo=0.20, grain=speckle(seed, grain_amount)), cover(d))
    # A thin bright line along the top's near edge, where the side begins.
    lip = np.clip(1 - np.abs(V - (rise + HALF - 0.012)) / 0.012, 0, 1) * cover(d) * (np.abs(U) < HALF - ROUND)
    c.paint((1, 1, 1), lip * 0.22)
    if gloss:
        gloss(c, rise)
    if extra:
        extra(c, rise)
    return c.image()


def ice_gloss(c, rise):
    inside = cover(rbox(0, rise, HALF - 0.03, HALF - 0.03, ROUND))
    diag = (U + V - rise) / 1.41
    for at, width, power in ((-0.16, 0.055, 0.42), (-0.045, 0.02, 0.30), (0.20, 0.035, 0.20)):
        band = np.clip(1 - np.abs(diag - at) / width, 0, 1) ** 1.5
        c.paint((1, 1, 1), band * inside * power)
    depth = np.clip(noise(77, 5, 2), -1, 1)
    c.paint((0.20, 0.45, 0.70), np.clip(depth - 0.25, 0, 1) * inside * 0.22)


def portal_well(c, rise):
    """A dark bowl in the stone. The light in it is drawn in the game, turning."""
    ring = ellipse(0, rise, 0.33, 0.33)
    c.paint((0.05, 0.03, 0.14), cover(ring))
    bowl = np.clip(-ring / 0.33, 0, 1)
    c.paint((0.20, 0.11, 0.42), cover(ring) * bowl * 0.85)
    rim = np.clip(1 - np.abs(ring) / 0.022, 0, 1)
    c.paint((0.62, 0.50, 0.92), rim * (0.35 + 0.45 * np.maximum(-facing(ring), 0)))


def rubble():
    """A crate that sank: its lid, level with the floor round it."""
    c = Canvas()
    d = rbox(0, 0, HALF, HALF, 0.06)
    c.paint((0.02, 0.02, 0.06), blur(cover(d - 0.02), 0.022) * 0.85)
    wood = solid((0.47, 0.33, 0.22), d, bevel=0.03, hi=0.12, lo=0.22, grain=planks(31, rows=3.4))
    c.paint(wood, cover(d))
    frame = np.maximum(d, -(rbox(0, 0, HALF - 0.075, HALF - 0.075, 0.03)))
    c.paint(solid((0.38, 0.26, 0.17), frame, bevel=0.02, hi=0.14, lo=0.2, grain=planks(32, rows=9, vertical=True) * 0.4), cover(frame))
    for sx in (-1, 1):
        for sy in (-1, 1):
            nail = ellipse(sx * 0.385, sy * 0.385, 0.02, 0.02)
            c.paint((0.20, 0.14, 0.10), cover(nail))
    return c.image()


# ---- things on the ground ---------------------------------------------------

def crate():
    c = Canvas()
    half, thick, rise = 0.315, 0.17, -0.115
    c.shadow(ellipse(0, 0.30, 0.36, 0.11), 0.03, 0.55)
    body = rbox(0, rise + thick / 2, half, half + thick / 2, 0.05)
    front = solid((0.50, 0.33, 0.19), body, bevel=0.02, hi=0.0, lo=0.1, grain=planks(5, rows=6, vertical=True) * 0.6)
    front = front * (1.0 - np.clip((V - rise - half) / thick, 0, 1) * 0.30)[..., None]
    c.paint(front, cover(body))
    top = rbox(0, rise, half, half, 0.05)
    c.paint(solid((0.86, 0.64, 0.38), top, bevel=0.03, hi=0.14, lo=0.2, grain=planks(6, rows=3.2)), cover(top))
    inner = rbox(0, rise, half - 0.075, half - 0.075, 0.02)
    frame = np.maximum(top, -inner)
    c.paint(solid((0.72, 0.50, 0.27), frame, bevel=0.022, hi=0.20, lo=0.24, grain=planks(8, rows=10, vertical=True) * 0.35), cover(frame))
    c.paint((0.30, 0.18, 0.09), np.clip(1 - np.abs(inner) / 0.012, 0, 1) * 0.5)
    brace = np.maximum(segment(-0.21, rise - 0.21, 0.21, rise + 0.21, 0.04), inner)
    c.paint(solid((0.74, 0.52, 0.28), brace, bevel=0.02, hi=0.2, lo=0.26, grain=planks(9, rows=11) * 0.3), cover(brace))
    for sx in (-1, 1):
        for sy in (-1, 1):
            c.paint((0.33, 0.21, 0.12), cover(ellipse(sx * 0.265, rise + sy * 0.265, 0.017, 0.017)))
    lip = np.clip(1 - np.abs(V - (rise + half - 0.01)) / 0.01, 0, 1) * cover(top) * (np.abs(U) < half - 0.05)
    c.paint((1, 0.92, 0.75), lip * 0.30)
    return c.image()


def button(down):
    """A plate's face, in greys: the game gives it its colour."""
    c = Canvas()
    half = 0.335
    rise = 0.0 if down else -0.045
    well = rbox(0, 0.0, half + 0.03, half + 0.03, 0.10)
    c.paint((0.16, 0.16, 0.16), cover(well))
    if not down:
        side = rbox(0, rise / 2 + 0.01, half, half - rise / 2, 0.085)
        c.paint((0.50, 0.50, 0.50), cover(side))
    d = rbox(0, rise, half, half, 0.085)
    top = solid((0.90, 0.90, 0.90) if not down else (1.0, 1.0, 1.0), d, bevel=0.035, hi=0.10, lo=0.22 if not down else 0.10, slope=0.05)
    c.paint(top, cover(d))
    groove = np.abs(rbox(0, rise, half - 0.10, half - 0.10, 0.05))
    c.paint((0.45, 0.45, 0.45), np.clip(1 - groove / 0.014, 0, 1) * 0.75)
    c.paint((0.42, 0.42, 0.42), cover(ellipse(0, rise, 0.04, 0.04)))
    return c.image()


def rod(c, x, top, bottom, r, tone=0.92):
    d = segment(x, top, x, bottom, r)
    across = np.clip((U - x) / r, -1, 1)
    metal = tone * (0.74 + 0.30 * np.clip(1 - np.abs(across + 0.35) / 0.9, 0, 1) - 0.20 * np.maximum(across, 0))
    c.paint(np.stack([metal] * 3, axis=2), cover(d))
    c.paint((1, 1, 1), np.clip(1 - np.abs(across + 0.4) / 0.22, 0, 1) * cover(d) * 0.30)


def gate(shut):
    """Bars across a cell, or drawn down into it. Greys: the game colours them."""
    c = Canvas()
    for x in (-0.25, -0.085, 0.085, 0.25):
        c.paint((0.08, 0.08, 0.10), cover(ellipse(x, 0.30, 0.052, 0.03)))
    if shut:
        c.shadow(rbox(0, 0.33, 0.36, 0.035, 0.03), 0.03, 0.5)
        for x in (-0.25, -0.085, 0.085, 0.25):
            rod(c, x, -0.36, 0.30, 0.036)
        for y in (-0.25, 0.14):
            d = rbox(0, y, 0.33, 0.034, 0.03)
            band = 0.88 * (0.85 + 0.25 * np.clip(1 - np.abs((V - y) / 0.034 + 0.4), 0, 1))
            c.paint(np.stack([band] * 3, axis=2), cover(d))
        for x in (-0.25, -0.085, 0.085, 0.25):
            c.paint((1, 1, 1), cover(ellipse(x, -0.375, 0.03, 0.024)))
    else:
        for x in (-0.25, -0.085, 0.085, 0.25):
            rod(c, x, 0.262, 0.30, 0.036)
            c.paint((1, 1, 1), cover(ellipse(x, 0.255, 0.03, 0.02)) * 0.9)
    # The two posts stand either way.
    for x in (-0.425, 0.425):
        c.shadow(ellipse(x, 0.33, 0.06, 0.03), 0.02, 0.45)
        rod(c, x, -0.30 if shut else 0.10, 0.31, 0.05, 0.70)
        c.paint((0.98, 0.98, 0.98), cover(ellipse(x, -0.32 if shut else 0.08, 0.062, 0.045)))
    return c.image()


def lantern(lit):
    c = Canvas()
    brass = (0.86, 0.62, 0.24)
    if lit:
        glow = blur(cover(ellipse(0, -0.02, 0.16, 0.2)), 0.16)
        c.paint((1.0, 0.78, 0.34), glow * 0.85)
        c.paint((1.0, 0.92, 0.62), blur(cover(ellipse(0, -0.02, 0.10, 0.13)), 0.05) * 0.6)
    c.shadow(ellipse(0, 0.265, 0.17, 0.05), 0.02, 0.5)
    handle = np.abs(ellipse(0, -0.245, 0.085, 0.085)) - 0.014
    c.paint(solid(brass, handle, bevel=0.012, hi=0.3, lo=0.3), cover(np.maximum(handle, V + 0.225)))
    base = rbox(0, 0.225, 0.155, 0.04, 0.03)
    c.paint(solid(brass, base, bevel=0.02, hi=0.25, lo=0.3), cover(base))
    glass = rbox(0, 0.02, 0.105, 0.17, 0.05)
    if lit:
        heat = np.clip(1 - np.hypot(U / 0.11, (V - 0.05) / 0.17), 0, 1)
        colour = np.stack([np.ones((N, N)), 0.78 + 0.22 * heat, 0.36 + 0.60 * heat ** 1.5], axis=2)
        c.paint(colour, cover(glass))
    else:
        c.paint(solid((0.20, 0.22, 0.34), glass, bevel=0.03, hi=0.3, lo=0.2), cover(glass))
        c.paint((0.95, 0.62, 0.25), cover(ellipse(0, 0.09, 0.022, 0.034)) * 0.9)
        c.paint((0.60, 0.70, 1.0), np.clip(1 - np.abs(U + 0.055) / 0.014, 0, 1) * cover(rbox(0, 0.02, 0.09, 0.13, 0.04)) * 0.35)
    for x in (-0.105, 0.105):
        bar = segment(x, -0.13, x, 0.19, 0.013)
        c.paint(solid(brass, bar, bevel=0.01, hi=0.3, lo=0.3), cover(bar))
    cap = rbox(0, -0.165, 0.135, 0.036, 0.03)
    c.paint(solid(brass, cap, bevel=0.02, hi=0.3, lo=0.3), cover(cap))
    knob = rbox(0, -0.205, 0.06, 0.03, 0.025)
    c.paint(solid(brass, knob, bevel=0.015, hi=0.3, lo=0.3), cover(knob))
    if lit:
        c.paint((1, 1, 0.92), cover(ellipse(0, 0.06, 0.03, 0.055)))
    return c.image()


def glow_ring():
    """The light in a portal, in greys: the game colours and turns it."""
    c = Canvas()
    ring = np.abs(ellipse(0, 0, 0.215, 0.215)) - 0.022
    c.paint((1, 1, 1), blur(cover(ring), 0.05) * 0.75)
    angle = np.arctan2(V, U)
    sweep = 0.55 + 0.45 * np.cos(angle * 2)
    c.paint((1, 1, 1), cover(ring) * sweep)
    inner = np.abs(ellipse(0, 0, 0.11, 0.11)) - 0.014
    c.paint((1, 1, 1), cover(inner) * (0.50 + 0.5 * np.cos(angle * 2 + 1.6)) * 0.9)
    c.paint((1, 1, 1), blur(cover(ellipse(0, 0, 0.04, 0.04)), 0.02))
    return c.image()


# ---- the pavers --------------------------------------------------------------

SKIN = (0.99, 0.86, 0.72)


def paver(cloth, view):
    """A small round worker in a brimmed hat. view: 'front', 'back' or 'side'."""
    cloth = np.asarray(cloth)
    dark = cloth * 0.62
    c = Canvas()
    c.shadow(ellipse(0, 0.275, 0.25, 0.085), 0.025, 0.55)
    lean = 0.035 if view == "side" else 0.0
    for sx in (-1, 1):
        foot = ellipse(sx * 0.095 + lean, 0.27, 0.075, 0.042)
        c.paint(solid((0.27, 0.22, 0.26), foot, bevel=0.03, hi=0.25, lo=0.2), cover(foot))
    if view == "back":
        pack = rbox(0, 0.10, 0.15, 0.14, 0.06)
    for sx in (-1, 1):
        if view == "side" and sx == -1:
            continue
        arm = ellipse(sx * 0.225 if view != "side" else 0.03, 0.09, 0.062, 0.085)
        if view != "side":
            c.paint(solid(dark * 1.2, arm, bevel=0.05, hi=0.25, lo=0.25), cover(arm))
    body = rbox(0, 0.085, 0.205, 0.195, 0.17)
    c.paint(solid(cloth, body, bevel=0.10, hi=0.16, lo=0.30, slope=0.10), cover(body))
    belt = np.maximum(body, np.abs(V - 0.16) - 0.022)
    c.paint(dark, cover(belt) * 0.85)
    if view == "front":
        c.paint((0.98, 0.85, 0.45), cover(rbox(0, 0.16, 0.03, 0.026, 0.008)))
    if view == "back":
        c.paint(solid((0.55, 0.38, 0.24), pack, bevel=0.04, hi=0.2, lo=0.28), cover(pack))
        c.paint((0.36, 0.24, 0.15), cover(rbox(0, 0.05, 0.15, 0.016, 0.01)))
    if view == "side":
        arm = ellipse(0.03, 0.10, 0.06, 0.09)
        c.paint(solid(dark * 1.25, arm, bevel=0.05, hi=0.25, lo=0.25), cover(arm))
    head = ellipse(lean, -0.155, 0.175, 0.17)
    c.paint((0, 0, 0), blur(cover(ellipse(lean, -0.10, 0.17, 0.15)), 0.02) * cover(body) * 0.25)
    if view == "back":
        c.paint(solid(cloth * 0.9, head, bevel=0.10, hi=0.14, lo=0.28), cover(head))
    else:
        c.paint(solid(SKIN, head, bevel=0.10, hi=0.10, lo=0.22, slope=0.06), cover(head))
    if view == "front":
        for sx in (-1, 1):
            c.paint((0.98, 0.52, 0.48), blur(cover(ellipse(sx * 0.105, -0.085, 0.036, 0.026)), 0.012) * 0.55)
            eye = ellipse(sx * 0.066, -0.135, 0.023, 0.032)
            c.paint((0.13, 0.10, 0.16), cover(eye))
            c.paint((1, 1, 1), cover(ellipse(sx * 0.066 - 0.007, -0.147, 0.008, 0.009)))
        smile = np.maximum(np.abs(ellipse(0, -0.085, 0.034, 0.026)) - 0.006, -(V + 0.078))
        c.paint((0.55, 0.30, 0.26), cover(smile) * 0.9)
    if view == "side":
        c.paint((0.98, 0.52, 0.48), blur(cover(ellipse(lean + 0.085, -0.085, 0.036, 0.026)), 0.012) * 0.55)
        eye = ellipse(lean + 0.10, -0.135, 0.02, 0.032)
        c.paint((0.13, 0.10, 0.16), cover(eye))
        c.paint((1, 1, 1), cover(ellipse(lean + 0.096, -0.147, 0.007, 0.009)))
        nose = ellipse(lean + 0.175, -0.115, 0.022, 0.02)
        c.paint(solid(SKIN, nose, bevel=0.02, hi=0.1, lo=0.2), cover(nose))
    brim = ellipse(lean * 1.6, -0.265, 0.265, 0.082)
    c.paint((0, 0, 0), blur(cover(ellipse(lean * 1.6, -0.235, 0.24, 0.07)), 0.015) * cover(head) * 0.30)
    c.paint(solid(cloth * 0.78, brim, bevel=0.05, hi=0.22, lo=0.25), cover(brim))
    crown = rbox(lean * 1.6, -0.345, 0.135, 0.085, 0.07)
    c.paint(solid(cloth, crown, bevel=0.06, hi=0.18, lo=0.26), cover(np.maximum(crown, V + 0.268)))
    band = np.maximum(crown, np.abs(V + 0.285) - 0.016)
    c.paint((0.99, 0.93, 0.80), cover(np.maximum(band, V + 0.268)) * 0.9)
    return c.image()


CORAL = (0.96, 0.47, 0.39)
SKY = (0.36, 0.66, 0.97)

SPRITES = [
    ("stone_set_a", lambda: bedded(STONE_SET, 11)),
    ("stone_set_b", lambda: bedded(STONE_SET, 23)),
    ("stone_set_c", lambda: bedded(STONE_SET, 37)),
    ("stone_loose", lambda: slab(STONE_LOOSE, STONE_SIDE, 41)),
    ("ice_set", lambda: bedded((0.50, 0.80, 0.94), 51, 0.012, gloss=ice_gloss)),
    ("ice_loose", lambda: slab((0.70, 0.92, 0.99), (0.28, 0.55, 0.78), 53, 0.010, gloss=ice_gloss)),
    ("portal_set", lambda: bedded((0.33, 0.28, 0.56), 61, 0.03, extra=portal_well)),
    ("portal_loose", lambda: slab((0.45, 0.39, 0.72), (0.24, 0.19, 0.44), 63, 0.025, extra=portal_well)),
    ("rubble", rubble),
    ("crate", crate),
    ("button_up", lambda: button(False)),
    ("button_down", lambda: button(True)),
    ("gate_shut", lambda: gate(True)),
    ("gate_open", lambda: gate(False)),
    ("lantern", lambda: lantern(False)),
    ("lantern_lit", lambda: lantern(True)),
    ("portal_light", glow_ring),
    ("ember_front", lambda: paver(CORAL, "front")),
    ("ember_back", lambda: paver(CORAL, "back")),
    ("ember_side", lambda: paver(CORAL, "side")),
    ("tide_front", lambda: paver(SKY, "front")),
    ("tide_back", lambda: paver(SKY, "back")),
    ("tide_side", lambda: paver(SKY, "side")),
]
COLUMNS, ROWS = 6, 4


def sky():
    """Night: deep blue to violet, two slow clouds of colour, and still stars."""
    w, h = 1280, 720
    rng = np.random.default_rng(7)
    yy, xx = np.mgrid[0:h, 0:w]
    t = yy / h
    img = np.stack([0.030 + 0.075 * t, 0.040 + 0.045 * t, 0.105 + 0.095 * t], axis=2)

    def cloud(seed, cells):
        total = np.zeros((h, w))
        amp = 1.0
        for o in range(5):
            n = cells * 2 ** o
            small = (rng.random((max(2, n * h // w), n)) * 255).astype(np.uint8)
            total += np.asarray(Image.fromarray(small).resize((w, h), Image.BICUBIC)) / 255 * amp
            amp *= 0.5
        return total / 1.94

    a = np.clip(cloud(1, 3) - 0.46, 0, 1) * 2.2
    b = np.clip(cloud(2, 4) - 0.50, 0, 1) * 2.2
    img += a[..., None] * np.array([0.02, 0.10, 0.17]) * (1.1 - t)[..., None]
    img += b[..., None] * np.array([0.15, 0.05, 0.20]) * (0.35 + t)[..., None]
    stars = np.zeros((h, w))
    for _ in range(520):
        x, y = rng.integers(0, w), rng.integers(0, h)
        stars[y, x] = max(stars[y, x], rng.random() ** 2.2)
    soft = np.asarray(Image.fromarray((stars * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.1))) / 255
    img += np.clip(stars * 0.9 + soft * 4.5, 0, 1)[..., None] * np.array([0.86, 0.90, 1.0])
    edge = np.hypot((xx / w - 0.5) * 1.25, (yy / h - 0.48) * 1.35)
    img *= (1 - np.clip(edge - 0.42, 0, 1) * 0.75)[..., None]
    Image.fromarray((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8)).save(DEST / "sky.jpg", quality=90)


def rock():
    """Grain for the slab: light and dark flecks that repeat without a seam."""
    size = 240
    rng = np.random.default_rng(3)

    def wrapped(cells):
        small = (rng.random((cells, cells)) * 255).astype(np.uint8)
        big = Image.fromarray(np.tile(small, (3, 3))).resize((size * 3, size * 3), Image.BICUBIC)
        return np.asarray(big)[size:size * 2, size:size * 2].astype(np.float64) / 255 * 2 - 1

    mottle = wrapped(4) * 0.6 + wrapped(9) * 0.4
    fine = wrapped(60)
    flecks = np.clip(wrapped(40) - 0.55, 0, 1) * 2.2
    value = mottle * 0.5 + fine * 0.35
    light = np.clip(value, 0, 1)
    dark = np.clip(-value, 0, 1) + flecks
    alpha = np.clip(light * 0.10 + dark * 0.16, 0, 1)
    tone = np.where(light * 0.10 > dark * 0.16, 1.0, 0.0)
    out = np.stack([tone, tone, tone * 1.0, alpha], axis=2)
    Image.fromarray((out * 255 + 0.5).astype(np.uint8), "RGBA").save(DEST / "rock.png", optimize=True)


if __name__ == "__main__":
    atlas = Image.new("RGBA", (COLUMNS * CELL, ROWS * CELL), (0, 0, 0, 0))
    for index, (name, make) in enumerate(SPRITES):
        atlas.paste(make(), ((index % COLUMNS) * CELL, (index // COLUMNS) * CELL))
        print(index, name)
    atlas.save(DEST / "atlas.png", optimize=True)
    sky()
    rock()
    print("Wrote atlas.png", atlas.size, "sky.jpg and rock.png")
