# GRAVEWAKE items

Static props, one `.glb` each: one mesh, flat colours through material base colours (no textures),
metres. Axes are the game's: +X right, +Y up, +Z forward (an item's front faces +Z); Tide's importer
negates glTF's x, so the files hold -x. The origin is the bottom centre unless the table says
otherwise. `tools/make_items.py` rebuilds them from the source packs (see its header); it uses the
helpers in `../weapons/tools/lowpoly.py`. Rebuilding writes the same bytes.

## Mystery chest

`chest_closed`, `chest_open_base` and `chest_lid` are one iron-banded chest with a skull on its lock
plate, front toward +Z.

- `chest_open_base` is the box without its lid: hollow, dark inside.
- `chest_lid` has its **hinge axis along X through its origin**, at the lid's back bottom edge;
  closed, the lid spans z 0..0.60 and y 0..0.20 in its own file, with the hasp hanging below y = 0.
- Place the lid at **(0, 0.40, -0.30)** from the base's origin and the two look exactly like
  `chest_closed`. To open it, turn it about X so that its front (+Z) rises toward +Y (Unity-style
  Euler (-angle, 0, 0)); about 105 degrees is wide open.

## Glow

The potions and the brazier carry TEXCOORD_1: (1, 0) on the potions' liquid and the brazier's coals,
(0, 0) on the rest of those files. The other files have no TEXCOORD_1. TEXCOORD_0 is (0, 0).

The potions' glass is opaque: the glass below the liquid's level is coloured as the liquid (and
marked to glow), the glass above it is a dim blue-grey.

## The items

Sizes are the model's box, X by Y by Z, in metres.

| file | size | tris | origin | what |
|---|---|---|---|---|
| chest_closed | 1.16 x 0.61 x 0.72 | 3320 | bottom centre | the chest, closed (1.10 x 0.60 x 0.60 without handles, bands and skull) |
| chest_open_base | 1.16 x 0.40 x 0.72 | 2416 | bottom centre | the chest without its lid |
| chest_lid | 1.12 x 0.28 x 0.67 | 904 | **the hinge** | the lid; see above |
| pu_max_ammo | 0.19 x 0.51 x 0.15 | 1808 | **centre** | a quiver full of arrows |
| pu_instakill | 0.38 x 0.50 x 0.38 | 336 | **centre** | a skull, facing +Z |
| pu_double | 0.30 x 0.50 x 0.27 | 1640 | **centre** | a star coin standing on a stack of coins |
| pu_nuke | 0.33 x 0.50 x 0.33 | 380 | **centre** | a golden chalice |
| pu_carpenter | 0.09 x 0.50 x 0.17 | 1474 | **centre** | a hammer, its face toward +Z |
| pu_firesale | 0.45 x 0.45 x 0.11 | 568 | **centre** | a skull coin, facing +Z |
| potion_red | 0.15 x 0.22 x 0.15 | 1392 | bottom centre | round flask, red |
| potion_blue | 0.17 x 0.24 x 0.17 | 1648 | bottom centre | shouldered bottle, blue |
| potion_green | 0.14 x 0.24 x 0.14 | 1648 | bottom centre | waisted bottle, green |
| potion_amber | 0.21 x 0.20 x 0.21 | 1392 | bottom centre | wide flat flask, amber |
| potion_violet | 0.14 x 0.24 x 0.14 | 1648 | bottom centre | teardrop bottle, violet |
| potion_white | 0.14 x 0.24 x 0.14 | 1904 | bottom centre | ribbed bottle, white |
| lever | 0.08 x 0.88 x 0.09 | 528 | **the pivot** | the power switch's moving arm: an iron bar, leather grip, ball end, up +Y; it turns about X |
| lever_base | 0.34 x 0.50 x 0.11 | 360 | **the pivot** | the wall plate and cheeks the lever turns in; the wall is at z = -0.075 |
| brazier | 1.04 x 1.42 x 1.08 | 1278 | bottom centre | wrought iron on three curled legs, a 1.04 m bowl of coals |
| plank | 1.20 x 0.18 x 0.05 | 200 | **centre** | a rough nailed board, long along X, its face toward +Z |
| gravestone_a | 0.78 x 1.10 x 0.28 | 252 | bottom centre | rounded headstone on a plinth, a cross cut in it |
| gravestone_b | 0.62 x 1.30 x 0.36 | 404 | bottom centre | celtic cross on a stepped base |
| gravestone_c | 0.62 x 0.92 x 0.29 | 192 | bottom centre | old pointed slab, leaning back, a corner broken off |
| grave_mound | 1.00 x 0.30 x 1.95 | 318 | bottom centre | a low heap of dirt, long along Z |
| well | 2.22 x 2.62 x 1.87 | 3560 | bottom centre | the square's well: a ring of fitted stones round dark water, two posts, a winch (its axle along X) with rope and bucket, a plank roof |

## Sources

The skull, coins, chalice and potions are Quaternius' **Ultimate RPG** pack
(https://quaternius.com/packs/ultimaterpg.html: Skull, Coin, Coin_Star, Coin_Skull, Chalice,
Potion1/2/3/4/5/8_Filled, recoloured); the quiver's arrows and the carpenter's hammer are his
**Medieval Weapons** pack (https://quaternius.com/packs/medievalweapons.html: Arrow,
Hammer_Small). Both are CC0 1.0 (public domain). Everything else was modelled in
`tools/make_items.py` for this project.
