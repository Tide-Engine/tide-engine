Verified 18 processes in 3 alternating rounds.

ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering.

Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.
Bevy/Tide is the median paired ratio; values above 1 favor Tide.

| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen90-1m-1 | 1 | 1.526000 (1.520500–1.664950) | 0.608300 (0.557300–0.703400) | 1.197350 | 0.400× |
| filtered-frozen99-1m-1 | 1 | 0.274400 (0.272250–0.275500) | 0.048450 (0.048450–0.048800) | 1.167350 | 0.178× |

Adjacent before/current comparison; Before/Tide above 1 favors the current Tide.
Change is the median paired percentage change in time (negative is faster).

| Case | Threads | Before ms (range) | Current ms (range) | Before/Tide | Change |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen90-1m-1 | 1 | 1.472650 (1.458900–1.546850) | 1.526000 (1.520500–1.664950) | 0.959× | +4.22% |
| filtered-frozen99-1m-1 | 1 | 0.458000 (0.457500–0.463600) | 0.274400 (0.272250–0.275500) | 1.667× | -40.02% |
