# Retargeted animation playback, 2026-10-30

This CPU-only comparison uses the owner's UE4 Kubold idle animation and Synty
POLYGON character from `D:/Projects/bannerlord-duel`: 89 source nodes and 55
target nodes. The live map has 10,004 bytes. Licensed models, imported or baked
animation bytes, packs and embedded pack sources are not redistributed.
Reproduction requires the same private inputs. The agent-written C/Tide timing
harness is included unchanged; input and build identities are recorded in
`identities.json`.

The baseline is `e7b32f297d03a0ae08082d8e49d94437381f1f65`, compiled in this
checkout and saved as `build/retarget-perf/retarget_perf_before_sampler.exe`.
Its executable and harness were preserved; its generated C, library and tidec
executable were not separately frozen. Their identities are therefore not
claimed. The candidate is identified by its measured production-source and
diff hashes, separately from the later documentation commit.

The candidate folds single-key channels with a unique node/path writer into
retained immutable sampling plans, using the same normalization arithmetic;
duplicate writers retain their ordered sampling. Flat scratch output releases
only temporary arrays allocated after it. Generated code also recognizes fresh
default poses and conditional expressions with two fresh branches, and avoids
looking up clip duration for non-looping playback. The retarget solver and
baker algorithms are unchanged. `candidate.patch` contains exactly
`compiler/src/codegen.c`, `compiler/src/skinned_adapters.inc` and
`engine/src/skinned.c` relative to the baseline.

Three public Tide paths perform the same pose construction and hips read:
build-time baked playback, source sampling followed by live retargeting, and
playback of a runtime-baked Animation stored in a local singleton. Runtime
baking runs once before steady-state timing. Each path runs at 1, 100 and
1,000 calls per view. The C controls separately measure baked sampling, raw
sampling, retargeting, combined sampling/retargeting, Auto/Create and the older
offline bake. A prepared C map is stored in an ordinary owned local byte list,
matching a game's chunked map storage.

The C controls warm up with 100 calls per case, then rotate six case orders
over nine rounds. Each reported sample is a batch mean of 3,000 calls, except
Auto/Create's 100. Every C call restores its scratch mark. Tide cases warm up
with three frames per mode/count, then rotate three modes over nine rounds.
Each Tide `RAW` value is a mean of ten frame costs, divided by calls per view;
`RESULT` is the median of those nine means. Scratch lasts for the whole view,
so 1,000 calls include the temporary-memory pressure that a per-call C reset
does not. The process summaries must not be described as pooled frame medians.

`LOAD` measures the first generated frame: map creation, runtime Bake and the
first sampled pose together. Asset loading and the preceding C controls are
outside that interval; this is not an isolated Bake timer or a fully cold
process. The steady-state interval includes generated view dispatch, pose
construction, sampling/retargeting and hips indexing. Drawing, skinning,
palette preparation, IK, gameplay and rendering are excluded; no FPS follows.

The harness checks runtime-call success and compares build-time/runtime-baked
hips-position sums at 0.2 seconds for 1, 100 and 1,000 calls. `FAILURES 0`
does not establish full-pose parity; separate native/WASM correctness tests
cover exact pose bits and ownership. Scratch-byte rows are diagnostics outside
timing. Every raw run remains in `raw/`, including the intermediate candidate's
adverse raw-sampling result. Intermediate runs are not pooled with the final
comparison.

The final native release comparison runs on Windows/Ryzen 9 7900X. Three
adjacent pairs alternate before/after, after/before, before/after, from
18:27:50.244 to 18:27:58.524 UTC. All six exits are zero. All task builds and
tests had completed before timing. There was no CPU quiet guard, detailed
contention monitor or clock monitoring; other desktop activity was uncontrolled.
No process or row from this final batch is excluded. `raw/sampler-final-order.json`
records the actual launch times and order. `summary.json` aggregates all fifteen
cases, keeping each process median, process ranges and paired ratios.

The table shows medians of three process statistics. At 1,000 calls, a
microsecond per pose is numerically a millisecond per batch. The reduction
column is the median of the three paired time reductions, rather than a ratio
of independently aggregated medians.

| Public Tide path | 100 calls, before / after (us per pose) | 1,000 calls, before / after (ms per batch) | Paired reduction at 1,000 |
| --- | ---: | ---: | ---: |
| Build-time baked | 0.688900 / 0.435400 | 1.676230 / 0.868360 | 47.98% |
| Live sample and retarget | 4.552600 / 4.185800 | 7.163840 / 5.268320 | 26.46% |
| Stored runtime Bake | 0.718900 / 0.453100 | 1.699460 / 0.886390 | 47.78% |

At 1,000, before/after process ranges are baked 1.657830-1.676250 /
0.862170-0.872010 ms; live 7.125820-7.553860 / 5.258120-5.280700; stored bake
1.685150-1.710190 / 0.876750-0.893100. The larger live gain is specific to many
calls sharing one view's temporary lifetime: its paired reductions at one and
100 calls are 3.55% and 8.49%. This does not establish an equivalent reduction
in the retarget solver itself or a complete character/game frame.

The C baked-sampling control falls 0.524000 to 0.383867 us per call (26.74%
paired). Raw sampling, retarget-only, combined sampling/retargeting, Auto/Create
and the legacy bake have adverse paired medians of 1.96%, 0.55%, 1.42%, 1.71%
and 1.06%, with overlapping process ranges. No improvement is claimed for those
controls. Raw-sample scratch retained per call falls from 8,624 to 5,136 bytes;
combined sample/retarget falls from 11,200 to 7,712. These diagnostics support
the temporary-lifetime mechanism, not a new retarget arithmetic optimization.

The first generated frame measures 30.4332 to 29.7465 ms by process medians,
with ranges 30.4084-33.3596 and 29.5628-32.2043. These overlap; no cold-load or
isolated Bake speedup is claimed. The earlier intermediate raw-sampling
regression is retained: adding a second caller stopped the compiler inlining
the value sampler; explicitly retaining its hot inline form is part of the
final candidate. The intermediate stack/helper probes did not establish a fix.

Complete native and web correctness suites pass 2,906 and 2,794 enabled tests
in 127.42 and 373.97 seconds respectively, each retaining the existing disabled
MicroDuck test. Web coverage includes 22 WebGPU, 10 D3D11 and 24 safety checks.
Separate sampling/ownership/compiler regressions preserve the existing exact
pose and state checks. These are correctness results, not web performance
measurements. This evidence archive adds no new benchmark executable or tests.

Preliminary evidence is kept separately from that final comparison:

| Raw files | Purpose |
| --- | --- |
| `sampler-before-1.txt` through `sampler-before-3.txt` | Initial preserved-baseline observations |
| `sampler-first.txt` | First candidate, including slower raw sampling |
| `sampler-stack256.txt`, `sampler-cold-helper.txt` | Intermediate stack/helper diagnostics; not final candidates |
| `sampler-control.txt` | Additional run of the preserved baseline |
| `sampler-inline.txt` | Candidate hot-path inline check before final paired timing |

Intermediate executable/source identities were not separately frozen; their
logs document adverse observations and investigation, not a reproducible final
candidate identity. They are retained without selecting only favorable rows.

For reproduction, restore the private input paths named in `harness.tide`,
including the profile JSON files and older `baked.glb`. Copy `harness.c` and
`harness.tide` to `build/retarget-perf/perf.c` and `perf.tide`. Its external
`CMakeLists.txt` is `tide_add_game(retarget_perf SOURCES perf.tide HOST perf.c
NAME perf)`. Build that target in release with strict C17,
`-O2 -fno-fast-math -ffp-contract=off`.
Run the preserved baseline and the candidate with `--frames 90`, under
`TIDE_HIDDEN=1`, retaining stdout and successful exit codes. The original build
directory is `build/retarget-perf`; the two harness files contain no model data.
The final report records actual run order and quiet-period limitations rather
than inferring a benchmark guard from the command alone.
