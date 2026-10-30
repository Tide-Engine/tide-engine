Verified 18 processes in 3 alternating rounds.

ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering.

Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.
Bevy/Tide is the median paired ratio; values above 1 favor Tide.

| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: |
| wide-1m-1 | 1 | 1.174100 (1.172050–1.179200) | 2.343350 (2.314250–2.350600) | 0.915300 | 1.993× |
| filtered-1m-1 | 1 | 2.032050 (2.020600–2.060900) | 2.218550 (2.201600–2.293300) | 1.037000 | 1.092× |
| filtered-frozen99-1m-4 | 4 | 0.040900 (0.040400–0.041300) | 0.030300 (0.029250–0.032000) | 0.610500 | 0.741× |
