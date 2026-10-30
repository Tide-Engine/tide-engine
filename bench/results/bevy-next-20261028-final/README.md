# Smaller cached sparse tasks

This pass streamlines task setup for retained sparse selections. The comparison
is scheduled ECS CPU tick work; it does not measure engine FPS. Final results
and process ranges are in [summary.json](summary.json) and
[summary.md](summary.md). The adjacent baseline is the retained-selection build
`c30a131da1e6782347f734fc925d91767660c79a`.

## Results

At 99% frozen, paired Tide tick time falls 12.69% serially and 10.74% with
four offered threads. At 95% frozen it falls 4.79%. Bevy remains faster in
these selective cases: about 2.1 times as fast serially and 1.3 with four
threads at 99% frozen, using the displayed medians.

| Case | Before Tide ms | Current Tide ms | Bevy ms | Paired Tide change |
| --- | ---: | ---: | ---: | ---: |
| 99% frozen, serial | 0.11190 | 0.09770 | 0.04730 | -12.69% |
| 99% frozen, four threads | 0.04540 | 0.04090 | 0.03220 | -10.74% |
| 95% frozen, serial | 1.04980 | 1.02315 | 0.22600 | -4.79% |

Controls are mixed. Adverse paired medians are +0.24% for 100k particles,
+1.26%/+0.64% for 1M particles serial/four, +1.62%/+1.56% for churn serial/four,
+1.52%/+1.33% for 1%/10% frozen, and +5.33% for 90% frozen. All these ranges
overlap the baseline ranges. A separate
[15-process 90%-frozen check](../bevy-next-20261028-density-control/summary.md)
is also adverse at +10.16%, with overlapping ranges and substantial variation
in the unchanged C reference. This remains a performance concern; neither the
wrapper nor environmental variation is established as the sole cause. The
original slow task's machine body is unchanged. No universal improvement or
tie with Bevy is claimed. Batches are not pooled or normalized by C timings.

The final batch runs from 23:57:51 UTC on October 28 to 00:02:51 UTC on
October 29, with zero quiet resets, contention events or failed processes.
All final logical digests agree across implementations, rounds and thread
budgets. The complete table and ranges are in summary.md; comparison.html
shows all 16 cases interactively.

## Implementation and identities

Eligible split systems with 32-128 bytes of accessed component data have a
small task wrapper. A whole chunk uses it only with a current, exactly validated
rare selection and exclusive ownership of every changed component lane page.
It checks all those references before changing metadata, invalidates the same
page hashes as before, then calls the existing cached rows helper directly.
Readonly component pages may remain shared. Empty selections return untouched.

An explicit non-inlined copy of the original task handles shared pages, split
chunks, dense/stale caches and unsupported archetypes. Original task numbers,
archetype offsets, queue contexts, work estimates, scratch marks, row signatures,
math and deferred order are preserved. No chunk grouping, new runtime API,
component/world/cache-layout change or raw-header growth is introduced. The
prefetch loop remains unchanged. The native wide header is still 168 bytes.

Measured compiler source SHA-256:

- codegen.c: `23ef9022a97ceaddb9294dd4af4ad5090d21a84dba13d8b38a70d01241eca645`
- unchanged rows_cache_codegen.inc: `485acd89238f99fd406e1fe0bcc9674b4903ab3dc00825a4f45e5fb5a8db8a7d`

The baseline/current wide executable SHA-256s are
`455e586b712041306307e742697385c40a07c1be2a899a9b772ad2afb13da1e2` and
`5b0935ac280c571ea1460b26ebd815c80134048680f4df0025f0e8602bfcaa2f`.
The unchanged Bevy executable is
`d83769333a56edceb3aed09ab38878774f8491e9c8b0ba3444db3bcfbeaed081`.
Game inputs and source are unchanged. Metadata records the pre-commit HEAD;
the source hashes identify the measured local compiler changes.

## Procedure and limits

Windows NT 10.0.26200.0, Ryzen 9 7900X (12 physical/24 logical CPUs), Clang
23.1.2, Rust 1.95.0/LLVM 22.1.2 and Bevy ECS 0.20.0. Both builds use O2 and
baseline x86-64 without FMA or fast math; Bevy retains normal change detection.
No affinity, clock, power or temperature control is applied.

Five adjacent alternating Before/Tide/Bevy rounds cover 16 cases and 240
processes, each with 200 warmup and 400 measured ticks. Before/current processes
also time the existing C reference internally. This is 96,000 measured ECS ticks,
48,000 ECS warmup ticks and 64,000 additional measured C-reference ticks.
Reported times are medians of process tick medians; percentages use median
paired ratios, so a percentage need not equal the ratio of displayed medians.
Ranges are process-median min/max, not confidence intervals.

The runner holds `Local\TideClaudeContinuationBenchmark`, requires 15 continuous
quiet seconds and polls known competing work every 250 ms. All raw processes,
including adverse observations, are retained. Ordinary desktop activity and
interference between polls remain uncontrolled. The runner verifies source
stability through before/after hashes. The analyzer verifies complete triples,
options, binary identities, Bevy samples/statistics, logical digests, exit codes
and raw stdout hashes.

Setup, final digests, retained snapshots, per-tick hashing, rollback, networking,
rendering and application frame loops are excluded. Snapshot/restore correctness
does not establish snapshot, cold-cache or changing-membership performance.
The C reference still scans all rows at high frozen densities; Bevy uses its
ordinary efficient archetypal filter. Tide's canonical component lanes remain
unchanged. No hardware cache-miss attribution or performance floor is claimed.

## Validation and reproduction

All 18 scoped optimized CTests pass per platform on native and WASI. The expanded
fixture compares the wrapper with its original slow task across two archetypes,
full chunks and tails, two changed components, a shared readonly component,
mixed reference counts 1/2/3, retained snapshots, stale keys and empty selections.
It checks table bytes, reference/hash-validity metadata, complete world hashes
and raw packs. Existing independent tests retain exact math, structural and
deferred ordering, dynamic membership, packed restore and split-chunk coverage.
A global string field exercises scratch-mark argument plumbing.

[validation.json](validation.json) records 78 Bevy/native Tide/C/WASI Tide/C
comparisons, including 95% and 99% frozen. The
[diagnostic hash/pack comparison](diagnostics/hash/comparison.json) records
75 baseline/current native world hashes and complete raw packs matching byte
for byte, with no normalization. The generated wide.h files differ only in
their simulation ID macro.
V8 accepts the existing seven row variants within its 1,000-parameter limit.
No full suites, browser/GPU, Bevy web, other OS/mobile or sanitizer execution was
run locally; no extra CI workflow was dispatched.

[The 90-process prototype report](../bevy-next-20261028-probes/README.md) retains
all six generated-C variants and controls. The smaller task path was selected;
skipping divisions and versioning the prefetch loop showed little isolated gain.
Its profile is diagnostic and perturbs timing. Prototype and final batches are
not pooled.

Build and validate using [bench/bevy/README.md](../../bevy/README.md). Freeze the
baseline executables before rebuilding, then run:

```powershell
$env:TIDE_HIDDEN = '1'
./bench/bevy/run.ps1 -OutputDir bench/results/<new-evidence> `
  -BeforeBuildDir <frozen-c30-build> -BeforeRevision c30a131da -FilterSweep
python bench/bevy/analyze.py bench/results/<new-evidence>
python bench/bevy/validate.py --output build/<validation>.json
```
