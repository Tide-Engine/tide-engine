# Benchmarks

Simulation benchmarks compare the same deterministic work in Tide and hand-written C. Build and run them with `cmake --build --preset release --target versus`; individual `build/release/bin/versus_<concept>` programs list their options with `--help`. Save repeated measurements with `--save <label>` in `bench/results/versus.jsonl`.

## Native runtime measurements

The 2026-10-04 `native-performance-before` and `native-performance-fixed` records compare five alternating runs on Windows 11, a Ryzen 9 7900X and Clang 23.1.2, after all builds and tests finished. Both builds use `-O2`, `-fno-fast-math` and `-ffp-contract=off`. These are medians of the five reported tick medians; the C ratio is the median of each run's ratio to its fastest C implementation (the explicit SIMD implementation for N-body).

| Workload | Threads | Tide before (ms/tick) | Tide after (ms/tick) | Before / best C | After / best C |
| --- | ---: | ---: | ---: | ---: | ---: |
| Life, 1024² cells | 1 | 0.704 | 0.581 | 8.84× | 7.34× |
| Life, 1024² cells | 12 | 0.315 | 0.298 | 3.99× | 3.78× |
| N-body, 2,000 bodies | 1 | 8.093 | 3.560 | 4.00× | 1.77× |
| N-body, 2,000 bodies | 12 | 1.161 | 0.587 | 3.64× | 2.02× |
| Events, 200,000 sends / 50,000 targets | 1 | 1.172 | 0.912 | 9.64× | 7.49× |
| Events, 200,000 sends / 50,000 targets | 12 | 1.285 | 0.896 | 10.56× | 7.39× |

All thirty runs match their handwritten C end-state digests. The retained changes use relative grid-window addresses, batch independent outputs in eligible counted list loops while preserving each reduction's order, and reserve queue-page runs for pure event sends. Simple one-pass list kernels keep their existing path. Event ordering, record bytes, sparse filters, C calls, snapshots and hot reload retain their existing contracts. Full native and web suites pass, including the existing cross-platform hashes; the web suite verifies correctness here, not these Windows performance numbers.

Life and Events still have substantial gaps. The Life worker timings overlap, so these runs do not establish a consistent worker speedup. Machine variation also affected exploratory runs: their labels identify diagnostics, and their results do not describe retained code. [Measurement metadata](results/native-performance-metadata.json) identifies source revisions, executable hashes, options, ranges and diagnostics; [Life diagnostics](results/native-performance-life-diagnostics.json) records discarded experiments.

## Compile and iteration time

Run the compile benchmark against a complete Tide installation, with Node:

```powershell
node bench/compile.mjs --tide C:/path/to/tide/bin/tide.exe --save baseline
node bench/compile.mjs --tide C:/path/to/tide/bin/tide.exe --targets native,web --configs debug,release --sizes 100,1000 --repeat 5 --save change-name
```

The defaults measure native debug builds of 100 reachable functions across 11 Tide files, five times. `--files 1000 --sizes 1000` also measures a game spread across a thousand source files. Each function runs in a system, so the frontend and C optimizer must process reachable code. These are synthetic source-size measurements; declaration count alone does not describe the complexity of a real game.

Each repetition uses a temporary game and measures a sequence:

| Scenario | What changes before `tide build` |
| --- | --- |
| `cold` | Remove the game's `.tide` cache and built output |
| `unchanged` | Nothing |
| `tide-body` | Change a reachable Tide function's calculation; verify generated C changes |
| `c-body` | Change one of two C translation units' calculations |
| `header` | Change a header used by one C translation unit; the other does not include it |

Wall times include process startup, the frontend, native compilation, linking and web page creation. The cold measurement includes compiling engine objects kept in the game's cache. It leaves the installed toolchain and OS file cache warm. The harness also records which cached object files were written, making unnecessary recompilation visible. These are build completion times; hot reload also includes watching the file and swapping the running game.

Use `--tidec build/release/bin/tidec.exe` to measure the frontend and code generation separately. This measurement runs after one warm-up and includes starting `tidec` and writing generated files; it does not include C compilation or linking. Use `TIDEC_TIMES=1` with `tidec` directly for its individual phase timings.

Every run appends individual samples, median and p95 wall times to `bench/results/compile.jsonl`, with the CPU, OS, Node and compiler versions, executable identity, Git commit and dirty state. `metadata.compilers.native` and `.web` identify each target's compiler, including macOS installations with Apple clang for native builds and Homebrew LLVM for web builds. `--results <path>` selects another destination, and `--keep` retains the temporary fixture for inspection. p95 uses the nearest rank; with five samples it is the slowest sample. Repeat runs on an otherwise idle machine and compare matching targets, configurations, source sizes and toolchains. Timings have no machine-dependent pass/fail threshold. Requested unavailable toolchains fail visibly.

The harness never edits a game in the repository or a supplied installation. `--tide` must point to the installed CLI; a bare build-tree executable without the installation's headers, sources and platform libraries cannot build games.

## Drawing

```powershell
cmake --build --preset release --target tide_platform_draw
$env:TIDE_BENCH_REVISION = "<revision and local changes>"
$env:TIDE_BENCH_MACHINE = "<CPU and GPU>"
build/release/bin/tide_platform_draw --bench --save change-name
```

The drawing benchmark measures 65,536 rectangles, circles, rectangles interrupted by text, textured quads and cube instances, plus an empty scene showing the readback cost. Each workload has 20 warmup frames and 100 measured frames. It reports median and p95 for draw-list recording, rendering with readback, and the combined time. Saved rows in `bench/results/draw.jsonl` identify the renderer, dimensions, compiler, machine and revision. On a web build, `?bench&save=change-name` prints JSONL records; `backend=webgpu` or `backend=webgl` chooses the backend.

These are C calls to Tide's drawing APIs. They exclude generated Tide view execution, and rendering time includes CPU submission, GPU completion and copying the target's pixels back. They are useful for tracking overhead and batching, but there is no handwritten GPU API baseline yet, so they do not establish competitive graphics performance. Pixel tests of generated shaders and manual instancing verify correctness separately.
