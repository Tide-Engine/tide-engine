"""Rebuild this example's authored PNG maps and quad with Python's standard library.

Run: python docs/examples/assets/render3d/make_materials.py
No source photographs, image libraries or downloaded assets are used.
"""

import math
from pathlib import Path
import struct
import zlib


ROOT = Path(__file__).resolve().parent
SIZE = 128


def png(name, pixels):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    rows = b"".join(b"\0" + bytes(row) for row in pixels)
    header = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)
    (ROOT / name).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
                             + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def byte(value):
    return round(max(0, min(1, value)) * 255)


def height(x, y):
    # Four repeating raised tiles. The edge trough gives AO a visible purpose.
    u, v = (x % 32) / 32, (y % 32) / 32
    edge = min(u, 1 - u, v, 1 - v)
    bevel = max(0, min(1, (edge - 0.035) / 0.10))
    return bevel * (0.72 + 0.10 * math.sin(u * math.tau) * math.sin(v * math.tau))


maps = {name: [] for name in ("tiles-base.png", "tiles-normal.png", "tiles-orm.png",
                             "tiles-height.png", "tiles-emission.png", "leaves.png")}
for y in range(SIZE):
    rows = {name: [] for name in maps}
    for x in range(SIZE):
        h = height(x, y)
        dx = (height(x + 1, y) - height(x - 1, y)) * 2.2
        dy = (height(x, y + 1) - height(x, y - 1)) * 2.2
        length = math.sqrt(dx * dx + dy * dy + 1)
        nx, ny, nz = -dx / length, -dy / length, 1 / length
        alternate = ((x // 32) + (y // 32)) % 2
        tint = (0.23, 0.63, 0.57) if alternate else (0.86, 0.59, 0.20)
        color = tuple(byte(c * (0.30 + 0.70 * h)) for c in tint)
        rows["tiles-base.png"].extend((*color, 255))
        rows["tiles-normal.png"].extend((byte(nx * 0.5 + 0.5), byte(ny * 0.5 + 0.5),
                                          byte(nz * 0.5 + 0.5), 255))
        # R occlusion, G roughness, B metallic. No sRGB transfer on these numbers.
        rows["tiles-orm.png"].extend((byte(0.15 + 0.85 * h), byte(0.25 + 0.65 * alternate),
                                      byte(0.15 + 0.85 * (1 - alternate)), 255))
        rows["tiles-height.png"].extend((byte(h), byte(h), byte(h), 255))
        glow = 1 if 2 <= x % 32 <= 3 and 7 <= y % 32 <= 24 else 0
        rows["tiles-emission.png"].extend((255 * glow, 120 * glow, 16 * glow, 255))

        # A leaf cluster with real empty space and soft alpha near its edges.
        u, v = (x + 0.5) / SIZE, (y + 0.5) / SIZE
        coverage = max(0, 1 - abs(u - 0.5) / 0.018) if 0.08 < v < 0.94 else 0
        for cx, cy, angle, radius in ((0.31, 0.27, -0.7, 0.21), (0.69, 0.39, 0.7, 0.22),
                                      (0.30, 0.59, -0.6, 0.23), (0.67, 0.77, 0.7, 0.20)):
            a, b = u - cx, v - cy
            along = a * math.cos(angle) + b * math.sin(angle)
            across = -a * math.sin(angle) + b * math.cos(angle)
            ellipse = (along / radius) ** 2 + (across / (radius * 0.46)) ** 2
            coverage = max(coverage, max(0, min(1, (1 - ellipse) * 16)))
        vein = 0.12 * math.sin((u * 9 + v * 7) * math.tau)
        rows["leaves.png"].extend((byte(0.22 + vein), byte(0.65 + vein), byte(0.29), byte(coverage)))
    for name in maps:
        maps[name].append(rows[name])

for name, pixels in maps.items():
    png(name, pixels)

(ROOT / "quad.obj").write_text("""# XY card, two triangles, UVs matching the authored PNGs.
v -0.5 -0.5 0
v 0.5 -0.5 0
v 0.5 0.5 0
v -0.5 0.5 0
vt 0 0
vt 1 0
vt 1 1
vt 0 1
vn 0 0 1
f 1/1/1 2/2/1 3/3/1
f 1/1/1 3/3/1 4/4/1
""", encoding="utf-8", newline="\n")
