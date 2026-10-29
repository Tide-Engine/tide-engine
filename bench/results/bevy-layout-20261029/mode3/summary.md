Verified 12 processes in 3 alternating rounds.

ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering.

Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.
Bevy/Tide is the median paired ratio; values above 1 favor Tide.

| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen95-1m-1 | 1 | 0.876200 (0.859200–0.915550) | 0.226050 (0.217900–0.309350) | 1.014350 | 0.263× |
| filtered-frozen99-1m-1 | 1 | 0.096300 (0.094200–0.096300) | 0.048300 (0.048000–0.048700) | 1.037850 | 0.502× |
