# Larger continuous GI examples

Actual Render3D/Vulkan captures of two new static architectural workloads:

- Atrium: 48 x 72 x 18 units, 315 mesh instances, columns, curved ribs,
  side galleries and ramps.
- Courtyard: 160 x 120 units, 476 mesh instances, arcades, a covered bridge,
  ramps/stairs, planters and sculptures.

The implementation source is a5bb2b83c71033942df3ef7a7556655d35930eb9 on `feat/render3d-gi`.
No production GI/Render3D code or defaults changed, and this work is not on dev.
The authored closed column and arch meshes have a byte/topology checked generator;
all probe coverage and positions are automatic. There is no bake.

## Captures and allocation

The [capture gallery](gallery.html) switches cameras, budgets and probe markers.
All sixteen [captures](captures/) retain the PNG, successful process log and raw
JSONL row. Names identify scene, `hero|overview`, and `off|512|2048|probes`.
Each on/off pair has identical geometry, camera, direct lights and exposure.
Probe images use the 2048 cap and actual relocated active capture centers;
marker sizes are enlarged with distance for visibility. Debug drawing is not
representative GI rendering cost.

| Scene | View | Cap | Active probes | Blocks | Shaded-wall spacing |
|---|---|---:|---:|---:|---:|
| atrium | hero | 2048 | 1221 | 123 | 6 |
| atrium | hero | 512 | 464 | 46 | 12 |
| atrium | overview | 2048 | 1009 | 128 | 6 |
| atrium | overview | 512 | 483 | 64 | 6 |
| courtyard | hero | 2048 | 1202 | 128 | 6 |
| courtyard | hero | 512 | 511 | 60 | 12 |
| courtyard | overview | 2048 | 1050 | 127 | 12 |
| courtyard | overview | 512 | 512 | 63 | 12 |

Capacity is a ceiling. The independent 128-block limit and geometry support
also constrain refinement, so requesting 2048 does not necessarily allocate
2048 probes or improve every receiver. Spacing is measured at the scene's
shaded display wall, not claimed uniformly across the world.

Hero cameras use the production 48-unit residency distance. Remote visible
geometry can therefore have direct lighting only. Whole-scene overviews request
200/350-unit residency and change the allocation workload. Higher capacity can
also remove incidental coverage from an oversized coarse cell; it does not
guarantee monotonic brightness at every pixel. Large scenes still expose dark
regions, reconstruction seams and visibility artifacts. There is no sky light,
ambient fill or multi-bounce transport to conceal those limits.

The examples use one shadowed sun plus six shadowed spots. Nonquick shadow
cells request 2048, bias .001 and slope bias .004; backend atlas limits may
reduce the cell size. These example settings remove severe broad-volume shadow
acne seen during initial inspection. Whole-frame resource/cost fields include
all direct lighting and shadows. They are not GI-only overhead.

## Validation

[Native ASan checks](validation/native-tests.log): 6/6, covering retained room
budgets and both larger scenes on Vulkan validation and OpenGL.
[Web-debug checks](validation/web-tests.log): 4/4, covering retained room
budgets on WebGL/WebGPU and both larger scenes on WebGPU. The mesh generator's
`--check` passes. The sixteen optimized native capture processes also pass.

Checks retain each new scene's independently counted 315/476 meshes, its own
shaded-wall brightness increase (>15 summed RGB levels), convergence, exact
requested capacity, bounded captures, unchanged match hashes and twelve shader
textures. No validation bypass or unrelated-room witness is used. Other native
OSes/mobile and large-scene WebGL remain unavailable local coverage.

## Timing status and reproduction

**No new controlled timing measurement was performed.** Other checkouts kept
building during the bounded 90-second quiet-guard attempt. No measurement child
started, and its `finally` released/disposed the shared mutex at
01:47:41.681 UTC. [The guard log](measurement-gate.log) preserves that result.
Timing fields in the sixteen capture rows are uncontrolled and must not be
aggregated, plotted as performance evidence or converted to FPS. Previous
[adaptive district timings](../globalillumination-adaptive-20261102/README.md)
measure a different workload and shadow configuration.

[metadata.json](metadata.json) records source/binary identity and the explicit
zero-process/zero-sample measurement status. The included capture script
reproduces all views after building this source (its destination must be new).

The prepared `run-large.ps1`/`summarize-large.py` protocol is included for a
future quiet window; it was syntax/synthetic validated but did not run against
the renderer in this attempt. It would run 18 serial processes: two scenes x
off/512/2048 caps x three alternating-order trials, 20 warmup and 100 measured
frames. It checks all GPU samples, static idle captures, exact scene counts,
hashes, brightness, source and immutable build inputs, retaining failures.
Use a new report directory, a clean checkout and one outer shared gate; never
nest gates. This prospective run records the current clean source SHA. To
repeat the exact captured implementation, copy both driver files to an ignored
directory before checking out the source SHA above (the evidence commit follows
that source commit), then invoke that saved copy instead:

```powershell
$env:TIDE_HIDDEN = '1'
cmake --build --preset release --target bench_globalillumination
pwsh -NoProfile -File bench/results/perf-pass/gate.ps1 pwsh -NoProfile -File `
    bench/results/globalillumination-large-20261102/run-large.ps1 `
    -SourceSha (git rev-parse HEAD) `
    -ReportDirectory build/gi-large/future-controlled-results
```

CPU/GPU intervals overlap, and synchronized native completion/readback is not
pipelined game FPS. Initialization/probe refresh and camera travel have costs
outside a settled static cache window. These examples make no new performance
or production-readiness claim.
