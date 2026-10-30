Verified 36 processes in 3 alternating rounds.

ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering.

Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.
Bevy/Tide is the median paired ratio; values above 1 favor Tide.

| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: |
| wide-1m-1 | 1 | 1.294900 (1.291100–1.319050) | 2.475100 (2.427500–2.489600) | 0.996200 | 1.887× |
| filtered-1m-1 | 1 | 2.144600 (2.137150–2.179250) | 2.518250 (2.497800–2.617300) | 1.120850 | 1.174× |
| filtered-frozen95-1m-1 | 1 | 0.997500 (0.984150–1.010350) | 0.273400 (0.267800–0.399850) | 1.143150 | 0.274× |
| filtered-frozen99-1m-4 | 4 | 0.040700 (0.040400–0.041100) | 0.030200 (0.030100–0.030700) | 0.630250 | 0.745× |

Adjacent before/current comparison; Before/Tide above 1 favors the current Tide.
Change is the median paired percentage change in time (negative is faster).

| Case | Threads | Before ms (range) | Current ms (range) | Before/Tide | Change |
| --- | ---: | ---: | ---: | ---: | ---: |
| wide-1m-1 | 1 | 1.324250 (1.291350–1.335350) | 1.294900 (1.291100–1.319050) | 1.012× | -1.22% |
| filtered-1m-1 | 1 | 2.158600 (2.152850–2.168300) | 2.144600 (2.137150–2.179250) | 1.010× | -0.99% |
| filtered-frozen95-1m-1 | 1 | 1.027750 (1.020800–1.079400) | 0.997500 (0.984150–1.010350) | 1.037× | -3.59% |
| filtered-frozen99-1m-4 | 4 | 0.045500 (0.045200–0.046600) | 0.040700 (0.040400–0.041100) | 1.126× | -11.21% |
