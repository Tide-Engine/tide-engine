# Art credits

`character.fbx`, `idle.fbx`, `run.fbx`, `jump.fbx`, `operator-a.png` and
`operator-b.png` are from Kenney's Animated Characters Retro 1.1
(<https://kenney.nl/assets/animated-characters-retro>), renamed and otherwise
unchanged.

`kit.png` is a 256 by 256 sheet of sixteen 64-texel squares. Its first twelve
are textures of Kenney's Retro Urban Kit
(<https://kenney.nl/assets/retro-urban-kit>): doors, wall, concrete, tiles,
metal, planks, signs, windows, wall_lines, metal_wall, wall_garage and bars,
in that order, row by row. The last four (wallpaper, carpet, ceiling tile and
plaster) were generated for this game.

Kenney's assets are Creative Commons Zero (CC0):
<http://creativecommons.org/publicdomain/zero/1.0/>.

The sounds in `sfx/` were made for this game by `make_sfx.py`, from sines and
noise: no recording is in them. Run it with the folder to write them again.

The skinned creatures in `monsters/` use the following sources. All prepared
models face +Z, use metres, and carry their clips in the GLB; the separate
skins are sampled with point filtering. The `monsters/tools/make_*.py` scripts
retain the preparation steps and accept the source files as arguments.

- `flayed.glb`, `flayed.png`, `flayed_dark.png`: "Horror Game Monster" by
  HorrorGameMaker (HorrorGameMaker.com),
  <https://opengameart.org/content/3d-horror-game-monster>, CC0. Kept one mesh
  and rig, cleaned and uniquely renamed joints, limited influences to four,
  rescaled to 2 m, combined the source clips, removed travel, closed loops,
  authored Lunge and Twitch, and reduced both skins to 128 px with increased
  contrast and 32 colour levels per channel. Prepared by `make_flayed.py`.
- `crawler.glb`, `crawler.png`: "Crawlmaster" by Teh_Bucket, textures by
  Joao Paulo (3dtextures.me), from "Nightmare Monsters",
  <https://opengameart.org/content/nightmare-monsters>, CC0 (the source archive
  includes the CC0 1.0 licence). Reposed onto four limbs, renamed and cleaned
  the rig, rescaled to 2.2 m long, combined its texture layers into one 128 px
  skin, and authored Idle, Crawl, Rush and Lunge. Prepared by
  `make_crawler.py` with `common_crawler.py`.
- `spider.glb`, `spider.png`: "Spider" by Quaternius, Easy Enemy Pack,
  <https://quaternius.com/packs/easyenemy.html>, obtained from
  <https://poly.pizza/m/yRYJiAJyiM>, CC0 1.0. Cleaned the rig, filled omitted
  animation channels from the source defaults, applied transforms, rescaled
  to a 2.29 m leg span, renamed clips, and replaced materials with UVs into a
  generated 64 px palette with separate eye colours. Prepared by
  `make_spider.py` with `common_spider.py`.
- `figure.glb`, `figure_mannequin.png`, `figure_shadow.png`: "Man" by
  Quaternius, Animated Men Pack,
  <https://quaternius.com/packs/animatedmen.html>, obtained from
  <https://poly.pizza/m/HMnuH5geEG>, CC0 1.0. Removed facial details, rounded
  the head, cleaned and rescaled the rig to 1.8 m, filled omitted animation
  channels from source defaults, generated two 64 px palette skins, and
  authored Creep and the static Stand. Prepared by `make_figure.py`.
- `zombie.glb`, `zombie.png` (the drowned corpse in code): "Animated Zombie"
  by Quaternius, <https://quaternius.com/packs/animatedzombie.html>, obtained
  from <https://poly.pizza/m/jkrEvQZb8J>. The author's pack is CC0 1.0;
  poly.pizza labels its conversion CC-BY 3.0, so Quaternius and both sources
  are credited here. Cleaned and rescaled to 1.75 m, turned the rest pose to
  +Z, preserved source defaults for omitted channels, closed and renamed
  clips, bloated the body, lifted the Crawl, and unwrapped/retextured it as a
  drowned corpse with a 64 px skin. Prepared by `make_zombie.py`.

CC0 1.0: <http://creativecommons.org/publicdomain/zero/1.0/>.
CC-BY 3.0: <https://creativecommons.org/licenses/by/3.0/>.
