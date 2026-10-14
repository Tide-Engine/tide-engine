# Skinned CPU comparison evidence

Source before: `874422c0becc35116c94e31e4d92bbe074dde6b3`. Source after: `a10d2f4b02c38b3bbb9eee51a00faef6dab0f3f6`. Fixture SHA-256: `409c8cd43fd512e8d9f2469573f70133560025c41250cab1a581f0d3b84bfad3`.

Six uninstrumented release OpenGL processes use three alternating before/after pairs, default counts 1/10/100/1000, all four modes, 20 warmup frames and 100 measured frames, repeat 1. The 96 rows contain 9,600 measured CPU frames and 9,600 completed GPU samples. Each case's comparison uses the median of three round medians; paired before/after ratios are reported separately in summary.json. CPU and GPU intervals overlap and must not be added or presented as pipelined FPS. GPU queries exclude upfront renderer preparation and geometry/instance uploads, but include on-demand palette texture creation/update inside draw_shaded; they are not pure shader execution time. Full-frame readback synchronizes each native frame. Palette computation moves from view recording into CPU submission, before the GPU query; view reductions alone do not describe net CPU improvement. Compare both CPU intervals and full-frame readback. The derived CPU sum in summary.json adds separate view/submission medians within each round, then takes the median across rounds. It is an estimate, not a median of combined per-frame samples, and adds no GPU timing.

Any interrupted attempts caused by concurrent external builds are listed in metadata.excluded_attempts and excluded from these 96 rows; their raw logs remain under scratch only.

clocks/ holds the six original 250 ms clock CSVs with only text normalization. clock-summary.json summarizes entire processes, not individual cases. Other desktop applications remained running; no clock, power or OS settings were changed.

The two final captures remain in scratch storage; summary.json records PNG and decoded RGB hashes, frame 119 at 1,000 independent poses, and the exact pixel comparison. Executables, libraries, objects, PDBs and capture images are excluded from this lightweight text export. Their measured identities remain in metadata and original-artifacts.json.

No before phase diagnostic is included in this comparison. No optimized phase diagnostic is included.

Every exported text file is UTF-8 without a BOM and uses LF. original-artifacts.json records original pre-normalization bytes/hashes. export-manifest.json records exported text bytes/hashes and original source mapping. Hashes inside the preserved measurement metadata continue to identify original measured files. Verify this export without binaries or scratch storage:

```sh
node bench/results/skinned-cpu3/reproduce/export.mjs --verify bench/results/skinned-cpu3
```

The measured scripts are archived under reproduce/. Restore run_pairs.ps1, analyze.mjs, export.mjs and preserve-after.ps1 to build/release/skinned-cpu3/ in a checkout when reproducing. run_pairs.ps1 expects the preserved before executable/metadata in build/release/skinned-874422c0/ and the tested after release executable in build/release/bin/. The pack is embedded in each executable. Build and validate both revisions in that checkout before measuring; generated benchmark host, game and fixture must remain identical. At the tested after revision, run preserve-after.ps1 to create the required after-snapshot metadata and source hashes before starting the paired runner. The runner enforces HEAD/source identity, unchanged fixture, the shared benchmark gate and no concurrent builds/tests. The ledger must retain its exact previous 240-row history. Atlas batching can change draw and upload counts; these differences remain explicit in summary.json and the final sampled-frame PNG/RGB pixels must match exactly. Configure its Node, nvidia-smi, GPU UUID and machine strings for the measurement machine. Use a fresh result ledger without the six paired labels.

```powershell
$env:TIDE_HIDDEN = '1'
./build/release/skinned-cpu3/preserve-after.ps1 -AfterRevision a10d2f4b02c38b3bbb9eee51a00faef6dab0f3f6
./build/release/skinned-cpu3/run_pairs.ps1 -AfterRevision a10d2f4b02c38b3bbb9eee51a00faef6dab0f3f6 -RunName paired
node build/release/skinned-cpu3/export.mjs build/release/skinned-cpu3/paired --output bench/results/skinned-cpu3 --normalize-ledger
```

If phase diagnostics are included, their build/run scripts retain their original scratch layout and machine paths. Their metadata names each input/object/library hash; rebuilding needs those files generated from the stated source revision. Linking the instrumented skinned and platform objects before the preserved engine/platform archives replaces their same symbols. The diagnostic records exact call counts and overlapping phase means including texture cache/creation/free/update activity. No diagnostic instrumentation belongs in the uninstrumented pair runs.
