Verified 18 processes in 3 alternating rounds.

ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering.

Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.
Bevy/Tide is the median paired ratio; values above 1 favor Tide.

| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: |
| wide-1m-1 | 1 | 1.174000 (1.167200–1.195500) | 2.333250 (2.329100–2.375600) | 0.913000 | 1.987× |
| filtered-1m-1 | 1 | 2.028700 (2.010850–2.054150) | 2.182050 (2.162650–2.244050) | 1.038700 | 1.085× |
| filtered-frozen99-1m-4 | 4 | 0.040800 (0.040000–0.040900) | 0.030500 (0.030100–0.030700) | 0.588700 | 0.748× |
