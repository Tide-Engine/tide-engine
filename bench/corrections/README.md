# Transform correction smoothing cost

This CPU-only experiment measures the existing `[Position]` / `[Rotation]`
implementation before deciding whether to give built-in `Transform` those
defaults. It changes no compiler/runtime behavior or language contract.

Four separate generated games read the same position, quaternion and scale
fields in one minimal per-entity view:

- `transform`: the actual built-in `Transform`, currently untagged.
- `plain`: an ordinary `Pose` component with precisely Transform's field types,
  order, offsets, 40-byte size and defaults, without tags.
- `position`: that same component with `[Position]` on `localPosition`.
- `both`: also `[Rotation]` on `localRotation`.

Compare the three `Pose` variants to isolate annotation cost. The built-in
control checks the representativeness of the proxy's untagged view. This is
not a patched built-in Transform or a hierarchy-performance/continuity test.
In particular, adding `[Position]` to the built-in must retain its existing
special handling for shard placement; this experiment makes no such change.

Each game populates 100, 1,000 and 10,000 entities. Both current and previous
snapshots are present, and views interpolate at alpha 0.5. Ordinary movement is
0.125 units between endpoints. Changed entities differ between timelines by
(-0.5, +0.25, 0) position and a normalized nonidentity quaternion rotation.
Changed entities are evenly spaced through the live handles, including the
highest one, so sparse errors exercise a full-span correction index.
Random distributions and multiple archetypes/views are not covered.

Scenarios:

- `steady`: no correction callback and no active error.
- `unchanged`: callback every frame, identical presentation endpoints.
- `sparse`: callback every frame; 1% of entities change position and rotation.
- `dense`: callback every frame; every entity changes both.

The last two alternate correction direction, preserving bounded, nonzero errors
while exercising accumulation into existing rows. They represent continuously
renewed errors, not one isolated correction decaying to zero. Callbacks every
frame are deliberately demanding; a game's actual correction rate may be much
lower. Untagged games have no callback, matching their generated host contract.

`cold` times the first callback into an empty correction table (31 resets,
reset/free outside timing). The process allocator/page pools can already be
warm: it is not process-cold memory. The steady scenario's cold value is only
the harness/timer floor. Then 64 warmup frames precede 31 samples, each
averaging 32 consecutive frames. Each sample records correction callback,
`tide_local_frame_time` (including error decay), minimal generated view, and
the actual combined interval. Timers and tiny loop overhead are retained,
never subtracted. The p95 is over these 31 batch averages, not individual
frame latency. Summaries take medians of process medians; frames/processes
are not pooled. Processes with detected contention are retained and excluded
from aggregates. Paired comparisons require both variants' same-numbered round
to be clean and report the number/range of surviving pairs. Small absolute
differences require caution.

The view consumes all ten floats into double-precision local accumulators
(avoiding long float checksum sums losing precision). This includes normal
ECS lookup, previous-state interpolation, correction lookup/application, and
view dispatch. It excludes draw-list recording/GPU work, hierarchy composition,
networking, rollback/replay, simulation ticks, population/spawn costs and
packing. One frame is one view per entity; more consumers can cost more.
WASI under Node uses production web-generated C/SIMD, but measures no browser,
hardware GPU or phone. No shipping FPS or full-game frame budget is inferred.

Memory rows report the correction table's requested allocated bytes: index
arrays, table page-pointer capacity, live page payloads and page headers.
They exclude allocator overhead/pool retention, the fixed local struct, and
birth-identity metadata in the world. An active row currently keeps the whole
40-byte Pose error, snaps/valid/padding, an entity handle and birth identity
(60 payload bytes before pages and the handle index). Position-only and both
therefore have the same row size. No rows or index allocation are created for
the fresh steady/unchanged workloads; table capacity can remain after past
errors. These offsets are machine-local and add no network payload or hashed
simulation fields. The additional population/birth-bookkeeping cost of enabling
corrections in an otherwise untagged game is not timed here.

Every workload first checks entity counts, the expected unsmoothed delta,
tagged position continuity, tagged rotation continuity, ordinary scale,
75 ms position-error half-life, exact active-row counts and unchanged world
hashes after presentation. Quick tests run all four scenarios without timings.
The evidence verifier additionally requires identical match hashes between
tag configurations and native/WASI, validates every process/row, and checks
raw-output SHA-256s. The Transform control has its own world schema/hash.

## Reproduce

Run with Node 24.22.1 or a newer 24.x patch, CMake/Ninja and clang:

```powershell
$env:TIDE_HIDDEN = '1'
cmake --preset debug
cmake --build --preset debug --target bench_corrections_transform bench_corrections_plain bench_corrections_position bench_corrections_both --parallel 8
ctest --preset debug -R '^bench_corrections_'
```

Repeat for `web-debug`, `release`, and `web-release`. Only the focused targets
and tests are needed; no production compiler/runtime changes are made.

```powershell
./bench/corrections/run.ps1 -Output ./bench/results/transform-corrections-new
python bench/corrections/summarize.py bench/results/transform-corrections-new
```

The Windows runner holds the shared `Local\TideClaudeContinuationBenchmark`
mutex, pins its own process/children to logical CPU mask 0x100, and waits 90
continuous quiet seconds (ten-minute limit). Three process rounds alternate
native/WASI and reverse variant order on even rounds. Known build/test/benchmark
processes, including repository Node runners, are polled every 250 ms; contention,
quiet resets, failures and partial output remain in the evidence directory.
Existing evidence directories are refused. A child is bounded to two minutes;
only this runner's own child is stopped. Other desktop apps and CPU clocks/power
are uncontrolled. No GPU/browser/audio device is opened.

The result metadata identifies workload/generated-source/binary hashes and
source revision. Generated sources and executables remain in the build tree;
raw JSONL, metadata and summaries are small enough to commit with the notes.
