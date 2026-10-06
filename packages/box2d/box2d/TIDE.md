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
state, and one machine's has to load on another. The rest are for speed, and
leave those bytes as they were.

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

Two are for how long a step takes in a browser, where upstream only knows
Emscripten. The first is WebAssembly's SIMD:

- **`core.h`, `contact_solver_wide.inl`, `simd.h`, `manifold.c`: a path on
  WebAssembly's own SIMD** (`B2_SIMD_WASM`), for any WebAssembly built with
  `-msimd128`, as Tide's web builds are. Upstream's one path for WebAssembly is
  SSE2's headers as Emscripten emulates them, which clang's own target has
  none of, so there Box2D took its scalar path. Each place upstream has code
  for SSE2 now has the same for WebAssembly after it: the contact solver's
  wide floats and its gather and scatter of bodies, the trees' bounds
  (`b2AABBV`), and the face search of two polygons (`b2FindMaxSeparation`).
  `core.h` has one branch for WebAssembly, Emscripten's too, where this stands
  in for the emulation; built without `-msimd128` it's the scalar path, as
  before.

  Each function gives, bit for bit, what the SSE2 one beside it gives for the
  same numbers, so a browser steps the world x64 does: the least and the
  greatest are `f32x4.pmin` and `f32x4.pmax` with their operands the other way
  around, which are `minps` and `maxps` exactly (the first where it's less, or
  greater, and else the second, whichever zero that is and whatever is NaN,
  where `f32x4.min` and `f32x4.max` take -0 for less than 0); comparisons are
  the ordered ones; a blend takes each bit by its mask's bit; the rest is
  IEEE 754's arithmetic, each operation rounded by itself, and lanes moved.
  Nothing is relaxed SIMD, whose fused multiply-add differs between machines.

  It's nearer to x64 than the scalar path was: that one gives the first of two
  equal numbers where SSE2 gives the second, so it could leave the other zero.
  The package's pins never showed that, and they are what they were, in debug
  and release web builds. Beside them, a check outside the tests ran every one
  of these functions on awkward numbers (every three of 24 values with both
  zeros, denormals, infinities and NaNs among them, 40,000 of any bits at all,
  and 40,000 small ones of which many are equal), and about 5,000 pairs of
  polygons through `b2CollidePolygons`, built for x64 and for the web: the
  same bits from both for all 23 kinds of result, where the scalar path
  differs in 9.
  Which NaN arithmetic gives of two isn't compared: that's the CPU's, and no
  NaN is ever in a world.

  It took a quarter off a step in a browser. The package's `README.md` has the
  numbers, and why it's no more: clang had already made SIMD of most of the
  scalar path by itself.

  At an update, the WebAssembly code is read beside the SSE2 code it follows:
  a function upstream adds to the solver's wide floats needs its twin, or the
  web build doesn't compile, and one it changes has to change there too.

The second is for threads:

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

And the rest are for what a match writes of an image from tick to tick: an
image as what changed of the last one.

A match takes an image after every step, and most steps change little of a
world: a few bodies are awake. Writing all of it, and comparing all of it with
the last one, goes by how big the world is. Here a world can say what changed
instead, and the image the parts make is, byte for byte, the image written
whole: `tide_box2d_stack`'s pinned hashes are of both, and `tide_box2d_parts`
compares the two after every step.

- **`snapshot.c`, `box2d.h`: `b2World_GetSnapshotParts`, `b2SnapshotPart`,
  `b2World_SetSnapshotAwakeShare`, `b2World_CheckSnapshotAt`,
  `b2GetSnapshotRecordSize`.** The parts to write over the world's last image,
  which the caller has: the world keeps no copy. It says there's no telling
  (the caller takes the image whole) for a world with no image yet, a world
  that was restored, one that had something done to it that isn't followed
  (below), and one with more than half of its bodies awake, where writing it
  whole costs no more. The last image is the one `b2World_GetSnapshotAt` or
  this last gave, whole and at the same bases. `b2World_CheckSnapshotAt` is the
  image written whole that isn't taken for the last one, for a test to compare
  the parts with; `b2World_SetSnapshotAwakeShare` has a test's world say what
  changed whatever is awake; `b2GetSnapshotRecordSize` is how much room a body,
  a shape or a contact takes, for what a failed comparison says.
  `physics_world.c` frees what follows the images with the world
  (`b2DestroySnapshotTrack`).
- **`recording.h`, `recording.c`, `snapshot.c`: the writer is the one that
  writes an image whole** (`b2RecBuffer.parts`, `b2SnapParts`). What it
  appends goes into memory of the world's own as parts that each say where in
  the image they are, and what it leaves out is what the last image has
  already. So there's one piece of code that says what a body's bytes are in
  an image, for both:
  - Bodies, shapes, their fat bounds and contacts each take the same room, so
    one is written at its own place: `b2SerSimArray` and `b2SerPodRuns` write
    them in runs, which is all of them for an image written whole and each
    stretch that may have changed for one in parts. A run of bodies or shapes
    goes as its bytes, and then zeros go over each one's `userData`
    (`b2SerSimRun`, `b2SnapZero`, `b2SnapAt`), where upstream writes a copy of
    each with its `userData` cleared, one after another: the same bytes, and
    an image written whole is quicker for it too.
  - The other sections are written whole. What goes into an image as it is in
    the world (a solver set's bodies and contacts, a tree's nodes, the pairs)
    isn't copied: the part points at it (`b2SnapW_Ref`), and whoever reads the
    parts compares it where it is. So the parts are good until the world is
    next stepped or changed.
  - A section that's smaller than it was has zeros after it, where the last
    image had its end (`b2SnapSection`). So every section is come to, in
    order, whatever the world has: upstream's loop never comes to the awake
    set's or the sleeping sets' in a world with no such set, which wrote the
    same bytes whole and would have left the last image's there in parts.
  - Four sections are left out when nothing was done to what's in them since
    the last image (`b2SnapKeepSection`): the static and the disabled set,
    the sleeping sets, the islands and the pairs. Nothing writes those but
    what says so (below): the step's workers only write what's awake.
  - A world with more bodies, shapes or contacts than its last image had goes
    on in parts: each of those sections is a count and then its things, so
    the new ones go after the ones that were there, and are all written.
- **`recording.c`: `b2RecBufReserve`**, which `b2RecBufAppend` now is, with a
  copy after: where the next bytes go, for a caller that writes them itself.
  **`snapshot.c`: `b2SerIslands`** asks once for all the islands and writes
  them there, where upstream appends each number of each island: the same
  bytes, for a fraction of the time. There's an island for every stack.
- **`physics_world.h`: `b2SnapTrack`, which bodies, shapes and contacts may
  have changed since the last image**, a bit each; a shape's bit is its fat
  bounds' too. Between two images, one has changed only if
  - its body (or one of a contact's two) was awake at the first image, at the
    second, or between: the step writes those on its worker threads, and
    nothing says which. So everything awake goes into the sets when an image
    is taken, for the next one (`b2SnapCommit`), and again when that one is
    (`b2SnapSeed`): a body, its shapes and the contacts in its list;
  - or it was written from outside the step's workers, on one thread, which is
    said where it's done. Each of these is marked `Tide:` and names
    `b2SnapTrack`:
    - `contact.c`, `b2CreateContact` and `b2DestroyContact`: the contact, both
      its bodies (their lists' heads and counts), its neighbors in both lists,
      whose links change, and the pairs. Any of them may be asleep or static:
      the ground is in a list with every crate on it.
    - An element taken out of an array moves the last one into its place, and
      the moved one's index is written: a contact of a solver set
      (`b2DestroyContact`, `b2WakeSolverSet`, `b2TrySleepIsland`,
      `b2RemoveNonTouchingContact`), of a graph color
      (`b2RemoveContactFromGraph`, `b2TrySleepIsland`) or of an island
      (`b2UnlinkContact`); a body of a solver set (`b2RemoveBodySim`) or of an
      island (`b2RemoveBodyFromIsland`); a sensor's shape (`b2DestroySensor`,
      `b2DestroyShapeInternal`). The one from the disabled set is any contact
      between two sleeping bodies, anywhere in the world.
    - `solver_set.c`, `b2WakeSolverSet` and `b2TrySleepIsland`: each body that
      wakes or falls asleep, with its shapes and contacts
      (`b2SnapTouchBodyDeep`), so that one that's awake between two images and
      at neither is written.
    - `physics_world.c`, where a step's contacts that started touching,
      stopped, or ended are gone through one at a time, and `island.c`,
      `constraint_graph.c`: a contact that's told its island
      (`b2AddContactToIsland`, `b2UnlinkContact`, `b2MergeIslands`) or where
      it is in the graph (`b2AddContactToGraph`), and a body that's told its
      island (`b2MergeIslands`, `b2CreateIslandForBody`).
    - `body.c`: `b2CreateBody`, `b2DestroyBody` (the body and each of its
      shapes), `b2Body_SetTransform` (each of its shapes: it doesn't wake),
      `b2UpdateBodyMassData`, and `b2Body_SetBullet` and
      `b2Body_SetMotionLocks` (the body's flags, which neither wakes it for).
      `shape.c`: `b2CreateShapeInternal` (the shape,
      its body, and the shape that headed the body's list), `b2CreateShape`
      (the body's flags), `b2DestroyShapeInternal` (the shape, its neighbors
      in its body's list, and the body).
  - **What a contact is to the solver isn't in the contact.** Its manifold and
    its impulses are in its bodies' solver set, or in the constraint graph
    when it touches and is awake: Box2D keeps a set for the static bodies, one
    for the disabled ones, which also holds every contact between two bodies
    that are asleep and don't touch, one for what's awake, and one for each
    island that sleeps. The awake set and the graph are written every time.
    The others are two sections of an image, the static and the disabled set
    and then the sleeping sets, and each is written when something says it
    was written (`b2SnapTouchSet`): a set made or destroyed
    (`b2TrySleepIsland`, `b2DestroySolverSet`, `b2CreateBody`), a body put in
    or taken out (`b2CreateBody`, `b2RemoveBodySim`), a contact put in or
    taken out (`b2CreateContact`, `b2DestroyContact`, `b2WakeSolverSet`,
    `b2TrySleepIsland`, `b2RemoveNonTouchingContact`), an island's place in it
    (`b2CreateIsland`, `b2DestroyIsland`), a body's place, mass or settings
    (`b2Body_SetTransform`, `b2UpdateBodyMassData`, `b2SyncBodyFlags`,
    `b2Body_SetLinearDamping`, `b2Body_SetAngularDamping`,
    `b2Body_SetGravityScale`), and a contact that's told where one of its
    bodies is now (`b2RefreshBodyContactIndices`, `b2UpdateBodyMassData`): a
    static body that goes moves another into its place, and every sleeping
    contact of that one is told.
  - **The islands are written when one was written** (`b2SnapTouchIslands`):
    made or destroyed (`b2CreateIsland`, `b2DestroyIsland`), given a body, a
    contact or a joint or losing one (`b2CreateIslandForBody`,
    `b2RemoveBodyFromIsland`, `b2AddContactToIsland`, `b2UnlinkContact`,
    `b2AddJointToIsland`, `b2UnlinkJoint`, `b2MergeIslands`), split
    (`b2SplitIsland`), or moved to another set (`b2WakeSolverSet`,
    `b2TrySleepIsland`). An island that only steps isn't written.
    `b2SplitIsland` may run as a task of the step's, by itself, while nothing
    else says anything, and the step waits for it. It says nothing of the
    bodies and contacts it tells their new island: they're an awake island's.
  - **Any other call writes the next image whole.** `recording.h`: `B2_REC`
    and `B2_REC_CREATE`, which every call that changes what an image holds
    goes through, say which call it is (`b2SnapNoteOp`, by an enum of the
    calls' names that's made from `recording_ops.inl`), and only the ones
    looked at for the lists above are followed: the step, making and
    destroying a body, its transform, its speeds and impulses, waking it or
    putting it to sleep, waking what touches it, its dampings, its gravity
    scale, whether it's a bullet and what it's locked from, making a circle, a
    capsule, a segment or a polygon, and destroying a shape. So are the
    world's settings that are numbers of its own, which every image writes
    (its gravity, its contact tuning and the rest), and turning sleeping off,
    which wakes every set the way anything does. The queries write nothing,
    and never come here: they're recorded apart (`b2RecQueryCommit`). A
    body's type, enabling and disabling, forces and torques, its mass and its
    other settings, a shape's settings and geometry, chains, joints, a blast,
    and whatever upstream adds: all of it is right, at the
    cost of one whole image. `b2MergeSolverSets`, `b2TransferBody`,
    `b2TransferJoint` and `b2DestroyJointInternal`, which a call that's
    followed could come to (a body that's destroyed takes its joints), say so
    themselves, as does a restore.
- **What isn't followed, on purpose: the trees.** A step's workers write a
  tree's nodes for every shape that leaves its fat bounds
  (`b2DynamicTree_MarkProxyMoved`), the next step builds what moved again,
  and nothing says which nodes. So the three trees are written every time, as
  parts that point at them: nothing is copied, and whoever reads the parts
  compares them. With 2,400 crates that's 250 KB compared for each image.

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
  `b2Body_WakeTouching`). All three are calls an image in parts follows.
- **There's no call to wake every body** but turning sleeping off, which the
  package does, and on again, when gravity changes (`b2World_EnableSleeping`).
  A world's gravity is in its image, which is how the package tells a change.
  Everything is awake after it, so the next image is written whole.
- **A body that goes wakes what it touched** (`b2DestroyContact`), and so does
  a shape. Waking is followed by an image in parts, but a stack that wakes
  can make the awake set's part of the image outgrow its place, and then the
  parts after it move and that image is written whole.
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
  package wakes what touches it first (`b2Body_WakeTouching`). It moves the
  body from one solver set to another (`b2TransferBody`), which an image in
  parts doesn't follow: the image after it is written whole.
- **`b2DestroyShape` with its last argument true** updates the body's mass,
  which is what the package wants when a part goes. An image in parts follows
  it, the mass too (`b2DestroyShapeInternal`, `b2UpdateBodyMassData`).
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

Then `tide_box2d_parts`, for the image in parts. Whatever upstream added that
writes a body, a shape or a contact of a body that isn't awake, or anything in
a solver set but the awake one, or an island, has to say so (`b2SnapTouchBody`
and the like), or be a call the next image is written whole after, which
every recorded call is until `b2SnapNoteOp` names it. A call that changes what
an image holds and isn't recorded at all would be missed: when this was
written every one of them was recorded (the others set callbacks and user
data, which an image doesn't hold). The test does what a game might to a world
at random and compares the two images after every step, and it says where the
first byte apart is.

Taking each of the lines that say something changed out in turn and running
that test says which of them it would notice the lack of: 57 of the 93 by
themselves, when this was written. 14 more were noticed with another line
taken out too, which says the same thing: the two times everything awake goes
into the sets, waking with the second of them and falling asleep with the
first, a new body's bit with the bits of what its last image had no room for,
and the pairs of places an island or a set is said to be written from (a
body's island made and the body put in it, a set's island taken out and its
body). 9 say again what being awake says, of a contact that starts or stops
touching or moves within the awake set or the graph, and weren't noticed all
taken out together. The other 13 weren't tried together, or are under calls
that write the next image whole anyway (joints, `b2MergeSolverSets`,
`b2TransferBody`).
