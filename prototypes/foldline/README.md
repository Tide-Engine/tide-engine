# FOLDLINE

A first-person cooperative spatial puzzle built in Tide. Two operators share
one amber/cyan portal pair inside a sunlit research atrium. Portals show live,
perspective-correct views of the space beyond them. Walk through a fold to carry
your position, facing and momentum to its other end.

Play alone by switching between the two bodies, or host a room and share its
code with a partner. A joining partner takes operator B. The unused body remains
in place in solo mode and can hold a pressure plate.

| Control | Action |
|---|---|
| WASD / left stick | Move |
| Mouse / right stick | Look |
| Space / south button | Jump |
| Left click / right trigger | Place the amber portal on a receptive panel |
| Right click / left trigger | Place the cyan portal on a receptive panel |
| E / west button | Pick up or release the resonance cube |
| Q / north button | Ping a location for your partner |
| Tab | Switch operators while alone |
| R | Reset the current chamber |
| Escape | Open the menu |

The menu's **01 / 02 / 03** buttons select an experiment. A successful experiment
opens the next one from its completion screen.

1. **A shortcut through space:** weight circuit A, then bring both operators to
   the exit beyond the divider. The cube can keep the circuit occupied.
2. **Two sides of the same idea:** weight both circuits at once. Reconnect the
   rooms to move a cube or an operator into position.
3. **The resonance bridge:** leave the cube on circuit A to power the bridge.
   An operator reaches circuit B across the lower machine bay. Completing the
   circuit sustains the bridge and opens the exit.

Circuits latch after solving; there is no timed sprint to an exit. Falling into
the machine bay returns an operator or a loose cube to its starting point.
Opening a menu stops this machine's controls while the shared match keeps going.

## Build and run

From the repository root:

```sh
cmake --preset debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset debug --target foldline foldline_test
ctest --preset debug -R '^foldline_test$'
build/debug/bin/foldline
```

Or run `tide run` inside this folder. No external packages are required.
For the public browser page:

```sh
cmake --preset web-debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset web-debug --target foldline_page foldline_test
ctest --preset web-debug -R '^foldline_test$'
```

The staged address is `/prototypes/foldline/`. Keyboard and mouse are the primary
controls; gamepad bindings are available for gameplay. The prototype does not
yet provide touchscreen movement.

This is original game code with procedural geometry and materials. The current
scope is three experiments, six vertical portal panels and a single shared pair.
