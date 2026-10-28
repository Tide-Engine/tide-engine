# Guarded prefetch against the same cache-only baseline

This probe restricts the broader hint path after the retained
[direct comparison](../bevy-cache-prefetch-20261028-paired/README.md) showed a
5.46% paired slowdown at 95% frozen. It compares the guarded candidate directly
with the same frozen cache-only executable and unchanged Bevy ECS 0.20.0.
These are separate batches; their timings are not pooled, and the guarded and
broad candidates were not alternated directly against each other.

Windows 11 / Ryzen 9 7900X, Clang 23.1.2 and Rust 1.95.0 / LLVM 22.1.2 retain
optimized O2 baseline x86-64 flags without fast math/FMA. Game sources,
component fields, starting values and 60 Hz arithmetic are unchanged.

## Direct measurements

Three alternating adjacent Before/Tide/Bevy triples per case preserve 27
successful processes, each with 200 warmup and 400 measured ticks. All logical
digests agree across implementations, rounds and the two 99%-frozen thread
budgets. This is 10,800 measured ECS ticks, 5,400 ECS warmup ticks and another
7,200 measured C-reference ticks inside the before/current Tide processes.
The handwritten C reference runs after Tide within each process and provides
context, rather than another independently alternating competitor.

Milliseconds per tick, median of three process medians. Change is the median
paired percentage change in time, not a ratio of the displayed medians:

| Frozen | Execution budget | Cache-only Tide | Guarded Tide | Bevy | Paired Tide time change |
| --- | ---: | ---: | ---: | ---: | ---: |
| 95% | 1 | 1.10825 | 1.13305 | 0.25115 | +1.49% |
| 99% | 1 | 0.11855 | 0.11080 | 0.05550 | -6.66% |
| 99% | 4 | 0.04940 | 0.04660 | 0.03935 | -5.53% |

Guarded process medians span 1.12480-1.14875, 0.10995-0.11140 and
0.04610-0.04755 ms respectively. Cache-only ranges are 1.09935-1.13620,
0.11780-0.11960 and 0.04880-0.04960 ms. Bevy remains faster in all three cases.
The adverse 95%-frozen observation remains in the report despite overlapping
ranges. [summary.md](summary.md) and [summary.json](summary.json) retain all
process medians, ranges and paired ratios; raw output and launches remain in
raw/ and records.jsonl.

The complete batch spans 23:09:10-23:09:58 UTC on October 28. The shared mutex,
explicit 15-second quiet guard and 250 ms inventory polling record no quiet
reset or contended process. All 27 processes exit zero; no attempts in this
directory are excluded. Desktop activity, CPU placement, clocks and temperature
remain uncontrolled. No confidence interval, pooled-tick statistic or portable
speed factor is inferred.

## Guard and scope

Membership caching still requires `pass < ceil(rows / 16)`. Native prefetch
additionally requires `pass < ceil(rows / 64)`, accessed component lanes totaling
32-128 bytes, and a changed component. It warms up to the first four selected
rows and hints four selected rows ahead, with bounded selected-index reads.
WASM omits the hints. Cache payload, validity checks, component layouts, math,
row/deferred ordering and runtime API are unchanged from cache-only.

Hints are disabled in the 95%-frozen workload, but this is not an identical
binary with one runtime setting toggled. The generated function parameters and
whole-program layout differ from cache-only. The remaining +1.49% observation
therefore does not isolate a cause or establish absence of regressions. The
broad candidate's earlier +5.46% adverse result remains evidence for narrowing
the hint guard; different batch conditions prevent subtracting the two
percentage changes to claim a directly measured improvement.

These are warmed scheduled ECS CPU ticks with stable membership. Setup, final
digest, per-tick hashing, retained snapshots, rollback, networking, rendering
and application frame loops are excluded. Cold construction, changing
membership, snapshot/reload speed and whole-engine FPS are not measured.
The [cache-only probe](../bevy-cache-20261028-probe/README.md) retains its separate
correctness and raw-header checks. The
[final comparison](../bevy-cache-20261028-final/README.md) records the broader
suite separately. [Shared methodology](../../bevy/README.md) defines the thread
budgets and validation rules.

## Identity and reproduction

`Before` is labelled `66b8ddb8f+cache-only`, an uncommitted candidate rather
than a Git commit. Its frozen directory is `build/bevy-cache-only-20261028`.
The wide executable SHA-256
`0446136ad2ab014af456a2a962484d71fd54bbcd72449de55608a99bb7dbcba7`
matches both the earlier broad-prefetch baseline and the cache-only probe's
measured candidate. Guarded wide is
`455e586b712041306307e742697385c40a07c1be2a899a9b772ad2afb13da1e2`;
Bevy remains
`d83769333a56edceb3aed09ab38878774f8491e9c8b0ba3444db3bcfbeaed081`.

metadata.json pins the guarded compiler sources:

- compiler/src/codegen.c: `0342cfe8fafde011bf038d492c4705e91f934b8548f2dedefd5dd4f52f295e30`
- compiler/src/rows_cache_codegen.inc: `485acd89238f99fd406e1fe0bcc9674b4903ab3dc00825a4f45e5fb5a8db8a7d`

These identities match the repository source used for the guarded candidate.
The recorded Git HEAD `66b8ddb8f` names its base, not all uncommitted changes;
reproduction requires source with the hashes above. This directory supplies no
compiler patch. Preserve the frozen baseline before rebuilding the matching
repository sources with the normal release flags and pinned Bevy executable,
then invoke the subset in a PowerShell session:

```powershell
$env:TIDE_HIDDEN = '1'
& ./bench/bevy/run.ps1 -OutputDir bench/results/bevy-guarded-new -Rounds 3 `
    -BeforeBuildDir build/bevy-cache-only-20261028 -BeforeRevision 66b8ddb8f+cache-only `
    -FilterSweep -OnlyCases 'filtered-frozen95-1m-1','filtered-frozen99-1m-1','filtered-frozen99-1m-4'
python bench/bevy/analyze.py bench/results/bevy-guarded-new
```

Recheck the saved evidence without running either engine:

```powershell
$env:TIDE_HIDDEN = '1'
python bench/bevy/analyze.py bench/results/bevy-cache-prefetch-20261028-guarded
```
