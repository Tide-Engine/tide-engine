---
description: "Use conservative CPU box occlusion queries to drive game-owned per-player visibility, with cached candidates and native/WebAssembly support."
---

# Corner Culling

`cornerculling` is an optional CPU visibility package in namespace `CornerCulling`.
It checks whether one opaque, axis-aligned box completely blocks the view between
observer and target bounds. It works without a renderer, physics integration,
GPU or worker threads, on native and WebAssembly builds.

## Add and query

```sh
tide add cornerculling
```

Keep a `CullingWorld` in your game's state, build it from wall boxes, and retain
one `CullingCache` per directed observer/target pair. This example's `Main` is
the game's existing main scene:

<<< @/../packages/cornerculling/test/example.tide

`Bounds.extents` is a **half size**, not a full size. A point observer has zero
extents. The target bounds should include the body and anything visible with it.
Only author boxes wholly inside opaque geometry; an enclosing mesh AABB can
cover empty space and incorrectly hide a visible player.

## Results and updates

| API | Behavior |
| --- | --- |
| `world.Rebuild(boxes)` | Snapshot the input list and rebuild its spatial tree |
| `world.Query(query, cache)` | Return a result and update a candidate cache |
| `world.Query(query)` | Query without retaining a cache |
| `result.IsVisible()` | True for `Visible` and `Unknown`; false only for `Occluded` |
| `result.occluder` | Input-list index of a certified blocker, or `-1` |
| `world.Update(index, box)` | Replace an existing source index and immediately refit; false for an invalid index/unbuilt world |
| `world.Clear()` | Empty contents, retaining capacity; queries become `Unknown` until rebuilt |

`Box.mask` and `CullingQuery.mask` are integer bitmasks, both defaulting to `-1`
(all bits). A box participates if their intersection is nonzero. Set a box's
mask to zero to disable it. Invalid geometry also cannot block. Source indices
remain stable until the next rebuild; adding entries requires rebuilding.
Assign `default` to the world to release its owned lists.

`CullingQuery.surfaceMargin` defaults to `0.0001` world units and must be finite
and nonnegative. It shrinks the blocker in addition to numerical safety padding.
All blocker extents must be positive; endpoint extents can be zero. Overlapping
endpoints, endpoint/blocker contact, tangency and incomplete coverage fail open.
A default/cleared world or invalid query returns `Unknown`; a built empty world
returns `Visible`.

One convex blocker must cover the complete corner envelope. The implementation
never combines unrelated ray hits into an occlusion certificate. Caches only
remember candidates: every query retests current geometry, so reuse after a
rebuild, update, rollback or with another world remains safe. When several
blockers qualify, cached and uncached queries can return different valid indices.

## Reveal before a peek

Expand observer and target bounds to cover their reachable positions until the
result will be consumed. Account for state age, remaining transit delay, jitter,
the culling interval, acceleration and prediction error. Include camera movement,
stance changes, animation and equipment. Do not count transit twice if state age
already includes it. Missing snapshots increase the horizon; current velocity
alone does not bound a player who can change direction.

Teleports remain visible until a reliable envelope exists. Update moving/opening
doors before queries, or disable uncertain blockers. Enlarging endpoints does
not compensate for stale walls. A larger envelope costs more and intentionally
reveals enemies earlier.

## Use Tide's visibility policy

The game builds an `Audience` from players whose query result `IsVisible()`.
Keep the target's owner in that audience, apply team/spectator rules, then use
[`Visibility.Set(group, audience)`](../game/visibility.md#share-an-audience) for
an entity group, or restrict the components containing sensitive state. The
server retains the complete world for collision and wall-penetrating shots.

The query does not implement smoke, sound or gameplay events. Treat those as
separate policies. Hiding one component does not hide copies of its position in
public singletons, physics snapshots, rewind lists, inputs or event payloads.
The [existing visibility limitations](../game/visibility.md#consequences-across-a-visibility-boundary),
including Box2D/Box3D privacy integration and projected-world host migration,
still apply. This package does not resolve network correction latency.

## Cost and scope

The package is ordinary Tide code with strict arithmetic, binary64 intermediates
and conservative numerical padding. Match storage participates in snapshots,
hashes, packing and hot reload. Scalar queries read the tree and allocate no
lists. Keep their four-byte caches in caller-owned storage; do not edit the
world's implementation fields. Concurrent readers need independent caches and
must not overlap mutation.

A cached blocker is tested first. Otherwise a balanced BVH rejects distant
subtrees and masked geometry before corner testing. Leaves hold at most four
boxes; three-way partitioning handles repeated centroids. Build outside the
query loop. `Update` currently performs an O(N) search/refit, suitable for a few
doors rather than thousands of moving occluders.

For a batch, loop over preallocated storage and call `world.Query` directly.
Take each cache element into a mutable local and write it back afterward.
Passing a world or its lists by value to per-pair helpers can add expensive
copies. There is no package-owned scheduler or batch allocation.

The first version supports axis-aligned boxes only. Rotated boxes, meshes,
terrain, rounded primitives, multi-blocker certificates, baked visibility sets,
automatic scene collection and renderer integration are not implemented.
Profile a representative map: overlapping near misses and large latency bounds
can dominate query cost. The package provides conservative geometry queries,
not a complete anti-cheat system.
