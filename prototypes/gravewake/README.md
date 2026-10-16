# GRAVEWAKE

Ashwick burned. Up to four survivors hold a wide, hilly clearing in the wood
outside it, round their fire, against endless and growing hordes of the dead,
thousands at once. It plays like Risk of Rain 2 in a PSX-era picture: each
picks a survivor before the first wave (a Gunner, a Breacher or an Engineer),
a gun to aim and three skills on cooldowns, and after every wave takes one of
three items that stack, most of which fight on their own. Every kill is
experience for the party, and every level makes them all a little stronger.
There is no end but the last of them falling; how far they got is the score.

Play at <https://tide-engine.dev/prototypes/gravewake/>. **Play** is alone;
**Host a room** and send the code to fight with friends, up to four.

## Playing

| Action | Keyboard and mouse | Gamepad |
| --- | --- | --- |
| Walk; forward runs, once you haven't fired for a moment | WASD | Left stick |
| Look | Mouse | Right stick |
| Jump (again in the air, with Spring Boots) | Space | South button |
| Fire | Left mouse | Right trigger |
| The secondary skill | Right mouse | Left trigger |
| The utility skill | Shift | Left shoulder |
| The special skill | Q | Right shoulder |
| Pick a survivor, take an item | 1, 2, 3 | D-pad left, up, right |
| Reload | R | West button |
| Raise a friend who's down (hold) | E | West button |
| The room's menu | Escape, or **Menu** | |

### The survivors

| Survivor | Gun | Right mouse | Shift | Q |
| --- | --- | --- | --- | --- |
| Gunner | Carbine: automatic, through two | Phase Round: through every one of the dead in its line | Slide | Frag Grenade |
| Breacher | Pump Shotgun: nine pellets, loaded a shell at a time | Boot: kicks the dead in front over; one down is stamped on | Leap: slams down where it lands, knocking them over | Firebomb |
| Engineer | SMG | Mine: bursts when the dead come near | Blink: ten metres the way you look | Turret: fires on its own for fifteen seconds |

Skills fire as soon as they're ready while their key is held. The guns never
run out; only their magazines do.

### The items

After each wave, three to choose from; each stacks.

| Rarity | Items |
| --- | --- |
| Common | Hollow Points (damage), Hair Trigger (fire and reload faster), Kevlar Vest (health), Running Shoes (speed), Leech Rounds (kills heal), Field Battery (skills back sooner), Extended Mag, Lucky Shot (hits for double) |
| Uncommon | Live Wire (hits arc to more of the dead), Powder Keg (kills burst), Gun Drone (shoots the nearest dead), Ember Ring (burns the dead near you), Frostbite (hits freeze), Spring Boots (jumps in the air) |
| Rare | Mortar Beacon (hits call a shell down), Second Wind (going down, straight back up, used up) |

Uncommon and rare items turn up more as the waves go on. What you take is
yours till the game's over, through going down and bleeding out. The next
wave starts five seconds after everyone has chosen; its end loads every gun,
readies every skill, raises whoever's down and brings back whoever bled out.

The dead come in hordes, more each wave (74, then 128, 190, ... 740 by the
tenth), weaker each than a single one used to be, up to 3000 at once. They
claw up out of the old graves and from among the roots round the edge, and
come for whoever's nearest, up the ramps to the cliff-top ruin and the rocky
shelf. A head hit is gold on the crosshair, a Lucky Shot's double yellow.

Down, you fight on from the ground with a pistol. A friend holding E for three
seconds raises you, the gun back in hand as loaded as it was.

Development keys, not in play's interface: F4 two thousand more of the dead,
F5 the wave's end (and the offer), F6 walk to the next part of the clearing,
F9 a camera over the clearing, F10 a random item.

## Ashwick Wood

About 90 m across, its ground rising into the trees at its edge. The dead claw
up out of the ground all round you, 16 to 32 m off, from every side.

| Where | What's there |
| --- | --- |
| The middle | The camp's fire, where everyone starts; worn paths out to each landmark |
| North | The chapel's rise, 6 m up, the ruin on it, a ramp down each side |
| East | The beacon's spire: a lower tier 4 m up, its top 9 m up with the beacon burning, a ramp to each |
| West | The old graveyard, sunk in its hollow behind a broken iron fence: ten graves the dead rise from |
| North-west | A ring of standing stones |
| South | The ravine, cut across the clearing, boulders on its banks |
| About the clearing | Groves of trees and big stones to stand behind |

## Build and run

From the repository root:

```sh
cmake --preset debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset debug --target gravewake gravewake_test
ctest --preset debug -R '^gravewake'
```

For the browser page, and the browser test on both backends:

```sh
cmake --preset web-debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset web-debug --target gravewake_page gravewake_test
ctest --preset web-debug -R '^gravewake'
```

Serve `docs/static` and visit `/prototypes/gravewake/`. Or run `tide run` in
this folder.

## Files

| File | What's in it |
| --- | --- |
| `game.tide` | The survivors, the input, joining and leaving, starting over, the development walks |
| `plan.tide`, `pieces.tide` | Ashwick Wood as one plan: the ground's heights (`Ground`), the clearing's edge, the ruin, the graves, the camp, the forest |
| `ashwick.tide` | The plan made solid: collision, the ground under feet, the lines rounds and grenades follow, the way to the survivors |
| `rounds.tide` | Waves, how many come and how tough, where they rise, the regrouping after each |
| `kits.tide` | The survivors' kits: each skill's use and cooldown, the Engineer's turret |
| `items.tide` | The items: what each does, the offer after a wave, what a hit or a kill sets off, the drone and the ember ring, levels |
| `zombies.tide` | The dead: rising, hunting, wounds, burning, freezing, going over |
| `weapons.tide` | The guns' numbers |
| `hands.tide` | Running and jumping, firing (each round a ray through the dead), loading, raising friends, going down |
| `missiles.tide` | Frag grenades, firebombs, mines, the Mortar Beacon's shells |
| `drops.tide` | Power-ups, and the cues for what's heard |
| `crowd.tide` | The horde drawn: low-poly bodies whose clips a vert plays from a texture, thousands in a few draws |
| `kit.tide`, `look.tide`, `eye.tide` | The clearing's meshes; light, shadows, fog, bloom; the camera |
| `people.tide`, `held.tide`, `setpieces.tide`, `fx.tide` | The survivors; the gun in hand, grenades, mines, turrets, drones; the graves and braziers; tracers, flames, blood, sparks |
| `hud.tide`, `sound.tide` | What's written over the picture (the offer and the survivors too), the title; what's heard |
| `fp.tide` | The first-person arms: which clips move them for each gun |
| `art/` | The forest, ruin, people, guns, items and first-person arms, with how each was made: see `art/CREDITS.md` (the arms are CC BY 4.0, by CoinCoin) |
| `sfx/` | The sounds, made by `sfx/tools/make_sfx.py`: see `sfx/CREDITS.md` |
| `test/` | Waves played with no window (`test.c`), a tick's cost with a horde (`perf.c`, `gravewake_perf`, not a test), and the browser test |
