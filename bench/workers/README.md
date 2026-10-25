# Local voxel worker bursts

Build the `bench_workers` target in release. This CPU-only bounded program
meshes 32³ voxel chunks with a one-cell halo, emits exposed face identifiers,
and checks exact face counts/checksums against a separate flat-grid C kernel.
No renderer, window or audio device is opened.

Options: `--chunks 1..4096`, `--pending 1..4096`, `--simulate`,
`--fallback`, `--profile`, `--quick`. The simulation case runs an independent
integer workload beside meshing and verifies every final element against a
serial oracle. Quick CTests exercise pool, fallback/one-slot and contention.

JSON reports burst time, view/tick median/p95/max, frame count and exact work.
Frames are unpaced polling turns, not a display cadence. Idle polls can
dominate percentiles; maximum view cost is the useful stall observation here.
Backpressure spreads transfers across turns. At an actual display cadence
those turns take additional wall time; these results are not FPS or delivery
latency predictions. The contention case ticks once per turn, so different
turn counts mean different total foreground work. Compare its per-tick costs
and exact per-tick oracle, not its burst duration as equal total work.
The C timing is a flat, serial kernel reference. It omits grid lookup,
allocation, captures, publication, scheduling and simulation and therefore
is not an equivalent asynchronous implementation or an end-to-end speed ratio.
No wall-clock threshold is a test assertion. Runs stop after 120 seconds or
100,000 frames if work fails to complete.

`../results/worker-controls/run.ps1` alternates three baseline/candidate pairs
for 64 chunks, 64 beside simulation, 16 in synchronous fallback and 256 chunks.
It takes the existing shared mutex and waits for five seconds without heavy
builds/tests. The baseline is `807e34b2` with these exact workload files;
its driver omits only the new worker limit field and worker profile rows.
Use the same Clang RelWithDebInfo configuration for both. Binary hashes,
machine metadata and raw rows are saved with the result. Keep adverse results;
do not interpret loaded or incomplete batches as clean measurements.

Raw `pending_limit: 0` records the requested default: unbounded in the baseline,
64 private jobs in the candidate. Initial rows used a lower-neighbor p95
estimate; final rows use nearest rank. Burst durations and maxima in both
batches are unaffected by that reporting correction.
