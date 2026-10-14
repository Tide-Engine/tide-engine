# Fourth skinned CPU comparison evidence

Before source: `a10d2f4b02c38b3bbb9eee51a00faef6dab0f3f6`; after source: `711af03879d9bd687edc4f977ec92f80544e0c22`. Fixture SHA256: `409c8cd43fd512e8d9f2469573f70133560025c41250cab1a581f0d3b84bfad3`. Six uninstrumented RelWithDebInfo (-O2) processes run three alternating before/after pairs, counts 1000/10000 and all four modes, 20 warmup and 100 measured frames, repeat 1. The 48 rows contain 4800 CPU frames and 4800 completed GPU samples.

Summaries use the median of three process medians. Per-process medians and p95s remain recorded; the median of process p95s is not a pooled-frame p95. Within-round paired ratios remain distinct from ratios of aggregate medians. Each case has 20 warmup frames; timings exclude first-load validation and initial allocation/pipeline setup. Palette computation is included in CPU submission on both sides. CPU view recording and submission are distinct; their summed medians are only a derived CPU estimate. GPU queries exclude upfront preparation/geometry/instance uploads but include on-demand palette uploads and drawing. Synchronized readback-frame timings do not establish pipelined FPS.

The host, Tide game, generated header/pack and fixture remain byte-identical to preserved a10 inputs. Generated game.c differs only in the two #line debug file paths naming the isolated candidate build directory. Both raw hashes and the hash after replacing exactly those two paths are retained. Work counts match before/after. Each character has 3072 vertices, 6048 triangles, 64 joints and four influences. Fixed summed projected area makes 10k characters tiny; no LOD, culling or match simulation is measured.

The named gate and 90-second sustained quiet guard reject detected competing builds/tests/benchmarks; other desktop applications remain running. GPU clocks sampled at 250 ms cover whole processes, not individual cases or clock-normalized throughput. No system settings changes were made. Any excluded attempts are disclosed in metadata.

The final 10k independent frame 119 comes from each side of the last pair. Two additional unsaved 1000-independent capture-only processes use the same 100+20 frames and are excluded from timing aggregates. PNG/RGB pixels must match exactly at each count separately. Large binaries/captures remain in scratch; hashes are preserved. The previous 360-row history stays exact; 48 new rows bring the ledger to 408. Original artifact hashes and exported UTF-8 LF hashes are separate.

Verify the exported text without binaries:

```sh
node bench/results/skinned-cpu4/reproduce/export.mjs --verify bench/results/skinned-cpu4
```

Reproduction requires rebuilding both stated revisions in this checkout; preserved binaries are not included. Use RelWithDebInfo (-O2), the identical host/game/fixture, and a private baseline snapshot at build/release/skinned-cpu3/after-snapshot/. The original baseline preservation script is retained as reproduce/preserve-before-original.ps1 for reference. Restore the CPU4 reproduction scripts into build/release/skinned-cpu4/ and the validation runner into build/skinned-cpu4-validation/. Adapt checkout-specific tool paths and machine descriptions. Preserve source/fixture identities and restore the exact 360-row input ledger.

For the candidate, run run-validation.ps1 -StartApproved -RunName <fresh-name> on the frozen implementation, commit those tested bytes, and run build-release.ps1 -AfterRevision <tested-commit>. This creates the isolated build/skinned-cpu4-release/ candidate and records its executable hash in the validation summary. Then invoke preserve-after.ps1 -AfterRevision <tested-commit> before run-final.ps1 -AfterRevision <tested-commit> -RunName <fresh-name>. run-final rechecks the complete validated source/test file set, file hashes and release hash, and acquires the shared measurement gate before invoking run_pairs.ps1. Export with export.mjs <verified-run-directory> --normalize-ledger.

The recorded original executable hashes identify this measurement; newly rebuilt binaries can differ. When reproducing on another checkout or toolchain, adapt expected hashes only after independently establishing the stated source/build identities. Direct run_pairs.ps1 is a lower-level fallback after equivalent source validation, snapshot preservation and executable/workload checks; it does not replace correctness validation.

The broad baseline phase diagnostic, if present, uses a separate private instrumented object and ten measured frames. Its nested phase means overlap and include timer overhead; they are exploratory attribution, not the paired uninstrumented timings.
