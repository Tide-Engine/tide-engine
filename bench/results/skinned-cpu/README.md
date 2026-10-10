# Skinned CPU comparison evidence

Source before: `b9faf142183e7ed41fb81e3e923af2a06cfbb4e3`. Source after: `c06de5f51d5eb521d10f4e2803bf554866f23453`. Fixture SHA-256: `409c8cd43fd512e8d9f2469573f70133560025c41250cab1a581f0d3b84bfad3`.

Six uninstrumented release OpenGL processes use three alternating before/after pairs, default counts 1/10/100/1000, all four modes, 20 warmup frames and 100 measured frames, repeat 1. The 96 rows contain 9,600 measured CPU frames and 9,600 completed GPU samples. Each case's comparison uses the median of three round medians; paired before/after ratios are reported separately in summary.json. CPU and GPU intervals overlap and must not be added or presented as pipelined FPS. Full-frame readback synchronizes each native frame.

Any interrupted attempts caused by concurrent external builds are listed in metadata.excluded_attempts and excluded from these 96 rows; their raw logs remain under scratch only.

clocks/ holds the six original 250 ms clock CSVs with only text normalization. clock-summary.json summarizes entire processes, not individual cases. Other desktop applications remained running; no clock, power or OS settings were changed.

The two final captures remain in scratch storage; summary.json records PNG and decoded RGB hashes, frame 119 at 1,000 independent poses, and the exact pixel comparison. Executables, libraries, objects, PDBs and capture images are excluded from this lightweight text export. Their measured identities remain in metadata and original-artifacts.json.

diagnostic-before/ is exploratory phase attribution with wall-clock instrumentation overhead included. Its phase values are arithmetic means per frame; the uninstrumented host frame values are medians. It is separate from the repeated performance comparison. No optimized phase diagnostic is included.

Every exported text file is UTF-8 without a BOM and uses LF. original-artifacts.json records original pre-normalization bytes/hashes. export-manifest.json records exported text bytes/hashes and original source mapping. Hashes inside the preserved measurement metadata continue to identify original measured files. Verify this export without binaries or scratch storage:

```sh
node bench/results/skinned-cpu/reproduce/export.mjs --verify bench/results/skinned-cpu
```

The measured scripts are archived under reproduce/. Restore run_pairs.ps1, analyze.mjs and export.mjs to build/release/skinned-improvement/ in a checkout when reproducing. run_pairs.ps1 expects the preserved before executable/metadata in build/release/skinned-before/ and the tested after release executable in build/release/bin/. The pack is embedded in each executable. Build and validate both revisions in that checkout before measuring; generated benchmark host, game and fixture must remain identical. The runner enforces HEAD/source identity, unchanged fixture, the shared benchmark gate and no concurrent builds/tests. Configure its Node, nvidia-smi, GPU UUID and machine strings for the measurement machine. Use a fresh result ledger without the six paired labels.

```powershell
$env:TIDE_HIDDEN = '1'
./build/release/skinned-improvement/run_pairs.ps1 -AfterRevision c06de5f51d5eb521d10f4e2803bf554866f23453 -RunName paired
node build/release/skinned-improvement/export.mjs build/release/skinned-improvement/paired --output bench/results/skinned-cpu --normalize-ledger
```

The phase build/run scripts retain their original scratch layout and machine paths. Their metadata names each input/object/library hash; rebuilding needs those files generated from the stated source revision. Linking the instrumented skinned object before the engine archive replaces its same symbols. No diagnostic instrumentation belongs in the uninstrumented pair runs.
