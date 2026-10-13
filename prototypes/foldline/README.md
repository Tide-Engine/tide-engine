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

Wallpaper and marked panels take folds; bare machinery refuses them. Shots
pass through grilles, but closed doors and moving platforms block them. Small
frames set an end's size: leaving one makes you and your carried object smaller.
You keep that size until another fold changes it. Folds are on upright walls;
floors and ceilings do not take them yet.

Both A and B must stay weighted to open an exit. The first operator through
stands on the lift's HOLD plate to let the other leave their plate. Both walk
into the lift to continue. Ordinary puzzle rooms have one loose cube, so
parking a spare crate on the other plate cannot replace that handoff. The
water challenges retain several physical objects for throwing and distraction.

The descent alternates spatial puzzles with horror passages:

1. **A shortcut through space:** learn crossings, carrying and the exit relay.
2. **High Ground:** two fenced galleries, one weight and two independent folds.
3. After the **Maze**, **Smaller** introduces a vent and a wall-owned small end.
4. **Undertow:** an underwater circuit, a creature and limited distractions.
5. After the **Long Pool**, **The Drums** tests timing and a hidden gallery.
6. **All of It** combines retrieval, size, a restricted entrance and drums.
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
