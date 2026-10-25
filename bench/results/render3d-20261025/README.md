# Render3D measurements, 2026-10-25

Measured native Vulkan at fixed **1920x1080**, with 20 warmup and 100 measured
frames per configuration, in three alternating process pairs. Six bounded
groups contain **36 processes, 18 paired rounds, 78 rows and 7,800 completed GPU
queries for 7,800 measured frames**. All processes completed with exit 0, empty
stderr and no detected competing work. Every case's untimed expanded-command
and full native pixel comparison passed; submitted work stayed fixed during
measurement. The batch lasted 99.6877061 seconds, ending
`2026-10-24T23:29:11.8955676Z`. October 25 labels and original UTC timestamps
are retained unchanged.

Hardware is the operator-supplied **Ryzen 9 7900X / RTX 4080 SUPER** machine;
NVIDIA inventory reports driver **617.42**. The measured API is Vulkan.
`adapter.actual` is null: the backend does not expose a physical-adapter query,
and inventory alone is not that query. Other desktop activity was uncontrolled.
**No clocks, power or temperature were recorded or controlled**; monitoring was
disabled in all six groups.

## Scope and provenance

The scene is a controlled grid of **24-vertex, 12-triangle cubes**, with one
additional flattened-cube receiver. Fixed rotation, orthographic projection and
summed squared scale keep continuous projected coverage constant as counts
change. It uses production Render3D PBR, light lists, atlas shadows and HDR
presentation. All requested lights overlap the grid. This is a small static
geometry workload, not a complex-model, animated or real-game capacity claim.

- Measured source: `a1d962c99d93657d23f79ba54b0c836f635905e8`.
- Executable: 1,499,648 bytes, SHA-256
  `9d3ab3ba54b734334b87e9276c331fc1bf97177583ae71802e19ae4d424d1ce9`.
- Mesh SHA-256:
  `071fafffd9372ec2b8ee04769aa8148fef8fdcd15584fbc6dde3b200216aa182`.
- Root confirmed Clang 23.1.2's release host `main.c` compile command includes
  `-O2 -DNDEBUG -std=c17 -Wall -Wextra -Wpedantic -Werror -fno-fast-math
  -ffp-contract=off`. Rows independently record `optimized=true`.

All six recorded benchmark source hashes match the raw Git blobs at the measured
commit exactly; those original bytes are in [sources/](sources/). All six frozen
executables were checked against their recorded hashes at export; binaries/PDBs
are excluded. [manifest.json](manifest.json) records both receipts and every
exported raw/source file's length/SHA-256. These identities pin the supplied
sources and executable, rather than independently proving a complete
source-to-binary correspondence for every engine dependency. A later comment
clarifies the observed single opaque batch; measurements and snapshots remain
those of `a1d962c9`.

`package` includes recurring generated views, ECS/hierarchy lookup, light
collection, shadow setup and recording. **`cached_direct` is a static-scene
submission floor**: it re-records the captured shader bindings, geometry,
transforms, numeric light/shadow data and identical passes through public C
drawing calls, one bulk call per captured compatible batch. Cache creation and
recurring collection/hierarchy work are excluded. Both ways retain the same
shared renderer and GPU work. This is not a dynamic renderer replacement or a
Tide-versus-C language comparison.

## Results

All values below are milliseconds. Each cell is the **median of three process
medians**, followed by the **minimum–maximum process median** in brackets.
`P/F` means package / cached direct floor. Submission columns show their two
medians; [summary.json](summary.json) contains their ranges as well as every
other interval's median, range, p95 and retained-memory range. Intervals are
reported separately: **CPU and GPU are never added**.

The mesh sweep uses exactly one directional light in both shadow settings.
All shadowed cases use one 512-pixel cell, including gutters.

| Cubes | Shadows | CPU record P | CPU record F | CPU submit P/F | GPU P | GPU F | Calls |
| ---: | --- | --- | --- | --- | --- | --- | ---: |
| 100 | Off | 0.03545 [0.0281–0.0371] | 0.00585 [0.00535–0.00795] | 0.05515 / 0.0454 | 0.087232 [0.08712–0.087328] | 0.087264 [0.087216–0.087648] | 4 |
| 100 | One map | 0.04395 [0.0395–0.0527] | 0.00705 [0.0066–0.0083] | 0.0484 / 0.0471 | 0.11744 [0.117344–0.11744] | 0.117392 [0.117184–0.117968] | 5 |
| 1,000 | Off | 0.189 [0.1811–0.20835] | 0.01705 [0.0105–0.0193] | 0.05785 / 0.0551 | 0.09744 [0.097264–0.103552] | 0.097312 [0.097104–0.10424] | 4 |
| 1,000 | One map | 0.31935 [0.28965–0.32315] | 0.0196 [0.0153–0.02695] | 0.0763 / 0.05135 | 0.132432 [0.132224–0.13976] | 0.132384 [0.132352–0.139616] | 5 |
| 10,000 | Off | 1.82285 [1.7553–1.83365] | 0.1138 [0.05875–0.12325] | 0.14435 / 0.12325 | 0.148672 [0.148–0.153504] | 0.148368 [0.147888–0.153552] | 4 |
| 10,000 | One map | 2.9299 [2.88545–2.98685] | 0.15775 [0.1101–0.23365] | 0.1863 / 0.152 | 0.260016 [0.21208–0.299984] | 0.207312 [0.206432–0.211264] | 5 |

The following cases all use 1,000 cubes. `1P`, `8P`, `32P` are overlapping point
lights. Mixed means one directional, two points and one spot: an eight-view
budget accepts one directional map, one spot map and one point's six faces;
the other point remains unshadowed. Six/eight views use a 1536-square atlas with
unchanged 512-pixel cells. Off/on rows within each group have the same lights.

| Group / lights | Shadows | CPU record P | CPU record F | CPU submit P/F | GPU P | GPU F | Calls |
| --- | --- | --- | --- | --- | --- | --- | ---: |
| `light1`: 1P | Off | 0.18885 [0.179–0.18935] | 0.01585 [0.01345–0.02755] | 0.05215 / 0.05085 | 0.103472 [0.103328–0.110064] | 0.10352 [0.103392–0.10352] | 4 |
| `light8`: 8P | Off | 0.18555 [0.18205–0.19765] | 0.0157 [0.0137–0.01575] | 0.0486 / 0.0492 | 0.352864 [0.352832–0.35288] | 0.352704 [0.35256–0.35296] | 4 |
| `light32`: 32P | Off | 0.1927 [0.1839–0.1954] | 0.0173 [0.0161–0.01835] | 0.0643 / 0.06025 | 1.099296 [1.09248–1.133152] | 1.09312 [1.092592–1.094752] | 4 |
| `point`: 1P control | Off | 0.18555 [0.181–0.20285] | 0.0172 [0.0157–0.019] | 0.05405 / 0.05375 | 0.104192 [0.104–0.110432] | 0.103872 [0.103696–0.104336] | 4 |
| `point`: 1P | Six faces | 0.75105 [0.70685–0.76435] | 0.061 [0.0513–0.0671] | 0.1047 / 0.0904 | 0.155936 [0.155136–0.162304] | 0.155424 [0.155152–0.15552] | 10 |
| `mixed`: four lights | Off | 0.21355 [0.1981–0.30595] | 0.02205 [0.02055–0.02365] | 0.07655 / 0.0617 | 0.212192 [0.210304–0.213168] | 0.212288 [0.191808–0.212672] | 4 |
| `mixed`: four lights | Eight views | 0.9585 [0.90035–0.9695] | 0.0816 [0.07225–0.0883] | 0.11365 / 0.0927 | 0.333952 [0.329952–0.334112] | 0.322928 [0.300672–0.332816] | 12 |

The one-point unshadowed control appears independently in `light1` and `point`.
It is **not pooled into six rounds**. Their inactive map budgets are zero and
six respectively, with no reserved views in either.

Actual batching is one opaque batch containing **all cubes and the receiver**,
despite their numeric material differences, plus one caster batch per shadow
view. The other three calls are two clears and presentation: 4/5/10/12 calls
for zero/one/six/eight views, independent of cube count. At 1,000 cubes, submitted
vertices/indices including passes and clears are 36,048 / 72,048 / 252,048 /
324,048 respectively. All warmed rows upload **zero texture bytes**. Captured
resources are two mesh slots, two targets and four texture slots without
shadows/five with views. Memory values include retained renderer/setup caches,
not solely the active case's allocations.

The GPU difference between package and cached direct at 10,000 cubes with the
sun map (0.260016 versus 0.207312 ms) is an observation with identical shader
work and uncontrolled clocks; **no GPU speedup is established**. CPU recording
scales with mesh and shadow-view counts in this static workload; the floor
excludes recurring work and is not evidence that a dynamic game can obtain the
same gap. Light-count GPU results change the shader's actual loop work.

## Tails and synchronized readback

These p95 columns are **medians of three process p95 values**, not a percentile
of pooled frames. `summary.json` retains their process ranges. Completion and
readback copy the existing full 1920x1080 RGBA frame and wait for completion;
that overhead remains separate from CPU recording/submission and GPU queries.

| Configuration | CPU record p95 P/F | GPU p95 P/F | Completion/readback median P/F | Completion/readback p95 P/F |
| --- | --- | --- | --- | --- |
| 10,000 cubes, sun, off | 2.3458 / 0.2934 | 0.151872 / 0.151424 | 2.3117 / 2.2577 | 3.3796 / 3.5489 |
| 10,000 cubes, sun, one map | 3.6702 / 0.4179 | 0.266624 / 0.209984 | 2.2808 / 2.2536 | 3.5699 / 3.4249 |
| 1,000 cubes, 32P, off | 0.2829 / 0.0446 | 1.701248 / 1.634464 | 3.06885 / 3.11645 | 4.398 / 4.639 |
| 1,000 cubes, 1P, six faces | 1.0926 / 0.1285 | 0.15792 / 0.156544 | 2.3796 / 2.3748 | 3.8214 / 3.8313 |
| 1,000 cubes, mixed, eight views | 1.536 / 0.1654 | 0.335488 / 0.325024 | 2.63015 / 2.60865 | 3.9249 / 3.7059 |

Native readback synchronizes every measured frame. These are **isolated frame
intervals, not shipping FPS**. GPU queries start after upfront renderer
preparation/uploads, include on-demand work inside shaded draws and exclude
readback. CPU/GPU overlap. Neither their sum nor sums of interval medians are
reported as a true frame median. Original JSONL contains per-process summaries,
not raw per-frame samples; a pooled p95 cannot be recovered.

## Evidence, validation and reproduction

`raw/20261025-*` preserves every original group/batch JSONL, metadata, stdout,
stderr and NVIDIA inventory file byte-for-byte. `raw/batch-wrapper/` preserves
the historical bounded wrapper and log. `.gitattributes` disables text
normalization for this subtree, protecting raw hashes across checkouts.
Original absolute paths in metadata are historical; the verifier resolves
exported files through the manifest's relative mappings.

This batch ran during an explicitly coordinated quiet window. Each group held
`Local\TideClaudeContinuationBenchmark`; the wrapper did **not** hold one mutex
continuously across all six groups. The first group requested five continuous
quiet seconds, later groups one, overriding the runner's default ninety.
Every quiet guard completed without resets. Polling found no contention but
cannot rule out short processes or unrelated desktop activity. No exclusions,
reruns, timeouts or failed attempts are hidden in this export.

Focused native/web debug benchmark builds and Vulkan/OpenGL/WebGL/served WebGPU
quick checks passed. Optimized native Vulkan/OpenGL quick checks also passed
(two CTests, 2.22 seconds). Quick checks disable timing and use four cubes with
32-pixel cells. Software browser smoke supplies correctness coverage, **not
hardware web performance**. This benchmark-only addition did not rerun full
engine suites; other operating systems, mobile, sanitizers and hardware web
performance are unmeasured here. Earlier package-wide validation is separate
in `notes/render3d.md`.

Verify and reproduce the deterministic tables without running a benchmark:

```powershell
$env:TIDE_HIDDEN = '1'
node bench/results/render3d-20261025/summarize.mjs
# Explicitly regenerate the derived summary after reviewing changed inputs:
node bench/results/render3d-20261025/summarize.mjs --write
```

The verifier checks all raw/source hashes, metadata/exit states and contention,
round order, row multiplicity, optimized/equivalence flags, every frame/query
count, fixed geometry/light/shadow parameters and batching/upload invariants.
It then computes each configuration from exactly three process summaries and
checks that `summary.json` is unchanged.

To repeat measurements, use a separately built executable at the recorded
source, the [benchmark instructions](../../render3d/README.md), fresh labels and
an independently arranged quiet window. These commands keep the runner's
default 90-second guard; do not rerun the historical deadline-bound wrapper:

```powershell
pwsh bench/render3d/run.ps1 -Label new-mesh -Revision '<built revision>' -Counts 100,1000,10000 -Cases unshadowed,dir -Directional 1 -Point 0 -Spot 0 -ShadowMaps 1
pwsh bench/render3d/run.ps1 -Label new-light1 -Revision '<built revision>' -Counts 1000 -Cases unshadowed -Directional 0 -Point 1 -Spot 0 -ShadowMaps 0
# Repeat new-light8/new-light32 with -Point 8 / -Point 32, keeping all else equal.
pwsh bench/render3d/run.ps1 -Label new-point -Revision '<built revision>' -Counts 1000 -Cases unshadowed,point -Directional 0 -Point 1 -Spot 0 -ShadowMaps 6
pwsh bench/render3d/run.ps1 -Label new-mixed -Revision '<built revision>' -Counts 1000 -Cases unshadowed,mixed -Directional 1 -Point 2 -Spot 1 -ShadowMaps 8
```

Each command uses Vulkan, 512-pixel requested cells, 20 warmup/100 measured
frames and three alternating process pairs. Supply `-Machine` for another
machine; do not inherit this PC's operator label. The batch source/input/binary
identities above, rather than the later comment correction or future builds,
are the identities of the saved measurements.
