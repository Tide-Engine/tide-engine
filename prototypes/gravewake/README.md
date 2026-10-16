# GRAVEWAKE

Ashwick burned. Up to four survivors hold a wide, hilly clearing in the wood
outside it, round their fire, against endless and growing hordes of the
dead. Each picks a class before the first wave (Warrior, Ranger or Mage), and
after every wave takes one of three cards: something for their class,
something for themselves, something for the ability on Q. There is no end but
the last of them falling; how far they got is the score.

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
| The ability (Leap, Dash, Frost Nova, ...) | Q | Left shoulder |
| Pick a class, take a card | 1, 2, 3 | D-pad left, up, right |
| Kick: knock the dead over; stamp on one that's down | F | East button |
| Reload | R | West button |
| Raise a friend who's down (hold) | E | West button |
| Quick stab | V or C | Right stick press |
| The other weapon (with Packmule Stout) | Wheel; 1, 2 | North button |
| The room's menu | Escape, or **Menu** | |

### The classes

| Class | Starts with | Its weapons | Its own cards |
| --- | --- | --- | --- |
| Warrior | Arming Sword, Leap | Swords, axes, hammers, polearms | Whetstone, Wide Swings (cleave), Bloodthirst (heal on kills) |
| Ranger | Hunting Bow, Dash; twice the arrows | Bows, crossbows, javelins, throwing axes, the handgonne | Broadheads, Deep Quiver, Bodkin Heads (pierce), Volley (more arrows), Steady Hands |
| Mage | Staff of Embers, Frost Nova; charges come back on their own | The staffs of embers, rime and storms | Focus, Split Spell (more fireballs, wider frost, longer chains), Wellspring (more and quicker charges) |

Every class can draw the Runeforge, which remakes the weapon in hand: more
damage and ammunition, and fire, frost, lightning, bleeding or a blast in
every hit.

### The cards

After each wave, three:

- **For your class:** a new weapon of it (better ones as the night goes on;
  from the tenth wave some come runeforged), or one of its upgrades.
- **For yourself:** a brew you haven't drunk (Ironhide Ale, Quickhand Mead,
  Second Wind Cider, Twinstrike Brew, Fleetfoot Tonic, Packmule Stout), a
  heart of oak (+40 health), hobnailed boots (harder, wider kicks) or a fleet
  foot (faster, longer running).
- **For your ability on Q:** another one in its place (Leap, Dash, Frost
  Nova, Fire Pot, Grave Bell, War Cry, Blink), or the one you have honed (a
  fifth less to wait, a quarter more power).

What you take is yours till the game's over, through going down and bleeding
out. The next wave starts five seconds after everyone has chosen; its end
fills everyone's ammunition, raises whoever's down and brings back whoever
bled out.

The dead come in hordes, more each wave (26, then 38, 53, ... over 200 by the
tenth), weaker each than a single one used to be, up to 150 at once. They
claw up out of the old graves and from among the roots round the edge, and
come for whoever's nearest, up the ramps to the cliff-top ruin and the rocky
shelf. Kick them over and they lie a while before getting up; kick one that's
down and you stamp on its head. Heavy blows from hammers, maces, axes swung
wide and anything two-handed knock them over too. A head hit is gold on the
crosshair; a kill blows apart the piece you hit.

Down, you fight on from the ground: with what you hold if it shoots, or a
hand crossbow of a dozen bolts if it doesn't. A friend holding E for three
seconds raises you; alone you're done unless you drank Second Wind Cider.

Development keys, not in play's interface: F5 the wave's end (and the cards),
F6 walk to the next part of the clearing, F9 a camera over the clearing, F10
the next weapon in hand.

## Ashwick Wood

About 80 m across, its ground rising into the trees at its edge.

| Where | What's there |
| --- | --- |
| The middle | The camp's fire, where everyone starts |
| North | The chapel's ruin on a cliff 4.5 m up, reached by one ramp from the south-west |
| West | The old graveyard in a hollow, behind a broken iron fence: ten graves the dead rise from |
| East | A hill 4 m high with a beacon fire on top; a cart and a wagon at its foot, a third fire |
| South-west | A rocky shelf 3 m up, its ramp coming down to the east, boulders round its foot |
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
| `plan.tide`, `pieces.tide` | Ashwick Wood as one plan: the ground's heights (`Ground`), the clearing's edge, the ruin, the graves, the camp, the forest |
| `ashwick.tide` | The plan made solid: collision, the ground under feet and missiles, the way to the survivors (and to a bell) |
| `rounds.tide` | Waves, how many come and how tough, where they rise, the regrouping after each |
| `abilities.tide` | The classes, and the ability on Q: each one's use and cooldown |
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
| `test/` | Waves played with no window (`test.c`), a tick's cost with a horde (`perf.c`, `gravewake_perf`, not a test), and the browser test |
