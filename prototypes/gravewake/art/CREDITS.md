# GRAVEWAKE art credits

Every model and texture here is from Quaternius' packs, released as CC0 1.0
(public domain), or modelled for this game by the scripts beside it. Each
folder's README (and its tools' headers) say which file came from where and
what was changed.

| Folder | What | Sources |
| --- | --- | --- |
| `kit/`, `tex/` | The clearing: trees, rocks, ferns, grass, the ruin's walls and floor, the iron fence, the cart and camp props; their textures | Medieval Village MegaKit, Fantasy Props MegaKit, Stylized Nature MegaKit (the Standard downloads), cut into one GLB per piece and texture by `kit/tools/split_kit.py`, textures shrunk and shaded by `kit/tools/make_textures.py` |
| `people/` | Six of the dead, four survivors, and their clips | Ultimate Modular Men, Ultimate Modular Women (bodies); the men's pack's clips and Universal Animation Library 1 and 2 (motions), retargeted by `people/tools/make_moves.py` |
| `weapons/` | Weapons, missiles and first-person hands | Medieval Weapons, Ultimate RPG (staff crystals); the maces, polearms, crossbows, handgonne, staffs, thrown weapons, bolt, bell and hands modelled by `weapons/tools/make_weapons.py` |
| `items/` | Power-ups, potions, the brazier, graves | Ultimate RPG (skull, coins, chalice, potions), Medieval Weapons (arrows, hammer); the rest modelled by `items/tools/make_items.py` |
| `guns/` | The survivors' guns and the turret's tripod | Ultimate Gun Pack, cut to one GLB each, recoloured and scaled by `guns/tools/make_guns.py` |
| `crowd/` | The horde: the six zombie bodies cut to about 700 triangles and their clips baked into `anim.png` | Made from `people/` by `crowd/tools/make_crowd.py` |
| `mutants/` | The three bosses: three of the dead reshaped (bones scaled and bent, growths added), and their clips baked into `anim.png` | Made from `people/` by `mutants/tools/make_mutants.py` (see `mutants/README.md`) |

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
- <https://opengameart.org/content/low-poly-guns-pack> (the Ultimate Gun Pack, CC0)

The sounds' sources are in `../sfx/CREDITS.md`.
