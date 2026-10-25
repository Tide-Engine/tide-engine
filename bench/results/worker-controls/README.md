# Worker controls: paired release observations

The runner uses the existing shared benchmark mutex and requires five seconds
without cmake/ninja/ctest/tidec or benchmark activity. All rows verify exact
meshing work. Hardware: Ryzen 9 7900X, 12 physical cores, Windows, Clang 23.1.2
RelWithDebInfo. Binary identities are in each raw file; the final source hashes
are in final-manifest.json.

Baseline: 807e34b2, using the exact benchmark mesher/driver with only new host
limit/profile API fields omitted. The initial candidate is the task/worker
gap implementation at 65226b00. The final candidate includes dev integration
and reduced redundant worker statement checks; loop/body/helper checks remain.

These are unpaced polling turns, not display frames or FPS measurements.
Empty polls dominate some distributions. The per-turn maximum is more useful
than a median for copy stalls. The contention case does a tick per turn, so
its total foreground work varies with turn count: its burst duration is not
an equal-total-work throughput comparison. The flat C kernel excludes owned
grids, captures, allocation, scheduling, transfers and simulation.

## Initial batch

initial.jsonl retains all 24 paired rows, including adverse results. Three
alternating-order pairs per workload; medians of each run's observation:

| Workload | Burst baseline/candidate ms | Worst view turn baseline/candidate ms |
|---|---:|---:|
| 64 chunks | 11.13 / 12.49 | 7.73 / 2.94 |
| 256 chunks | 59.99 / 54.90 | 37.27 / 4.65 |
| 16 chunks, no provider | 17.66 / 20.55 | 17.54 / 17.45 |
| 64 chunks beside simulation | 13.20 / 15.96 | 7.97 / 3.82 |

The initial 64-chunk and fallback completion times regress about 12% and 16%;
the larger burst has much smaller view stalls. Contention's median per-run
worst tick is 0.81 / 1.02 ms. These observations motivated removing redundant
per-statement checks from pure workers. Threadless hosts still execute the
calculation synchronously and cannot promise a short compute turn.

The initial p95 field uses a lower-neighbor estimate; final reporting uses
nearest rank. Duration/maxima and actual workload are unchanged by that fix.
Requested pending_limit 0 means the baseline's unbounded admission and the
candidate's default 64 private jobs. Soft transfer budgets spread copying
across turns; at a real display cadence this also delays burst delivery.

## Final batch status

The final paired binaries and source manifest are frozen. One ten-minute guard
wait expired while other checkouts still ran CTests, without producing samples;
the same guard is being retried. No post-adjustment speed claim is made yet.
