Verified 27 processes in 3 alternating rounds.

ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering.

Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.
Bevy/Tide is the median paired ratio; values above 1 favor Tide.

| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen95-1m-1 | 1 | 1.133050 (1.124800–1.148750) | 0.251150 (0.245700–0.279150) | 1.196100 | 0.223× |
| filtered-frozen99-1m-1 | 1 | 0.110800 (0.109950–0.111400) | 0.055500 (0.055400–0.057050) | 1.189750 | 0.505× |
| filtered-frozen99-1m-4 | 4 | 0.046600 (0.046100–0.047550) | 0.039350 (0.036350–0.040450) | 0.774350 | 0.851× |

Adjacent before/current comparison; Before/Tide above 1 favors the current Tide.
Change is the median paired percentage change in time (negative is faster).

| Case | Threads | Before ms (range) | Current ms (range) | Before/Tide | Change |
| --- | ---: | ---: | ---: | ---: | ---: |
| filtered-frozen95-1m-1 | 1 | 1.108250 (1.099350–1.136200) | 1.133050 (1.124800–1.148750) | 0.985× | +1.49% |
| filtered-frozen99-1m-1 | 1 | 0.118550 (0.117800–0.119600) | 0.110800 (0.109950–0.111400) | 1.071× | -6.66% |
| filtered-frozen99-1m-4 | 4 | 0.049400 (0.048800–0.049600) | 0.046600 (0.046100–0.047550) | 1.059× | -5.53% |
