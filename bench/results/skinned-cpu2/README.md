# Skinned CPU comparison evidence

Source before: `77be2f2441a2e91c9dc4c08a4b8ed2163dab32fd`. Source after: `625596e82d87de6ae6d3d2edd791ca4674ac4dd5`. Fixture SHA-256: `409c8cd43fd512e8d9f2469573f70133560025c41250cab1a581f0d3b84bfad3`.

Six uninstrumented release OpenGL processes use three alternating before/after pairs, default counts 1/10/100/1000, all four modes, 20 warmup frames and 100 measured frames, repeat 1. The 96 rows contain 9,600 measured CPU frames and 9,600 completed GPU samples. Each case's comparison uses the median of three round medians; paired before/after ratios are reported separately in summary.json. CPU and GPU intervals overlap and must not be added or presented as pipelined FPS. GPU queries exclude upfront renderer preparation and geometry/instance uploads, but include on-demand palette texture creation/update inside draw_shaded; they are not pure shader execution time. Full-frame readback synchronizes each native frame.

Any interrupted attempts caused by concurrent external builds are listed in metadata.excluded_attempts and excluded from these 96 rows; their raw logs remain under scratch only.

clocks/ holds the six original 250 ms clock CSVs with only text normalization. clock-summary.json summarizes entire processes, not individual cases. Other desktop applications remained running; no clock, power or OS settings were changed.

The two final captures remain in scratch storage; summary.json records PNG and decoded RGB hashes, frame 119 at 1,000 independent poses, and the exact pixel comparison. Executables, libraries, objects, PDBs and capture images are excluded from this lightweight text export. Their measured identities remain in metadata and original-artifacts.json.

diagnostic-before/ is exploratory phase attribution with wall-clock instrumentation overhead included. Its phase values are arithmetic means per frame; its instrumented host frame values are medians. It is separate from the repeated performance comparison. diagnostic-after/ contains the corresponding optimized diagnostic.

Every exported text file is UTF-8 without a BOM and uses LF. original-artifacts.json records original pre-normalization bytes/hashes. export-manifest.json records exported text bytes/hashes and original source mapping. Hashes inside the preserved measurement metadata continue to identify original measured files. Verify this export without binaries or scratch storage:

```sh
node bench/results/skinned-cpu2/reproduce/export.mjs --verify bench/results/skinned-cpu2
```

The measured scripts are archived under reproduce/. Restore run_pairs.ps1, analyze.mjs and export.mjs to build/release/skinned-cpu2/ in a checkout when reproducing. run_pairs.ps1 expects the preserved before executable/metadata in build/release/skinned-77be2f24/ and the tested after release executable in build/release/bin/. The pack is embedded in each executable. Build and validate both revisions in that checkout before measuring; generated benchmark host, game and fixture must remain identical. The runner enforces HEAD/source identity, unchanged fixture, the shared benchmark gate and no concurrent builds/tests. Configure its Node, nvidia-smi, GPU UUID and machine strings for the measurement machine. Use a fresh result ledger without the six paired labels.

```powershell
$env:TIDE_HIDDEN = '1'
./build/release/skinned-cpu2/run_pairs.ps1 -AfterRevision 625596e82d87de6ae6d3d2edd791ca4674ac4dd5 -RunName paired
node build/release/skinned-cpu2/export.mjs build/release/skinned-cpu2/paired --output bench/results/skinned-cpu2 --normalize-ledger
```

The phase build/run scripts retain their original scratch layout and machine paths. Their metadata names each input/object/library hash; rebuilding needs those files generated from the stated source revision. Linking the instrumented skinned and platform objects before the preserved engine/platform archives replaces their same symbols. The diagnostic records exact call counts and overlapping phase means including texture cache/creation/free/update activity. No diagnostic instrumentation belongs in the uninstrumented pair runs.
