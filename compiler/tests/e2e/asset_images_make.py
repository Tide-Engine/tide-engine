"""Regenerate checked-in image fixtures. Pillow is only needed to regenerate JPEGs.

The build and tests read the checked-in bytes and need neither Python nor Pillow.
BMP/TGA fixtures are written from their format layouts; JPEG reference pixels
come from Pillow/libjpeg, independently of Tide's stb_image decoder.
"""
from pathlib import Path
import struct
from PIL import Image

root = Path(__file__).parent / "assets" / "images"
root.mkdir(parents=True, exist_ok=True)
(root / "bad").mkdir(exist_ok=True)

rgb = Image.new("RGB", (8, 6))
rgb.putdata([(x * 29, y * 41, (x * 17 + y * 23) % 256) for y in range(6) for x in range(8)])
for name, mode, progressive in [("baseline", "RGB", False), ("progressive", "RGB", True),
                                ("gray", "L", False), ("cmyk", "CMYK", False)]:
    image = rgb.convert(mode)
    image.save(root / f"{name}.jpg", quality=95, subsampling=0, progressive=progressive)
    with Image.open(root / f"{name}.jpg") as decoded:
        (root / f"{name}.rgba").write_bytes(decoded.convert("RGBA").tobytes())
rgb.save(root / "reference.png")

pixels = [(255, 0, 0, 255), (0, 255, 0, 128), (51, 102, 153, 0),
          (0, 0, 255, 255), (0, 0, 255, 255), (51, 102, 153, 64)]
(root / "colors.rgba").write_bytes(bytes(c for p in pixels for c in p))

def tga(name, descriptor=0x28, rle=False, indexed=False, gray=False, bits=32):
    width, height = 3, 2
    header = bytearray(18)
    header[1] = int(indexed)
    header[2] = (1 if indexed else 3 if gray else 2) + (8 if rle else 0)
    if indexed:
        struct.pack_into("<HHB", header, 3, 7, 3, 32)
    struct.pack_into("<HHBB", header, 12, width, height, bits, descriptor)
    order = [pixels[(height - 1 - y if not descriptor & 32 else y) * width
                    + (width - 1 - x if descriptor & 16 else x)]
             for y in range(height) for x in range(width)]
    if indexed:
        palette = [pixels[0], pixels[1], pixels[3]]
        order = [7, 8, 9, 9, 9, 7]
        body = bytes(c for r, g, b, a in palette for c in (b, g, r, a))
        units = [bytes([v]) for v in order]
    elif gray:
        body = b""
        units = [bytes([p[0], p[3]]) if bits == 16 else bytes([p[0]]) for p in order]
    elif bits in (15, 16):
        body = b""
        units = [struct.pack("<H", (p[0] >> 3) << 10 | (p[1] >> 3) << 5 | (p[2] >> 3)) for p in order]
    else:
        body = b""
        units = [bytes((p[2], p[1], p[0], p[3])) if bits == 32 else bytes((p[2], p[1], p[0])) for p in order]
    if rle:
        i = 0
        while i < len(units):
            n = 1
            while i + n < len(units) and units[i + n] == units[i] and n < 128:
                n += 1
            body += bytes([0x80 | (n - 1)]) + units[i] if n > 1 else bytes([0]) + units[i]
            i += n
    else:
        body += b"".join(units)
    (root / name).write_bytes(header + body)

for descriptor, orient in [(0x08, "bottom_left"), (0x18, "bottom_right"),
                            (0x28, "top_left"), (0x38, "top_right")]:
    tga(f"raw_{orient}.tga", descriptor)
    tga(f"rle_{orient}.tga", descriptor, rle=True)
tga("rgb24.tga", 0x20, bits=24)
tga("rgb16.tga", 0x20, bits=16)
tga("gray8.tga", 0x20, gray=True, bits=8)
tga("gray_alpha16.tga", 0x28, gray=True, bits=16)
tga("indexed.tga", 0x28, indexed=True, bits=8)
tga("indexed_rle.tga", 0x28, rle=True, indexed=True, bits=8)

def bmp(name, bpp=24, top=False, hsz=40, bitfields=False, indexed=False):
    width, height = 3, 2
    masks = (0xf800, 0x07e0, 0x001f, 0) if bpp == 16 else (0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000)
    dib = bytearray(hsz)
    struct.pack_into("<IiiHHIIiiII", dib, 0, hsz, width, -height if top else height, 1, bpp,
                     3 if bitfields else 0, 0, 0, 0, 0, 0)
    extra = b""
    if bitfields:
        if hsz == 40:
            extra = struct.pack("<III", *masks[:3])
        else:
            struct.pack_into("<IIII", dib, 40, *masks)
    palette = b""
    if indexed:
        palette = bytes(c for p in [(255, 0, 0), (0, 255, 0), (0, 0, 255)][:2 ** bpp]
                        for c in (p[2], p[1], p[0], 0))
    body = b""
    for row in range(height):
        source = pixels[(row if top else height - 1 - row) * width:][:width]
        if indexed:
            indices = [0, 1, 0] if bpp == 1 else [0, 1, 2]
            packed = bytes(indices) if bpp == 8 else bytes([(indices[0] << 4) | indices[1], indices[2] << 4]) if bpp == 4 else bytes([0x40])
        elif bpp == 16:
            packed = b"".join(struct.pack("<H", (p[0] >> 3) << 11 | (p[1] >> 2) << 5 | (p[2] >> 3)) for p in source)
        else:
            packed = bytes(c for p in source for c in ((p[2], p[1], p[0], p[3]) if bpp == 32 else (p[2], p[1], p[0])))
        body += packed + bytes((-len(packed)) % 4)
    offset = 14 + len(dib) + len(extra) + len(palette)
    header = b"BM" + struct.pack("<IHHI", offset + len(body), 0, 0, offset)
    (root / name).write_bytes(header + dib + extra + palette + body)

bmp("rgb24.bmp")
bmp("rgb24_top.bmp", top=True)
bmp("rgb16_bitfields.bmp", bpp=16, bitfields=True)
for hsz in [56, 108, 124]:
    bmp(f"rgba32_v{hsz}.bmp", bpp=32, hsz=hsz, bitfields=True)
for bpp in [1, 4, 8]:
    bmp(f"indexed{bpp}.bmp", bpp=bpp, indexed=True)

def bad(name, data):
    (root / "bad" / name).write_bytes(data)

jpeg = (root / "baseline.jpg").read_bytes()
frame = jpeg.index(b"\xff\xc0")
lossless = bytearray(jpeg)
lossless[frame + 1] = 0xc3
bad("lossless.jpg", lossless)
twelve = bytearray(jpeg)
twelve[frame + 4] = 12
bad("twelve_bit.jpg", twelve)
bad("cut.jpg", jpeg[:-2])
compressed = bytearray((root / "rgb24.bmp").read_bytes())
struct.pack_into("<I", compressed, 30, 1)
bad("rle.bmp", compressed)
bad("cut.bmp", (root / "rgb24.bmp").read_bytes()[:-1])
bad("cut.tga", (root / "raw_top_left.tga").read_bytes()[:-1])
rle = bytearray((root / "rle_top_left.tga").read_bytes())
rle[18] = 0xff
bad("overrun.tga", rle)
alpha = bytearray((root / "rgb16.tga").read_bytes())
alpha[17] |= 1
bad("alpha16.tga", alpha)
indexed = bytearray((root / "indexed.tga").read_bytes())
indexed[30] = 6
bad("index.tga", indexed)
bad("unsupported.webp", b"RIFF\x04\0\0\0WEBP")
bad("unknown.bin", b"this is not an image")
bad("broken.png", b"\x89PNG\r\n\x1a\n")
huge = bytearray((root / "reference.png").read_bytes())
struct.pack_into(">I", huge, 16, 20000)
bad("large.png", huge)
