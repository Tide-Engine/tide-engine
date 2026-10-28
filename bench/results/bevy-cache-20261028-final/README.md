# Retained sparse-query membership

This follow-up substantially narrows the selective-query gap, but does not beat
Bevy in the 95%/99%-frozen cases measured here. At 99% frozen, Tide tick time
falls 58.33% serially and 45.35% with four offered threads by paired medians.
Tide still takes about 2.3 times as long as Bevy serially and 1.2 times as long
with four threads. The comparison remains scheduled ECS CPU work, rather than engine FPS.

## Implementation and identities

Before is the frozen `66b8ddb8f` build from the
[previous pass](../bevy-selective-20261028/README.md). The wide executable SHA-256
is `b7d7792afda75809d7e667b98c1ee37ddc4df8ae91a0156d39e0dba94a08816f`.
Game sources, inputs, arithmetic, counts and the Bevy executable remain unchanged.
The measured compiler source SHA-256s are:

- codegen.c: `0342cfe8fafde011bf038d492c4705e91f934b8548f2dedefd5dd4f52f295e30`
- rows_cache_codegen.inc: `485acd89238f99fd406e1fe0bcc9674b4903ab3dc00825a4f45e5fb5a8db8a7d`

Eligible generated match worlds now own derived selection plans. Each chunk
retains its presence pages and records the exact filter columns/required flags,
archetype, shift, row count, passing count and up to 128 ascending row indices.
Retaining pages forces normal presence changes through existing copy-on-write;
unchanged page identities therefore prove unchanged membership without relying
on a hash. Rebuilding happens serially before worker dispatch. Workers validate
the key again and use the existing scan if it no longer matches.

Chunks contain at most 2,048 rows. Only counts below ceil(rows/16) consume saved
indices, so at most 127 entries are read. Pieces use bounded binary searches.
Other densities can reuse cached counts, while their established row loops stay
in place. COW/apart copying and conservative scene/Owner/C/World/task fallbacks
retain their behavior. Cache ownership never copies from one world to another;
free releases retained pages and allocations. Saturated reference counts fall
back safely.

Native cached loops with 32–128 bytes of component data and a changed component
also prefetch four selected rows ahead, warming the first four. Hints apply only
below one match per 64 rows. Every address comes from a validated index; neither
skipped bodies nor float operations are speculated. WASM omits hints and their
control flow. The cache uses existing runtime calls and introduces no runtime API.

Component layouts, world hashes, arithmetic, entity order and deferred changes
are unchanged. Eligible worlds append an opaque cache pointer; its raw packed
header slot is zeroed and incoming pointers are immediately discarded. The native
wide header grows from 160 to 168 bytes. Delta networking already uses a separate
value header. The generated cache ABI marker forces migration before any private
payload/cleanup layout change; same-layout reloads validate owned filter metadata,
not an old ordinal or static descriptor. Contracts: notes/language.md,
Retained sparse row selection, and notes/runtime-ownership.md.

Retaining a filter page can require a copy when membership changes. Cold/rebuilt
plans scan presence; this batch does not measure frequent tag changes or cold
query latency. These are real cache costs, not a guarantee for every workload.

## Final comparison

Windows / Ryzen 9 7900X, Clang 23.1.2, Rust 1.95.0 / LLVM 22.1.2 and
Bevy ECS 0.20.0; O2 baseline x86-64 without fast math/FMA. Five adjacent
Before/Tide/Bevy triples per case reverse order on alternating rounds. Each
process uses 200 warmup and 400 measured ticks. All 240 processes succeed and
agree on final logical digests across implementations, rounds and thread budgets.
This is 96,000 measured scheduled ECS ticks, 48,000 warmup ticks and another
64,000 measured handwritten-C reference ticks within before/current processes.

The batch runs 23:11:56–23:18:01 UTC under the shared mutex, explicit 15-second
quiet guard and 250 ms known-work polling. No quiet reset, failed attempt or
contended process is detected. Placement, ordinary desktop activity, clocks,
power and temperature remain uncontrolled. metadata.json pins source,
configuration, generated C/header and binary identities before/after the batch.

Milliseconds per tick, median of five process medians. Change is the median
paired current/before percentage change, not a ratio of the displayed medians:

| Case | Threads | Before Tide | Current Tide | Bevy | Paired Tide time change |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1M components, 99% frozen | 1 | 0.27005 | 0.11000 | 0.04865 | -58.33% |
| 1M components, 99% frozen | 4 | 0.08660 | 0.04525 | 0.03760 | -45.35% |
| 1M components, 95% frozen | 1 | 1.47770 | 1.14145 | 0.24145 | -21.74% |
| 1M components, 90% frozen | 1 | 1.52940 | 1.39450 | 0.91955 | -10.09% |
| 1M components, 10% frozen | 1 | 3.48270 | 3.23935 | 3.09880 | -7.38% |
| 1M components, 1% frozen | 1 | 3.26095 | 3.07755 | 3.25825 | -5.62% |
| 1M components, half frozen | 1 | 2.32270 | 2.31355 | 2.62990 | -0.90% |
| 1M components, half frozen | 4 | 0.78000 | 0.75885 | 1.50110 | -3.22% |

At 99% frozen, before/current ranges are 0.26055–0.28150 / 0.10575–0.11335 ms
serial and 0.08280–0.08950 / 0.04480–0.05720 ms with four threads. Bevy ranges
are 0.04810–0.05700 and 0.03465–0.04935 ms. Four-thread current/Bevy ranges
overlap, but their paired median still favors Bevy; this is no demonstrated tie
or win. At 95% frozen, Bevy remains much faster.

Controls vary too: four-thread particles and churn are adverse by +2.20% and
+3.12% paired, with overlapping ranges. Particle, unfiltered wide, half-frozen
and churn results favor Tide against this Bevy implementation in this batch.
No universal improvement or portable speed factor follows.

All 16 cases, ranges, per-process values and paired ratios are in
[summary.md](summary.md) and [summary.json](summary.json).
[comparison.html](comparison.html) provides an interactive view. Raw stdout,
arguments, UTC launches, digests and hashes remain in records.jsonl and raw/.
Ticks are not pooled and no confidence interval or FPS is inferred.

## Retained experiments

- [Seven initial locality probes](../bevy-locality-20261028-probes/README.md):
  63 processes. Index batching, prefetching and four-wide SIMD without membership
  caching remain roughly five times slower than Bevy. The adverse rebuild control
  and all noisy results remain intact.
- [Cache-only probe](../bevy-cache-20261028-probe/README.md): 27 processes establish
  most of the gain, alongside separate dynamic-state correctness checks. Its quiet guard
  reset between launches; no timed process was marked contended.
- [Nine cached locality probes](../bevy-cache-locality-20261028-probes/README.md):
  81 processes. Staging matching rows into dense scratch arrays is slower than
  direct cached traversal, including the general all-field scatter variant.
  The narrower six-field scatter is specific to this known numeric body.
- [Broad prefetch pair](../bevy-cache-prefetch-20261028-paired/README.md): 27 direct
  cache-only/current comparisons improve 99% frozen but regress 95% by 5.46%.
- [Guarded prefetch pair](../bevy-cache-prefetch-20261028-guarded/README.md): 27
  comparisons retain 6.66%/5.53% serial/four-thread gains at 99%, while 95% remains
  adverse by 1.49% with overlapping ranges. Those observations motivated the final
  hint guard and are not pooled with the final batch.

Bevy walks matching archetype entity indices and stores a Motion's fields
together. Tide's canonical component lanes still scatter selected accesses.
That is a plausible remaining locality cost, not a measured cache-miss attribution
or a proven performance floor. No hardware counters or canonical-layout change
were part of this pass.

## Validation and reproduction

All 33 unique scoped optimized CTests per platform pass on native and WASI;
the 18 affected checks per platform pass again after the final helper/guard
change. New tests independently model dynamic sparse membership, full/empty/rare
states, moved last rows, later births, exact float/byte values, ordered visits,
snapshots, warm-destination copy/restore and hostile raw-header pointers. Private
tests cover filter/key changes, page replacement, empty-plan cleanup and saturated
references. Same-layout filter changes are simulated through private helpers;
no actual cache-bearing DLL filter-flip test is claimed. Existing migration,
delta, visibility, hierarchy, task and deterministic pin tests also pass.

[validation.json](validation.json) records 66 final Bevy/native Tide/C/WASI Tide/C
comparisons. [diagnostics/hash-comparison.json](diagnostics/hash-comparison.json)
records 75 native baseline/final world-hash and packed-state matches: removing
only the added zero cache-header slot reproduces every baseline raw packed byte.
The diagnostic host, before/final rows and test logs are retained in diagnostics/.
V8 accepts all seven generated row variants within its 1,000-parameter cap.
Independent read-only reviews found no blocking issue. No full suites, browser/GPU,
Bevy web, other OS/mobile or sanitizer execution was run locally; no extra CI
workflow was dispatched.

Build and validate using [bench/bevy/README.md](../../bevy/README.md). Freeze all
three baseline executables under `<before>/bin` before rebuilding, then run:

```powershell
$env:TIDE_HIDDEN = '1'
& ./bench/bevy/run.ps1 -OutputDir bench/results/bevy-cache-new `
    -BeforeBuildDir <before> -BeforeRevision 66b8ddb8f -FilterSweep
python bench/bevy/analyze.py bench/results/bevy-cache-new
```

Recheck this saved evidence offline with
`python bench/bevy/analyze.py bench/results/bevy-cache-20261028-final`.
Setup, final digest, per-tick hashing, retained snapshots, rollback, networking,
rendering and application loops are outside the timed work. Normal Bevy change
detection remains enabled; its maximum throughput and whole-engine equivalence
are not claimed.
