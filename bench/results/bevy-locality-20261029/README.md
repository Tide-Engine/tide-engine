# Sparse wide locality experiments

No candidate is promoted. These are manually edited generated-C kernels against
`d65b66ed455446cf5cbdbaea06a1a2c56683dbcb`, not a compiler optimization.
Canonical component storage, production sources and deterministic hash pins are unchanged.

## Results

Milliseconds per scheduled ECS tick, 1M entities, one thread. Each row uses its own
three adjacent alternating Before/probe/Bevy triples, 200 warmup and 400 measured
ticks per process. Times are medians of process tick medians. Change is the median
paired time change; negative is faster. Do not compare absolute values across batches.

| Probe | Frozen | Before ms | Probe ms | Bevy ms | Paired change |
| --- | ---: | ---: | ---: | ---: | ---: |
| Unchanged relink control | 95% | 0.87265 | 0.86100 | 0.20945 | -1.46% |
| Unchanged relink control | 99% | 0.09935 | 0.09710 | 0.04785 | -2.09% |
| One axis per chunk | 95% | 0.95170 | 0.94570 | 0.20765 | -0.63% |
| One axis per chunk | 99% | 0.09670 | 0.09630 | 0.04830 | -0.41% |
| One axis per 16 selected rows | 95% | 0.88140 | 0.92950 | 0.20870 | +6.93% |
| One axis per 16 selected rows | 99% | 0.09820 | 0.09800 | 0.04830 | -0.76% |
| One axis per 64 selected rows | 95% | 0.89125 | 0.95695 | 0.21475 | +7.37% |
| One axis per 64 selected rows | 99% | 0.09750 | 0.09675 | 0.04810 | -0.77% |
| Four gathered rows, SIMD | 95% | 0.87175 | 0.91945 | 0.21085 | +5.47% |
| Four gathered rows, SIMD | 99% | 0.09840 | 0.09875 | 0.04820 | +0.36% |
| Four neighboring rows, masked SIMD | 95% | 0.88360 | 1.09720 | 0.21270 | +23.02% |
| Four neighboring rows, masked SIMD | 99% | 0.09700 | 0.13330 | 0.04800 | +37.42% |

See each batch's summary.md/summary.json for every process range and paired result.
The unchanged relink control itself moves by roughly 1-2%, so small changes of
that size do not establish an optimization. No probe closes the gap with Bevy.

## Controls for neighboring-row SIMD

| Case | Before ms | Probe ms | Bevy ms | Paired change |
| --- | ---: | ---: | ---: | ---: |
| particles-1m-1 | 0.45070 | 0.44515 | 1.14100 | -1.29% |
| wide-1m-1 | 1.18590 | 1.18840 | 2.32690 | -0.61% |
| filtered-frozen90-1m-1 | 1.32715 | 1.22090 | 0.55110 | -8.18% |
| filtered-frozen99-1m-4 | 0.04110 | 0.05040 | 0.03080 | +22.18% |

The particle executable is byte-identical on both sides. Dense wide and 90%-frozen
chunks retain the original paths; their variation is a control, not evidence of
a kernel speedup. No changed candidate is promoted, so there is no production
particle regression or new performance guarantee.

## What was tested

- Axis variants visit selected rows three times, handling x/y/z separately.
  They reduce simultaneously accessed field streams while retaining the exact
  per-field arithmetic. Only this pure numeric body permits that reordering.
- Gathered SIMD explicitly packs four selected rows into four-wide arithmetic,
  then scatters six changed fields. It retains scattered loads and stores.
- Neighboring-row SIMD visits aligned groups of four physical rows containing
  a selection. It loads contiguous floats, computes the same arithmetic, and
  selects original bits for frozen neighbors. Only complete physical groups
  inside the selected piece's min/max row span qualify; boundaries use scalar
  code. This prevents writes across neighboring piece boundaries. Additional
  frozen-row computation/stores are included in its timing.
- Both SIMD probes contain packed add/multiply/subtract instructions in the
  saved assembly. The axis loops remain scalar. No FMA, fast math, AVX tuning,
  layout change, retained value cache or altered task dispatch is introduced.

These results show that adding SIMD or changing field traversal alone does not
remove the sparse-wide gap on this machine. They do not measure an alternative
canonical AoS/AoSoA layout or prove that storage layout is the sole bottleneck.
Persistent repacking would need to account for updates, membership changes,
snapshot ownership and packing costs; none is implemented here.

## Correctness and scope

Every timed process exits successfully and matches the independent C and Bevy
logical digest. A separate host compares every byte of all twelve lane arrays
against the original helper after every step: 14 row counts (1 through 2048,
including vector/chunk tails), 32 membership patterns including empty, four
selected-index pieces, 20 ticks, and five changed kernels. There are 179,200
exact comparisons on native and another 179,200 on ordinary WASM SIMD.
Frozen neighbors, bytes outside each piece, negative values and signed zeros
are compared too. This is scoped kernel validation, not proof of a general
compiler transformation or dynamic membership/snapshot performance.

Native builds use Clang 23.1.2, O2, strict C17 warnings-as-errors and unchanged
deterministic float flags. Web validation uses O2 and ordinary -msimd128.
The unchanged Bevy 0.20.0 binary uses Rust 1.95.0, O2 and baseline x86-64.
Hardware: Windows 11, Ryzen 9 7900X. No affinity, clocks or temperatures are
controlled. Setup, final digest, per-tick hashing, retained snapshots, rollback,
networking and rendering are outside timing. This is not whole-engine FPS.

The shared benchmark mutex, 15-second quiet guard and 250 ms known-process
polling apply. The first control attempt stopped during round two when other
cmake/ninja/ctest work appeared. All nine attempted processes and contention
records remain in interrupted-control and are excluded from completed summaries.
The six completed batches contain 144 successful processes; none is pooled
with the interrupted attempt. No full suites, hardware counters, web timing,
GPU/mobile/sanitizer validation or manual CI dispatch was performed.

## Reproduction and evidence

The byte-preserved diagnostics include every generated source, helper patch,
assembly, build command/input/binary hash and the native/WASI validation host.
Binaries/libraries remain in ignored build directories. The original first-five
build manifest has inherited `bounded`/`staged` descriptions from the older
builder; those fields do not describe these kernels. Its actual command arrays,
source identities and assembly do. The separate neighboring-row manifest has
corrected scope metadata. The CMake compile database supplied to the timing
runner is context only; the manual build manifests specify actual commands.

To reproduce, restore the diagnostics scripts under build/bevy-locality-20261029,
build the baseline revision, and run build-probes.ps1 (helpers.json is supplied).
Copy each executable into that probe's bin/versus_wide.exe alongside unchanged
particles/churn executables and generated files as expected by bench/bevy/run.ps1.
Use a frozen baseline bin directory, three rounds, 200/400 ticks, -FilterSweep
and the case IDs in metadata.json; always choose new output directories.
Run validate.py for the exact native/WASI row check. Scripts use the original
checkout layout and existing Clang/WASI libraries; they are experimental records,
not installed tooling. analyze.py verifies each completed batch.

No compiler/runtime/game source changes or expected hash updates accompany this report.
