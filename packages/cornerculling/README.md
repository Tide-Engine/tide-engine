# Corner Culling

Optional CPU visibility queries for Tide, in namespace `CornerCulling`. It
supports **rotated solid boxes and ellipsoids**, conservative observer/target
bounds, and bounded proofs combining multiple blockers. It is ordinary Tide
code, with no C library, renderer, physics package,
GPU, worker-thread or Unity dependency. Games omitting it compile none of it.

```sh
tide add cornerculling
```

Keep a `CullingWorld` in your own match or local state. Build it once from a
`List<Occluder>`, then call its readonly `Query` method:

```csharp
List<CornerCulling.Occluder> blockers = [CornerCulling.Occluder {
    bounds = CornerCulling.Bounds {
        center = float3(0), extents = float3(0.5, 4, 4)
    }
}];
culling.Rebuild(blockers);

var query = CornerCulling.CullingQuery {
    observer = CornerCulling.Bounds { center = float3(-5, 0, 0), extents = float3(0.5) },
    target = CornerCulling.Bounds { center = float3(5, 0, 0), extents = float3(0.5, 1, 0.5) }
};
bool visible = culling.Query(query, pairCache).IsVisible();
```

`culling` and `pairCache` must be mutable fields or locals of types `CullingWorld`
and `CullingCache`; only the cache changes during a query. Retain one cache per
directed observer/target pair. The overload `Query(query)` needs no cache.
`test/example.tide` is a complete usage fixture built by the package tests.

## Contracts

- `Bounds.center` is world-space. Endpoint extents are world-aligned half-sizes;
  blocker extents are local half-sizes or ellipsoid radii. Endpoint extents may
  be zero. All blocker extents must be positive.
- `Occluder.mask` and `CullingQuery.mask` are 32-bit integer bitmasks, default `-1`
  (all bits). A box participates when their bitwise intersection is nonzero.
  Set a box's mask to zero to disable it.
- `CullingQuery.surfaceMargin` defaults to `0.0001` world units. It shrinks each
  blocker in addition to automatic numerical padding. It must be finite and
  nonnegative; zero still retains numerical padding.
- `CullingResult.status` is `Unknown`, `Visible` or `Occluded`. **Only `Occluded`
  permits hiding.** `IsVisible()` returns true for both other results.
- `occluder` is the zero-based input-list index for a single-blocker proof.
  It is `-1` for a combined proof or no proof; use `status` to distinguish them.
  When several boxes block, which one is returned is
  unspecified. Cached and uncached queries can identify different valid boxes.
- `Rebuild(boxes)` takes a snapshot of the list and rebuilds the spatial tree.
  An empty built world reports `Visible`. A default or cleared world reports
  `Unknown`.
- `Update(sourceIndex, box)` replaces one existing box and immediately refits
  the tree, returning false for an unavailable world or invalid index. Indices
  remain stable until `Rebuild`. Disabled or invalid boxes can be re-enabled.
  Adding/removing list entries requires `Rebuild`; disabling needs only Update.
- `Clear()` empties the lists, retaining capacity for reuse, and makes queries
  `Unknown` until rebuilt. Assign `default` to release the world's owned lists.
  World fields and helper types are implementation storage: use these methods
  instead of editing tree entries or nodes directly.

A single convex blocker must strictly cover every observer-corner to
target-corner segment. Combined proofs either join solid wall sections or
subdivide the endpoint envelopes and prove every complete child envelope.
Different blockers hitting the sampled rays alone is not a valid proof.
Overlapping endpoints, endpoint/blocker contact, tangency, invalid geometry and
incomplete coverage fail open. Cached single blockers are always retested;
combined answers are recomputed, including after updates and rebuilds.

## Rotation, shapes and joined walls

`Occluder` replaces `Box`. `bounds.center` is world-space; `bounds.extents` gives
local half-sizes for `OccluderShape.Box` (the default) or three radii for
`OccluderShape.Ellipsoid`. `rotation` defaults to `quaternion.identity`. Finite nonzero
quaternions are normalized internally in binary64; zero/nonfinite rotations,
unknown shapes and nonpositive extents cannot block. Observer and target bounds
remain world-aligned, including the full motion/equipment envelope.

```csharp
Occluder diagonal = Occluder {
    bounds = Bounds { center = float3(0), extents = float3(0.25, 2, 4) },
    rotation = quaternion(0, 0.38268343, 0, 0.92387953)
};
Occluder pillar = Occluder {
    shape = OccluderShape.Ellipsoid,
    bounds = Bounds { center = float3(3, 0, 0), extents = float3(1, 2, 1) }
};
```

Touching or overlapping **axis-aligned** box sections can form a solid wall.
The proof uses a rectangular volume inside their union, never their enclosing
AABB. Internal seams disappear; `surfaceMargin` shrinks the joined volume's
outside surfaces. Exact touching is accepted only when the boundary arithmetic
is exact; uncertain joins require actual overlap. No tolerance bridges a real
gap. Joining retains at most 32 boxes derived from the first 16 eligible BVH
candidates, so dense or fragmented maps can still fail open. Rotated boxes are
not welded at a touching seam.

For other layouts, `maxSubdivisions` (default 32, clamped to 0 to 128) bounds extra
splits of observer/target bounds. Each child is fully certified against a single
blocker or a joined volume. Both children must pass. This can combine overlapping
shadows from differently oriented boxes or ellipsoids. Depth is capped at eight;
exhausted budgets return `Visible`. Zero disables subdivision, not wall joining.
A visible answer means **no proof was found**, not that an unobstructed ray was
necessarily found. These limits trade culling effectiveness for bounded work;
they never justify hiding an uncertain target.

## Motion, networking and ownership

Bounds must enclose every camera/player position that can matter before the
result is consumed. Include state age, the remaining transit delay, jitter and
the interval until the next query. Do not count transit twice when already
included in state age. Cover acceleration, prediction error, stance changes,
animation and visible equipment, not just the current capsule. Grow the horizon
across missing snapshots. Teleports remain visible until a reliable envelope
exists. Current velocity alone is insufficient when a player can change direction.

Synchronize moving/opening doors before querying. Endpoint expansion cannot
repair stale blockers. Disable a blocker while its state is uncertain. Author
boxes entirely inside opaque geometry: a mesh/collider's enclosing AABB is
**not** a safe occluder, and glass should not become an opaque box.

The package returns geometry results; it does not select teams, spectators,
audio, smokes, update frequency, or a network audience. Apply results through
Tide's existing `Audience`/`Visibility` APIs in match code and keep the target's
owner informed. Authoritative collision, shots and wall penetration still use
the complete server world. Check every transmitted copy of sensitive state:
public singletons, physics snapshots, rewind lists, events and inputs can disclose
positions independently of hiding a component. The existing Box2D/Box3D privacy
and projection/migration limitations remain unchanged.

Match storage uses ordinary snapshots, hashes, packing and hot reload. All
geometry arithmetic follows Tide's strict math flags, with binary64 intermediates
and conservative padding. Scalar queries allocate no lists and do not change
the world. Build/update use ordinary Tide list ownership; copies and snapshots
retain ordinary value/COW semantics. Read queries can share a world, with a
separate writable cache for each concurrent query. Do not mutate the world
concurrently with readers.

For batches, loop over preallocated query/cache/result arrays or lists and call
`world.Query` directly. Take a cache element into a mutable local and write it
back after the call. Avoid passing the entire world or its lists by value to a
per-pair helper: Tide's value copies can dominate the actual query. There is no
package-owned scheduler or batch allocation.

## Performance and initial limits

The BVH has leaves of at most four boxes, balanced median splits, three-way
partitioning for tied centroids and subtree masks. Cached blockers bypass tree
traversal when still valid. `Rebuild` belongs outside the relevance loop;
`Update` currently searches and refits the whole tree in O(N). Update a few doors
at a time; frequent movement of many blockers needs further profiling.

Boxes and ellipsoids are implemented; equal ellipsoid radii give a sphere.
There is no capsule, arbitrary convex hull, mesh/terrain import, baked PVS,
automatic ECS collection, GPU culling or automatic network policy. Large endpoint envelopes
and many overlapping near-miss blockers can cost more and hide fewer targets.
This is an initial package, not a production anti-cheat guarantee.

Run `tide_cornerculling` through the debug, release and web-debug CTest presets.
Its tests include cache/mutation cases, an independent interior-ray oracle,
native/WASM answer pins, projected-state privacy, snapshot/replay/pack checks,
and synthetic 10/50-player measurements with a handwritten C comparison.
Reference measurements and coverage are in `notes/corner-culling.md` in the
engine repository.

Open `test/visual.html` for a standalone client/server walkthrough. It replays
actual query results from the native fixture; it does not run culling in
JavaScript. Refresh it after changing fixtures or geometry:

```sh
python packages/cornerculling/test/make_visual.py build/debug/bin/tide_cornerculling.exe
```

Full guide: https://tide-engine.dev/packages/cornerculling
