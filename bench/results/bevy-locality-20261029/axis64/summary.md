Verified 18 processes in 3 alternating rounds.

ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering.

Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.
Bevy/Tide is the median paired ratio; values above 1 favor Tide.

| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen95-1m-1 | 1 | 0.956950 (0.946200–0.974750) | 0.214750 (0.208500–0.216800) | 1.053300 | 0.222× |
| filtered-frozen99-1m-1 | 1 | 0.096750 (0.095800–0.097000) | 0.048100 (0.048000–0.048350) | 1.020000 | 0.498× |

Adjacent before/current comparison; Before/Tide above 1 favors the current Tide.
Change is the median paired percentage change in time (negative is faster).

| Case | Threads | Before ms (range) | Current ms (range) | Before/Tide | Change |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen95-1m-1 | 1 | 0.891250 (0.887100–0.893750) | 0.956950 (0.946200–0.974750) | 0.931× | +7.37% |
| filtered-frozen99-1m-1 | 1 | 0.097500 (0.095400–0.098100) | 0.096750 (0.095800–0.097000) | 1.008× | -0.77% |
