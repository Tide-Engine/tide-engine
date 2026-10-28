# Intermediate optimization candidate, retained

This candidate is superseded by the [density-guarded final comparison](../bevy-optimized-20261028-final/README.md).
Its primary 50%-frozen workload improves, but always compacting matching indices
regresses other filter densities. It is retained as adverse evidence, not the
implementation being merged.

The same Ryzen 9 7900X/Windows/Clang 23.1.2/Rust 1.95.0/Bevy 0.20.0 setup as
the [original comparison](../bevy-20261028/README.md) runs five adjacent
Before/Tide/Bevy triples per case, reversing order every other round. Before
binaries are the original frozen binaries at `aac950038`; all executable,
source/generated-C/configuration hashes and exact arguments are in metadata.json.
The Bevy executable remains unchanged. Each process uses 200 warmup/400 measured
ticks. All 150 processes exit zero, all logical digests agree across implementations,
rounds and thread counts, and the shared gate/15-second quiet guard/250 ms polling
detect no competing work or quiet reset (21:03:25-21:06:05 UTC).

The intermediate primary cases, median process tick times in milliseconds:

| Case | Before | Candidate | Bevy | Median paired candidate time change |
| --- | ---: | ---: | ---: | ---: |
| Filtered 1M, one thread | 3.94805 | 2.19080 | 2.42515 | -44.52% |
| Filtered 1M, four-thread budget | 1.07005 | 0.71790 | 1.21340 | -32.88% |
| Churn 2k/tick, one thread | 0.44410 | 0.34755 | 0.36775 | -21.66% |
| Churn 2k/tick, four-thread budget | 0.45075 | 0.33790 | 0.34800 | -25.22% |

summary.md/json retain all cases, ranges and paired ratios; records.jsonl and
raw/ retain every process. `python bench/bevy/analyze.py
bench/results/bevy-optimized-20261028` verifies the primary batch.

A subsequent three-pair serial diagnostic adds 1/10/90/99% frozen rows. At
90%, all three before/candidate pairs regress: 1.3884/1.7153,
1.38465/1.7228 and 1.4712/1.9916 ms. At 1%, two of three pairs regress;
10% is essentially tied; 99% is also adverse. These diagnostics hold the same
mutex and use a 15-second quiet guard, with process inventory only before/after
each process, not the primary runner's continuous polling. They motivate the
guard and are not pooled with primary results. All outputs/digests and parsed
records remain in diagnostics/density-trial/. The final runner includes these
densities under its full monitoring instead.

diagnostics/intermediate-codegen.patch preserves the compiler changes relative
to `aac950038`; intermediate-runner.ps1 is the exact primary runner. The compiler
source SHA-256 is `d104b15db6f349f740476d757edb70b1e75a24efc9837ee0d213334dd5b7dd75`.
The final refinement adds separate ordinary/compact rows functions and selects
compaction only when at least one quarter passes and one quarter skips, using
the count already available. It leaves the churn change intact.

Earlier three-pair sparse/churn diagnostic outputs and baseline sampler logs
are retained under diagnostics/. Sampler-perturbed times and these preliminary
timings do not contribute to reported primary medians. The sampler attributes
75.9% of wide program samples to the mixed Tide row loop and 37.3% of churn
program samples to Tide's row loop (the C reference is another 30.7%). Current
churn disassembly shows packed mulps/addps/paddd arithmetic. Native/WASI overlap
test logs and 66 additional cross-engine correctness comparisons are retained.

These are scheduled ECS CPU ticks, excluding setup, final digest, per-tick
hashing, snapshots, networking and rendering. Clocks, temperature, placement
and ordinary desktop activity remain uncontrolled. No FPS or whole-engine
speed factor is inferred; executables/PDBs remain in ignored build directories.
