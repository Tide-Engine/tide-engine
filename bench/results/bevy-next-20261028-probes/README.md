# Cached sparse task probes

The isolated `fasttask` probe reduces 99%-frozen serial Tide tick time by
14.26% in the median of three adjacent baseline/probe pairs. Its follow-up
controls show an 11.21% reduction with four offered threads. Bevy remains
faster in both selective cases. These results support promoting the private-page
task shortcut alone; division specialization and prefetch loop versioning are
not selected. The OR-reference variant is noisy and does not establish a win.

These are manually edited generated-C experiments against frozen `c30a131da`,
not measurements of the later general compiler implementation. No runtime API,
component layout, arithmetic, task number, queue context or scheduled chunk
grouping changes. Production validation and final compiler timings belong to
the subsequent report.

## Construction and identities

The baseline revision is `c30a131da1e6782347f734fc925d91767660c79a`.
Before is the preserved `build/bevy-before-c30a131da/bin/versus_wide.exe`,
SHA-256 `455e586b712041306307e742697385c40a07c1be2a899a9b772ad2afb13da1e2`.
Its generated wide source SHA-256 is
`9667a05b51e0ff17aeb76db0ab58ba2c8108c1b73b630c8ebe519830380c1637`.
The frozen compiler inputs are codegen.c
`0342cfe8fafde011bf038d492c4705e91f934b8548f2dedefd5dd4f52f295e30`
and rows_cache_codegen.inc
`485acd89238f99fd406e1fe0bcc9674b4903ab3dc00825a4f45e5fb5a8db8a7d`.

The same Bevy executable is used throughout, SHA-256
`d83769333a56edceb3aed09ab38878774f8491e9c8b0ba3444db3bcfbeaed081`.
It uses Bevy ECS 0.20.0, Rust 1.95.0 / LLVM 22.1.2, O2, no LTO and 16 codegen
units, with `-C target-cpu=x86-64 -C target-feature=-fma`. The workload and
independent C reference are unchanged from the [comparison contract](../../bevy/README.md).

The [construction script](diagnostics/probes/build-probes.ps1) copies frozen
generated C/header/pack and runtime/platform/font/versus libraries. It verifies
the current main/model/headers against the frozen revision, then changes only
`tide_task_Shift` and/or `tide_rows_Shift_6`. It checks that replacing these
functions restores the original source. Exact generated files, source patches
and task/row disassembly are retained under [diagnostics/probes](diagnostics/probes).
The [build manifest](diagnostics/probes/manifest.json) records every actual
Clang invocation, input/library hash, source hash and binary hash. Its
`execution: Compiled only` describes the construction step; the seven timing
directories below record the later executions.

Clang 23.1.2 targets native x86-64 with strict C17, `-O2 -DNDEBUG`, debug
information, warnings as errors, `-fno-fast-math -ffp-contract=off`, and
function/data sections. No architecture tuning or arithmetic relaxation is
added. The simulation ID remains
`796e3ea03bf3e41f1956c906762efedda679494ac577c5a519eab240453cd72e`.

| Probe | Generated wide.c SHA-256 | Executable SHA-256 |
| --- | --- | --- |
| c30_control | `9667a05b51e0ff17aeb76db0ab58ba2c8108c1b73b630c8ebe519830380c1637` | `6509e121791f0deed3ee25e9e1108fb1634e009b2bee2064848b5b305a6c866e` |
| nodiv | `43462ded5faf057f52c09f4dc74a011c0f366eb18373290e2456776f09913dc1` | `8961fb53505be85e99a6f78a1d991d01b221a30721d13f420ccd1064f0ad3d10` |
| fasttask | `4ad2b76e7332d97668df86f948e31ea3b0a3bb390cedc790157f2fe12528341f` | `a84ffc034c6a4d8eeb53de164ee3fd133ed47a0fde5ae244ca23630aa5ef6f2f` |
| versioned | `65bfe60e88c58d7776c33f500ecabf4fee965ba63a6c26e32e1105a913526e46` | `22ed816b7bd91936b783ad34092ea576f589f6427a506256cc27c80b8aef2f69` |
| fasttask_versioned | `7525ce462f0af0d5dcf22e5922a8188b4c2b1d7cbcd045c91c205ed89545ed42` | `1905762d4a5fb6aa6ee74258696a69583cec3c3ec8e39c627ae468b06f65d472` |
| fasttask_versioned_or | `d882ae818c907cfa681cea0e55ad993a289386f79fec05822b08c7a17716516d` | `743f7b62cb5d0a5684fa2770644a0f6a96ca7d21de9262527fb074b366416dc5` |

The fasttask controls reuse the exact fasttask binary above. No probe executable
is rebuilt between its serial measurement and controls.

## Six serial probes

All use 1M twelve-float components, the sparse Frozen filter, 99% frozen, no
retained snapshots and one offered execution thread. Each directory contains
three adjacent Before/Tide/Bevy triples, reversed to Bevy/Tide/Before in round
two: nine processes per probe, 54 total. Every process uses 200 warmup and 400
measured ECS ticks. Before and Tide processes also time the unchanged C reference
after their Tide way; that additional work can affect later placement/temperature.

- `c30_control`: relink the unchanged frozen generated C using the probe build.
- `nodiv`: special-case `pieces == 1` in task/chunk decomposition and piece row
  bounds. Other cases retain division. The static task disassembly still has
  five divide instructions, so that count alone proves no hot-path reduction.
- `fasttask`: for a single-piece, validated rare cached chunk, first prove all
  twelve changed lane pages have `refs == 1`. Only then mark their hashes stale,
  obtain existing page data and call the unchanged cached row helper. Empty
  selections return before mutation. Every miss/shared-page/pieced case calls
  the original task body, moved into an explicit noinline slow function.
- `versioned`: move the prefetch density decision outside the row loop and split
  its hinted prefix from its at-most-four-row tail. First-four warmup, lookahead
  distance four, hinted addresses, selected row order and arithmetic stay the same.
- `fasttask_versioned`: combine those two transformations.
- `fasttask_versioned_or`: additionally replace the twelve short-circuit ownership
  checks with an OR of all twelve reference counts, retaining the same fallback.

Milliseconds per tick, median of three process medians; brackets give their
minimum and maximum. Change is the median paired current/before percentage,
which can differ from a ratio of the displayed medians.

| Probe | Before Tide | Probe Tide | Bevy | Paired change |
| --- | --- | --- | --- | ---: |
| [c30_control](c30_control/summary.json) | 0.11600 [0.11495, 0.11820] | 0.11545 [0.11480, 0.11760] | 0.04815 [0.04750, 0.04850] | -0.47% |
| [nodiv](nodiv/summary.json) | 0.11360 [0.11340, 0.11380] | 0.11315 [0.11270, 0.11340] | 0.04775 [0.04770, 0.04800] | -0.40% |
| [fasttask](fasttask/summary.json) | 0.11375 [0.11340, 0.11430] | 0.09800 [0.09690, 0.09815] | 0.04810 [0.04790, 0.04910] | -14.26% |
| [versioned](versioned/summary.json) | 0.11240 [0.11235, 0.11400] | 0.11225 [0.11195, 0.11270] | 0.04800 [0.04740, 0.04855] | -0.13% |
| [fasttask_versioned](fasttask_versioned/summary.json) | 0.11270 [0.11220, 0.11290] | 0.09570 [0.09560, 0.09600] | 0.04735 [0.04730, 0.04770] | -15.17% |
| [fasttask_versioned_or](fasttask_versioned_or/summary.json) | 0.11730 [0.11435, 0.16990] | 0.09750 [0.09740, 0.15820] | 0.04845 [0.04800, 0.06680] | -14.74% |

Division specialization and loop versioning alone are within the variation of
the unchanged relink control. Versioning has an adverse first pair (+0.31%).
The combined variant is paired against frozen c30, not against fasttask alone;
its lower median therefore does not isolate an extra benefit from versioning.
Code placement, linking and between-batch conditions also remain uncontrolled.

The OR variant retains all three pairs: current/before are 0.15820/0.11730,
0.09740/0.16990 and 0.09750/0.11435 ms, changes +34.87%, -42.67% and -14.74%.
Its first Bevy observation is also slow (0.06680 ms versus 0.04800/0.04845).
Neither removing that adverse round nor treating the unusually slow second
baseline as a clean gain is justified. OR is not selected.

C-reference observations vary too: before-C in the combined batch spans
1.14780-1.38595 ms, and the OR batch reaches 1.57175 ms before / 1.46655 ms
current. Even the unchanged c30 relink has before-C 1.17445-1.21955 versus
current-C 1.13040-1.15780 ms. All C and ECS observations remain in
[the complete per-round table](diagnostics/process-medians.md) and each summary.
They are controls for variation, not evidence that these changes optimize C.

## Fasttask follow-up controls

The later [controls batch](fasttask-controls/summary.json) has four cases,
three alternating triples each: 36 processes. The same 200/400 tick counts and
frozen Before/fasttask/Bevy binaries apply.

| Case | Threads | Before Tide | Fasttask Tide | Bevy | Paired change |
| --- | ---: | --- | --- | --- | ---: |
| Unfiltered wide | 1 | 1.32425 [1.29135, 1.33535] | 1.29490 [1.29110, 1.31905] | 2.47510 [2.42750, 2.48960] | -1.22% |
| Half frozen | 1 | 2.15860 [2.15285, 2.16830] | 2.14460 [2.13715, 2.17925] | 2.51825 [2.49780, 2.61730] | -0.99% |
| 95% frozen | 1 | 1.02775 [1.02080, 1.07940] | 0.99750 [0.98415, 1.01035] | 0.27340 [0.26780, 0.39985] | -3.59% |
| 99% frozen | 4 | 0.04550 [0.04520, 0.04660] | 0.04070 [0.04040, 0.04110] | 0.03020 [0.03010, 0.03070] | -11.21% |

The unfiltered and half-frozen medians move modestly despite those chunks
retaining the old body. Half-frozen round three is adverse by +1.23%, and
unfiltered round three is effectively unchanged (-0.02%). At 95% frozen the
paired improvements span -1.69% to -7.59%; the first Bevy process is unusually
slow (0.39985 ms versus 0.26780/0.27340). Four-thread 99% changes span -9.07%
to -12.66%. Before-C there spans 0.61925-0.74925 ms, while the unfiltered
current C spans 0.97955-1.06565. No observations are excluded or pooled between
cases, batches or implementations.

These controls support the shortcut in the measured sparse case, not a universal
speedup. Neither 95% nor 99% frozen beats Bevy. Frequent membership changes,
cold caches, retained snapshots/rollback, other game bodies and other CPUs are
not measured by this probe.

## Evidence audit and timing scope

The [read-only audit](diagnostics/audit.py) verifies all 90 processes: complete
alternating triples, successful exits, exact options, executable hashes, raw
stdout SHA-256s and parsed results, 400 Bevy samples/statistics, process-median
summaries and paired changes. It also verifies the byte-preserved source archive,
build-manifest inputs and available binaries/PDB. Its [saved output](diagnostics/audit.json)
records 36,000 measured ECS ticks, 18,000 ECS warmup ticks and another 24,000
measured handwritten-C ticks. C warmup work also runs inside the 60 Before/Tide
processes. All final digests agree across implementations, rounds and thread
budgets for each workload; the 99% digest is `0xa683eb7fe00d6622`.

Measurements run on Windows 11 build 26200 / Ryzen 9 7900X, 12 physical cores
and 24 logical processors. The six serial batches span 23:46:41-23:49:13 UTC
on October 28; controls span 23:49:55-23:51:07 UTC. The shared
`Local\TideClaudeContinuationBenchmark` mutex, explicit 15-second initial quiet
guard and 250 ms known-work polling record zero quiet resets and zero contended
processes across all seven completed batches. No failures or measurement
exclusions are recorded. Desktop activity, affinity, clocks, power, temperature
and interference outside the inventory guard are uncontrolled.

The runner hashes its inputs before launches and verifies them again before
setting `complete: true`. Every batch ended before compiler promotion began.
The later live codegen.c edit is deliberately resolved to the preserved c30
source for this historical audit; it is not substituted for the measured input.
Raw stdout, UTC launches, argument arrays and binary hashes remain in each
directory's `records.jsonl`, `metadata.json` and `raw/`.

Timing covers scheduled ECS ticks and their ordinary deferred work. Setup,
final digest, per-tick world hashing, retained snapshots, networking and rendering
are outside the interval. Standard Bevy change detection remains enabled.
Three process medians give limited noise characterization, not a confidence
interval, whole-engine FPS result or portable speed factor. Digest agreement is
scoped correctness evidence for these manually edited native kernels; it does
not validate a general compiler change, WASM or ARM execution.

## Diagnostic profile and reproduction

The separate [Tide-only baseline profile](diagnostics/profile/profile-c30-99-serial.txt)
removes only the C-reference entry from the host's ways array. Its
[host patch](diagnostics/profile/main.patch), [build script](diagnostics/profile/build.ps1)
and [manifest](diagnostics/profile/manifest.json) retain exact source/build/PDB
identities. The profiling executable SHA-256 is
`9dc8fe933735295c85ad2f0014bea3f017a75141ee9952dcfe3a05455d821c05`.
The sampling invocation was:

```powershell
tide_sample.exe --top 25 --lines 25 -- ./build/bevy-next-profile/versus_wide_tide_only.exe --count 1000000 --tagged 1 --frozen 99 --kept 0 --threads 1 --warmup 200 --ticks 20000 --json
```

The shared mutex was held after an explicit five-second wait for this
diagnostic; that invocation had no automated contention polling or quiet guard.
No sampler interval option was set. Sampling perturbs execution,
and its 20,000-tick duration/digest differs from the 400-tick comparisons.
Its 0.1484 ms median is not a performance-comparison result. Innermost samples
attribute 39.30% to cached rows, 13.35% to lanes_shared, 13.32% to float multiply,
12.22% to Step, 6.20% to the task wrapper and 6.01% to float add. Outermost
attribution is 70.85% rows and 23.11% task. This motivates task preparation
experiments but does not prove cache misses, a causal speed factor or a speed floor.

The compact [archive manifest](diagnostics/archive-manifest.json) maps original
paths to byte-preserved generated sources, patches, disassembly, build commands
and diagnostic files. Recoverable compiler/check/header/repository copies and
the redundant compile database are hash-only entries; binaries/libraries/PDBs
remain in the ignored build directories with their hashes recorded. No binary
is committed here. Historical scripts expect their original build-directory
layout and frozen c30 libraries; restore that layout in a separate checkout to
reconstruct a probe. Do not overwrite preserved evidence. The actual Clang
command arrays in the manifests, rather than the inherited CMake compile
database, describe these manual builds.

After reconstructing the six probe directories and preserved baseline, the
recorded timing settings can be reproduced into fresh output directories:

```powershell
$env:TIDE_HIDDEN = '1'
$names = 'c30_control','nodiv','fasttask','versioned','fasttask_versioned','fasttask_versioned_or'
foreach ($name in $names) {
    ./bench/bevy/run.ps1 -BuildDir "build/bevy-next-probes/$name" -BeforeBuildDir build/bevy-before-c30a131da -BeforeRevision c30a131da -Rounds 3 -Warmup 200 -Ticks 400 -QuietSeconds 15 -FilterSweep -OnlyCases filtered-frozen99-1m-1 -OutputDir "build/bevy-next-reproduction/$name"
    python bench/bevy/analyze.py "build/bevy-next-reproduction/$name"
}
./bench/bevy/run.ps1 -BuildDir build/bevy-next-probes/fasttask -BeforeBuildDir build/bevy-before-c30a131da -BeforeRevision c30a131da -Rounds 3 -Warmup 200 -Ticks 400 -QuietSeconds 15 -FilterSweep -OnlyCases wide-1m-1,filtered-1m-1,filtered-frozen95-1m-1,filtered-frozen99-1m-4 -OutputDir build/bevy-next-reproduction/fasttask-controls
python bench/bevy/analyze.py build/bevy-next-reproduction/fasttask-controls
python bench/results/bevy-next-20261028-probes/diagnostics/audit.py
```

These commands document reproduction; no workload was rerun while auditing
this evidence. No browser/GPU, web performance, other OS/mobile, sanitizer or
full-engine validation is claimed by this report.
