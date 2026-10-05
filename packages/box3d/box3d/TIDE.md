# Box3D, as this package has it

`src/` is Box3D's `src/` and, in `src/box3d/`, its `include/box3d/`, from
https://github.com/erincatto/box3d at commit
`e77352cd606dc1a34209094076199549a52ea0a1` (3 October 2026, version 0.1, an
alpha), under its MIT license (`LICENSE`). A game that ships with this package
ships Box3D, and its license asks for the copyright notice to go with it.

The headers are beside the sources so that `#include "box3d/box3d.h"` finds
them with no include path, which a game's C has none of.

## What's changed

Each change is marked `Tide:` where it is. They're all for one thing: a world's
image is kept in the match, where it's hashed and sent between players, so it
has to be the same bytes on every machine for the same state, and one
machine's has to load on another.

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

  It's beside the image and not in it, as a second list in the match
  (`Physics.geometry`), because it's the one part of a world's state that
  almost never changes: in the image, every hull would be written, compared
  and kept again every tick, and a mesh is megabytes. Beside it, a tick's
  image is as small as it was and the list is written when a shape of new
  geometry is made, and then only at its end: it's only ever added to, so no
  byte of it ever moves.

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
  The Neon change compiles (Android, arm64) and hasn't run: the test's pins
  are x64's and the web's until an ARM machine runs them.

### Threads where upstream knows none

- **`timer.c`: a lock that locks, on a platform the file doesn't know.**
  Tide's web target is WASI with threads, and upstream only knows Emscripten,
  so its lock there did nothing. The narrow phase's workers take manifolds
  from one allocator under that lock, so two of them took the same one, and a
  world stepped on threads was another than on one, from the test's tick 7.
  The lock spins, as a web page's main thread may not wait, and it guards a
  few instructions.

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
  without the table reads as before. `bridge.c` keeps each section where it
  was while that leaves it between none and a quarter of the one before it to
  spare.
- **`world_snapshot.c`: every shape, and every contact, takes the same room.**
  Upstream writes each shape's materials and geometry after it, and each
  contact's manifolds after it, so one that changes (a contact that starts
  touching gains a manifold) moves every one after it. Here the shapes are
  written together, then the material arrays of the few that own one; and the
  contacts together, then a manifold for each (zeros for one that has none),
  then what only mesh contacts have (their other manifolds and their triangle
  caches).

## What isn't changed

- On the web Box3D takes its scalar path, as it only knows Emscripten's
  WebAssembly: a step there takes about three times a desktop's.
- The scalar and SSE2 code differ in a zero's sign in two more places
  (`b3AbsW4` of -0, and `b3NegV`), whose results only feed comparisons. A new
  use that keeps one would show in the test.
- Joints, sensors, meshes and height fields go into an image as upstream
  writes them, with the changes above. The package's tests don't reach them;
  a check outside the tests (a mesh, a height field, a sensor and a joint,
  restored before every step, on x64 and for the web) had the same bytes
  everywhere.

## Updating it

Copy the two folders over, apply the changes again, and run `tide_box3d_stack`
natively and for the web: its pinned hashes are of the simulation and of every
image, so a field that isn't the same everywhere shows there. A new version
steps differently, so the hashes change with it, and so do games' matches.

When two builds disagree, print the world's hash after every tick on both to
find the first tick that differs, and there the bytes of `Physics.image` on
both. The table after the image's header (16 bytes in, 17 offsets) says which
section a byte is in, and each section starts with a count, so a byte's place
says which body, shape or contact it's in and which field. That's how the two
changes above that weren't for layout were found.
