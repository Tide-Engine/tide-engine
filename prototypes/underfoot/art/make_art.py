"""Underfoot's pictures: what is cut or painted on the tops of things.

The game is drawn in 3D (paint.tide): slabs, walls, coffins and keepers are
meshes it builds, lit by one cold light and by candles. What a mesh cannot say
is here: the red eye on a slab that lifts, a snowflake, a portal's star, a
coffin's cross, cracks in the floor, a ring. Each is flat colour on nothing, a
decal the game lays on a top face and lights with it.

Nothing is drawn by hand and nothing is borrowed: each is shapes given as
distances and filled. Run from any directory; it writes decals.png beside
itself. Needs numpy and Pillow. Eight decals of 250 pixels, four across, in
the order of DECALS; paint.tide's `Mark` enum is that order.
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


def eye_motif(c, line):
    """An eye cut in the slab: what marks a slab as one that lifts."""
    c.fill(line, np.abs(rbox(0, 0, HALF - 0.065, HALF - 0.065, 0.02)) - 0.011)
    almond = np.maximum(ellipse(0, 0.20, 0.36, 0.36), ellipse(0, -0.20, 0.36, 0.36))
    c.fill(line, np.maximum(almond, -(almond + 0.03)))
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


def eye():
    c = Canvas()
    eye_motif(c, BLOOD)
    return c


def frost():
    c = Canvas()
    frost_motif(c, (1, 1, 1))
    return c


def star():
    """What burns in a portal: a ring and a five-pointed star, in white."""
    c = Canvas()
    c.fill((1, 1, 1), np.abs(ellipse(0, 0, 0.33, 0.33)) - 0.022)
    points = [(np.sin(k * 2 * np.pi / 5) * 0.31, -np.cos(k * 2 * np.pi / 5) * 0.31) for k in range(5)]
    for k in range(5):
        a, b = points[k], points[(k + 2) % 5]
        c.fill((1, 1, 1), segment(a[0], a[1], b[0], b[1], 0.015))
    return c


def cross():
    """A coffin's lid: a frame and a pale cross."""
    c = Canvas()
    c.fill((0.24, 0.15, 0.10), np.maximum(rbox(0, 0, 0.5, 0.5), -rbox(0, 0, 0.40, 0.40, 0.02)))
    c.fill(PALE, rbox(0, 0.02, 0.04, 0.30))
    c.fill(PALE, rbox(0, -0.10, 0.19, 0.04))
    return c


def crack(seed):
    c = Canvas()
    rng = np.random.default_rng(seed)
    at = np.array([-0.5, rng.uniform(-0.3, 0.3)])
    fork = None
    for step in range(5):
        to = at + np.array([rng.uniform(0.15, 0.28), rng.uniform(-0.22, 0.22)])
        c.fill(STONE_CRACK, np.maximum(segment(at[0], at[1], to[0], to[1], 0.011), rbox(0, 0, 0.5, 0.5)))
        if step == 2:
            fork = at.copy()
        at = to
    c.fill(STONE_CRACK, np.maximum(segment(fork[0], fork[1], fork[0] + 0.16, fork[1] - 0.26, 0.009), rbox(0, 0, 0.5, 0.5)))
    return c


def ring():
    c = Canvas()
    c.fill((1, 1, 1), np.abs(ellipse(0, 0, 0.36, 0.36)) - 0.03)
    return c


def border():
    """A loose slab's line and nothing else: what a loose plate lies on."""
    c = Canvas()
    c.fill(BLOOD, np.abs(rbox(0, 0, HALF - 0.065, HALF - 0.065, 0.02)) - 0.011)
    return c


DECALS = [eye, frost, star, cross, lambda: crack(3), lambda: crack(8), ring, border]
SIZE = 250

if __name__ == "__main__":
    sheet = Image.new("RGBA", (4 * SIZE, 2 * SIZE), (0, 0, 0, 0))
    inset = (CELL - TILE) // 2
    for index, make in enumerate(DECALS):
        full = make().image()
        top = full.crop((inset, inset, inset + TILE, inset + TILE)).resize((SIZE, SIZE), Image.LANCZOS)
        sheet.paste(top, ((index % 4) * SIZE, (index // 4) * SIZE))
    sheet.save(DEST / "decals.png", optimize=True)
    print("Wrote decals.png", sheet.size)
