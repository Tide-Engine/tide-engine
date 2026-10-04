# Benchmarks

Simulation benchmarks compare the same deterministic work in Tide and hand-written C. Build and run them with `cmake --build --preset release --target versus`; individual `build/release/bin/versus_<concept>` programs list their options with `--help`. Save repeated measurements with `--save <label>` in `bench/results/versus.jsonl`.

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
