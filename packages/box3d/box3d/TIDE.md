# Box3D, as this package has it

`src/` is Box3D's `src/` and, in `src/box3d/`, its `include/box3d/`, from
https://github.com/erincatto/box3d at commit
`e77352cd606dc1a34209094076199549a52ea0a1` (3 October 2026, version 0.1, an
alpha), under its MIT license (`LICENSE`). A game that ships with this package
ships Box3D, and its license asks for the copyright notice to go with it.

The headers are beside the sources so that `#include "box3d/box3d.h"` finds
them with no include path, which a game's C has none of.

## What's changed

Hull limits (2026-10-25): `b3CreateHull` (`hull.c`) builds a hull again with a
smaller vertex budget when the one it built has more faces or half edges than
`B3_MAX_HULL_FACES` and `2 * B3_MAX_HULL_EDGES` allow, scaled down by how far
over it was, until it fits or would have fewer than four vertices. Upstream
logged and returned nothing: the vertex budget alone (128) doesn't keep a
hull's edges within their 128, and a smooth hull reaches that at about 45
vertices, so a pin turned on a lathe of 121 points made no shape. A hull that
fits the first time is built exactly as before. The builder adds the furthest
remaining point first, so a smaller budget keeps the best hull of that many.

Package completion (2026-10-22):

- `world_snapshot.c`/`box3d.h`: `b3World_InternHeightField` retains and
  deduplicates cooked height-field bytes in the canonical geometry store,
  with the same ownership and conservative resident marks as mesh interning.
- `b3World_Digest` includes persistent joint warm-start impulses, visited in
  native-id order, including sleeping joints. It excludes derived solver
  frames, masses and indices rebuilt after canonical restore. The no-joint
  digest is unchanged.
- `body.c`: an unsupported type change on a compound or height-field body
  clears `world->locked` before returning. The package keeps mesh/height
  bodies static while those colliders remain attached.

The package bridge also marks the arena whole before temporary canonical
packing. Geometry interning can modify an existing geometry directory, not
only new allocations; restoring the resident afterward must discard all
those temporary writes. Repeated full/delta packing of worlds containing
several hulls and terrain now preserves the live geometry directory.

Static mesh package integration (2026-10-15): `b3World_InternMesh` in
`world_snapshot.c`/`box3d.h` retains and deduplicates a cooked mesh in the
world's canonical geometry store. The package frees its temporary cook after
interning and gives shapes the retained pointer. Lookup compares the hash
and bytes through the existing buckets, without caching a temporary source
address that can be reused. Interning marks the world conservatively for
resident commits. The store owns its data through restore/destruction;
geometry no live shape uses is retained, as box hulls already are.

Each change is marked `Tide:` where it is. A match keeps Box3D's state through
resident pages for local snapshots and rollback, and sends a canonical image
and its geometry between players. The state a step computes and the image it
sends have to be the same on every supported platform, and one machine's
image has to load on another. Other changes reduce a step's cost in a browser
(WebAssembly's SIMD and workers that sleep), or fix a body put to sleep in a
world that was just restored.

### Resident state

The package's `Physics.state` is a `Resident` (`tide/resident.h`). After a
step it holds the arena pages Box3D wrote, with a stamp and a digest. A local
snapshot shares its page table; committing copies the pages marked as
possibly written and keeps the other pages shared. Before another tick or a
question reads a different snapshot, the arena restores the pages whose
identities differ. The stamp alone isn't used to decide whether a restore is
needed: two runs of one tick can have different local pages.

- **`core.h`, `core.c`: state and scratch have different allocators.**
  `b3Alloc` and `b3Free` use the allocator given to `b3SetAllocator`. The
  bridge supplies `tide_arena_alloc` and `tide_arena_free`: the arena keeps
  the allocator's own state and all the allocations a restored world needs,
  including geometry. New allocations are marked whole. The bridge marks a
  released block whole before freeing it, as it may have been written before
  it grew or was destroyed, and the later touch pass sees only its
  replacement. Restoring an older snapshot must put those old bytes back
  too. `b3FreeElement` (`block_allocator.c`) marks the free-list link it
  writes into a released manifold.

  `b3AllocScratch` and `b3FreeScratch` always use the system. The stack,
  workers' arenas and OS objects hold nothing a later step needs, and their
  memory is outside the resident arena.
- **`physics_world.c`, `box3d.h`: the world's struct is saved and relinked.**
  Box3D's world structs are in its global world slots, outside the arena.
  `b3World_StructSave` copies the active one into an arena allocation, and
  `b3World_StructLoad` puts it back after a restore. `b3World_Relink` replaces
  its scratch, callbacks and references to this build's constants. A mirror
  keeps the scratch this process has after every step, so restoring old
  pages doesn't restore old system allocations. A build taking over an arena
  makes scratch of its own and supplies its own callbacks.
- **`world_snapshot.c`: `b3World_TouchChanged` reuses the image writer's
  marks.** It marks the bodies, shapes and contacts that may have changed,
  with their materials and manifolds, and the other arrays that may have
  been written. A removed element can leave a write above its array's final
  count, so these arrays are marked to capacity. Sleeping sets and the pair
  set are marked only when their existing tracking says they were touched.
  A call whose writes aren't followed, or a world with most bodies awake,
  marks the whole arena. Destroyed shapes and contacts are marked as records
  without reading the old allocations their fields still name.

  The arena also keeps event arrays and task buffers, even where the
  canonical image omits them or a step clears them before use. Their writes
  are marked too: a local restore and the full-arena check preserve every
  byte the allocator has handed out. The system scratch above is the part
  that is relinked instead.
- **`dynamic_tree.c`: a rebuild marks its own scratch writes.** Its old node
  array is marked before it becomes the spare, as the tick may already have
  written it. Its leaf indices, leaf nodes and the active heuristic's arrays
  are marked only through the number of leaves gathered, rather than their
  capacity. The new node array becomes the current tree and is marked by
  `b3World_TouchChanged`. A rebuild that exits without doing work marks none
  of its spare or scratch. The touch hooks support calls from workers.
- **`bridge.c`: network bytes remain canonical.** The resident packer writes
  the stamp, Box3D's image with its sections placed consecutively, and the
  geometry the image names. Receiving it builds a world from those bytes and
  commits its arena into the resident. Raw arena pages and pointers stay on
  their own machine; a join or network delta uses the packer.

  The world hash includes the resident stamp and digest, without
  reading the arena pages. The stamp follows the previous stamp and the
  operations performed since it. `b3World_Digest` mixes the counts, awake
  body simulations and the contact impulses they carry into the next step.
  It is a check of that state, rather than a hash of every arena byte; other
  differences are expected to show as bodies step. Tree rebuild scratch,
  event buffers and allocation addresses aren't in that digest.

`b3t_check` compares every committed arena page with the live arena, byte for
byte, and reports the first page changed without a mark. It includes spare
and released storage, which a canonical image skips. The package's rollback,
sleep and course tests use the check; the image-in-parts tests below still
exercise the canonical writer and its tracking independently.

### Calls to take an image and to restore one

Upstream has a world's image inside its recording only (`b3SerializeWorld` and
`b3DeserializeIntoShell`, for a recording's first frame and its keyframes).

- **`world_snapshot.c`, `box3d.h`: `b3World_GetSnapshotAt`, `b3World_Restore`,
  `b3GetSnapshotBases`, `b3GetSnapshotHead`, `b3World_GetSnapshotGeometry`**,
  around upstream's two functions, which a recording still calls as it did.
  A restore is in place: the world keeps its id, its tasks and its callbacks,
  and bodies keep theirs.
- **`recording.h`, `recording.c`: an image is written straight into the
  caller's memory**, once (`b3RecBuffer.fixed`). Upstream's buffer is its own
  and grows as it goes. An image that doesn't fit is partly written; its whole
  size is still what's returned.
- **`world_snapshot.c`: a restore leaves nothing of the world it was done
  to.** The image's names replace the world's, where upstream adds them to
  what's there, and the events of the world's last step go, as they aren't in
  an image. A world restored in place would otherwise write names into its
  next image, or report touches, that a world made from nothing wouldn't.

### The geometry an image names

Upstream's image doesn't hold a shape's hull, mesh, height field or compound:
it names it by a number, and the recording the image is in keeps each once. A
match has no recording, and a player who joins has only the match's bytes.

- **`world_snapshot.c`, `physics_world.h`, `physics_world.c`: a world keeps
  the geometry its images name** (`b3SnapGeometry`, `b3World.snapGeometry`),
  each once, in the order images first named them, and
  `b3World_GetSnapshotGeometry` writes it: a kind, a size and the bytes for
  each, which is all a restore needs beside the image.

  It's beside the canonical image and not in it. Before resident state it
  was a second list in the match (`Physics.geometry`), because it almost
  never changes: writing every hull into each tick's image would keep and
  compare it again every tick, and a mesh is megabytes. Now the geometry
  lives in the resident arena and its pages stay shared until written; the
  network packer still writes it after the image. The geometry bytes are
  only ever added to, so no byte of that canonical list ever moves.

  It's the same bytes on every machine because the order is: images are taken
  of the same worlds at the same ticks everywhere, shape by shape, and a world
  made from the bytes takes its order from them. What it costs is that
  geometry no shape has any more stays.

  A hull is found again by its own hash and known by its bytes; the same bytes
  at the same place aren't compared twice, as a mesh is big and an image is
  taken every step.
- **`world_snapshot.c`: a world with a compound has no image**
  (`b3World_GetSnapshotAt` returns 0). A compound's bytes hold a tree with its
  pointers, so they're laid out otherwise where pointers are another width.
  A recording still keeps them.

### The same layout on 32-bit and 64-bit platforms

- **`body.h`, `joint.h`, `shape.h`, `contact.h`: a pointer in a struct that
  goes into an image as its bytes is 8 bytes on every platform**, a union with
  a `uint64_t`: `userData` of a body, a joint and a shape, a shape's
  `materials` and `userShape`, a contact's `manifolds`, and a mesh contact's
  triangle cache (a pointer and two counts, 16 bytes). A shape's geometry (a
  capsule, or a pointer, or a mesh's pointer and scale) is 32 bytes on every
  platform. A pointer of 4 bytes on the web made each of these structs
  another size there.
- **`world_snapshot.c`: the layout hash** is of the structs that go as their
  bytes only, and of a number for these changes, so an image of upstream's own
  making is refused. Upstream also mixes in `sizeof( void* )` and the sizes of
  the structs that hold pointers, which are written field by field, so an image
  only loaded where pointers are as wide as where it was made.

### The same bytes whatever is in memory

- **`world_snapshot.c`: zeros where a struct has a pointer, and where it has
  bytes no field owns** (`b3SnapZero`): after a body's `generation`, a joint's
  `collideConnected`, a shape's `filter` and `flags`, a contact's `friction`
  and its cache, a manifold point's `persisted`, a joint sim's `fixedRotation`,
  a hash set item's `hash`, a contact spec's `manifoldCount` and a sensor
  visitor's `generation`. What's in such bytes is whatever was in memory
  before. In the package's tests they were zeros on every build already, as
  upstream clears what it makes; the image doesn't rest on that. The offsets
  are checked when the file compiles (`_Static_assert`), on every platform.
- **`world_snapshot.c`: a shape's geometry is written in the 32 bytes it
  takes in the shape**: a sphere or a capsule as it is, or the geometry's
  number and a mesh's scale, and zeros after. Upstream writes the shape's
  bytes with its pointer in them, and the geometry after.
- **`world_snapshot.c`: a body state's two padding floats are zeros.** The
  4-wide and 8-wide solvers write zeros there and the scalar one writes
  nothing.

### The same bytes whatever the CPU

- **`simd_scalar.h`, `simd_neon.h`: the least and the greatest of two numbers
  (`b3MinW4`, `b3MaxW4`, `b3SymClampW4`, and Neon's `b3UnionAABBV`) are SSE2's
  on every path**: the first where it's less, or greater, and else the second.
  The three differed in which of 0 and -0 they gave, and in nothing else.
  Friction around a contact's normal is clamped to what the contact's force
  allows, and where that's nothing the scalar code left -0 where SSE2 and AVX2
  leave 0: the same simulation, and one byte of a manifold's `twistImpulse`
  another on the web than on x64, at the test's tick 7. Neon's own
  (`vminq_f32`, `vmaxq_f32`) take -0 for less than 0, which is a third
  answer. x64's is the one kept, as its code is the one that can't change.
  The Neon path gives the test's pins too: CI's ARM Macs run it.
- **`core.h`, `simd.h`, `simd_wasm.h`: wide floats on WebAssembly's own SIMD**
  (`B3_SIMD_WASM`), for any WebAssembly built with `-msimd128`, as Tide's web
  builds are. Upstream's one path for WebAssembly is SSE2's headers as
  Emscripten emulates them, which clang's own target has none of, so there
  Box3D took its scalar path. `simd_wasm.h` is ours, beside `simd_sse2.h` and
  `simd_neon.h`: the 4-wide floats that the contact solver and the hulls'
  separating axis test are written with (`wide4.c`), and the trees' bounds
  (`b3AABBV`). `core.h` has one branch for WebAssembly, Emscripten's too,
  where this stands in for the emulation; built without `-msimd128` it's the
  scalar path, as before.

  Each function gives, bit for bit, what `simd_sse2.h`'s gives for the same
  numbers, so a browser steps the world x64 does:
  - The least and the greatest are `f32x4.pmin` and `f32x4.pmax` with their
    operands the other way around, which are `minps` and `maxps` exactly: the
    first where it's less, or greater, and else the second, whichever zero
    that is and whatever is NaN. WebAssembly's `f32x4.min` and `f32x4.max`
    take -0 for less than 0, as Neon's do, and would bring back the zeros of
    the change above.
  - Comparisons are the ordered ones, false for a NaN. A blend takes each bit
    by its mask's bit. `b3NegW4` and `b3AbsW4` change the sign's bit and
    nothing else. `b3AnyTrueW4` and `b3AllZeroW4` go by each lane's top bit,
    as `movmskps` does.
  - Sums, products, quotients and square roots are IEEE 754's on both, each
    rounded by itself: nothing here is relaxed SIMD, whose fused multiply-add
    differs between machines, and Tide's flags fuse nothing.
  - Loads, stores, transposes and the bounds' shuffles only move lanes.

  A single 3D vector (`b3V32`, which SSE2 keeps in a register: ray casts, and
  the triangles of meshes and height fields) is still plain code there, as it
  is with Neon, and `simd.c` is as it was.

  The package's pins are what they were, in debug and release web builds.
  Beside them, a check outside the tests ran every function of the header on
  awkward numbers (every three of 24 values with both zeros, denormals,
  infinities and NaNs among them, 40,000 of any bits at all, and 40,000 small
  ones of which many are equal), built for x64 and for the web: the same bits
  from both for all 27 kinds of result, where the scalar path differs in 11.
  Which NaN arithmetic gives of two isn't compared: that's the CPU's, and no
  NaN is ever in a world.

  It took a quarter off a step in a browser. The package's `README.md` has the
  numbers, and why it's no more: clang had already made SIMD of most of the
  scalar path by itself.

### Threads where upstream knows none

- **`timer.c`: a lock that locks, on a platform the file doesn't know.**
  Tide's web target is WASI with threads, and upstream only knows Emscripten,
  so its lock there did nothing. The narrow phase's workers take manifolds
  from one allocator under that lock, so two of them took the same one, and a
  world stepped on threads was another than on one, from the test's tick 7.
  The lock spins, as a web page's main thread may not wait, and it guards a
  few instructions.
- **`solver.c`: a worker that has waited long for the next stage sleeps**
  (`b3SolverTask`, `B3_TIDE_WAIT_ROUNDS`), on the web only. A step's workers
  wait for each stage in a loop, with the CPU's pause in it and, every few
  rounds, the system's yield. WebAssembly has no pause and a page no yield, so
  there the loop holds a processor for as long as it waits, and with a worker
  on every one a page has, the system takes a processor from a worker that has
  a block to run whatever else the machine has: the stage then waits a time
  slice for that block. After 4096 rounds of the wait, which is about what
  waking a worker takes, so that the waits between stages go as they did, a
  worker sleeps until the main thread gives the next stage (`b3t_stage_sleep`,
  and `b3t_stage_wake` after each change of `atomicSyncBits`, both the
  bridge's, which has them from `tide/jobs.h`). On a thread that can't sleep,
  a page's own, it goes on with the loop. A world steps the same either way,
  as it does with any number of workers.

### What a match keeps of an image from tick to tick

- **`world_snapshot.c`, `recording.h`, `box3d.h`: an image in sections that
  keep their place** (`b3World_GetSnapshotAt`, `b3GetSnapshotBases`,
  `b3GetSnapshotHead`, `B3_SNAPSHOT_SECTIONS`). Upstream's image is one run of
  bytes that starts with its parts of changing size (the id pools, then the
  solver sets, the awake one before the sleeping ones), so a contact that
  starts or ends moves every byte after it. Here an image can have a table
  after its header of where each of 17 sections starts, which the caller
  gives: the writer goes to each one's place (`b3SnapSection`), over zeros,
  and the reader goes where the table says (`b3SnapSeek`). A section that runs
  into the next one's place is reported, with every section's size, and the
  caller places them again. The sections are in upstream's order, and an image
  without the table reads as before. The bridge's earlier image-based state
  kept each section where it was while that left it between none and a
  quarter of the one before it to spare. The resident packer uses the same
  format with consecutive sections, as it writes a whole image for transfer.
- **`world_snapshot.c`: every shape, and every contact, takes the same room.**
  Upstream writes each shape's materials and geometry after it, and each
  contact's manifolds after it, so one that changes (a contact that starts
  touching gains a manifold) moves every one after it. Here the shapes are
  written together, then the material arrays of the few that own one; and the
  contacts together, then a manifold for each (zeros for one that has none),
  then what only mesh contacts have (their other manifolds and their triangle
  caches).

### A body put to sleep in a world that was just made from an image

- **`solver_set.c`: `b3TrySleepIsland` only marks a body's move event where
  the world still has it.** A body that's awake after a step says which of
  the step's move events was its own (`b3Body.bodyMoveIndex`), which is in the
  image, and the events aren't: a restore clears them. Upstream looks the
  event up regardless, to say the body fell asleep, so `b3Body_SetAwake(
  false )` before a restored world's first step wrote past the events: an
  assert in a debug build (seen in `tide_box3d_parts`, which didn't put a
  body to sleep right after a restore for it, and does now), and otherwise a
  byte written wherever that was. The package puts bodies to sleep
  (`Body.asleep`), and a client that runs a tick again does it in a world
  that was just made. The body says it has no event from there on either
  way, so its bytes in the next image are the same. `tide_box3d_sleep` restores
  its world before every tick of a match that puts bodies to sleep.

### An image as what changed of the last one

Before resident state, the package took an image after every step. Most steps
change little of a world: a few bodies are awake. Writing all of it, and
comparing all of it with the last one, goes by how big the world is. These
APIs let a world say what changed instead, and the image the parts make is,
byte for byte, the image written whole. `tide_box3d_parts` compares the two
after every step. The resident touch pass uses the same marks, while the
package's ticks keep pages instead of writing images.

- **`world_snapshot.c`, `box3d.h`: `b3World_GetSnapshotParts`,
  `b3SnapshotPart`, `b3World_SetSnapshotAwakeShare`,
  `b3World_CheckSnapshotAt`.** The parts to write over the world's last image,
  which the caller has: the world keeps no copy. It says there's no telling
  (the caller takes the image whole) for a world with no image yet, one with
  more bodies, shapes or contacts than its last image had room for, one that
  had something done to it that isn't followed (below), and one with more
  than half of its bodies awake, where writing it whole costs no more. The
  last image is the one `b3World_GetSnapshotAt` or this last gave, whole and
  at the same bases, or the one the world was restored from. `b3World_CheckSnapshotAt` is the
  image written whole that isn't taken for the last one, for a test to compare
  the parts with; `b3World_SetSnapshotAwakeShare` has a test's world say what
  changed whatever is awake. `physics_world.c` frees what follows the images
  with the world (`b3DestroySnapshotTrack`).
- **`recording.h`, `recording.c`, `world_snapshot.c`: the writer is the one
  that writes an image whole** (`b3RecBuffer.parts`, `b3SnapParts`). What it
  appends goes into memory of the world's own as parts that each say where in
  the image they are, and what it leaves out is what the last image has
  already. So there's one piece of code that says what a body's bytes are in
  an image, for both:
  - Bodies, shapes and contacts each take the same room, so one is written at
    its own place: `b3SerBodyRun`, `b3SerShapeRun`, `b3SerContactRun` and
    `b3SerContactManifoldRun` write a run of them, which is all of them for an
    image written whole and each stretch that may have changed for one in
    parts. What's written over a struct's bytes after they're appended (zeros
    for its pointers and holes, a shape's geometry) goes through `b3SnapAt`,
    which finds them in the image or in the part.
  - The other sections are written whole. What goes into an image as it is in
    the world (a solver set's bodies, a tree's nodes, the fat bounds) isn't
    copied: the part points at it (`b3SnapW_Ref`), and whoever reads the parts
    compares it where it is. So the parts are good until the world is next
    stepped or changed.
  - A section that's smaller than it was has zeros after it, where the last
    image had its end (`b3SnapSection`).
  - The sleeping sets and the pairs are left out when nothing was done to them
    since the last image (`b3SnapKeepSection`): nothing writes a sleeping set
    but what says so, and a pair comes and goes with its contact.
  - A world with no mesh, height field or compound, and no shape with a
    material array of its own, isn't searched for the contacts and shapes that
    have more in an image than their room (`b3SnapTrack.oddShapeCount`): making
    one writes the next image whole, which counts them.
- **`recording.c`: `b3RecBufReserve`**, which `b3RecBufAppend` now is, with a
  copy after: where the next bytes go, for a caller that writes them itself.
  **`world_snapshot.c`: `b3SerIslands`** asks once for all the islands and
  writes them there, where upstream appends each number of each island: the
  same bytes, and a tenth of the time, in every image. There are as many
  islands as stacks, and they were most of what an image in parts cost.
- **`physics_world.h`: `b3SnapTrack`, which bodies, shapes and contacts may
  have changed since the last image**, a bit each. Between two images, one has
  changed only if
  - its body (or one of a contact's two) was awake at the first image, at the
    second, or between: the step writes those on its worker threads, and
    nothing says which. So everything awake goes into the sets when an image
    is taken, for the next one (`b3SnapCommit`), and again when that one is
    (`b3SnapSeed`): a body, its shapes and the contacts in its list;
  - or it was written from outside the step's workers, on one thread, which is
    said where it's done. Each of these is marked `Tide:` and names
    `b3SnapTrack`:
    - `contact.c`, `b3CreateContact` and `b3DestroyContact`: the contact, both
      its bodies (their lists' heads and counts), and its neighbors in both
      lists, whose links change. Any of them may be asleep or static: the floor
      is in a list with every crate on it.
    - An element taken out of an array moves the last one into its place, and
      the moved one's index is written: a contact of a solver set
      (`b3DestroyContact`, `b3WakeSolverSet`, `b3TrySleepIsland`,
      `b3RemoveNonTouchingContact`), of a graph color
      (`b3RemoveContactFromGraph`, `b3TrySleepIsland`) or of an island
      (`b3UnlinkContact`); a body of a solver set (`b3DestroyBody`,
      `b3TrySleepIsland`) or of an island (`b3RemoveBodyFromIsland`); a
      sensor's shape (`b3DestroySensor`). The one from the disabled set is any
      contact between two sleeping bodies, anywhere in the world.
    - `body.c`, `b3RefreshBodyContactIndices`: every contact of a body that
      moved in its set, which may be the static one.
    - `solver_set.c`, `b3WakeSolverSet` and `b3TrySleepIsland`: each body that
      wakes or falls asleep, with its shapes and contacts, so that one that's
      awake between two images and at neither is written.
    - `body.c`: `b3CreateBody`, `b3DestroyBody`, `b3Body_SetTransform` (which
      doesn't wake) and `b3UpdateBodyMassData`. `shape.c`,
      `b3CreateShapeInternal`: the shape, its body, and the shape that headed
      the body's list.
    - `shape.c`, `b3DestroyShape`: the shape, its neighbors in its body's
      list, whose links change, and the body, which has the list's head and
      its count. The contacts that go with it say so themselves
      (`b3DestroyContact`), and so does the body's mass. A sensor that goes
      moves the last one into its place, whose shape is told where it is now
      (`b3DestroyShapeInternal`, as `b3DestroySensor` already said for a
      sensor that goes with its body).
    - `body.c`, `b3Body_SetAwake`, either way: nothing of its own to say.
      `b3WakeSolverSet` and `b3TrySleepIsland` say what they write, as they do
      when the step calls them, and the island that's split before it's put
      to sleep (`b3SplitIsland`) is of bodies that are awake, which are
      written anyway, with their contacts.
    - `body.c`, `b3Body_SetLinearDamping`, `b3Body_SetAngularDamping` and
      `b3Body_SetGravityScale`: what the body's set has of it, which matters
      for a sleeping one (the static set and the awake one are written every
      time). `b3Body_SetBullet`: the body's own flags; its set's copy of them
      is said in `b3SyncBodyFlags`. None of the four wakes a body.
  - **Any other call writes the next image whole.** `recording.h`: `B3_REC`
    and `B3_REC_CREATE`, which every call that changes a world goes through,
    say which call it is (`b3SnapNoteOp`), and only the ones looked at for the
    list above are followed: the step, making and destroying a body, its
    transform, its speeds, forces and impulses, waking it and putting it to
    sleep, its dampings, its gravity scale and whether it's a bullet, making a
    sphere, a capsule or a hull, and destroying a shape. So are the world's settings that are numbers of its
    own, which every image writes: its gravity, its contact tuning, and its
    restitution and hit event thresholds. A body's type, enabling and
    disabling, whether a body may sleep and how readily
    (`b3Body_EnableSleep`, `b3Body_SetSleepThreshold`), the world's
    `b3World_EnableSleeping`, mass, shapes' settings, joints, and whatever
    upstream adds: all of it is right, at the cost of one whole image. The calls recorded by hand say so
    themselves (`b3Shape_SetHull`, `b3Shape_SetMesh`), as do
    `b3MergeSolverSets`, `b3TransferBody`, the joints' making and destroying,
    a shape with a mesh, a height field or a material array (made, or
    destroyed).
  - **A restore is followed** (`b3SnapFollow`, from `b3World_Restore`). A
    world that was just made from an image would write that image, byte for
    byte, were it asked for one: it's what the pins of a world restored before
    every tick rest on. So that image is the world's last one, at the bases its
    table says, and the next is written in parts that go on from it: what the
    sets hold is what was awake in it and what's said after. The writer runs
    once with nothing to write into, for where each section ends and how many
    shapes have more than their room, which the image doesn't say. A world
    restored from an image with more than half of its bodies awake writes its
    next one whole, as any such world does. The other readers of an image
    (the recording's replay, through `b3DeserializeIntoShell`) leave the
    world with no last image, as before.

    `b3Body_SetType` is left at a whole image on purpose. It moves the body
    to another solver set (`b3TransferBody`, which moves another body into its
    place there), lets go of every contact it has, takes it out of its island
    or makes one, and makes each of its shapes' proxies again in another
    tree: following it would be following all of those, for a call a game
    makes now and then. What it costs is that tick's image: with 2,400 bodies,
    0.3 ms written whole against 0.04 in parts.

## What isn't changed

- The scalar and SSE2 code differ in a zero's sign in two more places
  (`b3AbsW4` of -0, and `b3NegV`), whose results only feed comparisons. A new
  use that keeps one would show in the test. The first is the scalar wide
  floats', which nothing Tide builds for takes now that the web has its own;
  the second is a single vector's, which is plain code on ARM64 and on the web.
- On the web a step still takes longer than a desktop's: WebAssembly's SIMD is
  4 wide where x64 solves 8 contacts at a time with AVX2, and a browser's
  compiler isn't clang.
- Joints, sensors, meshes and height fields go into an image as upstream
  writes them, with the changes above. Of those the package makes sensors, and
  `tide_box3d_course` pins its match of them, in debug, in release and on the
  web, with every image in parts compared with the image written whole. The
  others it doesn't make; `tide_box3d_parts` makes them all in Box3D by
  itself, to compare an image in parts with the image written whole, which
  says nothing of other machines. A check outside the tests (a mesh, a height
  field, a sensor and a joint, restored before every step, on x64 and for the
  web) had the same bytes everywhere.
- `b3Body_SetType` returns with the world still locked for a body with a
  height field or a compound that's made other than static: upstream's. The
  package sets a body's type, and makes neither of those shapes.

## What the package asks of it

Nothing here is changed for these, but the package rests on them, and a new
Box3D has to be read for them again:

- **A shape is only reported to a sensor when both have `enableSensorEvents`**,
  which is off unless a shape is made with it, for a sensor too. The package
  makes every shape with it, so that a sensor sends `Touched` and `Parted`.
  It's a bit of each shape's `flags` in the image. What a sensor does in a
  step is written into its own arrays, which are a section an image in parts
  writes whole every time, and into no body, shape or contact.
- **Sensors see each other** (`b3SensorQueryCallback` has no check for it), so
  the package makes its sensors in one collision group below zero
  (`filter.groupIndex`), which Box3D never sets against each other: two that
  overlap would each report the other, and their entities hear of it twice.
- **What a sensor has in it is in the image** (its three arrays of visitors),
  and the events of a step aren't: a world made from an image has the same
  begins and ends to say after its next step as the world the image was taken
  of. `tide_box3d_course` restores its world before every tick to see that.
- **`b3Body_SetTransform` doesn't wake a body, or what rests on it.** The
  package wakes both when code moves a body, with `b3Body_SetAwake`, for the
  ones that are asleep (`b3Body_GetContactData` says what touches a body;
  there's no `b3Body_WakeTouching`). It's a call an image in parts follows.
- **Bodies sleep and wake an island at a time** (`b3TrySleepIsland`,
  `b3WakeSolverSet`): `b3Body_SetAwake( false )` puts to sleep every body of
  the island the body is in, whatever its speed, and `true` wakes all of the
  set it fell asleep with. An island is the bodies that touch each other, as
  the last step found them: every touching contact between two bodies of
  which neither is static joins their islands (`b3LinkContact`), a kinematic
  body's too, so a lift is in an island with what stands on it, and a floor
  that's static ties nothing together. A body that's put to sleep has no
  speed any more (its `b3BodyState` goes), and Box3D says none of which
  bodies went with it: the package asks, of each that was awake
  (`b3t_leave`), in the ticks whose step put a body to sleep for code.
- **A body that's made asleep** (`b3BodyDef.isAwake`) is alone in a set of its
  own, and a shape put on it leaves it asleep. Its contacts are made at the
  next step and aren't looked at until one of their bodies is awake, so
  bodies made asleep on each other wake one after another, a contact a step
  (`tide_box3d_sleep` has three do that). Each is also a sleeping set and an
  island of its own for as long as it sleeps, and those are sections an image
  writes whole whenever one of them changes: with thousands, a tick in which
  many wake costs more than it does where they sleep a stack to a set (Sumo
  measured both, and lets its ring have one step before it puts it to sleep).
- **A speed that isn't zero wakes a body** (`b3Body_SetLinearVelocity`,
  `b3Body_SetAngularVelocity`), and so does an impulse; the package puts
  bodies to sleep after everything else a tick wrote, right before the step.
- **Nothing checks whether the world's bodies may sleep when one is put to
  sleep** (`b3World_EnableSleeping` only says what the step does, and wakes
  every body as it's turned off): a body can be put to sleep in a world whose
  bodies don't fall asleep by themselves, and stays so until something wakes
  it. `tide_box3d_sleep` does that.
- **There's no call to wake every body** but turning sleeping off, which the
  package does, and on again, when gravity changes (`b3World_EnableSleeping`):
  not followed either, and everything is awake after it, so the next image is
  written whole anyway. A world's gravity is in its image, which is how the
  package tells a change.
- **A body made with no shape weighs nothing**, so an impulse does nothing to
  it: the package gives a new body's impulses once its shapes are on it, with
  the calls it gives any impulse with, which are followed.
- **Two shapes that meet hard are only reported when one of them has
  `enableHitEvents`**, which is off unless a shape is made with it. The
  package makes every shape with it, for `Hit`. That's in the image three
  times over: a bit of each shape's `flags` (`b3_enableHitEvents`), a bit of
  each contact's (`b3_simEnableHitEvent`), and, in each point of a contact
  that touches, how fast the two closed on each other as the step began
  (`b3ManifoldPoint.normalVelocity`, which is 0 for a contact nobody asked
  about). In the test's last image that's 24 shapes, 28 of 31 contacts and 58
  points: 280 bytes of 40,812. The speed is worked out where the solver
  prepares a contact, a lane of 4 or 8 at a time or by itself, and written
  for each contact that asks and for no other (`contact_solver_wide.inl`), so
  it's the same bytes whatever the CPU solves at a time: the test's two runs
  and a web build agree. Nothing reads it but the events, so the bodies step
  as they did: `tide_box3d_stack`'s pin of them didn't move.
- **Hit events are a step's own**, as its touches are: found after the solve,
  in the order of the contacts' ids, one for each contact whose fastest point
  was over the world's threshold, and gone at the next step.
- **`b3Body_SetType` lets go of everything the body touches**, with an end
  event for each, and finds what still touches it in the next step, with a
  begin event. The package says neither for a pair that has both in a tick
  that changed a type (`b3t_touches`).
- **A shape that's destroyed takes its contacts with it and wakes what it
  touched** (`b3DestroyShape`), with an end event for each that touched, and
  for each shape in it when it's a sensor. Those events name a shape that's
  no more: the package keeps whose each one was for the tick
  (`b3t_gone_shapes`).
- **The world's contact tuning has no call to read it back.** The package
  sets it, with the world's thresholds, before every step: they're numbers of
  the world's own, and the calls are followed.
- **`b3World_EnableSleeping( false )` wakes every body**, and turning it on
  again only says so: the package turns it off for a gravity that changed (to
  wake everything) and for a match whose bodies aren't to sleep.
- **Queries write nothing**, and the image in parts says so by name
  (`b3SnapNoteOp`): a world asked a thousand things has the image it would
  have had. They run on whichever thread asks, several at once: they only
  read the world.
- **Nothing in a query passes a sensor by.** `b3World_CastRay`,
  `b3World_CastShape` and `b3World_OverlapShape` go by a shape's category and
  mask alone, so the package's callbacks pass sensors by themselves
  (`b3Shape_IsSensor`).
- **What a shape says of a cast doesn't go by how far the search is cut
  off**, which is what lets the package's nearest hit not depend on the order
  Box3D comes to shapes in. `b3RayCastHull` only compares with the cut-off;
  `b3RayCastSphere` and `b3RayCastCapsule` work their fraction out before
  they compare it, and then hold it to the cut-off, which only changes a
  fraction that's past it; `b3ShapeCast` advances from 0 and stops at it.
  The package cuts the search off a ten-thousandth past the nearest hit so
  far, never at it, so that a shape exactly as near is reported whichever
  comes first and isn't held to anything. A new Box3D has to be read for this
  again: a cast whose answer went by the cut-off would give another answer
  on another tree.
- **A shape's own answer is plain floats.** The casts and the overlap test
  work in the shape's frame with `b3Vec3` and `b3Quat`, which are C, and a
  hull's support point with the 4-wide type, which gives the same on every
  path (above). The trees are only asked which shapes could be it, with
  bounds that are fatter than the shapes by more than any rounding, so
  whether a tree's own tests round one way or another on some CPU can't
  leave out a shape that's hit.
- **`b3ShapeCast` says a shape that starts in another is hit at 0, with no
  normal**: within a few millimeters counts (`B3_LINEAR_SLOP`), so a shape
  cast from where a body rests is in what it rests on. A ray says nothing of
  what it starts inside (the package passes a fraction of 0 by, as
  `b3World_CastRayClosest` does).
- **What a sensor has in it is `b3Shape_GetSensorData`'s**: its visitors as
  the last step found them, which are in the image.

## Updating it

Copy the two folders over, apply the changes again, and run `tide_box3d_stack`
and `tide_box3d_course` natively, in debug and release, and for the web: their
pinned hashes cover the simulation and the per-step resident digests. The
full-arena check also finds writes the resident marks missed. A new version
steps differently, so the hashes change with it, and so do games' matches.
`tide_box3d_course` also pins
what every question was answered (`ANSWERS_HASH`): a cast or an overlap test
that rounds another way somewhere shows there. And the casts have to be read
again for whether their answers go by how far the search is cut off (What the
package asks of it).

`simd_wasm.h` follows `simd_sse2.h`. A function upstream adds to that file's
wide floats or bounds needs its twin, or the web build doesn't compile; one it
changes has to change there too, which nothing says but the pins, so the two
files are read side by side at each update.

Then `tide_box3d_parts`, for the image in parts. Whatever upstream added that
writes a body, a shape or a contact of a body that isn't awake has to say so
(`b3SnapTouchBody` and the like), or be a call the next image is written whole
after, which every recorded call is until `b3SnapNoteOp` names it. The test
does what a game might to a world at random and compares the two images after
every step, and it says where the first byte apart is. Taking each of those
lines out in turn and running it says which of them the test would notice the
lack of: 36 of the 60 by themselves when this was written, and 9 of the 10
that came with a shape destroyed and a body's settings (the tenth has a
destroyed shape with a material array of its own write the next image whole,
which the writer finds by itself: it looks for such shapes while the world has
had any). The other 24 are
second sayings of what another line says, or of what being awake says: a body
that wakes is also awake at the next image, an element moved within the awake
set is an awake body's, a sleeping set that loses a contact is destroyed when
it wakes right after. Four such pairs were taken out together, and noticed:
the two times everything awake goes into the sets, waking with the second of
them, falling asleep with the first, and the two places a sleeping body's mass
says its set changed. The rest weren't tried together.

When two builds disagree, print the world's hash after every tick on both to
find the first tick that differs, and compare the canonical images of their
resident states. The table after the image's header (16 bytes in, 17 offsets)
says which section a byte is in, and each section starts with a count, so a byte's place
says which body, shape or contact it's in and which field. That's how the two
changes above that weren't for layout were found.
