# Benchmarks

The [October 11 large-scope rerun](results/scope-performance/README.md) records the current whole-suite baseline and paired map/skinned optimizations, with raw measurements, validation and remaining scale limits.

The opt-in [GPU clip experiment](results/gpu-clip/README.md) uses `bench_skinned --modes gpu`: a resident 60 Hz table of bone palettes, interpolated in the vertex shader with an independent phase per character. Default runs keep the original four modes. `--subframe 0.5` adds a half-tick rendering offset to every selected mode; `--verify-gpu` compares the baked approximation against the exact CPU sampler at 240 phases outside timing when GPU mode is selected. The experiment is specific to the unchanged one-second, 64-bone, four-influence fixture; it adds no public animation API or IK implementation.

Simulation benchmarks compare the same deterministic work in Tide and hand-written C. Build and run them with `cmake --build --preset release --target versus`; individual `build/release/bin/versus_<concept>` programs list their options with `--help`. Save repeated measurements with `--save <label>` in `bench/results/versus.jsonl`.

The opt-in [Bevy ECS comparison](bevy/README.md) ports particles, wide components,
sparse filtering and entity churn to pinned Bevy ECS 0.20.0. It checks matching
final-state digests before reporting repeated CPU tick measurements. The versus
programs accept `--json` to also print their record without appending to a ledger.

The [October 20 performance pass](../notes/benchmarks.md) added tools for measuring on a machine other agents build and benchmark on, all in `results/perf-pass/` but the profiler. Every timing they make holds the shared `Local\TideClaudeContinuationBenchmark` mutex and waits for a quiet spell with no builds, tests or benchmarks:

- `run.ps1 -Label <label> [-Rounds 3] [-Only a,b]` copies the benchmarks' binaries aside and runs every versus program, the sand and gallery sessions and drawing on OpenGL and Vulkan, saving each run as `<label>-r<round>`. Pass `-Only`'s list through `pwsh -Command "& ./bench/results/perf-pass/run.ps1 ..."`: `-File` would take it as one string. With `-Before <folder> -BeforeBuiltFrom <revision>`, a folder of earlier binaries, each program runs from both in turn (before, after, then after, before), saved as `<label>-before-r<round>` and `<label>-after-r<round>`: the way to compare a change on a busy machine.
- `summarize.mjs <label>...` prints, for each label, the median of its rounds per benchmark and thread count, Tide against the fastest C way.
- `gate.ps1 <program> [arguments]` runs one command under the gate, for a quick timing that isn't saved.
- `tide_sample` (`profile/sample.c`, Windows, built on request: `cmake --build --preset release --target tide_sample`) is a sampling profiler that needs no administrator rights: `tide_sample [--top n] [--lines n] -- <program> [arguments]` suspends each of the program's threads a few thousand times a second and prints where its own code was, by innermost function, outermost function and line, through `llvm-symbolizer` and the program's PDB. Profile release builds; samples in `ntdll.dll` are mostly threads waiting.

Set `TIDE_BENCH_MACHINE` to the CPU/machine and `TIDE_BENCH_REVISION` to the source revision and local changes before saving simulation or session runs, as for drawing below. New rows also record architecture, compiler version and whether C splitting was forced (`--split`, or `--quick`). Comparisons group matching options, machine, compiler and thread counts; older rows without that metadata form separate groups. Unidentified machines are reported as `unknown`; provide the machine label when comparing machines.

## Views over wide components (2026-10-30)

`bench_views` (`views/`) times views that run once for each of 20,000 entities, a view at a time: reading one field of a wide component and of a small one, per entity and through a query, and a helper handed a big local singleton's lists. `bench_views [frames]`. Results: `notes/benchmarks.md`, Views over wide components.

## Earth under load (2026-10-24)

`bench_earth` plays the home page's planet (`examples/earth`, with room for 256 players) with bots that land in towns, walk, drive, wave and go to other towns: `--towns 1` puts everyone in one, `--drivers` and `--hop` say how many drive and how often they travel. As any benchmark of a sharded game, `--split <n>` runs its world split into `n` regions, each shard timed apart (`--most` and `--busiest` let regions split and merge); `--lag <ticks>` lets bots fall further behind before the run fails; and with visibility, `--diverge <bot>` says what differs between that bot's world and what the server says it is, tick by tick. Results and what they mean: `notes/benchmarks.md`, Earth under load.

## Page hashes (2026-10-20)

`bench_hash` (`hash/hash.c`) times the engine's page hash (`tide_hash_wide`, and `tide_hash_wide_baseline` without AVX2) beside the candidates measured for its place, natively and as WebAssembly under Node (`node cmake/run_wasi.mjs build/web-release/bin/bench_hash.wasm`): an 8 KiB and a 16 KiB page in cache, 512 KiB and 8 MiB of pages in a shuffled order, 256 MiB of them from memory, and 64- and 1,000-byte runs. Each of 31 rounds (`--rounds`) takes every candidate in turn on every shape for a few milliseconds; it prints each one's median and best GB/s, the best being the nearest to a hash's own speed on a machine others use. `--only <name>` times one. Before timing it checks that every SIMD way gives its plain definition's bits; `--quality` instead runs the sanity checks (avalanche, one- and two-bit changes of a page of zeros and of bytes, the same bit in two stripes, zeros of every length, a counter), which take a few minutes natively. Its test runs `--quick`, those checks small. Results and the decision are in `notes/benchmarks.md`, Page hash, measured.

## Rollback, the schedule and thread scaling (2026-10-20)

`versus_rollback` runs a world the way a predicting client does, and compares what each part costs. Each timed tick is a period, one of the client's frames: `--every` ticks (4), each with the world before it kept in a ring of `--kept` worlds (16) and the world after it hashed, then a correction, which restores the world kept `--back` ticks ago (8, as many as are kept at the most), runs those ticks again with what they had the first time, keeping each again, and hashes the end, which has to match the hash it had. So a period runs 12 ticks, keeps 12 worlds, hashes 5 and restores one, about what a client 150 ms from its server does at 60 ticks a second. The world has `--movers` (100,000) movers with a Position and a Velocity, of which `--moving` of each hundred (25) move and the rest lie still, the movers that move coming first in spawn order, or spread through the rest with `--spread 1`; and `--turrets` (4,000) turrets, which cool down every tick and count each shot in a singleton and in a list of `--notes` (4,096) ints.

- `Tide` keeps worlds with `tide_world_copy`, ticks with `tide_world_tick_on` and hashes as sessions do (`hash_world_on` with a pool, which hashes the pages that changed on a few threads first, `tide_world_hash` without).
- `C` keeps its world in one block, its arrays one after the other: keeping a world is a `memcpy` of the block into the ring, restoring one a `memcpy` back, and the hash is wyhash (final version 4, written into the fixture) over every byte, in 64 KiB units hashed apart and then together, on threads past 4 MiB.
- `C, dirty` is what someone writes next, once they know a tick changes little: each array in 16 KiB blocks, each with a version a tick changes when its loops find (comparing bits as they write) that they changed a byte of the block, and each block's hash kept with the version it's of. Keeping a world copies the blocks whose version isn't the kept world's, restoring the same the other way, and the hash rehashes only blocks that changed since and then hashes their hashes.

Every way hashes all of its state each time, and ends with the same world and ring, bit for bit. Each reports its tick (the first time and again), snapshot, hash and restore times beside the whole period's (`ms a tick, by part`), and saves them in its row.

`versus_systems` measures the schedule: 64 systems over 16 components of two floats, in four kinds of entity that each have eight of them. Twelve components' systems read only what they change, four in a row each, and the first four components' are a band that each read the one before, ten deep. `--count` sets the entities of each kind: 0 is what a tick of 64 systems costs with nothing to do. The C way runs the same 64 loops in order, or from 16,384 entities a kind, in the ten stages an expert works out from what each reads and writes, a stage's loops at once in pieces on threads.

Any versus program takes `--threads-list 1,2,4,8,12`, which runs every way on each count of threads in turn; the default (one thread, then every core) and its output are unchanged. A pool of fewer threads than the platform's lets its workers sleep and wakes as many as a round asks for, as the platform's does. It doesn't choose which cores take part: on this machine's two CCDs (six cores and 32 MiB of L3 each), past six threads a run's threads straddle both, and curves there mostly show memory moving between the two halves, for C and Tide alike.

Three gated rounds at `e66b6477` (`coverage-baseline-r1..r3`, Windows 11, Ryzen 9 7900X, Clang 23.1.2), medians of the rounds' medians, milliseconds a period:

| Rollback, 100,000 movers, 25 of each 100 moving first | Tide | C | C, dirty | Tide / C | Tide / C, dirty |
| --- | ---: | ---: | ---: | ---: | ---: |
| Whole period, 1 thread | 1.813 | 2.166 | 1.544 | 0.84× | 1.17× |
| Ticks (12) | 1.607 | 0.882 | 1.244 | 1.82× | 1.29× |
| Snapshots (12) | 0.026 | 0.816 | 0.180 | 0.03× | 0.14× |
| Hashes (5) | 0.180 | 0.345 | 0.102 | 0.52× | 1.76× |
| Restore (1) | 0.0018 | 0.104 | 0.014 | 0.02× | 0.13× |
| Hashes, 12 threads offered | 0.132 | 0.344 | 0.102 | 0.38× | 1.30× |
| With `--spread 1`: whole period, 1 thread | 2.506 | 2.107 | 2.764 | 1.18× | 0.91× |
| ...ticks / snapshots / hashes | 1.788 / 0.082 / 0.631 | 0.881 / 0.774 / 0.344 | 1.242 / 1.091 / 0.330 | | |

Taking a snapshot and restoring one cost Tide next to nothing (its pages are shared until written), and its hash only reads the pages that changed; what the snapshots save shows up in the ticks instead, which write each shared lane into a page of their own and compare it with the shared one (`tide_table_lanes_begin`/`_end`). With a million movers (`--movers 1000000 --turrets 40000`), the period is 27.8 ms for Tide, 32.1 for C and 25.9 for C, dirty on one thread (0.85×, 1.08×), and 12.9, 22.2 and 10.9 on twelve; Tide's hash there is 3.3× C, dirty's (about 8 GB/s against 26), which fits the pages it hashes being new every tick, taken from the ones a snapshot let go of, where C, dirty writes its blocks in place and finds them still in the caches; this wasn't measured apart.

| Systems, entities of each kind | Tide, 1 thread (ms) | C (ms) | Tide / C |
| --- | ---: | ---: | ---: |
| 0 (64 systems, nothing to do) | 0.0009 | 0.0006 | 1.50× |
| 64 | 0.0047 | 0.0015 | 3.13× |
| 4,096 | 0.0652 | 0.0580 | 1.12× |
| 16,384 | 0.2575 | 0.2253 | 1.14× |
| 65,536 | 1.292 | 0.918 | 1.36× |

| Tide / best C on 1, 2, 4, 8, 12 threads | 1 | 2 | 4 | 8 | 12 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Systems, 16,384 a kind (Tide ms: 0.258, 0.199, 0.136, 0.202, 0.308) | 1.14× | 1.13× | 0.87× | 0.71× | 0.82× |
| Systems, 65,536 a kind (Tide ms: 1.292, 0.817, 0.488, 0.526, 0.465) | 1.36× | 1.42× | 1.33× | 1.11× | 0.86× |
| Particles, 1 million (Tide ms: 0.455, 0.240, 0.129, 0.145, 0.183) | 1.03× | 1.05× | 1.00× | 0.90× | 0.79× |
| Wide, 1 million (Tide ms: 1.366, 0.972, 0.830, 0.875, 0.802) | 1.28× | 1.27× | 1.16× | 1.17× | 1.10× |
| Sand, 1024² (Tide ms: 0.985, 0.549, 0.316, 0.234, 0.215) | 2.40× | 2.63× | 2.84× | 3.23× | 3.45× |
| Life, 1024² (Tide ms: 0.575, 0.331, 0.197, 0.193, 0.198) | 7.48× | 4.30× | 2.56× | 2.52× | 2.56× |
| Churn, 2,000 a tick (Tide ms: 0.431 to 0.419) | 2.16× | 2.10× | 2.12× | 2.11× | 2.11× |
| Events, 200,000 sends (Tide ms: 0.906 to 0.876) | 7.64× | 7.46× | 7.45× | 7.42× | 7.37× |
| N-body, 2,000 (Tide ms: 3.396, 1.727, 0.897, 0.489, 0.386) | 1.73× | 1.70× | 1.71× | 1.69× | 1.66× |
| Nested buckets, 4,096 (Tide ms: 0.086 throughout) | 11.84× | 12.93× | 12.36× | 6.34× | 4.93× |
| Map/set, 16,384 keys (Tide ms: 1.751 to 1.766) | 5.62× | 5.54× | 5.60× | 5.54× | 5.58× |

C's ways that don't split their work (Life, Churn, Events, Lookup's) take the same time on every count; Sand's and N-body's best C is their table-lookup and SIMD ways. Tide's churn, events, buckets and map/set ticks stay on one thread whatever is offered. Memory-bound loops (Particles, Systems) are fastest on four to six threads for both languages and slower past that, on this machine's second CCD. Two changes came of these: a chunk's lane bookkeeping is inline where no snapshot shares its pages (systems at 64 entities a kind 4.8 → 3.9 µs a tick), and on threads cheap tasks are taken a few at a time, starting from the first system with work left (systems at 16,384 a kind 17–20% faster on 2 and 4 threads). `notes/benchmarks.md` has the paired before/after rounds, what was tried and dropped, and the gaps left; closing the hash's (`tide_hash_wide` about 1.75× slower than wyhash here) needs the owner, as a faster page hash would move every pinned world hash.

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

Remaining coverage opportunities include asset-heavy compilation. Two items missing at the audit exist since 2026-10-20: a retained window of multiple snapshots with restore/replay is `versus_rollback` (above), and a handwritten graphics baseline is `versus_draw` (Drawing, below). Networked Sand/Gallery already measure replay, snapshot, hash, packing, transfer and joining costs together; the new retained-state variants isolate snapshot/write costs against C.

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

## Rows and lanes measurements

The [rows and lanes report](results/row-functions.md) compares the compiler/engine baseline at `7bd9777` with the tested implementation recorded in its [measurement metadata](results/row-functions-measurement-metadata.json). `7a7ba9e` is the compiler/engine full-suite validation point; subsequent benchmark corrections have their own measured source and binary identities. These records describe the benchmark implementation used for the measurement, before later dev integration.

`versus_wide` covers a component of twelve float lanes, six changed by each tick, both unfiltered and with a sparse tag. `--tagged 1 --frozen 0`, `50` or `100` selects all, half or none of the rows. The wide and particles `--kept 1` variants retain equivalent prior state in both Tide and handwritten C and validate its contents along with the live state. The same corrected benchmark sources are used in both builds. Historical kept-C records that omitted retained state remain in the ledger for context; their snapshot comparisons are not comparable to these corrected measurements, and their digests belong to separate benchmark groups.

Five before/after rounds run in turn, offering one and twelve threads. The report takes the minimum saved per-round tick median, and the minimum saved per-round end-to-end build median. Session values instead use minimum per-round recorded milliseconds per second of play, averaged per client where applicable; direct compiler diagnostics use minimum recorded wall times. These estimators differ from the median-of-medians summaries elsewhere on this page. The handwritten comparator is the fastest applicable C way. The benchmark mutex serializes cooperating sibling batches, while other work can still contend; metadata retains the pre-run CPU checks and uncertainty. Each runtime batch completes one 13-case alternating before/after round; end-to-end compiler pairs each take the gate, while the remaining direct compiler pairs share one finishing acquisition, as recorded in the metadata. Earlier partial runtime pairs remain in the ledgers but are excluded from the final tables through metadata date/options identities.

`bench/compile.mjs --loops 100 --archetypes 1` (or `4`) adds entity-system loops to its reachable-function fixture. The report covers native/web debug/release builds, cold and unchanged builds, and Tide/C/header edits. The [direct compiler records](results/row-functions-compile-phases.json) separate whole-command wall time, available LLVM IR/backend phase wall times and object size; unavailable phase logs do not imply a phase timing. Raw [runtime](results/versus.jsonl), [Sand](results/sand.jsonl), [Gallery](results/gallery.jsonl) and [compile](results/compile.jsonl) records retain the rounds. Mixed passing/skipped rows and unchanged lanes inside a `mut` component remain implementation gaps.

## Compile and iteration time

Run the compile benchmark against a complete Tide installation, with Node:

```powershell
node bench/compile.mjs --tide C:/path/to/tide/bin/tide.exe --save baseline
node bench/compile.mjs --tide C:/path/to/tide/bin/tide.exe --targets native,web --configs debug,release --sizes 100,1000 --repeat 5 --save change-name
```

The defaults measure native debug builds of 100 reachable functions across 11 Tide files, five times. `--files 1000 --sizes 1000` also measures a game spread across a thousand source files. Each function runs in a system, so the frontend and C optimizer must process reachable code. These are synthetic source-size measurements; declaration count alone does not describe the complexity of a real game.

That game has one system, which runs once a tick, so it has none of the code tidec generates for a system's loop over its entities. `--loops 100` adds a hundred systems that each run for every entity of a component of a dozen floats and change it, one in four leaving out the entities with a sparse tag, and `--archetypes` (1 to 4, 4 unless it says) is how many archetypes those entities come in: each loop is for all of them.

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

The `status-1008-*` runs (2026-10-08) were taken while other sessions built on the machine, with the package's tide (clang built in) and with one that runs an installed clang, as the earlier runs did. Their [record](results/compile-status-metadata.json) has the load, the same builds of Duel, Towers, Sumo and a sample, where a cold build's time goes file by file, and Sumo's compiles replayed several at a time: compare those rows by the least of a cell's samples.

tide compiles a build's C files several at a time since (as many as the machine has processors, or what `TIDE_JOBS` says, which a record's `metadata.environment` has). The `parallel-compiles` rows are that against `parallel-compiles-one-at-a-time`, the same tide with `TIDE_JOBS=1`, in turn on a quiet machine; their [record](results/parallel-compiles-metadata.json) has the same for the real games. Cold builds before that change compiled one file at a time: compare across it with `TIDE_JOBS=1`.

The harness never edits a game in the repository or a supplied installation. `--tide` must point to the installed CLI; a bare build-tree executable without the installation's headers, sources and platform libraries cannot build games.

## Drawing

```powershell
cmake --build --preset release --target tide_platform_draw versus_draw
$env:TIDE_BENCH_REVISION = "<revision and local changes>"
$env:TIDE_BENCH_MACHINE = "<CPU and GPU>"
build/release/bin/tide_platform_draw --bench --save change-name     # TIDE_BACKEND=vulkan or opengl picks the backend
build/release/bin/versus_draw --save change-name                    # the same workloads by hand, in TIDE_BACKEND's API
node bench/compare.mjs draw --tide change-name --c change-name      # side by side, with Tide's ratio to C
```

The drawing benchmark measures 65,536 rectangles, circles, rectangles interrupted by text, textured quads, cube instances, round rects (the smooth shapes' path) and rectangles with a round rect among every 1,024 (one run of both kinds, which all goes through the smooth shapes' pipeline), plus an empty scene showing the readback cost. Each workload has 20 warmup frames and 100 measured frames. It reports median and p95 for draw-list recording, rendering with readback, and the combined time. Saved rows in `bench/results/draw.jsonl` identify the renderer, dimensions, compiler, machine and revision. On a web build, `?bench&save=change-name` prints JSONL records; `backend=webgpu` or `backend=webgl` chooses the backend. `--only <n>` runs one workload (for a profiler), and `--dump-frames <prefix>` writes each workload's last frame as `<prefix>-<n>.ppm` after its timing.

These are C calls to Tide's drawing APIs. They exclude generated Tide view execution, and rendering time includes CPU submission, GPU completion and copying the requested pixels back. Pixel tests of generated shaders and manual instancing verify correctness separately.

`versus_draw` (`bench/draw/`, desktop only) is the handwritten baseline: the nothing, rects, circles, textured quads, cube instances and round rects workloads written directly against OpenGL 3.3 core (`draw_gl.c`) or Vulkan 1.3 (`draw_vk.c`, dynamic rendering, no window), as an expert writes them for these cases: a static quad or cube, one instance buffer the CPU writes straight into (an unsynchronized map on OpenGL, device-local host-visible memory kept mapped on Vulkan), one pipeline and one instanced draw per workload, minimal state, and one pixel read back. It uses the same target, frames, shapes, colors, camera and timing boundaries, and its frames are byte-identical to Tide's on both APIs (`versus_draw --compare <prefix>` against `tide_platform_draw --bench --dump-frames <prefix>`). It also draws the cubes with a whole matrix each, as Tide sends them, as an extra workload. Its rows have `"competitive_baseline":true`, `"way":"C"`, the API and the device; `TIDE_BACKEND` picks the API, Vulkan first without it. Its record part writes instances into GPU-visible memory, while Tide's record is its draw list and its upload is in its render part, so compare totals. `--quick` (a CTest on each API, label `gpu`, Vulkan under the validation layer) checks the pixel read back. Results and remaining gaps: `notes/benchmarks.md`, Drawing against handwritten OpenGL and Vulkan.

## Skinned meshes

```powershell
$env:TIDE_HIDDEN = '1'
cmake --build --preset release --target bench_skinned
$env:TIDE_BENCH_REVISION = "<revision and local changes>"
$env:TIDE_BENCH_MACHINE = "<CPU, GPU and driver>"
build/release/bin/bench_skinned --counts 1,10,100,1000 --frames 100 --warmup 20 --repeat 3 --save skinning-baseline
```

`bench_skinned` compares static geometry, an omitted default pose, an animated pose sampled once per frame and shared, and a separately sampled animated pose per character. Each character has its own local entity and Tide view. A preceding view sets the camera and samples the shared pose. Entity creation is outside the measurements. `--modes static,rest,shared,independent` selects cases; `--capture image.png` saves the final native frame.

The self-contained fixture has 3,072 vertices, 6,048 triangles, 64 skin joints, four positive influences per vertex and a one-second looping clip with 17 rotation keys per joint. It is a tapered tube with a joint chain, rather than a particular game's character. Static and skinned cases import the same geometry and use the same fragment. The grid keeps every instance visible at 1024 by 576 and scales instances to keep their summed nominal projected areas constant as counts increase. Independently animated characters use different loop phases. Reproduce the fixture with `node bench/skinned/generate.mjs`; `--check` verifies it.

Each case reports median and p95 generated-view/recording CPU time (including sampling and temporary cleanup), CPU submission time, asynchronous GPU query time with its sample count, draw calls (including the clear) and texture-upload bytes per frame. Native runs separately time waiting for and reading the completed framebuffer, and the whole frame through that readback. GPU queries start after upfront renderer preparation and geometry uploads, but include on-demand palette texture uploads inside shaded draws; they exclude readback and are not pure shader timings. These intervals overlap CPU work and cannot be added to infer FPS. WebGL has no GPU timer; WebGPU needs optional timestamp-query support. Web readback metrics are unavailable. The native readback intentionally synchronizes every measured frame, so these are isolated frame costs rather than a measurement of pipelined display throughput.

`--save` appends one row per count, mode and repeat to `bench/results/skinned.jsonl`, including compiler, revision, machine, fixture SHA-256, resolution and workload dimensions. Process memory and renderer-tracked GPU memory are retained totals after each case, including caches from earlier cases, rather than allocations attributable solely to that case. On the web, `bench_skinned.html?bench&counts=1,10,100,1000&frames=100&warmup=20&repeat=3&save=label&backend=webgpu` prints the same JSONL. Web dimensions are logical pixels; hardware browser measurements should force device scale factor 1. The `bench` argument disables synchronous per-call GL error checking for measurements; omit it for correctness checks. CTest smoke runs exercise every mode on native OpenGL and software WebGL/WebGPU without timing assertions. Use a quiet machine and repeat measurements; software browser smoke timings are not hardware performance results.

The initial 2026-10-10 baseline measures source `56a0d6ad` on Windows, Ryzen 9 7900X, RTX 4080 SUPER, NVIDIA 595.79 and Clang 23.1.2, using release OpenGL 3.3. Each of the 16 count/mode combinations has three repeats of 100 measured frames after 20 warmup frames. The table takes the median of the three per-repeat medians at 1,000 characters. GPU timings describe this shared desktop under its observed clock conditions: all 15 clock samples during the final independent-pose case report 210 MHz graphics, 405 MHz memory and P8. They do not establish peak GPU throughput. Other user applications remained running; this task's builds and tests had finished before measurement.

| 1,000 characters | CPU views/recording (ms) | CPU submission (ms) | GPU query (ms) | Mesh draw calls | Palette uploads/frame |
| --- | ---: | ---: | ---: | ---: | ---: |
| Static geometry | 0.057 | 0.031 | 0.500 | 1 | 0 |
| Omitted default pose | 133.064 | 0.110 | 5.784 | 1 | 0 |
| Shared animated pose | 139.063 | 0.143 | 5.829 | 1 | 4,096 bytes |
| Independent animated poses | 151.682 | 16.178 | 58.685 | 1,000 | 4,096,000 bytes |

CPU preparation was the clearest bottleneck in the initial study: at 100 characters the three skinned modes already took 13.315, 13.837 and 15.097 ms respectively. Sharing a pose preserved GPU batching, while CPU work remained per character. The benchmark fixed missing compiler scratch cleanup exposed by owned value copies; it initially changed no renderer optimization. [Saved runs](results/skinned.jsonl), [measurement metadata](results/skinned-baseline-metadata.json) and [clock observations](results/skinned-gpu-monitor.csv) retain that study's full cases, p95 values and artifact identities. Its 2,040 native and 2,013 web debug checks passed.

### CPU preparation improvements

The follow-up removes four repeated costs. Generated skinned draws borrow model/Pose handles when later arguments cannot change them, while preserving ordinary snapshots before later writes or C calls. Loaded models reuse full validation and the canonical content hash established during pack loading; owned/decoded draws retain full validation and hashing, and replacement or overlapping geometry writes invalidate the loaded proof. Comparing a shared pose's rig reads its header instead of flattening the complete owned model. Sampling a verified model's clips reuses key validation, retaining the sampler's stricter first-time check. Palette evaluation and capture still happen per draw; interpolation still happens at each animation sample. Standalone `Animations` assets do not establish this proof and retain full sampling checks unless their bytes share a separately verified model block.

A baseline phase diagnostic at 1,000 characters attributes about 44 ms/frame to complete model validation and 52 ms/frame to combined model hashing/influence capture. These are instrumented arithmetic means over ten frames, include clock overhead, and sit inside the draw-function interval; they are not the uninstrumented frame medians or an independent before/after comparison. The optimized code passes all 2,041 native and 2,014 web debug tests, including unchanged deterministic pose hashes, argument-order/COW regressions, pack invalidation, and OpenGL/WebGL/WebGPU pixel checks.

The subsequent clean merge with the asset-import changes at `1008c772` builds on native and web debug and passes 113 native plus 106 web checks for the overlapping imports, diagnostics, metadata, animation and rendering paths. The measurements below retain their exact pre-integration source and executable identities.

The comparison uses the preserved original `56a0d6ad` release executable and the tested `280b2180` executable with unchanged host, fixture and settings. Three rounds alternate before/after, after/before, then before/after; each process measures all 16 cases for 100 frames after 20 warmup frames. Each build contributes 4,800 CPU/GPU samples. Summary times take the median of three per-round medians; paired speedups take the median of the three within-round before/after ratios. Builds and tests finish before the measurements, and a coordination gate and process checks guard the batch. Other user applications remain running, and GPU clocks are observed every 250 ms without changing power settings.

| 1,000 characters | CPU views/recording before (ms) | After (ms) | Median paired speedup |
| --- | ---: | ---: | ---: |
| Static geometry | 0.057 | 0.057 | 1.01× |
| Omitted default pose | 132.549 | 3.463 | 38.30× |
| Shared animated pose | 139.436 | 2.663 | 52.37× |
| Independent animated poses | 151.600 | 8.964 | 16.97× |

The six processes completed 96 rows covering 16 count/mode cases, with 9,600 CPU frames and 9,600 GPU samples. Draw counts and upload bytes match before/after in every paired case. The final 1,000-character independent-pose captures at frame 119 have identical PNG bytes and decoded RGB pixels. At 100 characters the optimized rest/shared/independent CPU costs are 0.347, 0.292 and 0.860 ms/frame. Independently animated characters still need 1,000 mesh draws and 4,096,000 palette-upload bytes/frame at the largest count; this change removes CPU preparation overhead without introducing independently posed instancing.

GPU timings also change, but clock conditions differ: the original processes mostly report P8, while the optimized processes mostly report P0. Across the whole processes graphics clocks range from 210 to 2,835 MHz and memory clocks from 405 to 11,501 MHz. These observations are not per-case clock normalization and do not establish a GPU shader speedup. CPU submission, GPU query, readback and total intervals remain in the saved records. Two earlier attempts interrupted by concurrent external builds were excluded and the original ledger restored before the clean batch; metadata records both exclusions.

[Summary](results/skinned-cpu/summary.json), [measurement metadata](results/skinned-cpu/metadata.json), [paired rows](results/skinned-cpu/paired-rows.jsonl), [clock observations](results/skinned-cpu/clock-summary.json) and [baseline phase attribution](results/skinned-cpu/diagnostic-before/phase-summary.json) retain the measurements and their boundaries. [Reproduction instructions](results/skinned-cpu/README.md) include the measured scripts and artifact identities. Original measured hashes and LF-normalized committed hashes are recorded separately. Verify the exported text with:

```powershell
node bench/results/skinned-cpu/reproduce/export.mjs --verify bench/results/skinned-cpu
```

### Palette preparation and texture allocation

The next CPU change reuses captured palettes for exact repeated poses within a frame, reads validated animation endpoints without decoding unrelated keys, and initializes rest transforms without repeating node checks. Mutable models keep their checked paths, and cached poses compare their current values rather than handles. Matrix and interpolation arithmetic are unchanged.

Independent poses exposed another cost: the renderer created and deleted 1,000 palette textures per frame. It now determines which previous textures the complete current draw list references, then reuses obsolete integer-data allocations of the same size for new content identities. Each changed palette still uploads every byte; current and later draws retain separate textures, and unused allocations are freed by frame end. This does not introduce independently posed instancing.

The comparison measures preserved `6497cb79` and tested `cffb3752` release executables with the same fixture, host and three alternating pairs described above. All 96 rows, 9,600 CPU frames and 9,600 GPU samples completed after a 90-second quiet window, with no competing build/test jobs during measurement. Times below are medians of three round medians at 1,000 characters.

| 1,000 characters | CPU views before (ms) | After (ms) | CPU submission before (ms) | After (ms) |
| --- | ---: | ---: | ---: | ---: |
| Static geometry | 0.057 | 0.059 | 0.033 | 0.032 |
| Omitted default pose | 3.451 | 0.647 | 0.036 | 0.035 |
| Shared animated pose | 2.650 | 0.671 | 0.050 | 0.037 |
| Independent animated poses | 8.749 | 4.824 | 6.223 | 3.263 |

Median paired view speedups are 5.36× for rest, 3.95× for shared animation and 1.83× for independent animation; independent submission improves 1.91×. Submission varies substantially across optimized rounds, from 0.621 to 3.275 ms at 1,000 independent poses, so the table retains the conservative three-round median. Static view differences are only 0.003 ms in the aggregate. GPU observations are mostly P0 (93 of 95 samples), with two initial P8 samples; graphics clocks span 210–2,835 MHz and memory 405–11,501 MHz. These whole-process observations do not normalize individual cases or establish a shader speedup or pipelined FPS.

Draw counts and upload bytes match every before/after case, and the final frame-119 independent-pose captures match PNG bytes and decoded RGB pixels exactly. Separate phase diagnostics confirm one palette/hierarchy computation per frame for repeated rest/shared poses, and zero warmed texture creates/frees plus exactly 1,000 texture updates/frame for 1,000 independent poses. Those diagnostics include instrumentation overhead and do not supply the table's timings. Independently posed characters still issue 1,000 mesh draws and upload 4,096,000 palette bytes/frame. All 2,061 native and 2,032 web debug checks pass, including deterministic pose bits, malformed-data and snapshot boundaries, and OpenGL/WebGL/WebGPU rendering and device loss. [The complete comparison evidence](results/skinned-cpu2/README.md) retains paired rows, ranges, clocks, both diagnostics, validation summaries, reproduction scripts and artifact hashes. The historical 144 ledger rows are preserved alongside the 96 new rows.

The subsequent clean merge with release stripping at `62dafeb8` passes 52 overlapping native/web debug and release checks. A standalone pruned game's native/WASI probe verifies retained used mesh/animation assets, current model validation after pack offsets change, repeated palette reuse and scratch cleanup; release OpenGL/WebGL/WebGPU rendering and benchmark smoke also pass. The measured executable hashes and source identities above remain unchanged.

### Shared palette pages and worker preparation

The third change captures independent poses into shared integer-texture palette pages. A hidden per-instance matrix offset lets compatible characters form one mesh draw even with different poses. Captured model dependencies and local transforms remain owned by the draw list; the existing worker pool calculates independent palettes before rendering. Small workloads run serially, shaders with fully occupied attribute layouts retain individual palettes, and animation sampling still happens immediately.

Preserved `c75ebcda` and tested `bc3c7b2a` release executables use the unchanged host, fixture and three alternating pairs above. All 96 rows, 9,600 CPU frames and 9,600 GPU samples completed after the 90-second quiet window, without competing builds/tests or excluded attempts. These are medians of three round medians at 1,000 characters:

| Mode | CPU views before (ms) | After (ms) | CPU submission before (ms) | After (ms) |
| --- | ---: | ---: | ---: | ---: |
| Static geometry | 0.057 | 0.057 | 0.032 | 0.006 |
| Omitted default pose | 0.646 | 0.671 | 0.034 | 0.009 |
| Shared animated pose | 0.673 | 0.702 | 0.037 | 0.010 |
| Independent animated poses | 4.920 | 2.961 | 3.331 | 0.465 |

Palette math moves from views into CPU submission, so the view reduction alone overstates the net CPU gain. Summing the separate view/submission medians within each round gives a **derived estimate**, not a median of combined per-frame samples. The median of these sums is 8.178→3.425 ms for independent animation, with a median paired ratio of 2.21×. Baseline submission round medians are 3.497/0.646/3.331 ms; optimized medians are 0.465/0.426/0.650 ms. Static submission shows the same small alternating-process variation, so its isolated interval is not evidence of a static-rendering improvement. Rest/shared derived CPU sums are essentially unchanged at 0.680→0.682 and 0.710→0.711 ms; their full-readback frame medians slightly increase, 1.656→1.727 and 1.682→1.756 ms.

Independent animation now uses one mesh draw plus the clear, replacing 1,001 total draws with two. Palette uploads remain 4,096,000 bytes/frame at 1,000 characters. At ten independent poses, final-row padding increases uploaded bytes from 40,960 to 49,152; at 100 and 1,000 they remain equal. Final frame-119 captures have identical PNG bytes and decoded RGB pixels. The independent full frame through synchronized readback falls from 13.588 to 4.788 ms; GPU queries fall from 6.375 to 1.071 ms. Queries still include on-demand uploads and draw execution, and the batching changes submission; these values do not isolate shader arithmetic or establish pipelined FPS. Whole-process clocks have 65 P0 observations and one initial P8, spanning 210–2,835 MHz graphics and 405–11,501 MHz memory. Other desktop applications remain running.

All 2,064 native and 2,034 web debug CTests pass, alongside 40 focused skinned runtime tests and the docs production build. The native preparation probe observed three worker callbacks and matched every serial palette byte; the initial served WebGPU round completed on its caller, while WebGL exercised the no-pool fallback. [The complete comparison evidence](results/skinned-cpu3/README.md) retains rows, ranges, clocks, both immutable snapshots' identities/source hashes and validation summaries. The ledger preserves its exact previous 240 rows alongside 96 new rows. Verify the exported text with:

```powershell
node bench/results/skinned-cpu3/reproduce/export.mjs --verify bench/results/skinned-cpu3
```

Integration at `1af49c5b` with the latest web compression/private-header packaging passes 26 bounded native/web debug and release checks. Staged engine headers match their sources, and a standalone pruned game verifies separate-file clip binding, four instances with two poses in one batch, retained pack provenance, scratch cleanup and all 21 pixels across three frames on OpenGL, WebGL and WebGPU. Its release page verifies exact decoded bytes for separately compressed WASM and pack payloads under the generated minified runtime. The actual native standalone completes three hidden frames. Both measured executable identities remain unchanged; this adds no timing comparison.

### 10,000-character scaling

The same preserved final `bc3c7b2a` executable measures 1,000 and 10,000 characters in three fresh processes, all four modes, 20 warmup and 100 measured frames per case (2026-10-10). The 24 rows contain 2,400 CPU frames and completed GPU samples. The named gate and 90-second quiet window exclude competing builds/tests; no attempts are excluded. At 10,000 characters, medians of three process medians are:

| Mode | CPU views (ms) | CPU submission (ms) | GPU query (ms) |
| --- | ---: | ---: | ---: |
| Static geometry | 0.655 | 0.030 | 4.724 |
| Omitted default pose | 6.798 | 0.043 | 8.179 |
| Shared animated pose | 6.846 | 0.044 | 8.192 |
| Independent animated poses | 30.492 | 5.090 | 13.698 |

Fresh 1,000-character independent medians are 2.957 ms views, 0.454 ms submission and 1.078 ms GPU. Independent 10,000-character GPU process medians span 13.544–13.926 ms; the median of process p95 values is 15.412 ms, not a pooled p95. The derived CPU estimate (median of each process's separate view/submission median sum) scales 3.404→35.576 ms. Synchronized full-frame readback scales 4.922→48.281 ms; this does not establish pipelined FPS, and GPU queries must not be added to CPU intervals.

Independent poses occupy ten palette pages, issuing ten mesh draws plus clear and uploading 40,960,000 bytes/frame; the other modes keep one mesh draw plus clear. Sampling stays serial, and palette preparation keeps its cap of three workers plus caller. The fixed 1024×576 viewport and summed nominal projected area make each of the 100×100 characters only about 4.6 pixels tall. The fixture still submits 30.72 million source vertices and 60.48 million triangles/frame without LOD, culling or match simulation; this is not a close-up crowd scene. Whole-process clocks have 134 P0/3 initial P8 samples, spanning 210–2,835 MHz graphics and 405–11,501 MHz memory; other desktop applications remain running, with no settings changes. [The scaling evidence](results/skinned-10k/README.md) retains exact source/executable/fixture identities, all rows, ranges, clocks and scripts. The final 10,000-character capture is retained in scratch with recorded PNG/RGB hashes; no pixel equality across different grids is expected. The ledger preserves its 336-row prefix plus these 24 rows. This measurement changes no production code.

### Fourth CPU improvement

At `09c1cc20` (2026-10-10), sampling reuses current pack validation for private channel reads while checking each channel's first key time, then reuses binary-search intervals only for exactly equal short time arrays within one call. Pose-cache indexing uses the existing four-chain hash, and synchronous capture borrows contiguous scratch poses before making the retained copy. Animation arithmetic, pose bits, palette layout and rendering work are unchanged. All 2,067 native and 2,037 web debug CTests pass, including 42 focused native skinned checks and OpenGL/WebGL/WebGPU pixels.

The comparison against preserved `bc3c7b2a` uses three alternating before/after pairs at 1,000 and 10,000 characters, all four modes, 20 warmup and 100 measured frames. All 48 rows contain 4,800 CPU frames and completed GPU queries. At 10,000 characters, medians of three process medians are:

| Mode | CPU views before → after (ms) | CPU submission before → after (ms) | GPU query before → after (ms) |
| --- | ---: | ---: | ---: |
| Static geometry | 0.694 → 0.709 | 0.026 → 0.026 | 4.630 → 4.707 |
| Omitted default pose | 6.791 → 4.718 | 0.038 → 0.037 | 7.902 → 7.813 |
| Shared animated pose | 6.691 → 5.187 | 0.039 → 0.039 | 7.866 → 7.903 |
| Independent animated poses | 30.101 → 26.709 | 4.839 → 4.904 | 13.880 → 13.335 |

Independent view recording falls about 11.3%; the derived median of per-round view/submission median sums falls 34.93990→31.61265 ms, about 9.5%. Its median within-pair speedup is 1.115×; these are distinct aggregations, and the sum is an estimate rather than a combined-frame median. View round medians are [29.8186, 30.1010, 30.3239] before and [26.8006, 26.3477, 26.7090] after. Median-of-process-p95 recording falls 30.5573→27.1997 ms. Default/shared derived CPU estimates fall 6.82880→4.75485 and 6.73065→5.22530 ms. At 1,000 independent poses, views are 2.93510→2.58355 ms, submission 0.43780→0.44060 and the derived CPU estimate 3.36680→3.02755 ms.

Palette math remains inside CPU submission on both sides; its small change is not a claimed improvement. Static rendering is essentially unchanged. GPU queries include on-demand palette uploads and drawing, with the same shaders, geometry, draws and upload bytes; these observations do not establish a shader speedup. Independent synchronized readback-frame medians fall 47.3672→43.5442 ms, without establishing pipelined FPS. CPU and GPU intervals must not be added.

Both 1,000- and 10,000-character final independent captures match exactly in PNG bytes and RGB pixels. The 10k workload retains ten mesh draws plus clear, 40,960,000 palette upload bytes and 60.48 million source triangles/frame, with tiny projected characters and no LOD, culling or match simulation. A named gate and 90-second sustained quiet guard detected no competing builds/tests or excluded measurement attempts. Whole-process observations have 252 P0/2 initial P8 samples, spanning 210–2,820 MHz graphics and 405–11,501 MHz memory; other desktop applications remain running and no settings change. [Saved comparison evidence](results/skinned-cpu4/README.md) retains all rows, ranges, clocks, validation, baseline phase attribution and reproduction scripts. Raw generated C differs only in two debug `#line` paths for the isolated build; exact normalized hashes match. The original 360-row ledger prefix stays exact, with 48 new rows bringing it to 408.

### Fifth CPU improvement: known rest and sampling proof

At `57e885b5` (2026-10-11), omitted-pose draws reuse one owned rest capture per current pack proof and frame. Repeated instances avoid constructing/hashing TRS and comparing model tables. Explicit poses keep ordinary edit-safe capture. Sampling also reuses a load-time proof that all channels start at nonnegative times, skipping the selected-channel preflight only while that proof remains current. Negative-first models retain selected-clip checks; invalidated and owned models retain full checks. This uses the statically known omitted-pose case; it adds no general hoisting of read-only locals or new `mut` semantics.

The frozen final implementation passes all 2,067 native and 2,037 web debug CTests, 46 focused native skinned and 30 pack checks, and 406 WASI runtime checks with unchanged exact pose bits. OpenGL/WebGL/WebGPU palette and pixel checks pass, including native serial/pool equality with three workers. Three alternating pairs against immutable CPU4 `09c1cc20` cover 1,000/10,000 characters and all four modes, with 20 warmup/100 measured frames: 48 final rows, 4,800 CPU frames and 4,800 completed GPU queries. At 10,000 characters, medians of three process medians are:

| Mode | CPU views before → after (ms) | CPU submission before → after (ms) | GPU query before → after (ms) |
| --- | ---: | ---: | ---: |
| Static geometry | 0.589 → 0.604 | 0.027 → 0.026 | 4.421 → 4.409 |
| Omitted default pose | 4.823 → 0.811 | 0.045 → 0.035 | 7.730 → 7.766 |
| Shared animated pose | 5.306 → 5.102 | 0.046 → 0.042 | 7.932 → 7.462 |
| Independent animated poses | 29.221 → 24.071 | 6.503 → 6.458 | 13.967 → 12.975 |

Default recording is about 5.95× faster. Independent recording falls 17.6%; its round medians are [29.2047, 29.2205, 29.3683] before and [24.6077, 24.0712, 24.0413] after. Its median-of-process-p95 changes more modestly, 30.3179→29.1059 ms. The derived median of per-round view/submission median sums falls 35.86385→30.52960 ms (14.9% lower), with a distinct median paired ratio of 1.1747×. Default/shared derived sums fall 4.86260→0.84805 and 5.35595→5.14345 ms. At 1,000 characters, default views fall 0.46535→0.07720 ms and independent views/submission fall 2.89725→2.34155 / 0.49535→0.45605; independent derived sums fall 3.39260→2.80595. Shared 1k views are 0.52150→0.53410 ms.

Palette preparation remains in CPU submission on both sides, with sampling serial and at most three palette workers plus caller. The derived sums combine separate interval medians, rather than combined per-frame samples. GPU queries include on-demand palette uploads and drawing; shaders, geometry, draws and upload bytes are unchanged, so these observations do not establish a shader speedup. Independent synchronized readback-frame medians fall 48.11180→42.11020 ms, and default 13.02110→9.02925. These are readback measurements, not pipelined FPS; CPU and GPU times must not be added.

All four default/independent capture pairs at 1k/10k match exactly in PNG bytes and RGB pixels. The 10k independent workload still submits 60.48 million triangles/frame, ten mesh draws plus clear, and 40,960,000 palette bytes. Its 100×100 characters are about 4.6 pixels tall in the fixed viewport, with no LOD, culling or match simulation. The final named gate/90-second quiet guard has no detected contention, reset or excluded attempt; other desktop apps remain running and system settings stay unchanged. The six processes have 245 clock observations (243 P0, one P5, one initial P8), spanning 390–2,820 MHz graphics and 405–11,501 MHz memory. These whole-process observations do not normalize individual cases.

The initial candidate `f10e65ff` completed an adverse comparison: 10k independent views rose 27.08380→30.48430 ms. Its 48 rows and evidence remain under [superseded](results/skinned-cpu5/superseded/README.md). Exploratory timer and private layout experiments show timing sensitivity to code placement; they neither isolate a sole cause nor contribute timings to the final aggregate. The final comparison measures the combined implementation and its compiled layout, rather than isolated savings from the removed scan. [Saved final evidence](results/skinned-cpu5/README.md) retains identities, ranges, clocks, validation, exact generated-adapter comparison and reproduction scripts. The ledger preserves its original 408 rows, then the 48 adverse rows, then 48 separately labeled final rows (504 total).

### Pose.Sample, indexing and Model a call at a time (2026-10-30)

`bench_pose` (`pose/`) samples a separate animation export into `skinned/model.gltf`'s pose, as a view samples a character's clips: `clip.gltf`, the same 64-bone chain with other defaults and an extra leaf, translation, rotation and scale keys for every bone at 60 Hz (192 channels of 61 keys, as an FBX clip arrives; `node bench/pose/generate.mjs`, `--check`). It prints the cost of a call to `Pose.Sample` (rest, then the clip by name), `Animation.Sample` on the clip's own skeleton and the game's `Sampled()` called 1,000 times in a view, best of 20 batches, then copying 40 bones by index and a 33-deep bone's `Model` in the view; owned copies of the same bytes show the cost of planning every call. Every run first checks that kept sample plans give the bits of plans made for one call; `--quick` (the CTest, native and WASI) runs that check and one short batch. No window or GPU. Results: `notes/benchmarks.md`, Pose.Sample, indexing and Model a call at a time.

## Input delay in Sumo

```powershell
$env:TIDE_HIDDEN = '1'
cmake --build --preset web-release --target sumo --parallel 2
node bench/lag.mjs build/web-release/bin/sumo.html --pings 0,50,100,150 --seconds 150 --join 30 --threads 4 --save catch-up-web
cmake --build --preset release --target bench_lag_native --parallel 2
node bench/lag_native.mjs build/release/bin/bench_lag_native.exe --pings 0,50,100,150 --seconds 150 --join 30 --fps 144 --save catch-up-native
```

Each run has a host, a player who joins at the start, and another who joins after 30 seconds, with bots filling the remaining seats. Both harnesses use match seed 0; the browser accepts `--seed n` to choose another. The ping is the whole network round trip. Moving and charging inputs change throughout the run. Clients use their real tick costs to choose input delay with the existing 4 ms budget. The report gives delay over time, ticks run again, update and frame times, corrections, and the first second after each client's world arrived. The native executable also accepts `--frames n` to bound a short smoke run.

The browser uses three headless Chrome processes, isolated pages with threads, hardware GPU drawing by default, and a relay on this machine. `--browser` selects Chrome; `--gpu software` explicitly selects SwiftShader. Each page gets the requested processor count so three pools do not each take the entire machine. The native run uses three sessions in one process, updated in turn on a shared pool of at most four threads. It measures simulation, generated views and draw-list recording without opening a window or submitting GPU draws. Its frame intervals include all three machines' work. Compare session update costs between the platforms; their whole-frame measurements represent different workloads. A requested native frame rate is a pace limit; work and the scheduler can make frames longer. The browser's late join starts 30 seconds after the first client connects; the native join starts 30 seconds after the host starts. Delay timelines begin when each client receives its world.

`--save` appends summaries to `bench/results/lag.jsonl` and keeps compressed per-frame samples in `bench/results/lag-frames/*.jsonl.gz`. Saved records include relative raw-data paths and SHA-256 digests. Raw rows are `[player_index, frame_values]`, with indices 0, 1 and 2 in the order above; `lag_summary.mjs` defines the values, in `tide_run_netcode`'s order. A final object contains measurement metadata. `checkout_revision` identifies the harness checkout, and a browser page's `page_sha256` identifies its actual built artifact, including historical pages measured from elsewhere. Native `full_round` means a second ring round started; browser Sumo uses the host's confirmed ticks to establish that the first round's maximum duration passed. Check this field rather than assuming 150 seconds of wall time advanced the match far enough.

Run on an otherwise idle machine and repeat comparisons when sharing CPU or GPU resources. The continuation chats serialize these rounds with `Local\TideClaudeContinuationBenchmark`. The runners themselves do not acquire that coordination gate. Input delay is the extra ticks the engine chooses, rather than the full time between a button press and its display. The measurements propose changes to the budget; they do not change its default or the delay policy. The native harness also takes `--budget <ms>`, the clients' resim budget in place of the engine's 4 ms, which the saved record says as `resim_budget_ms`.

### Observations on dev `ff70b82` (2026-10-07)

The same sweeps on dev at `ff70b82`, which has what the Sumo performance work added after `1b5305b` (Box3D's world going on from its image in parts, `tide_same_bytes` on the web, the wide page hash, and what changed of a world hashed on a few threads): labels `page-hash-native` and `page-hash-web`, one run a ping, the same seed, harness and machine, with the user's own programs at about 18% load. Mean/max delay in ticks:

| Ping | Native first | Native late | Browser first | Browser late |
| ---: | --- | --- | --- | --- |
| 0 | 0.14/1 | 0.18/1 | 3.69/12 | 2.01/9 |
| 50 | 2.33/6 | 2.07/5 | 4.92/13 | 8.65/17 |
| 100 | 6.6/9 | 6.47/9 | 6.54/13 | 9.59/17 |
| 150 | 7.71/11 | 7.45/12 | 10.43/15 | 13.43/28 |

Against `1b5305b`: natively the same within a run's noise (the 100 ms run had frames over 50 ms on all three machines, the host's included, and reads two ticks higher); the browser's late joiner lower at 50 and 100 ms (8.65 and 9.59 where it was 16.13 and 26.32), one run each, and its 100 ms run has one 5-second stall of the page. The per-tick costs did fall (`sumo_costs`: keeping and hashing a full ring's world from 0.28 ms to 0.06 with threads called in), and none of it reaches the delay: `adjust_delay` goes by `tick_cost`, a moving mean of what `run_predicted`'s tick alone took, which has neither the hash nor the snapshot's copy in it.

What sets the delay is the policy's shape, which the timelines show. It climbs within seconds of a burst of expensive ticks (joining: 100 to 400 ticks in the first second; each round's ring raised: a 9 ms tick natively and 25 in a browser, then a second of 1.3 to 2 ms ticks; the rims falling), one tick per round trip, and comes back one tick per `SLACK_SECONDS` (5 s) plus a round trip, only while `(ahead - verified) * tick_cost` is under a third of the budget. So after a burst a client settles where its lead is about budget / (3 × tick_cost), two ticks natively and one in a browser, and the rest of its round trip is delay. The browser's late joiner at 50 ms goes 17 16 15 14 13 12 11 10 9 8 7 6 5 4 3 2 1 in its "delay every 5 s" line, one a step, until the next round's ring is raised.

`--budget 12` (`page-hash-native-budget12`, native, the same seed) says how much of it is the policy: mean delay 0.29/0.55 at 50 ms, 3.47/2.86 at 100 and 5.99/4.33 at 150 (first/late), with ticks run again up from 4 to 12 a second to 14 to 45, and 13 in one frame at the most. The bursts still take it to 9 to 11, and it still comes down one a step. Charging what re-simulation costs in practice (Sumo runs 0.03 to 0.15 ticks again a frame, in one or two rollbacks a second), or a quicker way back down, would move the delay more than any further cut of the tick; both are changes to the policy, which these measurements led to.

### The policy rebuilt (2026-10-07)

The delay now goes by what rollbacks cost in practice (`adjust_delay` in `engine/src/session.c`; AGENTS.md, Networking, has the rules): each rollback is measured as it happens, its first tick apart (that's where Box3D's world is made again), a rollback that would come now is reckoned over the client's lead of late, sampled only while it's level with the server, and may take up to a frame, never less than a 60th of a second; what rollbacks cost an update on the whole may take up to the budget; a client with no rollback for two seconds takes no delay and gives back what it has; the server's own machine never takes any; and a step up waits for a lead that's settled, so a stall of the machine or the network, after which a client runs far ahead for a moment, adds none. The harness records what the delay goes by (`rollback_ms`, `rollback_most_ms`, `resim_ms`, the last three values of `tide_run_netcode`, since this change). The same sweeps, labels `actual-cost-native` and `actual-cost-web`, mean/max delay in ticks:

| Ping | Native first | Native late | Browser first | Browser late |
| ---: | --- | --- | --- | --- |
| 0 | 0/0 | 0/0 | 0/2 | 0.78/13 |
| 50 | 0/0 | 0/0 | 0.28/3 | 1.14/9 |
| 100 | 0.13/4 | 0.18/3 | 1.91/18 | 1.82/12 |
| 150 | 0.63/6 | 0.65/5 | 2.51/8 | 6.07/46 |

Natively that's no delay through a whole round at any of the four pings, where it was 2.2, 6.5 and 7.6 ticks at 50, 100 and 150 ms: what's left is a few ticks for the seconds after the second round's ring is raised, when ticks are dearer, which the timelines show at their end. The price is the rollbacks themselves, each the frame it lands in: at 150 ms a client is eleven ticks ahead, past the 9 that others' inputs are guessed for, so it runs the tail again at each confirmation while someone holds a stick (140 ticks a second, 52 view corrections a second, which the `[Position]` smoothing takes), and a frame over 50 ms came three times in 150 s where the old policy's delay kept that at none. The browser's first joiner is a third to a quarter of what it was. Its late joiner, and the first at 100 ms, still spike when the page stalls: the headless Chrome pages share one machine and GPU, and at the second round's ring raise the 150 ms late joiner's page fell behind for good (306 frames over 25 ms, 257 inputs late, 212 ticks run again a second), where delay is the policy's answer to a machine that can't keep up. A machine stalling and then its network stalling, after which a client runs 60 ticks ahead, is `net_a_stall_leaves_no_lasting_delay` in `compiler/tests/e2e/net_test.c`, which checks neither adds delay.

Two cuts came before this one, measured and discarded the same day: one that charged real rollbacks but took every sample as it came, which a 600 ms stall of the machine turned into 41 ticks of delay that took 25 s to come back, and one that gave the browser's host a delay of 2 to 7 ticks from one cold first tick it never measured again, and held a client at a tick of lead forever since only multi-tick rollbacks taught it a tick's cost.

### Full-round observations (2026-10-06)

One run at each ping on Windows, Ryzen 9 7900X, a shared four-thread native pool and four threads per browser page, match seed 0 on both platforms. The native target paces frames at 144 Hz; the rendered browser runs use Chrome 154.0.8037.98 and hardware NVIDIA Lovelace WebGPU, verified on every page. All eight runs complete a round, without a replacement world or run/page errors. Native records no host-skipped time; the browser does not expose that counter. These are observations of the final pre-integration runtime `1b5305b`, rather than a repeated controller comparison.

![Input delay at each ping over time](results/lag-delay.png)

Delay is extra input delay in simulation ticks (16.67 ms per tick). Replay values list mean/p95/maximum ticks per frame, then ticks per second. Update and frame values list p95/maximum milliseconds. These frame distributions omit the first connected frame, which the join window includes separately. “First” and “late” identify the two remote clients. A replay p95 of zero means replay is sparse, rather than absent. Recorded view resets: browser 100 ms joined late: 1. [Summaries and raw-frame references](results/lag.jsonl) retain the host, distributions, individual join windows and earlier diagnostics.

**Native, without GPU draws**

| Ping/client | Delay mean/max ticks | Replay/frame; /second | Update p95/max ms | Frame p95/max ms | Corrections/second | Late/resync |
| --- | ---: | --- | --- | --- | ---: | --- |
| 0/first | 0.05/1 | 0.023/0/1; 3.2 | 2.189/11.023 | 11.245/32.351 | 3.3 | 0/0 |
| 0/late | 0.37/2 | 0.021/0/2; 2.9 | 1.955/11.515 | 13.544/28.827 | 3.6 | 0/0 |
| 50/first | 2.17/5 | 0.049/0/4; 6.7 | 2.027/11.326 | 11.119/29.558 | 3.1 | 1/0 |
| 50/late | 2.24/5 | 0.056/0/5; 7.6 | 2.089/9.262 | 11.253/24.774 | 3.4 | 0/0 |
| 100/first | 4.59/7 | 0.06/0/5; 8.2 | 2.091/11.8 | 11.274/27.823 | 3.3 | 0/0 |
| 100/late | 4.57/7 | 0.068/0/6; 9.1 | 1.93/14.74 | 13.22/32.393 | 3.6 | 4/0 |
| 150/first | 8.09/11 | 0.071/0/10; 9.6 | 1.96/18.799 | 11.222/39.746 | 4 | 14/0 |
| 150/late | 7.78/10 | 0.069/0/10; 9.3 | 1.967/14.876 | 11.527/39.457 | 3.8 | 13/0 |

**Chrome, with GPU draws**

| Ping/client | Delay mean/max ticks | Replay/frame; /second | Update p95/max ms | Frame p95/max ms | Corrections/second | Late/resync |
| --- | ---: | --- | --- | --- | ---: | --- |
| 0/first | 2.93/11 | 0.021/0/11; 2.8 | 4.01/30.425 | 11.73/159.64 | 2.6 | 3/0 |
| 0/late | 1.32/4 | 0.025/0/2; 3.3 | 3.95/56.89 | 11.555/210.555 | 3.3 | 7/0 |
| 50/first | 4.57/9 | 0.027/0/9; 3.6 | 3.675/21.455 | 11.315/139.78 | 3 | 3/0 |
| 50/late | 16.13/27 | 0.052/0/26; 6.6 | 3.855/123.665 | 11.47/196.785 | 0.7 | 53/0 |
| 100/first | 7.08/9 | 0.043/0/10; 5.6 | 3.48/26.985 | 11.04/141.19 | 3.3 | 4/0 |
| 100/late | 26.32/37 | 0.18/0/60; 22.1 | 3.705/601.755 | 11.37/604.165 | 1.7 | 130/0 |
| 150/first | 10.77/13 | 0.026/0/11; 3.4 | 3.935/24.555 | 11.6/123.465 | 2.2 | 1/0 |
| 150/late | 12.46/19 | 0.071/0/20; 9.1 | 4.22/122.235 | 11.83/152.36 | 3 | 71/0 |

The native mean delays rise from roughly zero at 0 ms to two, five and eight ticks at 50, 100 and 150 ms. Browser costs and delays are higher; the timelines also vary as the match changes. The startup late counts describe actual sampled inputs missing their server ticks, including thirteen per native client at 150 ms. They are not unsampled holes. The raw data is consistent with initial clock/feedback settling and update scheduling, but lacks per-packet timestamps to attribute each miss. The regressions separately check that known-stale ticks are not newly sampled.

The seeded browser runs retain substantial startup sensitivity. At 50 ms, the late client has a 533–1920 ms replay burst after receiving its world, with 344 replay ticks in 21 updates, then another shorter burst. Its lead returns to roughly zero by ten seconds, but delay reaches 27 ticks and decays slowly. At 100 ms, a 1.45–3.284 second burst includes 783 replay ticks in 25 updates and a 601.755 ms update. Lead recovers to three ticks by ten seconds, while delay peaks at 37 and remains 18 at the end. The 150 ms late client is milder in this sweep but still peaks at 19 ticks of delay and a 122.235 ms update. Typical update costs conceal these tails. CPU/RTT feedback is a source-consistent inference; internal clock, budget and sampling transitions were not logged. These observations leave the separate ordinary-prediction/clock feedback and slow adaptive-delay recovery problem unresolved.

### Earlier unseeded diagnostics

The earlier browser observations used platform entropy, while native used seed 0. They remain under the `-unseeded` labels and in the [earlier chart](results/lag-delay-unseeded.png), but are excluded from the final matched-seed table and browser join comparison.

The unseeded 100 ms browser late client has a transient startup replay burst: its first second executes 201 explicit replay ticks, and one sequence has ten replay-bearing updates above a 6.94 ms reference interval (144 Hz). Its predicted lead peaks at 25 ticks, falls to five by three seconds and zero by five seconds, while input delay rises to 16 ticks. A later group of actual late inputs around 114 seconds pushes delay as high as 19. Measured answer times rise well above the configured ping during these episodes. The burst ends, but the delay penalty takes much longer to shrink. Pure confirmed-work spikes also occur.

The unseeded 150 ms browser late client is more severe: mean delay is 30.43 ticks (507 ms), with a maximum of 45 (750 ms), a 479.745 ms update, 337 actual late-input counts and one view reset. Its predicted lead reaches 56 ticks, and one sequence has 51 replay-bearing updates above the 6.94 ms reference interval (144 Hz). Update p95 remains only 3.585 ms, but p99 is 23.065 ms; the tail is therefore essential to interpreting this run. The first client in the same run is much milder, averaging 10.97 ticks and peaking at a 22.51 ms update. This change does not solve the separate re-simulation feedback problem; full-round completion does not imply smooth play.

### Browser join comparison

A fresh 50 ms run of the historical baseline page uses the same seed 0, Chrome, worker count, GPU mode and harness as the final page. The baseline is `c455b97` with counters/hooks added, as documented in the artifact metadata. Browser runs have real scheduling and timed inputs, so this is one observation per build rather than a repeated timing distribution. The join window includes the first recorded connected frame and the following second of completed-frame samples. There is no timestamp inside snapshot loading; a long initial callback can extend this window past one wall-clock second from the actual load. “Predicted” includes explicit replay, and first-prediction time is relative to the first connected sample.

| Client/build | Confirmed | Predicted | Replay | Worst update ms | First prediction ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| First/before | 34 | 116 | 30 | 21.67 | 0 |
| First/after | 31 | 79 | 18 | 15.39 | 157 |
| Late/before | 48 | 137 | 56 | 113.24 | 0 |
| Late/after | 63 | 143 | 94 | 123.66 | 351 |

This browser pair is mixed. The first client executes less work and has a shorter worst join update. The late client executes more replay and total work, with its worst join update rising from 113.24 to 123.66 ms; its full-run mean delay rises from 5.38 to 16.13 ticks. The controlled native pairs below show the reduction in stale-world join work more clearly. Browser late counters also change accounting for unsampled holes, so their values are not packet-delivery ratios.

### Changed traffic and world pins

Controlled traces swap only the baseline/final session implementation while reusing identical release game and fixture objects. They identify the first effective server-input differences below; player slots are zero-based. The new client samples these inputs earlier, so the server receives the input for the tick where the old client used its previous/default input. This changes the authoritative world, rather than just its prediction. Later sampled controls and the seeded loss sequence can diverge from there.

| Fixture | First different tick/slot | Old effective input | New effective input |
| --- | --- | --- | --- |
| Sumo | 57/1 | Move (0, 0) | Move (0, 1) |
| Towers | 27/0 | No hands | Hands enabled |
| Sand | 32/0 | Default brush | Brush at (15, 11), material 1 |
| Gallery | 33/0 | Default pen | Pen at (27, 22), color `0x9e3779ff` |

The lossless local-events fixture applies identical server inputs throughout. The old clients sample six obsolete all-zero inputs each, ticks 8–13 at 0.209 seconds; the final clients start at tick 14. Client traffic changes from 10,740 to 10,596 bytes, while server traffic stays 29,132 bytes and effective server inputs remain identical. [Arrival evidence](results/lag-input-arrivals.json) records invocations, decoded and packed input bytes, sample/arrival times, reproduced old/new world pins and artifact hashes, with compressed full traces beside it. These are the pre-integration fixtures described above.

### Whole-ring join comparison

Three alternating native pairs compare the client before this change (`c455b97`, with counters added) and the final runtime (`1b5305b`) against the same Sumo world and game code. The world is 2,394,422 bytes, received over 118 updates (2.0 seconds of made-up match time), at tick 498. The fixture uses 40 ms one-way latency, 15 ms jitter and 5% loss, without worker jobs, a GPU or a tick-cost clock; it isolates join work rather than adaptive input delay. Both clients reach nine ticks ahead one second later, finish with the same 2,321 crates, and receive no replacement world.

| First second after receiving the world | Before | After |
| --- | ---: | ---: |
| Total ticks executed | 254 | 174 |
| Confirmed ticks executed | 119 | 127 |
| Predicted executions, including replay | 135 | 47 |
| Explicit replay ticks | 13 | 0 |
| Most ticks in one update | 120 | 15 |
| Median of the three worst updates | 93.7 ms | 9.4 ms |
| Range of worst updates | 91.6–95.8 ms | 8.8–11.1 ms |

The recorded late count changes from 118 to one, but this includes the corrected treatment of inputs that were never sampled; it is not a comparison of packet delivery rates. [Individual join records](results/lag_join.jsonl) retain every pair, artifact hashes and earlier diagnostic runs. [Runtime and validation metadata](results/lag-metadata.json) identifies the final artifacts and the historical browser baseline. The live measurements use this pre-integration Sumo fixture; subsequent changes from other continuation chats can change the game workload.

The subsequent merge of `7525737` passes all 1,475 native debug and 1,461 web-debug checks, plus 14 native release and 15 web-release affected checks. Combined Sumo/Towers pins reproduce across all four configurations; Sand/Gallery pins stay unchanged. A five-second native and eight-second hardware-WebGPU smoke check verifies the rebuilt three-player telemetry. These bounded checks, retained under `catch-up-integration-smoke`, do not replace the pre-integration full-round measurements above. The validation metadata records the merge and its pins.

### Provisional budget proposal

Keep the existing 4 ms budget in this scoped fix, pending a separate comparison of delay controllers; these runs do not establish it as adequate or optimal for heavy browser play. It estimates the cost of the current predicted lead from measured tick costs; it is not a hard limit on an update's confirmed work, new prediction or replay. A long update with few replayed ticks therefore does not by itself show that this budget is too large. The catch-up change leaves both this default and the delay policy unchanged. The remaining browser feedback bursts and slow delay recovery need their own investigation before selecting a replacement controller.

A fixed share of the nominal display interval could scale the allowance with the intended frame rate, but needs an agreed share and a measured latency tradeoff. Four milliseconds is about 24% of a 60 Hz frame and 58% of a 144 Hz frame. Use the nominal interval for such an experiment: using the interval of an overloaded frame would increase the allowance precisely when the machine is falling behind.

Time left after drawing would need a reliable frame deadline and a definition of drawing cost. The native measurement excludes GPU work, and a browser's CPU frame timing includes submission rather than GPU completion. These runs cannot establish how much GPU time remains. A moving allowance would also move input delay when rendering changes. None of the three alternatives was tested as a controller A/B here; the owner still decides any change.
