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

And one is for a body that code puts to sleep in a world that was just made
from an image:

- **`solver_set.c`: `b2TrySleepIsland` only marks a body's move event where
  the world still has it.** A body that's awake after a step says which of
  the step's move events was its own (`b2Body.bodyMoveIndex`), which is in the
  image, and the events aren't: a restore clears them. Upstream looks the
  event up regardless, to say the body fell asleep, so `b2Body_SetAwake(
  false )` before a restored world's first step wrote past the events: an
  assert in a debug build, and otherwise a byte written wherever that was.
  The package puts bodies to sleep (`Body.asleep`), and a client that runs a
  tick again does it in a world that was just made. The body says it has no
  event from there on either way, so its bytes in the next image are the
  same. `tide_box2d_sleep` makes its world again before every tick of a match
  that puts bodies to sleep.

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
- **Bodies sleep and wake an island at a time** (`b2TrySleepIsland`,
  `b2WakeSolverSet`): `b2Body_SetAwake( false )` puts to sleep every body of
  the island the body is in, whatever its speed, and `true` wakes all of the
  set it fell asleep with. An island is the bodies that touch each other, as
  the last step found them: every touching contact between two bodies of
  which neither is static joins their islands (`b2LinkContact`), a kinematic
  body's too, so a lift is in an island with what stands on it, and a floor
  that's static ties nothing together. A body that's put to sleep has no
  speed any more (its `b2BodyState` goes), and Box2D says none of which
  bodies went with it: the package asks, of each that was awake
  (`b2t_leave`).
- **A body that's made asleep** (`b2BodyDef.isAwake`) is alone in a set of its
  own, and a shape put on it leaves it asleep. Its contacts are made at the
  next step and aren't looked at until one of their bodies is awake, so
  bodies made asleep on each other wake one after another, a contact a step.
- **A speed that isn't zero wakes a body** (`b2Body_SetLinearVelocity`,
  `b2Body_SetAngularVelocity`), and so does an impulse; the package puts
  bodies to sleep after everything else a tick wrote, right before the step.

## Updating it

Copy the two folders over, apply the changes again, and run `tide_box2d_stack`
and `tide_box2d_course` natively, in debug and release, and for the web: their
pinned hashes are of the simulation and of every image, so a field that isn't
the same everywhere shows there, and bytes that are whatever was in memory
show between a debug build and a release one. A new version steps differently,
so the hashes change with it, and so do games' matches.
