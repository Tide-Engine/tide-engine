# GRAVEWAKE weapons, projectiles and first-person hands

One `.glb` per model: static, one mesh, flat colours through material base colours (no textures),
metres. `weapons.json` gives each model's kind, length, triangle count and the points the game
needs. `tools/make_weapons.py` rebuilds everything from the source packs (see its header for the
command). Rebuilding writes the same bytes.

## Axes

Every coordinate here and in `weapons.json` is in the **game's axes**: +X right, +Y up, +Z forward,
as Tide reads the files. Tide's importer negates glTF's x, so the files themselves hold -x.

- **Melee weapons, staffs, thrown things:** the right hand's grip centre at the origin, the weapon
  along +Y toward its business end, the cutting edge, axe blade or hammer face toward +Z, pommel or
  butt toward -Y. Double-edged blades have their edges at ±Z.
- **Bows:** grip centre at the origin, limbs along ±Y (upper limb +Y), arrows fly +Z. The string is
  not in the mesh; it attaches at `stringTop` and `stringBottom` and rests on the -Z side.
- **Crossbows and the handgonne:** the trigger hand at the origin, shooting along +Z, the stock
  toward -Z, the prod spanning ±X near the front. The string is not in the mesh.
- **Projectiles:** the arrow and the bolt lie along +Z with the nock (tail) at the origin and the tip
  at +Z. The cannonball is centred on the origin.
- **Hands:** see below.

### Exceptions and choices to know about

- **scythe:** the blade sits at the top pointing **-X** (`tip` is its point). Its cutting edge is the
  blade's concave underside, facing **-Y** (toward the hand), not +Z: it cuts on a downward pull.
- **Bows' arrow rest** (`offhand`) is on the bow's **-X** side just above the grip, as for a
  right-handed archer holding the bow in the left hand.
- **repeater:** its `offhand` is the cocking lever's handle (the off hand works the lever).
  **hand_crossbow** is one-handed (`offhand` null).
- **handgonne:** its bore is 0.066 m in radius, so the cannonball (0.06 m) fits the muzzle; the
  tube is a flared hand-bombard rather than a narrow historical one. `touchhole` is where the match
  goes (for a spark).
- **flail:** rigid; the chain continues straight on along +Y past the handle to the ball.
- **Staffs:** `offhand` is 0.32 m above the main hand, for a two-handed hold; the game may ignore it.

## weapons.json

An array, one object per file, in the order below.

| field | meaning |
|---|---|
| `name`, `file` | the model and its file |
| `kind` | `melee`, `bow`, `crossbow`, `gun`, `staff`, `thrown`, `projectile` or `hand` |
| `length` | overall length in metres: along Y, or along Z for crossbows, the gun and projectiles (the cannonball's diameter) |
| `tip` | the far end: blade tip, spear point, top of the head; a bow's front at the arrow; a crossbow's front at the bolt line; the muzzle |
| `offhand` | where the left hand goes on two-handers and crossbow fore-grips; a bow's arrow rest; else null |
| `head` | the striking head's centre (area-weighted) for axes, hammers, maces, the flail's ball, the club, halberd, pitchfork, bell; else null |
| `stringTop`, `stringBottom` | bows: the limb tips the string attaches to |
| `stringLeft`, `stringRight` | crossbows: the prod tips, the string's ends at rest |
| `nock` | crossbows: where the drawn string sits when cocked (the latch) |
| `boltTail`, `boltTip` | crossbows: a loaded 0.38 m bolt, tail at the nock |
| `muzzle` | handgonne and staffs: where the shot or spell leaves |
| `touchhole` | handgonne only |
| `wrist`, `forearmDir` | fists only: where `forearm.glb`'s origin goes and the direction it runs (-Z) |
| `elbow` | forearm only |
| `tris` | triangles in the file |
| `bounds` | the model's box, `[min, max]` |

## Hands

- **fist_r.glb:** a right hand in a dark leather glove with a steel plate on its back and a bar over
  the knuckles, closed round a 3.5 cm handle whose axis is the Y axis through the origin (the handle
  isn't in the mesh: a weapon's grip fills it). Palm toward -X, back of the hand toward +X, thumb over
  the top (+Y), knuckles forward (+Z), the wrist cuff toward -Z.
- **fist_l.glb:** its mirror (x negated).
- **forearm.glb:** a leather bracer with a steel plate and straps, then a red sleeve with a rolled
  cuff, from the wrist at the origin along -Y to the elbow at y = -0.28; 0.09 m wide (Z) by 0.08
  (X) at the wrist, 0.11 by 0.10 at the elbow. Its back (the plate) faces +X, the same side as the
  fists' backs. To join it to a fist, put its origin at the fist's `wrist` and turn it a quarter about
  X so that its -Y runs along the fist's -Z (the turn that takes +Y to +Z and +Z to -Y; Unity-style
  Euler (90, 0, 0)). The fist's cuff covers the join.

## Glow

`staff_fire`, `staff_frost` and `staff_storm` carry TEXCOORD_1: (1, 0) on the ember or crystals,
(0, 0) elsewhere. No other file here has a TEXCOORD_1. TEXCOORD_0 is (0, 0) everywhere.

## The models

| file | kind | length (m) | tris | source |
|---|---|---|---|---|
| rusty_sword | melee | 0.75 | 872 | Sword, recoloured: rusty blade, worn leather |
| dagger | melee | 0.35 | 736 | Dagger_2 |
| arming_sword | melee | 0.95 | 1006 | Sword_Golden, recoloured: steel blade, brass hilt |
| falchion | melee | 0.85 | 942 | Sword_2 (curved, single edge) |
| longsword | melee | 1.15 | 830 | Sword_Big |
| claymore | melee | 1.50 | 1033 | Claymore, recoloured: steel blade, leather grip |
| hand_axe | melee | 0.60 | 966 | Axe_Small |
| bearded_axe | melee | 0.80 | 1130 | Axe |
| greataxe | melee | 1.30 | 1892 | Axe_Double |
| war_hammer | melee | 0.80 | 1474 | Hammer_Small |
| maul | melee | 1.20 | 1918 | Hammer_Double |
| mace | melee | 0.75 | 662 | modelled: seven flanges on a steel shaft |
| morning_star | melee | 0.86 | 608 | modelled: spiked iron ball on a haft |
| flail | melee | 0.84 | 1084 | modelled: handle, six links, spiked ball |
| spear | melee | 2.00 | 1202 | Spear |
| halberd | melee | 2.10 | 674 | modelled: axe blade, back spike, top spike, langets |
| scythe | melee | 1.80 | 1310 | Scythe |
| pitchfork | melee | 1.60 | 644 | modelled: three iron tines |
| spiked_club | melee | 0.83 | 574 | modelled: club with nails and an iron band |
| hunting_bow | bow | 1.10 | 640 | Bow_Wooden2, string removed |
| longbow | bow | 1.70 | 704 | Bow_Wooden, limbs stretched, string removed |
| sun_bow | bow | 1.40 | 752 | Bow_Golden, string removed |
| wraith_bow | bow | 1.40 | 748 | Bow_Evil, string removed |
| hand_crossbow | crossbow | 0.45 | 726 | modelled: pistol grip |
| light_crossbow | crossbow | 0.82 | 672 | modelled: wooden prod, stirrup, trigger bar |
| arbalest | crossbow | 1.04 | 1000 | modelled: steel prod, windlass with two cranks at the butt |
| repeater | crossbow | 0.82 | 784 | modelled: chu-ko-nu bolt magazine and lever |
| handgonne | gun | 1.10 | 566 | modelled: banded iron tube on a wooden pole |
| staff_fire | staff | 1.72 | 958 | modelled; ember is the RPG pack's Crystal4 in an iron cage |
| staff_frost | staff | 1.72 | 902 | modelled; crystals are the RPG pack's Crystal2 and Crystal3 |
| staff_storm | staff | 1.66 | 1394 | modelled with copper coils; crystal is the RPG pack's Crystal1 |
| javelin | thrown | 1.41 | 322 | modelled |
| throwing_axe | thrown | 0.42 | 370 | modelled: a francisca |
| fire_pot | thrown | 0.23 (0.18 across) | 432 | modelled: clay pot, rag wick; origin at its centre |
| lure_bell | thrown | 0.25 | 594 | modelled: bronze hand bell, mouth toward +Y |
| arrow | projectile | 0.75 | 224 | Arrow |
| bolt | projectile | 0.38 | 62 | modelled |
| cannonball | projectile | 0.12 | 168 | modelled |
| fist_r | hand | 0.11 tall | 1100 | modelled |
| fist_l | hand | 0.11 tall | 1100 | modelled (fist_r mirrored) |
| forearm | hand | 0.28 | 540 | modelled |

"Source" names are files from Quaternius' **Medieval Weapons** pack
(https://quaternius.com/packs/medievalweapons.html) and **Ultimate RPG** pack
(https://quaternius.com/packs/ultimaterpg.html), both CC0 1.0 (public domain). Everything marked
modelled was made in `tools/make_weapons.py` for this project.

## Tools

- `tools/make_weapons.py` builds every file and `weapons.json`; `--only a,b` rebuilds some and keeps
  the rest of the JSON.
- `tools/lowpoly.py` holds the modelling helpers it shares with `../items/tools/make_items.py`.
- `tools/preview_render.py` and `tools/preview_sheet.py` render checking views (the origin's axes
  and the JSON's points marked, back faces culled) and a side-by-side sheet at true scale.
