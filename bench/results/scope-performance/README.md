# Large-scope performance pass, October 11

Baseline: `969e1406bbcee1e9c8c64fd0812d418b1bb42967`. Machine: Ryzen 9 7900X (12 cores/24 threads), RTX 4080 SUPER, NVIDIA 595.79, Windows, Clang 23.1.2. Release uses `-O2` and unchanged deterministic flags. No timing assertion or platform-independent speed guarantee is added.

All nine simulation-versus-C benchmarks, Sand/Gallery sessions, drawing workloads and four skinned modes were rerun three times before editing. Their `scope-baseline-r1/r2/r3` rows, plus `scope-baseline-steady-r1/r2/r3`, remain in the existing JSONL ledgers. [Baseline metadata](baseline-metadata.json) records the source revision, options and frozen binary hashes. Independent C digests agree. Session worlds converge with no resyncs; all three runs retain Sand hash `0x8860658ee80a5c69` and Gallery hash `0x5f3832ca362c55d6`. Gallery sends 4.41 MB on join and takes 2.917 s for its slowest catch-up with 50 ms each-way latency, 10 ms jitter and 1% loss. This rerun measures the existing eight-player session fixtures, not a large-player-count capacity limit.

Two internal changes follow that baseline: forced-inline map lookup specializes fixed generated kinds, and explicit skinned capture uses the existing unique current pack proof to skip repeated comparisons of unchanged model dependencies. Map storage/hash/order, simulation bits, pose arithmetic, palette bytes, APIs and rendering work are unchanged.

## Paired measurements

Three pairs alternate before/after, after/before, before/after. The original and candidate binaries are frozen inside this checkout. A shared benchmark mutex and 90-second quiet guard exclude builds/tests/other benchmark processes; the guard remains checked throughout each measurement. GPU clocks/state/power are sampled every 250 ms in [gpu.csv](gpu.csv), with empty monitor stderr. The monitor spans map and capture runs too, so low-power samples outside rendering are expected. No GPU-speedup attribution is made.

Numbers below are medians of three run medians; percentage reductions are medians of the paired ratios, so they need not exactly match the ratio of displayed aggregates. [Raw rows](paired-rows.jsonl), [summary](summary.json) and [metadata](paired-metadata.json) retain exact numbers, p95s, GPU samples, options, source/binary hashes, exits and capture hashes.

| Map/set workload, one thread | Before ms/tick | After ms/tick | Paired time reduction | After / C |
| --- | ---: | ---: | ---: | ---: |
| 16,384 keys, no replacements | 1.979 | 1.735 | 12.8% | 10.25× |
| 16,384 keys, 1,024 replacements | 1.799 | 1.593 | 11.6% | 4.98× |
| 65,536 keys, no replacements | 9.099 | 8.031 | 12.0% | 6.47× |
| 65,536 keys, 1,024 replacements | 7.149 | 6.330 | 11.5% | 4.89× |

Offering twelve workers yields similar 11.6–12.9% reductions; shared map writes remain serial. Every before/after digest matches the independent C implementation. This is a whole fixture tick, including map/set hits, misses, updates, bookkeeping and mandatory world hashing, not time per single lookup.

| Characters / mode | View recording before → after (ms) | Paired view time reduction | Submission before → after (ms) | Synchronized total before → after (ms) |
| --- | ---: | ---: | ---: | ---: |
| 1,000 shared pose | 0.495 → 0.421 | 14.9% | 0.0095 → 0.0094 | 1.553 → 1.491 |
| 1,000 independent poses | 2.320 → 2.234 | 4.2% | 0.443 → 0.439 | 4.133 → 4.065 |
| 10,000 shared pose | 4.980 → 3.960 | 21.5% | 0.034 → 0.034 | 12.872 → 11.884 |
| 10,000 independent poses | 24.683 → 23.471 | 4.7% | 6.015 → 6.060 | 42.942 → 41.087 |

All eight cases receive 100 GPU samples per run, after 20 warmup frames. Draw/upload counts remain identical. Static/rest controls gain no meaningful frame time: 1K static recording increases by 0.00685 ms, 10K static by 0.03110 ms, with totals 0.848→0.848 and 5.437→5.472 ms; rest totals are 1.200→1.189 and 8.718→8.709 ms. Those costs are disclosed, not counted as wins. CPU and GPU overlap; native totals synchronize framebuffer readback and are not pipelined FPS. Exact PNG bytes match before/after for all four modes at 1K and independent animation at 10K.

## Remaining limits

10K independently animated characters still require about 23.47 ms recording and 6.06 ms submission, plus overlapping GPU work. Each frame uploads 40.96 MB of palettes and draws 60.48 million triangles, in 11 calls including clear. The fixture deliberately has no LOD, visibility culling or match simulation. This pass does not establish readiness for a 60 Hz animated game of that size. Further sampler/pose preparation and upload work needs measurement; animation LOD and visibility policy need a game workload and the owner's design decisions.

The steady 16K map case remains an order-of-magnitude performance bug against C. Other rerun gaps remain: Life 7.49× C on one thread / 2.52× on twelve, Events 7.33× / 7.34×, nested buckets 6.06× / 2.46×. No claim is made that this patch fixes those. Multi-snapshot restore/replay comparisons and larger multiplayer session fixtures remain coverage gaps. Sand/Gallery exercise network joins, prediction/replay and snapshots at their existing sizes.

## Validation and reproduction

Full warning-free debug builds and suites pass: **2,078 native**, **2,048 web**, including safety, deterministic hashes/pose bits and OpenGL/WebGL/WebGPU pixels. New regression tests cover collision probes across map chunks/index wrap with independent snapshots, and owned immediate/deferred skinned captures across equal-content and changed reloads followed by unload. See [validation.json](validation.json) and CTest summary logs. Linux/macOS/MinGW/Android/sanitizer coverage was not run; CI was not requested.

To repeat on Windows, build release in this checkout at the baseline revision and run `bench/results/scope-performance/run-baseline.ps1` before editing. Then apply the code change, rebuild `bench_skinned` and `versus_lookup`, and run `bench/results/scope-performance/run.ps1`. Both scripts set `TIDE_HIDDEN=1`; all runs are bounded and child processes hidden. Do not overwrite the preserved baseline with candidate binaries. Run `node bench/results/scope-performance/analyze.mjs` from the repository root with Node 24.16+. Saved labels must be unique for a new experiment; the analyzer intentionally checks the original 24 lookup and 48 skinned rows. Binaries, full raw process logs and PNGs remain under `build/performance-baseline` and `build/performance-pairs`; metadata keeps their hashes, while the compact evidence here excludes binary/image payloads.
