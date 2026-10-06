# Box2D, as this package has it

`src/` is Box2D's `src/` and, in `src/box2d/`, its `include/box2d/`, from
https://github.com/erincatto/box2d at commit
`ac7c751eaeddbabdc1c4d41ae4f3a25d78627790` (4 October 2026, after v3.1.1), under
its MIT license (`LICENSE`). A game that ships with this package ships Box2D,
and its license asks for the copyright notice to go with it.

The headers are beside the sources so that `#include "box2d/box2d.h"` finds
them with no include path, which a game's C has none of.

## What's changed

Each change is marked `Tide:` where it is. Most are for one thing: a world's
image (`b2World_GetSnapshot`) is kept in the match, where it's hashed and sent
between players, so it has to be the same bytes on every machine for the same
state, and one machine's has to load on another.

- **`body.h`, `shape.h`, `joint.h`: `userData` is 8 bytes on every platform**, a
  union with a `uint64_t`. These three structs go into an image as their bytes,
  and a pointer of 4 bytes on the web made them another size there.
- **`snapshot.c`: the layout hash** is of those structs only. Upstream also
  mixes in `sizeof( void* )` and the sizes of the structs that hold pointers,
  which are written field by field, so an image only loaded where pointers are
  as wide as where it was made.
- **`snapshot.c`: `userData` is zeroed in all 8 bytes** as it's written.
- **`snapshot.c`: a tree's node capacity is written as twice its node count**,
  which is what a restore makes of it. Upstream writes the capacity the tree
  has, so a world that was restored wrote other bytes from then on than one
  that never was.
- **`snapshot.c`: a sensor's visitors are written field by field, with zeros
  after** (`b2SerVisitors`). A `b2Visitor` is a shape's index and its
  generation, 6 bytes of 8, and the other two are whatever was in memory where
  its array grew. Upstream writes the arrays as their bytes, so the image of a
  world with something in a sensor was other bytes from one run to the next:
  the package's match of sensors had another hash of its images in a debug
  build than in a release one. The reader takes them as before.
- **`contact_solver_wide.inl`: a contact point's `normalVelocity`** is set for
  the contacts that use it (restitution, or hit events) and zero for the rest,
  as the overflow solver does. Upstream decides for a whole block of contacts
  at once, 4 or 8 by the CPU's SIMD width, so a contact that doesn't use the
  field kept a value or didn't by what shared its block: the same simulation,
  and other bytes on an AVX2 machine than on the web.

One more is for speed, as an image is taken every step:

- **`recording.h`, `recording.c`, `snapshot.c`: `b2World_GetSnapshot` writes
  straight into the caller's memory**, once (`b2RecBuffer.fixed`). Upstream
  writes the image into memory of its own, which grows as it goes, and copies
  it over. An image that doesn't fit is now partly written; its whole size is
  still what's returned.

And one is for what a match keeps of an image from tick to tick:

- **`snapshot.c`, `recording.h`, `box2d.h`: an image in sections that keep
  their place** (`b2World_GetSnapshotAt`, `b2GetSnapshotBases`,
  `b2GetSnapshotHead`, `B2_SNAPSHOT_SECTIONS`). Upstream's image is one run of
  bytes that starts with its parts of changing size (the id pools, then the
  solver sets, the awake one before the sleeping ones), so a contact that
  starts or ends moves every byte after it, and the whole image differs from
  the one before though next to nothing changed. Here an image can have a
  table after its header of where each of 17 sections starts, which the caller
  gives: the writer goes to each one's place (`b2SnapSection`), over zeros,
  and the reader goes where the table says (`b2SnapSeek`). A section that runs
  into the next one's place is reported, with every section's size, and the
  caller places them again. The sections are in upstream's order, and an image
  without the table reads as it always did. `bridge.c` keeps each section where
  it was while that leaves it between none and a quarter of the one before it
  to spare, so an image is an eighth bigger and what changes of it is what
  changed in the world.

And one is for threads on the web, where upstream only knows Emscripten:

- **`solver.c`: a worker that has waited long for the next stage sleeps**
  (`b2SolverTask`, `B2_TIDE_WAIT_ROUNDS`), on the web only. A step's workers
  wait for each stage in a loop, with the CPU's pause in it and, every few
  rounds, the system's yield. WebAssembly has no pause and a page no yield, so
  there the loop holds a processor for as long as it waits, and with a worker
  on every one a page has, the system takes a processor from a worker that has
  a block to run whatever else the machine has: the stage then waits a time
  slice for that block. After 4096 rounds of the wait, which is about what
  waking a worker takes, so that the waits between stages go as they did, a
  worker sleeps until the main thread gives the next stage (`b2t_stage_sleep`,
  and `b2t_stage_wake` after each change of `atomicSyncBits`, both the
  bridge's, which has them from `tide/jobs.h`). On a thread that can't sleep,
  a page's own, it goes on with the loop. A world steps the same either way,
  as it does with any number of workers.

And one is for how a world is tuned, which a game sets through `Physics`:

- **`box2d.h`, `physics_world.c`: `b2World_GetContactTuning`**, which gives
  back what `b2World_SetContactTuning` set: the contacts' hertz, their damping
  ratio and the speed bodies are pushed apart at. Upstream has the setter
  alone. The three are in a world's image, so the package asks the world for
  them each step and only sets, and notes, what differs: a world that came
  from an image is asked the same way as one that was stepped here.

## What the package asks of it

Nothing here is changed for these, but the package rests on them, and a new
Box2D has to be read for them again:

- **A shape is only reported to a sensor when both have `enableSensorEvents`**,
  which is off unless a shape is made with it, for a sensor too. The package
  makes every shape with it, so that a sensor sends `Touched` and `Parted`.
  It's a byte of each shape in the image.
- **Sensors see each other** (`b2SensorQueryCallback` has no check for it), so
  the package makes its sensors in one collision group below zero
  (`filter.groupIndex`), which Box2D never sets against each other: two that
  overlap would each report the other, and their entities hear of it twice.
- **What a sensor has in it is in the image** (its three arrays of visitors),
  and the events of a step aren't: a world made from an image has the same
  begins and ends to say after its next step as the world the image was taken
  of. `tide_box2d_course` makes its world again before every tick to see that.
- **`b2Body_SetTransform` doesn't wake a body, or what rests on it.** The
  package wakes both when code moves a body (`b2Body_SetAwake`,
  `b2Body_WakeTouching`).
- **There's no call to wake every body** but turning sleeping off, which the
  package does, and on again, when gravity changes (`b2World_EnableSleeping`).
  A world's gravity is in its image, which is how the package tells a change.
- **A body made with no shape weighs nothing**, so an impulse does nothing to
  it: the package gives a new body's impulses once its shapes are on it.
- **A contact only says how hard it was when both shapes have
  `enableHitEvents`**, off unless a shape is made with it. The package makes
  every shape with it, for `Hit`. It's a byte of each shape in the image, and
  it has the solver keep each contact point's `normalVelocity`, which is in
  the image too (see `contact_solver_wide.inl` above). It changes nothing of
  how a world steps: the package's pin of the bodies alone didn't move.
- **A world's tuning is in its image**: gravity, the contacts' hertz, damping
  ratio and push speed, the restitution and hit thresholds and whether bodies
  sleep. The package compares each with what `Physics` says before a step.
- **Contact hertz count up to an eighth of the substep rate**
  (`physics_world.c`, `b2MinFloat( world->contactHertz, 0.125f * inv_h )`): 30 at 4
  substeps and 60 steps a second, which is the default, so stiffer contacts
  need more substeps. The package's docs say so.
- **`b2Body_SetType` lets go of the body's contacts and makes them again**, so
  the next step has end and begin events for what touched it, and it wakes the
  body. It doesn't wake what rested on it when the body turns static: the
  package wakes what touches it first (`b2Body_WakeTouching`).
- **`b2DestroyShape` with its last argument true** updates the body's mass,
  which is what the package wants when a part goes.
- **A body's type is what Box2D says** (`b2Body_GetType`), which is in the
  image; the package keeps no copy of it in the match.
- **The queries read nothing that isn't in the image**: the trees, the shapes
  and the bodies' transforms for `b2World_CastRay`, `b2World_CastShape` and
  `b2World_OverlapShape`, and a sensor's visitors for
  `b2Shape_GetSensorData`. They report shapes in the order the trees are
  walked in, which a restore doesn't keep, so the package never takes the
  first thing reported: the nearest is the least by fraction and then by the
  shape's index, with the walk clipped a hair past the best so far, and lists
  are sorted by the bodies' indices.
- **The queries report sensors**, which the package passes by
  (`b2Shape_IsSensor`).
- **A cast shape counts as touching within its target distance**
  (`B2_LINEAR_SLOP`, 5 mm, and a quarter of it more): one that starts that
  near something meets it at fraction 0 with a zero normal.
- **A cast's fraction doesn't depend on how far it was allowed to go**, while
  that's past the fraction (`b2ShapeCast` advances conservatively from the
  start), and a shape met exactly at the allowed fraction is a miss: which is
  why the walk is clipped past the best, not at it.
- **Queries change nothing of a world**, so threads may ask the same world at
  once. The package keeps what a query needs of its own per thread.

## Updating it

Copy the two folders over, apply the changes again, and run `tide_box2d_stack`,
`tide_box2d_course` and `tide_box2d_kit` natively, in debug and release, and
for the web: their pinned hashes are of the simulation, of every image and of
every answer to a query, so a field that isn't the same everywhere shows
there, and bytes that are whatever was in memory show between a debug build
and a release one. A new version steps differently, so the hashes change with
it, and so do games' matches.
