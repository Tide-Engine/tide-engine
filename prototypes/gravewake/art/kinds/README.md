# The kinds of the dead

Each kind of the dead but the ordinary its own body, grown from the dead of
`../people` (Quaternius' Ultimate Modular Men and Women, CC0) by
`tools/make_kinds.py` in Blender 4.4, as the bosses are (`../mutants`):

| File | From | What was done |
| --- | --- | --- |
| `runner.glb` | `zombie_worker` | Lean and hunched, its limbs long and thin and its claws longer, its head thrust out; bone through its spine and elbows |
| `bloater.glb` | `zombie_king` | Its belly a vast sac of flesh hanging out front, pustules glowing on it, its limbs fat, a lump on its back |
| `spitter.glb` | `zombie_maid` | Hunched, its throat a great sac of bile glowing through its skin, its cheeks swollen |
| `warden.glb` | `zombie_wanderer` | Squared up, iron on its shoulders, a round shield on its back, its head bare |
| `brute.glb` | `zombie_farmer` | Shoulders and forearms like a bull's, a little head sunk on a thick neck, short heavy legs; muscle heaped on its back, bone through its shoulders and forearms |
| `howler.glb` | `zombie_peasant` | Stretched thin and tall, its long neck thrown back, a frill of spines round its throat, its throat and lungs glowing |

Each is cut to about 1100 triangles and plays the horde's clips (walk, run,
sprint, idle, claw, fall, get up, two deaths, rise: `../crowd`), baked into
`anim.png` for crowd.tide's `ShambleKind`, laid out as in `../crowd`.
`kinds.json` holds the layout, the clips' frames and timing, and where each
body's head, chest and hips are, which zombies.tide's `KindHead`, `KindChest`
and `KindHips` take from them less those of a body left as it was.

```sh
blender -b --factory-startup --python tools/make_kinds.py -- ../people .
```
