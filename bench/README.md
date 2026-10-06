# Benchmarks

Simulation benchmarks compare the same deterministic work in Tide and hand-written C. Build and run them with `cmake --build --preset release --target versus`; individual `build/release/bin/versus_<concept>` programs list their options with `--help`. Save repeated measurements with `--save <label>` in `bench/results/versus.jsonl`.

Set `TIDE_BENCH_MACHINE` to the CPU/machine and `TIDE_BENCH_REVISION` to the source revision and local changes before saving simulation or session runs, as for drawing below. New rows also record architecture, compiler version and whether C splitting was forced (`--split`, or `--quick`). Comparisons group matching options, machine, compiler and thread counts; older rows without that metadata form separate groups. Unidentified machines are reported as `unknown`; provide the machine label when comparing machines.

## Coverage and the October 6 audit

The suite covers ECS streaming, grid blocks and stencils, entity churn, event dispatch, pairwise list calculations, nested lists, ordered maps/sets, and networked prediction/joining. `versus_lookup` adds map/set hits and misses, score updates, deletion/reinsertion and duplicate set additions. Its digest includes insertion order and observed results. `--changes 0` isolates hits, misses and updates without key replacement. The C implementation uses separate ordered open-addressed tables with backshift deletion; it calls no Tide container functions. Quick checks include empty/single collections and replacement past the collection's size, on native and WebAssembly.

`versus_particles --kept 1` and `versus_buckets --kept 1` keep the previous state before each tick. Tide shares pages and copies them when written; C copies its arrays. Both keep equivalent logical data, and the final digest checks the retained contents as well as the live state. The buckets fixture checks both nested and flat Tide layouts. Before this audit, particles' C reference ignored `--kept`, so its old retained-world ratios excluded C's snapshot cost. Those ratios are not comparable to the corrected variant.

Three repeated full runs on Windows 11, Ryzen 9 7900X and Clang 23.1.2 are saved as `benchmark-audit`, with `benchmark-audit-kept` and `benchmark-audit-lookup-steady` for the variants. These are medians of reported tick medians and per-run Tide/C ratios, rounded; each run offers one thread and twelve. The flags remain deterministic (`-O2`, no fast math or FMA).

| Workload | Tide, 1 thread (ms) | Tide / best C, 1 thread | Tide, 12 threads (ms) | Tide / best C, 12 threads |
| --- | ---: | ---: | ---: | ---: |
| Particles, 1 million | 0.508 | 1.20× | 0.289 | 1.06× |
| Sand, 1024² | 1.016 | 2.42× | 0.227 | 3.59× |
| Life, 1024² | 0.585 | 7.50× | 0.205 | 2.63× |
| Entity churn, 2,000 spawns/tick | 0.573 | 2.82× | 0.526 | 2.61× |
| Events, 200,000 sends | 0.885 | 7.39× | 0.892 | 7.45× |
| N-body, 2,000 bodies | 3.423 | 1.72× | 0.411 | 1.69× |
| Nested buckets, 4,096 | 0.043 | 5.94× | 0.043 | 2.25× |
| Map/set, 16,384 keys, 1,024 replacements | 1.773 | 5.50× | 1.781 | 5.51× |
| Map/set, same keys, no replacements | 2.003 | 10.96× | 1.995 | 10.90× |
| Particles with previous state retained | 1.032 | 1.11× | 0.651 | 0.79× |
| Nested buckets with previous state retained | 0.066 | 4.28× | 0.066 | 2.12× |

All end-state digests agree. Offering workers does not make shared mutable map/set operations parallel. Life and Events remain the previously diagnosed bottlenecks described below. The new steady map/set case reveals another substantial gap: source inspection finds repeated list header/page resolution in `tide_map_find`, repeated entry lookups for read/update, and probe resolution in erase; this is an investigation target, not a measured attribution. The replacement case can be faster because index capacity grows with dead entries, reducing probe lengths; compare each variant with its matching C work.

The session runs retain matching worlds and join times: Sand's clients run about 30 ms of simulation per simulated second, while Gallery's largest initial world is 4.41 MB as sent and its slowest catch-up is 2.67 s under the configured 50 ms each-way delay and 1% loss. One OpenGL drawing run is saved with the RTX 4080 SUPER; its empty-scene median readback is 0.296 ms and 65,536 rectangles take 1.111 ms including recording/readback. These measurements describe this machine; the audit changes the benchmarks and their validation, and makes no engine speedup claim. [Audit metadata](results/benchmark-audit-metadata.json) identifies the runs and binaries.

Remaining coverage opportunities include sparse ECS filters, a retained window of multiple snapshots with restore/replay, asset-heavy compilation, and a handwritten graphics baseline. Networked Sand/Gallery already measure replay, snapshot, hash, packing, transfer and joining costs together; the new retained-state variants isolate snapshot/write costs against C.

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

## Life and Events phase measurements

Build the optional diagnostic programs, which instrument a copy of generated C and leave ordinary versus executables and runtime code unchanged:

```powershell
cmake --build --preset release --target versus_life_phases versus_life_control versus_events_phases versus_events_control
node bench/versus/phases/measure.mjs --repeat 5 --threads 1,12 --label native-phases
```

The 2026-10-04 UTC measurements use the same Windows/Ryzen/Clang configuration and fixture sizes as above. Five alternating control/profile pairs at each thread count are kept in [phases.jsonl](results/phases.jsonl); [metadata](results/phase-metadata.json) includes executable hashes, repeated-run ranges and the protocol. The controls use the same standalone harness with no phase clocks. Both variants reset counters outside the tick timer, so their cache conditions differ from ordinary versus runs; compare the matched controls for instrumentation overhead.

One-thread measurements, rounded medians of five reported phase medians:

| Life, 1024² cells | ms/tick |
| --- | ---: |
| Whole tick, untouched control | 0.571 |
| Whole tick, instrumented | 0.620 |
| Window gathering, including halo/border handling | 0.213 |
| Cell calculation | 0.172 |
| Window writeback, including comparisons and chunk buffers | 0.158 |
| Final grid commit | 0.022 |
| Window output copy | about 0.011 |

Preparing windows and putting results back cost more than the calculation. The calculation itself still takes about twice the handwritten C whole tick (0.078 ms), so removing memory passes alone would not establish parity. Life's clocks/counters add about 8.6% in paired one-thread runs; the smallest copy spans approach clock resolution, and no mechanical timer-cost subtraction is applied.

With twelve workers, the parallel region is about 0.115 ms, but the final commit is about 0.122 ms, versus 0.022 ms on one thread. The whole control tick is 0.287 ms. This locates a significant serial cost after worker computation; these measurements do not split its copying, cache transfers, ownership checks and pool bookkeeping. Worker task intervals are summed elapsed spans (including descheduling), so their totals are neither CPU time nor percentages of the threaded tick. The parallel region contains those task intervals; the systems interval contains prepare, parallel work, commit and scheduling. Do not add nested intervals.

| Events, 200,000 sends / 50,000 targets, one thread | ms/tick |
| --- | ---: |
| Whole tick, untouched control | 0.883 |
| Scheduling and recording | 0.206 |
| Replay and cleanup | 0.670 |
| Command application, within replay | 0.668 |
| Queue clearing, within replay | about 0.001 |
| Heap cleanup, within replay | below useful resolution |

Replay is about 76% of measured time, from the per-run mean shares. Clearing/settlement is negligible; lookup, ownership checks, dispatch and handler work remain grouped inside application. This fixture takes no snapshots, so shared-page copying is not the explanation. The default recording work estimate is 7,601,519, below the native scheduler threshold of 20,000,000; offering twelve threads keeps it on one thread. Its replay is also sequential. Even eliminating all recording would limit the gain to roughly 1.3× with current replay. Batching replay and hoisting stable table/page metadata while keeping exact record/handler order are more relevant than recording-only improvements.

Events' paired profile/control variation is around measurement noise. Phase medians do not add to the tick median; percentages use nonoverlapping per-run means. Every one of the forty full measurements matches its C digest. Sixteen native/WASI quick checks cover control/profile on one/twelve requested threads. System sampling was attempted for replay internals, but Windows refused enabling the profiling policy; no trace was left running. Internal replay attribution still needs a usable sampling profiler.

A dense Life path can remove window/output/chunk staging after the compiler proves complete writes, keeping old inputs immutable, separate outputs, snapshot ownership and fallback paths for sparse or skipped writes. Events can process eligible adjacent queue runs without repeating generic dispatch scaffolding for every hit. These are implementation opportunities, not measured speedups; full-write and handler-side-effect proofs are prerequisites.

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

Wall times include process startup, the frontend, native compilation, linking and web page creation. The cold measurement includes compiling engine objects kept in the game's cache. It leaves the installed toolchain and OS file cache warm. The harness records which cached object files were written and which changed content, making unnecessary recompilation visible. It rejects Tide/C/header edit samples whose dependent object still has the old content, even if a timestamp changed. Object hashing runs outside the build timer. These are build completion times; hot reload also includes watching the file and swapping the running game.

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

The drawing benchmark measures 65,536 rectangles, circles, rectangles interrupted by text, textured quads, cube instances, round rects (the smooth shapes' path) and rectangles with a round rect among every 1,024 (one run of both kinds, which all goes through the smooth shapes' pipeline), plus an empty scene showing the readback cost. Each workload has 20 warmup frames and 100 measured frames. It reports median and p95 for draw-list recording, rendering with readback, and the combined time. Saved rows in `bench/results/draw.jsonl` identify the renderer, dimensions, compiler, machine and revision. On a web build, `?bench&save=change-name` prints JSONL records; `backend=webgpu` or `backend=webgl` chooses the backend.

These are C calls to Tide's drawing APIs. They exclude generated Tide view execution, and rendering time includes CPU submission, GPU completion and copying the target's pixels back. They are useful for tracking overhead and batching, but there is no handwritten GPU API baseline yet, so they do not establish competitive graphics performance. Pixel tests of generated shaders and manual instancing verify correctness separately.
