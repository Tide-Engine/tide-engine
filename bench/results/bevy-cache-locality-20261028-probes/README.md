# Locality probes with cached selections, 2026-10-28

These nine native scratch variants start from the new persistent-selection-cache
implementation and manually edit its generated `tide_rows_Shift_6` helper. The
unchanged cached control takes 0.11640 ms per tick; the prefetch/vector variants
take 0.10565–0.11380 ms and the staging variants 0.13955–0.16140 ms. None beats
Bevy, which takes roughly 0.048 ms. These are diagnostic generated-C edits,
not a production staged-row implementation or proof of a causal prefetch gain.

The staged variant that writes all twelve component lanes back takes 0.16140 ms.
It is the relevant full-component write-back control for a general staging
proposal. The six-lane alternatives use knowledge that this particular numeric
Shift body changes only position and velocity. There is no compiler proof here
that arbitrary game code leaves the other six lanes untouched. Even those
specialized alternatives are slower than the cached control in their separate
batches. No staged path from these probes was integrated into the compiler.

## Work, identities and procedure

The case is `filtered-frozen99-1m-1`: 1,000,000 twelve-float components,
`--tagged 1 --frozen 99`, one execution thread, no retained world. The seed
produces exactly 10,089 moving and 989,911 frozen rows. Each process performs
200 warmup and 400 measured ticks; its first warmup tick only spawns. Timing
includes the scheduled ECS tick and cache preparation/lookup. Initialization,
final digest, retained snapshots, per-tick hashing, rollback, networking,
rendering and application frame loops are excluded. Bevy ECS 0.20.0 retains its
ordinary sparse-tag-filtered query and normal change detection.

Each variant runs three adjacent triples: Before/prototype/Bevy,
Bevy/prototype/Before, then Before/prototype/Bevy. That is nine processes per
variant, 81 in total. The handwritten C way is also timed inside each Before
and prototype process, rather than as a separate process. All 81 exit zero,
all final digests equal `0xa683eb7fe00d6622`, all raw stdout SHA-256s agree with
their records, and all nine summaries validate. Every adverse or noisy process
remains in the evidence.

Before is the frozen build of
`66b8ddb8fb49e0b25a861909945891b8b9326f65` (`66b8ddb8f`), without the new
persistent cache. Its wide executable SHA-256 is
`b7d7792afda75809d7e667b98c1ee37ddc4df8ae91a0156d39e0dba94a08816f`.
The prototype seed is the **locally modified cache implementation** generated
while HEAD still named that revision. Thus the manifest's Git revision alone
does not identify the current-cache source. The archived generated source,
header, patches and hashes identify it exactly. Across these nine variants,
only `tide_rows_Shift_6` changes; current-cache ownership, validation, dispatch,
other generated helpers, game, main/model/pack, headers and runtime inputs are
held constant. The `cached` variant has an intentionally empty patch.

The Bevy executable is
`d83769333a56edceb3aed09ab38878774f8491e9c8b0ba3444db3bcfbeaed081`.
The cached-control source is
`48e638fd2623217e7711a763658bbfa3d58e420873bc99225312d7144bfcbc15`,
its header is `d4a6406eaebece40ff90b4cab6719cce97274634b21fb1452ebddaad1cb5a067`,
and its simulation ID is
`35d5e44cd31b3dbcecbfad5f3aafa27496e0865aab6b1979d8b660065bf06827`.
[The manifest](diagnostics/manifest.json) records all variant source/executable
identities and shared input hashes. Its `execution: "Compiled only; no executable
runs or timings"` describes the build step; the separate records capture the
subsequent measured runs.

The host reports Windows NT 10.0.26200.0 and Ryzen 9 7900X, 12 physical/24 logical
CPUs. Clang 23.1.2 uses strict C17, O2, warnings as errors,
`-fno-fast-math -ffp-contract=off` and baseline x86-64. All nine use copied common
headers, math, runtime/platform/versus/font libraries and their identical build
flags. Rust 1.95.0/LLVM 22.1.2 uses Cargo O2, no LTO and
`-C target-cpu=x86-64 -C target-feature=-fma`; compiler versions differ. No
affinity, clock, power or thermal controls are applied.

The runner holds `Local\TideClaudeContinuationBenchmark`, requires 15 continuous
quiet seconds before running, and polls known competing work every 250 ms.
There are **60 quiet-guard reset observations**: 35 for `prefetch8` and 25 for
`prefetch_all`, recording CTest/CMake work outside measured process intervals.
The other seven batches record zero resets. These are repeated observations,
not counts of distinct jobs. All 81 process records have no detected contention
and no contention events. Ordinary desktop activity and interference between
polls remain uncontrolled. The batches run sequentially from 22:49:27 to
22:54:19 UTC, including waits; the variants are not directly paired against
each other.

## Results

Times are the median of three process tick medians, followed by the minimum and
maximum of those medians in brackets, in milliseconds. The last column is the
median of the three paired prototype changes **against the frozen 66b8 Before**,
not against the `cached` control. Negative means less time than Before. Ticks
are not pooled across processes.

| Variant | Before ms [range] | Prototype ms [range] | Bevy ms [range] | Paired change vs 66b8 |
|---|---:|---:|---:|---:|
| [cached](cached/summary.json) | 0.27920 [0.26310–0.31510] | 0.11640 [0.11545–0.11910] | 0.04780 [0.04775–0.04895] | −58.65% |
| [prefetch4](prefetch4/summary.json) | 0.26220 [0.25910–0.26570] | 0.10565 [0.10490–0.10665] | 0.04930 [0.04830–0.05025] | −59.51% |
| [prefetch8](prefetch8/summary.json) | 0.27330 [0.26835–0.28240] | 0.11345 [0.11075–0.11430] | 0.04750 [0.04750–0.04755] | −58.73% |
| [prefetch_all](prefetch_all/summary.json) | 0.28200 [0.27630–0.29185] | 0.11350 [0.11335–0.12350] | 0.04770 [0.04740–0.05125] | −58.92% |
| [vector4](vector4/summary.json) | 0.27540 [0.26045–0.29015] | 0.11380 [0.11280–0.12130] | 0.04875 [0.04820–0.04905] | −59.04% |
| [staged_soa](staged_soa/summary.json), six-lane scatter | 0.28520 [0.27300–0.31880] | 0.13955 [0.13955–0.14080] | 0.04840 [0.04795–0.04870] | −51.07% |
| [staged_soa_all](staged_soa_all/summary.json), twelve-lane scatter | 0.30170 [0.28780–0.43425] | 0.16140 [0.15850–0.17465] | 0.05220 [0.04790–0.05335] | −46.50% |
| [staged_soa_lanes](staged_soa_lanes/summary.json), lane-major gather/six-lane scatter | 0.28325 [0.27955–0.29690] | 0.14610 [0.14440–0.14800] | 0.04810 [0.04800–0.04830] | −48.42% |
| [staged_aos](staged_aos/summary.json), six-lane scatter | 0.29630 [0.28180–0.37325] | 0.15850 [0.15110–0.21700] | 0.04950 [0.04935–0.05140] | −46.38% |

For completeness, the unchanged C controls have the following process-median
summaries. They perform their existing branchless frozen-row work, so they are
contextual controls, not a best-practical selective-query implementation.

| Variant batch | C inside Before ms [range] | C inside prototype ms [range] |
|---|---:|---:|
| cached | 1.21460 [1.17335–1.31395] | 1.23245 [1.17430–1.33290] |
| prefetch4 | 1.26625 [1.19585–1.36670] | 1.24935 [1.21370–1.26820] |
| prefetch8 | 1.24520 [1.24195–1.27525] | 1.39105 [1.20800–1.69200] |
| prefetch_all | 1.28510 [1.27170–1.41625] | 1.22565 [1.21610–1.40825] |
| vector4 | 1.20900 [1.19905–1.29500] | 1.21295 [1.20570–1.23925] |
| staged_soa | 1.29540 [1.19435–1.72060] | 1.23450 [1.23405–1.25530] |
| staged_soa_all | 1.40490 [1.25095–1.76295] | 1.20395 [1.17115–1.66085] |
| staged_soa_lanes | 1.26060 [1.19040–1.27280] | 1.20535 [1.19850–1.26590] |
| staged_aos | 1.27980 [1.24095–1.31395] | 1.32310 [1.22785–1.53980] |

The unchanged Before executable and C controls vary between batches. The large
paired reductions above include the persistent cache, since Before predates it.
They must not be presented as prefetch or staging gains over cached execution.
The 0.10565 ms prefetch4 observation suggests a candidate for direct comparison;
these sequential scratch batches do not isolate it from placement or machine
variation. All staging medians are higher than the cached control's separately
measured 0.11640 ms, and the full twelve-lane case is worse than the specialized
six-lane case. No result here demonstrates a path that beats Bevy.

## Transformations and equivalence limits

The cached caller already validates its presence-page/filter/count key and
supplies ascending selected indices only for a rare chunk: fewer than
`ceil(rows / 16)` matches, with at most 2,048 rows. Thus at most 127 indices can
be used, and any piece sees a subrange. Every new scratch buffer has 128 slots.
The [cached patch](diagnostics/cached.patch) is empty because it preserves that
current-cache helper exactly.

[prefetch4](diagnostics/prefetch4.patch) and
[prefetch8](diagnostics/prefetch8.patch) prime the first four/eight selected rows
in the current range, then request that many selected rows ahead while running
the original update loop. [prefetch_all](diagnostics/prefetch_all.patch) requests
every selected row before the update loop. Each request addresses all twelve
valid four-byte lanes, with write intent for the first six, read intent for the
rest, locality 3. Guards keep all selected-index reads inside the piece.

[vector4](diagnostics/vector4.patch) adds
`#pragma clang loop vectorize(assume_safety) vectorize_width(4) interleave_count(1)`
to the original cached-index loop. It relies on unique selected indices in this
numeric row body, preserving the arithmetic expressions and contraction flags.
The manifest's `packed_float` field counts matching disassembly lines; it is not
a runtime instruction count or an independent speedup measurement.

Every staged variant gathers **current** component values afresh on every call;
no staged field values persist between ticks. Real entity and scene handles are
copied alongside the selected values. The SoA variants use twelve 128-float
arrays and call the unchanged all-pass `tide_rows_Shift_2` over the dense scratch
range, then scatter back to the original selected rows:

- [staged_soa](diagnostics/staged_soa.patch) gathers twelve lanes per selected
  row and scatters only the first six lanes.
- [staged_soa_all](diagnostics/staged_soa_all.patch) has the same gather and
  writes all twelve lanes back, retaining the full mutable-component contract.
- [staged_soa_lanes](diagnostics/staged_soa_lanes.patch) gathers one lane across
  the selected rows before moving to the next lane, then scatters six lanes.
- [staged_aos](diagnostics/staged_aos.patch) gathers `Crate[128]`, calls the
  original numeric `tide_system_Shift` in selected order, and scatters six lanes.

For this known body, the six-lane forms retain the same final component bytes:
the untouched push/drag/bounce/radius lanes remain in canonical storage. That
does not establish a general field-write analysis, alias/effect proof or a
staging implementation for arbitrary systems. Full twelve-lane write-back is
the honest general work control among these probes. Matching final digests
validate this workload's result; they do not substitute for broader semantic,
snapshot, reload or cross-platform validation of a proposed production path.

All nine helper disassemblies are preserved as `diagnostics/<variant>.asm`;
[dense-kernel.asm](diagnostics/dense-kernel.asm) preserves the all-pass SoA helper
used by staging. Each result directory keeps `records.jsonl`, `metadata.json`,
raw stdout/stderr, `summary.json` and `summary.md`. These preserve all launch
orders/arguments, process times, timing medians/means, paired ratios, digests,
contention observations and identities. Executables/PDBs and the copied common
libraries/header tree are hash-pinned but not bundled.

## Reproduction

The original checkout root was
`C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7`.
Scratch inputs were frozen under `build/bevy-cache-locality-probe/common`,
variants under `build/bevy-cache-locality-probe/<variant>`, and Before under
`build/bevy-before-66b8ddb8f`. The manifest stores all input SHA-256s and the
twenty-one exact compile/link commands as executable plus argument arrays.
The archived [wide.c](diagnostics/wide.c), [wide.h](diagnostics/wide.h) and
[wide_pack.c](diagnostics/wide_pack.c) are the current-cache starting files,
not the original 66b8 generated output.

Apply each patch independently to a copy of archived `wide.c`. The patch headers
contain original absolute scratch filenames from `git diff --no-index`; adapt
only their filenames when applying elsewhere, or use the saved generator below.
Verify the resulting `variants[].source_sha256` against the manifest. The header
and pack input stay identical across variants; the header's simulation ID is
unchanged by a helper edit. Building 66b8 alone will not regenerate the locally
modified cache implementation, so use the archived starting files.

[build-probes.ps1](diagnostics/build-probes.ps1) is the exact source-replacement,
compile/link and disassembly recipe. It assumes its original location,
`build/bevy-cache-locality-probe/build-probes.ps1`, two directories below the
checkout root. Do not run the archive copy directly in `diagnostics/`: that
resolves a different root. Prepare a release tree with the manifest-matching
engine/platform headers, benchmark main/model sources, runtime/platform/versus/
font libraries and the archived current-cache generated files. The script
copies these inputs into `common/` before compiling, so later builds cannot
change its frozen copies. Prepare the separate frozen Before executables from
the recorded 66b8 revision and the pinned Bevy executable as well.

After preparing those prerequisites, from the checkout root:

```powershell
$env:TIDE_HIDDEN = '1'
$evidence = 'bench/results/bevy-cache-locality-20261028-probes'
$scratch = 'build/bevy-cache-locality-probe'
$generated = 'build/release/bench/versus/tide/versus_wide'
New-Item -ItemType Directory -Force -Path $scratch,$generated | Out-Null
foreach ($file in @('wide.c','wide.h','wide_pack.c')) {
    Copy-Item -LiteralPath "$evidence/diagnostics/$file" -Destination "$generated/$file"
}
Copy-Item -LiteralPath "$evidence/diagnostics/build-probes.ps1" -Destination "$scratch/build-probes.ps1"
& "$scratch/build-probes.ps1"
```

Compare the recreated manifest's input/source hashes and command settings before
timing; different toolchains/absolute paths can change executable bytes. Keep
rechecks separate from this evidence. The runner expects all three versus
executables in each build's `bin/`, even when selecting only wide:

```powershell
$env:TIDE_HIDDEN = '1'
$variants = @('cached','prefetch4','prefetch8','prefetch_all','vector4',
    'staged_soa','staged_soa_all','staged_soa_lanes','staged_aos')
foreach ($variant in $variants) {
    $variantBuild = "build/bevy-cache-locality-probe/$variant"
    New-Item -ItemType Directory -Force -Path "$variantBuild/bin" | Out-Null
    Copy-Item -LiteralPath "$variantBuild/versus_wide.exe" -Destination "$variantBuild/bin/versus_wide.exe"
    foreach ($other in @('particles','churn')) {
        Copy-Item -LiteralPath "build/release/bin/versus_$other.exe" -Destination "$variantBuild/bin/versus_$other.exe"
    }
    & ./bench/bevy/run.ps1 -OutputDir "bench/results/bevy-cache-locality-recheck-$variant" `
        -BuildDir $variantBuild -BeforeBuildDir build/bevy-before-66b8ddb8f `
        -BeforeRevision 66b8ddb8f -Rounds 3 -Warmup 200 -Ticks 400 `
        -QuietSeconds 15 -FilterSweep -OnlyCases 'filtered-frozen99-1m-1'
    python bench/bevy/analyze.py "bench/results/bevy-cache-locality-recheck-$variant"
}
```

The [comparator README](../../bevy/README.md) describes its build and runner
contracts. Complete all builds/checks before measuring. To verify an existing
variant's archived evidence without rerunning its workload, use
`python bench/bevy/analyze.py bench/results/bevy-cache-locality-20261028-probes/<variant>`.
The [earlier locality probes](../bevy-locality-20261028-probes/README.md) preserve
the separate experiments before persistent cached selection.
