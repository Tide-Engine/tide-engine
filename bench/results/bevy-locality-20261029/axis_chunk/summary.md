Verified 18 processes in 3 alternating rounds.

ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering.

Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.
Bevy/Tide is the median paired ratio; values above 1 favor Tide.

| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen95-1m-1 | 1 | 0.945700 (0.941050–0.954100) | 0.207650 (0.207150–0.209600) | 1.044200 | 0.221× |
| filtered-frozen99-1m-1 | 1 | 0.096300 (0.096000–0.097500) | 0.048300 (0.048100–0.048950) | 1.030350 | 0.502× |

Adjacent before/current comparison; Before/Tide above 1 favors the current Tide.
Change is the median paired percentage change in time (negative is faster).

| Case | Threads | Before ms (range) | Current ms (range) | Before/Tide | Change |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen95-1m-1 | 1 | 0.951700 (0.925650–1.068150) | 0.945700 (0.941050–0.954100) | 1.006× | -0.63% |
| filtered-frozen99-1m-1 | 1 | 0.096700 (0.096400–0.099300) | 0.096300 (0.096000–0.097500) | 1.004× | -0.41% |
