# GPU-resident clip experiment

This is an opt-in benchmark, not a new engine animation API. It uses existing Tide vertex shaders, shader-readable lists and ordinary mesh instancing. Default `bench_skinned` runs retain the original four modes and exact CPU Pose behavior. No engine/compiler/runtime code, deterministic pose math or match state changes.

`--modes gpu` samples the unchanged fixture's one-second clip into 60 bone palettes once during untimed setup, keeps the 245,760-byte table in local storage and a retained GPU data texture, then interpolates adjacent palettes in the vertex shader. Each character has an independent phase `seconds + index/count`, including arbitrary fractional phases; this does not quantize all characters into 60 shared poses. Four joint indices/weights are copied from canonical imported influences into a benchmark-specific vertex struct. Positions, normals, colors, indices, 3,072 source vertices, 6,048 triangles, outer transforms, viewport, lighting and nominal screen coverage match the reference. The shader uses the ordinary skinned stage's cofactor normal transform. The fixture has exactly four influences per vertex, 64 palette bones, one continuous one-second loop and unit total weights; this is deliberately not a general importer or playback system.

Linear interpolation of baked **final matrices** approximates the authored local-TRS/quaternion interpolation and hierarchy evaluation. It does not retain arbitrary procedural pose edits or IK. CPU gameplay bone queries remain on the existing exact path. Multiple clips/rigs, transitions, procedural overrides, hot reload and resource residency policy are future design/work, not capabilities added here. No culling or LOD is applied.

## Measurements

Ryzen 9 7900X / RTX 4080 SUPER / NVIDIA 595.79 / Windows / Clang 23.1.2. Same frozen release `-O2` binary supplies the reference and experiment. Three pairs alternate CPU/GPU, GPU/CPU, CPU/GPU, after a shared mutex and 90-second quiet guard; competing builds/tests/benchmarks are checked throughout. Each process runs 1K then 10K characters, 20 warmup and 100 measured frames, repeat 1. `--subframe 0.5` exercises rendering halfway between fixed ticks on both paths. Every one of the 12 rows has all 100 GPU samples. Initial baking, geometry/data uploads and shader setup are outside the warmed frame timings, as with the reference benchmark.

Values are medians of three process medians. Speedups are medians of paired ratios, which need not exactly equal the ratio of rounded aggregates.

| Characters | CPU view recording, exact → GPU clip (ms) | Paired recording speedup | CPU submission, exact → GPU clip (ms) | GPU execution, exact → GPU clip (ms) | Synchronized total, exact → GPU clip (ms) |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 2.457 → 0.219 | 11.04× | 0.448 → 0.0076 | 1.159 → 0.806 | 4.398 → 1.337 |
| 10,000 | 25.053 → 2.369 | 10.58× | 6.134 → 0.0322 | 13.693 → 7.116 | 43.592 → 9.970 |

At 10K, median paired submission and synchronized-total speedups are 184.40× and 4.37×; GPU interval speedup is 1.92×. GPU queries include on-demand uploads and changed batching, not isolated shader arithmetic. CPU/GPU intervals overlap, and readback totals do not establish pipelined FPS. Median GPU-clip p95s at 10K are recording 2.700 ms, submission 0.0463 ms, GPU 7.618 ms and synchronized total 10.485 ms; these are process-p95 aggregates, not pooled frame p95s.

Warmed palette uploads fall 4.096 MB/frame→0 at 1K and 40.96 MB/frame→0 at 10K. Every measured experiment frame asserts zero texture uploads and two calls including clear. The reference uses two calls at 1K, eleven at 10K; the resident table removes palette-page batch boundaries. This combines removal of CPU pose/palette work, per-frame uploads, fewer draws and improved palette reuse on the GPU. The experiment does not isolate their individual contributions. Source triangle count stays **60.48 million/frame at 10K**. Upload accounting covers textures, not instance/geometry buffers; character instance parameters are still transferred. One-time local table/vertex/index preparation measures approximately 0.64 ms at 1K and 0.57 ms at 10K, independent of character count, excluding renderer upload/pipeline setup.

[Raw rows](rows.jsonl), [summary](summary.json), [metadata](metadata.json) and [GPU monitor](gpu.csv) retain exact values, p95s, source/binary hashes, options, process exits, empty stderr sizes and whole-run clocks. GPU monitoring spans captures/reference validation too; it is not per-case clock normalization. The new labels append to `bench/results/skinned.jsonl` without changing its history. Previous scope-pass timings used zero subframe and a different compiled source layout, so the primary comparison is the matched reference here.

## Validation and visual difference

Full warning-free builds and suites pass **2,079 native** and **2,050 web** CTests. The new GPU clip test runs on OpenGL, WebGL and WebGPU with a half-tick rendering offset, asserts warmed residency/batching, and checks a CPU reference for every vertex at 240 quarter-tick phases including the loop seam. Maximum model-space position error is **0.001320204**; maximum unit-normal vector difference is **0.00160562643**, below the explicit 0.01 bounds. Native and web report the same maxima. This bounds the approximation for this fixture and those sample phases; it is not a universal bound for arbitrary animations or direct per-vertex GPU readback. Existing deterministic and GPU palette/pixel suites also pass unchanged.

One-character final half-tick captures were inspected visually and compared in RGB: **123 / 589,824 pixels differ**, just **two** by more than two color levels. Maximum channel difference is 127 at an edge; mean absolute channel difference over the whole image is 0.0004504. These are approximate results, not bit-identical images, and whole-image means include background. [Image comparison](image-comparison.json) retains capture hashes and exact metrics. Images and executable/full process logs stay in `build/gpu-clip`.

No Linux/macOS/MinGW/Android/sanitizer run or CI dispatch was performed. Software browser runs establish compatibility/correctness checks, not web hardware performance. This simple low-amplitude tube clip is not representative of every humanoid motion, blend or IK workload.

## Reproduction

Build `bench_skinned` with the release preset in this checkout. Run `bench/results/gpu-clip/run.ps1` from PowerShell; it sets `TIDE_HIDDEN=1`, snapshots this checkout's binary, uses hidden bounded children and records all attempts. Use unique save labels for a new experiment; `analyze.mjs` intentionally validates the original 12 rows. Run `node bench/results/gpu-clip/analyze.mjs` with Node 24.16+ and `python bench/results/gpu-clip/compare-images.py` with Pillow. The scripts reproduce timing summaries and the capture comparison. CTest invokes the untimed dense reference check through `--verify-gpu`; a hardware performance run omits that flag. This report demonstrates that a GPU-resident visual path is worth further development, while keeping the exact CPU/IK-capable pose path available.
