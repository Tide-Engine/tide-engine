Verified 27 processes in 3 alternating rounds.

ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering.

Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.
Bevy/Tide is the median paired ratio; values above 1 favor Tide.

| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen95-1m-1 | 1 | 1.253200 (1.165500–1.343400) | 0.230450 (0.214750–0.294200) | 1.502050 | 0.184× |
| filtered-frozen99-1m-1 | 1 | 0.109900 (0.108500–0.111250) | 0.047500 (0.047000–0.048700) | 1.348450 | 0.438× |
| filtered-frozen99-1m-4 | 4 | 0.045450 (0.044800–0.046600) | 0.033000 (0.032450–0.039600) | 0.718950 | 0.724× |

Adjacent before/current comparison; Before/Tide above 1 favors the current Tide.
Change is the median paired percentage change in time (negative is faster).

| Case | Threads | Before ms (range) | Current ms (range) | Before/Tide | Change |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen95-1m-1 | 1 | 1.198900 (1.116650–1.273850) | 1.253200 (1.165500–1.343400) | 0.948× | +5.46% |
| filtered-frozen99-1m-1 | 1 | 0.120950 (0.116900–0.123000) | 0.109900 (0.108500–0.111250) | 1.087× | -8.02% |
| filtered-frozen99-1m-4 | 4 | 0.048800 (0.048700–0.049100) | 0.045450 (0.044800–0.046600) | 1.080× | -7.43% |
