Verified 81 processes in 3 alternating rounds.

ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering.

Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.
Bevy/Tide is the median paired ratio; values above 1 favor Tide.

| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: |
| wide-1m-1 | 1 | 1.499450 (1.332800–1.893600) | 2.886150 (2.400800–3.619550) | 1.078150 | 1.911× |
| filtered-1m-1 | 1 | 0.456900 (0.452500–0.923400) | 2.517550 (2.360950–3.704200) | 1.160300 | 5.167× |
| filtered-1m-4 | 4 | 0.475100 (0.405350–0.904700) | 1.368350 (1.149050–1.891050) | 0.695550 | 2.835× |
| filtered-frozen1-1m-1 | 1 | 1.417650 (1.207150–1.980450) | 3.381950 (2.954200–4.222650) | 1.107000 | 2.386× |
| filtered-frozen10-1m-1 | 1 | 1.259900 (1.054100–1.666550) | 2.715150 (2.692000–3.240500) | 1.279150 | 2.155× |
| filtered-frozen90-1m-1 | 1 | 0.060000 (0.059000–0.060000) | 0.676600 (0.470100–0.965700) | 1.183150 | 11.277× |
| filtered-frozen95-1m-1 | 1 | 0.029700 (0.029700–0.029800) | 0.215300 (0.209900–0.222700) | 1.080950 | 7.225× |
| filtered-frozen99-1m-1 | 1 | 0.005800 (0.005700–0.005800) | 0.048100 (0.047600–0.048550) | 1.108000 | 8.293× |
| filtered-frozen99-1m-4 | 4 | 0.005800 (0.005700–0.005900) | 0.033000 (0.031750–0.043250) | 0.723150 | 5.593× |

Adjacent before/current comparison; Before/Tide above 1 favors the current Tide.
Change is the median paired percentage change in time (negative is faster).

| Case | Threads | Before ms (range) | Current ms (range) | Before/Tide | Change |
| --- | ---: | ---: | ---: | ---: | ---: |
| wide-1m-1 | 1 | 1.419250 (1.280750–1.968650) | 1.499450 (1.332800–1.893600) | 1.040× | -3.81% |
| filtered-1m-1 | 1 | 2.316150 (2.120100–2.942250) | 0.456900 (0.452500–0.923400) | 4.640× | -78.45% |
| filtered-1m-4 | 4 | 0.795850 (0.669000–1.155150) | 0.475100 (0.405350–0.904700) | 1.650× | -39.41% |
| filtered-frozen1-1m-1 | 1 | 3.016800 (2.921300–3.949200) | 1.417650 (1.207150–1.980450) | 2.128× | -53.01% |
| filtered-frozen10-1m-1 | 1 | 3.319650 (3.077150–3.930550) | 1.259900 (1.054100–1.666550) | 2.635× | -62.05% |
| filtered-frozen90-1m-1 | 1 | 1.306700 (1.284550–1.532700) | 0.060000 (0.059000–0.060000) | 22.147× | -95.48% |
| filtered-frozen95-1m-1 | 1 | 1.035500 (0.909750–1.317950) | 0.029700 (0.029700–0.029800) | 34.748× | -97.12% |
| filtered-frozen99-1m-1 | 1 | 0.097300 (0.097100–0.097800) | 0.005800 (0.005700–0.005800) | 16.776× | -94.04% |
| filtered-frozen99-1m-4 | 4 | 0.040400 (0.039500–0.041700) | 0.005800 (0.005700–0.005900) | 6.966× | -85.64% |
