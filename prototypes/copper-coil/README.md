# Copper & Coil

**Two robots. One bright idea.** A cooperative logic-circuit prototype made in
Tide. Copper and Coil restore a forgotten power station by reconfiguring gate
consoles, checking every input combination, and commissioning each bridge
together. All six puzzles currently share one miniature workshop layout.

## Play

The staged review build is served at
`http://localhost:4179/prototypes/copper-coil/`. From the repository root:

```powershell
cmake --preset web-debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset web-debug --target copper_coil_page
node prototypes/copper-coil/test/serve.mjs
```

Choose **Play the demo** for solo, **Same-keyboard co-op** for two at one desk,
or **Host online** and **Join a friend** on two machines. Online play requires
Internet access to Tide's existing room relay. Each online player gets one
robot; the surviving player can work both robots if a partner leaves.

| Mode | Controls |
|---|---|
| Solo | WASD or arrows move; E or Space interacts; Q switches robot |
| Same keyboard | Copper: WASD + E; Coil: arrows + Enter |
| Online | Each player: WASD or arrows + E or Space |
| All modes | Z undoes the shared last action; R resets; Chambers selects a puzzle |

Move beside your own console. E cycles WIRE/NOT or AND/OR/XOR. The orange and
mint outlines identify who can edit each console; gray consoles are fixed for
that chamber. A circuit must match **every** test row. Click a row to apply its
live input signals; the in-world levers also toggle them. On the last chamber,
Coil's extra lever controls safety signal C.

Once every row matches, select a row whose required output is ON and put both
robots on their round SYNC pads. Stay there for 1.2 seconds. The bridge locks
on, and both robots must reach their illuminated exit tiles. Power then stays
on so the crossing needs no precise timing. Use the hint and Guide when stuck.

Progress and undo belong to the current match; closing it does not save a
campaign. Reduced motion is in Guide; volume is in Menu > Settings. Narrow
screens have touch controls; desktop or landscape is the preferred demo view.

## Publisher demonstration

The four-page pitch and actual gameplay images are in
`docs/static/prototypes/copper-coil/`. The delivery ZIP also includes the native
Windows executable, its asset pack, the browser page and this walkthrough.
The title is a working title; the PDF separates current features from proposed
production work. No budget, sales forecast or external playtest is invented.

An eight-minute meeting route:

1. Open the title. Explain: "We give two friends the satisfaction of building
   a circuit that works, then let them bring a small world back to life."
2. Play **It takes two**. Move Copper D, D, W, W to stand beside MIX. Press E
   twice to select AND. Click the `1 1` test row. Move S, S, A onto Copper's
   SYNC pad. Switch with Q and move Coil A onto its pad. Wait for the bridge.
3. Switch with Q; move Copper D, W, W, D onto its exit. Switch again; move Coil
   W, A, W, A onto its exit. Show the shared completion card.
4. Open chamber 2. Explain that Copper configures AND while Coil must invert
   B. With a partner, give them Coil rather than demonstrating both jobs.
5. Use Chambers to open **The last light**. Show the eight-row test board,
   safety input C and the extra mixer. The complete solution is WIRE, WIRE,
   XOR, WIRE, AND; select row `1 0 1` before commissioning.
6. Use the final pitch page to discuss a funded vertical slice with distinct
   room layouts and movable modules. These are proposals, not shipped features.

## Build and verify

Enable `TIDE_PRIVATE_PROTOTYPES=ON`. Native targets are `copper_coil` and
`copper_coil_tests`; web staging is `copper_coil_page`. Every game build has
STATS. The C tests solve all six chambers through player commands and cover
authority, bad/stale commands, gate restrictions, charge, undo and snapshots.
`copper_coil_play_webgl` and `copper_coil_play_webgpu` complete all six chambers
in order through solo keyboard/mouse input, then check shared-keyboard controls
and two-browser WebRTC play with a local relay and muted audio. Test code is
isolated under `test/tide.packages`, outside CLI game discovery.

The PDF is reproducible with `test/make_pitch.py` (ReportLab, Segoe fonts on
Windows); screenshots come from actual play. `sfx/make_sounds.py` makes all
six original cues from arithmetic, without third-party recordings. No engine,
compiler or runtime implementation changes are needed for this prototype.
