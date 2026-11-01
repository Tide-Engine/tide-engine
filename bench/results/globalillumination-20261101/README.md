# Automatic world GI: local cost and captures, 2026-11-01

Experimental feature branch only: `feat/render3d-gi`. Nothing here authorizes a merge to dev.

The final implementation is source `ba070987a8dca293f35275275931c2e26180a17a`.
The optimized unsanitized executable has SHA-256
`22549f558edb3f41a373c2915d9671d3fd2768a4b046ca7f825e3074fa38340a`.
Windows Clang 23.1.2, RelWithDebInfo (-O2, no fast-math/FMA contraction), Vulkan,
Ryzen 9 7900X and NVIDIA RTX 4080 SUPER, driver 617.42. Hardware was queried
locally. No binaries are committed.

## Final cost

Same room, camera, lights, materials and exposure at 960x720. Every enabled mode
has 384 automatically placed probes, requested spacing 1.5 and budget 512.
Each row is the median of three process medians, each with 100 measured frames,
20 warmup frames and a fully initialized field (24 convergence frames at 16 updates).
Every process produced all 100 GPU samples; 1,500 GPU samples total for this table.

| Mode | CPU recording ms | CPU submission ms | Whole-frame GPU ms | GPU process-median range ms |
|---|---:|---:|---:|---:|
| GI disabled | 0.0123 | 0.0309 | 0.1464 | 0.1461–0.1466 |
| Frozen GI | 0.0131 | 0.0315 | 0.2805 | 0.2799–0.2805 |
| 1 probe/frame | 0.0327 | 0.0672 | 0.3728 | 0.3714–0.3733 |
| 4 probes/frame (default) | 0.0890 | 0.1783 | 0.6461 | 0.5854–0.6497 |
| 16 probes/frame | 0.3074 | 0.6298 | 1.6483 | 1.6452–1.6615 |

GPU time includes the ordinary scene, shadows, GI captures/filtering and presentation.
It is not GI-only time. CPU recording excludes hash/sampler correctness audits;
CPU submission is reported separately. CPU/GPU intervals overlap: never add them
or interpret headless completion/readback as shipping-game FPS. Disabled GI is
a runtime mode in this same binary, not a separate build omitting the package.

All measured modes retain unchanged match hashes, the expected probe-update budget,
a real white receiver that brightens with GI and at most twelve shader textures.
Warm texture-upload bytes are zero in every row. Frozen GI has no capture passes.
These numbers describe this bounded room on this device; they do not establish
mobile, large-world, animation or complex-scene performance.

## Method and retained evidence

[Raw final rows](parallel.jsonl), [machine-readable summary](summary.json).
Processes ran in forward/reverse/forward mode order, with the shared benchmark
mutex and five quiet seconds before each process. Builds and GPU tests had ended
before measurement. Other desktop activity and GPU clocks were not controlled.
All rows are retained; none was excluded or retried for timing.

[Earlier serial-projection rows](serial.jsonl) are diagnostic history from
`3592b33d91d9b452363dcfbffe67d838cf178014`, executable SHA-256
`98242ed5d858b3db954ccd2ce73b6cb633338323bed0383818ec9660279b9f14`.
Intervening builds/tests separate the two series, so this is not a tightly paired
before/after speedup experiment. Both use the same 384 rays and scene; grouped
half-float reduction can change a captured color channel by one byte. No bit-exact
visual-equivalence claim accompanies the optimization.

Reproduction and full interval definitions: [benchmark README](../../globalillumination/README.md).
Use `--mode off|frozen|update1|update4|update16 --frames 100 --warmup 20` through
`bench/results/perf-pass/gate.ps1` after correctness checks and other GPU work end.

## Actual captures and validation

[GI disabled](captures/off.png), [GI enabled](captures/on.png),
[probe debugging](captures/probes.png) are actual 960x720 native captures from the
same scene recipe as `bench/globalillumination/game.tide`. They use automatic
coverage and default quality/update controls after 160 rendered frames; no GI
volume is authored. Capture generation used the native ASan build, outside timing.
The previously black left-wall cutoff is removed. Contact/thin-geometry artifacts,
one diffuse bounce, rest-geometry coverage and uniform-grid scaling limits remain
explicitly documented in [package notes](../../../notes/global-illumination.md).

All 28 affected native ASan and 36 web-debug CTests pass across focused runs,
including Vulkan validation/OpenGL and WebGL/ANGLE D3D11/WebGPU, independent
wall/floor/visibility/bounce/continuity pixels, mapped skinned/deformed consumers,
cache/pack-generation/lifecycle/update limits, benchmark smokes and unchanged match
hashes. Production docs build passes. Other native OS/mobile hardware, Unity and
frozen-device-loss recovery were not exercised locally. No CI was dispatched.
