Verified 12 processes in 3 alternating rounds.

ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering.

Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.
Bevy/Tide is the median paired ratio; values above 1 favor Tide.

| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen95-1m-1 | 1 | 0.878300 (0.863850–0.960950) | 0.260250 (0.242250–0.305100) | 1.017800 | 0.296× |
| filtered-frozen99-1m-1 | 1 | 0.095750 (0.095650–0.097050) | 0.048400 (0.048000–0.048600) | 1.011000 | 0.505× |
