# Direct cache-only / broad-prefetch comparison

This probe compares the frozen cache-only candidate directly with its broader
prefetch variant and the unchanged Bevy ECS 0.20.0 executable. It follows the
[cache-only probe](../bevy-cache-20261028-probe/README.md). Windows 11 / Ryzen 9
7900X, Clang 23.1.2 and Rust 1.95.0 / LLVM 22.1.2 retain optimized O2 baseline
x86-64 flags without fast math/FMA. Game sources, component fields, starting
values and 60 Hz arithmetic are unchanged.

The `Before` label `66b8ddb8f+cache-only` identifies an uncommitted candidate,
not a Git commit. metadata.json records its frozen directory as
`build/bevy-cache-only-20261028`, including source and generated-file hashes.
Its wide executable SHA-256 is
`0446136ad2ab014af456a2a962484d71fd54bbcd72449de55608a99bb7dbcba7`,
the same executable measured as current in the cache-only probe. The broader
prefetch executable is
`7fb253f225c36e9a653b4d28cd468d9c239443390ebe8cd77f2719eeb0c1ff7a`;
Bevy remains
`d83769333a56edceb3aed09ab38878774f8491e9c8b0ba3444db3bcfbeaed081`.

## Result and interpretation

Three alternating adjacent Before/Tide/Bevy triples per case preserve 27
successful processes, each with 200 warmup and 400 measured ticks. All final
logical digests agree across implementations, rounds and the two 99%-frozen
thread budgets. This is 10,800 measured ECS ticks, 5,400 ECS warmup ticks and
7,200 additional measured handwritten-C reference ticks. The reference runs
after Tide within each Tide process and provides context; it is not a separately
alternating competitor.

Milliseconds per tick, median of three process medians. Change is the median
paired percentage change in time, rather than the ratio of the displayed medians:

| Frozen | Execution budget | Cache-only Tide | Broad-prefetch Tide | Bevy | Paired Tide time change |
| --- | ---: | ---: | ---: | ---: | ---: |
| 95% | 1 | 1.19890 | 1.25320 | 0.23045 | +5.46% |
| 99% | 1 | 0.12095 | 0.10990 | 0.04750 | -8.02% |
| 99% | 4 | 0.04880 | 0.04545 | 0.03300 | -7.43% |

Broad-prefetch process medians span 1.16550-1.34340, 0.10850-0.11125 and
0.04480-0.04660 ms. Cache-only ranges are 1.11665-1.27385,
0.11690-0.12300 and 0.04870-0.04910 ms. The 95%-frozen result is retained as
adverse despite overlapping ranges. Bevy remains faster in all three cases.
Full ranges, individual medians and paired ratios are in [summary.md](summary.md)
and [summary.json](summary.json); raw output and launch identities remain in
raw/ and records.jsonl.

The complete batch runs 23:00:18-23:01:09 UTC on October 28. The shared mutex,
explicit 15-second quiet guard and 250 ms inventory polling record no quiet
reset or competing work during the 27 processes. Ordinary desktop activity,
CPU placement, clocks and temperature remain uncontrolled. No attempts in this
directory are discarded, and no pooled-tick or confidence-interval estimate is
inferred.

The broader hint path applies to eligible cached queries below one passing row
per sixteen, with 32-128 bytes of accessed component lanes. It warms the first
four selected rows and hints four selected rows ahead, with bounded index reads.
Hints are omitted on WASM. This retains the cache's payload and validity rules,
row/math/deferred ordering, component layout and runtime API. It does not add a
new cache or alter logical match state.

The adverse 95%-frozen result motivated restricting hints to below one passing
row per sixty-four. That later candidate is recorded separately in the
[guarded probe](../bevy-cache-prefetch-20261028-guarded/README.md) and
[final comparison](../bevy-cache-20261028-final/README.md). Neither replaces this
direct comparison's results.

These are warmed scheduled ECS CPU ticks with stable membership. Setup, final
digest, per-tick world hashing, retained snapshots, rollback, networking,
rendering and application frame loops are excluded. The data establishes no
cold-construction, changing-membership, snapshot/reload performance or whole-engine
FPS result. [Shared methodology](../../bevy/README.md) defines thread budgets and
measurement limits; the cache-only probe retains the separate correctness and
raw-header compatibility checks.

## Preserved source and reproduction

diagnostics/ preserves the exact generated [wide.c](diagnostics/wide.c),
[wide.h](diagnostics/wide.h) and [wide_pack.c](diagnostics/wide_pack.c). Their
SHA-256 values match metadata.json:

- wide.c: `7acc2b737580229f266664a2c8b7003b65361484525e5bf2f5f28917738db44d`
- wide.h: `8e0dfd873ba70ae52b9d721445263af07bed722660611fc89c962c1d5aa0f11a`
- wide_pack.c: `5f2827e7dd68970a7270ddcd1a876c30a18255f0c017938bb18cb50ed16d5661`

The broad compiler source hashes were recorded, but its full compiler source
snapshot and an exact compiler patch are not supplied here. The cache-only
probe supplies its baseline compiler patch/include; adding the bounded hint
generation above can recreate the behavior. The saved generated files provide
the exact measured game source without requiring that reconstruction.

To replay this workload, compile those three saved files in a private build
directory with `bench/versus/wide/main.c` and `bench/versus/wide/wide.c`, using
the saved header directory first on the include path. Use the same checkout's
release harness/runtime libraries and recorded release options:

```text
-D_CRT_SECURE_NO_WARNINGS -O2 -DNDEBUG -std=c17 -D_DLL -D_MT
-Xclang --dependent-lib=msvcrt -g -Xclang -gcodeview
-Wall -Wextra -Wpedantic -Werror -fno-fast-math -ffp-contract=off
-ffunction-sections -fdata-sections
```

Include engine/include, platform/include and bench/versus. Link the usual
`versus_wide` objects/libraries (`tide.lib`, `tide_font_bold.lib`,
`tide_versus.lib`, `tide_platform.lib` and the Windows libraries from the release
target), with `-fuse-ld=lld-link -Xlinker /subsystem:console`. Keep these generated
files separate from normal tidec output so regeneration cannot replace them.
Use a separately configured release build directory at `build/bevy-prefetch-replay` so the
runner can record its CMakeCache.txt and compile_commands.json. Preserve the
saved generated .c/.h files under its `bench/versus/tide/versus_wide` directory
and put the rebuilt wide executable beside copies of the other two frozen
executables under its `bin` directory, then run the subset:

```powershell
$env:TIDE_HIDDEN = '1'
& ./bench/bevy/run.ps1 -OutputDir bench/results/bevy-prefetch-replay `
    -BuildDir build/bevy-prefetch-replay -Rounds 3 -FilterSweep `
    -BeforeBuildDir build/bevy-cache-only-20261028 -BeforeRevision 66b8ddb8f+cache-only `
    -OnlyCases 'filtered-frozen95-1m-1','filtered-frozen99-1m-1','filtered-frozen99-1m-4'
python bench/bevy/analyze.py bench/results/bevy-prefetch-replay
```

This replays the runtime workload with the recorded flags, not the original
compiler build. Recheck the saved evidence without running either engine:

```powershell
$env:TIDE_HIDDEN = '1'
python bench/bevy/analyze.py bench/results/bevy-cache-prefetch-20261028-paired
```
