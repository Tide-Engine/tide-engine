# The bosses

Three mutants grown from the dead of `../people` (Quaternius' Ultimate
Modular Men and Women, CC0) by `tools/make_mutants.py` in Blender 4.4:

| File | From | What was done |
| --- | --- | --- |
| `mauler.glb` | `zombie_king`, the men's clips | Chest, shoulders and arms swollen, the head shrunk and sunk between the shoulders, short thick legs; sores that glow on its back, spikes on its back and upper arms, lumps on its shoulders and forearms |
| `brood.glb` | `zombie_maid`, the women's clips | The belly a great egg sac that glows, thin limbs, a small hunched head |
| `stalker.glb` | `zombie_wanderer`, the men's clips | Long legs and neck, thin arms with long hands, spines down its back; a sac that glows at its throat |

Each is cut to about 1600 triangles and drawn as the horde is: its clips
(walk, run, idle, claw, slam, jump, land, hit, crouch, death, rise) baked into
`anim.png` for a vert to play, laid out as in `../crowd`. `mutants.json` holds
the layout, the clips' frames and timing, and where each body's head, chest,
hips and weak spot are, which `boss.tide`'s `MutantOf` copies.

```sh
blender -b --factory-startup --python tools/make_mutants.py -- ../people .
```
