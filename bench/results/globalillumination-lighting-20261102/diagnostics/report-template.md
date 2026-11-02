# GI coverage and capture-plane repair — 2026-11-02

Source: `__SOURCE__`, on `feat/render3d-gi`. This work stays off `dev`.
It repairs the lighting of the existing atrium and courtyard without changing
their geometry, materials, direct lights, camera or exposure. GI remains an
optional package depending on Render3D.

The repair retains distant visible receivers in the bounded adaptive field,
uses one physical metric for shared probes across coarse/fine bricks, excludes
black support behind the receiver's geometric plane, and reconstructs capture
depth from each triangle's plane. It adds no ambient light, sky, extra bounce,
capture resolution, rays or probe capacity. The final two-atlas field and
twelve-texture mapped material path remain.

## Matched visual evidence

The `captures/` directory contains sixteen actual Vulkan renderer PNGs and their
unabridged JSON/log records: two scenes × two cameras × off/512/2048/probes.
The probe overlay uses the 2,048 cap. PNGs are retained without image edits.
[The comparison](lighting-comparison.html) can compare the repair against
[the previous captures](../globalillumination-large-20261102/README.md) or GI
disabled, with matching camera and budget. Probe-placement overviews are also
available in the comparison.

__CAPTURE_RESULTS__

The atrium is 48×72×18 units with 315 meshes; the courtyard is 160×120 units
with 476 meshes. Each has one sun and six shadowed spots, zero ambient, exposure
.85 and the same nonquick 2,048-square shadow-cell request. Hero cameras request
48 units of nearby detail; overviews request 200/350. The current repair also
retains coarse support through the conservative camera frustum. All fifteen
rear-wall witnesses per scene are evaluated outside timed frames, as well as
the original shaded-wall witness. These regional checks do not establish
path-traced agreement for every pixel.
The `regional_*` JSON fields always describe the fixed hero validation camera,
including overview/debug invocations; final overview residency is a different
working set.

## Why the capture representation changed

The 48×24×64 uniform-emitter enclosure exposed a black floor sample on web,
despite complete captured support and correct captured radiance. Four raw
depths were about 5% short. Native/WASI recording parity and actual GPU traces
confirmed matching cameras, vertex/instance bytes, uniforms, 32×32 viewports
and RGBA16F images. A tiny plain WebGL program reproduced the shortened
positions without Tide, instancing or texture sampling. Changing the near plane
did not correct it; reconstructing from raster depth also failed.

The independent plane proof recovers the correct ray/plane distance while the
interpolated point remains displaced along the plane. Intermediate capture
metadata therefore stores perpendicular plane distance and an octahedral
geometric normal, with alpha distinguishing front/back/miss. Each ideal ray is
intersected before inverse-depth interpolation, retaining the physical radial
far limit. The normal and distance use the same stored normal on an exactly
FP16-representable coordinate grid. Derivatives precede cutout discard.

The final distance atlas and receiver tolerance remain unchanged. Permanent
floor and oblique raw-plane witnesses independently intersect the authored
planes. The original 64 enclosure samples retain their five-byte tolerance.
The controlled outside-range shell also exercises axial-inside/radial-outside
cube corners. Diagnostic sources, raw failures and rejected candidates are
retained under `diagnostics/`; no failed diagnostic is a validation pass.

Convolution also reconstructs fixed quadrature weights analytically. For face
vector V with squared length L, normalized direction times solid angle is
V/L². This removes one geometry-texture read and its FP16 rounding per ray;
the same 384 sample positions, positive cosine kernel, normalization and
backface exclusion remain. It is not a measured active-update speedup claim.

## Validation

__VALIDATION__

Native validation uses Windows ASan, Vulkan validation and OpenGL. Optimized
web validation uses the muted WebGL/Direct3D11/WebGPU runners and unchanged
quality assertions. Empty/skip results fail
the collecting script. The shared cross-worktree mutex is released after each
test. These correctness durations are excluded from performance claims.

Web enclosure coverage is split into 512/2,048-capacity processes. Its unasserted
scene backdrop uses a smaller HDR target; capture faces, captured probes/rays,
convergence updates, the full-size 4×4 witness pixels and every lighting/depth
oracle stay fixed. Ordinary room/material pixel integration and all benchmark
render resolutions remain unchanged. The combined and full-backdrop timeout
attempts are retained, rather than reported as passing.

The new maximum-capacity WebGL/D3D stress tests have explicit 300-second runner
budgets and 330-second outer guards. Existing WebGL/D3D defaults remain 120;
served WebGPU remains 100. The maximum case completed all 32 lighting checks
in a separate 188.65-second software-WebGL diagnostic before this registration
change. That diagnostic is not a pass under the old 120-second limit.

The earlier web-debug regional run reached its 120-second guard without a
returned oracle result. It is not reported as passing. A separate
hardware ANGLE diagnostic also timed out inconclusively; no hardware-web or
software-only attribution is claimed. Other native OSes, mobile and hardware
web devices were not validated in this Windows worktree. No CI was dispatched.

## Performance

__PERFORMANCE__

All capture JSON timing fields are uncontrolled and excluded from performance
claims. Cold setup, convergence, settled CPU recording and whole-frame GPU
time are different quantities; CPU/GPU overlap and synchronous readback do not
describe pipelined FPS. The previous large-scene and adaptive-grid cohorts are
different workloads and are not used as before/after timing controls here.

## Remaining limits

This is single-bounce diffuse probe GI with no sky source. Coarse support and
low-resolution captures still approximate lighting and occluders; grazing
geometry and quantized normals remain precision limits. More probes do not
guarantee monotonic brightness or uniform quality everywhere. The working set
is finite: nearby geometry plus the padded conservative camera frustum, at most
128 blocks and the selected physical probe cap. Cold topology work and newly
encountered distinct mesh caches can spike; their cost is not bounded by probe
count. These static architectural examples do not establish moving-scene or
many-complex-asset scalability.

Raw inputs, commands, binary/source hashes, validation logs and capture analysis
are retained beside this report. `manifest.json` pins the retained evidence.
