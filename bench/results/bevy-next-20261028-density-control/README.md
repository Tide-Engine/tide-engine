# 90%-frozen follow-up control

This five-round adjacent Before/Tide/Bevy check repeats only the adverse
`filtered-frozen90-1m-1` case from the
[16-case batch](../bevy-next-20261028-final/README.md). It uses the same frozen
c30 baseline, measured compiler implementation, binaries, game, flags and inputs:
1M wide components, one offered thread, 200 warmup/400 measured ticks. All
15 processes exit zero and agree on digest `0x07c33fc7d6a94b2d`; the analyzer
verifies complete triples, raw stdout hashes and saved identities.

Before/current/Bevy median-of-process-medians is 1.37005/1.49350/0.74990 ms.
The median paired Tide change is adverse at +10.16%; process-median ranges
overlap. [summary.md](summary.md) and [summary.json](summary.json) preserve
every round, the unchanged C-reference observations and paired statistics.
The C reference also varies substantially, including slower current-process
observations in rounds 3 and 4. No C-based normalization is used to adjust the
reported result. The original slow-task machine body is unchanged, but its
new wrapper adds work. Neither that overhead nor environmental variation is
established as the sole cause. The concern remains recorded.

The shared mutex, 15-second quiet guard and 250 ms known-process polling record
no detected contention or quiet reset. CI-log inspection began during this
follow-up; ordinary desktop/tool activity and interference outside the monitored
process set remain uncontrolled. This batch is not pooled with the earlier
batch. It measures scheduled CPU ECS ticks, excluding setup, digest, per-tick
hashing, snapshots, rollback, networking and rendering.

Use the main report's build/reproduction instructions with
`-Rounds 5 -FilterSweep -OnlyCases filtered-frozen90-1m-1` and a fresh output
directory. No benchmark, test or assertion was weakened in response to this
adverse observation.
