# Owned animation scans and skeleton binding (2026-10-11)

This CPU-only reproduction retains the owner's `D:/Projects/Test` game code:
100 characters, component-owned Animation data assigned every tick, and separate
idle/run/jump FBX exports sampled and bound in the view. Scratch copies of
`Game.tide` and `Player.tide` use the original assets through directory junctions.
The Rooms package/UI is omitted from this manual-world harness. Private project
sources, generated code and assets are not redistributed; reproduction requires
the same input. SHA-256 identities are in `identities.json`.

The before binary substitutes `engine/src/skinned.c` from base `c497e999` ahead
of the current release library. The after binary uses that same library directly.
The only production differences are vectorizable complete owned-key validation
and private node readers inside already validated separate sample/pose binding.
There is no retained validation cache, changed ownership or interpolation math.
Both binaries use identical generated source/pack, compiler and link flags.

`harness.c` advances the manual world at 60 Hz for 330 frames, discarding the
first 30 and averaging the next 300. It measures tick, world hash and draw
recording plus serial palette preparation separately. After each timed interval
it gathers the first character's state and hashes all integer palette/influence
textures. This untimed digest work perturbs caches equally in both variants.
The state mask 7 confirms all three clips are exercised. Matching final world
and accumulated palette hashes verify equivalent state and prepared draw data.
This excludes GPU rendering, networking, rollback and session/window startup;
it is not an FPS measurement or an isolated binding benchmark.

`measure.ps1` uses the existing named benchmark gate and requires 90 seconds
without detected builds, tests or benchmarks before running three alternating
process pairs. It also checks for competing work during each process. Other
desktop activity and CPU clocks are not controlled. Every measured row is kept
in `paired-rows.json`; preliminary short/instrumented runs are not comparison
evidence.

After the final 90-second quiet period, all six measured processes completed
without detected competing work at 14:10:30–14:10:37 UTC. Median of process
means for drawing is 2.765558 → 1.495396 ms, 45.9% lower (1.85× faster).
Before means are [2.649967, 2.774680, 2.765558]; after means are
[1.495396, 1.478381, 1.540453]. Tick/hash observations are separate and do not
establish a speedup. All rows have 100 characters, one draw command, a
5,689,760-byte pack, final world hash `10175709146634639136`, state mask 7 and
accumulated integer-texture hash `5069683498435541530`.

The world still packs to 13,117,656 bytes, with component-owned copies, tick
assignments, hashing, snapshots and networking costs. This change only reduces
sampling/binding overhead; storing state/time and choosing shared clips in a
view remains the simpler way to avoid that match-state cost.

Reproduction from this checkout, with the original game/assets copied to
`build/owned-animation/project`:

```powershell
$env:TIDE_HIDDEN = '1'
cmake --preset release -DTIDE_PLATFORM=OFF
cmake --build --preset release --target tide tidec
& ./build/release/bin/tidec.exe build/owned-animation/project/Game.tide build/owned-animation/project/Player.tide --game build/owned-animation/project --name game --pack-c -o build/owned-animation/gen
Copy-Item bench/results/owned-animation-scans/harness.c build/owned-animation/gen/measure.c
& 'C:/Program Files/LLVM/bin/clang.exe' -std=c17 -O2 -D_DLL -D_MT -Xclang --dependent-lib=msvcrt -fno-fast-math -ffp-contract=off -I engine/include -I build/owned-animation/gen build/owned-animation/gen/measure.c build/owned-animation/gen/game_pack.c build/release/engine/tide.lib -lws2_32 -lbcrypt -o build/owned-animation/measure-after.exe
```

Export `engine/src/skinned.c` from `c497e999` losslessly to scratch; build the
before binary with that source added before `tide.lib` and `-I engine/src`.
Run `bench/results/owned-animation-scans/measure.ps1` from the repository root.
Generated `#line` paths and linker identities can differ when reproducing in
another checkout; the saved identities describe the actual measured artifacts.
Exported text is normalized to LF; `exports` records its hashes separately
from the raw measured source identities.

Warning-free full native debug (2,136/2,136) and web debug (2,107/2,107) suites
pass, including OpenGL/WebGL/WebGPU checks and unchanged exact pose bits.
A final test-only addition exercises every key-scan tail length and one-key
clips; both complete runtime test executables pass again. Linux/macOS, MinGW,
Android, sanitizers and CI were not run on this Windows machine.

The same performance hunks were applied to the local installed runtime source
after checking its initial hash and compiling with its installed headers.
Its older absence of `tide_draw_gather_levels` is retained. A finite checkout-built
harness run confirms all three states and the same world/palette hashes. This
local patch does not replace the compiler/platform package or change the user's
project files; the CLI recompiles the changed runtime source on the next build.
