Verified 81 processes in 3 alternating rounds.

ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering.

Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.
Bevy/Tide is the median paired ratio; values above 1 favor Tide.

| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: |
| wide-1m-1 | 1 | 1.365800 (1.319550–1.388850) | 2.484750 (2.416650–2.595650) | 1.005200 | 1.819× |
| filtered-1m-1 | 1 | 0.427550 (0.414800–0.509750) | 1.136950 (1.123200–1.162550) | 1.119900 | 2.659× |
| filtered-1m-4 | 4 | 0.443450 (0.427900–0.510650) | 0.512850 (0.428400–0.578650) | 0.709600 | 1.133× |
| filtered-frozen1-1m-1 | 1 | 1.305300 (1.259900–1.305750) | 2.493400 (2.423050–2.545200) | 1.128900 | 1.923× |
| filtered-frozen10-1m-1 | 1 | 1.152400 (1.088550–1.156100) | 2.260300 (2.159700–2.387950) | 1.118500 | 2.066× |
| filtered-frozen90-1m-1 | 1 | 0.059100 (0.058900–0.059300) | 0.204200 (0.203800–0.222450) | 1.129750 | 3.467× |
| filtered-frozen95-1m-1 | 1 | 0.029700 (0.029500–0.048500) | 0.103200 (0.102950–0.105650) | 1.148050 | 3.466× |
| filtered-frozen99-1m-1 | 1 | 0.005700 (0.005700–0.005800) | 0.020150 (0.020050–0.020200) | 1.095050 | 3.518× |
| filtered-frozen99-1m-4 | 4 | 0.005800 (0.005700–0.005800) | 0.021900 (0.021500–0.022500) | 0.622700 | 3.776× |

Adjacent before/current comparison; Before/Tide above 1 favors the current Tide.
Change is the median paired percentage change in time (negative is faster).

| Case | Threads | Before ms (range) | Current ms (range) | Before/Tide | Change |
| --- | ---: | ---: | ---: | ---: | ---: |
| wide-1m-1 | 1 | 1.386500 (1.366100–1.434450) | 1.365800 (1.319550–1.388850) | 1.015× | -1.49% |
| filtered-1m-1 | 1 | 2.185450 (2.128200–2.189600) | 0.427550 (0.414800–0.509750) | 4.978× | -79.91% |
| filtered-1m-4 | 4 | 0.758900 (0.708750–0.777800) | 0.443450 (0.427900–0.510650) | 1.598× | -37.43% |
| filtered-frozen1-1m-1 | 1 | 2.969300 (2.960150–3.053850) | 1.305300 (1.259900–1.305750) | 2.340× | -57.26% |
| filtered-frozen10-1m-1 | 1 | 3.208300 (3.142400–3.265050) | 1.152400 (1.088550–1.156100) | 2.824× | -64.59% |
| filtered-frozen90-1m-1 | 1 | 1.326650 (1.322600–1.504750) | 0.059100 (0.058900–0.059300) | 22.379× | -95.53% |
| filtered-frozen95-1m-1 | 1 | 1.007150 (0.996300–1.068550) | 0.029700 (0.029500–0.048500) | 33.911× | -97.05% |
| filtered-frozen99-1m-1 | 1 | 0.097300 (0.095200–0.098000) | 0.005700 (0.005700–0.005800) | 17.070× | -94.14% |
| filtered-frozen99-1m-4 | 4 | 0.040100 (0.039200–0.040900) | 0.005800 (0.005700–0.005800) | 7.035× | -85.79% |
