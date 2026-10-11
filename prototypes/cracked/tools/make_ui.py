"""Makes CRACKED's two UI sheets from the Unity project's sprites.

    python make_ui.py <the Unity project's folder> [--check]

art/ui/upgrades.png: the cards' pictures, 8 by 5 cells of 192 pixels, a
card's at its number in `Upgrade Database.asset`. A card with no picture
there (Frost Guard) keeps the cell this sheet already has, which was drawn
for the port. art/ui/upgrades-small.png is the same sheet at half the size,
for where a picture is drawn small: the game draws both without mipmaps.

art/ui/kit.png: the Kenney UI sprites the Unity game's HUD and cards are made
of, each in a cell with clear pixels around it (KIT below says where, and
cards.tide has the same numbers). The round ones are there a second time at
40 pixels (SMALL), about the size the HUD draws them at.

It needs Pillow and PyYAML. `--check` makes nothing: it says what it would
read.
"""
import os
import re
import sys

import yaml
from PIL import Image

CELL = 192
COLUMNS = 8
ROWS = 5

# name: (file under the Unity project, x, y) in a sheet of 544 by 720. The
# squares and rounds are 128 across and the rectangles 384 by 128; 16 clear
# pixels are between any two, so a smaller copy of the sheet (its mipmaps)
# never reads a neighbor.
KENNEY = "Packages/dev.purrnet.mainmenu/Art/Sprites/KenneyUi/"
KIT = {
    "square_gloss": (KENNEY + "button_square_gloss.png", 8, 8),
    "square_depth_border": (KENNEY + "button_square_depth_border.png", 152, 8),
    "square_depth_gradient": (KENNEY + "button_square_depth_gradient.png", 296, 8),
    "round_depth_gradient": ("Assets/Art/Rounds/button_round_depth_gradient.png", 8, 152),
    "round_border": ("Assets/Art/Rounds/button_round_border.png", 152, 152),
    "round_depth_border": ("Assets/Art/Rounds/button_round_depth_border.png", 296, 152),
    "round_depth_half_border": ("Assets/Art/Rounds/button_round_depth_half_border.png", 8, 296),
    "rectangle_flat": ("Assets/Art/Sprites/UI/button_rectangle_flat.png", 8, 440),
    "rectangle_gradient": ("Assets/Art/Sprites/UI/button_rectangle_gradient.png", 8, 584),
    "rectangle_depth_gradient": (KENNEY + "button_rectangle_depth_gradient.png", 152, 296),
}
SMALL = {
    "round_depth_gradient": (408, 448),
    "round_border": (408, 504),
    "round_depth_border": (408, 560),
    "round_depth_half_border": (408, 616),
}
SMALL_SIZE = 40
KIT_SIZE = (544, 720)


def guid_of(meta):
    with open(meta, encoding="utf-8") as handle:
        for line in handle:
            if line.startswith("guid:"):
                return line.split()[1]
    return None


def icons(project):
    """Each card's picture, in the database's order: a path, or None."""
    upgrades = os.path.join(project, "Assets", "Prefabs", "Upgrades")
    sprites = os.path.join(project, "Assets", "Art", "Sprites", "Upgrades")
    by_guid = {}
    for root, _, files in os.walk(upgrades):
        for name in files:
            if name.endswith(".asset.meta"):
                by_guid[guid_of(os.path.join(root, name))] = os.path.join(root, name[:-5])
    pictures = {}
    for name in os.listdir(sprites):
        if name.endswith(".png.meta"):
            pictures[guid_of(os.path.join(sprites, name))] = os.path.join(sprites, name[:-5])
    database = open(os.path.join(upgrades, "Upgrade Database.asset"), encoding="utf-8").read()
    listed = database[database.index("_allUpgrades:"):]
    found = []
    for guid in re.findall(r"- \{fileID: 11400000, guid: ([0-9a-f]+)", listed):
        text = open(by_guid[guid], encoding="utf-8").read()
        body = yaml.safe_load(text[text.index("MonoBehaviour:"):])["MonoBehaviour"]
        image = body.get("upgradeImage") or {}
        found.append((os.path.basename(by_guid[guid])[:-6], pictures.get(image.get("guid"))))
    return found


def main():
    arguments = [a for a in sys.argv[1:] if a != "--check"]
    check = "--check" in sys.argv
    if len(arguments) != 1:
        sys.exit(__doc__)
    project = arguments[0]
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "art", "ui")

    cards = icons(project)
    if len(cards) > COLUMNS * ROWS:
        sys.exit("more cards than the sheet has cells: %d" % len(cards))
    sheet_path = os.path.join(out, "upgrades.png")
    old = Image.open(sheet_path).convert("RGBA") if os.path.exists(sheet_path) else None
    old_cell = old.width // COLUMNS if old else 0
    sheet = Image.new("RGBA", (CELL * COLUMNS, CELL * ROWS), (255, 255, 255, 0))
    for number, (name, path) in enumerate(cards):
        at = (number % COLUMNS * CELL, number // COLUMNS * CELL)
        if check:
            print("%2d %-18s %s" % (number, name, os.path.basename(path) if path else "(the sheet's own)"))
            continue
        if path:
            picture = Image.open(path).convert("RGBA")
        elif old:
            x, y = number % COLUMNS * old_cell, number // COLUMNS * old_cell
            picture = old.crop((x, y, x + old_cell, y + old_cell))
        else:
            continue
        # Premultiplied while it's made smaller, so a clear pixel's color
        # doesn't darken the edge it's beside
        picture = picture.convert("RGBa").resize((CELL, CELL), Image.LANCZOS).convert("RGBA")
        sheet.paste(picture, at)

    kit = Image.new("RGBA", KIT_SIZE, (255, 255, 255, 0))
    for name, (path, x, y) in KIT.items():
        sprite = Image.open(os.path.join(project, path)).convert("RGBA")
        if check:
            print("%-26s %3d %3d  %d by %d" % (name, x, y, sprite.width, sprite.height))
            continue
        kit.paste(sprite, (x, y))
        if name in SMALL:
            kit.paste(sprite.convert("RGBa").resize((SMALL_SIZE, SMALL_SIZE), Image.LANCZOS).convert("RGBA"), SMALL[name])
    if check:
        return
    # A clear pixel takes the white of the sprites, so nothing dark shows at their edges
    for image in (sheet, kit):
        pixels = image.load()
        for y in range(image.height):
            for x in range(image.width):
                if pixels[x, y][3] == 0:
                    pixels[x, y] = (255, 255, 255, 0)
    small = sheet.convert("RGBa").resize((sheet.width // 2, sheet.height // 2), Image.LANCZOS).convert("RGBA")
    pixels = small.load()
    for y in range(small.height):
        for x in range(small.width):
            if pixels[x, y][3] == 0:
                pixels[x, y] = (255, 255, 255, 0)
    sheet.save(sheet_path, optimize=True)
    small.save(os.path.join(out, "upgrades-small.png"), optimize=True)
    kit.save(os.path.join(out, "kit.png"), optimize=True)
    print("upgrades.png %d by %d, kit.png %d by %d" % (sheet.width, sheet.height, kit.width, kit.height))


if __name__ == "__main__":
    main()
