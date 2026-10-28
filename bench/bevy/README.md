# Tide / Bevy ECS CPU comparison

This standalone Rust comparator ports the existing `bench/versus` particles,
wide components, sparse Frozen filtering and bullet churn to Bevy ECS 0.20.0.
It is opt-in; building Tide does not install Rust or build Bevy.

Both implementations execute the same arithmetic on the same seeded starting
values at 60 Hz, with the same deferred entity changes. The first particle/wide
tick only spawns. The measured interval is a scheduled ECS tick, including
structural changes for churn. Initialization, final digest, world hashing,
retained snapshots, rollback, networking, rendering and application frame loops
are excluded. These results do not measure whole-engine FPS or equivalent
networking features.

Tide uses the unmodified existing game sources and their handwritten C reference.
The shared versus harness adds only `--json`, which prints an additional record
after normal output. Bevy uses an ordinary single-thread schedule, normal change
detection and `Query::iter_mut().for_each`, or `par_iter_mut()` for the parallel
cases. Frozen uses SparseSet storage on both sides. Component fields match;
Tide's generated column storage and Bevy's component storage remain their own.
Bevy's contiguous-query and change-detection-bypass alternatives are not measured,
and these results do not establish its maximum possible throughput.

`--threads N` is an execution budget including the calling thread. Bevy creates
N-1 pool workers and its query scope lets the caller help; its schedule remains
serial because these workloads have one update system. Bevy falls back to serial
queries with at most one owned worker, so use 1 or at least 3. Neither engine is
required to use the entire budget. No core affinity, clock, power or thermal
settings are changed. On processors with several cache groups, the pools can
choose different placements. Churn's parallel deferred-command application order
can differ, but logical bullet values and counts must match; this is no Bevy
cross-platform determinism guarantee.

## Reproduce on Windows

Use Rust 1.95.0 or newer compatible with the lockfile, Clang and Tide's normal
native/web prerequisites. The recorded run uses exact Rust 1.95.0.

```powershell
$env:TIDE_HIDDEN = '1'
cmake --preset release
cmake --build --preset release --target versus_particles versus_wide versus_churn
cmake --preset web-release
cmake --build --preset web-release --target versus_particles versus_wide versus_churn
rustup toolchain install 1.95.0 --profile minimal --no-self-update
$env:RUSTFLAGS = '-C target-cpu=x86-64 -C target-feature=-fma'
$env:TIDE_BENCH_RUSTC_VERSION = (rustc +1.95.0 --version)
cargo +1.95.0 build --locked --release --manifest-path bench/bevy/Cargo.toml --target-dir build/bevy
ctest --preset release -R '^versus_(particles|wide|churn)(_kept)?$' --output-on-failure
ctest --preset web-release -R '^versus_(particles|wide|churn)(_kept)?$' --output-on-failure
python bench/bevy/validate.py --output build/bevy-validation.json
& ./bench/bevy/run.ps1 -OutputDir bench/results/bevy-new
python bench/bevy/analyze.py bench/results/bevy-new
```

The Cargo release profile pins O2, no LTO and 16 codegen units. Rust uses ordinary
strict `f32` expressions with no fused arithmetic; Tide keeps its normal O2,
no-fast-math/no-contraction flags. Both target baseline x86-64, while their
compiler/LLVM versions differ. Dependency checksums are pinned by Cargo.lock.

The correctness script checks 66 Bevy cases against native Tide/C and WASI
Tide/C: 1/17/10,000 particles; empty/17 wide components; 0/50/100% frozen;
1/31/100 bullets spawned per tick; early and steady-state ticks; serial and
four-thread Bevy. Its collected timings are discarded. The timing runner also
requires matching digests in every process pair at its larger actual workloads.

The runner holds `Local\TideClaudeContinuationBenchmark`, waits 15 continuous
seconds without known build/test/benchmark work, checks before each process and
polls every 250 ms while it runs. If competing work appears before a process,
the quiet guard restarts. During a process it marks and retains the attempt,
then stops; analysis rejects incomplete/contended batches. This explicit short
quiet guard differs from older 90-second runners. Inventory polling itself has
overhead, short processes can finish between polls, and ordinary desktop
activity is not controlled. The shared mutex coordinates cooperating runners.

Five process rounds alternate Tide/Bevy then Bevy/Tide. Each uses 200 warmup and
400 measured ticks; this reaches steady-state bullet population before churn
timing. Cases are 10k/100k/1M particles, 1M twelve-float components, 1M components
with about half frozen, and 2,000 bullets spawned per tick, with one/four-thread
budgets for the larger cases. The C reference is timed after Tide within the
same process and is contextual, not a separate alternating competitor.

Existing evidence directories are refused. Raw output, command lines, UTC
launch times, contention events, CPU/toolchain identity, source/generated-C/
configuration/executable hashes and every result are retained. The analyzer
verifies raw stdout hashes, successful exits, complete alternating pairs,
matching options and invariant final digests. Reports use the median of five
process medians and show their min/max; Bevy/Tide is the median of five paired
ratios. It does not pool ticks across processes or invent confidence intervals.

## Official Bevy references

- [0.20.0 release](https://github.com/bevyengine/bevy/releases/tag/v0.20.0)
- [ECS dependency/features and minimum Rust version](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/Cargo.toml)
- [Schedule executor](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/schedule/executor/mod.rs)
- [Task-pool worker semantics](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_tasks/src/task_pool.rs)
- [Parallel query implementation](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/query/par_iter.rs)
- [Official iteration benchmark variants](https://github.com/bevyengine/bevy/blob/v0.20.0/benches/benches/bevy_ecs/iteration/mod.rs)
