# SKYWORKS

A first-person voxel automation prototype on three floating islands. Everything
is original to this prototype; it uses Tide's grids, deterministic simulation,
input, commands, drawing and GUI. No game packages or external assets.

Keyboard/mouse, 960 × 540 or larger. Progress lasts for the current session;
persistent saves aren't built.

## Play

From the repository root, for a player:

```powershell
cmake --preset debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset debug --target skyworks
build/debug/bin/skyworks.exe
```

Agents keep `TIDE_HIDDEN=1` and use `--frames`, `--capture` or `--script`.
The self-contained browser page is `build/web-debug/bin/skyworks.html` after
building `skyworks_page`, also staged at
`docs/static/prototypes/skyworks/index.html`.

| Control | Action |
|---|---|
| WASD / Space | Walk / jump |
| Mouse / arrow keys | Look (click the game to lock the mouse) |
| Hold left mouse / F | Mine blocks, chop logs, dismantle machines |
| Right mouse / G | Place selected machine or planks |
| E | Collect output; load ore into an empty kiln; toggle a switch |
| Tab | Open or close the workshop |
| R | Rotate the next output: east → south → west → north |
| 1–0 | Planks, drill, tube, kiln, crate, pump, tank, switch, wire, sensor |

Click a recipe card to select its machine, and **Craft** to make one. Machines
stand above the surface floor. To rotate a placed machine, dismantle and replace
it. Dismantling refunds the machine and all its contents. Falling returns you
to the protected landing without inventory loss. Factories run in the workshop.

## First factory

1. Chop logs (three wood each), mine grass/stone, and mine the orange copper seams.
   Each starting seam has 80 ore; it remains until depleted.
2. Hand-smelt six ore into ingots. Make a gear, drill, two tubes, wind kiln and
   crate. The full line costs **13 wood, 8 stone and 6 ore**, including smelting.
3. Put the drill on copper. Connect **drill → tube → kiln → tube → crate** on
   adjacent floor cells. Every copper output marker points to the next machine.
   Use SOUTH for a line going away from the landing.
4. The drill extracts every three seconds; the kiln makes an ingot every two.
   Transfers move one cell every half second. Full/incompatible storage stops
   transfer without losing items. These first wind machines need no fuel.
5. E collects the crate's copper. E also feeds ore directly to an empty kiln.

## Expand and finish

Start with 16 planks; one wood crafts four more. Select planks, look down at a
floor near the island edge, and place to extend it in your facing direction.
Walk onto the new block and repeat.

The **east** island has larger copper seams and sky crystals. The **south**
island has a blue spring. Put a pump above the spring; connect **pump → tube →
tank**, with outputs facing along the line. E collects water. Tanks hold 64;
tubes carry one item type at a time, including water.

Produce **12 ingots automatically**, then craft/place a beacon with **12 ingots,
4 crystals and 8 water**. Starting resources suffice for the complete route.

## Circuits

E toggles a switch. Orthogonal wires carry signals. A sensor signals when the
machine immediately ahead holds eight or more units. A machine adjacent to wire
only runs while an adjacent wire is live; unwired machines run freely. Wires OR
their sources and support loops. Signals update every half second. There are no
delays, inverters or adjustable thresholds yet.

The recommended next steps are saves/build previews, renewable forestry and a
sawmill, filtered/branching transport and water-consuming recipes; then energy,
richer circuits and resource islands. Portals/dimensions follow once the core
loop feels good. These are proposals. Engine gaps and validation:
[notes/skyworks.md](../../notes/skyworks.md).
