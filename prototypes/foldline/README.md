# FOLDLINE

A first-person cooperative spatial puzzle in a descending, liminal building.
Each operator has a portal pair: A uses amber/cyan, B uses rose/violet. Anyone
and any loose object can travel through either pair. Linked folds show the
actual space beyond them and carry position, facing, size and momentum.

Play alone by switching between two bodies, or host a room and share its code
with a partner. A joining partner takes operator B. The unused body stays where
you leave it in solo mode, including on a pressure plate.

| Control | Action |
|---|---|
| WASD / left stick | Move |
| Mouse / right stick | Look |
| Space / south button | Jump |
| Left click / right trigger | Place your first fold end; throw a held object |
| Right click / left trigger | Place your second fold end; throw a held object |
| E / west button | Pick up or drop a cube or crate |
| F / east button | Throw a held object |
| Q / north button | Ping a location for your partner |
| H | Show or hide the current puzzle hint |
| Tab | Switch operators while alone |
| R | Reset the current room |
| Escape | Open the menu |

Wallpaper and marked panels take folds; bare concrete, strapped pillars and machinery refuse them. Shots
pass through grilles, but closed doors and moving platforms block them. Small
frames set an end's size: leaving one makes you and your carried object smaller.
You keep that size until another fold changes it. Folds are on upright walls;
floors and ceilings do not take them yet. An end placed below the blood is
allowed, and its linked view is opaque red, including through another fold.

Every puzzle starts with its cube clamped in place. Two different operators
must occupy the green C and D controls together to release it and power the
exit circuit; crates cannot substitute for either operator. The controls are
at the room's puzzle workstations: galleries, separate vent rooms, opposite
banks or machinery stations. Once released, the cube stays free.

Both A and B must then stay weighted to open an exit. The first operator through
stands on the lift's HOLD plate to let the other leave their plate. Both walk
into the lift to continue. Its doors stay at the same positions through a brief
blackout, then the forward door opens. Your position and facing inside are kept. Ordinary puzzle rooms have one loose cube, so
parking a spare crate on the other plate cannot replace that handoff. The
water challenges retain several physical objects for throwing and distraction.
Loose cubes and crates float in water and can be picked up again. The creature
follows the latest splash for seven seconds; it eats neither blocks nor controls.
Green beacons and surface outlines mark submerged controls. Undertow's C control
is on a shallow step, where a body or recovered block can hold its door open.

The descent alternates spatial puzzles with horror passages:

1. **A shortcut through space:** one controls the far-wall cover, the other builds a crossing and takes over from the far bank.
2. **High Ground:** reach both fenced galleries, release the cube together, and keep routes back down.
3. After the **Maze**, **Smaller** needs both operators to shrink and navigate separate vents and baffles.
4. **Undertow:** a marked shallow control, a creature and reusable floating distractions.
5. After the **Long Pool**, **The Drums** pairs brake controls with a crossing and a hidden gallery.
6. **All of It** pairs a gallery controller with a shrinking partner crossing the drums.
7. After **The Works**, **Line of Sight** needs two door circuits and two folds.
8. **The Shaft** combines a rising deck, moving platforms and a small cargo slot.
9. **Blackout** combines a launch pad, controlled lighting and a creature.

A final Maze leads back to the start. The menu's stop selector follows this
order. The older **The Lure** room remains selectable through the test command
but is outside the descent. Later puzzle hints appear only when H is pressed;
passages always explain their survival rule. Opening a menu stops this machine's
controls while the shared match continues.

## Build and run

From the repository root:

```sh
cmake --preset debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset debug --target foldline foldline_test foldline_render_test
ctest --preset debug -R '^foldline'
build/debug/bin/foldline
```

Or run `tide run` inside this folder. The bundled Box3D package supplies physics;
no external package download is required. For the browser page:

```sh
cmake --preset web-debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset web-debug --target foldline_page foldline_test foldline_render_test
ctest --preset web-debug -R '^foldline'
```

Serve `docs/static` and visit `/prototypes/foldline/`. The page is self-contained.
Keyboard/mouse and gamepad gameplay are supported; touchscreen movement is not.
The PSX-style picture uses 300 rows with nearest textures and dithered lighting.
