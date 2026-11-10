# `tide`, packages and the package

## Stable-version package fixture (2026-11-07)

Release run 42378464688 failed `tide_cli_packages` on Linux and MinGW:
the fixture pinned 0.6.0 and expected a missing installation, but the invoking
stable compiler was itself 0.6.0 and correctly continued. The fixture now uses
0.6.1 when the invoking version equals 0.6.0, retaining the missing-installation
diagnostic and subsequent matching-version success checks. Product behavior,
pin syntax and version selection are unchanged.

Validation: built this checkout's Windows Clang 23.1.2 ASan CLI with
`TIDE_VERSION=0.6.0`; `tide_cli_packages` passes (1/1, 5.51 s).
Platform and external games were disabled for this host-tool-only build.
Linux/macOS/MinGW execution remains CI coverage; no web runtime changed.

## Hide stages shorter than half a second (2026-11-06)

Owner: show progress only if a stage lasts at least 0.5 seconds. The terminal
renderer now skips both active and retained rows while that stage's elapsed
time is below 500 ms. The existing activity thread reveals a longer stage on
its next frame; its timer still measures from the stage's actual start. A stage
that crosses the threshold between frames still gets its completed row.
Diagnostic replay, redirected output and reports are unchanged, including errors
from a stage that finishes before the threshold. No added waits or build delays.

Validation: Windows Clang 23.1.2 ASan CLI build and help, build_overlap,
build_report pass (3/3); production docs build passes. Scratch ASan feedback
probe checks 499 ms stays hidden,
500 ms reveals progress, fast stage transitions leave no rows, slow stages
retain completion even without an active frame, and failed stages retain their
actual count. No game or browser was launched; real-terminal appearance remains
manual coverage. No generated-C, scheduling or runtime changes.

## One-line active build progress (2026-11-06)

Owner: put the active label beside its bar, truncating text that would overlap.
Active build feedback now uses line_draw with the same 48-character label column
as retained rows. Labels are padded or truncated to that width for both active
and retained rows, so long filenames cannot shift or overwrite the bar. Existing
line redraw clears the tail when text gets shorter. Stage timing/counts and
compiler scheduling are unchanged; the two-row line helper remains available
to its existing tests but is no longer used by builds.

Validation: Windows Clang 23.1.2 ASan CLI build and help, build_overlap,
build_report pass (3/3). Scratch ASan feedback probe verifies active labels
and bars share one line, short and long labels put the bar in column 50,
and long filenames truncate before it. Production docs build passes.
Real-terminal appearance remains
manual coverage; no generated-C or runtime changes.

## C/C++ completion labels (2026-11-06)

Owner: show completed C files as they finish, rather than listing active jobs and
removing their names. run_compiles now updates its terminal label immediately
on each successful completion with that file's basename and the new count.
It keeps the last completion visible until the next; batch start has no filename.
Failed compiles do not advance the count or claim completion. The C/C++ stage
still retains one summary row. Redirected completion lines, scheduling, diagnostic
ordering and reports are unchanged. The active-jobs scan and comma-separated
label are removed.

Validation: Windows Clang 23.1.2 ASan CLI build and help, build_overlap,
build_report pass (3/3); production docs build passes. A scratch ASan probe checks the initial filename-free
label and the completed basename/count, alongside retained-row alignment and
success/failure behavior. No game or browser was launched; real-terminal
appearance remains manual coverage. No generated or web runtime changes.

## Aligned bars and asset build preparation (2026-11-06)

Owner's terminal capture showed unaligned completed bars and repeated asset-build
C-generation rows. Build summaries and the active bar now use the same 48-character
label column. Nested generation/packing notifications stay within Preparing asset
build program; compiling and running have separate rows. Failed rows keep the
stage label in that column, with diagnostics providing failure detail.

Inspection: build_code_run calls run_program once on a results-cache miss, then
rechecks sources to apply its results. run_program calls codegen once, compiles
its generated parts once, links once, and runs once. The repeated labels were
callbacks within codegen, including pack creation before dependency fingerprints
and the reduced build-input pack. No compiler caching or generated-C behavior was
changed here; existing root-result and engine-object caches remain effective.

Validation: Windows Clang 23.1.2 ASan CLI build, help, build_overlap and
build_report pass. build_report initially exhausted ASan allocation while the
docs build ran; it passes alone after that build completes. Production docs
build passes. Scratch ASan feedback probe checks every retained bar starts at
column 50, nested packing/generation adds no preparation rows, and failure and
success retain correct percentages. Real terminal appearance remains manual
coverage; compiler/runtime output and web execution are unchanged.

## Retained build progress stages (2026-11-06)

Owner: `tide build`/`run` erased finished bars and repeated "Generating C"
without explaining the work. Terminal feedback now retains each completed stage
as a summary line with a full bar and that stage's elapsed time. Active labels
and filenames still use the two-row display. Stage transitions and drawing share
the feedback lock, so the activity thread cannot redraw over a retained row.
Checking and asset imports share a stage because they interleave per declaration;
packing notifications share another regardless of singular/plural wording.
C generation distinguishes the asset build program, game C, and asset data/final
C generation. These are CLI labels; compiler callbacks and reports are unchanged.
Parsing and C/C++ retain their item percentages. The other compiler stages use
activity bars: their supplied count covers imports plus packing, not an individual
stage. Success completes the last row; failure retains its observed progress.
Package resolution failure does not advance to a completed package stage.

Validation: Windows Clang 23.1.2 ASan CLI build and four focused CTests pass
(unit, help, build_overlap, build_report). A scratch ASan probe of the actual
feedback functions with controlled clock/line output checks retained rows,
per-stage timing, filename updates without extra rows, all three C-generation
contexts, failure at 33%, and successful final completion. No game was launched.
The docs CLI reference and production docs build pass (usual unstaged example-page
404s). Real-terminal appearance and other OS terminals
remain manual/CI coverage; no game/runtime or WASM code changed.

## Faster edit builds of big games (2026-11-06)

Owner: time `tide run --release` of Mount & Rifle and make iterations faster. Measured with this checkout's
packaged tide (embedded clang/lld 23.1.2) on a scratch copy of the game (unpinned), Ryzen 9 7900X, `tide build
--release --report` after a one-constant edit in camera.tide; medians of three on a quiet machine (another worktree's
suite made single runs 2-3x slower, those were discarded):

| | before | after |
|---|---|---|
| edit, whole build | 27.2 s | 13-14.5 s |
| generated C compile (the edited part) | 5.1 s | 1.0 s |
| ThinLTO link | 12.5 s (incl. 0.6 s pack copy) | 4.5 s |
| splitting/writing generated C | 2.3 s | ~1.6 s |
| shader arrays' text | 1.1 s | 0.37 s |
| unchanged rebuild | 13.4 s | 7.4 s |
| `tide run --release --frames 2` after an edit | ~30 s | 16.7 s |

Truly cold (no .tide/): 126 s, then 98 s with the parts above, then 69 s with build code's program split and
without shaders (assets-and-files.md, Build code), then 47 s with images and levels of detail made ahead on every
processor (same file, "Made ahead"), then ~43 s with images decoded ahead of the asset loop (same file), then ~36 s on a quiet machine with build
code's compiles overlapping its generation on every processor (same file, Build code). Of the 69 s: asset imports 11.5 s, build code 39 s (of which
~24 s making levels of detail one mesh at a time, cached after), C 9 s. A full recompile of the generated C
(cached data part) takes ~19 s. Starting the game adds ~3 s (its 742 MB pack).

What changed:
- **The data part.** 93 of the game's 106 MB of generated C were shaders' SPIR-V/WGSL arrays. `write_code_parts`
  first splits with `SPLIT_DATA` (plumbing_split.inc): constant variables larger than `SPLIT_SHARED_CONST`, whoever
  names them (`split_name.data`), go to `<name>_data.c`, everything else stays to be partitioned. Each binary level
  of the old split re-scanned and re-wrote all 106 MB (~0.9 s per level); the code's levels now cut ~14 MB. An edit
  of the code leaves the data part's text, and so its object, as they were. It is compiled with ThinLTO like the
  other parts (compiling it without LTO made no link-time difference, and LTO keeps the constants visible to the
  optimizer as before). It is always written when splitting (possibly only declarations), so CMake can name it.
- **More code parts.** `codegen_options.split_code_parts` (most parts, a power of two up to 16; tidec's default 4)
  and `split_code_part_bytes` (fewer parts when each would have less code; 0 = always the most). The CLI asks for up
  to 16 of at least 512 KiB: Mount & Rifle gets 16. Swept on the old split: 4 parts 27.2 s, 8 parts 19.4 s, 16 parts
  17.1 s, 32 parts ~19 s (link no faster, splitting slower); with the data part: 8 parts 16.8 s, 16 parts 14.2 s.
- **Shader arrays without a printf per element.** `gen_shader_text`/`gen_shader_spirv` format lines directly; the
  text is byte for byte what `"%s%d,"`/`"%s0x%08x,"` wrote (checked on Mount & Rifle's 92 MB data part). Translating
  WGSL to SPIR-V itself is 0.28 s of the remainder.
- **The pack is left alone when it comes out the same.** tidec's `write_pack` compares the existing file first
  (`pack_file_is`, reading is cheaper than writing 742 MB); tide's `place_pack` keeps `<gen>/game.pack.placed`
  (destination and both files' stamps after the last copy) and copies again only when either file changed.

Findings that limit the rest: a ThinLTO link where every module is cached takes 0.24 s, but any edit re-runs the
backend of every generated module (all 16, ~4.5 s wall in parallel), even with `-import-instr-limit=0`: the cache
keys cover more than imports. Each part's cost is its code (frontend 1 s, optimizer 4.7 s, codegen 5.2 s for a
7 MB-of-code part without LTO); shader data and `-g` changed the backend time by under 1 s. What remains of an
edit build is mostly the Tide side (declarations ~1.1 s, generating C ~2.1 s, packing 1 s in memory, split/write
1.6 s). Not done: caching SPIR-V across builds, a single N-way split pass, skipping the in-memory pack build.

Tests: `tidec_split_code_stable` now expects `game_data.c` in the manifest and checks the compute fixture's shader
arrays are defined there alone; `tide_cli_build_report`'s large game reads its part list from the manifest
(a power of two of code parts, then the data part).

Validation (Windows, Clang 23.1.2, Node 24.22.1): ASan builds; tidec_split_code_stable, the five SPLIT_CODE e2e
fixtures (crossplatform pins, nested, tasks, compute, nested_split_debug), tidec_e2e_shaders/compute,
tidec_recompile, tide_cli_build_report/build_overlap/help pass (12/12). A locally built embedded-LLVM package
(native half only) passes `cache_test.mjs --part cache --gpu` and `--part modes --gpu`. Mount & Rifle builds and
runs two hidden frames in release and debug (16 plumbing parts and `game_plumbing_data.c`). Docs production build
passes. Not run: web builds of the split fixtures, Linux/macOS, Android (CI).

## One count for the Tide step (2026-11-06)

Owner: the bar for assets and Tide compiling went back and forth per file. Each notification carried its own
stage's count (a file's import 0/1 then 1/1, a glob's i/n, checking indeterminate, packing i/n), so the bar reset
with every label. Now `compile_progress_whole`/`_reach` (common.h) give one count after parsing: check.c's asset loop
sets it to twice the asset declarations and moves on as each is imported; build_pack takes the second half, as many as
were declared, whatever a release leaves out (it ends at the total). While it's set, every notification carries it,
so labels still name the file or asset while the bar only goes forward, and holds through checking and generating
C. Parsing keeps its files' count (it ends before); a game without assets keeps the old stage counts.
`compile_inputs` clears it before and after; build code's own codegen doesn't move it.
Follow-up (same day, the owner: "did we ever rework the progress bars to make them clearer?"): build code said
nothing for its seconds, so the label stayed on the last asset; it now says "Running build code: making its
program", "...: compiling its program", then "Running build code" while it runs. The game compiled again after
build code calls compile_progress_whole with the same total, which now keeps its place rather than starting the bar
over. The terminal shows the Tide step's percentage without its count (`feedback.percent_only`: assets imported plus
packed is no number to read); C/C++ keeps its files' count. Test: `compile_progress_whole_keeps_its_place_when_counted_again`.
Not checked in a real terminal this time (the receiver's text only changed).
Then (owner: "make capitalization consistent and rename linking to the optimization step"): `build_step_shown`
puts a step's label in the terminal with a capital first, as the stages' labels are, and a ThinLTO link (and an
Android one with generated_lto) shows "Optimizing across files and linking". The build report's stage names and
redirected `tide: <step>` lines keep their lowercase words ("linking"), which build_report_test and docs/tools/cli.md
name. CLI build report, overlap, help and run-exit tests pass under ASan. `tidec_recompile` checks that
after parsing every count is the whole one (4 for two assets) and never goes back, reaching 2 before generating C
and 4 after packing.

## Exact pins suppress upgrade notices (2026-11-05)

Exact project pin selection sets TIDE_NO_UPDATE_CHECK in the command's process
environment before continuing or dispatching. This suppresses automatic upgrade
notices both when the invoking release matches and when an older cached release
is downloaded/launched, including older compilers that already honor the opt-out.
Moving channels retain their update behavior; explicit upgrade is unchanged.
The setting does not change the owner's shell environment.

Validation: Windows Clang 23.1.2 ASan CLI/dispatcher/child builds and
tide_cli_versions pass (Node 24.22.1). The fixture starts without the opt-out and
checks matching-release suppression, inheritance after download and an unchanged
nightly channel. Native host tooling only; no WASM/runtime change. Linux/macOS
execution remains CI coverage. CLI reference updated.

## Parallel generated C with ThinLTO (2026-11-05)

Generated C groups at least 4 MiB now compile in four stable parts with `-flto=thin`. Small files stay whole.
Ordinary debug keeps game code at O0 in one file, splitting only sufficiently large O2 plumbing; release and explicit
optimization modes split the complete generated file. `generate` requests a part manifest; `queue_game` queues exactly
those objects, including native reloads, web, Unity, servers and Android. Old extra files are ignored. Existing
`game.o`/`game_plumbing.o` names remain; extra parts use `_part1.o` etc. Source/flags/header stamps include ThinLTO;
only the main part keeps simulation-identity header invalidation. Shared type/header changes still invalidate every
part. ThinLTO parts participate in size-prioritized scheduling; ordinary launchers retain their existing priority. Counts/progress are completed translation units,
not optimization-pass percentages. Linking retains activity feedback.

Native/web/Android link paths share ThinLTO flags, use `<configuration-cache>/thinlto`, and cap backend jobs at the
same TIDE_JOBS budget as C compilation. Mach-O uses its cache/thread options; ELF/MinGW/WebAssembly use their lld
options. A debug link uses O2 for its optimized plumbing's LTO backend; game code is a separate O0 native object.
Explicit optimization overrides remain effective. The managed reactor's identity-bearing launcher retains its
existing stronger header invalidation. Neither engine/package objects nor the game's handwritten C are switched to LTO.

Measurements (Windows, clang/lld 23.1.2, Ryzen 9 7900X; exploratory single passes, ordinary desktop load): Mount &
Rifle's 102,183,928-byte C took 49.47 s to compile as one file, 29.84 s with the prior two-file split, 19.39 s in four
parts, and 6.71 s in four ThinLTO parts. Successful DLL links retaining the game API took 0.32 / 0.45 / 0.56 / 10.24 s
respectively: about 49.8 to 17.0 s for that C-and-link work. These are MSVC-ABI experiments, not timings of the
packaged MinGW CLI or total game builds; asset import/Tide generation and engine/package compilation are excluded.
Separating shader arrays alone took 51.20 s and was rejected. The initial full-file time trace put 23.3 s in LLVM
optimization and 28.7 s in backend code generation, against 5.9 s in the frontend. Raw data and identities:
`bench/results/generated-c-partitions-2026-11-05.json`.

Runtime sanity check: three alternating release particles runs, 1M particles, one thread, 300 timed ticks after 20
warm-up ticks. Medians of process tick medians are 1.093 ms whole and 1.101 ms four-part ThinLTO (within 1%);
every run and its C comparison ends at `0x927c09795885dac2`. One split run was 1.306 ms and desktop load was not
guarded, so no general runtime-speed claim is made. All five Sumo variants (whole, two/four parts, with/without
ThinLTO) pass their 18 tests. Raw runtime output: `bench/results/generated-c-runtime-2026-11-05.json`.

Validation: complete Windows ASan (3,184 tests) and web-debug (3,008 tests) builds and suites, each initially
failing only the incoming sampler-index assertion in Render3D post tests; its corrected test passes on both. The large CLI report regression checks actual automatic four-part
selection, ThinLTO/cache/job flags, zero warm recompiles and at most the body/identity parts recompiling after a
body edit. Focused CLI unit/help/overlap/report tests pass. Docs production build passes (the usual example-page
404s). Linux/macOS/MinGW package and Android runtime coverage remain CI's; no emulator was run. No runtime or wire
contract changes. The compiler identity changes normally because compiler sources changed.

## Windows watched-run shutdown (2026-11-05)

Closing a game's window could leave `tide run` blocked during CRT exit: its
detached terminal reader held stdin's FILE lock in `fgets`. The Windows reader
now uses `ReadFile` on the CRT stdin descriptor's OS handle, so it holds no
stdio lock while waiting. This also respects redirected stdin. Restart-line
handoff, CR/newline trimming, EOF and final partial lines remain in place;
Windows now chunks raw bytes like POSIX (255 bytes, including CR in CRLF).
No host/runtime boundary or watch policy changed.

`tide_cli_terminal_exit` keeps an input pipe open after sending a restart line
and returns while the reader waits again; CTest bounds shutdown to 10 seconds.
The old Windows implementation times out in the same probe; the fixed ASan
executable exits in 0.16 seconds. Windows Clang 23.1.2 ASan CLI build and six
focused CTests pass: unit, run_exit, terminal_exit, help, hidden and build_overlap
(Node 24.22.1). The overlap fixture first lacked its compiler helper; after
building that target, it passes. No visible game was launched. Console close
interaction and Linux/macOS remain manual/CI coverage; no WASM code changed.

## Tide compilation progress labels (2026-11-05)

The terminal build display now receives synchronous compiler progress through
`compile_set_progress`/`compile_progress` in common.h. The CLI installs the receiver
only around source generation and copies labels under its existing feedback lock.
Parsing and method checking name the Tide source; imports name each resolved file
(including individual glob matches); packing names the qualified asset declaration.
Generating C has its own label. The terminal uses basenames so absolute directory
prefixes cannot hide the filename. Redirected output and build-report stages stay
unchanged. This is host-tool feedback only; no generated code/runtime contract changes.

Validation: Windows Clang 23.1.2 ASan builds of tide, tidec and CLI tests;
CLI unit/help and recompile CTests pass. The recompile regression checks actual
parsing, checking, file import, asset packing and C-generation notifications.
Docs production build passes with the usual unstaged example-page 404s.
Other OS terminals remain CI/manual coverage; no web runtime code changed.


Progress counts follow-up (2026-11-05): the compiler callback now includes
completed/total item counts (zero total means indeterminate). Parsing counts input
files, importing counts each resolved set of files (glob matches included), and
packing counts asset declarations. The existing terminal bar shows those stage
counts and percentages; checking and C generation retain activity animation.
Counts describe work items, not time estimates or whole-build completion.
Redirected output is unchanged. ASan CLI unit/help and compiler recompile checks
pass, including notification count bounds and parsing/import completion. Docs
production build passes; other host terminals remain CI/manual coverage.

## Optimized plumbing in debug builds (2026-11-04)

Asked for by the owner: a plain debug `tide run` of a large game (Mount & Rifle) was unplayable, while
`tide run -O2` was not, but `-O2` also optimizes the game's code, which a debugger then steps badly. Debug builds
(no `--release`, `--distribution` or `-O<n>`: `splits_plumbing` in `build.c`) now compile with the engine's
`-O2 -g`:

- **Package C/C++** (`queue_c_files` for a package: stamp and flags from `stamp_of(b, true)` and
  `queue_compile_as(..., optimized)`). The game's own C stays `-O0 -g`. Packages are library code, as the
  engine is.
- **The generated plumbing.** tidec's `codegen_options.split_plumbing` (`tidec --split-plumbing`) writes
  `game_plumbing.c` beside `game.c` (`compiler/src/plumbing_split.inc`, run on the finished text before `#line`
  pinning). `game.c` keeps the functions whose lines are `.tide` lines (systems, views, handlers, functions,
  methods, parallel steps, sort comparators, async bodies) and compiles as before; everything else (world copy,
  hashing, packing and deltas, client state and projections, rows and tasks that call the systems, dispatch,
  assets, shaders' data, layout, the game API) goes to the plumbing file, compiled into `game_plumbing.o` beside
  `game.o` (`queue_game`, in the game C's objects, so every link takes it: build, run/reload, Unity, Android,
  web, servers). Release, distribution and explicit levels still write one file.

How it cuts (comments at the top of the .inc): top-level items, by a scanner that follows strings, comments,
directives and braces. A function goes to one file. Types, prototypes, directives and macros go to both. A
variable is the plumbing's, except a small static const both name (in both), a static only the code names, and
the definitions codegen lists in `gen.code_at`: `tide_game_api` (it holds the simulation identity) and the
profile's `tide_system_sources`/`tide_view_sources` (`.tide` lines), so an edit of the code alone leaves
`game_plumbing.c` byte for byte, and its stamp (which leaves `TIDE_SIMULATION_ID` out of the header's hash, as the
launcher's does) keeps the `-O2` object. Top-level `.tide` `#line`s go to `game.c` only, for the same reason.
Linkage: a name one file uses and the other defines becomes external with hidden visibility (`TIDE_SPLIT`;
`TIDE_SPLIT_HELPER` keeps `TIDE_HELPER`'s `nodebug`), declared in the other file where the definition was; who
uses what is a fixed point over function bodies, initializers and macro bodies. A static of the plumbing's that
becomes external is renamed `tide_split_<name>` in both files: `tide_game_pack` (world packing) is also
`--pack-c`'s data, and `tide_embed_command/schema/view` the platform's. The game's code keeps its names, and so
does a name that is also a field's anywhere (after `.`/`->`: `tide_request_watch`, `tide_born`), as renaming would
reach the struct declaring it; a field mention isn't a use either. What
it can't cut (none of this build's games) stays whole in `game.c`, and `game_plumbing.c` says why in a comment.
Output is sized by a counting pass and `#line` pinning streams to the file (`write_pinned`): the arena never
frees, and Mount & Rifle's 96 MB of generated C ran tide out of memory otherwise.

Measured (Windows, clang 23.1.2, this checkout's installed toolchain against the same scratch copy of Mount &
Rifle at 6e963ed, unpinned; `tools/performance-route.txt`, seed 42, 3230 frames, 1280x720, `--profile`; the
machine ran up to three other Mount & Rifle sessions, so interleaved runs, medians):

| debug, ms/frame | before (5 runs) | after (4 runs) |
|---|---|---|
| match (`cpu_phases.match`) | 23.1 (p50 19.4) | 17.7 (p50 14.3) |
| match minus systems (hash, pack, load, copy) | 15.3 | 9.9 |
| views | 10.9 (p50 10.5) | 10.8 (p50 9.5) |

Single runs vary up to 2x with the other sessions' load. In 700-frame load-phase runs (2 each) match went
29.5/30.4 to 24.2/28.1. Sumo (`examples/sumo`, 1800 frames, seed 42, 3 runs each): match 7.1 to 1.7 ms, views
2.9 to 0.9 ms (its Box3D package C is most of it). One later split run of the route ended with the engine's "out
of memory" while three other Mount & Rifle instances were running; its other runs and the base runs before and
after it completed. Cold `tide build` of Mount & Rifle with a debug-built tide: 5:00, the `-O2` plumbing (86 MB,
mostly shader data; `game.c` 14.7 MB) compiling in about a minute alongside the engine.

Tests: `tidec_debugger_natvis` on the split fixture (`SPLIT_PLUMBING`, notes/tide-programs.md); every `.sources`
list of the debug build (463) generated with `--split-plumbing --visualizers --layout`: 443 split, both halves
compile with `-Wall -Wextra -Wpedantic -Werror` (MinGW target), no symbol defined by both, nothing undefined
that the whole file didn't need but the engine's own functions (helpers made external are emitted); the unsplit
output equals the previous tidec's but for the identity (it hashes tidec's sources). `build_code` fails without
`--build-cc`, as before. `cache_test.mjs` (native) expects `game_plumbing.o` in debug, a layout change to rebuild
it, a body edit not to, and its reuse after a failed build. CLI unit/help/run-exit tests pass. Not run here: web
and Android builds (CI's package checks run `cache_test.mjs --web`), LLDB (CI's Linux jobs), macOS/Linux.
## Build reports (2026-11-04)

Owner: "build reports; we should be able to have some output documenting where
time was spent building, where pack sizes come from and how big they are, what
was stripped maybe". Built as `tide build --report <file>`; the flag, format and
contents are my picks (Provisional). `.json` gets JSON, any other path a table.
`build` only: `run` refuses the flag (a watched run has many builds; one-shot
runs could take it later). User docs: docs/tools/cli.md, Build reports.

- `compiler/src/build_report.{h,c}` keeps one malloc'd report per build in a
  global (`build_report_current`), since diag phases, the checker and codegen
  have no shared context; nothing records while none is kept. It outlives the
  arena. `tide_build` begins it when `opts->report` is set and no report is
  active, and writes it after the build, failed builds included
  (`"succeeded": false`); a write failure fails the build.
- Steps: every `build_step` label ends the previous step (`build_report_stage`),
  plus `compiling C/C++` from `run_compiles`. Labels said twice in a row merge.
- Tide phases: `diag_phase` (also `TIDEC_TIMES`) now records into the report by
  name, merged across build code's second pass. New phase marks: `running build
  code` after `build_code_run`, and `packing assets` around `build_pack`
  (`generating C` before it), skipped for build code's own program.
- Asset import time: check.c times the resolve block of each asset declaration
  into `decl.import_ms`; warm import-cache hits are what it shows warm.
- Pack bytes: the same per-declaration measurement `--asset-sizes` uses (below):
  `asset_size` also reports each declaration, with `asset_description` (type,
  image size, BC1/BC3/RGBA, [Raw]/[Lossless]) as the report's type, so the two
  can't disagree. Content-hash sharing means a later asset with the same bytes
  adds 0 (`shared`); the remainder of the pack is entries/names. The last
  `build_pack` of a codegen wins (a server repacks). Left-out assets have
  their plain type: nothing was stored.
- Left out: codegen copies the asset list before `program_strip_unused_assets`
  and reports those missing after the final pack, telling server-only cuts
  (in `all_assets`, not kept) from unused ones.
- C/C++: each job's wall time from start to its observed exit, named from the
  game or installation folder; reused objects (stamp hits, engine files, an
  already-built engine) count as `upToDate`.
- Cuts: `report_cuts` describes the mode's policy from the same predicates the
  build uses (`strip_game_assets`, `strip_server_assets`, package C), naming
  `--strip-assets` where a game with C was pruned by it or could be.
- Outputs: the program, its pack and debugger sidecars beside it, or every file
  of a split web page's folder, plus an Android build's returned app.

Not built: per-object/section binary size attribution (needs linker maps),
per-asset compression or texture-format breakdowns, and reports for watched
runs, `tide unity` and deploy.

Test: `tide_cli_build_report` (build_report_test.mjs) uses build_overlap's
controlled compiler with the real front end: refusals, a cold release JSON
report (steps, phases, compiles, pack bytes equal to the file, sharing, the
unused asset left out with why, cuts, outputs' sizes), a warm table (0 compiled,
reused count, layout), debug packing everything, a `--server` build's
server-only cut and its pack, and a failed build's report. Validation: Windows
ASan build; that test, build_overlap, help, run_exit, CLI unit, server_pack and
the asset/pack/stages tidec e2e and unit tests pass; docs production build.
Linux/macOS and web targets' reports are CI/untested (no web build reported).

## Asset sizes and `--strip-assets` (2026-11-04)

From a game's feedback (notes/textures.md, `[Raw]` images compress too): finding what made its pack 618 MB meant
reverse-engineering the pack's table of contents.

- **`tide build --asset-sizes`, `tidec --asset-sizes`**: `codegen_options.asset_size_out` is called once a
  declaration is packed (`build_pack`: the builder's size before and after it, so a block an earlier declaration
  already put in counts as 0 for this one) with its qualified name and a description (`asset_size`: type, an
  image's size or a list's count, the formats of its first four images from `pack_item`'s new `stored`, and
  `[Raw]`/`[Lossless]`). `compiler/src/asset_sizes.{h,c}` collect and print them biggest first, ties by name,
  under the total (decimal units). The CLI passes a collector in `build_options.asset_sizes`; `generate_sources`
  empties it before each compile, so Android's per-CPU compiles report once; `command_main` prints it before
  `Built ...`, after the progress rows. Sizes are pack bytes after pruning, before a web page's compression.
- **`tide build --strip-assets`** (`build_options.strip_assets`): `strip_game_assets` prunes a release or
  distribution build whose game or packages have C sources or libraries, the client's twin of
  `--strip-server-assets`. Detecting C that reads assets automatically (the feedback's other suggestion) was
  not built: C reaches entries through `tide_pack_find` and generated names in ways a text scan would only
  guess at. CMake's `tide_add_game` has no such option yet.
- Neither changes telemetry (flags aren't recorded). Docs: `docs/tools/cli.md` (Asset sizes).
- **Tests:** `tide_cli_assets` (`assets_test.mjs`) builds with `--asset-sizes` and checks the total, a line for
  each of five declarations with its description, and the order; `tide_cli_help` covers both flags'
  help. `--strip-assets` itself has no fixture: it only returns true earlier in `strip_game_assets`, whose
  pruning `tidec_asset_usage` covers.

## Moving project channels and cache expiry (2026-11-03)

Owner approved `tide = nightly` and `tide = stable`, with `tide pin` accepting
both. Each occupies one installation under the shared versions cache, separate
from the main installation and exact releases. At command startup the launcher
checks the channel, using upgrade's existing release ordering (nightly includes
a newer stable release), checksum and package validation. First installation
retains download consent; selecting a channel authorizes subsequent updates.
Offline/failed downloads reuse a whole installed release and report its exact
version. An incomplete installation cannot run. Staging is removed after updates;
no historical nightly installation is created for a channel.

A shared process-file usage lock outside each installation covers the complete
selected command. Exclusive updates run only while idle; another session can
share the installed release and defers updating. A directly invoked cached CLI
holds its own usage lock until exit and recognizes its installation to prevent
recursive channel dispatch. The -dev and Unity package selection exceptions
remain. Unity continues to require an exact package-matching compiler; its
editor holds the same usage lock for selected cached compilers until reload/quit.

Exact cached directories expire after 30 days without use. Startup cleanup checks
only exact-version directories; top-level links, other folders, the main install,
channels and active users/installers are skipped. Last-use stamps live outside
the installations and refresh before and after commands. Legacy caches acquire
a new 30-day grace period on first observation. Expired pins are downloaded again
with existing consent. Lock files remain to preserve cross-process lock identity.
No background service, runtime ABI, telemetry field or game code changes.

Validation: strict warning-free Windows Clang 23.1.2 ASan builds; the seven
focused CLI CTests and tidels_tests pass (8/8). The final version fixture passes
after the direct cached/unpinned invocation fix, including real release CLI
dispatch, nightly/stable pin writes, invalid pin refusal, concurrent channel
sharing/update deferral, consent, checksum/offline fallback, last-use expiry,
legacy grace, active exact-version protection and symlink/junction boundaries.
It exercises both host commands and --web forwarding without running a game.
Unity 6000.6.0f1 VersionTests passes 12 tests in a scratch Sample under
TIDE_HIDDEN/batchmode/nographics/noAudio; one existing ABI-5-host test is ignored
because that unrelated fixture was not supplied. Editor lock exclusion/release
and last-use refresh pass. Node 24.22.1 docs production build and diff checks
pass; the docs retain existing allowed unstaged-game-page 404s. Release-dispatch
checks used an isolated 0.6.0-nightly.999 build; no user toolchain was installed
or project pin changed. Linux/macOS (including Unity's flock path), embedded
LLVM packaging and real game execution remain CI/manual coverage. No WASM/runtime
code changed, so separate web runtime suites are not applicable.


## Steady progress rows and active filenames (2026-11-03)

`line_draw_pair` overwrites both rows and erases only their remaining tails,
then flushes. It no longer clears and flushes a blank display every 100 ms,
which caused the reported terminal flicker. Final cleanup still clears both
rows. Width clipping and upgrade's single-line display remain in place.
C/C++ feedback lists the basenames of currently active jobs in source order,
clipped to the terminal width, so the last slow source remains visible while
the batch waits. The caller copies names under the existing feedback lock;
the drawing thread never reads compiler jobs. Redirected feedback is unchanged.

Validation: warning-free Windows Clang 23.1.2 ASan CLI build; CLI unit,
packages, build_overlap, upgrade and hidden CTests pass (5/5). The new output
regression requires no full-row erasure between updates and tail cleanup when
labels/bars shrink. A real ConPTY build with the controlled compiler shows
multiple active names, then `engine.c` throughout a two-second barrier, and
clears before completion. This exercises presentation, not real game execution.
The production docs build and diff check pass (Node 24.22.1). Other host
terminals/OSes remain CI/manual coverage; no game or WASM runtime code changed.

## Directory order in overlap diagnostics tests (2026-11-03)

The next CI run passed native Linux/Windows, MinGW and all Linux web shards.
macOS passed 2,809/2,810 native tests; only the overlap fixture's last case
failed. It assumed `engine.c` preceded `zlast.c`, while `folder_find` preserves
raw directory discovery order. macOS discovered `zlast.c` first, so reporting
its error first was correct. Production scheduling and diagnostic order remain
unchanged.

The fixture now reads discovery order with `opendirSync` (unlike `readdirSync`,
which sorts names), makes the second discovered engine source large enough to
start first, and requires only the earlier source's error to be reported. A
second case recreates the two files in reversed creation order to exercise both
enumerations on Linux. Both cases retain the barrier, game-error suppression,
invalid engine stamp and earlier-unstarted-job checks. No assertion or timeout
is removed to accommodate macOS.

All eight scenarios pass on Windows ASan and the freshly built Linux CLI under
WSL with its work directory on the Linux filesystem. Node syntax and diff checks
pass. Actual APFS enumeration remains the next push's macOS CI coverage.

## Build code's tools (2026-11-03)

`generate` (build.c) gives tidec's codegen_options the tools for build code's program (notes/assets-and-files.md,
Build code): its compiler command (`tide cc` when clang is built in), and for this machine whatever the game's
target is: on Windows the MinGW target and sysroot, with `-fuse-ld=lld -nodefaultlibs`, the native libraries and the
builtins archive; elsewhere the native libraries (and lld when built in). The installation's `include/` and
`src/engine/` are its headers and sources. The program and its engine objects are kept in the game's
`.tide/build-code/`, what it made in `.tide/assets/imports/<importers>/` (its keys keep the whole compiler's identity). `game_info.assets_affect_match` is set for a game with
constants build code made, so a watched file a constant's code reads rebuilds the game rather than repacking.
Checked by hand with a scratch installation (this checkout's tide.exe, the engine's files, the installed MinGW
sysroot): `tide build` ran build code and linked.

## Linux overlap fixture cache path (2026-11-03)

With watched shutdown fixed, Linux CI's host case reached its unchanged-object
check and exposed a Windows-only fixture path assumption. Native Linux library
builds intentionally keep `-fPIC` objects in `engine-pic`, separately from a
standalone build's `engine`; Windows/macOS use `engine`. The fixture now selects
the existing platform-specific directory for its host case. Object timestamp,
no-recompile, overlap, failure-recovery and diagnostic-order assertions remain
unchanged; no production cache code or thresholds changed.

Validation: all seven overlap scenarios pass with this checkout's freshly built
Clang 23 Linux CLI and controlled compiler under WSL, and on Windows ASan. Linux
unit, run-exit, help, packages and hidden CTests also pass. The WSL overlap work
directory uses its Linux filesystem: DrvFS did not deliver a file-watch event
for the initial gate when the same fixture lived under `/mnt/c`, while the file
existed and the build completed after gate release. No test workaround is
shipped for that local filesystem limitation. Node syntax/diff checks pass;
the next push validates the normal hosted Linux filesystem and remaining gates.

## POSIX watched-run shutdown (2026-11-03)

Linux release CI passed 2,809/2,810 tests but hung in the overlap fixture's
inert-host run after compilation. The detached terminal reader blocked in
`fgets` with stdin still open; libc exit cleanup waited on its `FILE` lock after
the host had exited. A small Linux probe using the actual `sys_read_lines`
reproduced the main thread waiting on a futex and the reader on a pipe.

The POSIX reader now reads stdin's descriptor directly, retries interrupted
reads and preserves newline/CR trimming, 255-byte chunks, final partial lines
and the existing atomic handoff/EOF contract. It holds no stdio lock while
waiting, so process exit does not need terminal input. Windows retains `fgets`.
No scheduler, host, runtime or watch policy changes. The original seven-case
overlap fixture, assertions and timeouts remain intact.

Validation: the blocked-stdin Linux probe exits successfully with the fix;
the new terminal-boundary/EOF unit test passes on Linux and Windows ASan.
The six focused Windows ASan CLI CTests pass (unit, run_exit, help, packages,
hidden and build_overlap). Linux/macOS full CLI and package checks remain the
next ordinary push run's coverage.

## Overlapping build stages (2026-11-03)

The owner's edit-to-test follow-up overlaps independent engine compilation with
Tide generation/assets. After successful package resolution, `generate` queues
engine jobs and starts up to four of the largest sources, capped at `jobs - 1`.
This leaves a CPU for the in-process front end and avoids delaying it on dozens
of child-process launches. The ordinary C batch adopts those same processes,
then starts generated/game/package C within the existing TIDE_JOBS budget.
The front end stays on its caller thread: no concurrent arena, checker or
importer access. Already-cached engines start no speculative jobs; TIDE_JOBS=1
keeps generation and C compilation serial. Android still generates once before
its two ABI builds; its C batches use the shared scheduler without prelaunch.

Within the existing batch, generated game.c keeps first priority, followed by
engine sources in descending file-size order, then other C in its existing
order. File size is a cheap scheduling heuristic, not a duration estimate.
Diagnostics retain the original source order, including earlier jobs still
unstarted when a later job fails. Successful objects keep their stamps;
engine completion still requires every engine job. Failed Tide generation
kills/joins its outstanding compilers, discards their diagnostics and leaves
the engine stamp invalid. Retrying replaces the previous engine-object list.

The initial native reload host is queued after game/package jobs in that same
batch instead of compiling after the game library links. Host/library links
remain serial, with their existing cache stamps and publication rules. Later
watch builds don't queue the host. The single progress display remains owned
by the caller; engine prelaunch keeps the Tide/assets activity label until the
full C batch has a known file count. No runtime API, code-generation semantics,
pack format, optimization mode or telemetry field changes. The existing source
revision fingerprint includes build.c, so Simulation IDs change with this
compiler revision as usual.

Paired measurements (`build-overlap-20261103.json`): Windows Ryzen 9 7900X,
Clang 23.1.2, optimized unsanitized external-compiler CLI, native default-debug
games and 24 workers. Five pairs per case alternate baseline/new order at the
same game/install paths. Each timed run follows an untimed build of the same
baseline source. Code edits change only an added singleton/system's integer
assignment; cold cases remove objects/stamps while keeping assets cached.
Fixtures copy repository Duel/Sumo without C test hosts and package test folders.
Times cover complete `tide build`, including linking, without game startup:

| Fixture | Unchanged, before / after | Code edit, before / after | Cold code cache, before / after |
|---|---|---|---|
| Duel | 160.4 / 167.4 ms | 527.4 / 535.7 ms | 2147.5 / 1822.4 ms |
| Sumo | 258.4 / 261.4 ms | 788.5 / 797.8 ms | 2727.7 / 2446.1 ms |

The useful measured improvement is cold code builds (15.1% Duel, 10.3% Sumo).
Warm/no-op and code-edit paths have no demonstrated gain. This does not measure
the owner's game or claim a native watched-startup speedup. Each pair checks
identical generated C/headers after normalizing only the expected compiler
revision Simulation IDs, and exact packs where present. Starting a whole CPU's
worth of engine processes before entering generation was discarded: process
launch cost delayed the front end and left the long engine files until later.
The bounded head start and larger-file priority address that scheduling gap.

`tide_cli_build_overlap` uses a controlled compiler, process/file barriers and a
local HTTP asset to prove overlap without speed assertions. It covers one-worker
serialization, two-worker overlap, host batching and unchanged reruns, failed
generation killing its compiler then recovering, package failure before any C
starts, game/engine failures and source-order errors when a larger later engine
source starts first. Its fake linked host is an inert GUI-subsystem fixture on
Windows: it opens no game, console, window or audio device. The fixture requires
the external-compiler CLI; embedded-LLVM packages retain their real cache tests.

Validation: warning-free Windows Clang 23.1.2 ASan builds and six focused CLI
CTests pass (unit, run_exit, help, packages, hidden, build_overlap), including
the expanded seven-case barrier fixture. The installed native/web cache script
passes real debug/release/distribution builds, optimization overrides, byte
parity at one/three/default workers, failure recovery, cache invalidation,
debug/release web watching, consecutive web runs and a bounded native dedicated
server. A freshly built native Duel also exits successfully after two hidden
frames. Production docs build, Node syntax and diff checks pass. Platform
libraries were built in this checkout. Actual browser execution, real native
watched library loading, Android, embedded LLVM and other host OSes remain CI
coverage; no runtime code changed.

## Build progress feedback (2026-11-03)

Fresh build/run asset work now has a persistent import/preparation cache as well
as the existing code-object caches. Contracts, measured cold/warm costs and
invalidation/corruption/concurrency coverage are in assets-and-files.md,
Persistent build-time asset imports. The older cache inspection below records
the gap before that implementation; it is no longer the current status.

Final presentation correction: terminal builds now share one two-row display,
the current step above one bar. A single activity thread owns drawing throughout
the initial native/web build, including C compilation and linking; nested source,
library and compile steps update its locked status instead of printing lines or
starting independent displays. Standalone builds, watch rebuilds and repacks
close their own feedback scope; native/web runs close initial feedback before
launch/serving. Android closes it around key/bundle stdout messages. The renderer
clips rows to terminal width so wrapping does not leave extra progress rows.
Known C batch totals use file percentages; other steps use moving activity, with
elapsed time covering the build. Completion clears both rows and replays buffered
diagnostics. Redirected plain output remains available. The native/web watcher
hint is shorter, with the same reload/restart/stop behavior. `line_draw_pair`
extends the shared line helper without changing upgrade's single-line display.
The earlier per-stage terminal output descriptions below are superseded.

Validation of the final display: warning-free Windows ASan CLI build; CLI unit,
dispatch, help, hidden and package CTests pass (5/5), plus upgrade's regression.
Real ConPTY runs of the delayed local-HTTP asset fixture and a controlled slow
C compiler show the same two rows through source work, C compilation and linking,
then only completion output; the invalid-source case clears both rows before
its diagnostic. The docs production build, Node syntax check and diff check pass.
Actual game execution, Android, other native hosts and the full installed web
cache fixture were not exercised for this presentation change.

Asset-cache inspection for the owner's question: asset_files.c's `read_at` cache
keeps decoded images/audio/meshes by path/size/mtime in process memory; watch
rebuilds reuse it, a fresh CLI process does not. Downloads have their URL/ETag
disk cache and mesh_lod.c persists generated levels under `.tide/assets/lod`.
codegen.c's build_pack/write_pack still constructs and writes the pack for a
fresh generation. No general persistent import/pack cache was added here.

Tide/assets follow-up: the synchronous generation step now runs under a terminal
activity bar. Its four-character marker bounces every 100 ms with elapsed time,
including source discovery, external-C identity hashing, checking, code generation
and asset work. It gives no percentage for this unmeasured stage. The compiler
stays on its caller thread; only line drawing runs on a joined helper thread.
An event/condition variable wakes drawing immediately at completion, avoiding
an extra 100 ms wait on fast or cached builds.
`sys_activity_begin/end` saves stderr in a temporary file, stops/joins drawing,
clears the line, restores stderr and replays diagnostics. Diagnostic color
capability is cached before redirection. Allocation/file/thread startup failure
falls back to the ordinary stage label; redirected commands create no activity
thread or diagnostic capture. Package resolution still precedes this activity.
No compiler, asset, generated-code or runtime interface changes.

The activity regression checks repeated start/stop, animation during synchronous
work, deferred/replayed diagnostics larger than the replay buffer including ANSI
text/newlines, stderr restoration and later child-process diagnostics. Windows
restores the Win32 standard handle and its inheritance as well as the CRT fd.
Windows ASan CLI builds and
five selected CLI CTests pass. A real ConPTY run with a local HTTP asset delayed
1.5 seconds shows continuing animation through the wait, then successful
generation and the ordinary C compilation bar. A second build reports an actual
Tide error only after clearing activity. C compilation/linking in this scratch
installation use a controlled compiler executable, not a real game binary.
The docs production build and diff check pass. POSIX execution, Android and
full installed-package native/web integration remain CI coverage.

Owner correction: terminal feedback now follows upgrade's `line_draw`/`line_end`
style. C/C++ batches redraw a 24-character bar in place with file percentage,
completed/total, elapsed seconds and latest completed basename. It starts at zero
and ends at 100 only on success; failures clear it. Diagnostics clear the active
bar before printing, preserving their ordinary order. Other stages are short
labels, as before. The bar advances on actual file completions, without polling
or a time estimate. Redirected output retains the original plain stderr lines.
The paragraph below describes that retained nonterminal behavior.

Correction validation: Windows ASan CLI rebuild and the same four focused CLI
CTests pass. A real Windows ConPTY run of the controlled compiler fixture shows
one bar redrawn at 0%, 50% and 100%; a failing run clears the bar before its
diagnostic and prints no completed bar. Captured output retains plain lines.
The production docs build and whitespace checks pass. No game/runtime or
cross-platform process-wait implementation changed.

Builds now print flushed, newline-delimited stderr feedback for preparation,
package resolution, Tide/asset compilation, C/C++ compilation and linking.
C/C++ batches announce their file/worker counts and each successful completion
with a completed/total count and basename; empty batches and reused links say
they are up to date. Native runs announce launch; web/Android standalone builds
announce packaging, and Android links identify their ABI. Shared build paths
also give watched rebuilds this feedback. stdout completion messages, diagnostic
ordering, cache stamps, compiler scheduling and runtime contracts stay unchanged.
These are real file counts, not an estimated overall percentage or timer.

The package cache fixture checks cold builds reach their reported file total.
Its parallel failure comparison removes only the worker-count/completion lines,
whose order naturally varies, retaining exact compiler diagnostic comparison.
The CLI reference documents stderr progress under redirection.

Validation: warning-free Windows Clang 23.1.2 ASan CLI build; run_exit, help,
packages and hidden CTests pass (4/4). A scratch installation with the actual
CLI/front end and a controlled compiler executable exercises cold completion
counts, warm object reuse, bounded native launch and compilation failure with
captured output. This is a feedback-path check, not real game compilation or
GPU execution. Node 24.22.1 syntax check, production docs build and diff checks
pass. Full installed-package native/web cache tests and Android execution were
not run; this changes host CLI feedback, not WASM or game runtime behavior.

## Telemetry queue claim race (2026-11-03)

Run 41557439497's Linux release telemetry fixture timed out waiting for the two
back-to-back commands' events. Unlike its other cases, this case starts the
second recorder before waiting for the first sender. Inspection found the
shared queue's append/cap and rename/read operations had no synchronization.
A controlled POSIX interleaving reproduces data loss: the sender renames,
reads and deletes the queue after the next writer opens it but before fwrite;
that writer appends to the unlinked file. Only one of two events arrives and
no queue remains. This establishes a real race consistent with the CI failure;
the old hosted log did not record the remaining queue/event count.

telemetry.c now uses the existing process-file lock for append/capping and
claiming/recovering a batch. It releases the lock before creating the HTTP body
file or running curl, so a network wait cannot block a command's queue write.
Local contention has a 100 ms clock budget with 1 ms sleeps; a busy/unwritable
queue remains best-effort, like other local I/O failures. A failed HTTP batch
whose requeue fails keeps its sending file for a later sender's stale-file
recovery. No usage-data field, opt-out rule, endpoint or language behavior changes.

tide_cli_telemetry_queue uses actual file operations/locks with a controlled
fopen interleaving, HTTP stub and clock. It checks lossless claim versus append,
append during HTTP without lock waiting, bounded contention and recovery, and
retaining/recovering an offline batch when requeue is blocked. The original
implementation fails the POSIX interleaving (one event instead of two); the
corrected one preserves both. Windows Clang 23.1.2 ASan builds pass, and the
new fixture plus the original five-case local-HTTP integration pass 2/2 CTests
(5.73 s, Node 24.22.1). The queue fixture also passes a strict C17 WSL Clang
ASan/UBSan build on the POSIX filesystem. No WASM runtime code changed; hosted
Linux/macOS/MinGW integration follows the next normal push.

## Agent-readable profile captures (2026-11-02)

The owner's approved profiling work adds run-only `--profile <file.json>` and
`--profile-warmup <n>`. The CLI makes the report path absolute from its invoking
directory and forwards both arguments to the ordinary standalone host. Profiling
does not select a window/headless mode by itself: the run must also have
`--frames`, `--script` or `--interactive`, and keeps their one-shot build/run
behavior. Web, Android and dedicated servers are refused before building.
Warmup is one decimal integer from 0 through 1,000,000,000, defaults to zero,
and requires a report path. Duplicate profile/warmup options, empty/option-like
paths, malformed numbers and missing values fail with corrective diagnostics.

The shared help catalog, CLI reference and agent materials describe optimized
repeatable captures, actual callback CPU work rather than fixed-step FPS, and
source/counter evidence before changing culling. Report lifecycle, multiplayer
suffixes, measurement meanings and runtime contracts are in standard-host.md.
No usage-data field or build mode changes.

Regression coverage: run_exit_test.c checks all three bounded dispatch routes,
argument/path forwarding, warmup boundaries and early invalid/unsupported
refusals; hidden_test.cmake checks real CLI refusals without installed engine
sources; help_test.cmake checks topic discovery, scope and limits. Validation:
Windows ASan `tide_cli_run_exit`, `tide_cli_help` and `tide_cli_hidden` pass,
as does the generated stripped-metadata regression. The docs production build
passed on Node 24.22.1 (build/profile-docs-build.log). Final native/web profiler
integration results are recorded in notes/standard-host.md.

## Actionable CLI diagnostics (2026-11-02)

The diagnostic wording pass keeps accepted arguments, exit codes, build modes,
package selection and runtime interfaces unchanged. In main.c, known options
with a missing trailing value now show a valid example and an argument help
topic instead of claiming the command does not take the option: output/title,
Unity assets, join/connect, server capacity/port and shard counts/width. Upgrade's
missing --version has a concrete install example. Unknown upgrade/editor options
point to command help; server/player and web/Android conflicts say which option
to remove. Reserved #if symbols and dash-prefixed save directories explain the
replacement spelling.

Package saves and missing local folders now suggest permission/path checks,
while eject's existing destination explains moving the folder aside for a fresh
copy. A damaged pinned executable names the precise cached folder to move aside
and says to rerun in a terminal for a consented download. Compiler launch failures
identify the compiler and executable check; an unrecognized library says to
supply one built for the selected target. Package fetching distinguishes a tool
that could not start from a transfer's exit code, and suggests updating curl for
its success-without-output failure and checking tar/cache access after unpacking
fails. No URLs, credentials or new telemetry values are added to messages.

Regression coverage: tide_cli_help exercises missing values across real command
dispatch, checks a valid help route, reserved-symbol guidance and retained
wrong-command option refusals. tide_cli_versions pins the exact cache-folder
recovery note while retaining its consent/checksum/forwarding behavior checks.
The CLI reference describes these recovery paths. Niche Android signing,
deployment/service responses, Unity conversion and editor-install diagnostics
were left unchanged; existing detailed messages remain the source of recovery
guidance there.

Validation: the Windows ASan build passes without compiler warnings.
tide_cli_tests, tide_cli_help, tide_cli_hidden, tide_cli_run_exit,
tide_cli_packages, tide_cli_versions and tide_cli_fetch all pass, alongside the
compiler/editor diagnostic checks (2,203 selected CTests total). The docs
production build passes. Changed paths are native tools shared by all game
targets; no game runtime or web behavior changed.

## Compiler dependency files (2026-10-30)

`tidec --depfile <path>` writes Make-style source and transitive asset dependencies
after successful code generation, using the existing `game_info.asset_paths`
collection. It refuses inspection-only modes and fails on output I/O errors.
`tide_add_game` consumes the file for reference model/side-buffer rebuilds;
the in-process CLI watcher already reads the same asset path collection.
No game runtime ABI or telemetry fields change. Contracts and validation:
notes/tide-programs.md and notes/assets-and-files.md.

## Release prunes again; distribution keeps only shipping cuts (2026-10-30)

Owner, on the split below: "also release should prune unused stuff / i think
it moved too many things to distribution layer". Standalone `--release`
(and so `--distribution`) builds again compile with function/data sections and
link with --gc-sections (ELF, MinGW lld, Android) or -dead_strip (Apple), and
Tide-only games omit unreferenced assets (`strip_game_assets`). WebAssembly
collects sections in every mode, as before. Distribution alone keeps: symbol
stripping (-s, Apple -S -x -no_exported_symbols, wasm -s), name/shader-name
stripping with the private game.names map, debugger-sidecar cleanup, -DNDEBUG,
the Windows GUI subsystem, the minified/compressed web page, Android's export
allowlist, appId requirement, non-debuggable APK and AAB. Release keeps -g,
PDB/dSYM, natvis/LLDB views, names and assertions. NDEBUG stays a distribution
cut: it is no pruning, and the split's release-keeps-assertions choice stands
until the owner says otherwise.

Package CI fixes in cache_test.mjs (Nightly 41059993148): the asset-mode and
optimization fixtures ran native games with --frames, which open a hidden GL
window that CI's GPU-less Windows runner can't make. Those runs now need
`--gpu`, like nativeRunTest; the optimization fixture's installed engine probe
instead `#error`s at compile time unless __OPTIMIZE__/NDEBUG match the mode and
level, so CI still sees each level reach clang. The probe is outside the game
folder, so the custom-C simulation identity check is unchanged; its -O0 text
returns with its first mtime so the reuse check still finds nothing written.
The optimization fixture's output was `<root>/optimization-native`, the game's
own folder on Linux/macOS (no extension), so ld failed with EISDIR; it is now
`optimization-<target>-out`. Expectations: unused engine/user/GLFW code and
the unused asset are gone in release and distribution, kept in debug.

## Per-project optimization experiments (2026-10-30)

Owner requested an explicit optimization-level flag for project experiments.
CLI build/run/unity accepts one of -O0, -O1, -O2 or -O3. The borrowed
build_options.optimization overrides engine, generated game, launcher and
game/package C/C++ compile/link flags. It does not change mode, DEBUG,
NDEBUG, visualizers, stripping or deterministic float rules. Prebuilt libraries
are unchanged. Without the flag the existing mode defaults remain. Other
levels, -Ofast and duplicate optimization flags fail before building.

Explicit levels use caches such as native-release-O3, including watch/server/
Android/web targets. Compile stamps include the override; returning to a level
can reuse its objects. Trusted-C simulation identity includes the effective
game optimization level because __OPTIMIZE__ can change custom C behavior.
Unity's declaration fallback forwards the flag with space for its argument.
No runtime API, Tide semantics, telemetry fields or CMake defaults changed.
The CLI reference and focused help describe the override and its limits.

Validation: strict Windows ASan CLI/dispatch builds and the two focused
run-exit/help CTests pass. The installed optimization-only cache fixture builds
all four levels on native/web, plus release/distribution at O0 and O3, retaining
each mode's name-map/visualizer policy and separate custom-C identities.
Eight bounded two-frame native runs check actual engine, game C and C++
optimization macros, NDEBUG policy, noncontracted multiply/add bits and
subnormal preservation. Switching back to O0 rewrites no engine/game objects.
Release O3 web-watch passes C/Tide edits, identity/launcher reuse, custom-C pack
changes and visual-only repacking. The production docs build, Node syntax and
diff checks pass (Clang 23.1.2, Node 24.22.1, private staged ASan CLI using
external clang). The normal package cache test includes the optimization cases.
Android, Unity editor execution, embedded LLVM, other OSes and actual WASM
runtime FP probes remain CI/follow-up coverage; this does not claim full
all-platform determinism validation of every optimization level or measure
performance. No workflow was manually dispatched.

## Optimized testing versus distribution (2026-10-30)

Owner approved three CLI modes: default debug, optimized `--release` for
testing/profiling, and `--distribution` for shipping. This supersedes the
older entries below that call the shipping policy `--release`.

Release compiles all game/package C/C++ at -O2 -g, keeps C assertions, names,
assets and native debugger visualizers/PDBs/dSYMs. Default game code stays
-O0 -g and the engine -O2 -g. Distribution implies release, uses the previous
-O2 -DNDEBUG policy, and owns standalone metadata/shader-name stripping,
unused-asset pruning, native export/section stripping, debugger-sidecar cleanup,
Windows GUI subsystem selection, and compressed/minified web pages. Existing
watched/library/layout metadata exceptions remain. Required safety checks and
tick verification are unchanged. Both optimized modes leave Tide DEBUG
undefined; no new language symbol or telemetry field is introduced.

Caches are separate <target>-debug/release/distribution directories. Custom-C
simulation identity records all three modes because NDEBUG can affect trusted
C. Android release APKs stay debuggable and accept the default app ID;
distribution requires appId and produces the shipping AAB/limited exports.
Deploy explicitly requests distribution, preserving its shipping contract.
Unity declaration fallback forwards the selected mode. CMake configurations
and tidec's explicit strip options are unchanged.

Tests: five focused native Windows ASan CTests (CLI unit, dispatch, help,
packages, hidden) pass. The expanded installed cache stripping fixture passes
all three modes on native/web: optimization and NDEBUG probes, retained
names/debug info versus stripped artifacts, shader identifiers/private maps,
debug/release-to-distribution sidecar cleanup, split/single-file pack parity,
custom-C assets and Tide-only unused-asset pruning. Native debug/release
two-frame hidden smoke runs pass. Debug/release web-watch cache/repack cases
and consecutive runs pass; native/web C++ fixture builds all three modes.
Clang 23.1.2, Node 24.22.1, locally rebuilt MinGW/web platform libraries and
a private staged ASan CLI using external clang. Docs production build passes.
Parsed workflow comparison changes only three shipping smoke arguments.
No compiler/runtime code or deterministic hash expectations changed.
Embedded-LLVM gzip, Android execution/packaging, actual optimized debugger
stepping, macOS/Linux and their export/dSYM behavior remain CI/follow-up
coverage. No additional workflow was dispatched.

## What packages need resolves by itself (2026-10-30)

The owner, of a game that had to list `box3d` because its local `combat`
package did ("we should include dependencies automatically its stupid not
to"; found in the Bannerlord duel's gaps list). `packages_resolve`
(`compiler/lsp/packages.c`) is now a worklist of `wanted` lines: the game's
lines first, then each line a resolved package needs that nothing in the list
is yet, with the folder its path is from (`base`), the file it's in (`where`)
and the package that needs it (`by`). Each is checked and read as the game's
lines were (commit, a builtin that exists, fetch, folder, `package` line,
`tide 0.x`, no pin, no second package of a name), with errors at `where`'s
line, so a problem in a package's need points at that package's file. Edges
between packages go by `wanted` index and become the needs matrix the
compile order sorts with, as before.

What code relies on:

- **The game's line wins.** A need matches the game's lines first
  (`same_package`: same source, any commit, or the same folder), and is never
  checked further: a package's line with no commit, or a different commit,
  doesn't matter once the game lists it. This was the rule before; it's now
  also how a game chooses between packages that disagree.
- **Two commits of one package is an error,** at the second need's line:
  `<B> needs <source> at <7>, and <A> needs it at <7>`, with the note that a game
  builds one commit of each and the game's line to add (the first need's,
  `line_text`, so a folder is written from the game's folder). No newest-wins:
  commits have no order without the repository's history. Same commit, any
  refs, is one package; the first line found is the one built.
- **A package's git line needs its commit** unless the game lists that
  package; the error says so at the package's line.
- `package` gained `listed_in` (the file `line` is in) and `file` (its own
  tide.packages, kept until `packages_free`): `line` points into the game's
  file or a needing package's `file`, so both live as long as the result.
  `tide publish`'s "outside the game's folder" error reports at `listed_in`.
- `packages_missing`/`package_need` are gone. `tide add`, `update` and `eject`
  write only the line they name and then resolve with reports (`resolves` in
  `packages_cmd.c`, was `add_needed`): writing a package's needs into the game
  would pin them there and hide the package's own later commits. `tide fetch`
  resolves (downloading what packages need) and also downloads the `#if`-off
  git lines of the game and of every resolved package.
- tidels gets the same packages, as it calls `packages_resolve`.

Not built: a game's line for a package of the same *name* from another source
(an ejected `./packages/rooms` while `lobby` needs `rooms`) still clashes as
"both called Rooms"; letting the game's win by name is a proposal for the
owner. Ejecting a package the game doesn't list needs `tide add` first, as
the message says.

Tests: `tide_cli_packages` (`packages_test.cmake`): `add` writes one line (folder,
git, builtin) and downloads what the package needs; a two-deep local chain
(`outer` -> `../physics` -> `../maths`) with relative paths from each
package's folder, alone and with the game listing all three; a missing
folder a package needs reported at that package's `tide.packages:2`, with the
game's file unchanged; two packages needing maths at `M1` and `M2` (a commit
with a `Maths.Tick` system to show which built) failing with both named and
the game's line in the note, the game's `M2` line then building `M2`, the same
commit from both building, a package's commitless git line failing until the
game lists it; `fetch` downloading a need without writing it, and a local
package's `#if STEAM` git line. `tide_cli_tests`
(`cli_packages_that_come_with_tide_are_in_one_folder`): `dice` alone resolves
`rolls` from Dice's line, in order, with `listed_in` pointing at each file.

## Interactive hidden play (2026-10-29)

`run --interactive <replay.txt>` forwards the replay's absolute path and
selects the ordinary one-shot native build/run path, retaining inherited
stdin/stdout. The hidden runner accepts live frame-queued script input,
explicit steps and current-frame captures, with executed actions saved for
--script replay; contracts and platform tests are in notes/standard-host.md,
Dynamic agent play. Existing web/Android/server checks refuse this native
player mode before building. The help catalog, CLI reference and agent guides
describe the protocol and provisional names. No CLI telemetry field changed.
Run-exit dispatch/path forwarding, hidden/unsupported/missing-value checks,
and focused help tests cover the CLI integration. Native CLI builds and docs
production build pass. After merging the Windows sanitizer work, the affected
CLI builds and six selected platform/CLI ASan CTests pass; final compatibility
results are in standard-host.md.

## Isolated editor tests under Windows ASan (2026-10-29)

editors_test.cmake and jetbrains_test.cmake copy any clang ASan runtime DLLs
beside their source tide executable into the scratch installation's bin.
These two harnesses replace PATH with their fake editor directory, so inheriting
CTest's sanitizer environment alone cannot load the runtime. Their isolated
PATH, profiles and real CLI behavior stay unchanged. Ordinary builds with no
ASan DLL copy nothing extra. Validation is recorded in notes/testing.md.

## CLI pins select a side-by-side release (2026-10-29)

Owner requested using a project's pin and asking to download a missing release,
without replacing the main installation. `versions.c` dispatches build/run,
schedule/shaders and add/update/eject/fetch/publish. Build/run/schedule/shaders
now select before option validation (2026-11-02 entry below supersedes the
original validation-first order).
The original argv, working directory, stdio and exit code are preserved. Pin,
help, version and installation commands describe/use the invoking installation;
Unity package selection and the source-build `-dev` exception remain intact.

The cache is Unity's `TIDE_VERSIONS` or LOCALAPPDATA/Tide/versions (Windows),
HOME/.tide/versions (elsewhere). Missing/incomplete entries prompt [y/N]; EOF
and decline fail without downloading. No-terminal callers fail with guidance;
TIDE_INTERACTIVE enables prompts for callers forwarding stdin. Cached releases
need no network. Probe --version checks the executable before forwarding to
prevent a corrupt/misnamed cache from recursively selecting itself.

`upgrade.c` shares download/checksum/unpack code with its existing upgrade path.
Version downloads use a per-version process lock and private staging directory;
VERSION and executable presence are checked before publishing the whole folder.
Failures remove staging, and neither main-install files nor editor plugins are
updated. Unity may publish the same version during download; a complete cache
is reused. Unity itself retains its existing installer/locking policy.

Tests: versions_test.mjs uses the real dispatcher and installer with local release
archives and a compiled command fixture: matching invoking version, missing
noninteractive entry, decline/EOF, bad checksum, wrong archive VERSION, successful
consented installation, quoted argv and exit code, offline reuse and corrupt
executable rejection. The driver explicitly uses a release version even when the
engine is built as -dev. Existing package/help/run-exit/upgrade tests cover CLI
integration and preserve upgrade behavior. The final version fixture also checks
all nine real CLI command paths and repairs a VERSION-only cache entry.

Validation: Windows Clang 23.1.2, Node 24.22.1, native debug CLI configured as
0.5.0-nightly.26 to exercise release pin selection. Strict warning-free builds;
the seven affected CLI CTests pass (unit/package/version/upgrade/help/run-exit/
hidden), with the expanded version fixture passing afterward. Production docs
build and diff checks pass; docs retain their existing allowed unstaged-example
404s. Downloads use only local fixture releases; no user version is installed.
This is host CLI code, shared by native/web build commands, with no WASM/runtime
change. Actual Unity editor concurrency, Linux/macOS execution and embedded-LLVM
packaging remain CI/follow-up coverage.


## Shipping shaders and exports (2026-10-27)

The owner requested shader identifier minification and tighter public export
lists. Existing strip_names now also shortens the source-derived identifiers
in emitted GLSL/WGSL, with private per-stage aliases in game.names. Contracts
and compiler/backend regressions are in shaders.md. Readable debug/watched/
Unity output remains the default. No generated/runtime API, pack format or
simulation result changes.

Standalone release macOS links add -no_exported_symbols beside -dead_strip,-S,-x,
so defined global names cannot remain public. Shared/hot-reload/embed links keep
their APIs. Android standalone release links use platform/android/game.exports:
only ANativeActivity_onCreate is global; all other game/runtime definitions are
local. Its entry point already has the required explicit visibility/root.
The platform install ships that file as lib/android/game.exports, shared by
the two ABI folders. A missing file fails the release build with reinstall
guidance. Separately supplied game libraries are not relinked or restricted.
The APK, generated headers and host ABI do not change. Custom code relying on
dynamic lookup of libgame.so's former incidental exports must expose its API in
a separate library. CMake shipping apps use the same export policy; debug and
profiling configs retain names.

exports_test.mjs cross-links real ARM64/x86_64 Android and macOS probes and
inspects their symbol tables with LLVM: Android's one defined dynamic symbol is
the activity; macOS has no defined public symbols. cpp_test.cmake's Android
package case checks both actual release libraries when llvm-readobj is present.
cache_test.mjs checks source-derived shader names in native/web debug versus
shipping output and the private map, and checks macOS defined external symbols.
Validation (Windows, Clang 23.1.2, Node 24.22.1, WASI SDK 24, Android NDK
30.0.16248370): warning-free native/web builds; full native debug passes all
2,816 enabled CTests (390.81 s), full web debug all 2,709 (722.93 s), retaining
the existing disabled MicroDuck right-turn case. Minified callable shader pixels
and batching pass Vulkan/OpenGL/WebGL/D3D11/WebGPU. The all-fixture check pins
99 byte-identical SPIR-V modules plus unchanged non-name GLSL/WGSL tokens.
Native/web installed --strip-only checks pass with actual shader sentinels and
private maps, including split/single-file pages and debug-to-release cleanup.

Both Android platform ABI libraries build and install the allowlist. The staged
CLI passes cpp_test.cmake with WEB and ANDROID enabled: real debug/release APKs,
C++ constructors/virtual dispatch/custom allocation, expected failure cases,
and only the NativeActivity export in both release libgame.so files. CMake's
x86_64 Android Duel app and ordinary console cpp test also build; the app's
exports pass the same check. Console targets intentionally receive no activity
allowlist. Cross-link probes pass both ARM64/x86_64 Android and macOS policies.

The tide_add_game opt-in's prototype compatibility check builds BANKED and its
tests in native/web Release against this engine, with banked_test passing on
both. No prototype sources or deployment changed. Docs production build, Node
syntax and diff checks pass. No deterministic expectation was changed. No Vulkan
validation layer is installed; Android device/emulator execution, actual macOS
game linking/runtime, other OSes and the embedded-LLVM package remain CI coverage.
No extra workflow was dispatched and existing public demos were not redeployed.

## Standalone release metadata (2026-10-27)

The owner requested removing readable metadata as a first deterrent to game
reverse engineering. generate passes strip_names for a release that is not
watching, a library or a layout build. Native, web, Android and managed
standalone code generation share that policy. Debug/watched/Unity generation
keeps its existing metadata. codegen's private game.names stays in
.tide/<configuration>/gen/; it is never copied beside the program or into a web
page/pack. Keep it privately with the corresponding build for diagnosing IDs.
The user-facing contract and limits are in docs/tools/cli.md. Replacing a native
debug output removes its old PDB, natvis, LLDB script and (on macOS) dSYM; a
cleanup failure fails the build with a message naming the remaining file.

cache_test.mjs's existing stripping fixture now includes named systems/views,
an assertion, a text asset and a skinned asset whose vertex default reads that
text. It inspects actual native binaries and split/embedded web payloads:
private names/source paths absent only in release, authored assertion messages
present, the private map present only in the generation cache, and no generated
C/headers/maps/PDB/natvis/LLDB sidecars beside the release output. The native
fixture first builds debug at the same output path and requires its real debug
sidecars to exist, then checks that the release removed them. Web checks
also require no name, DWARF, source-map or external-debug custom sections; the
Windows native check rejects COFF symbols and PE debug directories. Existing
code/data pruning and custom-C asset preservation assertions remain. Arbitrary
user C strings, shader text and model contents may still disclose information;
this does not stop repackaging or behavioral cloning.

Validation (Windows, Clang 23.1.2, Node 24.22.1, pinned WASI SDK 24): full native
debug passes 2,812 enabled CTests (241.27 s), plus the new stripped-loader target.
The full web debug run passes the other 2,706 enabled checks (591.63 s), including
WebGL/D3D11/WebGPU. Its new target initially ran before its missing hash-pin
compile definition was corrected and its executable built; that was an ENOENT
setup failure, and the corrected native/WASI target passes with the existing
SKINNED_HASH. Both suites retain the existing disabled MicroDuck right-turn case.
Final CLI tests pass 16/16 after the sidecar fix; the staged installed CLI passes
native/web --strip-only artifact checks, including split and single-file pages
and actual debug-to-release output reuse. A CMake Release Duel build emits its
private map only in its generation directory and passes a hidden --frames 2
smoke run. Docs production build, Node syntax and diff checks pass. No hash pin,
runtime model representation, server protocol or simulation behavior changed.
No Vulkan validation layer is installed. Android, Linux/macOS (including dSYM
cleanup), sanitizers and the embedded-LLVM package remain CI coverage; no extra
CI was dispatched. This does not rebuild or redeploy existing public demos.

## Web builds are a folder; `--single-file` (2026-10-27)

`tide build --web` writes `.build/web/index.html` plus `index.wasm` and
`index.pack`, which the page downloads with progress and caches by hash;
`--single-file` (build only, refused without `--web`) is the former
self-contained `.build/<name>.html`. A game's `shell.html` replaces the
package's shell and its `icon.png` becomes the loader's logo and the tab's
icon, in built and `tide run --web` pages. `tide deploy` builds its client
single. The non-watching branch of `tide_run_web` serves a split page's two
files, though `tide run --web` always watches today. `cache_test.mjs`'s
stripping test checks the split files (raw, named by hash and size in the
page) and a `--single-file` page with the same program; CI's "Try the
package" also builds `--single-file`. Runtime, stages and the shell contract:
`notes/web.md`, Split pages and the loader.

## Debug builds carry their debugger views (2026-10-26)

Native non-release builds (`debug_views` in `build.c`) generate with
`.visualizers`, compile game objects with `-fno-eliminate-unused-debug-types
-gno-column-info`, link the natvis into the PDB on Windows (`-Xlinker
-Xlink=-natvis:...`), and put `<stem>.natvis` and `<stem>_lldb.py` beside the
program or `game-<n>` library (`place_debug_views`); a macOS standalone debug
program also gets a dSYM with the LLDB views. `notes/debugging.md`.

## Direct authored startup and native editor Play (2026-10-24)

`tide build` and `tide run` accept `--scene <qualified-name>`, also exposed by
`tidec`. It selects a prefab or a scene declaration without renaming source
symbols or requiring a handwritten Main/loader. The project's persistent
selection is a declaration-valued `settings { startScene = Levels.Start; }`;
`--scene` overrides it for that build. With neither selection, the existing
Main behavior remains. A prefab entry uses the ordinary checked Scene.Load
path, its authored root and normal Spawned handlers, then ordinary systems,
views and session startup. Match and local scene entries retain their normal
placement. No new runtime function or generated-C/runtime ABI is introduced.
CLI help exposes --scene for build/run and describes hosting and several-player
startup using the selected match/local entry. The help fixture pins the option,
persisted-setting example and removal of the former Main-only requirements.

The private editor flags `--source-overlay <original> <draft>` and
`--editor-cache <directory>` build an applied unsaved document using its
original source path, namespace/package context and relative asset paths. An
original .tprefab not present on disk is included as a game input. The private
cache isolates generated files and objects from ordinary builds and other
editor runs; its configuration subdirectory keeps the usual target/mode
separation. Neither the persisted source bytes nor its project startup setting
are changed by this build. These flags are internal editor plumbing, separate
from the public startup selection.

`tools/tide-editor/play.h` exposes a zero-initialized editor_play, Start/Poll/
Stop/Close and explicit STOPPED, BUILDING, RUNNING, FINISHED and FAILED states.
Start copies the applied source, qualified selected prefab, project/path,
revision and runtime arguments into a unique .tide/editor-play/<pid>-<serial>
workspace. It first builds a standalone normal game, then Poll launches its
executable through the ordinary game host. All normal views run, including
unmarked ones; the game's own camera, devices, local player and session are
used. This is distinct from the frozen authored-view preview. The UI must Stop
before replacing or closing its document. Later authoring edits do not change
the captured running build. Stop destroys the runtime process and its private
artifacts; it does not copy simulation state into the authoring document.

The compiler and game are each owned by sys_start_tree_logged: a Windows job
assigned while the new process is suspended, or a POSIX process group set
before exec. Cancellation kills descendants, and a completed parent also
cleans up remaining descendants. Output is logged, windows remain suppressed
under TIDE_HIDDEN, and the helper refuses hidden Play without bounded --frames,
--capture or --script arguments. Errors retain the workspace/log until Stop,
Close or another Start. Tests use explicit scratch --data-dir paths.

Focused Windows validation: tide_editor_play passes its actual executable/
grandchild cancellation fixture, cleanup after a successful compiler exits,
immutable unsaved revision, build/run/error states and hidden/conflict refusals.
The installed-game test
`node tools/tide-editor/tests/play.mjs <tide_editor_play_tests> <installed tide>
<work>` passes against this checkout's debug CLI and freshly built MinGW
platform/runtime. It builds a no-Main authored game twice, verifies unsaved
values, Spawned, systems, unmarked views, scripted input and fresh runtime
state after Stop, checks real camera/capture pixels, and preserves source
bytes and the dirty authoring document. The standalone process test requires
no GPU; the installed-game test does. POSIX process groups and other desktop
hosts were not run here. The native scene_startup lifecycle fixture and compiler
startup contract script pass on native and web. Full native/web builds and all
engine checks pass; the final seven editor CTests and docs build pass. An
external prototype hash-pin failure reproduces with the HEAD compiler; optional
external browser playthroughs were stopped. Details: notes/editors.md.

## Bundled JetBrains installation (2026-10-21)

`tide editors` now installs the release's own JetBrains plugin into existing
standard IDE profiles, and `editors --update` (called by `tide upgrade`) only
updates profiles already containing Tide. `compiler/cli/jetbrains.c` finds
2024.2+ Rider, CLion, IntelliJ and other supported product/version directories:
Windows APPDATA/JetBrains, macOS Library/Application Support/JetBrains, Linux
XDG_CONFIG_HOME/JetBrains (default ~/.config) with plugins under
XDG_DATA_HOME/JetBrains (default ~/.local/share). New IDEs need one launch to
create their profile; custom idea.config.path/idea.plugins.path are manual.

The offline archive carries Tide and pinned LSP4IJ 0.21.0, with licenses and
source locations. Missing LSP4IJ is installed; an existing installation stays
untouched because other plugins may share it. Settings, disabled_plugins.txt
and unrelated plugins stay intact. Extraction is staged outside plugins, with
whole-directory replacement and rollback; Windows file locks refuse the update
with close-IDE/rerun guidance. Successful explicit installation says to restart;
successful upgrade remains quiet. No IDE is launched and no network is used.

Upstream license texts retain their exact bytes, including original whitespace;
their cli-licenses directory is marked -text/-whitespace in .gitattributes.

Gradle buildCliInstaller builds editors/tide-jetbrains-installer.zip separately
from the Marketplace ZIP. cmake/package.cmake takes -DJETBRAINS=<archive>, or
build/jetbrains/tide-jetbrains-installer.zip from CI, rejects a different VERSION,
and also copies the dependency notices to licenses/jetbrains. The release
workflow supplies the same artifact to all three platform packages and runs
the real archive's installation smoke test on each OS. README, Install, CLI,
Editor setup and the plugin README document the behavior.

The corrupt-archive fixture exposed an existing unsigned underflow searching
for a ZIP end record in files shorter than 22 bytes. unzip.c now rejects those
files before searching; unzip_test.c covers every length 0 through 21.

Validation: Windows Clang 23.1.2/Node 24.21.0 full native debug 2,452/2,452
(142.30 s), full web debug 2,394/2,394 (299.62 s), including 36 native GPU
checks, WebGL/WebGPU and ANGLE D3D11. The final CLI/help/editor/archive tests
pass 5/5, including the additional package fixture after the full run. The
actual Gradle archive installs byte-for-byte into an isolated Rider profile.
Tests cover initial install, repeat/upgrade, stale JAR removal, missing LSP4IJ,
preserving existing LSP4IJ/settings/disabled state/other plugins, old/unrelated
profiles, EAP, absent/corrupt/incomplete payloads, locked Windows JARs and
packaging version mismatch/notices. JDK 21/Gradle 9.8.0 buildCliInstaller and
all eight enabled adapter/platform tests pass. Docs production build, workflow
YAML/job-graph checks and diff checks pass. Actual Rider UI loading, Linux/macOS
installation, complete embedded-LLVM platform archives, Android and sanitizers
were not run; native GPU tests ran without a Vulkan validation layer. No CI was
dispatched and the public standalone plugin source was not exported again.

A final full native run included the additional package fixture (2,453 tests)
and exposed upgrade_test.cmake inheriting the real editor profiles: its dummy
package intentionally has no plugins, so the new missing-payload diagnostic
correctly broke its one-line-output assertion. The fixture now isolates HOME,
USERPROFILE, APPDATA, LOCALAPPDATA and both XDG roots. All other 2,452 tests
passed in that run; the corrected upgrade and both JetBrains fixtures pass
3/3. The macOS/Linux discovery branches also pass strict warning-free syntax
checks with the build's CRT definitions; actual OS execution remains untested.

Integration: origin/dev through 50112539 merged cleanly, bringing Unity outline
output and Quick reference docs. Rebuilt tide/tidec/tidels and web's native
tidec_host for the compiler/CLI dependency overlap. All 36 native CLI/outline/
schedule checks pass across the run and one telemetry rerun; all 21 web-host
outline/schedule checks and the merged docs build pass. The telemetry test's
off/on case once observed one extra event after writing its synthetic queue;
it passed both earlier full runs and the isolated rerun without a telemetry
code change. No telemetry behavior is changed by this task. Rebuilt the actual
CLI bundle as 0.5.0-dev (matching this checkout), repeated the byte-for-byte
Rider-profile smoke test, and staged it with the real package script against
a minimal native fixture, verifying version and notice extraction.

## Usage data (2026-10-21)

The owner approved anonymous CLI usage data: anonymous events, a random install
ID, on by default with a one-time notice, and opt-out. The site side (storage,
admin tab, privacy page) is in `notes/docs-site.md`, CLI usage data.

`compiler/cli/telemetry.c`. `main` is now a wrapper: `cc`/`-cc1` first (no
timing, no event), then the internal `_telemetry-send`, then `command_main`
(the old `main`) timed with `sys_now_ms`, then `tide_telemetry_record`.
`run_exit_test.c` stubs the three entry points.

- **The event** is one JSON line built only from fixed sets: `id`, `command`
  (allow-list in `commands[]`, `other` otherwise; `help` for bare `tide`, `help`,
  `--help`/`-h` and `<command> --help`; `version` for `--version`), `target`
  (`native`/`web`/`android` from `--web`/`--android` for build/run/unity,
  otherwise `none`), `version` (the build's, `unknown` if it has odd characters),
  `os`, `arch` (compile-time macros), `ci` (15 common CI variables, set and not
  `false`/`0`), `ms` (clamped to a day), `outcome` (exit code 0), `error` only
  on failure, `files`/`packages` in ranges only when a command read a game.
  Docs page `docs/tools/telemetry.md` lists every field; the site rejects any
  other value, so the three must change together.
- **Error kinds**: `telemetry_fail(enum)`, last one wins: `setup` (`build_setup`,
  now a wrapper over `setup`), `packages` (`find_packages`), `compile`
  (`generate`, `tide_schedule`), `c` (`run_compiles`), `link` (`explain_link`,
  which every failed link calls), `network` (upgrade's list and download),
  `game` (a native run's game exiting nonzero, watched or not). None noted:
  exit 2 is `usage`, anything else `other`. Sizes come from `game_inputs`
  (`telemetry_sizes`: the game's own files, its package count).
- **Not recorded**: `cc`, `-cc1`, any `_` command (`_editor-preview`, the
  sender), `telemetry` itself and `editors --update` (what upgrade starts).
- **Off** (`telemetry_off`, also the reason `status` prints): `TIDE_TELEMETRY`
  `0`/`false`/`off`/`no`; `DO_NOT_TRACK` anything but empty or `0`; a `-dev` version (`tide_version_dev`, now in `release.c`, shared with
  the update check) unless `TIDE_TELEMETRY_URL` and `TIDE_TELEMETRY_TEST` are
  both set (test-only); `TIDE_HIDDEN` (`sys_hidden`), which CI's workflows set
  globally; `enabled no` in the settings.
- **Settings and queue**: `<root>/telemetry/` beside `channel` (the
  installation, `%LOCALAPPDATA%\Tide` / `~/.tide`; upgrade replaces only the
  release's folders). `settings` is lines `enabled yes|no`, `notice shown`,
  `id <32 hex>` from `rtc_random` (as signing keys). `queue` is an event a
  line, capped at the newest 100 (`cap_queue`, temp file + rename).
- **Notice**: the first run that would send makes the ID, writes settings and
  prints one stderr line, and queues nothing, so turning it off then has sent
  nothing. An installation whose settings can't be written never sends.
  `tide telemetry on` marks the notice shown.
- **Sending**: after queueing, `start_sender` runs `tide _telemetry-send`
  through the new `sys_start_detached` (Windows: `DETACHED_PROCESS |
  CREATE_NEW_PROCESS_GROUP`, no inherited handles, breakaway from the job when
  allowed; POSIX: double fork, `setsid`, stdio to `/dev/null`, fds closed),
  unless a live sender's `sending-<pid>` file exists. The sender renames the
  queue to `sending-<pid>` (plus dead senders' files), POSTs
  `{"events":[...]}` with curl (`-q -fsS --connect-timeout 3 -m 5`,
  `--proto =https` for the default `https://tide-engine.dev/telemetry`), quiet.
  Exit 0 or 22 (HTTP error: refused) drops the batch; anything else appends it
  back to the queue. It loops (8 rounds at most) while the queue refilled: it
  removes its file before looking again and a recorder appends before looking
  for senders, so a command that ends mid-send is never stranded.
- **`tide telemetry on|off|status`**: one line for on/off (off also deletes the
  queue); status prints on/off with the reason, the field list, the endpoint
  (or `TIDE_TELEMETRY_URL`), the install ID, the settings folder and the docs
  link. Help catalog: `telemetry` command and `setting` argument (the overview
  is 28 lines with embedded clang, `help_test`'s limit).

Tests: `tide_cli_telemetry` (`compiler/cli/tests/telemetry_test.mjs`, under
`TIDE_NODE`) copies tide into scratch installations and runs it against local
servers with `TIDE_HIDDEN`, CI variables and opt-outs removed from the child's
environment: a dev build without the override and `TIDE_HIDDEN` runs write
nothing and send nothing; the notice is one line, once, and its run sends
nothing; IDs differ per installation; eight commands' events (schedule ok and
compile failure with ranges, usage failure with target web, `other`,
`--version`, `help`, `<command> --help`, `CI=true`) have only allowed keys and
values, the settings' ID and the CLI's version, and no request body contains
the game's folder name, `game.tide`, the work path, user or host name;
`TIDE_TELEMETRY=0`, `DO_NOT_TRACK` (`1`, `true`; `0` and empty still send), and
`telemetry off` (queue deleted, status reason, ID shown) send nothing and `on`
sends again (the `0` and empty runs use an installation of their own since
2026-10-21: their sender looks at its queue again after its event arrives,
and took the queue the case then writes before `off` deleted it, failing two
runs in three here); offline (a server that cuts every connection), a seeded 150-event
queue is cut to the newest 100 with the run's own event, the command returns
at once, a second offline run stays at 100, and back online all 100 arrive and
the queue empties; a 400 answer drops the batch. `tide_cli_help` covers the new
topics.

Validation (Windows, Clang 23.1.2, Node 24.21.0): full native debug 2,450
CTests (the telemetry test first ran against a binary built before the last
`DO_NOT_TRACK` change and failed as expected; rebuilt, all 13 `tide_cli`
tests pass) and full web debug 2,393/2,393, including WebGL/WebGPU; the
telemetry test passed four runs in a row. Not run: Linux and macOS (the
`fork`/`setsid` sender), sanitizers, Android; no CI or deployment.

## Third-party licenses in the package (2026-10-21)

The package carries the license or notice of everything third-party it
redistributes, as binary or source, in `licenses/<name>/`, each installed with
the `tide` component by the tree that ships the work. Texts come from the
exact tree built, never written by hand: vendored files from the repository,
FetchContent downloads from their source directories, and archives without
license files from their pinned source commits (hash-checked raw files).

| `licenses/` | Ships as | Text from | Installed by |
|---|---|---|---|
| `llvm/` (`LICENSE.TXT`, `BLAKE3/LICENSE`, `COPYRIGHT.regex`), `clang/`, `lld/` | in tide/tidels; clang's headers in `lib/clang/` | llvm-project at `llvmorg-${TIDE_LLVM_VERSION}`, into `build/llvm-<version>/licenses` (the release archives have none) | `cmake/LLVM.cmake` (package) |
| `zlib/`, `zstd/` (BSD of its BSD/GPLv2), `libxml2/` (Windows) | linked into tide | FetchContent source dirs | `cmake/LLVM.cmake` (package) |
| `glfw/` | objects inside `lib/native`'s library | `glfw_SOURCE_DIR/LICENSE.md` | `cmake/GLFW.cmake` (desktop trees) |
| `moltenvk/` | `lib/native/libMoltenVK.dylib` | release's `LICENSE` | `cmake/MoltenVK.cmake` (macOS) |
| `vulkan-headers/` | compiled into native/Android platform layers | `platform/third_party/vulkan` | platform (non-web) |
| `stb_truetype/`, `tuffy/` | font code and font inside every platform layer | the header's own license section, cut out at configure time; `platform/font/LICENSE.txt` | platform (trees with `TIDE_PACKAGE_LIBS`) |
| `fflate/` | in `web/tide*.js` | `platform/third_party/fflate` | platform (web) |
| `wasi-libc/` (+ `cloudlibc/`, `musl/`), `wasi-compiler-rt/` | `wasi/` | wasi-libc `b9ef79d7` and llvm-project `26a1d660`, the commits wasi-sdk 24 builds from, into `build/wasi-sdk-24/licenses` | `cmake/wasi-toolchain.cmake`, platform (web) |
| `mingw-w64/` (`COPYING`, `COPYING.MinGW-w64.txt`, `COPYING.MinGW-w64-runtime.txt`), `llvm-mingw/` | `mingw/` (Windows) | the llvm-mingw archive already downloaded; winpthreads/winstorecompat left out as nothing ships them | `cmake/mingw-toolchain.cmake`, platform (MinGW) |
| `musl/COPYRIGHT.h` | `src/engine/math64` | `engine/src/math64/COPYRIGHT.h` | engine |
| `ufbx/`, `dr_libs/`, `stb_image/`, `stb_dxt/` | in tide/tidels/tidec | `compiler/third_party` | compiler (native) |
| `balanced-match/`, `brace-expansion/`, `minimatch/`, `semver/`, `vscode-*` | bundled into `editors/tide.vsix` | each non-dev `package-lock.json` entry's own license file, collected by `vsix.cmake` (fails if one has none) | `tools/CMakeLists.txt` (package) |

Box2D and Box3D ship as sources in `src/packages/` with their own `LICENSE`
files, unchanged. Not shipped and so not covered: the Android NDK (fetched on
the user's machine after Google's license), the Steamworks SDK (declarations
only, written from its headers) and Microsoft's static C runtime in Windows'
tide. Not covered: notices that live only inside sources linked from LLVM
(xxhash's BSD-2 header, ConvertUTF, SipHash), dlmalloc's CC0 statement in
wasi-libc and Polly (Linux release libraries on the link line, but `cc.cpp`
stubs its registration). Tide's own `LICENSE-MIT` and `LICENSE-APACHE` install
to `licenses/tide/` (root `CMakeLists.txt`).

The llvm-mingw guard now also requires `licenses/`, so a download from before
this is taken again once. LLVM's and wasi's license files download beside
their archives with pinned SHA-256s; a version change needs new hashes.

Validation (Windows, Clang 23.1.2): `cmake --install <tree> --component tide`
into scratch prefixes from freshly configured `web-debug` (wasi, fflate,
stb_truetype, tuffy, musl), `mingw-release` (fresh llvm-mingw download:
mingw-w64, llvm-mingw, glfw, vulkan, stb_truetype, tuffy), `debug` and
`package` (with the main checkout's unpacked LLVM as `TIDE_LLVM_ROOT` and the
VS Code extension built: llvm/clang/lld with BLAKE3 and regex, zlib, zstd,
libxml2, glfw and the nine bundled npm packages). Reconfiguring does not
download the LLVM files again. The stb_truetype file matches stb's `LICENSE`
(as in `compiler/third_party/stb_image`) plus the header's closing rule.
Full native debug passed 2,451/2,451 and web debug 2,394/2,394. Not run: the
Linux/macOS/Android package trees (their LLVM/GLFW/MoltenVK/platform rules are
the same code paths) and `cmake/package.cmake` as a whole.

## Hidden folders and `.build/` (2026-10-21)

The owner's call, after AI trials put probe copies of a game under `build/` and
had them compiled into it: builds go in `.build/`, and no hidden folder (a name
starting with `.`) is part of a game. One rule replaces three: source discovery
(`find` in `compiler/src/folders.c`, behind `folder_find` and `folder_find_own`)
used to walk every folder, `.tide/` and `.git/` included, with `.tide/` and
`build/` filtered later by some callers; C discovery skipped hidden folders and
`build/`; asset discovery skipped hidden entries and the root's `build/`. Now the
walk never enters a hidden folder, assets skip hidden files and folders, and
`build/` is an ordinary folder. That covers tide, tidels, `tide-editor`,
`tide publish`'s sources, `tide eject`'s copy (`copy_entry`: hidden folders and
`.tide-fetched` at the top), the builder's archive check (tide-services'
`builds/builder.mjs` refuses a file under any hidden folder) and `tide_add_game`'s globs in
`cmake/Tide.cmake`. The game folder or package folder passed in may itself be
hidden: only subfolders are skipped.

`default_output` in `compiler/cli/build.c` is the native/web/server and Android
default output: `<folder>/.build/<file>`, with a `.gitignore` of `*` as `.tide/`
has, not marked hidden, so Explorer shows it (Finder and `ls` hide it by name).
Where `<folder>/build/<file>` exists, an older tide's build of the same output,
it says once per build to delete `build/`, which would otherwise be compiled
and linked as part of the game (a stale `.dll` there is a game library now).
No backward compatibility: nothing reads `build/` as tide's.

Tests: tide-services' `builds/publish-cli.mjs` (`tide_cli_publish` until the
2026-10-21 split) sends `build/levels.tide` as the game's and leaves out
`.build/`, `.tide/`, `.git/`, `systems/.scratch/copy.tide` and `c/.old/noise.c`;
its `builds.test.mjs` refuses hidden folders at any depth; `cache_test.mjs`
(the package job) checks `.build/` outputs, its `.gitignore` and the warning.
User docs: CLI Files, the spec's game definition, Namespaces, Your first game and
the AI overview.

## tide unity (2026-10-20)

`tide unity [folder] -o <path> [--once] [--release] [--define S]` builds a game
for Unity's Tide package (`notes/unity.md`): `build_options.unity` makes
`generate` pass `codegen_options.embed`. Into `<path>`: `tide_embed`
(`tide_embed.dll`, `libtide_embed.so`/`.dylib`), linked from a one-line
`embed_host.c`, the engine's objects and the installed platform library, its
ten `tide_embed_*` functions exported (a `.def` file under MinGW, `-u` on ELF
and Mach-O) and renamed into place whole; then `game-<n>`/`pack-<n>` from
`build_library` with a library stub defining `TIDE_HAS_EMBED` and exporting
`tide_unity_schema`, numbered on from the highest already there
(`highest_numbered`, through `sys_list`), so a game running from the folder
takes each; and `schema.json`, written whole by loading the new build
(`sys_library_open`/`_find`/`_close`, new in `sys.c`) and calling its
`tide_unity_schema`. Watching, it rebuilds as `tide run` does and ends when its
input ends (`sys_input_ended`, set by the line reader at EOF); a `restart` line
writes the folder's `restart` file. `--once` builds once. It refuses
`--web`/`--android`/`--server` and a missing `-o`; `tide help unity` and
`--once` have help entries; `tide_cli_run_exit_tests` stubs `tide_unity`.
User docs: `docs/tools/cli.md`, Unity. Checked by hand with a scratch install
(MinGW release plus debug trees) on `unity/Sample`, and by its play mode test.

`--assets <Assets>` (`build_options.unity_assets`) adds the Unity project's
prefabs and scenes (`compiler/cli/unity_assets.c`, reading YAML with
`unity_yaml.c`; the format and rules are in `notes/unity.md`, Authoring as
built). `generate` appends their Tide text as `compile_input`s with `.text`
(new: a file's text given rather than read), paths `Assets/...`, after the
game's own files, and `Library/Tide/Unity.tide` declaring `UnityEngine.Source`; it
writes each text to `<path>/prefabs/<asset>.tprefab`. The parser treats paths
ending `.prefab`/`.unity` as `.tprefab` files (prefab declarations only).
`game_stamp` folds in `unity_assets_stamp` (paths and stamps of every
`.prefab`, `.unity` and `.meta`), so a saved prefab rebuilds the game. After
each schema, `unity.json` (the UnityEngine.Source map, kept on `build.unity_map` from
the last good generate), `outline.json` (below) and then `Packages/com.tide.game`
beside `Assets` (`package.json`, `Tide.Game.asmdef`, `schema.json`, and since
2026-10-22 the game's C# from `unity_code_write` (`compiler/cli/unity_code.c`:
`Components/`, `TideGame.cs`, `components.json`; `notes/unity.md`, Components
of their own), each written only when it changes, as Unity's shared build cache
keys a compile by its sources; the hash stub `TideGameSchema.cs` that recompiled
the generator's types is deleted). `outline.json` (2026-10-21, `write_unity_outline`,
written first, as the C# takes text fields' defaults from it): `packages`, the resolved
`build.packages` (name, absolute folder, and the game's line for each: `kind`
`builtin`, `git`, `local`, or `found` for a folder of Assets with its `package`
line, whose line is 0; `line`, `source`, `ref`, `commit`), `builtin`
(`packages_builtin_names`), and the declarations `print_outline` gave the last
good generate (`codegen_options.outline_to`, copied to `build.outline` out of
the arena, which the next generate resets). The package's inspectors read it
(`notes/unity.md`, File inspectors). Since 2026-10-26 its declarations carry
`doc` (their comment lines), fields `line` and `doc`, and enums `memberDocs`,
and after writing the C# `write_unity_package` records what became of the old
C#'s names in `<-o>/renamed.json` (`unity_code_record`, from the
`Packages/com.tide.game/schema.json` it replaces): `notes/unity.md`, Tide types
and their C# twins.

`tide unity --web` (once: `--web` turns watching off) is `tide_unity_web`: a
standalone wasm build of `generate`'s game with `unity_web_main` (and
`unity_web_exports`, split under C's string length limit) for its `main.c`, in
the web cache, written as `tide-game.js` by `write_unity_script` (config,
program and pack as `page_payload` base64, then the release or debug runtime
with `strip_reloading`), and `unity.json`. `--assets` is taken by `build` and `run` too (for servers of Unity-made games); the prefab texts under `-o` are `tide unity`'s alone. The embed host's exports gained
`tide_embed_draw`, then `tide_embed_render`, `tide_embed_render_view` and
`tide_embed_render_reset` (2026-10-22); the web's `unity_web_main` exports `tide_unity_render`,
`_render_size` and `_render_reset`.
`tide help unity` lists `--web` through the shared option.

With `--assets`, `write_unity_package` also calls `unity_shaders_write`
(`compiler/cli/unity_shaders.c`). It writes the Unity shaders of the renderer's WGSL
(`../../platform/src/shaders.h` with `TIDE_SHADERS_WGSL`) and of the game's
shaders, which `generate` collects through `codegen_options.shader_out` into
`build.shaders` (name and WGSL, NUL after each, copied out of the arena).
It also writes `web-keys.txt`, each shader's WGSL key and its file's, for the web's
program, which has no SPIR-V. Test: `unity_shaders_written` (`compiler/cli/tests/unity_shaders_test.c`): it writes 11
files named by key, with the right Shader names and the HLSL's markers, removes a
stale file and its meta, and removes the game's file once the game has no shaders.

**Builds the editor shows, and C# from a game that doesn't build (2026-10-25).**
Watching, `tide unity` prints `tide: building game-<n>` (`building`) before each
build, the one an asset change makes too, which also says `tide: built game-<n>`
now; the package shows a status-bar task from that line to the result
(`notes/unity.md`, Closing the gaps). After a build that failed, the first one
included, `write_unity_declarations` runs `tide unity <folder> -o <path>/declared
--declarations --assets <Assets>` (with `--release` and the build's `--define`s
but DEBUG) quietly through `sys_run`, so the build's own errors are the only ones
said. `--declarations` (`build_options.declarations`, implies `--once`) is
`tide_unity_declared`: `write_unity_library`, `build_library` and
`write_unity_schema` into that folder with `codegen_options.declarations_only`,
which `compile_inputs` applies before checking (`keep_declarations` drops every
nonbuiltin `DECL_SYSTEM`, including package systems/views/handlers, plus the
game's own `DECL_FUNCTION` and `DECL_SHADER`, before the embed taps are added).
Builtins and package functions/shaders remain: package fields such as Render3D's
Material need real callback defaults and shader binding types. Package bodies,
assets and defaults retain ordinary checking/generation; retained draw helpers
can plan and emit GPU shaders. No checker mode, stub or asset bypass is added.
Game-defined shader-typed fields and callback defaults which need omitted game
functions remain a limitation of this fallback. It writes `Packages/com.tide.game` from that schema,
but not the shaders (`unity_shaders_write` is skipped: the game's would go), and
through `unity_code_add`, which takes no component script away: an engine
component only the game's code gives entities (`Owner`) isn't listed in a build
of the declarations, and its script would have gone and come back. The child is
tide's own executable (`sys_exe_dir`), not `build.compiler`, which is clang where
tide hasn't it built in.
Nothing loads its library: the editor's game keeps its last build, whose schema
hash the new C# doesn't match, so live values wait for the next build that works.
`tidec --declarations` is the same option. Test: `tidec_declarations_only`
(`compiler/tests/declarations_only.cmake`: a game spawning a prefab that isn't
there fails whole and builds declared, with its component and event in the header
and neither its system nor its function in the code; a component's own error still
fails). Its Render3D consumer additionally checks every component, real callback
dependencies, omitted incomplete game code, retained package shader plans and
strict C17 syntax, plus unchanged missing-asset/package-body errors. The focused
native tidec build and this regression pass (1/1, including the C17 probe); root
owns full native/web suite repeats. A separate process was picked over a second `generate` in the same one
because `generate` replaces the build's outline, info, sources and shaders.

## Terminal editor configuration helper (2026-10-18)

`tide ide emacs|vim|neovim` prints ready-to-copy init configuration from the
current installation. It writes no dotfiles, runs no editor/server and does no
package setup, cleanup or update check. main dispatches it before that work;
the implementation lives with the other editor helpers in editors.c. Help has
the ide command, editor positional topic and ordinary --help/-h aliases.

Emacs output loads the bundled tide-lang-mode, chooses the sibling tidels and
adds eglot-ensure. Neovim output adds the bundled runtime, sets the matching
server command and enables tidels. Vim output sets runtime/comment/indentation
support and names the server in a comment for the user's own LSP client.
Executable arguments are lists, with editor-specific string quoting rather than
shell quoting. Commas in Vim runtime entries are escaped. The helper checks all
required payload files and the server before printing; incomplete installations
fail with reinstall/install-build guidance (1). Unsupported editors/counts fail
with usage guidance (2). Vim/Neovim runtime paths containing glob characters
(*?[]) are rejected with a move/install-path fix: real Windows runtime lookup
treated the bracketed directory as a pattern despite ordinary slash escaping.
Emacs retains support for bracketed paths. Moving Tide requires regenerating
the copied configuration. No language or generated-runtime contract changed.

tools/CMakeLists.txt now ships tide-emacs and tide-vim under editors/, excluding
their test directories. This supersedes the initial source-only installation
record in notes/editors.md. The existing VS Code installer/upgrade helper and
JetBrains distribution are unchanged. README, both terminal runtime READMEs and
the CLI/Editors user pages describe the helper, init destinations and client
requirements. The new tide_cli_ide CTest uses the actual tools install rules in
an isolated stage with spaces, an apostrophe, dollar sign and comma. It checks
help/argument failures, repeatability, every missing payload/server, rejected
wildcard paths, excluded tests and preserved configuration sentinels. Optional
TIDE_TEST_EMACS, TIDE_TEST_VIM and TIDE_TEST_NVIM executable paths run the printed
config in real isolated editor processes, with personal plugins disabled.

Validation: warning-free Clang 23.1.2 builds, full native debug 2,386/2,386
(47.08 s) and web debug 2,295/2,295 (247.43 s), including native GPU and
WebGL/WebGPU coverage, with Node 24.21.0. The final native suite includes all
optional generated-config host cases: Emacs 30.2 (all six ERT checks, real
hover/shutdown), native Windows Vim 9.2.1202 (runtime detection/settings) and
Neovim 0.11.5 (automatic connection, hover and shutdown). The generated snippet
also passes on Neovim 0.12.6. The production docs build and diff checks pass;
existing allowed unstaged-example 404s remain. No compiler/runtime semantics
changed. Other operating systems, Emacs 29.1, Vim 8, Android, sanitizers and the
embedded-LLVM package build were not run; no CI or deployment was dispatched.

## Test save directories (2026-10-18)

`tide run --data-dir <folder>` forwards a full native save-directory path to
the ordinary/watched host without changing watch/headless selection. Missing,
empty or option-like directory arguments fail before building; use `./-name`
for a directory whose name starts with a dash. Web, Android and server modes
reject it before building. It is a run option, not a build setting. The shared
help catalog and user CLI/files docs describe it. The fixed argument buffer still
fits every ordinary host option once (now 27 arguments, within its 32 slots).

`tide_cli_run_exit` checks every forwarded argument, directory absolutization
from the invoking directory, and windowed dispatch with the override alone.
`tide_cli_hidden` checks bounded dispatch and early unsupported/missing-value
errors; `tide_cli_help` discovers the new topic. Host/provider behavior and
final validation are recorded in `notes/standard-host.md`, Explicit save directory.

## Focused CLI help (2026-10-17)

`tide` and `tide help` now show only a compact command list with a route to
details. `tide help <command>` prints usage, explanation and relevant arguments;
`tide help <argument>` describes a flag or positional argument, including its
defaults, limits and commands. `tide help <command> <argument>` selects the
command's meaning when names overlap (--project, --size and --frames). Exact
topic names are required; unknown topics, invalid command/argument pairs and
extra topics return 2 with guidance. Bare tide keeps exit code 2; explicit
valid help returns 0. --help and -h remain top-level help aliases.

`tide <command> --help` and `-h`, with no other arguments, show command help
before installation setup, cleanup, update checks or command dispatch, including
open/login/deploy/upgrade. `tide help --help` describes the help argument itself;
`tide help help` describes the help command. Embedded Clang retains its own
`tide cc --help`; only embedded builds advertise cc in Tide's help catalog.
No operational command parsing or language semantics changed.

`compiler/cli/help.c` owns shared command/argument metadata for the overview,
command lists and topic details. main.c dispatches help before doing work.
New CLI arguments need catalog entries as well as their parser cases. README
and docs/tools/cli.md describe discovery. No spec change is needed.

The tide_cli_help CTest runs a standalone copy without installed sources or a
sibling editor. It checks compactness, all public commands and argument topics,
command aliases, relevant-option lists, scoped defaults, failure diagnostics and
exit codes. Windows validation with Clang 23.1.2 and Node 24.16.0 passes:
full native debug 2,366/2,366 (63.71 s), full web debug 2,277/2,277 (237.09 s),
including native GPU and WebGL/WebGPU tests, and the docs production build.
The embedded-Clang catalog branch also passes strict C17 syntax/warning checks;
the development CLI uses external Clang. git diff --check passes. Docs retain
their existing allowed unstaged-example 404s. Other operating systems, Android
and sanitizers were not run; no CI dispatch or deployment was requested.

## Failed-build cache comparison (2026-10-15)

Build `38761765659` passed Windows packaging but Linux and macOS packaging
failed the parallel-compilation fixture's comparison after repairing custom C.
The managed-hosting identity change intentionally keeps ordinary launcher
objects through identity-only edits to game.h. A Linux reproduction differs
only in main.o: its DWARF line table retains the failed build's game.h checksum.
Recompiling against the restored header changes that checksum; stripping debug
information leaves byte-identical objects. This is debug metadata, not stale
gameplay code or a changed Simulation ID in the launcher.

The fixture now requires byte-identical reuse of the failed build's launcher
and clean-build parity for every other file, including generated game code,
objects and stamps. Missing/extra files and differing objects are named on
failure. Its clean builds at different parallelism levels still compare every
file, including main.o. Compiler/cache behavior and public contracts are unchanged.
The original comparison fails locally under Linux with only main.o named;
the corrected fixture passes all parallel-build and failure-recovery checks.
The staged Windows CLI with an external Clang 23.1.2 also passes the full
native/web cache, stripping, failure recovery, debug/release watch and dedicated
server integration script with Node 24.16.0.

## Selected Apple SDK (2026-10-15)

The next requested Build, `38767273771`, reached macOS package smoke testing
on image 20261013.0424.1 and failed in Apple's linker with an unknown
arm64e.x1 architecture in the standalone Command Line Tools 27 SDK. The
[image manifest](https://raw.githubusercontent.com/actions/runner-images/macos-26-arm64/20261013.0424/images/macos/macos-26-arm64-Readme.md)
lists Xcode 26.6 as default alongside those newer standalone tools. Unlike
the previous run's older image, this exposes find_macos_sdk's hardcoded
preference for the standalone SDK while /usr/bin/ld follows selected Xcode.
The prior Apple-linker workaround was active; this is a mismatched SDK/toolchain.

Automatic SDK discovery now captures `/usr/bin/xcrun --sdk macosx
--show-sdk-path`, trims its line ending and requires an existing absolute
directory before setting SDKROOT. Thus DEVELOPER_DIR/xcode-select govern both
SDK and linker, as [Apple's developer-tool selection](https://developer.apple.com/library/archive/technotes/tn2339/_index.html)
intends. Explicit SDKROOT is preserved. Failed/empty/relative/missing discovery
or an environment-write failure returns failure; no fallback silently mixes
two developer installations. README documents the selection contract.

The new tide_cli_toolchain CTest compiles the real Apple discovery branch on
every native platform with process/environment/directory probes stubbed. It
checks the selected SDK (including spaces), explicit override and those failure
paths without changing the machine's tools or environment. Actual Apple
linking still needs the subsequent requested macOS package job.
The original discovery fails the selection regression locally; the corrected
fixture passes 3/3 on Windows and in a separate strict C17 Linux build. Full
Windows native debug passes 2,360 CTests (119.33 seconds), and web debug passes
2,272 (259.60 seconds), including WebGL/WebGPU. Node 24.16.0/Clang 23.1.2,
Node syntax checks for the earlier fixture edits and git diff --check pass.
Actual macOS confirmation: runs 38768886333 and 38772910822 pass all installed
native/web/Android smoke, C++ and cache/watch checks. The former uses the same
20261013.0424.1 image that exposed the mismatched CLT 27/Xcode 26.6 installation.

## Complete Windows installer preflight (2026-10-15)

The installer fixture's startup preflight now uses its real mocked fresh-install
path, sharing the release list with timed cases and exercising selection,
scratch creation and the real checksum before stopping at mocked unpacking.
It verifies the selected release, credential-free requests and temporary-file
cleanup. Cases retain their 30-second limits, assertions and fresh isolated
module caches; preflight retains its 300-second cold-start guard and writes no
user installation, PATH or registry state. Run 38768886333's phase evidence,
one-time-delay/failure probes and validation are in releases-and-ci.md.
Run 38772910822 passes the Windows native installer CTest in 22.83 seconds and
all required release checks, publishing v0.4.0-nightly.3. Local POSIX coverage
also passes all eight cases; Windows passes all nine. Individual case and
preflight guards remain 30 and 300 seconds respectively.

Folder deployment link (2026-10-15): `tide deploy` needs no dashboard setup.
Its existing project auto-creation now normalizes the folder name and saves
the immutable project ID plus service origin atomically in `tide.deploy.json`.
The file is portable and contains no credentials. Future deploys use the ID
after folder renames; `--project <name-or-id>` selects/relinks explicitly.
Malformed links, service mismatches and inaccessible IDs fail without creating
another project. Explicit missing names still create a Small/one-match project.
Bundle-only builds ignore the link. Help, the hosting guide and operator README
describe this behavior. Installed-CLI coverage exercises first deploy, repeat
after rename, invalid/mismatched/missing links, name creation and ID selection;
validation results are recorded in managed-hosting.md.

Managed deployment (2026-10-15): `login`, `logout` and `deploy` are implemented
in deploy.c, using explicit browser device approval, origin-bound credentials,
Windows DPAPI/POSIX private files and bounded HTTP responses. Whole-game local
release builds are default; `--server-only` and offline `--bundle-only` are
available. The managed WASI reactor has its own cache and no platform library;
normal web/native targets are preserved. Every build writes an output.simulation
sidecar. CLI hashes normalized custom C/header contents plus all library target
variants conservatively. C edits now rebuild game.o to publish the new Simulation
ID; ordinary launchers still reuse their objects on body/identity-only edits.
The managed launcher embeds the ID and must rebuild on it. Cache tests pin this
distinction. Provider setup, wire bundle format, actual CLI/browser deployment
tests, quotas and remaining launch work: managed-hosting.md, hosting/README.md (tide-services).
Custom C also makes asset edits refresh compatibility; the watch regression now
checks that path, then removes C to verify visual-only pack reload is preserved.

Dedicated service builds (2026-10-15): run/build --server has separate server
debug/release caches, an ordinary console entry, no watch/reload and section
collection in both configurations. Server-only runtime options are forwarded
and incompatible player/capture targets rejected. Closed builds automatically
strip client-only assets; --strip-server-assets opts custom C in explicitly.
serve.h/server_run.h ship through existing platform header/archive installation.
Contracts, entry callbacks, auth and validation: dedicated-servers.md.

## Upgrade fixture channels (2026-10-14)

`tide_cli_upgrade` reads both the version and default channel from the built
CLI's `version` output. Its local release JSON uses `prerelease: false` for
stable builds and `true` for nightly builds; output and saved-channel checks
expect that same channel. It still exercises upgrading a running executable,
an already-current installation and recovery after an interrupted upgrade,
including checksums, replaced/removed files and preservation of game packages.
No CLI behavior or public contract changed.

Stable Build run 38577620235 at 8637bd0f failed only this test in all five
native/MinGW/sanitizer suites: the fixture had always advertised a nightly
release, which a stable CLI correctly refused. The original fixture reproduces
that error locally with a 0.3.0 stable CLI; the corrected fixture passes all
three cases. The full nightly-default native debug suite passes 2,286 tests
with Clang 23.1.2 and Node 24.16.0 (43.35 s). Full web debug passes 2,249 tests
(171.75 s), including WebGL/WebGPU. `git diff --check` passes. Owner-requested
stable Build run 38584459544 confirms the fixture passes on Windows, macOS,
Linux, MinGW and sanitizers. All platform tests/package checks passed and
v0.3.0 published; the separate Marketplace upload failure and remaining setup
are recorded in notes/releases-and-ci.md.

## Native editor preview builds (2026-10-13)

The native editor's private _editor-preview command takes project, original
prefab source, draft file, qualified prefab, output directory and match/local
placement. tide_build_editor reuses ordinary folder/package/generated C/native
library compilation and asset packs, with DEBUG. The unsaved source overlay
keeps its original path, checked against the selected project boundary. Engine
objects reuse the ordinary native cache; revision modules have separate game
outputs. The command is an internal transport, not advertised as a user build
mode. Preview flags and lifecycle are documented under tide open and Editors.
Installed packages include the private tide/editor_preview.h callback header.

On Windows, logged/quiet children use CREATE_NO_WINDOW so preview compiler jobs
create no console windows. Unredirected children explicitly inherit the caller's
stdio handles, preserving --check/hidden-run output when tide-editor is a GUI
subsystem app. Existing sys_run's output contract stays the same. Installed
preview.mjs checks the real native build/asset/library path, shader captures,
unsaved drafts/undo/redo and C link failure. Ordinary CLI/hidden/installer tests
run in the full native suite. No browser or new runtime entry point is needed.

## Native authoring command (2026-10-12)

tide open forwards arguments to the sibling tide-editor executable and returns
its exit code; it builds/runs no game and opens no browser. Native builds with
the platform enabled install the app in bin beside tide/tidels. The CLI names
the missing desktop installation/build target if the app is absent. Project
discovery, --check, bounded captures/scripts and limits are in docs/tools/cli.md,
docs/tools/editors.md and notes/editors.md. The app uses the shared package
resolver without fetching, and checks with DEBUG/package symbols. Normal build,
watch and package behavior is unchanged. CLI/UI fixtures exercise real dispatch.

## Optimized engine in development builds (2026-10-12)

The owner approved optimizing the engine by default after the external
2,500-character FBX stress test exposed the cost of an entirely unoptimized
runtime. `tide build` and `tide run` now compile engine sources with `-O2 -g`
in development, while generated game/launcher code and game/package C/C++
retain `-O0 -g`. Assertions and the Tide `DEBUG` symbol remain enabled;
release flags, determinism flags, stripping and hot reload are unchanged.
The policy applies through the shared compiler path to native, web and
Android, including watched libraries. Repository CMake debug presets stay
fully unoptimized for engine development and correctness testing.

Engine compile jobs and their object stamps use the same engine-specific
flags. An old engine stamp rebuilds engine objects without changing the
game's compilation policy. Existing per-game/configuration caches remain;
ordinary game edits do not rebuild the engine. No ABI, runtime API or
language semantics changed, and no animation sampling/binding cache was added.

The package cache integration test checks optimization/assertion macros in
engine and game probes for native/web debug/release, plus invalidation of an
old unoptimized engine stamp while preserving other objects. Its optimization
probe fails against the preceding CLI with `engine_must_be_optimized`.

Validation on Windows with Clang 23.1.2 and Node 24.16.0: full native debug
passed 2,235/2,235 CTests (62.26 s), full web debug passed 2,203/2,203 (162.96 s),
including OpenGL/WebGL/WebGPU coverage, and the docs production build passed.
The complete staged CLI cache/stripping/parallel-compilation and debug/release
web-watch integration passed with an external compiler. Separately relinking
the unoptimized core-test objects against `-O2 -g` engine libraries with
assertions enabled passed 435/435 on both native and WASI, including unchanged
cross-platform math/random and skinned-pose bits. Android, other operating
systems and sanitizers were not run; no CI or site deployment was requested.

The CPU-only external stress diagnostic recorded 2,500 synchronized characters
with 61-node animation sources and 1,604 triangles per character. Mean view
recording over eight measured frames after two warmups was 273.36 ms with the
unoptimized engine and 23.46 ms with only the engine optimized, keeping callers
unoptimized, assertions enabled and the same Windows debug C runtime. This
isolated diagnostic excludes simulation, GPU work and display waiting; its
private FBX fixture is outside the repository, and no gameplay FPS is inferred.

## Web-watch request diagnostics (2026-10-11)

- Build run `38139106428` at `bd9ea8d5` passed every platform suite and the
  Linux/macOS package checks. The Windows package failed in
  `cache_test.mjs`'s debug web-watch startup, after its native/web stripping,
  parallel-compilation and cache-invalidation checks passed. The only failure
  printed was `The operation was aborted due to timeout`, 26 seconds after
  the watch case began. The initial page fetch was the only request whose
  five-second timeout escaped to the script's top-level error handler; the log cannot
  distinguish waiting for headers from waiting for the body or establish why
  the hosted request stalled. The runner used Node 24.21.0.
- Page requests retain their five-second limit, including reading their body,
  and fail immediately on transport errors or timeouts. Build/pack polling
  retains its previous unavailable-request behavior and 120-second readiness
  guard. Every web-watch failure includes debug/release mode, the last request's URL,
  header/body phase or HTTP status, elapsed time and captured CLI output.
  Reload edits, object-reuse assertions, model identities, pack counters and
  last-good-build assertions are unchanged; the CLI/server implementation is
  unchanged.
- Validation: this checkout's compiled CLI and staged web library passed the
  original and revised debug/release watch cases. A temporary fetch fault
  fixture reproduced the original bare error on the first header timeout.
  Injected header/body timeouts, connection refusal, HTTP 404 and invalid page
  content still failed in the revised test, with request/CLI evidence. A
  permanently unanswered page still failed at its five-second request guard.
  A candidate page-readiness retry recovered from injected timeouts, but was
  discarded: it did not establish the hosted stall's cause and would permit a
  longer initial page wait. Full Windows debug suites passed 2,085 native and
  2,057 web tests, including OpenGL, WebGL and WebGPU, using Node 24.16.0. The integration
  stage uses external clang rather than embedded LLVM; hosted Windows
  confirmation remains outstanding. After merging incoming compiler changes
  through `05887c6f`, rebuilding the CLI and repeating the debug/release watch
  checks and injected body timeout also passed their expected outcomes.
  No Actions run or retry was dispatched.

## Smaller standalone web releases (2026-10-10)

- Integrated revision `0100b415`, including concurrent skinned batching, was packaged with fresh native, web and both Android platform libraries. Native/web cache, stripping, parallel-compilation and watch checks passed; all 2,440 installed files matched the stage by SHA-256. Installed JPEG/TGA/BMP and MP3/FLAC builds passed on native/web, their decoded packs matched exactly, and a hidden three-frame capture produced rendered pixels and nonzero audio. Current minimal/Duel releases are 990,208/1,423,872 native bytes and 397,615/610,951 HTML bytes. Matched readable/raw pages using those same decoded payloads are 965,358/1,534,650 bytes: reductions of 58.81%/60.19%. Original isolated feature measurements remain valid; the small subsequent size increases are from the incoming rendering work.
- Requested Build run `38085091225` at that revision passed all three platform package jobs, Linux native/web/sanitizers and Android checks, macOS native/web, MinGW, all 2,037 Windows web tests, docs and JetBrains verification. The Windows installer and real WebGPU playtest passed in 13.99/53.62 seconds. Windows native passed 2,047 of 2,048 tests; only the unchanged `last_light_browser_process` forced-shutdown case failed, so the overall run failed and published no nightly. That helper and its test are byte-unchanged throughout this size work and passed both earlier runs and this run's Windows web job. The verified local installation is the built dev snapshot, independent of publishing.
- Final validation after the private-header stamp and browser pacing changes passed all 2,064 native and 2,034 web tests. Updating the local installation verified all 2,440 files against the final stage. Installed native/web image/audio builds and a hidden three-frame capture passed; rebuilding minimal and Duel reproduced their measured native/HTML sizes exactly.
- `tide build --web --release` uses the installed `web/tide.release.js`, generated conservatively from the ordinary runtime with whitespace/comments removed and lexical identifiers shortened. Property names and the public `Tide`/WASM interfaces stay intact. Debug builds and watched runs use readable `tide.js` and their existing reload protocol. The checked-in generated runtime, source-hash gate and regeneration instructions are described in `notes/web.md`; building a user's game needs no JavaScript build tools.
- Packaged Tide reuses LLVM's existing zlib dependency to gzip the embedded WASM and asset pack independently, before base64, only when this makes the payload smaller. The gzip header has no timestamp and OS 255 on every host. The release runtime accepts raw or gzip bytes synchronously and validates the decompressed size and CRC before the program sees them. The generated WASM and native `TIDEPACK` format are unchanged. A development CLI built without embedded LLVM keeps raw payloads; the same release runtime accepts them. Payloads beyond zlib's single-buffer limits also remain raw.
- `cache_test.mjs` decodes either representation, requires compressed release WASM for a packaged compiler, checks WASM magic and compares the embedded pack byte for byte with `game.pack`. Its ordinary debug fixture still requires raw payloads. Native release linking, object stamps and watching do not depend on page compression.
- Validation: full debug suites passed 2,062 native and 2,032 web tests; the added runtime tests and all 35 rebuilt browser/page consumers passed afterward, as did the docs production build. The fresh embedded-LLVM package passed native/web debug/release stripping, compressed-pack parity, debug/release web watching and the native/web C++ integration cases. Installing that tested stage verified all 2,440 files by SHA-256; the installed CLI built the image/audio fixture for native/web, and a bounded native capture drew successfully with nonzero imported audio. Final page measurements and compressed browser checks are in `notes/web.md`.

Build notes, moved here from `AGENTS.md` as they were: what's built, what code relies on, the tests and the measurements. `AGENTS.md` has the rules every session needs and the index of these files. The headings below are that file's sections as they were, and "see <Section>" names one of them: the index says which files each is in. What a note calls "below" or "above" may be in another of these files now: search `notes/` for its name.

## Packaging and releases

- `tide run [--host [port] | --join code | --connect address]`, `tide build [--release] [--web | --android]`, `tide schedule`, `tide shaders`, `tide add`, `tide update`, `tide eject`, `tide fetch`, `tide publish`, `tide editors`, `tide upgrade` and `tide version` (`compiler/cli/`, owned by Claude). It runs tidec's front end in-process, compiles the generated C and the engine's sources with the determinism flags (as separate files, like the CMake build), and links the prebuilt platform layer. `tide run` reloads the game as its files change by default (see Hot reloading). A run with no window (`--capture`, `--capture-audio`, `--frames`, `--microphone`, `--script`: see Rendering and platform) runs once, with no watching: tide makes the files' paths whole from where it was run, since the game runs in its own folder.
  - **What `run` passes on to the game is a list kept by hand** (the options' loop in `compiler/cli/main.c`, into `game_args`): `--host`, `--join`, `--connect`, `--capture`, `--capture-audio`, `--script`, `--microphone`, `--frames`, `--scale`, `--players`, `--size` and `--seed`. An option of the game's own command line (`tide_run_configure` in `tide/host.h`) with no case there is one `run` "doesn't take", whatever the docs say: `--capture-audio` and `--microphone` were so from the day the host got them until 2026-10-08, listed in `docs/tools/cli.md` and refused with exit code 2. So an option the host gains gets a case in that loop (its file through `path_absolute`, and `headless` where the host makes the run one with no window), a line of `tide help`, and a place in `run_exit_test.c`'s run of every option once.
  - **`game_args` has 32 slots, and every option once is 25 arguments.** The loop refuses ("run takes each of its options once") only past 28, which leaves room for the longest option, `--size`'s 3, and the NULL after them, so every option once fits in any order, with the game's folder after them. It was 24 slots and a refusal past 20, which the two audio options would have outgrown: all of them once were refused.
  - **Repeated `--define` flags have no fixed parser cap** (fixed 2026-10-08): `main.c` keeps an argument-sized heap array, with room for `DEBUG`, and `build_options.defines` borrows it until the command returns. The array and its argv strings survive the arena resets of every watched build; `generate` and `tide_schedule` pass its pointer and count to the in-process compiler, so no extra compiler command line or command-length limit is involved. The old 64-slot field rejected the 64th valid symbol as an invalid name. `tide_cli_packages` schedules an offline game with 71 user symbols in debug and release, requiring early, middle and late symbols and the right `DEBUG` value.
  - **Help text respects C17's 4095-character string-literal guarantee.** The former full usage literals were replaced by the focused command/argument catalog described above; keep each catalog string within that limit for `-Wpedantic` builds.
  - **A file that isn't there yet has a whole path on Linux and macOS too** (found and fixed 2026-10-08). `path_absolute` (`compiler/cli/sys.c`) asked `realpath`, which answers only for what exists, and gave the path back as it was typed otherwise, so `--capture` and `--capture-audio`, whose files a run makes, went to the game by the path typed, into a game that runs in its own folder. Seen with a tide built for Linux under WSL, from a folder beside the game's: `tide run game --frames 2 --capture x.png` wrote `game/x.png`, `--capture-audio x.wav` wrote `game/x.wav`, and a file in a folder (`shots/x.png`, `./shots/x.png`, `../there/shots/x.png`, one through a link) was one it "can't write". `--script` and `--microphone` name files that exist, and Windows' `GetFullPathNameA` needs none to, so both were right. Now, where `realpath` has no answer, it's asked for as much of the path as is there, a name less at a time from the end, down to the current folder for a path that isn't from the root, and the names left follow as they were typed (`absolute_missing`). What code relies on:
    - **A file has the path it will have once it's written:** the part that's there is `realpath`'s, with its links followed and its `.` and `..` gone, as a path that exists always was. Joining the current folder and the path as typed would name the same file, under another path than the next call's (`out/../x.png`, a folder that's a link), and that's what the game says it captured and what `Built` says. Names after one that isn't there stay as typed, a `..` among them too: the system refuses such a path, from the root as from here.
    - **No path stays no path** (`""`, as on Windows), never the current folder: `sys_exe_dir` asks with one when it can't read where tide is.
    - Nothing changed for a path that exists, and nothing on Windows. What comes back for one that doesn't is all that did, so `path_absolute`'s other callers make the files they made (the game's folder for `build`, `run`, `schedule`, `fetch` and `publish`, `-o`'s file, `TIDE_ANDROID_KEY`, Android's SDK folders: what tide links and writes itself runs in the folder tide was run in), and say what Windows says: `tide build nothere` names the whole folder, as `schedule` and `run` did, and a first `tide build -o out/game` says `Built` with the whole path, as the next one did.
    - Tests: `absolute_path_of_a_file_not_there_yet` (`compiler/cli/tests/sys_test.c`, in `tide_cli_tests`): a file in a folder that's there, a name alone, folders that aren't there, a slash at the end, up from a folder and back, a whole path and no path, each against the path of the folder that's there; the file written through its whole path is the one typed, and has the same path after; on Linux and macOS, a file through a link, there and not. It fails on the `sys.c` before on Linux; Windows, whose side didn't change, passes it. `tide_cli_run_exit` compares what `run` passes on with `path_absolute`'s own answer, so it never pinned this. Run on Windows and under WSL with LLVM 23's clang (`ctest -R tide_cli`, all ten on each), and under WSL `cache_test.mjs` with the installed tide, natively. By hand under WSL with the tide before and the one after, from beside the game's folder: of seven runs (the six above and a whole path) one wrote where it was typed before, and all seven after; and `build`, `schedule`, `shaders`, `fetch`, `publish` and `run` of a folder that isn't there, a package's folder that isn't, and `build -o` four times made the same files with both, and said the same but for five lines, each the whole path where the typed one was. Not run: macOS, and `cpp_test.cmake` (the scratch installation builds no web games).
  - Tests: `tide_cli_run_exit` (`compiler/cli/tests/run_exit_test.c`) runs `main` with every option once and reads what the game would be given, each as typed and the four files as `path_absolute` makes them, has `--capture-audio` and `--microphone` each make a run that isn't watched, and has an option given 16 times refused; `tide_cli_hidden` (`hidden_test.cmake`) has each of the two go on to build by itself where nobody watches, and each file's option say what it takes with no file. Both fail on the `main.c` before. Tried by hand on Windows with an installation made of this checkout's debug and `mingw-release` trees, from a folder beside the game's: `--frames 2 --capture-audio x.wav` wrote the WAV where it was typed (1,600 frames), `--microphone` read a file from there and said so of one that isn't there (exit code 2), and both went through a run of `--players 2`. Not run: Linux and macOS.
- Packages have clang and lld built into tide (`TIDE_EMBED_LLVM`, `compiler/cli/llvm/cc.cpp`), so users need no compiler: `tide cc` is clang, whose compiles run in tide's process and whose links go to lld in it. tide links the static libraries of LLVM's own release, downloaded once into `build/llvm-<version>` and pinned (`cmake/LLVM.cmake`, which also builds the zlib and zstd they were built with, and on Windows libxml2; unpacking needs the `zstd` program). macOS's release holds them as LLVM bitcode, so tide links there with that release's lld. Builds of this repo without the option use an installed clang (`--web` needs `wasm-ld`, next to it or on `PATH`).
  - **On a Mac, games link with Apple's linker, not lld** (2026-10-14). `link_apple` (`compiler/cli/llvm/cc.cpp`) runs `/usr/bin/ld`, which runs the linker of the developer folder `xcode-select` names. The command line tools that have it are what tide needs for the SDK anyway (`find_macos_sdk`). clang's driver writes the same ld64 arguments for either linker, since tide passes itself as the linker path and so isn't taken for lld. lld still links where `/usr/bin/ld` can't run.
    - Why: GitHub's `macos-26-arm64` image of 2026-10-13 (version 20261013.0424.1) has an SDK whose stubs (`.tbd`) list the target `arm64e.x1-macos`. LLVM 23.1.2's lld rejects each such stub as malformed ("unknown target"), and with it every symbol it defines (`libSystem`, Cocoa, IOKit), so the package job's `tide build` of the demo failed (Build run `38634155349`). The same code had passed an hour earlier on version 20260907.0351.1.
    - It happens to any Mac with that SDK, not only CI. Apple's linker reads Apple's stubs, whichever SDK. Others hit the same with lld 21 (Apple's forums, thread 849269) and with Zig's linker; no lld fix was found.
    - Checked on Windows by compiling `link_apple` against LLVM 23.1.2's headers. A Mac runs it only in CI's package job.
- tide is nearly all LLVM, so its size is what of LLVM it links and how (the owner, 2026-10-06: "if we can make it smaller we should"). Only what tide builds with is in it:
  - **The targets tide doesn't build for are left out.** lld's drivers register every target LLVM's release was built with, for link-time optimization (`InitializeAllTargets` and the like, which are inline, so lld's own objects name each target's functions), and naming a target's `LLVMInitialize...` functions links the whole of it: 7 targets in LLVM's Windows release, 20 in its Linux one, which were a third of tide for Linux (AMDGPU alone 28 MB). `cc.cpp` defines those functions, empty, for every target but tide's three (x86-64, arm64 and WebAssembly), from LLVM's own lists (`llvm/Config/Targets.def`, `AsmPrinters.def`, `AsmParsers.def`), so the linker never takes theirs out of the libraries, which stay on the link line. A target a new LLVM adds is left out with no change here. A left-out target's object taken some other way is a duplicate symbol, an error; a kind of function LLVM starts naming for every target (a disassembler's, say) takes the targets' libraries again, which shows as tide growing by tens of megabytes, and gets a line in `cc.cpp`.
  - **The same for Polly,** which LLVM's Linux release builds into every pass pipeline (`llvm/Support/Extension.def`): each extension there is one that registers nothing. tide never asks for it, and `tide cc -mllvm -polly` is an unknown option.
  - **What nothing reaches is left out, functions that are the same code are one, and there's no table of names** (`cmake/LLVM.cmake`, on `tide_cc`, in Release builds). Windows' linker (lld-link) does the first two by itself where there's no debug info, and a program there has no symbol table: the explicit `/OPT:REF,ICF` gives the same bytes. Linux and macOS have to be told: on Linux `--gc-sections` and `-s`, and `--icf=safe` with lld, which the build links tide with where clang finds one (CI's does; GNU ld folds nothing, and tide is 8 MB more); on macOS `-dead_strip`, `-x`, `--icf=safe` and `-no_exported_symbols`, as nothing loads into tide, which makes every name one the table leaves out. So a crash of tide's own says addresses, not functions, on every platform. macOS's lld keeps the code it made of LLVM's bitcode in a cache that CI keeps between runs, under keys that leave out most of how the code is made (whether a function's address matters, which `--icf=safe` goes by): a change to that takes another number in the cache's name in `build.yml` (`lto-2-`).
  - lld's link-time optimization still works for tide's three targets, and `tide cc` for another target says there's none, as it did (cc.cpp never registered the others for clang).
  - Sizes, in millions of bytes, from 0.3.0-nightly.73 to nightly.75: tide went from 120.9 to 105.0 for Windows, from 210.1 to 99.5 for Linux and from 192.2 to 92.7 for macOS, and their packages from 63.2 to 59.1, from 71.9 to 45.2 and from 67.5 to 46.0. A link map says where the bytes are (`-Wl,-Map=<file>` with lld, `/lldmap:<file>` with lld-link), a line a function with its library.
  - **tide keeps LLVM's own release libraries, which are built for speed, with profiles** (decided 2026-10-06: "i prefer speed for sure"). An LLVM of our own built for size (MinSizeRel, the three targets, no static analyzer) made tide for Linux 78.3 MB where it's 99.4, and its clang took 1.3 to 1.5 times as long to compile a game, which is every build and every hot reload. The same LLVM built for speed was no smaller than the release's, and 1.1 to 1.2 times slower, having no profiles. So no size is bought with how fast tide compiles.
- Generated game and launcher objects are reused in `.tide` when their C contents, generated header, installed headers, compiler and flags are unchanged, in builds, within a hot-reload run, and from one watching run to the next in the same `run/watch-<n>` folder, whose host and first library also skip linking when nothing they're made of changed (`notes/hot-reload.md`, 2026-10-26). Tide still checks and generates the whole game and links each build; this is object reuse, not per-file incremental Tide analysis. Replacing installed headers requires restarting a running `tide run`.
- **Standalone release games collect unused code and data** (2026-10-10). `compiler/cli/build.c` compiles release engine/game/package C with `-ffunction-sections` and `-fdata-sections`, then links MinGW and ELF executables with `--gc-sections,-s`, macOS executables with `-dead_strip,-S,-x`, and WebAssembly with `-s` (wasm-ld already collects unused functions and data in every configuration). The section flags enter the ordinary object stamps; every build relinks, so a changed link policy never reuses a stale program. `-O2` and the determinism flags stay as before, with no LTO. Debug links and watched/hot-reload links keep their existing policy and public API. Android release libraries collect private sections and strip debug/symbol tables while retaining public exports, including the NativeActivity entry; no export visibility is narrowed. Prebuilt platform objects get sections in Release, RelWithDebInfo and MinSizeRel through `tide_flags`, so collection can work within an archive member as well as between members. Runtime references, retained exports and callbacks remain roots, and arbitrary prebuilt third-party libraries can only be collected as finely as their own sections allow.
  - `cache_test.mjs` checks the installed build path using a private installation copy: an extra engine source and the game's own C each contain a called function/data sentinel and an uncalled one. Both uncalled markers exist in the release objects but disappear from the final native/web program, both called markers remain, and native debug retains all four. An asset with no Tide reference remains available to the game's C. `--strip-only` runs these checks without the cache/watch cases; they also run in the ordinary package cache test. `cpp_test.cmake` builds the successful constructor, virtual-dispatch and custom-allocation cases in debug and release on native/web, and its Android case in both configurations when requested.
  - Automatic unused-asset pruning is enabled only for release standalone games without layout/watch mode and without C/C++ or prebuilt libraries in the game or any resolved package. C can look up a pack entry by name without a Tide reference, so such interop games keep every asset. This opt-in is independent of `DEBUG`; the compiler's default and custom-host builds keep their asset API. Asset graph details are in `notes/assets-and-files.md`.
  - Validation on Windows: the staged package passed native/web sentinel checks, all C++ package cases in debug/release, and Android compilation for both ABIs using the installed NDK. Both Android release libraries retained `ANativeActivity_onCreate` and `main` exports. Web watching passed in debug and release; the native scripted reload/swap test passed with ordinary debug libraries and isolated optimized libraries compiled with function/data sections. Node syntax and strict C17 warning checks passed. No Android emulator or macOS linker was run for this change.
  - Full debug suites passed 2,061 native and 2,031 web tests, and the production docs build passed. With identical source cases and LLVM 23.1.2, a minimal Windows release shrank from 1,534,464 to 991,744 bytes and Duel from 1,915,392 to 1,436,160; their WASM payloads shrank from 835,829 to 601,001 and 1,274,398 to 1,033,845 bytes. These are raw payload sizes, not compressed download sizes. The runtime JavaScript stayed at 148,672 bytes. Bounded native/browser smoke runs, stale-pack cleanup and unused URL/path omission passed; asset pack measurements are in `assets-and-files.md`.
  - WSL passed the seven asset-usage cases under ASan/UBSan with leak detection. ELF probes using Clang 18/23 and both GNU ld and lld preserved called functions, C++ constructors and virtual dispatch, removed unused function/data sections and stripped `.symtab`. GNU ld retained a pooled unused string sharing a section with a used literal; packaged Linux's embedded lld removed it. After merging concurrent skinned runtime/codegen optimizations, the affected asset/model/pack/order suites passed 206 native and 201 web tests. Requested Build run `38079507404` covers `c75ebcda`, including these changes; its Windows and macOS package checks exposed the missing private-header installation described below.
- **Gameplay model assets rebuild under watching** (built 2026-10-08, extended 2026-10-09): `game_info.assets_affect_match` is true when generated code has a skinned asset or assets reached by its typed skinned vertex defaults, including Decode-only programs. Native and web watchers in `compiler/cli/build.c` send settled asset changes, including external glTF buffers, through the ordinary library/program build path; the generated header's canonical model-content and selected default-dependency fingerprints give that build its new identity. A game without either keeps its existing pack-only repack path. A failed generation retains the last successful `game_info` and its watched asset paths, so correcting a bad model triggers another build without an unrelated source edit. `game_info` is returned in-process; there is no serialized CLI metadata format for this flag. `tidec_skinned_default_identity` covers asset dependencies through Decode without an imported skinned declaration.
  - The web-watch regression sets the external buffer's before/after timestamps to distinct fractions of the same fixed second, with unchanged size (2026-10-09). This deterministically catches a stale compiler asset cache even when the watcher successfully publishes another build; waiting for another wall-clock second would hide the bug. `folder_file_info` now preserves POSIX nanoseconds as the CLI watcher already did (`notes/assets-and-files.md`).
  - `compiler/cli/tests/cache_test.mjs`'s web-watch test covers C and Tide reloads, ordinary assets publishing a pack without compiling, adding a skinned model, changing its glTF and external buffer, refusing a bad model while keeping the build/pack, correcting it, and removing the last model to restore pack-only reload. `--watch-only` runs this case against an installation with just the web toolchain. It needs no browser or GPU; the test polls the served build/pack numbers. This case passed on Windows using an isolated clang AddressSanitizer CLI and this checkout's web-debug platform/library installation. It also found a `--layout` generation crash in synthesized `BoneTransform` defaults, fixed in codegen and the default AST before the successful run.
- **A build's C files compile several at a time** (built 2026-10-08, asked for after the compile benchmark showed a cold build was one compiler after another: 38 engine files, then a package's, Box3D's 53). As many at once as the machine has processors, 64 at the most (`SYS_AT_ONCE`, what Windows waits for in one call), or as many as `TIDE_JOBS` says (Claude's pick, Provisional: for a machine short of memory and for measuring; anything but a whole number from 1 up is refused, with what to set). What a build makes and says is what one file at a time makes and says. What code relies on:
  - **Files are listed, then compiled** (`compiler/cli/build.c`: `compiles`, `queue_compile`, `run_compiles`). `compile_game` lists the engine's files (`queue_engine`), `game.c` and what starts the game (`queue_generated`), the game's own C and each package's (`queue_c_files`), which is the order they were always compiled in, and runs the list; Android does it for one CPU after the other. Whether a file is listed is decided as before, by its stamp, and the lists a link is given (`build.engine`, `c_side.objects`) are filled as files are listed, never as they finish: a link gets its objects in the order it did.
  - **A failure is the one a file at a time would reach.** Once a file hasn't compiled, no file after it in the list is started, and every file before it still is (`stop`, `next_compile`); the ones on their way finish, and keep their objects and stamps. What the compilers said is kept and printed a file at a time in the list's order, up to the first file that failed, and nothing of the files after it (`show_compiles`). So which errors a build shows never depends on which compiler was quicker. Don't start files out of that rule: the first file that fails in the list's order has to have been started whatever else failed.
  - **`game.c` is started first** (`compile_job.first`), as it takes longest (a release build's is seconds), though its turn to speak is after the engine's.
  - **Stamps.** A file's stamp goes as its compile starts and is written once it compiled, so a compile cut short leaves nothing that passes for its object. The engine's one stamp goes when any of its files is listed and is back once the last of them compiled; with none listed it stands. The engine's files are first in the list, so a build that fails in the game's C leaves the engine built.
  - **What a compiler says is kept through a pipe** (`sys_start_kept`, `sys_wait_any` in `compiler/cli/sys.c`): read by a thread a program on Windows, where the wait is `WaitForMultipleObjects` on the processes, and with `poll` on the pipes elsewhere, whose end is the program's. Nothing is timed or polled. tide lets go of a pipe's end to write once its program has started, and the ends to read aren't inherited, so no program holds another's.
  - **Colors are asked for.** A compiler can't see that its words are for a terminal any more, so when tide's stderr is one (`diag_colored`, `compiler/src/common.c`) each compile gets `-fcolor-diagnostics`, and on Windows `-fansi-escape-codes`. They're no part of an object's stamp. Not looked at in a real terminal: an agent's runs have none.
  - A failed C++ file's explanation (`explain_cpp_compile`) reads what was kept, where it ran the compiler a second time.
  - **Measured** (2026-10-08, the packaged tide on Windows, 12 cores of two threads, a quiet machine, the same tide with `TIDE_JOBS=1` and without, in turn; `bench/results/parallel-compiles-metadata.json`, and `parallel-compiles*` in `bench/results/compile.jsonl`). A cold build, least of two rounds each way: Sumo 7.5 s to 1.2 in debug and 17.1 to 2.5 in release (15.6 to 2.4 for the web), Towers 6.9 to 1.0 and 14.0 to 3.0, Duel 3.2 to 0.64 and 7.7 to 1.8, the tower defense sample 2.9 to 0.67 and 6.8 to 1.1. Builds after the first are what they were (nothing changed 0.1 s, a system's body edited 0.4 to 0.6 s in debug). A cold release build is now its `game.c` and little else: 1.6 s of Duel's 1.8, 2.0 of Sumo's 2.5. That one file is what's left to split.
  - Tests: `atOnceTests` in `compiler/cli/tests/cache_test.mjs`, which CI runs on each platform's package, natively and for the web: one, three and a processor's worth at once leave the same objects, generated files and stamps, byte for byte but for the time a Windows object's header says; with two files that don't compile, every way says exactly what one at a time says, four times over; the build after a failed one compiles only the game's C; `TIDE_JOBS=many` is refused. Run on Windows with both kinds of tide, and on Linux under WSL (the `poll` side); `tide build --android` was tried on Windows, for both CPUs (`cpp_test.cmake` with `-DANDROID=ON`). Not run: macOS.
  - Not built: the engine's objects shared between games (each game's folder compiles its own for each configuration, which is where tide keeps its work: the owner's to decide), and `game.c` in more than one file.
- The package brings the C library for web games (wasi-libc) and for Windows games, which build for MinGW-w64 (see MinGW builds): Microsoft's C runtime can't ship with tide, and the UCRT is part of Windows, so players need nothing else. Linux games use the system's C development files, and macOS games the SDK of Apple's command-line tools (tide sets `SDKROOT`).
- On macOS, `tide build` and `tide run` also put MoltenVK beside the game's program (`place_moltenvk`): the package's `lib/native/libMoltenVK.dylib`, of which only the slice for the game's CPU goes (`lib_universal_slice`). See `notes/rendering.md`, MoltenVK on macOS.
- A game is a folder of `.tide` files. tide keeps its work in `<folder>/.tide/` (hidden on Windows too, and it ignores itself in git) and puts `tide build` output in `<folder>/.build/` (not hidden on Windows; it ignores itself in git too). Hidden folders are no part of a game (see Hidden folders, 2026-10-21).
- A game's packages are what its `tide.packages` lists (`docs/packages/using-and-writing.md`, `docs/spec.md`, Packages): git repositories, each pinned to a commit, folders, and the packages that come with tide (below). No lock file: the game's file lists the packages it chooses, what they need comes from their own lines, and the game's line for one wins (see What packages need resolves by itself, 2026-10-30). `compiler/lsp/packages.c` reads the file and finds each package's folder (tide and tidels share it). `compiler/cli/fetch.c` downloads a commit as its host's archive, and reads branches and tags from git's `info/refs`, with curl and tar as `tide upgrade` does, so users need no git; commits are kept once for every game (`TIDE_PACKAGES`, or `%LOCALAPPDATA%/Tide/packages` and `~/.tide/packages`). A commit unpacks in a folder of that tide's own, and its files move into the commit's folder one by one, never as a folder (`fetch_place`): Windows renames no folder while a program has anything in it open, as antivirus has each new file for a moment, and a file such a program has open still moves. So a commit's folder is whole when it has `.tide-fetched` (`packages_fetched`), which goes in last, and one without it is downloaded again; a file that's there already is that commit's, so a download goes on from one that stopped and two tides can get a commit at once. `compiler/cli/packages_cmd.c` is `tide add` and `tide update`, which only change the file's commits and add what packages need. Packages' files compile first, each package after those it needs, then the game's (`compile_inputs`), and their C with the game's. A folder with a `tide.packages` is a game or a package of its own, never part of the one around it (`folder_find_own`). Tests stand in for git hosts with `TIDE_GIT_MIRROR` (`compiler/cli/tests/packages_test.cmake`).
  - **A Unity project's Assets folder** (2026-10-21, owner) is a game whose packages include, with no lines, every folder under it whose `tide.packages` says `package Name` (`packages_unity_assets`, `add_unity_packages` in `resolve`), whose own C, headers and libraries are none (`own_files_of`), and whose package commands work from any folder of it (`command_folder`). Details and tests: notes/unity.md, The game is Assets.
  - **Adding a package preserves its whole source** (fixed 2026-10-08): local and built-in sources go directly to `append`; git source/ref/commit lines allocate the exact formatted length. The previous 1024-byte buffer silently truncated accepted sources before saving them, leaving an unusable package line after recognizing the package. `tide_cli_packages` adds a valid local source over 1200 bytes (redundant `./` parts normalize to a short real path on every OS), checks the complete saved line, runs its schedule and adds it again.
  - **Package commands keep resolution failures** (fixed 2026-10-08): `tide add` checks a package's metadata before appending its line. It then calls `packages_resolve` with diagnostics and returns its result (since 2026-10-30 it appends no dependency lines first), so `add` and `update` fail on missing folders, malformed package metadata or incompatible Tide versions. A missing-lines count of zero formerly let both commands report success for an unusable graph. `tide_cli_packages` checks that malformed metadata fails before changing the game's file, missing transitive folders fail in both commands, malformed transitive metadata fails in `update`, and correcting the dependency makes `update` and `schedule` succeed.
  - **A game pins its tide** (2026-10-25, the owner: pinning "in the tide.packages i think makes sense", with only Unity's package switching versions). `tide = 0.5.0-nightly.12` (`packages_file.pinned`, without a `v`; `packages_exact_version`: three numbers and perhaps a pre-release) in a game's `tide.packages`: `split_words` takes three words for that line alone; one more `tide` line of either kind is an error. `resolve` refuses a tide whose `tide_version_text` isn't the pin (`packages_pin_kept`), noting `tide upgrade --version <pin>` or `tide pin`, or in a Unity project (`packages_unity_assets`) `tide pin <this>` or moving the package to the pin's commit; a build of tide's own source (`-dev`) keeps any pin, as someone working on tide. A package's `tide =` is an error (only a game pins). Superseded by the 2026-10-29 CLI version-selection entry above: project commands now select cached releases and prompt to download missing ones. `tide pin [version]` (`tide_pin` in `packages_cmd.c`) writes `tide = <version>` in place of the file's `tide` line (`set_line`, its comment kept), or as its first line; with none given, this tide's version, refused for a `-dev` build; a package's folder is refused. Help entry `pin`, `tide_cli_run_exit_tests` stubs it. Tests: `cli_packages_pin_one_tide` (`compiler/cli/tests/packages_test.c`: the line with its `v` and a comment, five mistakes, the comparison including `-dev`), `tide_cli_packages` (by the tested tide's own `--version`, as CI's tide is a release's: pin without a version refused for `-dev` and this tide's for a release; refused for `0.5`, written over `tide 0.1` keeping its comment, again with `v0.6.0`, prepended to a file with none; a `-dev` tide scheduling a game pinned to another, a release's refusing it and scheduling it pinned to itself; a package's pin refused in a build and by `tide pin`; it first failed every CI test job, assuming `-dev`, and was checked with `-DTIDE_VERSION=0.5.0-nightly.26`), `tide_cli_help`.
- Packages that come with tide are in its installation, and a game opts into one with a line: one that doesn't list it compiles and links nothing of it. How the line is written and where the package is looked up are picks (Provisional in `docs/spec.md`, Packages, awaiting the owner). What's built, and what code relies on:
  - The line is a name alone, lower-case letters and digits after a letter (`packages_is_builtin` in `compiler/lsp/packages.c`): `box2d`. Nothing else in the file is written so, as a git source has a `/`, a folder starts with `.` or is absolute, and `package` and `tide` take a second word. It's `package_line.builtin`, apart from `local`: no commit and no `@ref`, each an error that says it's the version this tide came with.
  - The package is the folder `<name>/` of the one folder that holds them, which tide and tidels say as they start (`packages_set_builtin_folder`); until then no package comes with tide, as in tests that say nothing. In an installation that's `<root>/src/packages/`, beside `src/engine/`, as both are sources that compile with the game. tide says its own: `<root>` is the folder its `bin` is in (`install_root` in `compiler/cli/main.c`), as for `include/` and `src/engine/` (`build_setup`), so a tide built from this repo has them once `cmake --install` put them beside it, as it has the engine's files. tidels says its installation's (`installed_root` in `compiler/lsp/main.c`, the folder above its own with `include/tide/math.h`) or, in a build of the repo, the repo's `packages/` (`TIDE_SOURCE_DIR`), as its engine headers are the repo's then.
  - This repo's are in `packages/`, a folder each, which every tree installs as `src/packages/` (the top `CMakeLists.txt`; `OPTIONAL`, as there may be none), without what tide makes in a game's folder (`.tide/`, `.build/`). Not as `packages/`: where the installers put tide, an installation's `packages/` is where packages from git are kept (`packages_cache`), and `tide upgrade` replaces every folder its release has, whole. So an upgrade replaces these with `src/`, and never touches that one.
  - Beyond that it's a folder's package: `packages_folder` gives its folder, so resolving, the build's Tide files, C and libraries, `tide run`'s watching (`game_stamp`) and tidels go as for a folder. Its `tide.packages` says `package Name` and lists what it needs, which the game builds without listing: by name, or from git, never a folder.
  - `tide add <name>` writes the name, in lower case however it was typed. `tide update` leaves such lines alone and says nothing about them. A name no package has is an error that lists those there are (`packages_builtin_names`: the folders named as one that have a `tide.packages`) and the closest (`packages_builtin_unknown`, with `suggest_close` of `compiler/src/common.c`).
  - **A package that needs a library tide doesn't have** is one of these too: `steam` (2026-10-11, `notes/lobby-steam-package.md`) calls Steamworks' library, which a game brings in its own folder. Nothing but a game that lists it compiles it, so no build without the SDK is touched, and its C stops a web or Android build with an `#error` that says what to write. Its folder is `steam`, as a hyphen (`lobby-steam`) is no part of such a line.
  - `tide_cli_tests` checks the lines and a folder of packages made up for the test (`compiler/cli/tests/packages_test.c`); `tide_cli_packages` puts tide in an installation of its own, with packages, and runs `tide add`, `update` and `schedule` there; `lsp_open_folder_with_a_package_that_comes_with_tide` checks tidels.
- **`#if` lines in `tide.packages`** (built 2026-10-11; the owner's idea, for a Steam package in shipping builds and ours in testing: `--define STEAM`; the details are Claude's picks, Provisional in `docs/spec.md`, Packages). `packages_read` in `compiler/lsp/packages.c`. What code relies on:
  - **A line a false `#if` leaves out isn't in `packages_file.lines`,** so nothing that resolves, compiles, watches or names a game's packages has a rule for it: the resolver, the build's inputs, `PACKAGE_` symbols, the C and libraries, tidels. It's in `packages_file.off`, for the commands that keep every line: `every_line` in `packages_cmd.c` gives `tide add` (already listed), `update` and `eject` both lists in the file's order, and `tide_fetch` downloads `off`'s git lines after resolving the build's.
  - **The symbols are said by whoever builds** (`packages_set_symbols`, kept, not copied): `main.c` gives a build's defines, `DEBUG` among them without `--release`, before anything reads the file. Nothing said is `DEBUG` alone, which is what `add`, `update`, `fetch`, `eject` and tidels read with, as editors read code. So those check the file as `tide run` would build it, and a mistake only a `--define`d build has shows in that build.
  - **Conditions have a reader of their own** (`condition_or` and the rest), with a `.tide` file's grammar. The lexer's is tied to a file being lexed and records each symbol read for the game's hash, which this file's aren't part of; the packages a condition brings change the generated code themselves.
  - **A directive is `#if`, `#elif`, `#else` or `#endif` at a line's start** then a space, `(`, `!` or the line's end (`directive`). Every other `#` line is the comment it was, `# if ...` with a space included. `#ifdef` and `#ifndef` would have been comments, and silently no condition, so they're errors.
  - `package` and `tide` lines inside an `#if` are errors; "listed twice" is among one build's lines; an `#if` that never ends is reported at its line. They nest 32 deep (`PACKAGES_DEPTH`).
  - Tests: `tide_cli_packages` (the last section of `packages_test.cmake`): the branches by `--define` and `--release`, `PACKAGE_` following, nesting, a package in two branches, each error, and `update`, `add`, `fetch` and `eject` on a line the build leaves out.
- **`tide eject <package>`** (built 2026-10-11; the owner, of a command that puts a package's files in the project to edit: "a good thing to have anyways"; what it does in detail is Claude's picks, Provisional in `docs/spec.md`, Packages): `tide_eject` in `compiler/cli/packages_cmd.c`. What code relies on:
  - **The package stays a package.** Its folder is copied whole, `tide.packages` included, to `<game>/packages/<last part of the line's source>`, and the line becomes that folder (`./packages/rooms`). A folder with a `tide.packages` is never part of the game around it (`folder_find_own`), so its namespace, its place in the compile order, its `PACKAGE_<NAME>` symbol, its C and libraries and what it needs are what they were, and nothing else in the game changes. Copying its files in among the game's would have changed all of those.
  - **What tide made isn't copied:** hidden folders (`.tide`, `.build`, `.git`) and `.tide-fetched` at the package's top (`copy_entry`). An installation's packages have no `test/` (the top `CMakeLists.txt` leaves it out).
  - **The line keeps its comment** (`set_line`: all of the line before a comment is replaced, a git line's commit with its source).
  - **Nothing is half done:** a folder already there is an error before anything is copied; a copy that fails is removed and the file isn't written; the file is written last. A git line with no commit is an error that names `tide update`; one not on this machine is fetched first. A line that's a folder already says so and exits 0.
  - It's named as `tide update` names one (`names_line`), and ends with `add_needed`, which resolves the game's packages with reports as `add` and `update` do.
  - Tests: `tide_cli_packages` (`packages_test.cmake`): one that comes with tide and one from git, the comment kept, tide's own files left out, the game building with the copy (a system added to it shows in `tide schedule`), `tide update` finding nothing from git after, a folder in the way, a name the game doesn't list, and no name. `run_exit_test.c` has a stub for it, as for `add`.
- A game's C is in its folder too, outside hidden folders: every `.c` file compiles with the game's flags, and every `.cpp`, `.cc` and `.cxx` one as C++ with no runtime (see Language), each again only when it, a header there (`.h`, `.hpp`, `.hh`, `.hxx`, `.inl`) or the flags change, and every library (`.a`, `.lib`, `.so`, `.dll`, `.dylib`) links when its contents say it was built for the target's platform and CPU (`compiler/cli/libraries.c`), since `.a` is every platform's. `.so`, `.dll` and `.dylib` files are copied next to the program, and `.so`/`.dylib` ones found there through its rpath; a `.dll` links through its import library. Linux's libraries and Android's are both ELF, for the same CPUs: a shared one is Android's when it has Android's note (`.note.android.ident`, which the NDK's `crtbegin_so.o` puts in everything linked for Android, Go's libraries too), never by what it needs (musl's C library is `libc.so`, as Android's is). A static one has nothing that tells (clang makes the same objects for both), so an ELF archive is Linux's, and Android games bring shared libraries: no folder or name marks one as Android's. An Android build links the game's library for each CPU to that CPU's shared libraries, and they go in the APK and the bundle beside it (`lib/<abi>/`), stored on 16 KiB pages, each under the name the game's library asks for it by: its soname, or its file's name when it has none, which has to be `lib<name>.so`. tide warns about one whose segments are aligned for less than 16 KiB, as phones with those pages may not load it; when an Android link fails, it names the ELF libraries for that CPU it left out as Linux's. A web program may only leave undefined what the prebuilt platform layer does, which are GL's functions, from the page: tide lists them from that library's objects (`page_imports_flag`, `wasm_archive_imports`) for the linker, so any other function nothing defines fails the link, where allowing them all would leave it to fail when it's called. After a failed web link, tide says which of the game's extern functions nothing defines.
- The package is everything installed as the `tide` component: `bin/` (tide, tidels), `include/`, `src/engine/`, `src/packages/` (the packages that come with tide, as sources), `lib/native/`, `lib/web/` and `lib/android/<abi>/` (the prebuilt platform layer, one library each, with GLFW's objects inside the native one), `wasi/` (wasi-libc and the compiler runtime for wasm), `mingw/` (on Windows: MinGW-w64's headers, C runtime and compiler runtime), `lib/clang/` (clang's own headers), `web/` (the page and `tide.js`), `editors/`, `licenses/` (a folder for each third-party work it redistributes: see Third-party licenses in the package, 2026-10-21) and `VERSION`. It comes from the `package` preset (clang and lld built in, static C runtime on Windows), the `web-package` preset, the `android-package-arm64` and `android-package-x86_64` presets and, on Windows, the `mingw-release` preset, put together by `cmake -DNAME=tide-windows-x64 -P cmake/package.cmake` into `build/dist`. Anything tide needs at build time must be installed into the package; a game build can't see this repo. The same goes for `tidels`, which reads the engine headers from the package's `include/`. Nothing else goes in: games are C, built for this machine's CPU, Android's and the web, so the package leaves out C++ headers, Windows' interface definitions (`.idl`) and type libraries, and clang's headers for other CPUs, GPUs and the runtimes it doesn't ship (`cmake/LLVM.cmake`, `platform/CMakeLists.txt`). Every C header of the C libraries stays, since a game's C can include any of them.
- **Engine private headers ship with its sources** (fixed 2026-10-10): `engine/CMakeLists.txt` installs `.c` and `.h` files from `engine/src/` together, preserving relative subfolders. Installing only `.c` files left the new `skinned_cache.h` behind, so an installed game could not compile even though repository builds passed. Before compiling, `cache_test.mjs` checks every installed engine source/private header's quoted includes against its own folder and the installed public includes. Removing `skinned_cache.h` from a private stage makes this gate fail with the missing include named. An isolated CMake configure/install populated the header, and that private stage passed installed native/web game compilation and stripping checks in debug and release; Node syntax and diff checks passed.
  - Private headers under installed `src/engine/` also enter the engine's aggregate object stamp. Changing one rebuilds engine objects without invalidating generated game, launcher or user C objects; unchanged private headers reuse those engine objects. `cache_test.mjs` changes only `skinned_cache.h` to require a compiler error, restores it, checks that only engine objects rebuild and checks the next unchanged build reuses every object, on native and web. This covers header-only updates with unchanged compiler/public-header/source stamps. The regression failed on the previous packaged CLI; the rebuilt embedded-LLVM CLI passed the full native/web cache, stripping, parallel compilation and debug/release web-watch cases from a private stage, plus Node syntax and diff checks.
- `editors/tide.vsix` is the VS Code extension (`tools/tide-vscode`), for VS Code, Cursor, VSCodium and Windsurf. Only the `package` preset builds it (`TIDE_EDITOR_EXTENSIONS`), since it needs Node's npm. The installers run `tide editors`, which installs it into every one of those editors it finds, and `tide upgrade` updates it in the editors that have it.
- The JetBrains plugin (`tools/tide-jetbrains`, Gradle and Java; Marketplace ID `io.github.blenminer.tide`) ships in editors/tide-jetbrains-installer.zip with pinned LSP4IJ for offline installation by `tide editors`. Upgrades replace Tide where already installed. CI verifies the separate Marketplace ZIP and publishes stable/nightly channels; Marketplace installations and their updates remain available independently. See Bundled JetBrains installation above.
- `tide build --android` (`compiler/cli/build.c`, `android.c`) builds the game for both of Android's CPUs, as `libgame.so` with `lib/android/<abi>/`'s platform layer, and makes the app with `apk.c`, signed with `~/.android/tide.pem`, or the key `TIDE_ANDROID_KEY` names (made once: a phone only updates an app with the key it was installed with, and it's the user's upload key on Play); `--release` makes the bundle too. The ID is the game's `appId` setting, or `dev.tide.<name>`, which `--release` refuses, and the version people see its `version` setting (1.0 without it). The version code is the minutes from 2020 to now (`SOURCE_DATE_EPOCH`, where it's set), so every build updates the last with no number to keep. `tide run --android` installs it with adb, starts it with the session's flags (the intent's `tide.args`, which the backend gives `main`), and shows what it prints (`adb logcat -s tide`) until it ends. Saving a game's file builds the app again, installs it and starts it over (the match with it: no code is swapped into a running app yet). The NDK and adb are the ones Android's tools installed (ANDROID_NDK_HOME, ANDROID_NDK_ROOT, ANDROID_HOME, ANDROID_SDK_ROOT, or where Android Studio puts them), or else tide fetches them once from Google, pinned and checked against Google's SHA-1s, after the user accepts Google's license (or `TIDE_ACCEPT_ANDROID_LICENSE` is set; editors' runs pass on what's typed, and set `TIDE_INTERACTIVE` so tide asks there though its input isn't a terminal), into `<root>/android/`: only the NDK's C headers, its libraries for Android 10 and its compiler runtime, unpacked by tide's own unzip (`unzip.c`: Linux's tar can't read zips). tide's built-in clang has X86, AArch64 and WebAssembly, and lld's ELF driver, on every host.

## Authored prefab inputs (2026-10-12)

Folder discovery and watched source stamps include .tprefab alongside .tide,
under the existing game/package boundaries. Both extensions are sorted together
in the same canonical source list; prefab files do not create a second project
root or dependency graph. The CLI package test rejects an invalid unused prefab
and accepts its corrected tree, exercising actual folder discovery. Watching
rebuilds the normal game; there is no automatic rewrite of existing live instances
and no tide open/native-editor command yet.


## Pinned launcher option forwarding (2026-11-02)

main.c selects build/run/schedule/shaders before version-specific validation,
including hidden-run restrictions. A bounded discovery pass skips known option
operands, including size/source-overlay pairs and optional host ports. Unknown
options stop discovery: preserve the folder already found, or current directory.
An explicit folder must precede newer options whose operand counts the launcher
cannot know. The selected executable receives original argv and cwd and owns
validation. No-pin/same-version/source builds retain the normal parser/errors;
Unity exclusion, version probe and damaged-cache recursion protection are unchanged.
Keep discovery arities in step with new parser options.

versions_test.mjs exercises a real selected executable accepting unknown options,
explicit folders from a different cwd, known options before the folder, implicit
cwd, quoted values and a known value rejected by the launcher's validator.
Windows ASan CLI builds pass; versions, run_exit, help and hidden CTests pass.
The --web dispatch case uses the same host CLI; no WASM runtime code changed,
so separate web runtime tests are not applicable. Other host OSes remain CI coverage.
Old installed launchers require upgrade or direct cached-executable invocation:
a project pin cannot repair released launcher binaries. No external project or pin
was edited.
## User symbols in handwritten C/C++ (2026-11-06)

Mount & Rifle reported that --define affected Tide but never its C files. queue_c_files now passes each selected symbol as a separate -D argument (value 1) to handwritten game and package C/C++. This includes the existing default-debug DEBUG entry. Automatic PACKAGE_ symbols remain Tide-only. Generated code, host launchers and engine sources receive no user macros; engine-object reuse is preserved. The path is shared by native, web, Android and reload builds.

The handwritten-source stamp contains exactly these additional arguments. A symbol used only by trusted C also enters the external simulation fingerprint: changing it must change compatibility even if no Tide #if reads it. No new symbol syntax or runtime API is introduced. User docs/help describe C/C++ forwarding.

Validation: Windows Clang 23.1.2 ASan CLI/helper builds and help, build_overlap and build_report pass (3/3). The controlled-compiler regression checks game and package .c/.cpp macro arguments, no macros on engine/generated sources, a zero-compile unchanged rebuild, and removing a C-only symbol recompiling all four handwritten sources while retaining engine objects and changing the simulation ID. This validates the common host-side command/cache path, without a game or browser launch; Android/other OS execution remains CI coverage. The integrating task ran the production docs build; the added CLI wording changes no docs structure.
