# GRAVEWAKE

The dead rise in Ashwick. Up to four survivors hold the village at night,
round after round, with swords, axes, hammers, polearms, bows, crossbows and
whatever the Pilgrim's Chest gives them. Every kill and every hit pays; the
gold opens the village's doors, buys weapons off its walls and brews from its
barrels, and the beacon in the keep lights the alchemists' fires.

Play at <https://tide-engine.dev/prototypes/gravewake/>. **Play** is alone;
**Host a room** and send the code to fight with friends, up to four.

## Playing

| Action | Keyboard and mouse | Gamepad |
| --- | --- | --- |
| Walk | WASD | Left stick |
| Look | Mouse | Right stick |
| Sprint | Shift | Left stick press |
| Jump | Space | South button |
| Attack; draw a bow (hold, let go to loose) | Left mouse | Right trigger |
| Heavy blow (melee), aim (bows, crossbows) | Right mouse | Left trigger |
| Use: buy, open, rebuild boards, raise a friend (hold) | E | West button |
| Kick: knock the dead over; stamp on one that's down | F | East button |
| Reload | R | West button |
| Quick stab | V or C | Right stick press |
| Throw a fire pot | G | Right shoulder |
| Ring a grave bell (the dead go to it) | Q | Left shoulder |
| Next weapon; a weapon by its slot | Wheel; 1, 2, 3 | North button |
| The room's menu | Escape, or **Menu** | |

Everyone starts in the square with a rusty shortsword, a hand crossbow,
two fire pots and 500 gold. Hold E at a broken window to nail its boards back.
Kick the dead over and they lie a while before getting up; kick one that's
down and you stamp on its head. Heavy blows from hammers, maces, axes swung
wide and anything two-handed knock them over too, and the fallen take more
from every blow. A head hit is gold on the crosshair; a head kill takes the
head off.

Down, you can still shoot a bow or crossbow; a friend holding E for three
seconds raises you, and alone you're done unless you drank Second Wind Cider.
Who bleeds out comes back at the next round.

Development keys, not in play's interface: F2 10,000 gold, F3 the beacon, F4
every door, F5 the round's end, F6 walk to the next area, F9 a camera over the
village, F10 the next weapon in hand.

## Ashwick

| Area | Opened from | What's there |
| --- | --- | --- |
| The square | | Hand Axe, Hunting Bow |
| The tavern | the square's east door, 750 | Light Crossbow, Rondel Dagger, Ironhide Ale, the chest's first place |
| The stables | the tavern's back door, 1000 | fire pots |
| The smithy | the rubble in the south alley, 1000; the stable bars, 750 | the Runeforge, Longsword, War Hammer, Halberd, Quickhand Mead |
| The chapel | the rubble up the north street, 1000 | Flanged Mace, Boar Spear, grave bells, Second Wind Cider |
| The graveyard | the chapel's side door, 750 | Grave Scythe, Fleetfoot Tonic |
| The keep | the iron bars in the west alley, 1250; the graveyard's gate, 1250 | the beacon, Arbalest, Twinstrike Brew, Packmule Stout |

Brews only pour once the beacon is lit (Second Wind always). The Pilgrim's
Chest (950) gives any of the 31 weapons beyond the starting two; after a few
uses it may show a skull and fly to another of its five places. The Runeforge
(5000) remakes the weapon in hand: more damage and ammunition, and fire,
frost, lightning, bleeding or a blast in every hit.

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
| `plan.tide`, `pieces.tide` | Ashwick as one plan: buildings, walls, doors, windows, graves, what's sold where |
| `ashwick.tide` | The plan made solid: collision, areas, the way to the survivors (and to a bell) |
| `rounds.tide` | Rounds, how many come and how tough, where they rise |
| `zombies.tide` | The dead: rising, tearing boards, climbing in, hunting, wounds, burning, freezing |
| `weapons.tide` | Every weapon's numbers, and what the Runeforge makes of it |
| `hands.tide` | Walking, buying, using, fighting, raising friends |
| `missiles.tide` | Arrows, bolts, shot, javelins, axes, fireballs, fire pots, bells |
| `shrines.tide` | The chest, the Runeforge, power-ups |
| `kit.tide`, `look.tide`, `eye.tide` | The village's meshes; light, shadows, fog, bloom; the camera |
| `people.tide`, `held.tide`, `setpieces.tide`, `fx.tide` | The dead and the survivors; the weapon in hand; what's on walls; flames, blood, sparks |
| `hud.tide`, `sound.tide` | What's written over the picture, the title; what's heard |
| `fp.tide` | The first-person arms: which clips move them for each weapon, how a weapon sits in the hand |
| `art/` | The village, people, weapons, items and first-person arms, with how each was made: see `art/CREDITS.md` (the arms are CC BY 4.0, by CoinCoin) |
| `sfx/` | The sounds, made by `sfx/tools/make_sfx.py`: see `sfx/CREDITS.md` |
| `test/` | Rounds played with no window (`test.c`), and the browser test |
