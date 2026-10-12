# O2 versus O3: external skinned stress test, 2026-10-12

The owner requested a higher-optimization comparison after enabling an optimized
engine in development builds. This measures the unchanged 2,500-character
`D:/Projects/Test` fixture at source revision `83ccb219`, using Clang 23.1.2 on
Windows 11 / Ryzen 9 7900X. Private game sources/assets and generated pack/C are
not redistributed; `metadata.json` records their SHA-256 identities.

Only engine optimization differs: `-O2 -g` versus `-O3 -g`, with assertions,
`-fno-fast-math` and `-ffp-contract=off` in both. Each platform links the same
already-compiled `-O0 -g` caller/game/pack objects to the two engine libraries.
Native uses the Windows debug CRT; WebAssembly runs under Node 24.16.0 WASI,
not a browser. Runtime libraries and binaries stay in ignored build scratch.

Each process advances one manual world at 60 Hz for 330 frames, discards 30
warmups and measures the following 300. Tick, world hashing, generated views,
serial palette preparation and their actual combined interval are separate
metrics. Every frame hashes active integer palette/influence texture bytes
after timing, perturbing caches equally. Five alternating pairs per platform
produce 20 valid rows and 6,000 measured frames. The named benchmark gate and
90-second quiet guard waited through eight detections of other build/test work;
no competing work was detected during measured processes. Other desktop
activity and CPU clocks remain uncontrolled.

Reported values are medians of the five process arithmetic means, in ms/frame;
paired ratios and per-process median/p95 values remain in the raw/summary data.

| CPU interval | Native O2 | Native O3 | WASI O2 | WASI O3 |
| --- | ---: | ---: | ---: | ---: |
| Tick | 0.368 | 0.372 | 0.541 | 0.545 |
| Views | 20.616 | 21.647 | 28.706 | 28.676 |
| Tick + hash + views + preparation | 20.990 | 22.023 | 29.257 | 29.227 |

Native views are 5.00% slower by the ratio of those medians; every native pair
is slower, with paired ratios 1.0201–1.1011 and median paired ratio 1.0468.
WASI views differ by -0.10%, within observed variation. This fixture gives no
reason to replace O2 with O3; it does not establish results for all workloads.
GPU execution, window/display wait, interpolation snapshots, rollback,
networking and Rooms UI are excluded. These are not gameplay FPS measurements.

The later compile batch holds the same gate and checks for competing work before
and after each build, immediately following the completed runtime batch. Five
alternating cold-engine pairs per platform remove only the engine target's
objects and build with twelve compiler jobs; configuration, caller compilation
and final executable linking are outside timing. Tool/filesystem caches are
warm. Median native engine compile is 1.487 -> 1.508 s (+1.41%); WASI is
1.278 -> 1.344 s (+5.14%). Raw variation and paired ratios are retained; these
small wall-time differences do not establish a general compilation penalty.
The earlier setup compile rows were collected before the quiet guard detected
other tests and are explicitly excluded, retained in
`preliminary-setup-build-rows.json`. Two setup linker-script mistakes were
corrected before any runtime comparison.

Every runtime row has 2,500 characters, all three animation states (mask 7),
one draw command and two active integer textures. Final world hash
`818c38d6c6daad37` and accumulated prepared-texture hash `aef3539b6e00d762`
match across O2/O3 and native/WASI. Separate entirely O3 core-test builds,
including optimized callers, pass 435/435 on native and WASI with unchanged
math/random/pose pins. These are scoped checks, not an all-platform O3 rollout
validation. No production flag/default changes, CI dispatch or deployment.

Reproduce from the checkout root on Windows with Clang, CMake, Ninja and the
same private fixture. The scripts expect this checkout's `build/debug/bin/tidec.exe`
and a Node 24.16+ executable at `build/tools/node/node.exe`; configure/build the
native debug compiler first. CMake downloads its pinned WASI SDK if absent.

```powershell
$env:TIDE_HIDDEN = '1'
./bench/results/optimization-levels/measure.ps1 -Game D:/Projects/Test
./bench/results/optimization-levels/measure-compiles.ps1
./build/tools/node/node.exe ./bench/results/optimization-levels/analyze.mjs
```

Outputs go into `build/optimization-levels/`. Verify the committed data without
rerunning the private fixture:

```powershell
./build/tools/node/node.exe ./bench/results/optimization-levels/analyze.mjs bench/results/optimization-levels
```
