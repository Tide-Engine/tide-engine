Verified 12 processes in 3 alternating rounds.

ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering.

Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.
Bevy/Tide is the median paired ratio; values above 1 favor Tide.

| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen95-1m-1 | 1 | 0.910450 (0.888750–0.923150) | 0.295450 (0.232450–0.313350) | 1.013250 | 0.332× |
| filtered-frozen99-1m-1 | 1 | 0.094950 (0.094200–0.095100) | 0.048100 (0.047750–0.048200) | 1.027200 | 0.506× |
