# GRAVEWAKE

Ashwick burned. Up to four survivors hold a clearing in the wood outside it,
round their fire, against wave after wave of the dead, with swords, axes,
hammers, polearms, bows, crossbows and staffs. After every wave each of them
takes one of three cards: a new weapon, a brew, or something that lasts. There
is no end but the last of them falling; how far they got is the score.

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
| Take a card, after a wave | 1, 2, 3 | D-pad left, up, right |
| Kick: knock the dead over; stamp on one that's down | F | East button |
| Reload | R | West button |
| Raise a friend who's down (hold) | E | West button |
| Quick stab | V or C | Right stick press |
| Throw a fire pot | G | Right shoulder |
| Ring a grave bell (the dead go to it) | Q | Left shoulder |
| The other weapon (with Packmule Stout) | Wheel; 1, 2 | North button |
| The room's menu | Escape, or **Menu** | |

Everyone starts by the fire with a hand crossbow and two fire pots. You carry
one weapon (two with Packmule Stout); the stab (V) and the kick (F) are always
to hand, whatever you carry. The dead claw up out of the old graves and from
among the roots at the clearing's edge, and come for whoever's nearest.

Kick the dead over and they lie a while before getting up; kick one that's
down and you stamp on its head. Heavy blows from hammers, maces, axes swung
wide and anything two-handed knock them over too, and the fallen take more
from every blow. A head hit is gold on the crosshair; a kill blows apart the
piece you hit.

After each wave, three cards:

- **A weapon** you don't carry, in place of the one in hand (or into your
  second hand with Packmule Stout). Better ones turn up as the night goes on;
  from the tenth wave some come runeforged.
- **A brew** you haven't drunk: Ironhide Ale (more health), Quickhand Mead
  (faster reloads, draws and raises), Second Wind Cider (raise others faster;
  alone, rise again once a wave), Twinstrike Brew (faster, harder blows),
  Fleetfoot Tonic (run faster, longer), Packmule Stout (a second weapon).
- **A keepsake**: the Runeforge (the weapon in hand remade: more damage and
  ammunition, and fire, frost, lightning, bleeding or a blast in every hit), a
  whetstone (a fifth more damage), a heart of oak (40 more health), hobnailed
  boots (harder, wider kicks), a deeper quiver (half again the arrows and
  bolts), a pyre satchel (two more fire pots) or grave bells.

What you take is yours till the game's over, through going down and bleeding
out. The next wave starts five seconds after everyone has chosen. The wave's
end also fills everyone's arrows, bolts and pots, raises whoever's down, and
brings back whoever bled out.

Down, you fight on from the ground: with what you hold if it shoots, or a
hand crossbow of a dozen bolts if it doesn't. A friend holding E for three
seconds raises you; alone you're done unless you drank Second Wind Cider.

Development keys, not in play's interface: F5 the wave's end (and the cards),
F6 walk to the next part of the clearing, F9 a camera over the clearing, F10
the next weapon in hand.

## Ashwick Wood

| Where | What's there |
| --- | --- |
| The middle | The camp's fire, where everyone starts |
| North | The chapel's ruin: half its walls, its floor, two torches |
| West | The old graveyard behind a broken iron fence: eight graves the dead rise from |
| East | A cart and a wagon left by the road, a second fire |
| South | Boulders, a pine and a twisted oak |
| The edge | Roots the dead claw up from, and the trees, close and endless |

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
| `plan.tide`, `pieces.tide` | Ashwick Wood as one plan: the clearing's edge, the ruin, the graves, the camp, the forest |
| `ashwick.tide` | The plan made solid: collision, the way to the survivors (and to a bell) |
| `rounds.tide` | Waves, how many come and how tough, where they rise, the regrouping after each |
| `cards.tide` | The cards after a wave: what's dealt, what each does when it's taken |
| `zombies.tide` | The dead: rising, hunting, wounds, burning, freezing, going over |
| `weapons.tide` | Every weapon's numbers, how far into the night it turns up, and what the Runeforge makes of it |
| `hands.tide` | Walking, fighting, kicking, raising friends |
| `missiles.tide` | Arrows, bolts, shot, javelins, axes, fireballs, fire pots, bells |
| `drops.tide` | Power-ups, and the cues for what's heard |
| `kit.tide`, `look.tide`, `eye.tide` | The clearing's meshes; light, shadows, fog, bloom; the camera |
| `people.tide`, `held.tide`, `setpieces.tide`, `fx.tide` | The dead and the survivors; the weapon in hand; the graves and braziers; flames, blood, sparks |
| `hud.tide`, `sound.tide` | What's written over the picture (the cards too), the title; what's heard |
| `fp.tide` | The first-person arms: which clips move them for each weapon, how a weapon sits in the hand |
| `art/` | The forest, ruin, people, weapons, items and first-person arms, with how each was made: see `art/CREDITS.md` (the arms are CC BY 4.0, by CoinCoin) |
| `sfx/` | The sounds, made by `sfx/tools/make_sfx.py`: see `sfx/CREDITS.md` |
| `test/` | Waves played with no window (`test.c`), and the browser test |
