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

Wallpaper takes folds, on a wall, a floor or a ceiling. Bare concrete, metal and
machinery refuse them. An end on a floor or a ceiling lies the way you shot it.
What goes into one end comes out of the other with its speed turned: fall into a
floor's end and you are thrown out of a wall's; walk into a wall's end and you
come up out of a floor's. You always come out upright. Shots pass between grille bars; bodies and loose objects do not.
Closed shutters and moving platforms stop shots, and so does an open fold: you
cannot shoot through one. An end opens only where you can see the middle of its
opening: a shot that grazes the edge of a face cannot put one round the far side
of it. A shot over an open end replaces it, yours or your partner's: that end
closes and yours opens there, unless something is standing in that fold's mouth. A shutter, gate or door closed over an open end
seals that fold; anything entering its other end bumps back until it moves away.
Some pairs are already open when you arrive; they still belong to their operator
and can be moved normally. In certain passages the device cannot place new
ends, while existing ends work; there the reticle shows only its middle dot.

The building is mostly dark. Each operator's device carries a torch whose beam
casts shadows, shows in the haze and shines through a fold: point it into one
end and it lights what is past the other. A fold's ends glow in their colours.

Chairs, cabinets, desks, shelves and other rigid furniture stop bodies and
objects. Their actual legs, boards and panels provide the collision, so small
bodies can still use real gaps underneath or between them. Cloth and painted
signs remain decoration. Portal openings must clear solid furniture too.

The crosshair's left and right marks match your two placement buttons. Bright
brackets mean that end can be placed at the current aim; short dim marks mean
the shot cannot place it. The small squares underneath fill when each end has
already been placed, independently of the current aim. Tab switches these marks
to the other operator's own pair. Carrying an object disables the placement
brackets because clicking throws it.

Painted frames set the size of an end. Crossing multiplies your current size by
the destination size divided by the entry size. Repeated crossings compound:
half, quarter, eighth. Crossing in reverse restores size. There is no gameplay
maximum size; the opening you enter and the space beyond it must fit you.
A technical lower limit of 1/16 refuses a smaller crossing with a bump.
Movement, jumping, reach and mass follow size. An object much larger than your
body cannot be picked up, and one that grows too large for your hands while you
carry it through a fold slips out of them. Grown cargo lands heavily rather than
tumbling. Lost objects return to where they started, at the size they started.
Try things: R rebuilds the room and restores both bodies.

Pressure plates power their visible wires while enough weight rests on them.
A lit wire leads to the device it powers; each input has its own red/green lamp.
A plate's rim lamps fill amber with part of the weight it needs and turn green
once it has enough, so too light reads as too light. Mass grows with
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

Each place has its own layout and lighting, and the puzzles get hard quickly
after the first. Watch what happens in the world, including through the folds:
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

## Map authoring

The campaign's metric assemblies live in `maps/*.foldmap.json`. They own lift
slots, plates, moving devices, cable anchors and circuit connections. Reception
also defines its structural boxes and pre-opened folds there. Named box faces
provide attachment coordinates; the format does not impose a tile grid.
Other rooms keep their procedural geometry and encounter scripts in Tide.

After editing a map, regenerate its checked-in Tide source from the repository
root (Python 3.10 or newer, standard library only):

```sh
python prototypes/foldline/tools/foldmap.py
cmake --build --preset debug --target foldline
```

The build rejects stale generated output and validates all fourteen actual
constructed places against their source assemblies. It checks complete cable
support and source/target connections, lift clearance and floor support,
mounted signs, and fold backing. Custom Tide geometry goes through those same
built-scene checks. Native and WASI tests include the validator and its defect
regressions. Format details and limits are in `tools/README.md`.

The ordinary `tide run` command consumes the checked-in generated source;
run the checked CMake build after map changes before using that shortcut.
Spatial validation complements the input-driven solves and GPU checks;
it does not establish puzzle quality, pacing or every possible physics route.

The simulation fixtures include input-driven puzzle solves. Pacing, first-time
discoverability, live human co-op and sound balance still need human playtesting.
The intended 45–75 minute descent is a design target, not a measured playtime.
