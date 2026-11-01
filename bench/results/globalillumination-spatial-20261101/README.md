# World GI spatial quality and paired cost, 2026-11-01

Experimental branch only: `feat/render3d-gi`. This work has not been merged to dev.

## What changed

Bounded triangle-based probe relocation improves usable coverage without authored
volumes or extra probes. Positive cosine integration replaces truncated SH lobes.
Inverse projective depth and receiver-plane visibility filtering remove the sharp
black contact fringe while retaining thin-wall blocking. The sphere's secondary
bands are smoother; faint corner occlusion lobes remain.

[Updated GI](captures/on.png), [GI disabled](captures/off.png) and
[actual probe positions](captures/probes.png) are native 960x720 captures after
160 frames with the same scene, camera, lights, materials, exposure and defaults
as the [earlier captured room](../globalillumination-20261101/captures/on.png).
The fully warmed updated image is identical at frames 160 and 256. Disabled output
is pixel-identical to the earlier disabled image. These are actual renderer
outputs, not offline reference renders.

## Source and binaries

Updated source: `8838029c96fd99e401697378bbdfc3b2f011afd3`.
Optimized executable SHA-256:
`135001cfe7c156bdf6de69b4b0d9fe784dc036953d755cdf0e729c4000a0cc48`.

The preserved earlier executable is source `ba070987a8dca293f35275275931c2e26180a17a`,
SHA-256 `22549f558edb3f41a373c2915d9671d3fd2768a4b046ca7f825e3074fa38340a`.
It was copied before rebuilding this checkout's optimized target. No binaries
are committed. The preceding `6013c9e44` report commit changed no measured code.

Both binaries use Windows Clang 23.1.2, RelWithDebInfo (-O2, no fast-math/FMA),
Vulkan, Ryzen 9 7900X and NVIDIA RTX 4080 SUPER, driver 617.42. Hardware and driver
were queried locally. All programs were built in this checkout and ran hidden.

## Paired local cost

Same room at 960x720, requested spacing 1.5, budget 512, and 384 actual probes for
GI-enabled modes. Each cell is the median of three process medians. Each process
has 100 measured frames, 20 warmup frames and a fully initialized field (24 frames
at 16 updates/frame before measurement). Every process returned 100 completed GPU
samples: 1,800 GPU samples total. Disabled mode performs no GI captures.

| Version | Mode | CPU recording ms | CPU submission ms | Whole-frame GPU ms | GPU process-median range ms |
|---|---|---:|---:|---:|---:|
| Earlier SH | off | 0.0095 | 0.0226 | 0.1460 | 0.1460–0.1464 |
| Earlier SH | frozen | 0.0101 | 0.0225 | 0.2804 | 0.2804–0.2806 |
| Earlier SH | update4 | 0.0861 | 0.1623 | 0.6434 | 0.6026–0.6445 |
| Updated spatial GI | off | 0.0096 | 0.0232 | 0.1490 | 0.1489–0.1491 |
| Updated spatial GI | frozen | 0.0129 | 0.0302 | 0.7728 | 0.7042–0.7750 |
| Updated spatial GI | update4 | 0.0896 | 0.1538 | 1.0608 | 1.0497–1.1238 |

Default four-update mode costs 0.4174 ms more GPU time in this room (1.0608 vs
0.6434 ms); CPU recording is 0.0896 vs 0.0861 ms. Frozen mode also includes the more
expensive receiver visibility lookup while performing no probe updates. These
are quality/cost comparisons, not equivalent-output optimization speedups.

GPU time includes the ordinary scene, shadows, captures, filtering and
presentation. CPU recording, submission and GPU intervals overlap; do not add
them or convert headless completion/readback time into shipping FPS. Cold BVH
construction is outside measurement. CPU geometry-cache memory is not part of
the reported GPU-memory counter. This small room does not establish mobile,
large-world, animated-scene or general hardware performance.

## Method and raw evidence

[Earlier raw rows](baseline.jsonl), [updated raw rows](spatial.jsonl),
[machine-readable summary](summary.json). No row was excluded or retried for
its timing. Every row retains unchanged world hash `05dc4724d8c871fa`, zero warm
texture-upload bytes, expected update budget, a real brightened receiver and at
most 12 shader textures.

Cases alternate adjacent earlier/updated processes for off, frozen and update4,
then reverse mode and version order for the middle round. The last round repeats
the first order. Each process used the shared benchmark mutex and five quiet
seconds through `bench/results/perf-pass/gate.ps1`. Cooperating builds, GPU tests
and agent GPU work had ended. Other desktop activity and GPU clocks were not
controlled. Process-median ranges are retained rather than hidden.

Reproduce with the [benchmark harness](../../globalillumination/README.md):
`--mode off|frozen|update4 --frames 100 --warmup 20`, one process at a time through
the shared gate, using the pinned earlier and updated binaries in alternating
order.

## Validation

All seven affected native Windows ASan CTests pass on Vulkan with validation and
OpenGL. All nine affected web-debug checks pass across scoped runs on WebGL,
ANGLE D3D11 and WebGPU; two optimized native benchmark smoke checks pass too.
Tests retain their original blocked/invalid/isotropic/skinning/hash oracles and
add actual contact, physical thin-wall/open control, no rear cosine lobe,
relocated-offset metadata and bounded/stable placement checks. Analytic geometry
queries cover closed/open/inward shells, mirrors, shear, tiny scale and invalid
inputs. The production docs build passes on Node 24.22.1.

Uniform-bounded sampler loops preserve neighbor and bilinear evaluation order
while avoiding a software-driver expansion regression: mapped-skinning WebGL
passes in 4.36 s instead of 67.9 s, and the pixel fixture passes in 106.17 s under its
unchanged 120 s runner. The optimized native image is pixel-identical to its
accepted pre-optimization image. No timeout or check was relaxed.

No compiler, runtime, backend, Render3D API or simulation behavior changed.
Other native OS/mobile hardware, Unity and device-loss recovery were not tested
locally. No CI run was dispatched. Full contracts and remaining limits are in
[the package notes](../../../notes/global-illumination.md).
