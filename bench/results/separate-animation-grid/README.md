# Owned separate-animation grid (2026-10-11)

This is a short CPU-only reproduction using the owner's `D:/Projects/Test`,
copied into checkout scratch with both spawn loops changed to 10. Original
project/assets are not redistributed. The generated source and pack identities
are saved in `measurements.json`; reproduction requires the same private input.

Both binaries have the approved name/hierarchy binding change. The before
binary substitutes only the prior `channel_valid` implementation, compiled from
`build/animation-check/skinned-before-opt.c`, into the same release library.
The after binary uses the release library directly. Engine and generated game
are compiled with Clang 23.1.2, C17, `-O2 -fno-fast-math -ffp-contract=off`.
Release engine is CMake RelWithDebInfo, `TIDE_PLATFORM=OFF`.

`harness.c` includes the scratch generated `game.c` and links its embedded
`game_pack.c`. It ticks/hashes the world, records the generated views and
prepares palettes serially: 30 frames, first 10 discarded, remaining 20 averaged
separately for tick, hash and drawing. No renderer, networking, rollback,
session join or window is involved. The draw interval includes reset, view
recording and palette preparation. It is not a GPU or FPS measurement.

After our builds/tests ended at 12:22:44 UTC, the alternating three pairs began
after 12:24:26 UTC. No build/test ran concurrently with these six processes;
other desktop activity was not controlled. All six measured runs are retained.
Earlier exploratory debug runs overlapped builds/tests and are not comparison
evidence. No clock normalization, detailed per-frame distribution or GPU
measurement was collected.

Median of process means: drawing 6.711750 → 3.463030 ms, 48.4% lower. Tick/hash
remain separate observations, without a speedup claim. All six runs have 100
characters, one mesh command, 4,968,800 pack bytes, 13,117,656 world bytes and
final world hash `13602177110211490499`. Component-owned Animation copies still
cost world storage, tick copying, hashing, snapshots and networking. Keeping
state/time and selecting shared clips in the view avoids that pattern.

To reproduce with the original input, use this checkout's `tidec` to generate
`game.c`, `game.h` and embedded `game_pack.c` from the 10×10 scratch copy. Put
`harness.c` beside `game.c`. Configure/build the release `tide` target, then link:

```powershell
$env:TIDE_HIDDEN = '1'
cmake --preset release -DTIDE_PLATFORM=OFF
cmake --build --preset release --target tide
& 'C:/Program Files/LLVM/bin/clang.exe' -std=c17 -O2 -fno-fast-math -ffp-contract=off -I engine/include -I <generated-directory> harness.c <generated-directory>/game_pack.c build/release/engine/tide.lib -lws2_32 -lbcrypt -o <scratch>/after.exe
```

For the before comparison, compile the prior `channel_valid` in the otherwise
identical `skinned.c`, and put that object before `tide.lib` in the link command.
Run before/after alternately three times, without simultaneous builds/tests.
Saved hashes identify the actual measured artifacts, including this harness.

Full native debug 2,079/2,079 and web debug 2,050/2,050 CTests and the production
docs build pass. Exact cross-platform pose bits remain unchanged. Separate
hidden manual-world native captures verify actual run/jump rendering on the
100-character grid; they are not these timed runs or a session-join test.
