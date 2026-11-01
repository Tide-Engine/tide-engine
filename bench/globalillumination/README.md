# Global illumination workload

`bench_globalillumination` draws one fixed room through the optional
GlobalIllumination package and Render3D. The room has seven meshes, one point
light with six shadow faces, colored walls, a white box/sphere and an emissive
ceiling panel. Geometry, camera, exposure and light inputs stay identical across
GI modes. No simulation ticks run during the measured frames.

Modes are `off`, `frozen`, `update1`, `update4` and `update16`. Every enabled
mode first fills the complete automatic probe field with sixteen updates per
frame, outside measurement. It then uses the requested update budget, warms
twenty frames and measures one hundred at 960x720. `frozen` keeps the captured
field and performs no further probe captures. No authored GI volume is used.

Build and run correctness checks first:

```powershell
$env:TIDE_HIDDEN = '1'
cmake --build --preset sanitize --target bench_globalillumination
ctest --preset sanitize -R '^bench_globalillumination' --output-on-failure
cmake --build --preset web-debug --target bench_globalillumination
ctest --preset web-debug -R '^bench_globalillumination' --output-on-failure
```

Quick checks use 128x96, one warmup frame and two checked frames per mode,
with GPU timing disabled. They retain the real capture/convolution shaders,
automatic coverage, all five budgets, unchanged match hashes and a rendered
white-box witness that must brighten with GI. The command and every shader
are checked against the twelve-texture limit. Web checks use the existing
muted WebGL/WebGPU test runners; their software-GPU costs are not performance
evidence.

Measure only an optimized unsanitized build after other builds and GPU tests
have ended. For an exploratory local run, the existing shared gate supplies
five quiet seconds and serializes cooperating benchmark processes:

```powershell
cmake --build --preset release --target bench_globalillumination
$env:TIDE_BACKEND = 'vulkan'
pwsh -NoProfile -File bench/results/perf-pass/gate.ps1 `
    build/release/bin/bench_globalillumination.exe `
    --frames 100 --warmup 20 --output build/gi-cost.jsonl
```

For reported comparisons, coordinate the quiet window, repeat adjacent process
runs in alternating mode order, retain source/binary identities and every raw
row, and record competing work. The simple gate does not monitor contention
after it launches the program; it is not a replacement for that coordination.
Single-process sequential mode results alone do not establish a universal
speed claim. Use `--mode off`, `--mode frozen`, `--mode update1`, `--mode update4`
or `--mode update16` for separate controlled processes. `--capture final.png`
saves the actual final frame of a native run, after the timed samples.

JSONL separates CPU frame recording, CPU renderer submission, native
completion/readback, and asynchronous GPU queries. CPU recording includes
world-bounds cache lookup, probe scheduling and ordinary Render3D views. GPU
intervals include the main pass, shadow and probe passes, convolution and
presentation; available scopes differ between native and web backends.
Unavailable GPU timers are null, with an explicit sample count. Verify that a
reported GPU result has the intended number of completed samples.

Rows include the automatic probe count, update/capture-face budget, maximum
shader textures and mean submitted draws/vertices/texture-upload bytes. Actual
submitted geometry may vary as different probes are refreshed, so this harness
reports that work instead of asserting identical counts at every probe index.
Retained GPU memory includes resources from preceding modes. Native full-frame
readback synchronizes frames. CPU and GPU intervals overlap and must not be
added or converted to shipping game FPS. This is a bounded room workload, not
a large-world, animation or dynamic-geometry throughput limit.
