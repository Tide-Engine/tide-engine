# GRAVEWAKE art credits

Every model and texture here is from Quaternius' packs, released as CC0 1.0
(public domain), from CoinCoin's first-person kit (CC BY 4.0, which asks for
the credit below), or modelled for this game by the scripts beside it. Each
folder's README (and its tools' headers) say which file came from where and
what was changed.

**First-person arms** (`fp/`): "Animated Bow", "Animated Tomahawk" and
"Animated Knife and Firesteel" by **CoinCoin**, licensed under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/), from
<https://opengameart.org/content/animated-bow>,
<https://opengameart.org/content/animated-tomahawk> and
<https://opengameart.org/content/animated-knife-and-firesteel>. Changed: the
three files' arms joined on one skeleton, recoloured in flat colours (gloves,
bracers, sleeves; the bow's wood, grip and string), clips resampled to 30 a
second, the left hand mirrored onto two-handed grips, and further clips made
on the same skeleton by `fp/tools/make_fp.py`. The credit is also on the
game's title screen.

| Folder | What | Sources |
| --- | --- | --- |
| `kit/`, `tex/` | The village: walls, roofs, floors, doors, furniture, props, trees; their textures | Medieval Village MegaKit, Fantasy Props MegaKit, Stylized Nature MegaKit (the Standard downloads), cut into one GLB per piece and texture by `kit/tools/split_kit.py`, textures shrunk and shaded by `kit/tools/make_textures.py` |
| `people/` | Six of the dead, four survivors, and their clips | Ultimate Modular Men, Ultimate Modular Women (bodies); the men's pack's clips and Universal Animation Library 1 and 2 (motions), retargeted by `people/tools/make_moves.py` |
| `weapons/` | Weapons, missiles and first-person hands | Medieval Weapons, Ultimate RPG (staff crystals); the maces, polearms, crossbows, handgonne, staffs, thrown weapons, bolt, bell and hands modelled by `weapons/tools/make_weapons.py` |
| `items/` | The chest, power-ups, potions, the beacon's lever, brazier, planks, graves, the well | Ultimate RPG (skull, coins, chalice, potions), Medieval Weapons (arrows, hammer); the rest modelled by `items/tools/make_items.py` |
| `fp/` | The first-person arms, the bow and arrow in them, and every first-person clip | CoinCoin's first-person kit (CC BY 4.0, above); clips for two-handed weapons, polearms, crossbows, staffs, throwing and drinking made by `fp/tools/make_fp.py` |

The packs' pages:

- <https://quaternius.com/packs/medievalvillagemegakit.html>
- <https://quaternius.com/packs/fantasypropsmegakit.html>
- <https://quaternius.com/packs/stylizednaturemegakit.html>
- <https://quaternius.com/packs/ultimatemodularcharacters.html>
- <https://quaternius.com/packs/ultimatemodularwomen.html>
- <https://quaternius.com/packs/universalanimationlibrary.html>
- <https://quaternius.com/packs/universalanimationlibrary2.html>
- <https://quaternius.com/packs/medievalweapons.html>
- <https://quaternius.com/packs/ultimaterpg.html>

The sounds' sources are in `../sfx/CREDITS.md`.
