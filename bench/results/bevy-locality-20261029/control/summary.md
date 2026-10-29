Verified 18 processes in 3 alternating rounds.

ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering.

Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.
Bevy/Tide is the median paired ratio; values above 1 favor Tide.

| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen95-1m-1 | 1 | 0.861000 (0.854950–1.021950) | 0.209450 (0.208650–0.210950) | 1.033500 | 0.244× |
| filtered-frozen99-1m-1 | 1 | 0.097100 (0.096900–0.098400) | 0.047850 (0.047450–0.048200) | 1.020850 | 0.490× |

Adjacent before/current comparison; Before/Tide above 1 favors the current Tide.
Change is the median paired percentage change in time (negative is faster).

| Case | Threads | Before ms (range) | Current ms (range) | Before/Tide | Change |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen95-1m-1 | 1 | 0.872650 (0.867650–1.042450) | 0.861000 (0.854950–1.021950) | 1.015× | -1.46% |
| filtered-frozen99-1m-1 | 1 | 0.099350 (0.098950–0.100500) | 0.097100 (0.096900–0.098400) | 1.021× | -2.09% |
