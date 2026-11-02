# Shared shader binding preparation, 2026-11-02

The compiler reuses completed numeric-list and texture bindings inside a proven
read-only view invocation. Per-instance transforms/material values are still
captured normally. Numeric uniforms are still packed to form an exact key; a
hit skips resource conversion and restores the completed uniforms and counts.

No tree asset, placement, representation threshold, camera, wind, light, shadow
setting or shader calculation changed. This is an internal optimization of the
ordinary ECS/material renderer, without a public batching API or runtime entry point.

Three 1920x1080 final-frame captures have **zero changed RGBA pixels**. All GPU
programs and layouts match exactly. Correctness is established; the first elapsed
timing cohort is too variable to establish a reliable general 100k-tree speedup.

## Scope and correctness

The generated runner allocates two entries for each reachable supported shader
before its per-row scratch mark, then restores its previous private TLS scope.
Exact copied uniform/resource keys and captured texture-slot witnesses decide
reuse. Entries retain no borrowed source storage. Mutable/unsupported views,
scratch sources, pending releases, implicit effects and host callbacks retain
the general path. Managed texture-size callbacks disable reuse before invocation.
The complete contract and exclusions are in [language.md](../../../notes/language.md).

The private fixture counts two list conversions and two texture conversions
across six A/B/A/B/A/B draws, while checking each distinct tint, current source
data, counts and outer-scope restoration. The original fallback fixture exercises sixteen
scenarios, including stable-grid mutation through externs, operators, constants,
defaults, raw/color aliasing and host metadata/text callbacks.

All 2,911 Windows ASan and 2,797 web-release CTests pass. The final narrow
text-measure callback exclusion and expanded fixture additionally pass all three
focused tests on both platforms. [validation.json](validation.json) pins the full
ignored logs and retained suite summaries. Coverage includes Vulkan/OpenGL,
WebGL, ANGLE D3D11 and WebGPU. Other native OSes, mobile, Unity execution and
hardware-web performance were not run locally. No manual CI run was dispatched.

[shader-equivalence-final.json](shader-equivalence-final.json) verifies identical
28 GLSL, 14 WGSL, 14 SPIR-V and 14 shader metadata/layout blocks.
[pixel-equivalence.json](pixel-equivalence.json) records decoded RGBA hashes for
10k canopy, 100k wide and 100k ground. Each finite capture uses two warmup and four
measured frames; only its final frame is compared. PNGs remain under ignored
build/forest-batch/captures-before and captures-after.

## Integration with incoming generated mesh LOD work

Merging dev at 4bb944606 adds a `lod:` draw operand retained outside the ordinary
argument array. The compiler's purity, lane read/write, borrowed-query, worker,
asset/dependency and argument-order walks now visit that operand. Two further
fallback scenarios check a transitive extern that edits the shared grid from
`lod:` and a declared mutable helper evaluated after the transform argument.
The fixture now covers eighteen scenarios.

The merge passes four focused probes on each platform, followed by 61 affected
native ASan and 65 web-release tests. These cover compiler ordering/reads/borrows,
the moved mesh decoder/simplifier, drawing, Render3D and forest smoke scenes.
The ordinary release forest was rebuilt. [validation-merge.json](validation-merge.json)
pins its executable/generated source and all check/build logs.

Three additional finite Vulkan captures of the merged result again match every
baseline pixel and retain identical work/world hashes. The merged shader programs
and metadata also remain byte-identical. See
[pixel-equivalence-postmerge.json](pixel-equivalence-postmerge.json) and
[shader-equivalence-postmerge.json](shader-equivalence-postmerge.json).
These correctness captures used the shared mutex without an idle-CPU requirement;
their timing values are not performance evidence. The timing cohort below remains
pinned to its original pre-merge frozen candidate; it was not rerun after merging.

A later dev update at 70db4a9f5 adds generic functions and skinned/model LOD.
The skinned written-operand path also needs to take the retained `lod:` child
before returning. A regression checks an earlier transform and an earlier
nested helper argument against a later mutating LOD helper. Generic instances
remain ordinary checked functions, so their bodies/effects stay visible to the
preparation proof. All 26 focused native ASan and 26 web-release checks pass.
The final release was rebuilt and frozen under build/forest-batch/final-dev;
three final captures and all shader blocks again match the original baseline.
[validation-finaldev.json](validation-finaldev.json),
[pixel-equivalence-finaldev.json](pixel-equivalence-finaldev.json) and
[shader-equivalence-finaldev.json](shader-equivalence-finaldev.json) pin this
final integration. No new paired timing was performed.

## Initial elapsed-timing cohort

Windows; operator-supplied Ryzen 9 7900X / RTX 4080 SUPER, driver 617.42; Clang
23.1.2, ordinary optimized release (-O2, no sanitizer); Vulkan 1920x1080.
Three alternating pairs per case: 18 processes, 20 warmup and 100 measured frames
each, with 1,800/1,800 completed GPU queries. No profiling flag. Pair orders are
before/after, after/before, before/after.

The shared gate serialized benchmark windows with the parallel GI work and
required five initially quiet seconds. It did not monitor the whole initial
batch. A post-batch snapshot found a surviving cmake/ninja pair started at
00:50:21 UTC, within later process-log spans. This establishes recognized build
process lifetime overlap, without measuring frame-level load or its exact effect.
The initial cohort therefore is **not an isolated performance comparison**.
[initial-cohort-timeline.json](initial-cohort-timeline.json) retains those
observations and output-file timestamps. CPU/GPU clocks and desktop activity
were uncontrolled.

Every result is retained, including the second ground pair's regression.
The elapsed view-recording interval includes descheduling; these are observations,
not isolated CPU-work savings:

| Trees / camera | Pair | Before ms | After ms | Elapsed reduction |
|---|---:|---:|---:|---:|
| 10k / canopy | 1 | 14.60 | 12.95 | 11.3% |
| 10k / canopy | 2 | 14.52 | 12.30 | 15.3% |
| 10k / canopy | 3 | 29.41 | 26.76 | 9.0% |
| 100k / wide | 1 | 99.54 | 72.49 | 27.2% |
| 100k / wide | 2 | 262.72 | 115.54 | 56.0% |
| 100k / wide | 3 | 267.39 | 100.73 | 62.3% |
| 100k / ground | 1 | 93.89 | 75.88 | 19.2% |
| 100k / ground | 2 | 185.84 | 273.71 | -47.3% |
| 100k / ground | 3 | 126.43 | 104.19 | 17.6% |

Canopy has a consistent observed 9.0–15.3% reduction (paired median 11.3%).
The wide median paired reduction of 56.0% and ground median of 17.6% do not
resolve their large spread. They must not be presented as dependable speedups.
The paired median is not the ratio of the medians of the process values.

GPU work is unchanged; GPU/readback times also vary substantially. No GPU
speedup or shipping FPS claim is made. Recording, CPU submission, GPU and
completion/readback intervals overlap and must not be added.
[summary.json](summary.json) retains every process median, p95, range and pair
ratio for those separate intervals.

[measurement-invariants.json](measurement-invariants.json), run-pairs.ps1 and
summarize.py verify original settings, exact planted/representation/visible
counts, world hashes, submitted geometry and draw work, zero warmed texture
uploads and complete frames/queries. Visible trees remain 8,262 / 96,479 / 78,286;
draw calls remain 24 / 6 / 24 respectively. The overview's high camera remains
outside the existing 260m caster budget and has no tree shadow casters. Ground
and canopy retain nearby shadow work. No quality budget was reduced.

## Supplemental process CPU control: not executed

The bounded mechanism check never launched: its gate remained queued for
21.1 minutes behind recognized clang/cmake/ctest/ninja activity. At 01:25:27 UTC,
only this task's verified queued gate was cancelled. No benchmark child or
measured control process started, and no other process was interrupted.
[cpu-control-status.json](cpu-control-status.json) preserves the wait and
contender observations. There are **no supplemental CPU-time measurements**;
neither the counter mechanism nor a 100k speedup is claimed validated. The
prepared scripts/plan are retained for a future quiet window, without replacing
any original elapsed-timing result.

The predeclared protocol in [cpu-control-plan.json](cpu-control-plan.json) uses
the same frozen binaries, settings and frame counts, with three alternating
pairs for each noisy 100k view (12 processes). It records whole-process
total/user/kernel CPU time separately from elapsed time. This includes startup,
world construction/hashing, backend/driver work and teardown; it cannot establish
per-view CPU savings, explain all GPU variation or establish FPS.

run-cpu-control.ps1 uses PowerShell 7/.NET, retains the process handle through
exit, drains stdout/stderr independently and never reacquires the outer mutex.
Each planned child requires five quiet seconds. One-second contender observations
exclude the owned child and preserve any competing processes. Completed adverse
or contended runs are retained without replacement; the next child waits quiet.
Coarse OS frequency snapshots are diagnostic metadata, not a continuous clock
trace. Final status and outputs are recorded beside the plan.

## Identities and reproduction

The baseline is source-equivalent to committed
5d128c4064d52b1de7f069f2dc16ddb79b1f8355, using the preceding verified optimized
binary. SHA-256:
7acb1c1444d79abd2eb8bf952d15b97a35b7b01b8d56b19b27ffdf01c108e724.
[baseline-metadata.json](baseline-metadata.json) pins its executable, generated
sources and source-tree identity.

The final candidate executable SHA-256 is
1d8d95f627e4c118a3bb6949a282012b8fdaa43772581fca56f619ed1a197c28.
[final-candidate-metadata.json](final-candidate-metadata.json) pins both changed
compiler sources and 292 unchanged source identities. It is frozen under
build/forest-batch/after-final. Earlier preliminary candidate metadata/snapshots
are retained and are not used by the measured runs. No executables, PDBs or private
art are committed.

Build source-equivalent before/after programs with the same release toolchain,
freeze the corresponding paths, and verify the saved identities. From the repo
root, with no competing work:

~~~powershell
$env:TIDE_HIDDEN = '1'
& bench/results/perf-pass/gate.ps1 powershell -NoProfile -ExecutionPolicy Bypass -File bench/results/forest-batch-20261102/capture.ps1 -Variant after
python bench/results/forest-batch-20261102/compare-captures.py
& bench/results/perf-pass/gate.ps1 powershell -NoProfile -ExecutionPolicy Bypass -File bench/results/forest-batch-20261102/run-pairs.ps1
python bench/results/forest-batch-20261102/summarize.py
~~~

Select a fresh evidence/output directory before repeating; the scripts refuse
to overwrite completed captures and timing cohorts. Raw original process logs
retain PowerShell's encoding. compare-generated.py verifies the shader blocks
independently. profile.ps1 is an optional instrumented diagnostic, outside the
timing cohort; its initial baseline gate was stopped before any profiling child
started. No new per-view profiler result is claimed.
