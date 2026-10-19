# Foldline assembly maps

`maps/*.foldmap.json` is the source for the campaign's lift slots, circuitry,
moving machines and their connections. Reception also owns its structural
boxes and initial folds here; its ending reuses that same map. Other places
keep their procedural architecture, encounters and gameplay in Tide.
Coordinates are metres, with arbitrary real values and heights. There is no
cell grid or fixed room size.

The compiler writes `maps_generated.tide`, which is checked in so ordinary
`tide run` and packaged source builds need no Python at runtime. Each place
builder calls its generated `BuildMap...` function. Change the JSON, then run:

```powershell
python prototypes/foldline/tools/foldmap.py
python prototypes/foldline/tools/foldmap.py --check
python -m unittest discover -s prototypes/foldline/tools -p "test_*.py"
```

`--check` rejects any stale or hand-edited generated Tide. The native and WASI
test hosts can export the actual built entities, including geometry made by
custom Tide code:

```powershell
build/debug/bin/foldline_test.exe --export-layout layout-built.json
python prototypes/foldline/tools/foldmap.py --check --scene layout-built.json
```

For diagnosis while editing a map, `--scene-only --scene layout-built.json`
checks that exported geometry without requiring source/output correspondence.
The full check always uses `--check --scene`.

## Source format

Every source has `version: 1`, a unique `name`, a `function` name and
`place_ids`. Campaign IDs 2..15 must each belong to exactly one source map;
Reception owns `[2, 15]`. Its generated function takes `(Arena arena, mut Place
place)` and adds `mut Fold fold` when it creates preplaced ends.
Unknown fields are rejected at every source-object level, so a misspelled
setting cannot silently use a default. Assembly IDs are unique across object
kinds within a map; anchor names use a separate identifier namespace. Flags
are JSON booleans. Dimensions and other numeric inputs must be finite.

- `boxes`: named `id`, `min`, `max`, `paint`, optional `takes` and `kind`.
  These generate the real `Fill` calls; they are geometry, not a second list
  of approximate collision annotations.
- `lifts`: named `id`, `role` (`arrival`, `exit`, `stop`), floor middle `at`
  and quarter-turn `yaw` (`NORTH`, `EAST`, `SOUTH`, `WEST`). An exit has
  `need: [signal numbers]` and optional `any`; a stop has `stay` seconds.
  An exit that needs signals also has `socket_x`: where its signal lamps sit
  along the lintel in the lift's own frame (x along the doorway), 0.9 to 2.6 m
  from the sign at its middle, on the side its cable comes up. Its cable ends
  at that socket, (socket_x, 4.2, 2.65) in the lift's frame.
  The standard prefab creates its shell, threshold, door and exit sign.
- `plates`: named `id`, top middle `at`, `signal`, optional `heavy`, `need`
  kilograms, `half` footprint, `linger` seconds and `invert`.
  An explicit `need` is positive; omit it to use the standard/heavy threshold.
  Linger is nonnegative; a stopping lift's stay is positive.
- `doors`: named `id`, closed middle `at`, `size`, `need`, optional `any`,
  `speed`, named/vector `socket` and `lamp_depth`. A socket mounts the
  machine's actual fixed circuit lamps; it is not detached metadata.
- `machines`: named `id`, `from`, `to`, `size`, `need`, fixed `lamp`, `along`,
  optional `any`, `speed` and `lamp_depth`. This retains arbitrary platform
  and shutter travel. `DOOR_SPEED` and `MOVER_SPEED` are allowed speed values.
  Signal lamps use a unit horizontal cardinal axis (positive or negative x/z).
  Uncontrolled moving props with no lamps may use a zero axis. An explicit
  lamp depth is positive.
- `anchors`: named surface attachments. A named-box attachment derives its
  world position from `support`, `face`, two `uv` world coordinates on the
  remaining axes, and an outward `offset`. For example:

  ```json
  {"support":"divider", "face":"-x", "uv":[4.35,4], "offset":0.03}
  ```

  Faces are `+x`, `-x`, `+y`, `-y`, `+z`, `-z`. A custom Tide surface uses
  `{"support":"scene", "at":[x,y,z]}`; its actual supporting faces are
  resolved from the built export. Geometry still has to support the entire
  cable, including the spaces between its anchors.
- `wires`: named `id`, `signal`, source plate `from`, target lift/machine
  `to`, and 2..12 `points`, each an anchor name or explicit world vector.
  All needed target inputs require an incoming connection.
- `ends` / `frames`: named backing `wall`, `face`, bottom middle `foot`,
  `size`; an end also has its operator-owned `end` index 0..3. Backing is
  resolved to the actual named structural box.

`external_geometry: true` explicitly declares that the map also uses geometry
built by Tide. Named-box anchors are still checked immediately. Complete
surface and apron proofs are deferred to the mandatory actual-scene check,
where Tide-created geometry receives the same checks as generated geometry.
This allows custom shapes and scripts without turning them into an exemption.

## What is checked

The source compiler checks references, finite dimensions, signal connections,
plate contacts, target sockets, point limits, named-face bounds and preplaced
fold fit/backing. Complete maps also prove cable support and lift clearance
before generating Tide.

The actual-scene check checks every campaign place. It compares the generated
connection assemblies against the real entities, independent of entity order:
wire routes/counts/signals, plate footprints/weights, machine travel/speed/
sockets, lift transforms/roles and generated structural boxes. An omitted
`BuildMap` call, leftover old wire, missing map export or changed circuit cannot
pass merely because there is nothing left to inspect.

Every actual cable segment must lie continuously 4..90 mm outside real solid
faces. Exact parameter intervals are merged across adjoining surfaces; two
supported endpoints alone are insufficient. Grille collider envelopes cannot
serve as solid mounting surfaces. Small tangential seams have a 40 mm joining
tolerance. Rotated static prop faces use the actual body quaternion.

Every lift clears its full six-metre opening and four-metre height, with a
two-metre approach for a through route. A stopping vignette instead reserves
a one-metre observation apron before its protective grille. A flat floor must
cover the entire apron: the proof partitions all actual floor edges and tests
the exact rectangle union, including narrow gaps. Rotated prop/ramps use their
oriented boxes for obstruction checks. Exit signs must occupy the room-facing
header socket, face the approach and have a supporting surface; the exit's
signal lamps must not cover it. Every actual
frame and open preplaced end must fit a receptive backing face at least 0.6 m
thick.

Furniture exports include every attached Box3D compound part, transformed by
its owning prop's body. The audit requires the structural part count for each
solid furniture kind and the corresponding actual shapes in the geometry
export. A visual chair/cabinet with no collider, or an exporter that omits its
parts, fails. The game uses the same `PropBlockPart` description to draw those
structural pieces and attach their shapes, keeping leg and shelf gaps open.
Flat signs, clocks, silk, cloth and foliage details remain dressing; curtain
rails, plant pots and stems are physical.

Structural top faces may meet at edges but cannot overlap over an exposed
area. The check subtracts the exact union of covering solids, so buried joints
remain valid and a narrow visible strip still fails. Grille envelopes cannot
hide an overlap. Room boundaries also leave the full eight-metre lift shell
slot around its six-metre doorway; a room wall cannot share the cabin lining's
inner side plane, including when the lift is rotated.

These are assembly guarantees. They do not infer puzzle solutions, monster
fairness, every possible movement exploit, sign glyph readability or portal
shader correctness. Input-driven solves and the real GPU regressions remain
necessary for those properties.
