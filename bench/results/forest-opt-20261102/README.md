# Forest preparation optimization, 2026-11-02

The combined change reduces repeated numeric-list capture work, avoids unused
numeric material loads in binding-only views, and shares callback factory layouts.
Tree assets, placement, camera, geometry tiers, wind, lighting and shadow settings
are unchanged. Three 1920x1080 captures have exactly identical RGBA pixels.

Windows, Ryzen 9 7900X / RTX 4080 SUPER driver 617.42, Clang 23.1.2, ordinary
release preset (-O2 without sanitizers), Vulkan 1920x1080. Three alternating pairs
per case, 20 warmup/100 measured frames per process: 18 processes and 1,800
measured frames with 1,800 completed GPU queries. Pair order is before/after,
after/before, before/after. No profiling flag in these measurement runs.

| Trees / camera | Visible | Calls | CPU record before / after ms | Median paired reduction | GPU before / after ms |
|---|---:|---:|---:|---:|---:|
| 10,000 / canopy | 8,262 | 24 | 16.16 / 14.82 | 9.4% | 13.32 / 13.46 |
| 100,000 / wide | 96,479 | 6 | 151.09 / 118.72 | 17.6% | 36.64 / 34.82 |
| 100,000 / ground | 78,286 | 24 | 148.35 / 129.58 | 16.2% | 13.45 / 12.93 |

Absolute times are medians of three process medians. Reductions are the median
of three paired ratios, so they differ from ratios of the displayed medians.
CPU/GPU overlap; neither their sum nor these synchronized readback frames is
shipping FPS. No GPU shader-speedup claim is made. Large-case clocks and desktop
activity varied; full ranges and every pair ratio are retained in summary.json.

| Trees / camera | CPU before range | CPU after range | GPU before range | GPU after range |
|---|---:|---:|---:|---:|
| 10,000 / canopy | 16.10–16.62 | 14.64–15.41 | 11.97–13.75 | 12.81–14.74 |
| 100,000 / wide | 144.09–159.16 | 114.56–128.24 | 32.20–44.54 | 30.64–36.76 |
| 100,000 / ground | 134.50–164.94 | 112.74–145.60 | 11.97–15.82 | 12.00–14.96 |

The 100k overview selects coarse trees and has no tree shadow casters because
its high camera is outside the existing 260m caster budget. Canopy/ground exercise
nearby casters and detailed foliage. This budget is identical in each pair;
no view distance, tree count, geometry level or shadow quality was reduced.

summarize.py checks exact paired settings, representation counts, world hashes,
main bark/foliage counts, draw/vertex submission ranges, zero warmed texture
uploads and complete frame/query counts. pixel-equivalence.json records decoded
RGBA hashes for 10k canopy, 100k ground and 100k wide at the same fixed timestep.
Capture PNGs remain under ignored build/forest-opt/captures-before and
captures-after. Each capture uses 2 warmup + 4 measured frames; the final frame of each case
is compared. This is not a proof of every camera/animation state; moving-wind,
shadow and material regression fixtures cover other controlled states.

All 2,908 native Windows ASan CTests and 2,794 WASI/browser Release CTests pass,
including Vulkan/OpenGL, WebGL, ANGLE D3D11 and WebGPU. validation.json pins
the complete ignored logs and stores suite summaries beside this report. Other
native OS/mobile/Unity and hardware-web performance remain untested locally.

The runner used the existing Local\TideClaudeContinuationBenchmark mutex and
5-second initial quiet guard. It waited for all task builds/tests to finish.
No per-process contender or clock monitor was used, and other desktop activity
remained uncontrolled. All completed pairs are retained; no attempt was excluded.

Scope and attribution:

- The draw list keeps eight frame-local flat numeric-list bindings after checking
  current source page hashes, layout/writer, and captured texture identity. Dirty,
  scratch, asset-backed and chunked lists retain the general path. Source mutation,
  COW, lifetime/reuse and slot lifecycle regressions compare to fresh conversion.
- Non-drawing views may use existing partial lane loads even for shader component
  types. Drawing views retain complete implicit shader inputs; all prior correction,
  interpolation and ownership guards remain. A new fixture checks both shader
  capture and attached-field writes in blended/current frames.
- A single factory per forest callback hook removes duplicate capture variants.
  Surface and wind calculation bodies stay unchanged.
- --profile supplies optional per-view diagnostic totals. Profiling files here
  include overhead; the sampler also includes setup/hash/teardown and system waits.
  They identify candidates, not isolated speedups or parts to add to paired data.

The initial instrumented frame attributes roughly 85 ms of 104 ms recording to
DrawMesh, 10 ms to Cast, 5 ms to BindForest and 3 ms to transparent collection.
Fresh later diagnostics vary with the machine and are not paired measurements.
The combined comparison does not isolate the contribution of each optimization.
Per-entity material/resource preparation remains a substantial 100k CPU cost.

A forced-inline wrapper hypothesis was explored only in ignored generated C;
the normal build regenerated that edit, so no valid inline measurement was
collected and no forced-inline heuristic is shipped.

Provenance/reproduction: metadata.json records parent 1f825bbe5, both frozen
binary SHA-256s and optimized LF-normalized production-source SHA-256s. Build
the parent and this change with the same release configuration, preserving each
checkout-built executable as build/forest-opt/{before,after}/bench_forest.exe.
Then run from the repo root with no competing work:

```powershell
$env:TIDE_HIDDEN = '1'
& bench/results/perf-pass/gate.ps1 powershell -NoProfile -ExecutionPolicy Bypass -File bench/results/forest-opt-20261102/run-pairs.ps1
python bench/results/forest-opt-20261102/summarize.py
```

Archive existing result files or select a fresh result directory before repeating;
the benchmark appends JSONL and the verifier expects exactly 18 processes. Raw
process stdout logs are retained as PowerShell originally wrote them. No binary
or private art is committed. Earlier unpaired forest-scale results remain in
forest-20261101 and are not used as this comparison baseline.
