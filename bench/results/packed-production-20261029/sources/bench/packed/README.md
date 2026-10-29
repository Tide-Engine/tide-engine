# Transient packed-row integration checks

These opt-in tools exercise the compiler/runtime's `TIDE_PACKED_TABLES` build.
They regenerate the actual docs `entities` game and the actual `versus/wide`
workload. They do not inject handwritten loops or use a runtime overlay.

Build the entire engine and compiler with the option enabled, in separate
native/web build directories, then run:

```powershell
$env:TIDE_HIDDEN = '1'
Remove-Item Env:TIDE_PACKED_MODE -ErrorAction SilentlyContinue
python bench/packed/build.py
build/packed-production/entities.exe
node --no-warnings cmake/run_wasi.mjs build/packed-production/entities.wasm
```

`build.py` defaults to `build/packed-release` and `build/packed-web-release`;
override them with `--native-build` and `--web-build`. It records compiler/link
commands and binary SHA-256s in `build/packed-production/build.json`. Both hosts
use strict O2 C17 and normal WASM SIMD; no relaxed float operations. The Windows
LLVM path is explicit and overridable. Every consumer must use the same table
ABI; an enabled host must never link an ordinary disabled engine archive.

The entities replay uses two worlds in one process, controlling the generated
diagnostic mode directly: ordinary versus forced packing. It runs 360 ticks
with real encoded Drop/Poison/Cure commands and verifies entity IDs, logical row
order, health, membership, actual destruction, hashes, raw pack equality, raw
restore and changed delta reconstruction against the other world's baseline.
It retains the previous world and compares the real views' ordered drawing
commands, text, plain shapes, vertices, indices, quads, camera/clip/turn payloads
and Pointing counts. The check requires positive packed pass and row counters;
an identity-only run cannot count as packed coverage. It records draw lists,
not rendered pixels, and does not create a window, browser or audio device.

After all builds/tests finish, measure the wide workload with:

```powershell
python bench/packed/run.py
```

Each process receives count, frozen percentage, mode, snapshot and churn flags.
Setup is untimed with packing off. The 10 warmup and 30 measured ticks include
the generated partition/update/restore path and a world hash every tick.
Snapshot mode retains the prior world inside timing; churn changes 16 tag
presence bytes each tick inside timing. Churn is storage membership stress,
not the cost of queuing/applying Tide commands. The host emits all 40 per-tick
hashes, its final hash and digest, samples and actual packing counters.

The runner takes three alternating ordinary/forced pairs over six controls:
1M/50%, 1M/95% with and without snapshots, 1M/99% with snapshots, 4,099/95% with
snapshots, and 1M/95% with snapshots plus churn. It checks complete cases, every
tick's hash agreement, final digests, option identity and actual packed work.
Results are medians of process medians and median adjacent paired percentages,
never pooled samples. Files go to a new measurements directory; existing or
failed attempts are not overwritten. JSON and stderr, process arguments/times,
binary SHA-256 and contention are retained.

On Windows the runner holds `Local\TideClaudeContinuationBenchmark`, requires
15 continuous quiet seconds, and polls known native builds/benchmarks every
250 ms. Contention stops the batch while preserving partial evidence. It does
not inspect Node commands or control other desktop work, affinity, clocks or
temperature. This comparison evaluates transient packing with full restoration
cost; it does not establish the performance of resident packed tables or a new
Bevy comparison. No timing assertions belong in correctness tests.
