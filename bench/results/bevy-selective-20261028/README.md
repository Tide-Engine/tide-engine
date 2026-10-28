# Rare sparse traversal follow-up

Windows / Ryzen 9 7900X, Clang 23.1.2, Rust 1.95.0 / LLVM 22.1.2,
Bevy ECS 0.20.0; O2 baseline x86-64 without fast math/FMA. Before is the frozen
`a2564c773` implementation from the [previous optimization pass](../bevy-optimized-20261028-final/README.md).
Game sources, counts, inputs, arithmetic and the Bevy executable are unchanged.
The current measured compiler source SHA-256 is
`fcfc03c95da80f321c3e872d983b450b32889d60a8210a22c07040fdb81e4a03`.
metadata.json pins source, generated C, configurations, executables and supplied
baseline files. Before/current control executables can differ through normal
simulation build identities; all three generated headers match after normalizing
only `TIDE_SIMULATION_ID`.

Eligible sparse-only in-place systems now use a separate rare-match loop when
the existing count finds fewer than one passing row per 16 rows. It compares
bounded 16-byte presence blocks, extracts matching bits, then visits their rows
in ascending order. A short tail uses scalar reads. SSE2 and ordinary WASM SIMD
have direct bit extraction; other targets extract comparison lanes with integer
operations. Each presence column is compared before intersection, so any nonzero
presence byte works. No row body is speculatively executed.

The existing count scan remains. Mixed-density index compaction, mostly passing
loops, COW/apart copying and conservative scene/Owner/C/World/task fallbacks keep
their contracts. No runtime API, world layout, math order, deferred ordering,
scheduling, entity IDs or language behavior changes. Implementation contracts
are in `notes/language.md`, Rare sparse row masks.

## Results

Five alternating adjacent Before/Tide/Bevy triples per case use 200 warmup and
400 measured ticks. All 240 processes succeed and agree on logical digests across
engines, rounds and thread budgets. This is 96,000 measured scheduled ECS ticks,
48,000 warmup ticks and another 64,000 measured handwritten-C reference ticks
inside the before/current processes. The batch runs 21:44:16–21:49:59 UTC under
the shared mutex, 15-second initial quiet guard and 250 ms inventory polling.
No contention, quiet reset or failed attempt is detected. Desktop activity,
placement, clocks and temperature remain uncontrolled.

Milliseconds per tick, median of five process medians. Change is the median
paired current/before time change, rather than a ratio of the two medians:

| Case | Threads | Before Tide | Current Tide | Bevy | Paired Tide time change |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1M components, 99% frozen | 1 | 0.46430 | 0.27090 | 0.04880 | -39.50% |
| 1M components, 99% frozen | 4 | 0.12660 | 0.08510 | 0.03450 | -33.16% |
| 1M components, 95% frozen | 1 | 1.46480 | 1.41225 | 0.22585 | -4.80% |
| 1M components, 90% frozen | 1 | 1.46725 | 1.59270 | 0.76095 | +2.77% |
| 1M components, half frozen | 1 | 2.22900 | 2.27215 | 2.56560 | +1.04% |
| 1M components, half frozen | 4 | 0.76445 | 0.77065 | 1.33380 | -1.61% |
| 2,000 bullet spawns/tick | 1 | 0.39065 | 0.37870 | 0.43625 | -0.24% |
| 2,000 bullet spawns/tick | 4 | 0.38040 | 0.38585 | 0.47675 | +1.43% |

The 99%-frozen before/current process-median ranges are 0.43360–0.49015 /
0.26440–0.29630 ms serial and 0.12570–0.13550 / 0.08220–0.08690 ms with four
offered threads. Bevy remains about 5.5 times faster serially and 2.5 times faster
with four threads. At 95% frozen the ranges overlap (1.44290–1.52150 /
1.38780–1.47095), and Bevy still leads substantially.

The adverse 90%-frozen result is retained. Its ranges overlap broadly:
1.45470–1.74895 / 1.48105–1.64390 ms. The tighter guard keeps the common 90%
case on ordinary filtering; the result does not establish that whole-program
layout or runtime cost stayed equal. Unchanged controls also vary, and no
universal no-regression claim follows. Particle, unfiltered wide, half-frozen
and churn cases still favor Tide against this Bevy implementation in this batch.

All 16 cases, every process median, ranges and paired ratios appear in
[summary.md](summary.md) and [summary.json](summary.json). [comparison.html](comparison.html)
offers the same medians/ranges as an interactive comparison. Raw stdout,
commands, UTC launches, digests and hashes remain in raw/ and records.jsonl.
Ticks are not pooled, and no confidence interval or FPS is inferred.

## Retained probe and validation

The [18-process preliminary probe](../bevy-selective-20261028-probe/README.md)
used masks below one-quarter passing density. It improves 99% frozen by 40.02%
but regresses 90% by 4.22%, motivating the narrower guard. Its raw results,
exact reconstructed compiler patch and measured runner are retained. Its
timings are separate from the final five-round estimates.

All 18 affected optimized CTests pass on native and WASI. Seventeen pass in the
recorded initial runs; the stale four-function parameter assertion is updated
to cover all six variants and that test passes separately on both. The generated
WASM object also satisfies V8's 1,000-parameter limit. The new independent C
model covers four intersecting presence columns, empty/short tables and
16/128/2048 boundaries, ordered visits, deferred removal, byte updates,
serial/pieced execution, snapshot immutability and packed restore. Existing
tests cover scene filtering, World/Owner aliases, tasks and structural changes.

[validation.json](validation.json) records 66 Bevy comparisons against native
Tide/C and WASI Tide/C. A standalone exhaustive check covers all 65,536 masks,
including presence encodings 1/128/255. The generated-C checks pin bounded loads,
guarded ascending bit enumeration, the density guard and fallback paths.
The implementation received independent read-only review.
[Diagnostics](diagnostics/README.md) retain checks, kernel source/assembly and
sampler logs. The ARM kernel check is assembly-only; no ARM runtime result is
claimed. No full suites, browser/GPU, Bevy web, other OS/mobile or sanitizer
execution was run locally; no extra CI workflow was dispatched.

## Reproduce

Preserve all three baseline executables under `<before>/bin` before rebuilding
the candidate. Build/validate native and WASI using the commands and exact
timing boundaries in [bench/bevy/README.md](../../bevy/README.md), then run:

```powershell
$env:TIDE_HIDDEN = '1'
& ./bench/bevy/run.ps1 -OutputDir bench/results/bevy-selective-new `
    -BeforeBuildDir <before> -BeforeRevision a2564c773 -FilterSweep
python bench/bevy/analyze.py bench/results/bevy-selective-new
```

To recheck this saved evidence offline, run
`python bench/bevy/analyze.py bench/results/bevy-selective-20261028`.

These are scheduled ECS CPU ticks. Setup, final digest, per-tick world hashing,
retained snapshots, rollback, networking, rendering and application loops are
outside the timing. Normal Bevy change detection remains enabled; this is not
its maximum possible throughput or a whole-engine performance comparison.
