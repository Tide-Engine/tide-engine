# Density-aware filtering and churn optimization

Windows 11 / Ryzen 9 7900X, Clang 23.1.2, Rust 1.95.0 / LLVM 22.1.2,
Bevy ECS 0.20.0, optimized O2 baseline x86-64 without fast math/FMA. The frozen
before binaries match the [original comparison](../bevy-20261028/README.md),
revision `aac950038`. Game sources, inputs, component fields, counts and tick
lengths remain unchanged; Bevy is the same executable. metadata.json pins all
source/generated-C/configuration/binary identities. The measured compiler source
SHA-256 is `0b49106e1bb895e5051af958f906619d28d5583e957b90e8eb8ebcba519aff79`.

The compiler makes two internal changes. Mixed sparse/scene-filtered in-place
loops select matching indices in 128-row blocks, preserving their order and
never executing skipped bodies. It uses the existing count to choose this path
only when at least a quarter of the rows pass and a quarter skip; biased masks,
ownership/C/World-alias paths and COW/apart loops retain ordinary filtering.
Strictly eligible arithmetic followed by conditional self-destroy computes
updates/predicates first, then records the destroys in order from 128 stack
flags. Other bodies retain their original behavior. Plain destruction also
reuses its already generation-checked location. No runtime API, world storage,
math, scheduling or entity-ID policy changes. The original generated header
layouts are unchanged; their normal simulation build identities change.

Five alternating adjacent Before/Tide/Bevy triples per case use 200 warmup and
400 measured ticks. All 210 processes succeed and agree on final logical
digests across engines, rounds and thread budgets. This is 84,000 measured ECS
ticks, 42,000 warmup ticks and another 56,000 measured handwritten-C reference
ticks within the Tide/before processes. The complete batch runs
21:12:27-21:17:33 UTC under the shared mutex and explicit 15-second quiet guard,
with 250 ms inventory polling: no contention, quiet reset or failed attempt is
detected. Ordinary desktop activity, placement, clocks and temperature remain
uncontrolled. [Reproduction and detailed timing boundaries](../../bevy/README.md).

Milliseconds per tick, median of five process medians. Change is the median of
paired current/before time changes, which can differ from a ratio of medians:

| Case | Threads | Before Tide | Current Tide | Bevy | Paired Tide time change |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1M components, half frozen | 1 | 4.01150 | 2.29915 | 2.66665 | -42.37% |
| 1M components, half frozen | 4 | 1.17425 | 0.77645 | 1.43990 | -33.14% |
| 2,000 bullet spawns/tick | 1 | 0.57090 | 0.40030 | 0.53390 | -27.22% |
| 2,000 bullet spawns/tick | 4 | 0.54610 | 0.42030 | 0.62120 | -21.51% |

Current median paired Bevy/Tide ratios are 1.160/1.707 for half-frozen filtering
and 1.229/1.423 for churn (one/four-thread budgets). Bevy's churn process medians
vary substantially: 0.44675-0.57530 ms serial, 0.45225-0.86780 with four offered
execution threads. These observations establish no portable engine-speed factor.
All ten original cases, four extra densities, per-process ranges, ratios and
unchanged C reference timings are in [summary.md](summary.md) and
[summary.json](summary.json). Raw outputs/launches/hashes remain in raw/ and
records.jsonl; no tick pooling, percentile, confidence interval or FPS is inferred.

## Density guard and remaining sparse-query cost

The [intermediate candidate](../bevy-optimized-20261028/README.md) always compacted
mixed rows. Its separate 90%-frozen diagnostics consistently regressed, prompting
the guard. Its complete 150-process batch, diagnostics, sampler logs, code patch
and runner are retained rather than replacing those results.

The final serial density sweep, before/current/Bevy ms:

| Frozen | Before | Current | Bevy | Paired current time change |
| --- | ---: | ---: | ---: | ---: |
| 1% | 3.14740 | 3.14530 | 3.21910 | -0.07% |
| 10% | 3.34230 | 3.31715 | 2.81815 | +0.85% |
| 90% | 1.52240 | 1.43645 | 0.78880 | -3.67% |
| 99% | 0.46025 | 0.48700 | 0.04915 | +10.76% |

The 99%-frozen result remains adverse and is retained. Its before/current
process-median ranges overlap (0.43970-0.52270 / 0.43935-0.63875 ms). The ordinary
filtered loop's instruction text is identical when compiling the frozen/current
generated sources with the benchmark flags, after removing debug directives and
normalizing local labels (diagnostics/); that does not establish equal whole-
program layout or runtime cost. Unchanged particle/wide controls also vary,
including a +7.30% paired serial 1M-particle result. No universal no-regression
claim follows. Very selective sparse queries still substantially favor Bevy;
this pass does not address that remaining query/storage cost.

## Correctness and coverage

Thirty scoped optimized CTests pass on native and WASI during implementation;
the 18 affected checks on each are rebuilt and pass after the density refinement
(logs included). Coverage includes rows/sparse/scene selection, COW/replay,
World/Owner aliases, lifecycle/structural changes, hierarchy/prefabs/shards,
commands/events, tasks and unchanged cross-platform/random pins. The two new
fixtures check 12,294 sparse/scene rows with ordered visits and a separate
4,116-birth C model with exact arithmetic, destroy order, swapped rows, LIFO slot
reuse/generations, snapshots and packed restore. C queue observers and effectful
helper fallbacks retain original row/command interleaving. A generated-C check
requires the numeric update/flag path and rejects C/helper fallbacks.

validation.json records 66 further Bevy comparisons against native Tide/C and
WASI Tide/C over early/steady ticks, small/empty counts and 0/50/100% filtering.
The analyzer additionally verifies complete adjacent triples, successful exits,
raw hashes, options, executable identities, Bevy samples/statistics and invariant
digests. The implementation and initial runner changes received read-only review.
No full suites, browser/GPU, Bevy web, other OS/mobile or sanitizer run is claimed;
no extra CI workflow was dispatched.

Recheck this evidence without running either engine:

```powershell
$env:TIDE_HIDDEN = '1'
python bench/bevy/analyze.py bench/results/bevy-optimized-20261028-final
```

These are scheduled ECS CPU ticks. Setup, final digest, per-tick world hashing,
retained snapshots, rollback, networking, rendering and application loops are
outside the timed workload. No engine-wide or frame-rate conclusion follows.
