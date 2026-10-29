Verified 18 processes in 3 alternating rounds.

ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering.

Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.
Bevy/Tide is the median paired ratio; values above 1 favor Tide.

| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen95-1m-1 | 1 | 0.929500 (0.904900–0.942450) | 0.208700 (0.206150–0.209350) | 1.042050 | 0.225× |
| filtered-frozen99-1m-1 | 1 | 0.098000 (0.096000–0.098300) | 0.048300 (0.047700–0.048400) | 1.049200 | 0.493× |

Adjacent before/current comparison; Before/Tide above 1 favors the current Tide.
Change is the median paired percentage change in time (negative is faster).

| Case | Threads | Before ms (range) | Current ms (range) | Before/Tide | Change |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen95-1m-1 | 1 | 0.881400 (0.863900–1.077150) | 0.929500 (0.904900–0.942450) | 0.935× | +6.93% |
| filtered-frozen99-1m-1 | 1 | 0.098200 (0.097900–0.098750) | 0.098000 (0.096000–0.098300) | 1.008× | -0.76% |
