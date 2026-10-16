# Lays out preview_render.py's renders (plain Python with Pillow):
#
#   python preview_sheet.py <png dir> [--json weapons.json] [--sheet out.png] [--ppm 300]
#
# Each <name>_side/_back/_34.png trio becomes <name>.png with captions. With --sheet, the
# sheet_<name>.png renders (all at one scale) go side by side in rows, labelled, with a 10 cm bar.
import sys, os, json
from PIL import Image, ImageDraw

args = sys.argv[1:]
DIR = args[0]


def arg(name, default=None):
    return args[args.index(name) + 1] if name in args else default


JS, SHEET, PPM = arg("--json"), arg("--sheet"), float(arg("--ppm", "300"))
order = [e["name"] for e in json.load(open(JS))] if JS else None
BG = (56, 58, 64)

trios = sorted({f[:-9] for f in os.listdir(DIR) if f.endswith("_side.png")})
for n in trios:
    ims = [Image.open(os.path.join(DIR, f"{n}_{v}.png")).convert("RGB") for v in ("side", "back", "34")]
    w, h = sum(i.width for i in ims), max(i.height for i in ims) + 22
    out = Image.new("RGB", (w, h), BG)
    d = ImageDraw.Draw(out)
    x = 0
    for im, cap in zip(ims, ("from +X: +Z right, +Y up", "from behind: +X right, +Y up", "three-quarter, back faces culled")):
        out.paste(im, (x, 22))
        d.text((x + 6, 5), f"{n}  {cap}", fill=(235, 235, 235))
        x += im.width
    out.save(os.path.join(DIR, f"{n}.png"))

if SHEET:
    names = [f[6:-4] for f in os.listdir(DIR) if f.startswith("sheet_") and f.endswith(".png")]
    if order:
        names = [n for n in order if n in names] + [n for n in names if n not in order]
    tiles = []
    for n in names:
        im = Image.open(os.path.join(DIR, f"sheet_{n}.png")).convert("RGBA")
        bb = im.getbbox() or (0, 0, 1, 1)
        tiles.append((n, im.crop(bb)))
    maxw = 3000
    rows, row, rw = [], [], 0
    for n, im in tiles:
        tw = max(im.width, 70) + 18
        if row and rw + tw > maxw:
            rows.append(row)
            row, rw = [], 0
        row.append((n, im))
        rw += tw
    rows.append(row)
    H = sum(max(im.height for _, im in r) + 40 for r in rows) + 60
    out = Image.new("RGB", (maxw + 40, H), BG)
    d = ImageDraw.Draw(out)
    y = 20
    for r in rows:
        rh = max(im.height for _, im in r)
        x = 20
        for n, im in r:
            tw = max(im.width, 70)
            out.paste(im, (x + (tw - im.width) // 2, y + rh - im.height), im)
            d.text((x, y + rh + 6), n, fill=(235, 235, 235))
            x += tw + 18
        y += rh + 40
    d.line((20, H - 30, 20 + PPM * 0.1, H - 30), fill=(255, 220, 0), width=3)
    d.text((26 + PPM * 0.1, H - 37), "10 cm (every model at the same scale)", fill=(255, 220, 0))
    out.save(SHEET)
