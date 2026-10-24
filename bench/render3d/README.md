# Render3D static-scene benchmark

`bench_render3d` measures the Render3D package after mesh aliases and multiple
directional, point and spot lights. It uses the production `Render3D.Lit`,
`AtlasVertex` and `TonemapScene` shaders, including PBR, shadow atlas and HDR
presentation. It introduces no compiler, runtime or package change.

The two ways are `package` and `cached_direct`. The latter is a **static-scene
submission floor**, not a dynamic-renderer replacement or a Tide-versus-C
language comparison. Untimed setup captures an already culled package frame's
immutable geometry, transforms, shader bindings, packed light/shadow data and
pass state. C then re-records the same clears, cameras, clips, shaded batches
and presentation through public drawing APIs each frame. It uses
`tide_draw_shaded_instances` for each compatible captured batch. It does not
replay the whole command buffer or call `tide_draw_append` inside timing.
Recurring ECS queries, hierarchy lookups, light collection, shadow allocation
and shader-data packing are absent from that cached path. Creating the cache
is excluded, and the scene cannot change while it is used.

This avoids copying the Render3D pipeline or writing an easier shader for the
baseline. It retains Tide's renderer, submission, uploads and GPU work in both
ways; it is not a handwritten Vulkan/OpenGL backend comparison.

## Workload

Output is always **1920 by 1080 physical pixels at scale 1**. A fixed 40 by
22.5 orthographic camera views a grid of equal, fixed-rotation cubes. The mesh
is the self-contained `mesh.obj`: **24 vertices and 12 source triangles**.
Counts default to 100, 1,000 and 10,000 cubes, plus one fixed flattened-cube
receiver behind them. Count therefore excludes that one receiver, which adds
24 vertices and 12 triangles. There is no LOD, animation, match playback,
networking or representative complex-model asset in this workload.

Each cube's squared scale is `160 / count`. Orthographic projection and a
fixed rotation keep the summed continuous projected silhouette area constant.
Rasterization and shadows can still cover different pixels as the grid changes;
no equality across different counts is claimed. All primary cubes stay in view.
The receiver's size is fixed. Directional lights and all punctual lights overlap
the whole grid: point/spot range is 100 and spots use full 130/150-degree
inner/outer cones aimed at it. Intensity is normalized by requested light count
to retain usable brightness during light sweeps.

The default bounded matrix has twelve scene configurations, each run both ways:

| Case | Directional | Point | Spot | Shadow-view budget | Actual views |
| --- | ---: | ---: | ---: | ---: | ---: |
| `unshadowed` | 1 | 4 | 4 | 0 | 0 |
| `dir` | 1 | 0 | 0 | 1 | 1 |
| `point` | 0 | 1 | 0 | 6 | 6 |
| `mixed` | 1 | 2 | 1 | 8 | 8 |

In `mixed`, the directional and spot reserve one view each, one point takes six
faces atomically, and the other point stays unshadowed. The default requested
cell size is 512 including gutters. One map uses a 512-square atlas; six/eight
views use a 1536-square atlas with unchanged 512-pixel cells. Results include
requested/active cell size, atlas size, view count, selected/dropped lights and
unshadowed requests. Budgets above sixteen are refused by the host.

`--directional`, `--point`, `--spot` and `--shadow-maps` override the selected
cases' defaults. Light counts must sum to at most 64. `unshadowed` always disables
shadows. `--shadow-resolution` is independently selectable. A small light sweep
can hold `--counts 1000 --cases unshadowed --directional 0 --spot 0` and use
`--point 1`, `8`, `32` in separate paired batches. A directional or spot sweep
uses the corresponding selector instead. Avoid a full Cartesian matrix unless
the question requires it.

## Correctness and timing boundaries

Every case checks the package and direct paths **outside timing**, even when
`--ways` selects only one for measurement. The exact expanded comparison checks
effective targets/cameras/clips/stencil, shader IDs, each instance, uniforms,
mesh vertices/indices and all sampled texture bytes, including numeric light
and shadow data. It ignores irrelevant registry offsets and batch boundaries.
Native additionally compares every pixel of completed full frames. Web compares
nine rendered pixels plus the same full expanded-command check. Both paths are
warmed and must have equal draw calls, submitted vertices and texture-upload
bytes; these counts must remain fixed on every measured frame. Views must leave
the match hash unchanged.

`scene_digest` includes live resource identity and is an in-process identity,
not a portable scene hash across process launches. `world_hash`, mesh SHA-256,
source/executable identities and the recorded parameters identify reproduction.

Default measurement uses 20 warmup frames, 100 measured frames and three
internal repeats. Internal repeats alternate way order. `--ways` can specify
`package,cached_direct`, the reverse order, or either alone, so the runner can
alternate whole processes.

- `view_record_ms`: wall time for reset, frame metadata and generated views/GUI
  completion, or for reset and public C direct recording. Package time includes
  collection, hierarchy resolution, shadow setup and shader-data recording.
- `cpu_submit_ms`: CPU wall time in `tide_platform_draw`, including renderer
  preparation and submission. It may include driver waits.
- `gpu_ms`: **actual delayed backend queries**, with `gpu_samples` recorded.
  Vulkan/OpenGL measure the elapsed drawing interval after upfront preparation
  and resource uploads. On-demand uploads inside shaded draws remain inside it.
  WebGPU sums render-pass timestamps, excluding gaps between passes. These scopes
  are different. WebGL has no GPU timer; WebGPU needs optional timestamp-query
  support. Unavailable or uncompleted queries are `null`, never fabricated zero.
- `completion_readback_ms`: native waiting for and reading the existing full
  framebuffer after submission, including copying 1920x1080 RGBA bytes. It is
  `null` on web. The corresponding native whole interval is `total_readback_ms`.

Native readback intentionally synchronizes frames. These are isolated frame
costs, **not shipping FPS or pipelined display throughput**. CPU/GPU intervals
overlap and must not be added. Query resets can flush Vulkan and are outside the
measured intervals. Warmup queries are discarded at the measurement boundary;
sixteen empty callback frames drain delayed results afterward. Finite GPU queues
can skip samples, and WebGPU retains only the newest completion; sample counts
can legitimately be smaller than the measured-frame count.

Rows also contain draw calls, submitted indices/vertices (including repeated
passes and clears), upload bytes, captured command/run/instance and resource
counts, and retained process/renderer GPU memory. Memory includes setup caches
and preceding cases; it is not a per-case allocation delta. Source triangles
are distinct from submitted vertices and from hardware shader invocations.

## Build and run

```powershell
$env:TIDE_HIDDEN = '1'
cmake --build --preset release --target bench_render3d
$env:TIDE_BACKEND = 'vulkan' # Or opengl; explicit Vulkan refuses fallback.
build/release/bin/bench_render3d --quick
$env:TIDE_BENCH_REVISION = '<built source identity>'
$env:TIDE_BENCH_MACHINE = '<CPU; GPU; driver; operating system>'
build/release/bin/bench_render3d --counts 100,1000,10000 --frames 100 --warmup 20 --repeat 3 --save baseline
```

`--save` appends JSONL to `bench/results/render3d.jsonl`; `--output file.jsonl`
selects an explicit append-only path. `--capture final.png` saves the final
native frame, outside timing. Pass `--help` for all selectors. Without
`TIDE_BENCH_MACHINE`, rows say `unknown`; the platform reports the backend, not
the physical adapter. In particular, a machine can have both a discrete NVIDIA
GPU and integrated Radeon graphics. The runner supplies this machine's confirmed
Ryzen 9 7900X / RTX 4080 SUPER label and records NVIDIA inventory/driver data;
that inventory is not a backend adapter query. Override it on another machine.

`--quick` disables timing entirely and uses four cubes, one receiver, one
warmup/two checked frames per way and requested 32-pixel shadow cells. It keeps
the fixed output size and covers all four cases. CTest registers native default
and forced OpenGL checks, plus WebGL and served WebGPU smoke through the existing
muted browser runners. There are no timing thresholds.

The web page uses the existing test shell, for example:

```text
bench_render3d.html?quick&backend=webgpu
bench_render3d.html?bench&counts=1000&cases=point&ways=package,cached_direct&frames=100&warmup=20&repeat=3&save=label&backend=webgpu
```

Web JSONL prints to the page log. `bench` disables synchronous per-call GL error
checks; omit it for correctness. Use a browser device scale factor of one for
hardware measurement. **CTest's software WebGL/WebGPU runs are correctness
coverage, not hardware performance evidence.** No hardware web performance
runner is supplied by this focused change.

## Gated native runner and artifacts

```powershell
pwsh bench/render3d/run.ps1 -Label render3d-initial -Revision '<built source identity>' -MonitorClocks
```

The runner does no building or correctness checks. It freezes the supplied
checkout-built executable and hashes it plus workload/host/direct-path/runner
sources. Default evidence is `build/render3d-measurements/<label>/`; an existing
directory is refused. Six processes cover three alternating package/direct
pairs on the explicitly selected backend (`-Backend vulkan` by default).
`-Counts`, `-Cases`, `-Frames`, `-Warmup`, light/budget overrides and
`-ShadowResolution` bound a smaller batch. `-Capture` retains final PNGs.

It holds `Local\TideClaudeContinuationBenchmark` through a 90-second continuous
quiet guard, the batch and cleanup. Gate/quiet waits stop after ten minutes;
each process stops after fifteen. Competing build/test/benchmark processes are
polled every 250 ms and recorded in metadata; shorter work can be missed. A
contended attempt is retained and marked, never silently discarded or retried.
Only owned children are cleaned up. Other desktop activity and clocks are not
controlled; `-MonitorClocks` records available NVIDIA clock/power observations
every 250 ms, without changing device settings.

Per-process JSONL, stdout/stderr, source/binary hashes, arguments, UTC boundaries,
contention, optional captures and GPU observations stay in the evidence folder.
The runner does not alter the shared results ledger. Preserve adverse attempts
as well as final ones; the final measurement report should cite the exact
executable/source identity, observed clock range, sample counts and unavailable
platform coverage. No performance claim is made by adding the harness.
