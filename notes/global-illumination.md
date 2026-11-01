# Global Illumination package

## Separate automatic-coverage implementation (2026-11-01)

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

The first capture image is radiance. The second has R normalized radial
distance, G one-sided geometric back-face flag, B geometry-hit flag and alpha
one. Empty black B=0 means far/unoccluded; this preserves nearby distance
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
