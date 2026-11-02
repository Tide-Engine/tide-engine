# Global illumination workload

`bench_globalillumination` draws the optional GlobalIllumination package through
production Render3D shaders. The default `--scene room` retains the original
seven meshes, colored walls, white box/sphere, emissive ceiling panel and one
shadowed point light. No simulation ticks run during measured frames. Geometry,
camera, exposure and light inputs are identical across GI modes.

`--scene district --buildings N` repeats that room on a grid with 24-unit pitch,
for 1..256 buildings (64 by default). Every building has seven meshes and its
emissive panel; only the primary room has the point light. There is no enclosing
floor spanning the empty space. The default camera still frames the primary
room, so adding distant buildings tests local coverage and scene traversal
without changing the nearby receiver or its direct lighting.

The host applies workload options before the first simulation tick. Construction
runs once on that tick, and enabled GI runs require the observed mesh count to
equal `7 * buildings` (seven for the room scene). A requested building count is
therefore checked against actual scene content rather than trusted as a label.

Two continuous architectural scenes extend beyond the separated-room workload:

- `--scene atrium`: a 48 by 72 by 18 unit hall with 18 columns, nine curved
  arch ribs, elevated side galleries, sloping ramps and colored end panels.
  Its 216 touching four-unit floor slabs make the scale visible. The independent
  inventory is 315 mesh instances.
- `--scene courtyard`: a 160 by 120 unit plaza with perimeter facades, curved
  arcades, covered bridge, ramps, broad stairs, planters and sculptures. The
  300 touching eight-unit floor slabs contribute to a total of 476 meshes.

Both use a real directional sun and six shadowed spotlights, with zero ambient
irradiance. A fixed directional shadow volume covers the architecture; the sun
uses one map and each spotlight one, for seven maps total. Hero cameras keep
their shaded display-wall receiver within the ordinary 48-unit resident range.
Neither scene authors probes or baked light. These are static examples;
`--travel` and `--moving-light` are rejected for them.

Outside quick checks, these two examples request 2048-square shadow cells with
`shadowBias = 0.001` and `shadowSlopeBias = 0.004` to avoid self-shadow striping
across their broad fixed sun volumes. The atlas/device limit can reduce the
actual cell size when packing seven maps. Quick checks retain 32-square cells.
Reported whole-frame costs include these shadow passes and their resources;
they do not isolate GI cost.

The closed, outward-wound column and semicircular arch OBJ meshes are authored
here and reproduced by `python bench/globalillumination/generate_meshes.py`.
`--check` verifies the exact asset bytes, opposite edge incidence and positive
enclosed volume. No downloaded environment or generated raster image is used.

Additional options:

- `--probes N` selects the physical capacity, 125..2048 (default 512), preserving
  the package's production defaults and bounds. A higher cap changes allocation
  quality and initialization work; compare it with an identical scene/camera.
- `--overview` frames the whole district from above and requests a larger GI
  view distance of `2 * extent + 16`, where `extent` is the larger grid dimension
  times 24 world units. The elevated camera otherwise has no nearby geometry
  within the ordinary 48-unit working set. This is a different coverage workload;
  the nearby and travel cameras retain the production default. Atrium and
  courtyard overviews request view distances 200 and 350 respectively, covering
  their full scene from the elevated cameras. Their hero views retain 48.
- `--debug` draws the actual resident relocated probe positions. Large-scene
  markers grow with camera distance to stay visible in the overview, using the
  same package shader and depth test; this changes only visualization size.
- `--travel` follows the district's serpentine building order at 12 units per
  second, stopping at the last building. It requires the district scene and
  cannot be combined with overview.
- `--moving-light` moves the primary room's point light and varies its intensity.
  The light lives in local rendering state; animation never changes match state.

Travel and light animation advance by exactly 1/60 second per warmup/measured
frame, independent of machine speed. Both start at time zero after convergence.
Remote buildings remain useful receivers and occluders even with a still camera.
These workloads exercise separated interiors, residency and lighting changes;
they do not represent every large world or moving/deformed geometry workload.

Modes are `off`, `frozen`, `update1`, `update4` and `update16`. Update numbers are
maximum captures per frame; unchanged regions can perform no captures. Each mode
starts with GI disabled in timed runs, checks its own scene's shaded receiver,
fills the requested view's resident field with sixteen updates per frame, then applies
the requested budget. Defaults are twenty warmup and one hundred measured
frames at 960x720. No authored GI volume is used. Frozen mode performs no further
captures; travelling beyond initialized coverage can therefore expose uncaptured
regions rather than generating new lighting.

Build and run correctness checks first:

```powershell
$env:TIDE_HIDDEN = '1'
cmake --build --preset sanitize --target bench_globalillumination
ctest --preset sanitize -R '^bench_globalillumination' --output-on-failure
cmake --build --preset web-debug --target bench_globalillumination
ctest --preset web-debug -R '^bench_globalillumination' --output-on-failure
```

Quick checks use 128x96, one warmup and two measured frames per mode, with timing
disabled. Quick runs without overview, travel or moving light reuse the
once-filled, validated resident field between budget modes. Timed runs and those
moving/view-changing quick cases retain independent cold starts. They retain
real capture/convolution shaders, automatic coverage, all
five budgets, unchanged match hashes and a rendered white-box witness that must
brighten by more than fifteen summed RGB levels with GI. Room/district witnesses
use the original camera. Each large scene instead projects its own shaded +Z
display-wall face through its own hero camera, with that scene's sun/spots.
Validation uses still lighting outside measurement, including when
overview/debug/travel is requested. Field
accounting and capture budgets are checked; frozen captures must remain zero,
and static live modes must demonstrate an idle capture frame after convergence.
Every shader retains the twelve-texture limit. Existing muted WebGL/WebGPU
runners supply browser coverage; the larger scenes add scoped WebGPU checks
under the existing 240-second runner. Software-GPU timings are not performance data.
Value options accept both `--scene atrium` and `--scene=atrium`, including
browser queries such as `?quick&scene=atrium&mode=update4&probes=512`.

For bounded native checks of the additional cases:

```powershell
build/sanitize/bin/bench_globalillumination.exe --quick --scene district --buildings 64
build/sanitize/bin/bench_globalillumination.exe --quick --scene district --buildings 64 --overview --debug
build/sanitize/bin/bench_globalillumination.exe --quick --scene district --buildings 64 --travel --moving-light
build/sanitize/bin/bench_globalillumination.exe --quick --scene atrium --mode update4
build/sanitize/bin/bench_globalillumination.exe --quick --scene courtyard --mode update4
```

Measure an optimized unsanitized build after other builds and GPU tests have
ended. The shared gate supplies five quiet seconds and serializes cooperating
benchmark processes:

```powershell
cmake --build --preset release --target bench_globalillumination
$env:TIDE_BACKEND = 'vulkan'
pwsh -NoProfile -File bench/results/perf-pass/gate.ps1 `
    build/release/bin/bench_globalillumination.exe `
    --scene district --buildings 64 --mode update4 `
    --frames 100 --warmup 20 --output build/gi-district-cost.jsonl
```

For reported comparisons, coordinate a quiet window, repeat adjacent process
runs in alternating mode/count order, retain source/binary identities and every
raw row, and record competing work. The gate does not monitor contention after
launch. Use separate `--mode` processes for controlled comparisons. Compare
1/64/256 buildings with a fixed camera first; overview, travel and moving-light
results have different work and should be identified separately. Static live
results can be idle-cache costs; the actual capture counters distinguish them
from changing-light or refill work. There are no timing assertions or universal
speed claims.

`--capture final.png` saves the actual final native frame after measured samples.
For a probe overview or a traversal ending near the second building:

```powershell
build/release/bin/bench_globalillumination.exe --scene district --buildings 64 `
    --mode update4 --overview --debug --frames 1 --warmup 1 --capture build/gi-district-probes.png
build/release/bin/bench_globalillumination.exe --scene district --buildings 64 `
    --mode update4 --travel --frames 120 --warmup 1 --capture build/gi-district-travel.png
build/release/bin/bench_globalillumination.exe --scene atrium --mode update4 `
    --probes 512 --frames 1 --warmup 1 --capture build/gi-atrium.png
build/release/bin/bench_globalillumination.exe --scene courtyard --mode update4 `
    --probes 2048 --overview --debug --frames 1 --warmup 1 --capture build/gi-courtyard-probes.png
```

JSONL separates CPU recording, CPU renderer submission, native completion/readback
and asynchronous GPU queries. CPU recording includes scene maintenance, probe
scheduling and ordinary Render3D views. GPU intervals include main, shadow and
probe passes, convolution and presentation; scopes differ by backend. Unavailable
GPU median and p95 are reported; unavailable GPU timers are null with an
explicit sample count. Verify complete intended sample counts before reporting
GPU costs.

Rows report `gi_scene_instances`, scene options, active probe/block counts, fixed probe capacity,
remaining dirty probes, finest resident spacing and spacing at the fixed primary
floor witness. Primary spacing zero means that point has no resident block.
Requested update budget is separate from actual mean captures, capture faces,
capture candidate instances and submitted capture draws. They also include total
draws/vertices/texture-upload bytes, idle capture frames and maximum shader
textures. Actual work can change as probes and camera regions change.
`gi_scene_instances` counts the package's observed mesh/skinned instances and
must equal `7 * buildings`, 315 for atrium or 476 for courtyard when enabled;
GI-off rows report zero because package
scene bookkeeping is disabled. Spacing fields report the actual allocation;
the requested finest spacing is not a guarantee for the primary receiver.

`setup_record_ms` is the first enabled validation frame's CPU recording, after
asset loading and the GI-off reference frame. `convergence_record_ms` sums CPU
recording across validation and requested-view fill frames. These are separate
from warmed medians and do not include asset loading or GPU convergence time.
Match hashes, shader audits, convergence, receiver readbacks and final allocation
scans are outside measured intervals. If a requested native capture fails the
receiver brightness check, a diagnostic frame is saved at the capture path and
the process still fails; there is no validation bypass. Native full-frame readback synchronizes
frames. CPU/GPU intervals overlap; never add them or convert these measurements
to shipping game FPS. Retained GPU memory can include resources from prior modes.
