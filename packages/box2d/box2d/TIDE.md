# Box2D, as this package has it

`src/` is Box2D's `src/` and, in `src/box2d/`, its `include/box2d/`, from
https://github.com/erincatto/box2d at commit
`ac7c751eaeddbabdc1c4d41ae4f3a25d78627790` (4 October 2026, after v3.1.1), under
its MIT license (`LICENSE`). A game that ships with this package ships Box2D,
and its license asks for the copyright notice to go with it.

The headers are beside the sources so that `#include "box2d/box2d.h"` finds
them with no include path, which a game's C has none of.

## What's changed

Each change is marked `Tide:` where it is. They're all for one thing: a world's
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

## Updating it

Copy the two folders over, apply the changes again, and run `tide_box2d_stack`
natively and for the web: its pinned hashes are of the simulation and of every
image, so a field that isn't the same everywhere shows there. A new version
steps differently, so the hashes change with it, and so do games' matches.
