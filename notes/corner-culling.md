# Corner Culling package

## Initial box implementation (2026-10-26)

The owner requested a separate Tide package based on the local Unity
CornerCulling project for a CS2-style prototype, with performance kept in mind.
The first implementation is `packages/cornerculling`, namespace `CornerCulling`:
ordinary Tide code, not a Unity dependency or a change to language/engine APIs.
It installs through the existing package-directory rule and is opt-in through
`tide add cornerculling`. Public documentation is the installed README and
`docs/packages/cornerculling.md`; the docs sample is compiled as part of the test.

### Geometry and safety contract

`Bounds` contains world-space float center/half-extents. `Box` adds an int mask
(default all bits). `CullingQuery` contains observer/target bounds, a mask and
nonnegative finite surfaceMargin (default 0.0001 world units). This first shape
is axis-aligned only. A box needs positive extents; endpoints may be points or
planar/volumetric bounds. Invalid boxes are nonblocking. Invalid query/bounds or
an unbuilt world return Unknown. An empty built world returns Visible. Only
Occluded permits hiding; CullingResult.IsVisible treats Unknown as visible.

All rays must intersect the strict interior of one convex blocker. The center
segment rejects candidates early; up to 8x8 corner segments establish the
certificate. Convexity extends corner coverage to the endpoint interiors when
endpoints are outside the blocker. Different blockers never combine their hits.
Endpoint bounds overlapping each other or the blocker fail open. Slab contacts,
tangencies, too-thin geometry and numerical uncertainty also fail open.

Inputs widen to binary64 before arithmetic. Endpoint bounds expand by
1e-12 times per-axis max(abs(center), extent); point coordinates are exactly
representable and stay points. Blockers contract by that allowance and the
user's surface margin. Broadphase bounds expand instead. Clipping retains a
strict >1e-12 parameter interval inside the segment. These margins deliberately
dominate ordinary double rounding; tests include extreme scales and boundary
cases, but are not a formal exhaustive floating-point proof. Finite screening
conservatively rejects values above the rounded 3.402823e38 threshold too.
No fast math or approximate reciprocal is enabled.

### Storage, acceleration and mutation

CullingWorld is an ordinary value holding Entry/Node lists and a ready flag.
The lists participate in snapshots, hashing, packing and hot reload when stored
in match data; no external pointer, global cache, Resident or runtime API exists.
The game owns placement, lifetime, visibility and synchronization of this value.
Public helper/storage fields are implementation details, not mutation APIs.

Rebuild snapshots caller boxes and stores stable original-list indices in its
spatially partitioned entries. The tree uses longest-bound-axis balanced median
splits, median-of-three/three-way quickselect, at most four entries per leaf,
subtree mask ORs and preorder escape indices. Traversal is iterative/stackless.
Balanced recursion is build-only; tied centroids do not cause degenerate depth.
Quickselect has no promised worst-case bound on adversarial distinct keys.
Invalid/disabled entries remain present with no blocking mask, so Update can
enable them. Their positions do not enlarge active subtree bounds.

Update uses an original input index, replaces one box and synchronously refits
all nodes bottom-up. Invalid indices/unbuilt worlds return false. It currently
costs O(N) for lookup plus refit, and moving boxes can reduce tree quality;
rebuild at a suitable boundary if spatial quality degrades. Adding/removing
membership requires Rebuild. Setting a mask to zero disables immediately.
Clear zeroes list contents, retains capacity for reuse and leaves an unbuilt/
fail-open world. Assigning default releases owned lists by ordinary value rules.

CullingCache is one four-byte candidate index plus one (zero means no candidate).
Every hit retests the actual current box, bounds, masks and margins. Reusing a
cache after updates, rebuilds, rollback or with another world cannot preserve a
stale result; it may select a different valid blocker. The original source index
is returned only with an Occluded result; otherwise it is -1. No particular
blocker is guaranteed when several can certify the query.

Scalar Query is a readonly method and allocates no lists; only its explicit mut
cache is written. Each concurrent query needs a distinct cache and must not
overlap world mutation. The test batch uses direct calls over preallocated lists,
taking cache elements into locals and writing them back. There is deliberately
no by-value free-function batch wrapper: inspection of generated C found that
it copied the complete world and query list. Builder helpers are world methods
for the same reason; a List<Entry> input helper copied entries at every node.
Neither rejected form remains in the package. Rebuild's one input value copy
stays off the hot path and preserves ordinary Tide semantics.

### Networking and initial scope

The package chooses no audience. Game code applies owner/team/spectator policies
and passes recipients to existing Visibility APIs. The test demonstrates
component conceal/reveal for another player, retaining the owner, and checks
that the hidden marker is absent from actual projected bytes. Its numerical
fixture singleton deliberately holds test queries/results; a real game must
not publish sensitive positions in an unrestricted singleton like this fixture.
The separate docs example holds only static walls and a local result of a fixed
synthetic query.

Reachable bounds must cover state age, remaining transit, jitter, scheduling,
movement/acceleration/prediction error, camera stance and visible equipment.
Dropped snapshots grow the horizon; unbounded motion/uncertain doors fail open.
Masks are not material inference: author only interior opaque wall boxes, never
an enclosing AABB substituted for arbitrary mesh geometry. Collision, wallbangs,
smokes and audio remain game policy. Existing physics singleton/rewind privacy,
projection correction latency and projected host-handover limitations remain
unchanged (language.md, netcode.md, docs/game/visibility.md).

Rotated boxes, rounded shapes, mesh/terrain certificates/import, scene collection,
multi-blocker proofs, PVS, GPU occlusion, scheduler integration and automatic
network policy are not implemented. There is no claim of production readiness
or representative-map bandwidth/anti-cheat validation.

### Validation and measurements

The `tide_cornerculling` CTest target builds the package and its real usage
example. Coverage includes endpoint overlap, tangency/contact, point/bounds
queries, masks, invalid numeric values, source indices, disabled/invalid-box
reactivation, moving boxes across old partitions, cache reuse/corruption,
unrelated blockers, snapshots, replay, packing and stable heap/scratch use.
An independent face-plane oracle checks 27x27 interior/corner rays for every
Occluded result in 1,024 randomized queries. A brute-force package-kernel pass
also checks BVH and cached/uncached agreement. The cold answer fingerprint is
`cc7d62b609066c8b` (652 Occluded queries), pinned across builds.

Performance cases cover 10/50-player directed matrices, point and expanded
observers, one/4,096 blockers, cached/uncached queries, a same-arithmetic
handwritten one-box C reference, 512 overlapping near-miss candidates,
single-box update/full refit, and 4,096 identical-centroid construction.
All measurements are synchronous and include the generated tick/list-result
loop. The C reference times only its equivalent one-box query loop; it does
not implement a BVH, ECS tick or a 4,096-box baseline. Point vs expanded cases
use target body bounds in both; expanded adds 0.5 units on every endpoint axis.

Windows native debug, native RelWithDebInfo and WASI debug builds pass all nine
package checks. Clang is 23.1.2 and Node is 24.22.1. All three retain the answer
fingerprint above, actual projected-byte privacy checks and stable heap/scratch
usage in the repeated-query workload. The production docs build passes,
including source includes, page/sidebar/link/anchor checks; existing allowed
404s are unstaged playable examples. No engine/compiler changes or broad suites
are needed for this isolated package/test-target addition. Other OS/Android/
MinGW/sanitizer coverage remains CI's. No GPU/browser test is needed for this
CPU-only implementation. No representative prototype/map was changed or tested.

Native RelWithDebInfo reference run, Ryzen 9 7900X / Windows, same strict float
flags as the engine. Values are microseconds per complete directed matrix:

| Players | Observer | Boxes | Cached | Uncached | Handwritten C, one box |
| ---: | --- | ---: | ---: | ---: | ---: |
| 10 | Point | 1 | 7.825 | 8.275 | 4.419 |
| 10 | Point | 4,096 | 8.606 | 10.706 | 4.419 |
| 50 | Point | 1 | 200.475 | 214.363 | 114.144 |
| 50 | Point | 4,096 | 221.100 | 272.812 | 114.394 |
| 10 | Expanded bounds | 1 | 36.175 | 36.663 | 22.644 |
| 10 | Expanded bounds | 4,096 | 35.881 | 37.850 | 22.538 |
| 50 | Expanded bounds | 1 | 916.538 | 918.119 | 572.094 |
| 50 | Expanded bounds | 4,096 | 915.456 | 965.050 | 574.056 |

Ten players have 90 directed links, 50 players 2,450. Each matrix has exactly
50/1,250 Occluded links; every benchmark checks that count for both paths. Four
warmups precede nine samples of 16 repeated ticks. The table is the median of
the nine sample means, not tick-tail latency. Query inputs are stationary and
hot; movement/query construction, asset authoring and real networking are not
timed. The 4,096-box layout is one central divider plus spatially irrelevant
boxes, deliberately testing tree rejection; it is not a dense production map.
The one-box package/C ratio is about 1.6-1.8x here, including package tick/output
overhead. The C numbers in the 4,096-box rows remain a one-box reference, not an
equal spatial workload comparison.

The adversarial 90-query/512-box near-miss pass stays Visible and costs
823.737 us; one-box replacement plus full refit at that size costs 14.387 us
(including the tick and its one extra scalar query). Building 4,096 identical-
centroid blockers costs 0.682 ms in a single observed build. This last value is
not a repeated median. Readonly scalar query loops retain their heap/scratch
usage, but COW allocation under live snapshots is not measured as a zero-allocation
claim. Tree construction/mutation are not allocation-free.

Timings are exploratory shared-desktop measurements: clocks were not controlled,
and native/web correctness/docs builds overlapped this run. They establish an
initial baseline and expose the candidate-overlap cost, not production budgets
or superiority to Unity/Burst. The deterministic counts and storage assertions
are correctness checks independent of timing. Raw local test output remains in
`build/{debug,release,web-debug}/Testing/Temporary/LastTest.log`.
