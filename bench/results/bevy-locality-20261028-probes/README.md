# Selective-row locality probes, 2026-10-28

These seven native scratch probes test whether collecting selected row indices,
prefetching their component lanes, or requesting four-wide vectorization closes
the remaining Bevy gap at 99% frozen. They do not: the selected/prefetch/vector
prototypes take about 0.26–0.28 ms per tick, against Bevy's roughly 0.05 ms.
The rebuilt unchanged-source control is adverse and unusually variable; it is
retained with every other attempt. These results do not establish a production
prefetch speedup or a compiler-integrated optimization.

Only the private `tide_rows_Shift_5` helper in a copy of generated `wide.c` was
edited. The Tide compiler, game, header, row arithmetic, ascending selected-row
order, dispatch, pack code and runtime libraries stayed at the recorded baseline
inputs. The prototypes continue to access Tide's canonical component lanes;
they add no persistent membership or component-data cache. No change from these
probes was integrated into the compiler or runtime.

## Work and procedure

The case is `filtered-frozen99-1m-1`: one million twelve-float components,
`--tagged 1 --frozen 99`, one execution thread, no retained world. The deterministic
seed actually selects 10,089 moving and 989,911 frozen rows. Every process uses
200 warmup ticks and 400 measured ticks. The first warmup tick only spawns.
Timing covers scheduled ECS ticks, including their normal bookkeeping, and
excludes initialization, final digest, retained snapshots, per-tick world
hashing, rollback, networking and rendering. Bevy retains normal change
detection and its ordinary sparse-tag-filtered query.

Each variant has three adjacent process triples: Before/prototype/Bevy,
Bevy/prototype/Before, then Before/prototype/Bevy. Thus each variant has nine
successful processes and all seven have 63. The unchanged handwritten C way
is also timed inside each Before and prototype process; it is contextual,
not a separately launched competitor. All 63 processes exit zero, every final
digest is `0xa683eb7fe00d6622`, and all seven summaries validate.

Before is the frozen native build of
`66b8ddb8fb49e0b25a861909945891b8b9326f65` (`66b8ddb8f`). Its wide executable
SHA-256 is `b7d7792afda75809d7e667b98c1ee37ddc4df8ae91a0156d39e0dba94a08816f`.
The Bevy ECS 0.20.0 executable is
`d83769333a56edceb3aed09ab38878774f8491e9c8b0ba3444db3bcfbeaed081`.
Prototype source and executable identities are in
[the build manifest](diagnostics/manifest.json) and each variant's metadata.
The manifest's `execution: "Compiled only; no executable runs or timings"`
describes the build step; the separate result records document the later runs.

The host reports Windows NT 10.0.26200.0, Ryzen 9 7900X, 12 physical/24 logical CPUs.
Clang 23.1.2 compiles strict C17 at O2 with `-fno-fast-math -ffp-contract=off`,
the usual warnings as errors, and the same headers/math and frozen runtime
libraries as the baseline inputs. Bevy uses Rust 1.95.0/LLVM 22.1.2, Cargo O2,
no LTO, and `-C target-cpu=x86-64 -C target-feature=-fma`. Compiler versions
differ. Affinity, clocks, power and thermal settings are uncontrolled.

The shared `Local\TideClaudeContinuationBenchmark` gate, explicit 15-second
continuous quiet guard and 250 ms known-work polling are the normal comparator
runner's. Every metadata file records no quiet resets, and all process records
have no detected contention or contention events. Polling cannot rule out
ordinary desktop activity or interference between polls. The seven batches
were sequential, from 22:19:56 to 22:23:10 UTC; they are not simultaneous or
randomized comparisons among the seven prototypes.

## Results

Each cell is the median of three process tick medians, with their minimum and
maximum in brackets, in milliseconds. The last column is the median of the
three paired percentage changes against that variant's adjacent Before
process; negative means the prototype took less time. Ticks are not pooled.

| Probe | Before ms [range] | Prototype ms [range] | Bevy ms [range] | Paired change |
|---|---:|---:|---:|---:|
| [mask16](mask16/summary.json), unchanged-source rebuild | 0.42180 [0.39980–0.49815] | 0.51700 [0.31145–0.54910] | 0.04955 [0.04830–0.05350] | +10.23% |
| [selected](selected/summary.json), index list | 0.30075 [0.28315–0.30765] | 0.27710 [0.25490–0.29545] | 0.04865 [0.04810–0.04930] | −3.97% |
| [prefetch4](prefetch4/summary.json) | 0.31290 [0.28130–0.44045] | 0.26140 [0.25375–0.27600] | 0.04900 [0.04850–0.05220] | −18.90% |
| [prefetch8](prefetch8/summary.json) | 0.28855 [0.27720–0.29690] | 0.26130 [0.25775–0.27005] | 0.05070 [0.04950–0.05080] | −9.44% |
| [prefetch16](prefetch16/summary.json) | 0.30020 [0.29910–0.30140] | 0.26965 [0.26515–0.27240] | 0.04980 [0.04940–0.05000] | −10.53% |
| [prefetch32](prefetch32/summary.json) | 0.28365 [0.27130–0.28905] | 0.26425 [0.25965–0.27360] | 0.04755 [0.04745–0.04760] | −5.35% |
| [vector4](vector4/summary.json) | 0.29740 [0.28860–0.30190] | 0.26220 [0.24550–0.27325] | 0.05005 [0.04920–0.05105] | −13.15% |

`mask16` uses exactly the baseline generated source, rebuilt and linked by the
scratch recipe. Its source hash is
`8ff6f84e26e6afcc54e12ca2f47cbcc7d537eb81b45cb2047eb2de8ce5eb2acb`;
its [empty patch](diagnostics/mask16.patch) is intentional. Its 0.517 ms median
and 0.31145–0.54910 ms range must not be discarded to make the other probes
look better. The identical Before executable also varies between batches.
Recompilation/link placement and uncontrolled machine state are not isolated
here. These probes therefore do not prove that prefetching alone caused the
reported paired differences; every prefetch variant also uses the index-list
transformation, and the variants were not paired directly with each other.

All four prefetch distances and the vector request leave approximately a
fivefold Bevy gap. The records support continuing investigation of selection
metadata and component locality, not claiming that a prefetch-only shipping
change solves selective traversal. This is a single native workload and CPU;
no web, retained-world, dynamic-tag, other density or whole-game performance
conclusion follows from these scratch measurements.

## Exact transformations and evidence

[selected.patch](diagnostics/selected.patch) first scans the current range with
the existing sixteen-byte SIMD presence masks, appending matching indices to a
128-element C-stack array. It then performs the original gather/update/scatter
body in ascending index order. This rare-match helper is called only when
matches are less than `ceil(range / 16)` and a range has at most 2,048 rows;
therefore at most 127 indices occupy the 128 slots. This bound is specific to
the preserved dispatcher, not a general-purpose selected-row buffer contract.

[prefetch4.patch](diagnostics/prefetch4.patch),
[prefetch8.patch](diagnostics/prefetch8.patch),
[prefetch16.patch](diagnostics/prefetch16.patch) and
[prefetch32.patch](diagnostics/prefetch32.patch) add lookahead by that many
**selected indices**, within the same range. Each valid lookahead issues twelve
`__builtin_prefetch` calls, one per four-byte lane address: write intent for the
first six mutable lanes, read intent for the other six, locality 3. The bounds
guard skips requests beyond the selected list; larger distances often have no
eligible lookahead in a chunk at this density.

[vector4.patch](diagnostics/vector4.patch) adds
`#pragma clang loop vectorize(assume_safety) vectorize_width(4) interleave_count(1)`
to the selected-index update loop. The unique ascending indices and unchanged
numeric row body are the premise of this scratch request. It changes neither
floating-point contraction flags nor arithmetic expressions. A vectorization
request is not itself a proof of the resulting instruction mix; the saved
`diagnostics/<variant>.asm` disassemblies preserve what was compiled.

Every variant directory contains `records.jsonl`, `metadata.json`, raw stdout
and stderr, `summary.json` and `summary.md`. Records preserve launch order,
arguments, UTC process times, exit status, stdout/executable hashes, contention
observations, all timing summaries and final digests. Metadata preserves source,
configuration and binary hashes and the exact baseline path. No unsuccessful,
noisy or adverse result was removed. Executables and PDBs are not bundled.

## Reproduction

The original checkout root was
`C:\Users\blenm\.t3\worktrees\PurrEngine\t3-2f0c48c7`.
The frozen build was `build/bevy-before-66b8ddb8f`; scratch sources and binaries
were under `build/bevy-locality-probe/<variant>`. The manifest records the
original absolute paths, input SHA-256s and all seventeen compile/link commands
as executable plus argument arrays. Those commands and hashes, rather than
the prototype's ordinary Git revision label, identify the measured manual edit.

The archive includes the exact starting generated files:
[baseline-wide.c](diagnostics/baseline-wide.c),
[baseline-wide.h](diagnostics/baseline-wide.h) and
[baseline-wide_pack.c](diagnostics/baseline-wide_pack.c).
Apply each variant's patch independently to a copy of `baseline-wide.c`
renamed `wide.c`; all patches are relative to that baseline, not incremental
patches on `selected`. Compare the resulting file's SHA-256 with
`manifest.json`'s `variants[].source_sha256` before compiling. Header identity
and the simulation ID remain those recorded by the manifest.

[build-probes.ps1](diagnostics/build-probes.ps1) preserves the exact generator,
compiler and linker recipe. It assumes its **original location**:
`build/bevy-locality-probe/build-probes.ps1`, two directories below the checkout
root. Running the archived copy directly from `diagnostics/` resolves the wrong
root. To recreate the original recipe, prepare the recorded revision's native
release artifacts in `build/bevy-before-66b8ddb8f`, including the engine,
platform and versus libraries and the three versus executables. Also provide
the hash-matching `build/release/tide_font_bold.lib` input. Use the recorded
revision's engine/platform headers and benchmark main/model sources; a later
tree with different inputs is a new experiment. The saved generated files can
restore the frozen build's `bench/versus/tide/versus_wide/` inputs exactly.

From the checkout root, after preparing those prerequisites:

```powershell
$env:TIDE_HIDDEN = '1'
$evidence = 'bench/results/bevy-locality-20261028-probes'
$scratch = 'build/bevy-locality-probe'
New-Item -ItemType Directory -Force -Path $scratch | Out-Null
Copy-Item -LiteralPath "$evidence/diagnostics/build-probes.ps1" -Destination "$scratch/build-probes.ps1"
& "$scratch/build-probes.ps1"
```

The recipe emits each `<variant>/wide.c`, object and executable plus its own
manifest, compiling the common main/model/pack inputs once and linking every
prototype against the same frozen runtime libraries. Compare input, source and
build settings with the archived manifest; changing root paths or toolchain
can change executable bytes even with identical source. Preserve fresh results
separately. The comparator runner expects all three versus executables in each
build directory even when only the wide case is selected:

```powershell
$env:TIDE_HIDDEN = '1'
$variants = @('mask16','selected','prefetch4','prefetch8','prefetch16','prefetch32','vector4')
foreach ($variant in $variants) {
    $variantBuild = "build/bevy-locality-probe/$variant"
    New-Item -ItemType Directory -Force -Path "$variantBuild/bin" | Out-Null
    Copy-Item -LiteralPath "$variantBuild/versus_wide.exe" -Destination "$variantBuild/bin/versus_wide.exe"
    foreach ($other in @('particles','churn')) {
        Copy-Item -LiteralPath "build/bevy-before-66b8ddb8f/bin/versus_$other.exe" -Destination "$variantBuild/bin/versus_$other.exe"
    }
    New-Item -ItemType Directory -Force -Path "$variantBuild/bench/versus/tide" | Out-Null
    foreach ($workload in @('particles','wide','churn')) {
        Copy-Item -LiteralPath "build/bevy-before-66b8ddb8f/bench/versus/tide/versus_$workload" `
            -Destination "$variantBuild/bench/versus/tide" -Recurse
    }
    Copy-Item -LiteralPath "$variantBuild/wide.c" -Destination "$variantBuild/bench/versus/tide/versus_wide/wide.c" -Force
    Copy-Item -LiteralPath 'build/bevy-before-66b8ddb8f/CMakeCache.txt','build/bevy-before-66b8ddb8f/compile_commands.json' -Destination $variantBuild
    & ./bench/bevy/run.ps1 -OutputDir "bench/results/bevy-locality-recheck-$variant" `
        -BuildDir $variantBuild -BeforeBuildDir build/bevy-before-66b8ddb8f `
        -BeforeRevision 66b8ddb8f -Rounds 3 -Warmup 200 -Ticks 400 `
        -QuietSeconds 15 -FilterSweep -OnlyCases 'filtered-frozen99-1m-1'
    python bench/bevy/analyze.py "bench/results/bevy-locality-recheck-$variant"
}
```

Build the pinned Bevy comparator as described in
[its README](../../bevy/README.md), and finish all builds/checks before running
timings. The archived results themselves can be reanalyzed with
`python bench/bevy/analyze.py bench/results/bevy-locality-20261028-probes/<variant>`.
Reanalysis verifies evidence; it does not rerun a benchmark.
