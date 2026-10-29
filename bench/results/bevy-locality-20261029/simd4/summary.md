Verified 18 processes in 3 alternating rounds.

ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering.

Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.
Bevy/Tide is the median paired ratio; values above 1 favor Tide.

| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen95-1m-1 | 1 | 0.919450 (0.918950–0.934550) | 0.210850 (0.209350–0.211200) | 1.042900 | 0.228× |
| filtered-frozen99-1m-1 | 1 | 0.098750 (0.097800–0.098900) | 0.048200 (0.047700–0.048300) | 1.029500 | 0.488× |

Adjacent before/current comparison; Before/Tide above 1 favors the current Tide.
Change is the median paired percentage change in time (negative is faster).

| Case | Threads | Before ms (range) | Current ms (range) | Before/Tide | Change |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen95-1m-1 | 1 | 0.871750 (0.845750–0.886300) | 0.919450 (0.918950–0.934550) | 0.948× | +5.47% |
| filtered-frozen99-1m-1 | 1 | 0.098400 (0.098000–0.098400) | 0.098750 (0.097800–0.098900) | 0.996× | +0.36% |
