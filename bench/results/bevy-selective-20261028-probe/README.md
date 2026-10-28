# Preliminary rare-row mask probe

This retained intermediate uses SIMD presence masks below one-quarter passing
density, before the final guard was narrowed to fewer than one match per 16
rows. Its reconstructed `candidate-codegen.patch` applies to `a2564c773` and
produces the exact measured compiler SHA-256
`67ff7f4acb77cfea8c7a045d5932bae00ee773e806cd61c01ebd4f1f6cbec999`.
The saved `run.ps1` also matches its measured identity in metadata.json. Other
workload/compiler/runtime inputs remain at the baseline revision.

Windows / Ryzen 9 7900X, Clang 23.1.2, Rust 1.95.0 and Bevy ECS 0.20.0;
unchanged O2 baseline x86-64 flags, game sources and Bevy executable. Three
alternating adjacent Before/Tide/Bevy triples per case use 200 warmup and 400
measured ticks. All 18 processes succeed, with matching logical digests and no
detected contention or quiet reset. Placement, desktop activity, clocks and
temperature remain uncontrolled.

Milliseconds per tick, medians of three process medians. Change is the median
paired current/before time change:

| Frozen | Before Tide | Masked Tide | Bevy | Paired Tide time change |
| --- | ---: | ---: | ---: | ---: |
| 90% | 1.47265 | 1.52600 | 0.60830 | +4.22% |
| 99% | 0.45800 | 0.27440 | 0.04845 | -40.02% |

Both densities take the intermediate mask path. The adverse 90% result motivates
the narrower guard in the [final comparison](../bevy-selective-20261028/README.md).
Bevy remains about 5.6 times faster at 99% frozen in this probe. Detailed ranges,
raw process medians, options and hashes are in [summary.md](summary.md),
[summary.json](summary.json), records.jsonl, metadata.json and raw/.

Recheck offline with `python bench/bevy/analyze.py bench/results/bevy-selective-20261028-probe`.
To rerun the candidate, apply its patch to the baseline, restore the saved runner
to `bench/bevy/run.ps1`, build the normal three versus targets, and invoke it with
`-BeforeBuildDir <frozen-baseline> -BeforeRevision a2564c773 -FilterSweep -Rounds 3
-OnlyCases 'filtered-frozen90-1m-1','filtered-frozen99-1m-1'` and a fresh output
directory. The runner is saved here for identity; its relative root calculation
assumes its original `bench/bevy/run.ps1` location.

These are scheduled ECS CPU ticks. Setup, final digest, per-tick world hashing,
retained snapshots, rollback, networking, rendering and application loops are
outside the timing. This probe establishes no whole-engine performance ratio.
