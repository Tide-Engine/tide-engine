# Forest stress, 2026-11-01

Windows, Ryzen 9 7900X / RTX 4080 SUPER (driver 617.42), Clang 23.1.2,
ordinary optimized release preset, Vulkan, 1920x1080. Production Render3D
entities/passes, wind and thin-leaf lighting; 1024px requested shadow cells.
Three independent processes per case, each 20 warmup + 100 measured frames.

Values below are medians of three process medians; brackets retain their range.
CPU recording and GPU intervals overlap: do not add them or infer shipping FPS.
Native readback synchronizes each frame and is a separate interval in the raw rows.

| Trees / camera | Visible trees | Calls | CPU recording ms [range] | GPU ms [range] |
|---|---:|---:|---:|---:|
| 1,000 / wide | 988 | 20 | 2.55 [2.48, 2.64] | 5.04 [5.02, 6.88] |
| 10,000 / canopy full detail | 8,262 | 14 | 16.69 [16.56, 17.01] | 12.61 [11.98, 12.84] |
| 10,000 / canopy | 8,262 | 24 | 16.05 [16.02, 16.21] | 9.49 [9.47, 9.59] |
| 10,000 / wide | 9,760 | 12 | 11.15 [11.08, 12.09] | 6.36 [6.11, 8.33] |
| 50,000 / wide | 48,334 | 6 | 48.51 [48.40, 75.87] | 22.73 [17.67, 23.06] |
| 100,000 / ground | 78,286 | 24 | 164.02 [162.02, 171.20] | 31.96 [20.36, 66.25] |
| 100,000 / wide | 96,479 | 6 | 113.70 [100.26, 142.87] | 36.67 [32.70, 37.70] |

All 21 process rows pass summarize.py: 2,100 measured frames and 2,100 completed
GPU queries, unchanged match hashes, positive independently counted main-target
bark and foliage instances, and zero warmed texture-upload bytes per frame.
The visible counts are main-target instances of each mesh kind, not necessarily
unoccluded trees; source index submissions include both main and shadow passes.

50k/100k wide are distant-canopy and CPU recording stress: the high camera puts
every tree beyond the 260m caster cutoff and selects the coarse representation.
Four shadow views remain configured but contain no tree casters. The 10k canopy
and 100k ground cases exercise nearby casters and detailed foliage; receiver
shadows stop at 160m. Geometry tiers are selected once for the fixed camera.

At 10k canopy, levels reduce index/vertex submissions from 11,944,128 to
4,075,218, with the same 8,262 main bark and foliage instances. GPU process
medians are 12.61ms full detail versus 9.49ms with levels. Representations differ
visually; this is a quality/performance tradeoff, not equivalent pixel work.

The stress identifies a CPU limit: 100k wide records in about 114ms and 100k
ground about 164ms despite successful GPU batching. Source inspection identifies
multiple full ECS/material traversals, query materialization, attached-field
lookups, transform reconstruction and per-mesh shader/resource packing before
culling as candidates. No sampling attribution or handwritten equivalent was
measured, so percentages and a sole cause are not claimed. GPU times also vary
substantially across processes; clocks/power/temperature and desktop activity
were uncontrolled. These results do not establish a production forest frame rate.

Runs used the existing perf-pass/gate.ps1 shared mutex and 5-second initial quiet
guard. All task builds/tests ended before each batch; no per-run contention or
GPU-clock monitor was used. All successful attempts are retained, with no exclusions.
Initial captures while building were diagnostics and do not contribute timing rows.

Reproduce from the repo root:

```powershell
$env:TIDE_HIDDEN = '1'
cmake --build --preset release --target bench_forest
& bench/results/perf-pass/gate.ps1 powershell -NoProfile -ExecutionPolicy Bypass -File bench/results/forest-20261101/run.ps1
& bench/results/perf-pass/gate.ps1 powershell -NoProfile -ExecutionPolicy Bypass -File bench/results/forest-20261101/run-supplement.ps1
python bench/results/forest-20261101/summarize.py
```

Use a fresh result directory or archive the existing JSONL before reproducing:
the benchmark appends rows and the verifier expects exactly the original 21.
metadata.json pins the original base revision, measured binary and source hashes
(both raw Windows bytes and LF-normalized source), validation and capture hashes.
The measured source was the base plus this commit's foliage changes. Capture PNGs
remain under ignored build/: forest-100000-ground.png, forest-10000-canopy.png
and forest-100k-wide.png. No executable or private art is committed.

Focused correctness: 24 native Windows ASan and 32 WASI/browser CTests pass
across the initial and corrected selections, plus the docs production build.
Browser rendering uses the existing muted launchers. See notes/render3d.md for
fixture corrections and implementation contracts. Other OS/mobile/Unity and
hardware web scaling remain untested locally.
