# Forest stress scene

`bench_forest` renders ordinary Render3D mesh/material entities, including the
package's collection, wind deformation, leaf lighting, shadow and HDR passes.
Each tree has separate bark and cutout foliage meshes. Setup is deterministic
and excluded from frame timings; scene traversal and material binding are timed.
All runs are finite and headless. There is no cached direct-draw replacement.

Build the `bench_forest` target in an ordinary optimized configuration to measure
performance. Use the sanitizer build for correctness, rather than timing it.

```powershell
$env:TIDE_HIDDEN = '1'
cmake --build --preset release --target bench_forest
build/release/bin/bench_forest --count 1000 --camera ground --capture forest-ground.png
build/release/bin/bench_forest --count 10000 --camera canopy --capture forest-canopy.png
build/release/bin/bench_forest --count 50000 --camera wide --capture forest-wide.png
build/release/bin/bench_forest --count 100000 --camera wide --frames 100 --warmup 20 --output forest.jsonl
```

The three fixed camera presets inspect the forest from the ride at eye height,
above the canopy, and from a distant overview. The tree grid grows at constant
nominal 5.5 m spacing: 10,000 trees span about 550 m and 100,000 about 1.74 km.
The rendered terrain spans 2.4 km. The camera far plane is 4 km. Trees vary in
position, orientation, height and tint; a winding central ride shifts trees
sideways without silently reducing the requested count.

The procedurally authored fixture has three representations: detailed trees have
108 bark + 184 foliage triangles, medium 42 + 72, distant 8 + 24. The foliage
consists of multiple intersecting cards carrying sprays of small leaves; the
distant mesh retains volume. These are original analytic test assets, rather
than scanned trees or baked impostors. They are generated reproducibly by:

```powershell
node bench/forest/make-assets.mjs
```

Representations are selected **once at setup for the chosen fixed camera**:
detailed below 90 m, medium below 280 m, coarse beyond. This benchmark does not
implement camera movement, dynamic LOD switching or crossfades. `--levels 0`
keeps every tree's detailed mesh as a control; `--wind 0` and `--shadows 0`
provide controls for animation and shadow costs. Four directional shadow
cascades cover receivers to 160 m, with casters submitted within 260 m of the
camera. Distant trees remain lit but do not cast distant shadows. Shared local
material bounds conservatively cover the wind deformation for color and shadows.

The leaf texture is generated once into a mipmapped grid. This demonstrates
ordinary cutout sampling; it does not claim coverage-preserving mip generation
or alpha-to-coverage. Scene-only atmospheric perspective is implemented through
the surface callback before exposure and tone mapping.

`--help` lists the count, resolution, frame, camera and quality controls.
`--quick` is a bounded 64-tree, 320×180 correctness smoke with timing disabled.
It checks exact planted counts, nonempty submitted work, unchanged match state,
and a nonempty completed native image. The independent package fixtures test
wind deformation and leaf lighting numerically and on the GPU.

`--profile` optionally attaches Tide's existing per-view profiler. It clears
totals at the warmup boundary and prints each view's total milliseconds and
arithmetic mean over the requested measured frames. These are diagnostic
attribution intervals with instrumentation overhead, rather than medians or a
performance comparison. The normal path attaches no profiler and adds no
profiling clock reads. Match setup remains outside the view totals; with zero
warmup, first-frame resource initialization is included.

Measured runs print JSONL to stdout and optionally append it to `--output`.
Rows include near/medium/far tree counts, submitted vertex and draw-call ranges,
main-target bark/foliage instance ranges, mean texture-upload bytes, memory,
view/recording and CPU-submission median/p95,
and completed GPU-query median/p95 with sample count. GPU timing is `null` when
unavailable. Native completion/readback is a separate synchronized interval;
web does not report that interval. CPU and GPU intervals overlap and must not be
added or interpreted as shipping FPS. Sanitized/debug runs and software web
rendering are correctness coverage, not hardware performance evidence.

For recorded evidence, supply `TIDE_BENCH_REVISION` and `TIDE_BENCH_MACHINE` in
the measurement shell. Machine identity is operator supplied; the benchmark
does not query the physical adapter. Run performance measurements separately
from builds and other GPU tests, retain repeated runs and report their spread.
