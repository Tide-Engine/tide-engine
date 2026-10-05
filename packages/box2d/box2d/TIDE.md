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

## Updating it

Copy the two folders over, apply the changes again, and run `tide_box2d_stack`
natively and for the web: its pinned hashes are of the simulation and of every
image, so a field that isn't the same everywhere shows there. A new version
steps differently, so the hashes change with it, and so do games' matches.
