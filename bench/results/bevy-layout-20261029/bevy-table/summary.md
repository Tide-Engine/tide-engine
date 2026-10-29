Verified 30 processes in 3 alternating rounds.

ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering.

Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.
Bevy/Tide is the median paired ratio; values above 1 favor Tide.

| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: |
| wide-1m-1 | 1 | 1.197100 (1.160900–1.297400) | 2.312200 (2.306850–2.326950) | 0.892950 | 1.927× |
| filtered-1m-1 | 1 | 2.027700 (2.004350–2.036950) | 1.035350 (1.030400–1.051950) | 1.020400 | 0.514× |
| filtered-frozen95-1m-1 | 1 | 0.865900 (0.860650–0.867600) | 0.100900 (0.099600–0.101000) | 1.022500 | 0.116× |
| filtered-frozen99-1m-1 | 1 | 0.095850 (0.095800–0.097000) | 0.019900 (0.019800–0.019950) | 1.012150 | 0.207× |
| filtered-frozen99-1m-4 | 4 | 0.039900 (0.039700–0.040000) | 0.021700 (0.021700–0.022100) | 0.558600 | 0.544× |
