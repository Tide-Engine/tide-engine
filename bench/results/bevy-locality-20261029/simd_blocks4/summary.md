Verified 54 processes in 3 alternating rounds.

ECS ticks only; excludes setup, snapshots, per-tick hashing, networking and rendering.

Times are milliseconds per tick, median of process medians. Ranges are process-median min/max.
Bevy/Tide is the median paired ratio; values above 1 favor Tide.

| Case | Threads | Tide ms (range) | Bevy ms (range) | C ms | Bevy/Tide |
| --- | ---: | ---: | ---: | ---: | ---: |
| particles-1m-1 | 1 | 0.445150 (0.440650–0.467900) | 1.141000 (1.139550–1.141100) | 0.406400 | 2.563× |
| wide-1m-1 | 1 | 1.188400 (1.178700–1.189200) | 2.326900 (2.310000–2.482050) | 0.908850 | 1.974× |
| filtered-frozen90-1m-1 | 1 | 1.220900 (1.218600–1.228800) | 0.551100 (0.544300–0.561400) | 1.034900 | 0.451× |
| filtered-frozen95-1m-1 | 1 | 1.097200 (1.068050–1.115950) | 0.212700 (0.206400–0.217250) | 1.032700 | 0.193× |
| filtered-frozen99-1m-1 | 1 | 0.133300 (0.133000–0.134400) | 0.048000 (0.047600–0.048900) | 1.037200 | 0.360× |
| filtered-frozen99-1m-4 | 4 | 0.050400 (0.050100–0.054650) | 0.030800 (0.029500–0.033100) | 0.577200 | 0.606× |

Adjacent before/current comparison; Before/Tide above 1 favors the current Tide.
Change is the median paired percentage change in time (negative is faster).

| Case | Threads | Before ms (range) | Current ms (range) | Before/Tide | Change |
| --- | ---: | ---: | ---: | ---: | ---: |
| particles-1m-1 | 1 | 0.450700 (0.447000–0.474000) | 0.445150 (0.440650–0.467900) | 1.013× | -1.29% |
| wide-1m-1 | 1 | 1.185900 (1.185650–1.204950) | 1.188400 (1.178700–1.189200) | 1.006× | -0.61% |
| filtered-frozen90-1m-1 | 1 | 1.327150 (1.306000–1.339100) | 1.220900 (1.218600–1.228800) | 1.089× | -8.18% |
| filtered-frozen95-1m-1 | 1 | 0.883600 (0.871450–0.907100) | 1.097200 (1.068050–1.115950) | 0.813× | +23.02% |
| filtered-frozen99-1m-1 | 1 | 0.097000 (0.094850–0.097800) | 0.133300 (0.133000–0.134400) | 0.728× | +37.42% |
| filtered-frozen99-1m-4 | 4 | 0.041100 (0.040100–0.041250) | 0.050400 (0.050100–0.054650) | 0.818× | +22.18% |
