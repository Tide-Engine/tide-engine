# Global Illumination package

## Lighting quality repair (2026-11-02)

The owner rejected the large examples' lighting, independently of their art.
This repair keeps their geometry, materials, direct lights, camera and exposure
unchanged. It supersedes the hard nearby-only receiver eligibility described
below; it adds no ambient fill, sky source or multiple-bounce feedback.

Receiver eligibility now includes the nearby `viewDistance` sphere and a
conservative numeric main-camera frustum. Visible distant receivers retain
coarse blocks while nearby detail refines. This fixes the prior budget-dependent
loss of distant wall lighting: coarse cells accidentally extended outside the
48-unit sphere, then finer cells removed that incidental support. Root bounds
expand dyadically only for eligible receivers; geometry outside both the nearby
sphere and padded conservative frustum does not consume the field. Camera FOV/aspect/near/far and roll
changes trigger rebuilds. Padded planes cover the existing view-cell and
rotation hysteresis. Camera far is capped at one million units; extreme frusta
or world coordinates can still exceed the bounded hierarchy's useful detail.
The single hierarchy, physical capacity bounds, 128 blocks, stable anchor phase,
slot history, two GI atlases and twelve-texture mapped path remain.

Outside the nearby radius, planning uses a target cell size proportional to a
conservative lower bound on camera distance. It does not build every distant
visible surface at base detail only to discard that plan in the merge pass.
Near cells keep the original finest target. This bounds unnecessary distant
planning, but cold topology construction and repeated probe recounts still cost
CPU time and must be measured separately from a settled field.
Coverage also prepares triangle caches for newly eligible distinct static
assets, including distant coarse receivers. That asset-dependent cold/reveal
work is not bounded by the probe count; the simple architectural examples do
not establish its cost for many distinct complex meshes.

Shared physical probes now use the common base-spacing metric for both facing
regularization and receiver-PCF footprint/near-plane blending. Referencing a
record through a coarse versus fine brick cannot change those physical weights.
Controlled nonuniform shared-anchor and two-tap visibility cases pin these
invariants; the latter failed before repair despite identical physical inputs.

A larger uniform-emitter enclosure exposed a separate loss even with complete
mapped support. Captures inside it held the correct radiance, but black probes
behind a wall passed coplanar visibility while their global backface fractions
stayed below .25. Their small facing floor diluted valid front-side lighting.
Sparse gathering now excludes support behind the unperturbed geometric receiver
plane, with a continuous .001-base-spacing coplanar margin. Tangent/front-side
support remains; rejected support still enters the confidence denominator.
Normal maps cannot move that geometric plane. The independent legacy analytic
sampler is unchanged. No distance-atlas encoding, capture resolution or ray count
changes are required.

The enclosure also exposed a separate capture-ray error on the browser renderer.
A standalone WebGL program reproduced shortened depth on the same large plane,
without Tide, while native/WASI recorded cameras, vertices and instance bytes
matched exactly. Intermediate capture data now stores the physical triangle
plane instead of a world-position-derived radial distance. GI intersects each
point-sampled plane with its ideal cube ray before inverse-depth interpolation.
Normal coordinates and plane distance use the same quantized normal; independent
physical floor and oblique-plane witnesses check that contract. Captures retain
the radial far limit, transparent misses and distinct one-sided backfaces.
The final distance atlas and receiver visibility margin are unchanged. The
encoding, rejected depth-buffer candidate and precision limits are recorded in
[Render3D's capture-plane note](render3d.md#physical-capture-planes-2026-11-02).

New tests scan 16 wall and 16 floor points in a uniformly emitting 48x24x64
enclosure at capacities 512 and 2048. Every point must reconstruct the same
known incoming radiance, with the original five-level pixel tolerance. A
controlled black-back-layer/bright-front-layer case also pins valid energy and
opposite-side blocking. Each large benchmark scene retains its original shaded
wall witness and now checks fifteen additional analytically visible rear-wall
points: direct-only darkness, complete mapped/captured interpolation support,
nonzero bounce and a conservative regional energy lower bound. Raw paired
colors/world positions/spacing are retained outside timed frames. These are
regional failure oracles, not a claim of path-traced agreement everywhere.

Browser selectors `--group=regional512` and `--group=regional2048` run the
enclosure capacities independently on WebGL, ANGLE D3D11 and WebGPU. The 512
group retains its 32 wall/floor points, physical floor capture-depth witness
and independent oblique capture-depth witness; the 2048 group retains all 32
of its wall/floor points. Native no-argument execution still runs both
capacities, all 64 points and both raw-depth witnesses together. The combined
optimized browser regional run reached its existing 120-second guard, so the
split partitions cumulative fixture work by capacity. Per-capacity geometry,
settings, full convergence/capture work, assertions and tolerances are
unchanged. The new regional2048 software-WebGL deadline is documented below.
All six split regional browser registrations pass in final validation;
no renderer speedup follows from this fixture partition.

The regional enclosure alone now replaces its newly prepared backdrop HDR
target once with `RenderTarget.Window(.125, Format.Rgba16f)`, before GI updates
or color recording. It disposes the unused full-size target at that boundary;
Render3D retains ordinary ownership of the replacement. On the 128x64 test
root, this reduces only unasserted main-camera PBR backdrop shading to 16x8.
The full-resolution screen witness quads/readback points remain unchanged, as
do screen/camera aspect, world geometry, GI surface provider, all physical
32x32 capture targets, rays, update budgets, convergence frames and assertions.
The regular room tests still render and assert their full-size PBR center;
new locals restore default flags/target ownership for every other fixture.
Production examples and the 960x720 benchmark are unaffected. This removes
unasserted harness work after standalone regional2048 reached its original
120-second guard; the reduced-backdrop fixture still exceeded that guard.
This is a reduction in harness work, not a renderer speedup.
Capacity and raw-depth success messages flush after their assertions, allowing
later timeout reports to identify completed sections.

The newly added regional2048 stress fixture captures 1,184 active probes,
384 rays each, over approximately 74 convergence frames. One quiet, extended
software-WebGL diagnostic completed all 32 wall/floor oracles in 188.65 seconds
without a quality failure. Original 120-second WebGL/D3D11 deadline failures
remain failed runs; the extended diagnostic does not turn them into passes.
Only `tide_globalillumination_pixels_regional2048` and its `_d3d11` counterpart
now request an explicit 300-second browser budget and 330-second outer CTest
guard. All pre-existing WebGL/D3D11 defaults remain 120 seconds; served WebGPU,
including regional2048, retains its original 100-second guard. Geometry,
resolution, probe counts, rays, settings and every numeric oracle/tolerance
are unchanged. This new heavy case has a scoped correctness-test budget,
not a relaxed lighting assertion or a renderer performance claim. The harness
contract is in [testing.md](testing.md#explicit-budget-for-the-new-gi-regional-stress-case-2026-11-02).
Final scoped validation passes all 17 affected Windows ASan CTests and all 35
optimized web CTests across Vulkan/OpenGL and WebGL/ANGLE D3D11/WebGPU. This
combines the fresh 11/26 GI checks with unchanged 6/9 Render3D pixel/material/
skinning checks. The docs production build passes on Node 24.22.1. CTest command
inspection pins the two timeout overrides; an invalid zero timeout is rejected.
Raw successful and failed attempts, source hashes and matched large captures
are retained in the [lighting repair report](../bench/results/globalillumination-lighting-20261102/README.md).
No other native OS/mobile or hardware-web validation was performed, and no CI
was dispatched. This work remains on `feat/render3d-gi`, off dev.


Final matched Vulkan captures on source `2d4d057e43573f4e3d0d706c299818c1b8f0923b` retain all four direct-only
RGBA baselines and all sixteen world hashes. Both previously black 2,048-cap
hero rear-wall regions now pass all fifteen support/energy witnesses. Actual
images still show coarse spatial variation; these checks do not establish
production aesthetic quality. The quiet controlled cohort passed all eighteen
processes / 1,800 GPU samples. Settled whole-frame GPU medians on RTX 4080 SUPER
at 960x720 are 4.601/4.910 ms for atrium 512/2,048 and 2.685/2.631 ms for
courtyard (off .869/.595 ms). CPU recording is .199/.205 and .281/.294 ms.
No captures/uploads occur in those timed frames. First-frame setup CPU medians
span 103.50–566.16 ms; cold topology/reveal spikes remain unresolved. This is
no active-update speedup or FPS claim. The final shared gate was released and
disposed at 06:44:25.368 UTC, before offline evidence/plots; no GI processes or
gates remain queued. Full ranges, identities, raw failures and limits are in
the linked lighting repair report. No dev merge or CI dispatch occurred.

## Larger continuous examples (2026-11-02)

The owner requested larger examples after the adaptive district work. The
existing GI benchmark now also builds a 48x72x18-unit atrium (315 meshes) and a
160x120-unit courtyard (476 meshes). They combine touching receiving floors,
closed curved arches/columns, elevated galleries, bridges, ramps and stairs.
The two small OBJ assets are authored in the repository; their generator checks
exact bytes, closed opposite edges and positive volume. There is no external
environment download, baked lighting or authored probe placement.

Both scenes use a sun and six shadowed spotlights with zero ambient irradiance.
Their fixed shadow bounds cover the architecture. Nonquick runs request
2048-square shadow cells, bias .001 and slope bias .004 to avoid severe acne
from the broad sun volume; quick checks retain 32-square cells. The backend's
atlas limits can reduce the requested cell size. Shadow cost/memory belongs to
the whole example, not to GI overhead. The production Render3D/GI code and
defaults do not change.

`--scene atrium|courtyard` chooses the static examples; `--probes 125..2048`
selects physical capacity (default 512). Hero views retain ordinary 48-unit
residency. Whole-scene `--overview` deliberately requests distances 200/350,
which is a different coverage workload. `--debug` displays actual active,
relocated capture positions with enlarged distance-scaled markers. These
static scenes reject travel/moving-light options rather than applying the
district animation to unrelated geometry. Options also accept `--key=value`
for browser queries.

The host checks independently counted meshes and projects each example's own
shaded wall through its actual hero camera. It retains the original >15 summed
RGB brightness increase, match hash, convergence, capacity/budget and shader
texture-limit checks. Overview/debug initialization still validates that same
scene. A failed native brightness check can save a diagnostic capture but
still fails. Source and invocation details are in
[the benchmark guide](../bench/globalillumination/README.md).

All six affected native Windows ASan CTests pass (Vulkan validation/OpenGL),
including the retained all-budget room checks and both larger scenes. All four
affected web-debug checks pass: retained WebGL/WebGPU room budgets and both new
WebGPU scenes. The architectural mesh generator's byte/topology checks pass.
Optimized native captures also pass each scene's original brightness/count/hash
oracles. Other native OSes/mobile and large-scene WebGL are unmeasured here.

[The larger-scene evidence report](../bench/results/globalillumination-large-20261102/README.md)
retains sixteen matched off/on/probe captures and exact active counts. A higher
cap still meets the independent block limit and does not guarantee better light
at every receiver; remote hero-view geometry can lie outside the 48-unit field.
These examples expose remaining dark regions and reconstruction seams. A bounded
90-second quiet-window attempt launched no measurement child while other builds
continued. Its shared gate closed at 01:47:41.681 UTC; capture timing fields are
uncontrolled and excluded from performance claims. No new large-scene frame-time
claim or dev merge is made.

## Adaptive coverage and regional updates (2026-11-02)

The owner approved replacing the world-sized uniform lattice after inspecting
its probe placement and questioning large-scene scalability. This section
supersedes the earlier topology, initialization and round-robin descriptions
below. Those sections retain their source-pinned quality experiments and room
measurements as history; their timings do not measure the adaptive implementation.
The package remains opt-in, machine-local and on `feat/render3d-gi`; no dev merge,
publication or light baker is included. Render3D's general provider/capture
interface is unchanged, with no compiler, engine runtime or backend API change.

### Coverage, physical slots and interpolation

`WorldBounds` tracks current visible mesh/skinned instances, their numeric
material facts, transforms and asset-local bounds. A balanced instance BVH is
rebuilt after geometry changes and shared by coverage, placement and capture
candidate queries. Nearby eligible static assets lazily acquire their existing
triangle BVHs. Coverage tests actual triangle proximity where available rather
than treating a whole mesh AABB as a filled receiver volume; other geometry
uses conservative bounds. Skinned and custom-deformed instances are kept as
unbounded capture candidates so rest-bound culling cannot discard their current
rendered pose.

The production field is sparse. A bounded hierarchy skips empty regions and
refines relevant leaves toward `probeSpacing`, prioritizing geometry near the
main view. Leaves are 3x3x3 logical probe blocks, at most 128 blocks and depth 24.
The physical probe capacity is exactly the clamped `Settings.maxProbes` (default
512, range 125..2048); it is not rounded to a multiple of 27. Coincident dyadic
anchors share physical IDs across neighboring blocks and subdivision levels.
Each node also caches a 27-bit support mask. A conservative instance query
retains only logical anchors whose interpolation support can reach receiver
bounds, including the normal-bias and boundary-filter padding. The same mask
drives physical budget counting and reconciliation; omitted logical entries
encode zero on the GPU and -1 in the CPU block. Block capacity is independently
bounded at 128 rather than estimated as maxProbes / 8. Reserving every outer
anchor could otherwise force the 125-probe thin-wall fixture to 96-unit spacing
even though its actual fine support fits: the pruned fixture needs 88 physical
anchors at the requested spacing 3. Real geometry controls leaf relevance;
conservative bounds control support pruning within each relevant leaf.
The planner first resolves useful geometry within its bounded planning table,
then coarsens complete sibling groups from the least important eligible parents
until both the unique physical-anchor count and block cap fit. Checking the
physical budget at each individual split could trap neighboring coarse cells
whose collective refinement fits while a single intermediate refinement does
not. Importance measures nearby receiver support intersecting the cell, rather
than letting a cell's empty portion claim proximity to the camera. It combines
cell size with squared receiver distance and a continuous view bonus in 1..2.
The requested finest spacing remains a request, not a guarantee at any named
receiver. Eligible sibling groups use min-heaps with stable ties, avoiding a
whole-plan scan for every merge. Unique-anchor counts are still recomputed
within the bounded plan as merges proceed; this is not a constant-cost topology
builder. The reachable budgeted tree is compacted before reconciliation and
GPU upload. The representation does not allocate a full enclosing world lattice.

Base spacing increases in stable power-of-two steps only when needed for
coordinate precision. Every subdivision retains a half-base-cell anchor phase,
reducing exact alignment with common planes without making spacing continuously
track the camera coordinate. Geometry relevance has .3 base-spacing padding,
covering the .04 normal bias and .25 boundary filter together.

`viewDistance` defaults to 48 world units and bounds receiver residency around
the main camera. View-cell crossings, significant orientation changes, geometry
changes and coverage settings can rebuild topology. `ReconcileBlocks` reserves
surviving blocks and exact matching physical anchors before assigning new ones.
Matching probes retain their atlas ID, light, captured flag and relocation
offset; new/reused anchors have their records cleared before use. Root/topology
changes retain a compatible atlas. Capacity or base-spacing changes and pack
replacement can recreate the field. Empty coverage or disabling GI/Render3D
releases it. This is a finite working set, not unlimited detailed world coverage;
competing geometry inside the resident range can still force coarse blocks.

The existing 18x19 physical records retain their 16-square octahedral interior,
gutters and offset metadata row. A 64-row strip appended below all probe records
stores byte-encoded hierarchy codes and 28 entries per block in the distance
atlas: 27 physical probe IDs plus a 20-bit edge/corner transition mask in RGB.
Codes are zero for missing, 1..128 for blocks and child-table offset + 256 for
branches; physical IDs use index + 1. Each leaf's B channel holds six direct
face-transition flags. Atlas size is
`576 x (19 * ceil(maxProbes / 32) + 64)`, at most 576x1280. Pixel-center lookups
share the existing bilinear/clamp sampler, preserving two GI texture bindings
and the fully mapped skinning path's twelve-binding limit.

Physical slots initialize independently. `Environment.initialized` counts
captured active probes, not a prefix of atlas indices, and `activeProbes` counts
unique resident physical anchors. Metadata alpha marks a slot captured on the GPU;
fresh slots stay transparent until convolution writes their own metadata.
Debug uses active/captured flags and actual relocated positions.

Each block performs its eight-neighbor visible-irradiance gather. Interior
receivers need one hierarchy lookup. Equal-resolution shared faces are already
C0-continuous and retain one gather up to the face. Direct-face flags and separate
edge/corner bits restrict the multi-block path to actual resolution transitions
or missing coverage near the affected feature. A fixed world-space box filter
of radius .25 base spacing weights those blocks by overlap; up to eight blocks
can contribute. Neighboring leaf and empty-domain lookups are cached within the
gather to avoid repeating a serial hierarchy walk. Shared anchors capture only
once, though boundary gathering can sample a shared record through more than
one contributing block. Visibility
metrics, encoded offsets and bounded relocation use the common base spacing,
independent of a block's interpolation spacing. This preserves the meaning of a
physical record across subdivision changes. The positive cosine convolution,
receiver-plane visibility and one-bounce diffuse contract remain.

Partial refinement also gathers available captured outer corners while new fine
nodes initialize, blending to the fine result as captured fine spatial support
becomes complete. It can preserve provisional light through an ordinary
subdivision, but pruned corners and changes across several levels can leave
insufficient retained support. Populated history is not guaranteed through every
topology change. Missing metadata never licenses stale reused-slot lighting.

The analytic dense sampler and production world sampler have separate entry
and gather paths (`SampleLegacy` and `SampleWorld`). A shader using one does not
compile both large sampling branches into its software-driver JIT path. The
legacy path remains for independent controlled-light fixtures; it is not the
production world's allocation policy.

### Changes, scheduling and captures

New settings are `viewDistance = 48`, `captureDistance = 48` (both clamp
1..1000000) and `refreshInterval = 600` (clamp 1..36000 rendered frames).
`updatesPerFrame` remains 0..16 but is a maximum actual capture count, rather
than a requirement to spend the full budget. Initialization precedes dirty
refresh, then aged background refresh. Dirty selection orders by oldest capture
before proximity, preventing continuous motion from starving farther dirty
probes by continually selecting the same nearest ones. Age makes a probe eligible
subject to the budget; it is not a guaranteed complete refresh interval. Zero
freezes captures while residency can still change, leaving newly streamed anchors
uncaptured until updates resume.

Comparable numeric transforms/material values, additions/removals and selected
Render3D light changes mark affected probe capture neighborhoods dirty. Light
comparison uses exact numeric values rather than hash equality as identity.
Point/spot lights invalidate old and new influence regions; directional light
changes invalidate all resident probes. Skinned/custom-deformed instances request
updates conservatively each frame. Dirty captures retain their previous light
while awaiting refresh. Changing capture range dirties the field.

Shadow dependencies also include castShadows flags, moved/added/removed casters
and cutout silhouette controls. Such changes propagate through each affected
selected shadow light once per frame, so a caster outside a probe's capture
range can still invalidate a captured receiver's changed direct shadow.
Directional shadow lights dirty the whole resident field; unbounded skinned or
deformed casters conservatively affect all selected shadow lights. Numeric
opaque emission/color/roughness edits do not propagate as silhouette changes.
`SameLight` includes allocated shadowIndex/cascadeCount, and changes to
Renderer.shadows invalidate retained captures.

Callback captures and live texture identities/contents have no comparable value
API. Games report those changes with
`GlobalIllumination.Invalidate(mut Environment, LocalBounds region)` before the
package update. A valid region dirties probes whose finite capture range can
intersect it and propagates possible shadow effects through affected selected
shadow lights; an invalid/default region dirties all active probes. This uses
the previous effective light set when called before this frame's collection.
The function queues work, never captures synchronously. Background refresh is a
fallback for unreported changes, not immediate change detection.

The instance BVH chooses local candidates for a probe. Current attached material
resources and prepared poses cannot be stored as component copies: live query
bindings record each selected instance once into the first face, with automatic
frustum rejection disabled and LOD(0) forced for an all-direction source packet.
Package-local `GIRecordMarkTo`/`GIReplayTo` in `cache.c` reuse those frame-owned
shaded packets for the other five faces. Their explicit RenderTarget restores
the face destination because evaluating Draw.list first selects the window.
The face camera is settled by Clear before replay; per-face static bounds can
reject packets conservatively. Raw mark/replay helpers remain the CPU command
test boundary. Each replay gets an independent run descriptor while sharing owned instance/uniform/texture
bytes. This retains shader callbacks and resources without escaping query rows,
patching shader-specific uniform offsets or adding a runtime entry point.
Ordinary capture updates reupload the lookup strip as well, allowing it to
recover with captures after GPU resources are lost.

`PackRays` and inverse-depth gathering ignore hits whose reconstructed radial
distance reaches the capture limit. Cube-face corner geometry beyond the finite radial
capture range therefore contributes neither radiance nor visibility, despite
being inside a face's axial far plane. This requires no Render3D change.

Scene maintenance still walks instances each frame. Obtaining live bound
materials still scans the mesh/skinned queries once per updated probe, so this
CPU work remains proportional to total scene instances. Captures no longer
repeat that traversal for all six faces. Dynamic/unbounded geometry, BVH rebuilds
and adaptive topology construction have further costs; no fully sublinear or
general large-world performance claim follows from these bounds. Finite capture
distance can omit distant emissive geometry/occluders, and coverage limits from
selected lights and the main shadow atlas remain. Small emitters, thin geometry, rest bounds, coarse
support and mixed-resolution filtering remain quality/performance limits.
Entity-to-asset slot-cache clearing thresholds scale with the live population
above a 4096-entry floor, so a large stable scene does not clear/rebuild those
maps every frame merely for exceeding the old fixed threshold.

The tracked benchmark adds 1..256 separated buildings, overview/debug captures,
camera travel and a local moving light. It distinguishes physical capacity,
resident probes/blocks, dirty probes and actual capture/candidate/draw counts;
primary receiver validation, convergence and allocation audits stay outside
timing. Its elevated overview requests view distance `2 * extent + 16` (extent is
the larger grid dimension times 24), explicitly covering the district from that
camera rather than applying the ordinary nearby-view default 48 in empty air.
Nearby/travel views retain production defaults; overview is a different coverage
workload. Scene construction now runs once on the first simulation tick after
host workload options are set. The former Main Spawned handler ran during
world initialization before those options: early uncommitted exploratory
district rows therefore still contained the seven-mesh room and are invalid
large-scene scaling evidence. They must not enter the final report. Enabled
benchmark runs now require geometryCount == 7 * buildings and report
`gi_scene_instances` alongside the requested count.

Static quick runs without overview/travel/moving light reuse the once-filled,
validated resident field across budget modes rather than repeating identical
full-field convolution. Timed runs and moving/view-changing quick cases retain
independent cold starts. Lighting oracles, all budgets, hashes, shader texture
limits and runner deadlines remain. Cold topology construction and warmed
rendering require separate measurement.

The expanded browser pixel fixture has independent analytic, room, direct-bounce,
geometry, regional512 and regional2048 groups on WebGL, ANGLE D3D11 and WebGPU. Native no-argument runs
still execute every case together. Every group retains its original lighting
oracles and probe settings. Existing groups retain their 120/100-second browser
guards; only the newly added regional2048 WebGL/D3D11 runs have the explicit
300-second budget described above, with WebGPU unchanged. This partitions
cumulative test work rather than reducing capture quality or claiming a runtime
speedup. The refresh helper always records a first frame, then waits for both
all active slots captured and zero dirty probes within its bounded cycle;
an initialized count alone cannot skip a changed-light recapture.

### Validation and evidence

All seven focused native Windows ASan CTests and all eighteen focused web-debug
CTest checks pass with Clang 23.1.2 and Node 24.22.1. Native GPU coverage is
Vulkan with validation and OpenGL; browser coverage is SwiftShader WebGL,
ANGLE D3D11/WARP and WebGPU. The package CPU fixture now has 22 cases, including
shared/omitted anchors, retained history, camera recentering at large coordinates,
regional and shadow invalidation, continuously dirty fairness and packet replay
safety. Pixel checks retain the independent constant/directional-light,
visibility, coarse/fine/filter-edge, sparse readiness, sphere contact and real
thin-wall oracles. The D3D11 room group passed at 119.92 seconds against its
unchanged 120-second guard; this is a local pass with little timing margin,
not a guarantee for other software-renderer hosts.

Additional native ASan quick cases pass for the actual 256-building nearby view
(1792 mesh instances), a 64-building expanded overview with debug markers and
240 frames of camera travel with a moving light. The nearby 256-building case
has 510 resident physical probes in 74 blocks and keeps base spacing 1.5 at the
primary floor receiver under capacity 512. The overview deliberately spreads
that capacity over a wider range. All these cases retain unchanged match hashes,
the twelve-texture limit and capture counts within the requested maximum.
The production docs build passes on Node 24.22.1; subsequent prose corrections
do not change its structure or examples.

Controlled optimized timing rows and actual engine captures are recorded in
[the adaptive evidence report](../bench/results/globalillumination-adaptive-20261102/README.md)
after this source commit. Historical room timings below do not establish the
adaptive implementation's cost. Other native OSes, Android, Unity execution and
device-loss recovery are not locally exercised. No dev merge or CI dispatch is
part of this work.

The controlled source is `b1c69748f47a800269fae11fbf7d4f3370df7c8c` on the
queried Ryzen 9 7900X / RTX 4080 SUPER, NVIDIA 617.42, native Vulkan 960x720.
All 30 planned processes retain 6300 GPU samples. Across three 100-frame process
medians, the 256-building scene costs 1.551456 ms whole-frame GPU / 1.1064 ms
CPU recording in the settled idle cache, and 3.277920 ms GPU / 1.4061 ms CPU
with a moving light and four actual captures/frame. GI-off is 0.349088 ms GPU /
0.8716 ms CPU. Moving-light also changes direct shadow work, so subtraction does
not isolate GI cost. CPU scene bookkeeping/live capture queries still scale with
instances; no fully sublinear CPU or general game-FPS claim follows.

Three separate 1200-frame static runs reach aged background refresh: median GPU
1.562624 ms and p95 3.620864 ms, CPU recording median 1.1682 ms and p95 2.0463 ms,
with 0.602 captures/frame on average. A separate single 240-frame camera-travel
and moving-light run retains all 240 samples: GPU median/p95 3.927392/6.642688 ms,
CPU recording 0.5579/14.4221 ms. Synchronous topology reconstruction remains a
streaming CPU spike, and the ending field still has 440 pending captures/refreshes
at the four-update budget. These limits remain experimental work, not an
unqualified large-world performance or fully converged streaming-quality claim.
The report retains ranges, raw rows, source/binary hashes, actual captures,
CPU-selected placement maps and an interrupted pre-window start separately.

## Spatial quality follow-up (2026-11-01)

The owner's "wobbling" report means uneven bands in the still image, not
temporal instability. This pass addresses spatial reconstruction and probe
coverage; it adds no temporal filter. It remains on `feat/render3d-gi` and must
not be merged to dev without the owner's instruction.

### Geometry-aware capture placement

World-space lattice anchors and the total probe budget stay fixed. Before each
scheduled capture, the package checks nearby eligible instances and can move a
probe out of a closed solid or away from a surface. Target clearance is .08
spacing; correction starts below .04 spacing. An established free offset is
retained. Three correction steps and a final check may move at most .45 spacing from
the anchor; an unresolved location retains its previous offset and remains
subject to raster back-face rejection. No boxes are authored or treated as
solid geometry.

`placement.tide` caches actual local triangles in a balanced preorder BVH per
asset/pack generation. Node bounds and candidate triangles are transformed for
world-space closest-point distances, with double intermediates and double
relocation-bound comparisons. Mirrors, nonuniform scale and shear preserve
render-oriented feature normals. Singular/nonfinite queries fail conservatively.
Triangle traversal has checked forward escape/range guards.

Topology welds equal positions, additionally snapping coordinates within the
largest local axis extent times 1e-12 of zero only for the weld. This closes the
example sphere's tiny trigonometric seam/pole residues; retained noncollapsed
triangle positions are unchanged. A closed mesh requires two opposite
incidences for every welded edge. Signed winding with deterministic oblique
rays distinguishes outward solids from inward room shells and reversed cavity
boundaries; ambiguous edge hits retry, then remain unknown. Open meshes supply
unsigned surface clearance, never an inferred filled AABB.

Only visible opaque single-sided static meshes without custom vertex callbacks
guide relocation. Cutout, skinned, double-sided and deformed meshes still use
the existing raster capture/rejection path; CPU authored triangles do not claim
to classify their current occupied volume. BVHs build lazily when updates run.
Pack-generation changes now restart capture history as well as bounds/geometry
caches. Disabling releases local caches and GPU resources as before.

Probe records are now 18x19: the original 18-square irradiance/moment tile and
gutters plus a metadata row encoding offset/spacing + 128/255. Zero is exactly
representable in the Rgba8 fallback. Pixel-center metadata reads use the same
bilinear/clamp sampler as distance moments, preserving two final GI texture
bindings and the fully mapped skinning path's twelve-binding limit. Visibility,
facing and debug positions use the actual capture position; interpolation cells
remain anchored to the lattice. The maximum atlas is 576x1216, within 2048.
Capture near clipping scales down with spacing to remain below small clearances.

### Positive angular reconstruction

The previous SH9 truncation produced a positive rear lobe for narrow incident
light and extra bands on the sphere. Irradiance now uses nonnegative cosine
quadrature over the same 384 symmetric capture rays. A small per-direction
quadrature correction preserves PI*L for uniform incoming light. Back-face rays
remain in the normalization denominator, so rejected radiance is not restored.

A 64x6 two-color scratch target packs each ray once per updated probe. One
output holds radiance with front-face coverage alpha; the other holds encoded
direction with solid-angle alpha. Ordinary texture reads recover straight
channels, so convolution explicitly restores the required radiance alpha.
The retained geometry image is still written, preserving its resource/lifecycle
contract, but convolution no longer reads it. For each of the same 384 indices,
it derives the exact PackRays face and symmetric 32-square texel center. With
`v = FaceVector(face, uv)` and `L = 1 + dot(plane, plane)`, the unit ray is
`v / sqrt(L)` and solid angle is `1 / (L * sqrt(L))`; therefore the cosine
weighted ray is exactly `v / (L * L)`. Sphere/back-face normalization uses that
same analytic solid angle. Only the retained radiance/front-face image is read
inside the loop, removing one RGBA16F sample and unpremultiply per ray.
This also removes the old intermediate FP16 ray/solid-angle rounding; radiance,
all ray positions/counts, positive-cosine correction, visibility, captures and
budgets remain unchanged. Existing independent integral/directional/occlusion
oracles retain their expected values and tolerances. The fresh 11 native ASan and 26 optimized web GI checks pass with these
analytic weights; fewer samples alone is not a measured renderer speedup. The SH projection/reduction targets and
passes are removed. The six captures and final two-atlas sampling boundary stay
the same.

Visibility stores inverse projective depth in R and its standard-deviation
channel in G, with the per-probe back-face fraction in B. Captures currently
write zero G; numerical sigma is max(G, mean * .001, 1e-5). For a probe-relative displacement
v, the receiver metric is spacing / (abs(v.x)+abs(v.y)+abs(v.z)). A capture's
plane metadata is intersected with its ideal pixel ray, then converted to the same metric. This
metric is affine over an octahedral fold for a planar occluder. Misses contribute
zero inverse depth. Four capture pixels first interpolate inverse axial cube
depth, then convert to the octahedral metric. This avoids point-sampled angular
error and subtracting nearly equal half-float first and second moments.

The receiver uses four atlas texels as a visibility PCF, comparing each tap
against the plane through the actual receiver with its unperturbed vertex
normal. The normal bias only chooses interpolation coordinates and cannot move
the occlusion endpoint through a thin wall. Taps meeting the plane behind the
probe have no support. Plane intersections far from the receiver are smoothly
downweighted by 1 / (1 + (distance / (.5 * spacing))^4). Near-zero plane distance
smoothly falls back to point-depth comparison. Filtered probability is cubed
once. A max(mean * .01, 1e-5) comparison margin avoids turning capture/half-float
rounding into coplanar self-occlusion. The same default is exercised by the
physical thin-wall blocking and open-emitter control.

Visibility retains cubic probability suppression and aggregate confidence
gating, while removing the lowest-20%-probability zero-support region that
made contact fringes overly sharp. Eight trilinear neighbors remain; facing is
max(dot(N, towardProbe), 0)^2 + .01, regularized at the probe. The small floor
retains free dark support without giving grazing probes enough weight to print
shadow-terminator contours. Rejected probes remain in the confidence denominator
so usable coverage fades continuously. Wider spatial kernels and renderer
derivative/curvature inputs were inspected but were unnecessary after the PCF
correction; no Render3D API change remains. Uniform-bounded neighbor/tap loops
retain the same iteration and bilinear evaluation order without forcing static
expansion of the whole sampler on software drivers.
This is still bounded single-bounce diffuse GI: sparse support, thin geometry,
small emitters and coarse world coverage remain quality limits.

All seven affected native Windows ASan CTests pass, including Vulkan validation
and OpenGL, as do all nine affected web-debug CTests across scoped runs on
WebGL, ANGLE D3D11 and WebGPU. The original twelve-texture mapped-skinning
oracles remain intact. New checks cover actual sphere-contact light, a physical
thin wall and its open-emitter control, a narrow incident ray with no rear lobe,
nonzero bounded/stable world offsets and their GPU metadata, plus independent
closed/open/inward/reflected/sheared/degenerate geometry queries and large-world
relocation bounds. Match hashes remain unchanged. The production docs build
passes on Node 24.22.1 with the existing allowed unstaged-example 404s.

An initial software WebGL pixel run exceeded its existing 120-second runner.
Uniform-bounded loops bring mapped-skinning WebGL from 67.9 to 4.36 seconds;
the unchanged pixel oracles then pass in 106.17 seconds. No timeout or assertion
was relaxed. The optimized native capture is pixel-identical to the accepted
pre-optimization result. Native captures at 160 and 256 frames are identical;
GI-disabled output is identical to the earlier implementation. This report
concerns spatial artifacts, not a new temporal claim.

The same-scene final captures are in
`bench/results/globalillumination-spatial-20261101/captures/`. The reported black
contact fringe and secondary sphere bands are removed. Small corner occlusion
lobes remain; no general leak-free or path-traced quality claim is made.
Other native OS/mobile hardware, Unity and device-loss recovery remain
unavailable local coverage. Final paired cost measurements follow this code
commit. The earlier measurements below describe their pinned SH-based source,
not this implementation. Work remains on the feature branch only.

The paired optimized run uses code commit 8838029c9 against the preserved
ba070987a executable, in adjacent alternating processes after correctness work
ended. On the queried RTX 4080 SUPER / Ryzen 9 7900X at 960x720, default four
updates/frame costs 1.0608 ms whole-frame GPU and .0896 ms CPU recording, versus
.6434 ms and .0861 ms earlier. Frozen updated GI is .7728 ms GPU; runtime-disabled
GI is .1490 ms. This is a measured quality/cost tradeoff: the higher-fidelity
receiver lookup adds cost, while CPU recording stays near .09 ms in this room.
All 1,800 GPU samples, process-median ranges, source/binary identities and method
are retained in the [spatial report](../bench/results/globalillumination-spatial-20261101/README.md).
No general hardware/game-FPS claim is made, and no dev merge is authorized.

## Initial separate automatic-coverage implementation (2026-11-01)

The owner approved implementing GI as its own optional package depending on
Render3D, with automatic coverage across rendered geometry, no authored probe
volumes, and both visual quality and performance as requirements. Work remains
experimental on `feat/render3d-gi`; do not merge it to dev without the owner's
instruction. The original bounded Render3D prototype was reverted from dev as
76582cce5 after the owner rejected its captured quality. Its history and defects
remain in render3d.md.

The package is `packages/globalillumination`, dependency name
`globalillumination`, namespace `GlobalIllumination`; its manifest depends on
`render3d`. Adding the package opts into GI. `Environment.settings.enabled`
defaults true and can disable it locally. Render3D alone owns no GI field,
targets or history. Its general provider/capture boundary is recorded in
render3d.md; no compiler, engine runtime, backend or language changes are needed.

### Settings and lifecycle

The local `Environment.settings` is `Settings`: `probeSpacing = 1.5` clamps to
.1..1000, `maxProbes = 512` clamps to 125..2048, `updatesPerFrame = 4` clamps to
0..16, `intensity = 1` clamps to 0..16, and `debug` defaults false. The lower probe cap provides a minimum practical working budget. These bounds and implementation sizes are prototype choices, not
language requirements.

`WorldBounds` collects visible static/skinned Render3D mesh assets after
Prepare and before Lights. Asset-local bounds are cached by asset identity;
entity-to-slot maps avoid repeated linear lookup for established instances.
Maps are periodically cleared when their retained entry count grows beyond
4096. `cache.c` supplies package-local `GIAssetGeneration`, a bit-preserving
wrapper around the existing `tide_pack_generation()`. Pack-generation changes
clear the bounds caches; the bridge retains no pointers, geometry or external
state. Bounds are transformed into world space per instance, with skinned and
custom-deformed coverage currently based on authored rest geometry.

The layout pads observed receivers by at least a quarter cell and uses a world-anchored
half-cell lattice. Spacing doubles until its count fits the probe budget;
binary64 fitting intermediates avoid overflow during count calculation. A
precision floor prevents distinct probes collapsing at large coordinates.
Empty or unusable geometry produces no field. Coverage is retained while
observed bounds remain safely inside it and grows when necessary, avoiding
rebuilding for every small movement. Requested spacing/budget changes reset
the retained geometry union. Very large worlds coarsen a single uniform grid;
there is no sparse/adaptive grid or unlimited-detail claim.

GI updates run after Render3D Shadows and before Color. They install the
indirect provider before ordinary mesh drawing. A layout change recreates the
field and resets progressive initialization. Settings intensity/debug/budget
changes retain a matching layout. Budget zero freezes captured lighting.
Disabling GI or Render3D, or having no valid geometry, releases all GI targets.
Explicit `GlobalIllumination.Dispose(environment)` belongs before recording.
GPU history lost on device reset refills as updates run; a frozen field cannot
recover until updates resume. All GI state remains machine-local and outside
simulation snapshots, networking and match hashes.

### Captures, reconstruction and cost

One update reuses six 32-square two-output Rgba16f targets. Render3D's diffuse
capture mode shades direct diffuse reflection plus emission, excluding ambient,
prior indirect light and direct specular. Static and skinned opaque/cutout
material paths retain maps, callbacks, prepared poses and shared deformation.
Surface/UV callbacks use the probe view direction; geometry callbacks keep the
main eye. Transparent surfaces receive GI but are omitted from captures.
Eligible world transforms are resolved once per frame and reused across all
faces and probes; borrowed query rows supply current material resources.

The first capture image is radiance. The second has R normalized perpendicular
plane distance, GB an octahedral plane normal facing away from the capture eye,
and alpha 1 front/.5 one-sided back/0 miss. Transparent-black alpha0 means far/unoccluded; this preserves nearby distance
precision without per-face auxiliary clear draws. Double-sided cards have no
solid interior and do not mark back faces.

Each probe projects a symmetric 8x8 ray grid on each face (384 rays) into
nine signed spherical-harmonic coefficients. Sixteen independent 24-ray groups
write a retained 9x16 scratch target, then reduce into 9x1. Lambert band factors
are applied during reduction. Partial sums and weights share a scale that keeps
HDR half floats finite and cancels during normalization. The bounded uniform
loops avoid excessive software-driver unrolling and long serial texture-read
chains in a nine-fragment draw. A second pass expands coefficients
into nonnegative directional irradiance and gathers a local 3x3 seam-aware
kernel of capture distances. This avoids repeating full-sphere integration for
every atlas texel. Constant incoming radiance reconstructs PI times that light.
Probe records use 16-square octahedral interiors with reflected one-texel
gutters, in a 32-column atlas with two Rgba16f outputs: irradiance and distance
moments. The maximum 576x1152 atlas stays within the portable 2048-side baseline.
Moment B stores solid-angle-weighted back-face coverage; probes with at least
.25 coverage are rejected. Rejected probes are not relocated.

Receiver sampling interpolates visible valid neighbors, normalizes their
visibility-weighted irradiance, and applies a separate confidence gate for
the all-blocked case. Occluded neighbors no longer remain as black light in
the normalization denominator. At field edges, clamped sampling has smooth
distance falloff rather than the old immediate world-box cutoff. The final
provider reads two atlas textures; fully mapped skinned materials fit the
existing twelve-texture capacity. Debug octahedra show gathered light,
uncaptured magenta and rejected orange/red probes after transparency and before
PostPrepare.

Work is bounded by total probes and per-frame whole-probe updates. Lower
spacing can preserve smaller spatial features; larger probe counts cost
memory and longer complete refresh cycles. More updates increase per-frame
capture and convolution work. A cycle takes approximately ceil(total/budget)
frames. No hardware-independent frame-time guarantee or performance win is
claimed from those bounds alone.

### Remaining limits and validation

Sparse valid probes, small emitters and thin geometry can still cause missed
light, leaks or reconstruction artifacts. There is no automatic relocation,
temporal smoothing, multiple-bounce transport, sky lighting, glossy GI or light
baker. Rest-geometry bounds can miss large skinning/deformation excursions.
Captures reuse selected lights and the main-camera shadow atlas, so
camera-fitted shadow coverage may not include every probe-visible surface.
Float-target fallback reduces HDR range and visibility precision.

The production docs build passes on Node 24.22.1, including the new sidebar
page, page/link/anchor checks and Markdown exports. Existing allowed 404s remain
for unstaged playable examples. No example game or browser interaction changed.

All 28 affected native Windows ASan CTests and 36 web-debug CTests pass across
focused runs on Clang 23.1.2, pinned WASI SDK 24 and Node 24.22.1. GPU coverage
includes Vulkan with validation, OpenGL, WebGL, ANGLE D3D11 and WebGPU. Existing
Render3D regressions and bootstrap checks retain their original oracles. New
cases cover bounds/caching/pack reload, budget/lifecycle/hash contracts, exact
wall/floor receivers, constant and directional irradiance, invalid/blocked
probes, partial visibility and continuity across differently lit probes,
dynamic emission/light changes, and mapped posed/deformed skinning with GI.
The latter retains the independent Render3D geometry/normal pixel oracles.

Actual native captures use the same room, camera, direct lights, materials and
exposure in off/on/debug modes, with automatic coverage and default settings.
They were inspected after 160 frames. Exterior padding was reduced to preserve
requested detail within the existing budget. Visibility confidence is weighted
continuously, and near-probe facing is regularized across the receiver-bias
radius. Review also corrected the atlas dimensions for minimum-capability
devices and moved benchmark correctness audits outside the timing interval.
No runtime/compiler/backend or simulation behavior changed.

The new benchmark passes native/web correctness smokes and builds in optimized
unsanitized mode. Controlled local cost measurements follow this source commit;
no timing result is claimed here before measurement. Mobile/other native OSes,
Unity execution, device-loss recovery and large-world performance remain
unavailable local coverage. Work stays on the feature branch, without a PR or
merge to dev.

### Parallel projection follow-up

The first optimized local cost run identified probe updates as the dominant
work. Projection now splits the same 384 rays over sixteen independent groups
before reducing nine coefficients. This adds one small GPU pass per updated
probe, preserves the final sampling layout and adds no CPU readback. The same
seven scoped native ASan checks pass, including analytic light/visibility and
continuity pixels, real bounce, mapped skinning and benchmark correctness.
The same nine scoped browser checks pass on WebGL, ANGLE D3D11 and WebGPU,
including mapped skinning and benchmark smokes. Together with the unchanged
Render3D checks, coverage remains 28 native and 36 web CTests across scoped runs.

On the queried RTX 4080 SUPER / Ryzen 9 7900X machine, optimized Vulkan at
960x720 with this room's 384 probes gives a median whole-frame GPU cost of
0.6461 ms and CPU recording of 0.0890 ms with the default four updates/frame.
Frozen GI is 0.2805 ms GPU; runtime-disabled GI is 0.1464 ms. These are medians
of three 100-frame process medians, with all GPU samples and raw rows retained.
They are scoped room measurements, not a general hardware or game-FPS claim;
recording, renderer submission and GPU intervals are separate.
[Methods, source/binary identities, ranges, raw rows and actual captures](../bench/results/globalillumination-20261101/README.md).
The measured code is ba070987a8dca293f35275275931c2e26180a17a. Subsequent report
changes do not alter it. This branch has not been merged to dev.
