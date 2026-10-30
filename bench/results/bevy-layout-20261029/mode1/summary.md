Verified 12 processes in 3 alternating rounds.

ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering.

Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.
Bevy/Tide is the median paired ratio; values above 1 favor Tide.

| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen95-1m-1 | 1 | 0.872900 (0.850500–0.909800) | 0.309800 (0.270750–0.329250) | 1.020850 | 0.341× |
| filtered-frozen99-1m-1 | 1 | 0.096100 (0.094300–0.096100) | 0.048500 (0.048300–0.048750) | 1.038800 | 0.507× |
