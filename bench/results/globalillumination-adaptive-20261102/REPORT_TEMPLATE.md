# Adaptive GI scene scaling — report draft

Measurements are pending. This template becomes a result only after all 27
short processes (and three long processes when requested) complete and
summarize-scaling.py validates their full rows.

## Identity and validation

- Source commit: `<metadata.json source_sha>`.
- Executable SHA256: `<metadata.json binary_sha256>`.
- Compiler/build commands: `metadata.json`, `CMakeCache.txt`, `build-commands.txt`.
- Hardware/driver/OS: `<fill from metadata.json and captured renderer strings>`.
- Correctness checks: `<record actual native ASan and web target results>`.
- Unavailable coverage: `<record remaining hardware/platform/device-loss limits>`.

No old-grid before/after result is claimed. Pre-fix exploratory district rows
that actually contained seven meshes are excluded from scaling evidence.

## Work and method

The same primary room, camera, exposure and initial light are retained while
adding separated buildings. All short runs explicitly select district with
1/64/256 buildings: 7/448/1792 meshes. The enabled benchmark verifies 7*N observed
instances; GI-off disables package bookkeeping and reports gi_scene_instances=0.
One shadowed primary point light and one emitter per building are retained.

Cases are GI off, static update4 and moving-light update4. All use 960x720,
100 measured frames, 20 warmup frames and independent cold initialization.
GI uses requested base spacing 1.5, capacity 512, view/capture distances 48 and
refresh age 600. Actual allocation and capture work are reported separately.
Requested spacing is not a guarantee at the primary receiver.

Optional `-LongStatic` appends three independent 256-building static update4
processes with 1200 measured frames and 20 warmup frames. Their window reaches
aged background refresh, unlike the settled 100-frame window before eligibility.
They require all 1200 GPU samples and are never pooled with short-window rows.

Three trials run adjacent processes in forward/reverse/forward count/case order
using the shared 5-second quiet gate and cooperating benchmark mutex. No builds
or other GPU tests may overlap. The gate does not continuously monitor system
contention, and other desktop activity, clocks and temperature remain uncontrolled.
Per-process GPU-state snapshots are observations, not clock normalization.

All raw logs, JSONL and run metadata are retained. Failed/interrupted attempts
must remain documented; this runner stops instead of retrying or dropping them.
Source HEAD/tracked cleanliness and executable hashes are checked around runs.
Each process requires its full GPU sample count and a successful correctness finish.

## Results

`summary.json` contains nine short groups, each based on three process medians.
Its separate `long_static_groups` has one group when requested. Min/max are
ranges of process medians; p95 summaries are medians of process p95 values,
not pooled frame percentiles. `chart-data.csv` supplies short chart values/ranges;
`long-static-chart-data.csv` holds the separate background-window result.

`<Insert chart of whole-frame GPU ms versus building count, one trace per case.>`

`<Insert chart of CPU recording ms versus building count, one trace per case.>`

`<Report observed probe counts, primary spacing and actual captures/candidates.>`

`<Report 1200-frame capture mean, CPU/GPU median and p95, with three-process ranges.>`

Short static update4 measures a settled cache when actual capture count is zero.
The optional long static case measures idle and background refresh together;
its mean capture work and p95 expose work hidden by an idle-frame median.
Moving-light includes changed direct-light/shadow work as well as GI refresh;
there is no matching moving-light GI-off case, so its difference from static off
does not isolate GI overhead. Static GPU deltas are derived differences of
corresponding process medians, not directly timed GI-only scopes.

CPU recording, CPU submission and GPU intervals overlap and must not be added.
Native completion/readback synchronizes frames; these figures are not pipelined
game FPS. Cold setup/fill recording is reported separately from warmed medians.
It excludes asset loading and does not time GPU convergence end to end.

## Visual evidence and limits

`<Link actual primary-room/probe/overview/travel captures, with scene options.>`

Overview deliberately expands viewDistance to cover its elevated whole-district
camera and is a different workload. Travel changes residency and is also separate
from these fixed-camera timings. Document remaining artifacts and finite-range,
thin-geometry, dynamic-bound and mixed-resolution limits without extrapolating
these measurements to all games or hardware.

## Reproduction

Build and validate the final source first. Commit it before measurement, then run:

```powershell
$env:TIDE_HIDDEN = '1'
pwsh -NoProfile -File build/gi-adaptive/run-scaling.ps1 `
    -SourceSha <full-40-character-source-SHA> `
    -ReportDirectory bench/results/globalillumination-adaptive-20261102 `
    -LongStatic
```

The report directory must be new or empty. No automatic builds, test dispatches,
publishing or retries occur. Scripts and fingerprints are copied into the report.
