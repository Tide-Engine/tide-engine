# Packed tables with Tide's world lifecycle

This is an **isolated integration probe, not a shipping compiler/runtime
optimization**. The owner approved the row-mapping/partition boundary in
`notes/packed-storage-integration.md`. This step implements that boundary in
an overlay of Tide's actual table runtime and exercises the unchanged generated
wide game, scheduler, page allocator, entity metadata, snapshots, hashes,
raw saves and deltas. Automatic compiler eligibility, order-sensitive traversal,
visibility, attached fields, C rows and hot reload are still outstanding.

The result supports a restricted large/sparse path, **not unconditional packing**.
It is deliberately not installed in `engine/` or enabled by `tidec`: the current
generated column loops do not preserve logical order after a partition for
arbitrary systems. The numeric wide workload has no cross-row effects, so its
existing filtered/dense loop variants are safe for this experiment.

## Storage and compatibility

- A table has optional logical-to-physical and inverse permutations, stored as
  two ordinary 2048-row COW columns. Entity locations remain logical. Get, cell,
  gather, scatter, add, remove and table moves preserve that coordinate space.
  Column and lane APIs deliberately remain physical.
- Partition swaps all columns and updates both permutations serially. It scans
  membership each call; incremental O(1) membership maintenance is not built.
- Copy/free retain/release map pages. Removing a logical row preserves the old
  logical last-row replacement and entity location update, while compacting the
  physical tail separately. Growth appends a new logical/physical pair.
- Hashing and all table serialization stay in logical order. A private logical
  page cache is refreshed from changed physical pages. Exact retained page
  identities, map identities and counts establish validity, not a probabilistic
  fingerprint. Holding source pages forces later writes to detach. Four-byte
  lane updates avoid a general memcpy/memcmp call per value.
- The cache is derived, excluded from copy and serialization, and freed with
  its table. Restore starts in identity layout. The existing generated raw
  header clearing clears the new table pointers too. Logical world hashes and
  delta values agree; the experimental raw header is larger than shipping
  (two additional pointers per table). Raw-byte equality below is between the
  two layouts **within this experimental ABI**, not against shipping raw saves.
- At 1M Crates the logical mirror adds about 66 MB of row payload and the two
  maps about 8 MB, before metadata, page rounding and retained old pages. These
  are layout-derived sizes, not measured process working sets.

The overlay links the ordinary engine archive with its `table.c` replaced.
Only this workload's reachable runtime paths are exercised. In particular the
archive's old-layout C-row/attached-state code is not used. This is not evidence
that a mixed-ABI general game works. A production build must rebuild everything.

## Measured results

Windows 11, Ryzen 9 7900X (12 cores/24 logical CPUs), Clang 23.1.2, O2,
strict C17, no fast math/FMA contraction. One execution thread, same binary
and arithmetic on both sides. Identity is the **experimental runtime without
partitioning**, not a freshly measured shipping Tide binary. No Bevy run or
new Bevy ratio is claimed.

Each cell is the median of three process medians, in milliseconds. The final
column is the median of the three adjacent paired percentage changes; it need
not equal the percentage obtained from the displayed two medians.

| Rows / frozen | Timed scope | Identity | Packed | Paired change |
|---|---|---:|---:|---:|
| 1M / 50% | tick + hash | 7.28975 | 14.81905 | +104.25% |
| 1M / 50% | snapshot + tick + hash | 11.84230 | 14.17630 | +21.48% |
| 1M / 95% | tick + hash | 6.42200 | 5.28395 | -9.87% |
| 1M / 95% | snapshot + tick + hash | 8.85360 | 5.65210 | -33.30% |
| 1M / 99% | tick + hash | 4.58325 | 3.32740 | -31.64% |
| 1M / 99% | snapshot + tick + hash | 8.53765 | 3.03800 | -65.24% |
| 4,099 / 95% | tick + hash | 0.00360 | 0.02560 | +602.78% |
| 4,099 / 95% | snapshot + tick + hash | 0.00960 | 0.03055 | +218.23% |

The 95%-frozen tick-only diagnostic is 1.90890 versus 0.03520 ms. Its large
gain does **not** describe a complete simulation tick including mandatory
hashing. At 0% and 100% frozen partition creates no mapping, so those controls
measure identical paths plus process noise. The 17-row cases approach timer
resolution. All 28 scope/size/density groups, including adverse cases, are in
`summary.json`; every sample and process identity is in `measurements/`.

Three rounds alternate identity/packed, packed/identity, identity/packed.
Every process has 100 warmup and 200 measured ticks, with snapshots also kept
during warmup where requested: 168 successful processes, 33,600 measured and
16,800 warmup ticks. Initial seeding, partition, initial cache construction,
final digest, serialization, networking, rendering and membership changes are
outside timing. Hash mode calls `tide_world_hash` **every timed tick**. Snapshot
mode retains the previous world before each tick.

The shared `Local\TideClaudeContinuationBenchmark` mutex, 15-second initial
quiet guard and 250 ms polling found no competing native builds/known benchmark
processes. This runner does not inspect Node command lines, CPU placement,
clocks, temperature or unrelated desktop activity. Native process medians vary
substantially; these are local results, not a hardware-independent promise.
Early smoke timings were exploratory and not retained as measurement evidence;
one overlapped the web build. They are excluded from all tables here.

## Correctness

- Native and WASI each pass **44,728,800 exact column comparisons**, over eight
  sizes (0, 1, 3, 17, 2047, 2048, 2049, 4099), five densities and repeated ticks.
  Checks cover inverse maps, alternating partitions, changed presence, logical
  entity order/locations, COW snapshots, growth, logical tail removal and moves
  between differently arranged tables. Raw saves, restore, logical hashes and
  deltas against identity and packed bases agree. Both platforms finish with
  aggregate digest `4340558840213487123`.
- A separate native four-thread provider runs 1,000,003 entities for twelve
  ticks with retained snapshots and changing membership. **192,000,576 exact
  column comparisons**, 36 actual helper entries, serial/parallel equality;
  digest `5837159062605574566`. An initial 100,003-row attempt stayed below
  scheduler admission and failed the explicit worker-entry assertion; it was
  increased, not counted as concurrent coverage.
- The existing sixteen table/queue tests pass against the overlay on native
  and WASI. Every timed identity/packed pair has matching per-tick hash XOR
  and final logical hash. `analyze.py` checks complete cases, launch order,
  options, all 200 samples, recorded binary identity and digest equality.

Not validated: general compiler transformations, order-sensitive systems,
event/query/extern traversal after packing, visibility, attached fields, actual
hot reload, long rollback rings, changing-membership performance, true WASI
worker concurrency, browser/GPU, other OS/mobile or sanitizers. No production
full suite or manual CI run was dispatched. Shipping hash pins are unchanged.

## Reproduce

Use the recorded source revision from `manifest.json` and build the ordinary
native/web engine archives in this checkout with `TIDE_PLATFORM=OFF` and empty
`TIDE_EXTERNAL_DIRS`. Copy `diagnostics/` into a fresh `build/packed-integration/`
directory; the scripts resolve the repository from that location.

```powershell
$env:TIDE_HIDDEN = '1'
cmake --preset release -DTIDE_PLATFORM=OFF -DTIDE_EXTERNAL_DIRS=
cmake --build --preset release --target tide tidec
cmake --preset web-release -DTIDE_PLATFORM=OFF -DTIDE_EXTERNAL_DIRS=
cmake --build --preset web-release --target tide
python build/packed-integration/prepare.py
python build/packed-integration/cache.py
python build/packed-integration/build.py
build/packed-integration/check.exe
node --no-warnings cmake/run_wasi.mjs build/packed-integration/check.wasm
python build/packed-integration/extra.py
build/packed-integration/threaded.exe
build/packed-integration/table-tests.exe
node --no-warnings cmake/run_wasi.mjs build/packed-integration/table-tests.wasm
& build/packed-integration/run.ps1
python build/packed-integration/analyze.py
```

The build scripts use the recorded Windows LLVM installation path. Change it
explicitly for another installation and record the new tool identity. The runner
refuses an existing measurements directory. `prepare.py` reconstructs the first
straightforward canonical-copy backend; `cache.py` adds the measured incremental
logical cache. Keep both steps. `commands.json`, `extra-commands.json` and
`manifest.json` preserve source/library/binary identities; binaries are omitted.

The production follow-through remains the approved integration checklist. The
measurement gate rules out blanket enablement: eligibility must prove that
physical traversal has no observable ordering effects, and layout selection
must avoid smaller and mixed-density cases. This commit preserves the probe and
its evidence; it does not claim that full integration is finished.
