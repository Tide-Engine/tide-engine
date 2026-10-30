Verified 12 processes in 3 alternating rounds.

ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering.

Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.
Bevy/Tide is the median paired ratio; values above 1 favor Tide.

| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen95-1m-1 | 1 | 0.863500 (0.863100–0.877750) | 0.241700 (0.235950–0.260750) | 1.033650 | 0.275× |
| filtered-frozen99-1m-1 | 1 | 0.096100 (0.095750–0.096800) | 0.048700 (0.048700–0.048900) | 1.040950 | 0.509× |
