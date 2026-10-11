# Reusing owned storage after asset writes (2026-10-11)

This follows the [asset-sharing ownership experiment](../asset-sharing-prototype/README.md).
The owner asked to investigate and improve its bulk reset-and-edit slowdown.
This remains an isolated generated-code/C-wrapper prototype. Production compiler,
runtime, installed package and user game sources are unchanged.

**Timing status: deferred by the owner.** Continuous work in other checkouts
prevented the quiet window. After one 45-minute guard timeout, the owner asked
to stop the restarted timing runner and merge the experiment with measurements
left for later. No comparison rows or speedup summary were produced. The clocks
in `wasi-checks.json` and `diagnostic.json` are correctness/instrumentation-run
output under competing work, and are not performance evidence. The allocation
counters below establish the mechanism; they do not prove the speedup or rule
out the written-then-read tradeoff.

## Candidate

The previous shared prototype drops an owned destination on asset assignment,
then allocates another owned list on the first subsequent write. Old blocks wait
for the batch flush, so a second allocation set increases the high-water mark.
Releasing old lists also causes snapshot COW, zeroing and dirty-page hashing.

The candidate reuses a destination already owned by its receiving heap:
assignment copies the asset into the existing storage with `tide_list_set`.
An empty or still-shared destination retains the asset reference, and its first
write still detaches. Ordinary owned sources follow the previous assignment path.
Only known-asset assignment adds an ownership check. There is no byte comparison,
new cache, layout change, allocator change or runtime entry point.

A separate instrumented final-round check confirms the mechanism: the naive
16 KiB reset/edit case has 192 pending blocks after assignment and 6,291,456
dirty bytes before hashing. Baseline and reuse each have zero pending blocks
and 3,145,728 dirty bytes. `diagnostic.c`/`diagnostic.json` retain that evidence;
the diagnostic timings include instrumentation/concurrent work and are not
performance comparison rows. These counters are independent of clock speed.

This policy favors fields that are repeatedly edited. A field edited once and
later repeatedly assigned assets without writing them remains owned, so it
continues copying/hashing like the original baseline instead of regaining the
read-only sharing benefit. `written-then-read` explicitly measures that tradeoff;
it is not omitted or treated as an ordinary never-written asset field.

## Equivalent work and validation

Three variants link the same checkout-built native release library:
`base` is original eager value copying, `naive` is the previous sharing/detachment
policy, and `reuse` differs only in the owned-destination asset-assignment choice.
All variants use identical source except these compile-time choices and the
private generated helper. The private game/pack input is unchanged and is not
redistributed; its hashes and source/binary/library identities are recorded.

Each container case runs 150 rounds, discarding 30 and averaging 120, with one
pre-round snapshot. Assignment, writes, pending-block flush and hash intervals
are disjoint. Their process means are summed before taking the median of three
process totals. Untimed before/after hashes assert that every retained snapshot
stays immutable. Final independent expected values and count/digest assertions
cover every one of the 64 lists, or every float in the single float list.
The packed asset remains byte-identical. These checks are trusted C benchmark
logic, not a complete general Tide storage implementation.

- `asset-assign`: assign the 16 KiB asset to 64 initially empty lists repeatedly.
- `first-write-small`, `first-write`, `first-write-large`: bulk reset all 64
  lists to a 4/16/64 KiB asset, then edit one byte in every list before one flush.
- `first-write-once`: initialize and first-edit 64 fresh lists each round;
  resetting/freeing the heaps is untimed.
- `written-then-read`: initialize owned lists and edit them once before the
  measured loop, then repeatedly assign the asset without further edits.
- `mutable-copy`: assign 64 copies of an already-owned 256-byte list.
- `mutable-edit`: update all 65,536 floats of an already-owned 256 KiB list
  through the writable chunk cache.

The 100-character manual-world grid runs 330 frames at 60 Hz, discarding 30 and
averaging 300. It retains a pre-tick snapshot, ticks, hashes, records views and
prepares palettes serially. Snapshot retention has a separate interval; untimed
hashes assert preservation of every retained world. Every character's state,
position, timer, clip index/name and every integer palette/influence texture
contribute to accumulated semantic digests. All three clips are exercised.
Raw world hashes differ across the baseline/shared representation; the two
sharing variants agree. The grid excludes GPU drawing, networking, restore/replay,
Rooms UI and startup; these CPU intervals do not measure FPS.

`measure.ps1` rotates the three process orders across rounds, takes the existing
named benchmark gate and requires 90 seconds without detected builds, tests or
benchmarks before measurement. It checks for competition during every process.
Every valid measured row is retained. Other desktop activity and clocks are
uncontrolled. Web/WASI runs provide correctness checks, not performance evidence.
The first guard attempt timed out after 45 minutes of competing work, before
running any measured case. Only the timing runner was restarted, with unchanged
binaries and the same quiet-period requirement; there were no excluded timing rows.
`analyze.mjs --checks-only` verifies the 27 existing WASI results, applicable
owned/shared layouts, exact grid semantic/palette outputs, the three native
diagnostic results against their WASI values, and exported artifact SHA-256
hashes. Every run asserts independent expected values and snapshot preservation.
Full native/WASI coverage comparison and performance analysis await the native
timing batch. No full production suites, browser/GPU, other platforms,
sanitizers or CI were rerun for this standalone experiment.

## Remaining implementation scope

The storage reuse experiment addresses the identified allocation/reset cost.
Current physical pack handles still lack the stable identity/lifetime behavior
needed for a production sharing implementation. Reload, portable reference
packing, general nested containers and all mutation paths remain outside this
experiment. Its small C wrapper is not a replacement for those mechanisms.
The reuse choice is an internal optimization proposal that preserves the
benchmarked values; language semantics are unchanged. No full engine feature
or universal performance guarantee is claimed.

## Reproduction

With the previous experiment's identical generated base/shared sources and pack
available, and the same native release/web-debug libraries built in this checkout:

```powershell
$env:TIDE_HIDDEN = '1'
& ./bench/results/asset-owned-reuse/build.ps1
& ./bench/results/asset-owned-reuse/check-web.ps1
# Deferred until a quiet window is available:
& ./bench/results/asset-owned-reuse/measure.ps1
# Separate instrumented mechanism check, outside timed comparison runs:
& ./bench/results/asset-owned-reuse/diagnostic.ps1
```

Only checkout-built finite programs are run, hidden and silent. Copy `rows.json`
and `wasi-checks.json` from `build/asset-reuse-prototype` here, generate the artifact
identities/exports, then run `build/node/node.exe bench/results/asset-owned-reuse/analyze.mjs`.
For the evidence currently present, run
`build/node/node.exe bench/results/asset-owned-reuse/analyze.mjs --checks-only`.
Without that flag, the analyzer requires all 81 deferred native timing rows and
writes the comparison summary; do not treat correctness-run clocks as substitutes.

Native flags are strict C17 `-O2 -fno-fast-math -ffp-contract=off` with the dynamic
CRT; standalone container compilation treats all warnings as errors. WASI uses
Clang's threads target, pinned wasi-sdk 24 sysroot/builtins and Node 24.16, with
`-O2 -pthread -msimd128` and deterministic float flags. Its linked engine library
is web debug. Sources are normalized to LF on export; the manifest separately
records raw measured inputs and exported text hashes.
