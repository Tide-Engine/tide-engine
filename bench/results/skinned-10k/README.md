# 1,000 and 10,000 character rendering measurements

Measured source: `a10d2f4b02c38b3bbb9eee51a00faef6dab0f3f6`; checkout HEAD at measurement: `389b7768f5e331100617f590fcc2b7517cf40e35`. The preserved CPU3 release executable SHA-256 is `46abd8b50fb5abb0ed240ee6dca5ee5b6e5b9c51fbc78e4d65245a7ab08bf575`; its embedded fixture is `409c8cd43fd512e8d9f2469573f70133560025c41250cab1a581f0d3b84bfad3`. No production code was changed or rebuilt for this measurement.

Three fresh hidden OpenGL processes each run counts 1,000 then 10,000, with static/rest/shared/independent cases in that order, 20 warmup and 100 measured frames, repeat 1. Labels are skinned-10k-final-r1/r2/r3. All 24 rows contain 2,400 CPU frames and 2,400 completed GPU samples. Summaries report the median of three process medians and retain each median/range. The p95 summary is the median of three process p95 values, not a pooled-frame p95. Scaling ratios compare 10k/1k within each process before taking their median.

CPU view recording, CPU submission, GPU queries and synchronized completion/readback have distinct boundaries. Deferred palette math is included in CPU submission. GPU queries exclude upfront preparation/geometry/instance uploads, but include on-demand palette uploads and drawing; they are not isolated shader timings. The CPU sum adds separate interval medians and is only a derived estimate. Whole-frame readback timings do not establish pipelined FPS.

The fixture has 3,072 vertices, 6,048 triangles, 64 joints and four influences per character. At 10k this submits 30,720,000 vertices and 60,480,000 triangles/frame. The grid keeps nominal projected coverage constant; characters become smaller, so this is not ten times the pixel workload. Ten independent palette pages produce 11 total draws including clear and 40,960,000 palette upload bytes/frame. Memory values include earlier-case caches. The fresh 1k cases have a different preceding-case history from the original CPU3 study.

The named gate and 90-second sustained quiet guard exclude competing builds/tests/benchmarks; other desktop applications remain running. 250 ms NVIDIA observations cover entire processes, not individual cases or clock-normalized throughput. No system/power settings were changed. Excluded attempts, if any, are identified in metadata.

The final capture is frame 119 with 10k independently sampled poses at 1024×576. PNG/RGB hashes are retained in summary.json; the image and executable remain in scratch storage. No pixel comparison across the different character-count grids is implied. The 336-row prior ledger prefix remains exact; 24 new rows bring it to 360. Original measured artifact hashes and exported LF text hashes are recorded separately. CPU3 sealed evidence remains unchanged.

Verify the lightweight export:

```sh
node bench/results/skinned-10k/reproduce/export.mjs --verify bench/results/skinned-10k
```

To reproduce, rebuild and preserve the stated source revision with the unchanged host/fixture, restore reproduce scripts to build/release/skinned-10k/, and adapt the machine-specific snapshot/executable/Node/GPU paths in run.ps1. Update the expected executable hash from that validated preserved build and the machine/driver description; keep the source, fixture and historical-ledger identities fixed. Restore the exact 336-row history before saving fresh labels. The runner refuses existing output directories and an unexpected history hash. It records the measured snapshot revision separately from checkout HEAD. Export afterward with export.mjs <verified-run-directory>.
