Verified 15 processes in 5 alternating rounds.

ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering.

Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.
Bevy/Tide is the median paired ratio; values above 1 favor Tide.

| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen90-1m-1 | 1 | 1.493500 (1.422750–1.604950) | 0.749900 (0.566600–0.980700) | 1.199450 | 0.516× |

Adjacent before/current comparison; Before/Tide above 1 favors the current Tide.
Change is the median paired percentage change in time (negative is faster).

| Case | Threads | Before ms (range) | Current ms (range) | Before/Tide | Change |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen90-1m-1 | 1 | 1.370050 (1.319300–1.595800) | 1.493500 (1.422750–1.604950) | 0.908× | +10.16% |
