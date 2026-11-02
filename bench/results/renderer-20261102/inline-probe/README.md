# Material helper inlining experiment

This is an isolated generated-C experiment, not a shipped compiler policy.
The production compiler and Render3D package remain unchanged. Both measured
executables were built in this checkout with the ordinary release flags.
`generated.patch` forces only Maps, DrawMaterial and DrawMaterialGeometry inline,
in declarations and definitions. The candidate was compiled/relinked with the
same Ninja-reported commands. The ordinary generated output and executable were
restored afterward.

Whole-process sampling of 600 mixed-shadow frames (`profile.txt`) points at
material helpers, shaded recording and list access. It includes startup,
readback and all threads: percentages are not percentages of view time.
Profiler overhead means its timings are not performance results. An earlier
attempt omitted the executable's .exe suffix and launched no process.

A source-only attempt to bind VisualMesh(mesh).Value to a local did not eliminate
the repeated resolution in generated C; the compiler retained the asset handle.
It was reverted without claiming a timing result.

## Paired measurement

Six processes alternate before/after, after/before, before/after. Each covers
1,000 and 10,000 cubes, unshadowed and mixed shadows, package recording only,
20 warmup and 100 measured frames at 1920x1080. All 24 rows/2,400 GPU samples
complete, with full native package/direct pixel and expanded-command equality.
Draws, submitted vertices, warmed texture uploads and world hashes agree across
all six processes for each case. All six final 10,000/mixed captures have the
same PNG SHA-256, recorded in hashes.json.

Recording medians in ms, all three rounds retained:

| Case | Before | Forced inline | Median paired reduction |
| --- | --- | --- | ---: |
| 1,000 unshadowed | .27675 / .26235 / .26055 | .24945 / .25020 / .26120 | 4.6% |
| 1,000 mixed | .39660 / .38125 / .39090 | .36205 / .38515 / .37265 | 4.7% |
| 10,000 unshadowed | 3.77855 / 2.80780 / 2.84305 | 2.57225 / 2.51800 / 2.48630 | 12.5% |
| 10,000 mixed | 4.17885 / 6.56675 / 4.88345 | 4.10030 / 4.08115 / 4.06485 | 16.8% |

The 10,000 cases improve in every pair, but the mixed baseline varies markedly.
One small-count pair regresses in each case. Inlining changes call boundaries,
optimization opportunities and code layout together; the experiment does not
attribute all savings specifically to argument copying. No GPU/FPS claim follows.

The shared mutex and explicit five-second quiet window cover the cohort. Known
native build/test/benchmark processes are checked before and after quiet time,
then every 250 ms during each process. No competitors were observed. This small
runner does not inspect Node/browser command lines or monitor clocks; desktop
activity, transient competition and clock drift remain uncontrolled. Before and
after clocks are not normalized. Native full-frame readback synchronizes frames.

`run.ps1` is the archived runner used from build/renderer-inline-pairs.ps1;
copy it back there to use its relative paths. It requires the baseline under
build/renderer-before and the patched candidate under build/release, and refuses
an existing evidence directory. Generated C/PDBs/binaries/captures stay under
build/renderer-inline-pairs; hashes.json pins the C, executables, captures and
raw rows. Committed text may normalize CRLF; hashes describe original bytes.

The candidate passes Vulkan benchmark quick correctness. No broad compiler,
web or sanitizer validation was run, since no compiler/runtime change is being
promoted. Before production adoption, isolate which helper(s) matter, bound
code growth and validate a general compiler rule against recursion, debug builds
and unrelated workloads. Hardcoding these three package names is not proposed.
