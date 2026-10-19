# GRAVEWAKE items

Static props, one `.glb` each: one mesh, flat colours through material base colours (no textures),
metres. Axes are the game's: +X right, +Y up, +Z forward (an item's front faces +Z); Tide's importer
negates glTF's x, so the files hold -x. The origin is the bottom centre unless the table says
otherwise. `tools/make_items.py` rebuilds them from the source packs (see its header); it uses the
helpers in `../weapons/tools/lowpoly.py`. Rebuilding writes the same bytes.

The village's chest, beacon lever, window planks, coin and hammer power-ups and well are still in
`tools/make_items.py`, but not in `ITEMS`: the forest uses none of them, so they're no longer made or
packed.

## Glow

The potions and the brazier carry TEXCOORD_1: (1, 0) on the potions' liquid and the brazier's coals,
(0, 0) on the rest of those files. The other files have no TEXCOORD_1. TEXCOORD_0 is (0, 0).

The potions' glass is opaque: the glass below the liquid's level is coloured as the liquid (and
marked to glow), the glass above it is a dim blue-grey.

## The items

Sizes are the model's box, X by Y by Z, in metres.

| file | size | tris | origin | what |
|---|---|---|---|---|
| pu_max_ammo | 0.19 x 0.51 x 0.15 | 1808 | **centre** | a quiver full of arrows |
| pu_instakill | 0.38 x 0.50 x 0.38 | 336 | **centre** | a skull, facing +Z |
| pu_nuke | 0.33 x 0.50 x 0.33 | 380 | **centre** | a golden chalice |
| potion_red | 0.15 x 0.22 x 0.15 | 1392 | bottom centre | round flask, red |
| potion_blue | 0.17 x 0.24 x 0.17 | 1648 | bottom centre | shouldered bottle, blue |
| potion_green | 0.14 x 0.24 x 0.14 | 1648 | bottom centre | waisted bottle, green |
| potion_amber | 0.21 x 0.20 x 0.21 | 1392 | bottom centre | wide flat flask, amber |
| potion_violet | 0.14 x 0.24 x 0.14 | 1648 | bottom centre | teardrop bottle, violet |
| potion_white | 0.14 x 0.24 x 0.14 | 1904 | bottom centre | ribbed bottle, white |
| brazier | 1.04 x 1.42 x 1.08 | 1278 | bottom centre | wrought iron on three curled legs, a 1.04 m bowl of coals |
| gravestone_a | 0.78 x 1.10 x 0.28 | 252 | bottom centre | rounded headstone on a plinth, a cross cut in it |
| gravestone_b | 0.62 x 1.30 x 0.36 | 404 | bottom centre | celtic cross on a stepped base |
| gravestone_c | 0.62 x 0.92 x 0.29 | 192 | bottom centre | old pointed slab, leaning back, a corner broken off |
| grave_mound | 1.00 x 0.30 x 1.95 | 318 | bottom centre | a low heap of dirt, long along Z |

## Sources

The skull, coins, chalice and potions are Quaternius' **Ultimate RPG** pack
(https://quaternius.com/packs/ultimaterpg.html: Skull, Coin, Coin_Star, Coin_Skull, Chalice,
Potion1/2/3/4/5/8_Filled, recoloured); the quiver's arrows and the carpenter's hammer are his
**Medieval Weapons** pack (https://quaternius.com/packs/medievalweapons.html: Arrow,
Hammer_Small). Both are CC0 1.0 (public domain). Everything else was modelled in
`tools/make_items.py` for this project.
