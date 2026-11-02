"""Reproduce the Render2D demonstration's analytic color and normal maps.

Run with Python's standard library; there are no downloaded source images.
"""
import math
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parent
SIZE = 128


def png(name, pixels):
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data)))
    data = b"".join(b"\0" + bytes(row) for row in pixels)
    head = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)
    (ROOT / name).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", head)
                             + chunk(b"IDAT", zlib.compress(data, 9)) + chunk(b"IEND", b""))


def byte(value):
    return round(max(0, min(1, value)) * 255)


def tile(x, y):
    u, v = (x % 32) / 32, (y % 32) / 32
    edge = min(u, 1 - u, v, 1 - v)
    return max(0, min(1, (edge - .035) / .09))


def plate(x, y):
    r = math.hypot((x - 63.5) / 60, (y - 63.5) / 60)
    rim = max(0, min(1, (1 - r) * 15))
    return rim * (.6 + .25 * math.cos(r * math.tau * 4))


def fern(x, y):
    u, v = (x - 63.5) / 64, (y - 63.5) / 64
    height = 0
    for k in range(9):
        angle = k * math.tau / 9 + .3
        along = u * math.cos(angle) + v * math.sin(angle)
        across = -u * math.sin(angle) + v * math.cos(angle)
        ellipse = ((along - .38) / .53) ** 2 + (across / .13) ** 2
        height = max(height, max(0, 1 - ellipse))
    return height


for name, height in (("floor", tile), ("plate", plate), ("fern", fern)):
    colors, normals = [], []
    for y in range(SIZE):
        color_row, normal_row = [], []
        for x in range(SIZE):
            h = height(x, y)
            dx = (height(x + 1, y) - height(x - 1, y)) * 3
            dy = (height(x, y + 1) - height(x, y - 1)) * 3
            length = math.sqrt(dx * dx + dy * dy + 1)
            grain = .025 * math.sin(x * 8.234 + y * 6.821)
            if name == "floor":
                stagger = .035 * (((x // 32) + (y // 32)) % 2)
                tint = (.25 + stagger, .30 + stagger, .34 + stagger)
                alpha = 1
            elif name == "plate":
                tint = (.8, .47, .19)
                alpha = 1 if math.hypot(x - 63.5, y - 63.5) < 59.5 else 0
            else:
                tint = (.15, .52, .31)
                alpha = max(0, min(1, h * 20))
            color_row.extend((*[byte(c * (.65 + .35 * h) + grain) for c in tint], byte(alpha)))
            # Image rows go down; Render2D's green normal axis points up.
            normal_row.extend((byte(.5 - dx / length * .5), byte(.5 + dy / length * .5),
                               byte(.5 + .5 / length), 255))
        colors.append(color_row)
        normals.append(normal_row)
    png(name + ".png", colors)
    png(name + "-normal.png", normals)

lamp = []
for y in range(SIZE):
    row = []
    for x in range(SIZE):
        r = math.hypot(x - 63.5, y - 63.5) / 64
        ring = .60 < r < .85 or r < .22
        row.extend((255, 255, 255, 255 if ring else 0))
    lamp.append(row)
png("lamp.png", lamp)
