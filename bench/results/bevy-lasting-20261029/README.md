# `[Lasting]` Frozen against Bevy

The wide benchmark's Frozen tag, built two ways from the same `bench/versus/wide/wide.tide`:

- **Before**: today's default, `[Sparse]`: a presence byte in every crate's row.
- **Current**: `versus_wide_lasting`, the `LASTING` build: `[Lasting] component Frozen { }`
  plus a `Thaw` system that can remove it (never asked during timing), so Frozen is
  a toggleable tag kept in an archetype of its own.

Each batch uses a different Bevy executable. `sparseset/` uses the ordinary comparator
(`bench/bevy`, Frozen with SparseSet storage). `table/` uses the same source with only
the storage line removed (`table-main.rs.patch`), so Bevy also gives frozen entities
a table of their own. The `wide-1m-1` case has no tags and is a control.

1M entities; 3 alternating before/current/Bevy rounds; 200 warmup and 400 measured
ticks per process; median of process medians, in ms per tick. Change is the median
paired change of current against before. Bevy/Tide above 1 favors Tide.

| Case | Threads | Before ms | `[Lasting]` ms | Change | Bevy SparseSet ms | Bevy/Tide | Bevy Table ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| wide-1m-1 (no tag) | 1 | 1.41925 | 1.49945 | -3.81% | 2.88615 | 1.91× | 2.48475 | 1.82× |
| 50% frozen | 1 | 2.31615 | 0.45690 | -78.45% | 2.51755 | 5.17× | 1.13695 | 2.66× |
| 50% frozen | 4 | 0.79585 | 0.47510 | -39.41% | 1.36835 | 2.84× | 0.51285 | 1.13× |
| 1% frozen | 1 | 3.01680 | 1.41765 | -53.01% | 3.38195 | 2.39× | 2.49340 | 1.92× |
| 10% frozen | 1 | 3.31965 | 1.25990 | -62.05% | 2.71515 | 2.16× | 2.26030 | 2.07× |
| 90% frozen | 1 | 1.30670 | 0.06000 | -95.48% | 0.67660 | 11.28× | 0.20420 | 3.47× |
| 95% frozen | 1 | 1.03550 | 0.02970 | -97.12% | 0.21530 | 7.23× | 0.10320 | 3.47× |
| 99% frozen | 1 | 0.09730 | 0.00580 | -94.04% | 0.04810 | 8.29× | 0.02015 | 3.52× |
| 99% frozen | 4 | 0.04040 | 0.00580 | -85.64% | 0.03300 | 5.59× | 0.02190 | 3.78× |

Before/current and the SparseSet Bevy column come from `sparseset/`. The Table
column and its ratio come from `table/`. The Tide times of the table batch agree
within a few percent; see each `summary.md`. The first batch has wider ranges
(for example wide-1m-1, 1.33-1.89 ms), so its control's -3.81% is noise, not a
change: that executable runs the same rows either way.

## What this shows and doesn't

- With `[Lasting]`, `without Frozen` admits one archetype, and the loop is the dense
  unfiltered one over the active entities alone. Tide leads Bevy's sparse and table
  storage in every case here.
- Four threads at 50% frozen (0.475 ms) are no faster than one (0.457). The sparse
  default did scale there (2.32 to 0.80). The cause wasn't investigated. At 99%,
  5.8 µs is too little work to split.
- No toggle is timed: `Thaw` never runs, so the row copy of each Add or Remove,
  and archetype growth, are unmeasured. Bevy's table storage pays the same kind of
  move cost.
- Excluded, as in the earlier comparisons: setup, final digest, per-tick world
  hashing, retained snapshots, rollback, networking and rendering.

## Correctness

All 162 processes exit successfully, and every before/current/Bevy triple agrees on
the logical digest. `versus_wide_lasting --quick` checks it against the C reference.
`compiler/tests/e2e/lasting` passes on native debug and web debug (WASI).

## Setup

Windows 11 / Ryzen 9 7900X, Clang 23.1.2 O2 (release preset), Rust 1.95.0 /
Bevy 0.20.0 O2, `RUSTFLAGS=-C target-cpu=x86-64 -C target-feature=-fma`. The tree is
`e315d1aab` plus this change, uncommitted at measurement (the metadata revision is
the base commit). Before and current are both built from that tree. The only
difference in the before build is two unused Yard fields, `thaw` and `thawing`.
Frozen staging directories `build/sparse-bench` (before) and `build/lasting-bench`
(current) held the binaries and generated C, whose hashes are in each
`metadata.json`. The runner is `bench/bevy/run.ps1` with its shared mutex,
15-second quiet guard and 250 ms process polling. No contention or quiet reset
was recorded. Both batches ran 2026-10-29 12:57-13:02 UTC, `-Rounds 3 -FilterSweep`,
with `-OnlyCases` set to the nine cases above. The table batch passes
`-BevyExe build/bevy-table/release/tide-bevy-bench.exe`.
