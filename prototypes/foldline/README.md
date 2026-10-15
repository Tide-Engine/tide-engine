# FOLDLINE

A first-person spatial puzzle for two bodies, descending through an increasingly
wrong office building. Each operator has their own pair of folds: amber/cyan for
A, rose/violet for B. Anyone, and any loose cube or crate, can cross either pair.
The opening shows the real space beyond it; position, facing, size and momentum
cross with you.

Play alone with Tab or host a room and share its code with a partner. A joining
partner controls B. Alone, the body you leave behind stays where it is and keeps
looking in the direction you left it. The puzzles and passages use both bodies.

| Control | Action |
| --- | --- |
| WASD / left stick | Move |
| Mouse / right stick | Look |
| Space / south button | Jump |
| Left click / right trigger | Place your first fold end; throw a held object |
| Right click / left trigger | Place your second fold end; throw a held object |
| E / west button | Pick up or drop a cube or crate |
| F / east button | Throw a held object |
| Q / north button | Ping a location |
| Tab | Switch bodies while alone |
| R | Rebuild the current place |
| Escape | Open the menu |

Wallpaper takes folds. Bare concrete, metal, machinery, floors and ceilings
refuse them. Shots pass between grille bars; bodies and loose objects do not.
Closed shutters and moving platforms stop shots. Some pairs are already open
when you arrive; they still belong to their operator and can be moved normally.
In certain passages the device cannot place new ends, while existing ends work.

Painted frames set the size of an end. Crossing multiplies your current size by
the destination size divided by the entry size. Repeated crossings compound:
half, quarter, eighth. Crossing in reverse restores size. There is no gameplay
maximum size; the opening you enter and the space beyond it must fit you.
A technical lower limit of 1/16 refuses a smaller crossing with a bump.
Movement, jumping, reach and mass follow size. An object much larger than your
body cannot be picked up. Try things: R rebuilds the room and restores both bodies.

Pressure plates power their visible wires while enough weight rests on them.
A lit wire leads to the device it powers; each input has its own red/green lamp.
Some heavy plates have gauges that show how close the load is. Mass grows with
the cube of size, so being small can leave you too light. The cube and crates
can hold a circuit after both operators leave. There are no latched plate
puzzles, lettered release controls or lift HOLD rituals.

The route has six puzzle places, four horror passages and three short lift
stops, followed by a conclusion:

1. Reception
2. Mailroom
3. Lamps Out
4. Showroom
5. Weighbridge
6. The Slot
7. Crawlspace
8. Long Throw
9. The Ward
10. Archive
11. Exchange
12. Red Stair
13. Atrium
14. Reception?

Each place has its own layout and lighting. Watch what happens in the world:
the game has no objective text, hint key or monster-rule overlay. The HUD keeps
the reticle, operator marker and connected room code. Controls and the stop
selector live in the menu. Opening it stops this machine's controls while the
shared match continues.

Both bodies enter the exit lift to continue. Its doors close, the ride keeps
your relative positions and facing, and the doors ahead reveal the next place.
Each new place makes bodies whole again. A fallen or killed body returns whole;
a creature catching you in a survival passage restarts that passage for both.
R resets the current place, its objects and both bodies. The last stop holds
its conclusion instead of looping back into the campaign.

## Build and run

From the repository root:

```sh
cmake --preset debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset debug --target foldline foldline_test foldline_render_test
ctest --preset debug -R '^foldline'
build/debug/bin/foldline
```

Or run `tide run` in this folder. Box3D supplies physics; the game includes its
art and needs no external package download. For the browser page:

```sh
cmake --preset web-debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset web-debug --target foldline_page foldline_test foldline_render_test
ctest --preset web-debug -R '^foldline'
```

Serve `docs/static` and visit `/prototypes/foldline/`. The page is self-contained.
Keyboard/mouse and gamepad gameplay are supported; touchscreen movement is not.
The PSX picture uses 300 rows, nearest textures and dithered lighting.

The simulation fixtures include input-driven puzzle solves. Pacing, first-time
discoverability, live human co-op and sound balance still need human playtesting.
The intended 45–75 minute descent is a design target, not a measured playtime.
