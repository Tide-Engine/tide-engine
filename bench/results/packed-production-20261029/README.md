# Transient packed storage: implemented, validated, rejected

Decision: abandon this full-copy transient implementation. It preserves tested
behavior but loses decisively once conversion, restoration and mandatory hashes
are timed. The production compiler/runtime changes were removed after testing;
`implementation.patch` preserves the complete experiment against engine
`cb533364e9d7e68cdfaae0a36063a1ea217971d9`. Shipping code and hash pins are unchanged.

This does **not** disprove resident packed storage. The earlier persistent
runtime probe had different costs and left ordinary consumers unconverted.
That larger integration remains unbuilt. With no eligible partitions in the
current prototype tests under these conservative rules, we recommend deferring
that rewrite until a real game demonstrates the relevant workload.

## What was actually integrated

This run rebuilt the actual engine, compiler, generated games and all linked
consumers under `TIDE_PACKED_TABLES=ON`; it used neither a runtime overlay nor
handwritten replacements for the generated update loops. The default OFF build
retained the original table ABI and code path.

The compiler recognized independent numeric row updates through helper chains,
excluding shared writes, queries, C calls, random draws, queued effects, methods
and other unsupported constructs. It selected one sparse presence predicate per
table. An experimental serial tick partitioned the table before an eligible
system, ran its normal generated tasks, then copied all columns into identity
order before another system, query, event, view or C consumer could see it.
Snapshots/hash/raw/delta support used the real mapped table implementation.

Controls were off, guarded (65,536 rows and at most 1/16 matching), and forced.
Those thresholds were experimental, not established performance policy. Even a
nonzero mode with no eligible table used a serial tick. Timings compare off and
forced with one thread on both sides; automatic and multithread performance are
not established. The header marked the experimental ABI. No new language syntax
or game source changes were needed.

Restoration allocates logical pages and copies every column, discarding cached
hashes even for untouched columns. This explains an inherent cost of this
implementation; partition/update/restore phases were not separately timed.

## Measurements

Windows, Ryzen 9 7900X, Clang 23.1.2, strict C17/O2, no fast math or FMA. Both
sides use the same rebuilt experimental binary. Values are milliseconds,
medians of three process medians. Every row includes update and world hashing;
snapshot/churn rows additionally include those operations inside timing.

| Rows / frozen | Additional work | Ordinary | Transient packed |
|---|---|---:|---:|
| 1M / 50% | none | 6.47710 | 183.16045 |
| 1M / 95% | none | 6.08790 | 128.29755 |
| 1M / 95% | retained snapshot | 8.79705 | 149.76765 |
| 1M / 99% | retained snapshot | 8.20130 | 143.31820 |
| 4,099 / 95% | retained snapshot | 0.01260 | 0.52285 |
| 1M / 95% | snapshot + 16 membership changes | 9.23515 | 152.49455 |

All six cases lose. Median adjacent paired ratios range from 16.51x to 40.74x;
these need not equal ratios of the displayed median columns. No new Bevy ratio
is claimed. These timings must not be pooled with the earlier resident probe.

Three alternating off/forced pairs per case, 36 successful processes, 10 warmup
and 30 measured ticks each: 360 warmup and 1,080 measured ticks. Every one of
the 40 per-process logical hashes matches its paired run, as do final hashes
and digests. All forced processes execute 40 partitions and `40 * count` rows.
Initial spawning is untimed. Membership stress toggles 16 presence bytes through
the runtime, not through Tide's deferred command queue; the separate Entities
replay tests real commands.

The runner held the shared benchmark mutex, required a 15-second quiet window
and polled known native build/benchmark processes. No contention was detected.
It does not inspect Node command lines or control desktop work, CPU affinity,
clocks or temperatures. The nominal 250 ms inventory polling can take much
longer: recorded process durations include inventory/reap delay and are not tick
timings. Only the in-process monotonic samples are summarized. The process
medians vary substantially, but every paired comparison loses decisively.

## Correctness and game coverage

- The rebuilt full runtime test executable passes on native and WASI. Focused
  table/queue tests pass 18/18 enabled and 16/16 disabled; the final added empty
  table reuse cases pass on both targets too.
- Compiler fixture and generated-code checks pass 2/2 on native and WASI. The
  fixture requires 12 actual partitions, follows helper calls, then exercises
  shared state, ordered C calls, query order, queued tag changes/events, a
  helper-hidden spawn, entity IDs, retained snapshots and identical raw saves.
- The unchanged docs Entities example passes 360 paired off/forced ticks on
  native and WASI: **348 partitions, 3,576 rows, 34 destructions**, matching
  aggregate digest `18032996935774763744`. Real Drop/Poison/Cure commands, IDs,
  membership, logical hashes, raw saves/restores, changed delta reconstruction
  and ordered interpolated view payloads agree. The first attempt correctly
  failed its positive-coverage assertion because eligibility was too restrictive;
  its logs are retained. The corrected run actually packs.
- All 49 docs examples, 11 gallery games, and Inside Out, Underfoot, Foldline,
  Gravewake, Banked and Parry build on native/web. All 66 pass bounded native
  `--frames 3` startup checks. Those short checks show no actual partitions and
  are startup coverage, not gameplay or performance evidence.
- Inside Out, Underfoot, Foldline, Gravewake, Banked and Two Forks simulation
  tests pass on both targets with forced mode requested. **All six report zero
  partitions**, so these establish fallback compatibility only. Foldline's
  build-time map/connection checks also pass. Prototype checkout: `31429e7`, no
  tracked source edits; its untracked `.claude/` was left alone.

The ordinary WASI runner supplies an empty environment, so the initial CTest
web run did not receive the force setting. The six tests were rerun with the
saved `run_wasi_force.mjs`, injecting only hidden/mode/report variables; their
explicit zero counters and successful results are retained. The paired replay
and wide harness set mode directly and do not depend on that environment.

Not claimed: browser/pixel playtests, long prototype playthroughs, Sidekick,
cross-OS/mobile/sanitizers, actual library hot reload, concurrent packed
execution, resident-layout safety or performance. Parry has no registered
simulation test. No CI workflow was manually dispatched.

## Reproduce

Use the base revision above with this report and `bench/packed/` copied in.
The archived patch passes `git apply --check` against that source. It contains
the CMake option, compiler/runtime changes, fixtures and implementation notes.

```powershell
$env:TIDE_HIDDEN = '1'
git apply bench/results/packed-production-20261029/implementation.patch
cmake --preset release -B build/packed-release -DTIDE_PACKED_TABLES=ON -DTIDE_EXTERNAL_DIRS=
cmake --preset web-release -B build/packed-web-release -DTIDE_PACKED_TABLES=ON -DTIDE_EXTERNAL_DIRS=
cmake --build build/packed-release --target tidec tide_tests tidec_e2e_packed_rows
cmake --build build/packed-web-release --target tide_tests tidec_e2e_packed_rows
ctest --test-dir build/packed-release -R "^(tide_tests|tidec_e2e_packed_rows|tidec_packed_rows_codegen)$" --output-on-failure
ctest --test-dir build/packed-web-release -R "^(tide_tests|tidec_e2e_packed_rows|tidec_packed_rows_codegen)$" --output-on-failure
python bench/packed/build.py
build/packed-production/entities.exe
node --no-warnings cmake/run_wasi.mjs build/packed-production/entities.wasm
python bench/packed/run.py
```

Unset `TIDE_PACKED_MODE` and `TIDE_PACKED_REPORT` before direct paired harness
runs; they reject overrides. Finish builds/tests before timing. The runner
refuses an existing measurement directory. For the game matrix add
`-DTIDE_DOC_EXAMPLES=ON -DTIDE_DOC_PLAYGROUND=OFF` and the prototype path to both
configurations, then build `docs_examples sample_games private_games banked`
and the six named test targets. Sources, commands, library/binary identities,
raw samples and logs are retained here; binaries are omitted. `sources/` is the
tested historical implementation, not the shipping tree after rejection.
