Verified 27 processes in 3 alternating rounds.

ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering.

Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.
Bevy/Tide is the median paired ratio; values above 1 favor Tide.

| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen95-1m-1 | 1 | 1.072900 (1.071150–1.078100) | 0.225400 (0.212750–0.228350) | 1.177450 | 0.210× |
| filtered-frozen99-1m-1 | 1 | 0.119950 (0.117700–0.120000) | 0.048750 (0.048700–0.049950) | 1.176500 | 0.414× |
| filtered-frozen99-1m-4 | 4 | 0.049050 (0.048400–0.054400) | 0.037900 (0.034550–0.044600) | 0.685250 | 0.783× |

Adjacent before/current comparison; Before/Tide above 1 favors the current Tide.
Change is the median paired percentage change in time (negative is faster).

| Case | Threads | Before ms (range) | Current ms (range) | Before/Tide | Change |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen95-1m-1 | 1 | 1.392350 (1.391600–1.397850) | 1.072900 (1.071150–1.078100) | 1.297× | -22.90% |
| filtered-frozen99-1m-1 | 1 | 0.274200 (0.272050–0.282300) | 0.119950 (0.117700–0.120000) | 2.330× | -57.08% |
| filtered-frozen99-1m-4 | 4 | 0.085850 (0.084700–0.086900) | 0.049050 (0.048400–0.054400) | 1.750× | -42.86% |
