# Benchmarks

## O2 versus O3 on the external stress test, 2026-10-12

The owner requested an optimization-level benchmark after approving optimized
engine code in development builds. At `83ccb219`, identical unoptimized caller
objects exercise the unchanged 2,500-character private FBX fixture against
`-O2 -g` and `-O3 -g` engine libraries, retaining assertions, no fast-math and
no FMA contraction. Five alternating process pairs per platform run 30 warmup
and 300 measured 60 Hz manual-world frames, covering idle/run/jump. All twenty
rows and 6,000 measured frames are kept in
[optimization-levels](../bench/results/optimization-levels/README.md), with
input/source/library/binary identities, scripts, summaries and core-test logs.

On Windows/Ryzen 9 7900X/Clang 23.1.2, median-of-process-mean view recording
is 20.615984 -> 21.646871 ms: O3 is 5.00% slower, and every native pair is
slower (median paired ratio 1.0468). Node 24.16.0 WASI is 28.705785 ->
28.675940 ms (-0.10%), within observed variation. Actual combined tick/hash/
view/preparation intervals are native 20.989503 -> 22.023309 and WASI
29.257243 -> 29.227485 ms. This workload gives no reason to change O2;
production defaults remain unchanged. GPU, display wait, interpolation
snapshots, rollback, networking and Rooms UI are excluded; no FPS is inferred.

The named gate/90-second quiet guard waited through eight detections of other
build/test work before runtime timing. No competing work was detected during
measured processes. Other desktop activity and clocks remain uncontrolled.
A subsequent five-pair cold-engine compilation batch holds the gate and
checks before/after each build; twelve compiler jobs, warm tools/filesystem,
configuration/caller compilation/final links excluded. Medians are native
1.487 -> 1.508 s and WASI 1.278 -> 1.344 s; these small noisy
differences do not establish a general compilation penalty. Earlier setup
build rows precede the quiet guard, are retained and explicitly excluded.

All runs match world hash `818c38d6c6daad37` and accumulated active integer
palette/influence texture hash `aef3539b6e00d762`, with state mask 7, 2,500
characters, one draw command and two active integer textures. Separate O3
core-test builds with optimized callers pass 435/435 on native and WASI,
including unchanged exact math/random/pose pins. The harness builds warning-free
on native/WASI and the analyzer verifies every runtime/compile pair and digest.
No all-platform O3 rollout, full-suite rerun, CI dispatch or deployment was
performed for this measurement-only work.

## Production retained assets and transfer checks, 2026-10-11

The owner asked to finish integration and put it on dev after the standalone experiments below. Production now retains stable immutable content across field copies/snapshots/reload and detaches on write, including ordinary asset values. Already-owned reset storage is reused; same known-ID reassignment does no work. Production networking negotiates exact pack identity, sends known references or sparse asset-relative chunks only when smaller, and retries self-contained state when decoding fails. Raw saves include each retained payload once. This supersedes the earlier experiments' *production integration gaps*; their timing claims and deferred adverse-workload timing gate remain separate.

The unchanged `D:/Projects/Test` 100-character project runs 1,200 deterministic ticks and prepares one 100-instance frame. Native and WASI each retain one 114,224-byte animation payload with 100 leases, in a 6,480-byte match heap. Self-contained raw world bytes are native 128,440/web 128,392 (the local save header still includes platform storage layout). Known-pack whole-world transfer is 6,258 bytes versus 120,482 self-contained: 94.8% fewer bytes. Simulation hash `b63eb72b0cc75f1c`, prepared palette hash `ab1b21c3dd4b57f1` and both packet hashes agree exactly. Both platforms decode each other's packets, including full packets after unload. A fresh native hidden three-frame capture also renders the project. These are structural sizes and exact correctness checks, not FPS, CPU timing or real join traffic/latency measurements.

The machine remains busy with other work; the owner deferred timing tests. No production speedup or ordinary-container overhead claim follows from this run. The earlier read-only prototype gain is encouraging, but production timing of cold detachment, owned reset/edit, read-only reassignment, hashing, views and encode/decode remains pending. Comparing both candidate encodings costs CPU only on eligible asset-origin transfer pages; end-to-end measurements must include it. Sources, structural rows and reproduction are in [asset-production](../bench/results/asset-production/README.md). Full native/web and docs validation results are recorded there once complete. No CI, sanitizers or other desktop/mobile platforms were run for this integration.

Production integration validation above passes all 2,189 native and 2,158 web debug CTests, including browser/GPU coverage, plus the docs build. Initial full-suite failures exposed a local-borrow lifetime issue (retired-pool lookup) and the invalid grid-header scan; both are corrected and covered. Descriptor and portable-wire hash pins were updated only after native/WASI agreement. Later dev integration validation is recorded separately.

Integration with newer dev (`e377d0a9`, optional fields/function task starts/request timeouts) builds every native/web target warning-free and passes 112 native plus 104 web overlapping CTests, including the new features, asset/container ownership, sessions, migration/reload and ordinary sand/gallery controls. The docs build passes again. The asset fixture now stores, reads, clears and retains an asset-backed optional list through snapshots/transfers. Descriptor authentication uses a cached sum/count updated only on lease changes, preserving all pinned hashes; heaps with no asset dictionary avoid the new registry validation allocation/walk. The private project check rerun keeps every structural number and simulation/palette/packet hash unchanged, including both foreign packet decodes after unload. Its source/binary/input identities are refreshed in the artifact directory. A later clean merge of unrelated camera/prototype/docs work does not require repeating these checks.

## Asset-relative value encoding, 2026-10-11

The owner asked to try asset IDs plus changes for saves/transfers, especially
new connections. A standalone C byte-value codec registers the exact immutable
baseline by SHA-256, encodes an unchanged known reference by ID, and uses the
existing delta codec's XOR/unchanged-region compression against the asset for
owned values. Full encoding uses that same codec without a baseline. The smaller
complete full/patch frame wins, including ID overhead. A receiver with that exact
asset version needs no prior world or edit history, reconstructs ordinary owned
mutable bytes and verifies their hash; missing/wrong IDs fail and the independent
full form works without an asset. Production compiler/runtime/protocol, installed
package and user game are unchanged; this is no approved production wire format.

For the first canonical `movementAnims` model blob in the owner's frozen pack
(114,224 bytes), existing compressed full encoding is 107,471 bytes. An unchanged
known reference is 37, owned unchanged 50, one-byte edit 59, and 64 sparse edits
441 (full 107,482). Replacing 4 KiB with deterministic noise is 4,160 versus
107,516; a constant-XOR block compresses to 300 versus 107,247. Dense replacement
uses full at 114,383 bytes, and zeros use full at 158. Appending 257 repeated bytes
is 313 versus 107,478; truncation is 55 versus 107,298. Middle deletion shifts the
remaining bytes and still needs 57,012 versus 107,737: no edit-script compressor
is implemented. The 16-byte reference control is adverse (37 versus 35 full),
while its owned form chooses full. These are exact value-frame bytes rather than
raw-size ratios, whole-world/save sizes, actual join traffic/time or FPS.

Warning-free native/WASI builds and all 38 value cases pass: twelve workloads on
each of 1 MiB noise, repeating-pattern and private animation-byte fixtures, plus
two tiny-value controls. Every reconstructed byte and logical length is checked,
receiver edits leave the asset unchanged, and complete output rows (including
encoded-message SHA-256s) agree across platforms. Each executes 11,252 rejection
assertions for cut frames, trailing bytes, invalid modes, corrupt result hashes,
missing/wrong versions and an oversized logical length; failures preserve caller
output/guards. An initial fixture decoder used uninitialized output, contrary
to `tide_delta_get`'s zeroed-buffer contract; the growing-value check exposed it,
and the final decoder uses `calloc`. No timing evidence came from that run.

The owner has deferred runtime timing measurements while the machine is busy.
No clocks were recorded in this batch. The optional guarded timing runner and
analyzer are ready, with alternating encode/decode order, hashing/compression/
allocation inside timing, and exact message/value checks outside it. Encoding
both candidate forms may cost more CPU; existing eligible joins already reuse
initial-world pages, so an end-to-end comparison remains necessary. Persisted
provenance, stable registry/lifetimes and reload, exact world/allocator/hash
reconstruction, save compatibility, missing-asset recovery, nested/typed values
and all mutation paths remain integration gaps. No production suites, browser/
GPU, other platforms, sanitizers or CI were rerun for these standalone artifacts.
Sources, raw rows, input/library/binary identities, byte-size table, verifier and
reproduction are in
[asset-relative-delta](../bench/results/asset-relative-delta/README.md).

## Owned storage reuse after asset writes, 2026-10-11

The owner asked to investigate the asset-sharing prototype's bulk reset/edit
regression. The naive policy releases an owned list on asset assignment, then
allocates another on the next write before the pending old blocks are flushed.
A scoped candidate copies the asset into an already-owned destination through
the existing `tide_list_set`; empty/still-shared destinations retain the asset
reference and detach on first write. Ordinary owned sources are unchanged.
Only known-asset assignment adds an ownership check; there is no data comparison,
new runtime entry point, allocator/layout change or production storage change.

Instrumented native 16 KiB reset/edit runs confirm the mechanism: naive has
192 pending blocks after assignment and 6,291,456 dirty bytes before hashing;
baseline/reuse each have zero pending blocks and 3,145,728 dirty bytes. Reuse
restores the original 3,146,600-byte heap high-water mark from naive's 6,293,096,
with original heap hash and identical independently checked final values.
These diagnostic clocks were collected under competing work and are not speed
evidence. All 27 WASI checks pass across three policies and nine workloads,
including 4/16/64 KiB reset/edit, fresh first-write, ordinary owned copies/edits
and the unchanged 100-character grid. Every retained snapshot is hash-checked
before/after mutation; assets and every list's expected values are checked.
Native diagnostic values/layouts agree with their three WASI counterparts;
grid semantic/palette outputs agree across all WASI policies.

After continuous work in other checkouts caused a 45-minute quiet-guard timeout,
the owner explicitly deferred timing/tests to later and requested merging this
experiment. The restarted timing runner was stopped; neither attempt produced
a comparison row. No speedup, ordinary-list overhead or preservation of the
previous grid timing gain is established for this candidate. The rotated
81-row native comparison, full native/WASI comparison and timing analysis remain
ready to run later. The existing correctness/mechanism/export verifier passes.
No full production suites, browser/GPU, other platforms, sanitizers or CI were
rerun for these standalone artifacts; production code is unchanged.

The policy keeps once-edited destinations owned even when later repeatedly
assigned assets without editing. That retains original copy/hash costs instead
of recovering naive sharing's read-only benefit; `written-then-read` is included
in the deferred comparison. Stable identities/pack lifetimes, reload, portable
reference packing, nested containers and all mutation paths remain prerequisites
for a production sharing implementation. This is a measured allocation-mechanism
fix in a prototype, with its runtime performance gate still pending. Sources,
checks, diagnostic counters, raw identities, reproduction scripts and the
checks-only verifier are in
[asset-owned-reuse](../bench/results/asset-owned-reuse/README.md).

## Asset sharing ownership gate, 2026-10-11

The owner approved considering known asset sharing with detach-on-write for
ordinary asset values as well as immutable animation data, and required proof
before adopting the broader storage change. A scratch generated-code/C-wrapper
experiment retains current pack handles until writing; no production compiler,
runtime, installed package or user game source is changed. It measures ownership
placement, not a complete stable-reference/reload implementation.

Three alternating release pairs of the unchanged 100-character game, with
one pre-tick snapshot retained, reduce the median of process-mean CPU
tick+hash+view/preparation sums from 4.139521 to 1.023344 ms (75.3% lower).
Every pair improves, with ratios 3.87x/4.06x/4.16x. Tick/hash/views are separately
1.633971/0.879640/1.625910 -> 0.010648/0.000272/1.011959 ms; retaining the
snapshot separately is 0.022408 -> 0.000112. Packed native world size is
13,117,656 -> 7,256 bytes. All game/palette digests agree across both variants
and native/WASI. Raw world hashes intentionally differ across representations.
This excludes GPU rendering, networking, restore/replay, Rooms UI and session
startup; no FPS claim is made.

The corrected ordinary-container controls include snapshotting, cleanup before
hashing, cold detachment and bulk repeated asset reset followed by editing.
Cached owned-edit total is close (0.087626 -> 0.088710 ms), with writes about
1.5% higher; cold first-write totals have mixed pairwise changes and essentially
equal medians. Bulk reset-and-edit is 1.93x slower (0.256220 -> 0.493319 ms),
with doubled heap high-water size while old blocks await the batch flush.
This fails the blanket rollout gate despite the read-only gain. Sharing known
unchanged data is supported; a complete general candidate must address or
explicitly justify that adverse workload, preserve stable identities/value
semantics through reload/snapshots, cover every mutation path and remeasure.
No general container-storage feature is landed by this experiment.

All 66 valid comparison rows, the 30 initial rows and fixture correction,
strictly compiled native/WASI checks, source/binary/library/input identities,
reproduction scripts and verifier are retained in
[asset-sharing-prototype](../bench/results/asset-sharing-prototype/README.md).
An initial C fixture wrongly read a byte asset as a float list; the native/web
digest mismatch exposed its overread. Its mutable-edit results are invalid,
explicitly excluded and retained for audit; both corrected experiments were
remeasured and the float results independently checked. Final comparison runs
each followed a complete 90-second quiet window with no detected competing
build/test/benchmark work. Other desktop activity/clocks remain uncontrolled.
Full production suites, browsers/GPU, other platforms, sanitizers and CI were
not rerun for these standalone experimental artifacts; no production code
changed.

## Owned key reductions and validated binding, 2026-10-11

The owner's unchanged 100-character `D:/Projects/Test` scenario now uses
vectorizable complete validation for owned animation keys and private node
readers after both separate-binding node tables pass. There is no retained
proof/cache, skipped content validation, ownership change or interpolation
arithmetic change. This builds on, rather than replaces, the earlier separate
export binding and owned-channel-span improvement below.

Three alternating release pairs, after a complete 90-second quiet period,
exercise idle/run/jump over 30 warmup and 300 measured manual-world frames per
process at 60 Hz. CPU view recording plus serial palette preparation falls
from a median of process means of 2.765558 to 1.495396 ms (45.9% lower, 1.85×).
Before means are [2.649967, 2.774680, 2.765558], after [1.495396, 1.478381,
1.540453]. All six processes have 100 characters, one mesh command, 5,689,760
pack bytes, 13,117,656 packed-world bytes, state mask 7, final world hash
`10175709146634639136` and accumulated palette/influence texture hash
`5069683498435541530`. No competing builds/tests/benchmarks were detected during
the measured processes; the guard reset while waiting for other work. Other
desktop activity and clocks were not controlled. Tick/hash are observed
separately, without a speedup claim. This excludes GPU rendering, networking,
rollback, Rooms UI and session startup; no FPS claim is made. The longer run
and newer pack distinguish these rows from the prior short comparison below.
Raw rows, harness, identities and reproduction are in
[owned-animation-scans](../bench/results/owned-animation-scans/README.md).

Full warning-free native debug 2,136/web debug 2,107 suites pass, including
exact pose bits and OpenGL/WebGL/WebGPU checks. Both complete runtime test
executables pass again after the final test-only tail-length addition.
The local installed runtime received only these performance hunks, retaining
its older drawing interface; a checkout-built harness using a separate library
compiled entirely from installed sources/headers confirms matching world and
palette hashes across all three states. Project sources remain untouched.
Animation assignments still create large component-owned match data, with
copying, hash, snapshot and networking costs. This improves the existing path;
state/time plus shared asset selection in the view still avoids those copies.

## Owned separate-animation grid, 2026-10-11

The owner's `D:/Projects/Test` assigns an `Animation` component field every tick and samples it in a view. A scratch 10×10 grid exposes repeated key-span validation in this owned-model path, independently of the previous default-pose binding rejection. `channel_valid` now scans the already checked spans once, retaining finite/nonnegative/strictly increasing time checks and finite checks on every value/cubic tangent before mutation. Sampling arithmetic, state bytes, hashes and draw counts are unchanged.

Three alternating release pairs, after over 90 seconds without our builds/tests, measure 10 warmup plus 20 timed frames per process. Both variants include the new binding policy; only owned-channel validation differs. Median of the three process means for CPU view recording plus serial palette preparation is 6.711750→3.463030 ms (48.4% lower); individual before means [7.085040, 6.711750, 6.219630], after [3.463030, 3.619675, 3.433880]. This short CPU-only test excludes GPU rendering, networking, rollback and window/session startup; it is not FPS or an isolated binding-cost measurement. Tick/hash timings do not establish a separate improvement. Every run has 100 entities, one mesh command, 13,117,656 packed-world bytes and final hash `13602177110211490499`. Raw rows, harness and identities are in [separate-animation-grid](../bench/results/separate-animation-grid/README.md).

The large world remains: assigning clips to component fields creates owned canonical model bytes per entity, then tick hashing/snapshots/networking process them. Keeping only animation state/time in match components and selecting the immutable asset clip in a view avoids this storage pattern; no new asset-handle language semantics or runtime storage system was introduced. Hidden manual-world native captures verify run/jump drawing, but do not measure a 100-entity session join. Full native 2,079/web 2,050 debug suites and docs build pass; the existing exact pose hash is unchanged. The owner's project sources remain untouched.

## GPU-resident clip experiment, 2026-10-11

At the owner's request to try a larger animation speedup, `bench_skinned --modes gpu` adds an opt-in visual experiment using existing shader/list/mesh APIs. Untimed setup samples the unchanged one-second, 64-bone, four-influence fixture at 60 Hz into a 245,760-byte retained palette table. The vertex shader interpolates adjacent final matrices and applies each character's independent phase, preserving source geometry, lighting, viewport and coverage. This is a visual approximation of CPU local-TRS/quaternion sampling, not a new language/engine feature or an IK implementation. Default runs keep the original four modes. `--subframe 0..1` supplies fractional-tick rendering time to any selected mode; `--verify-gpu` checks every vertex at 240 phases when GPU mode is selected, outside measured intervals. CPU gameplay/procedural Pose behavior is unchanged.

Three alternating same-binary CPU/GPU pairs on Ryzen 9 7900X / RTX 4080 SUPER / NVIDIA 595.79 / Clang 23.1.2, release deterministic flags, 20 warmup/100 measured frames, repeat one, use a half-tick rendering offset and all distinct character phases. At 10K, median-of-process-medians recording is 25.0526→2.36885 ms, submission 6.1343→0.0322, GPU 13.6929→7.11629 and synchronized readback total 43.5922→9.9702. Median paired recording/submission/total speedups are 10.58×/184.40×/4.37×. At 1K, recording is 2.45715→0.21935 ms (11.04× paired), submission 0.4475→0.0076 and total 4.3982→1.33705. Warmed texture uploads become zero from 4.096/40.96 MB per frame; draws including clear become two from two/eleven. Every experiment frame asserts residency and batching; all 12 rows receive 100 GPU samples. CPU/GPU overlap and readback synchronization preclude a pipelined-FPS claim. GPU intervals include uploads/batching, so they do not isolate shader arithmetic. Source triangles remain 60.48 million/frame at 10K. One-time host table/geometry preparation is about 0.6 ms, excluding renderer upload/pipeline setup.

Full warning-free debug builds and native 2,079/web 2,050 CTests pass. New OpenGL/WebGL/WebGPU checks exercise interpolation and CPU reference error bounds: maximum model-space vertex deviation 0.001320204 and unit-normal vector difference 0.00160562643 over 240 phases, identical native/web results, below the explicit 0.01 bounds. These are CPU evaluations of the approximation, not universal animation guarantees or direct GPU vertex readback. Existing exact pose/hash and GPU pixel checks pass unchanged. A visually inspected final one-character half-tick capture differs in 123/589,824 RGB pixels, two by more than two levels; maximum edge channel difference is 127. Approximate captures are not asserted bit-identical. Linux/macOS/MinGW/Android/sanitizers and CI were not run. [Evidence and reproduction](../bench/results/gpu-clip/README.md) retain raw ledger rows, source/binary identities, paired ratios/p95s, GPU monitoring, reference output, capture hashes/differences and validation. The guard waited for a complete 90-second quiet period after tests; measured runs had no detected competing work or exclusions. Multiple clips/rigs, large-motion quality, blending, procedural/IK overrides, residency and hot reload remain future work; no generic GPU animation API has been agreed.

## Large-scope rerun and optimization, 2026-10-11

All nine versus-C simulation fixtures, Sand/Gallery sessions, drawing and skinned rendering were rerun three times on the Ryzen 9 7900X / RTX 4080 SUPER / NVIDIA 595.79 / Clang 23.1.2 machine at `969e1406`. Baseline rows are `scope-baseline-r1/r2/r3`, plus `scope-baseline-steady-r1/r2/r3`, in the existing ledgers. Independent C digests and session world hashes agree. Both animated-scene scale and multiplayer simulation scale are priorities for this pass.

Forced-inline `tide_map_find` specializes fixed generated kinds without changing storage/hash/probe/order or snapshot bytes. Three alternating frozen-binary pairs reduce whole lookup-fixture tick time by 11–13% at 16K and 65K keys. One-thread steady 16K is 1.979→1.735 ms; 1,024 replacements is 1.799→1.593 ms. At 65K these are 9.099→8.031 and 7.149→6.330 ms. Offered twelve workers give similar results; shared map writes remain serial. Steady 16K still costs 10.25× handwritten C, so the performance bug remains. Plain optional inlining and a private generic cached reader were tried unsaved and rejected before the retained forced-inline candidate; no timing claim relies on those exploratory runs.

Explicit skinned draws now reuse the existing unique current pack proof identity to avoid repeating comparisons of unchanged model dependencies; different identities retain exact owned-byte comparisons. At 10K characters, shared-pose recording falls 4.980→3.960 ms (21.5% median paired reduction), independent-pose recording 24.683→23.471 ms (4.7%). Independent submission remains about 6.06 ms. At 1K, paired view reductions are 14.9% shared and 4.2% independent. Static/rest controls gain no meaningful whole-frame time; static recording rises by 0.00685 ms at 1K and 0.03110 ms at 10K. All draw/upload counts remain equal, every case gets all 100 GPU timer samples, and five matching before/after captures have identical PNG bytes. Native readback totals synchronize frames, CPU/GPU intervals overlap, and no pipelined-FPS or GPU-speedup claim follows.

The 10K independent case still uploads 40.96 MB of palettes and draws 60.48 million triangles per frame, with 11 calls including clear, no LOD/culling and no match simulation. It is not established as a 60 Hz game workload. Sampling, pose preparation and uploads remain performance targets; animation LOD/visibility decisions need the owner and a representative game. The simulation rerun still shows Life 7.49× C (one thread), Events 7.33× and nested buckets 6.06×. Multiple retained snapshots with restore/replay and larger-player-count session comparisons remain coverage gaps. Existing eight-player Sand/Gallery joins/replay/snapshot tests converge without resyncs; Gallery's 4.41 MB join takes up to 2.917 s catch-up with the configured latency/loss.

Full native (2,078) and web (2,048) debug suites pass warning-free, including new map collision/chunk/wrap/snapshot and skinned equal/changed-reload ownership regressions, unchanged determinism checks and OpenGL/WebGL/WebGPU pixels. Linux/macOS/MinGW/Android/sanitizers were not run; CI was not requested. [Compact evidence and reproduction](../bench/results/scope-performance/README.md) retain baseline/paired source and binary identities, raw rows, medians/p95s, GPU monitoring, capture hashes and validation commands. All measurements exclude competing build/test/benchmark processes after a 90-second quiet guard.

Build notes, moved here from `AGENTS.md` as they were: what's built, what code relies on, the tests and the measurements. `AGENTS.md` has the rules every session needs and the index of these files. The headings below are that file's sections as they were, and "see <Section>" names one of them: the index says which files each is in. What a note calls "below" or "above" may be in another of these files now: search `notes/` for its name.

## Building

### Benchmarks

- `bench/` holds games that stress the engine, played by bots, as targets for what a general engine has to handle: `sand` (falling sand and water that every player paints into at once) and `gallery` (every player has a big canvas of their own, and players keep joining). A voxel world, endless, with blocks placed and broken, joins them once the engine can hold one.
- Each is a program, `bench_<name>` (`--help` lists its options), on a harness of its own (`bench/bench.h`): a dedicated server and bots, each a client of its own on the loopback network, with latency and loss, in made-up time, so a run goes as fast as the machine can take it and repeats exactly. It reports each machine's update times, what they spent simulating, hashing, snapshotting and packing worlds (through the game's `tide_game`), what each machine sends, how long joins take, and the world's size.
- Measure with the `release` preset. The machines run one after another, so a run takes about as long as all of them together.
- `bench_skinned` measures rendering through generated Tide views (2026-10-10): static geometry, omitted rest pose, one animated pose shared across characters, and an independently sampled pose per character. Its reproducible embedded-buffer glTF has 3,072 vertices, 6,048 triangles, 64 skin joints (66 canonical nodes), four positive influences per vertex, and a one-second clip with 17 rotation keys per joint. `bench/skinned/generate.mjs --check` verifies the fixture. Geometry, fragment, viewport (1024 by 576) and summed nominal projected areas match across cases; every character remains visible. A preceding view samples the shared pose once and each local Character has its own drawing view. The custom host resets local state and applies entity spawns in an untimed setup frame before each case. This per-entity view boundary matters for temporary lifetime: a single view containing a large draw loop keeps its intermediate value copies until that whole view returns.
  - Defaults are 1, 10, 100 and 1,000 characters, 20 warmup frames, 100 measured frames and three repeats; CLI selectors and methodology are in `bench/README.md`. CPU generated views/recording, CPU renderer submission, and asynchronous GPU queries are reported separately, with median/p95, GPU sample count, draw calls and texture-upload bytes. Native headless runs synchronize each measured frame with framebuffer readback, timed separately and in a whole-frame total; web readback metrics are null. GPU queries exclude upfront renderer preparation/geometry uploads and readback, but include on-demand palette texture uploads inside shaded draws; they are not pure shader timings. CPU and GPU overlap, so do not add them or treat these isolated frame measurements as pipelined FPS. Unsupported GPU timers are null, never zero. Retained process/GPU memory includes caches from previous cases. `--save` appends per-case/per-repeat records with fixture hash and hardware/compiler/source identity to `bench/results/skinned.jsonl`; web prints JSONL. CTest exercises all four cases at small counts on OpenGL and software WebGL/WebGPU without timing assertions. This corrects the broader upload-exclusion description retained in the original measurement metadata; the measured boundaries and values themselves have not changed.
  - Validation: full Windows debug suites pass 2,040 native and 2,013 web tests, including the new benchmark smoke and scratch-lifetime regression. Release OpenGL also completes all four cases at 1,000 characters and captures every instance. The benchmark exposed missing compiler scratch-use tracking for owned structs; that fix and its regression are recorded in `notes/language.md`.
  - Initial native baseline at `b9faf142`, saved as `initial-skinned-native` in `bench/results/skinned.jsonl`: three repeats of 100 measured frames at each default count/mode, Ryzen 9 7900X / RTX 4080 SUPER / NVIDIA 595.79 / Clang 23.1.2 release OpenGL. At 1,000 characters, median-of-repeat-medians CPU view/recording costs are 0.057 ms static, 133.064 ms omitted rest, 139.063 ms shared animated pose and 151.682 ms independent poses; GPU queries report 0.500, 5.784, 5.829 and 58.685 ms. Clock observations in the last independent-pose case are all 210 MHz graphics / 405 MHz memory / P8, so GPU numbers are observed low-clock shared-desktop costs, not peak throughput. There were no concurrent task builds/tests; other user apps remained running. Metadata and the 15 clock samples are committed beside the runs.
  - At the initial baseline, shared/default poses batch into one mesh draw; independent poses use one mesh draw per character and upload 4,096,000 palette bytes/frame at 1,000 characters (shared: 4,096; rest: zero). The initial study found approximately linear CPU preparation even when GPU work batches, identifying owned-model copies and per-draw validation/hashing as investigation targets. The follow-up at `c06de5f5` removes safe generated model/Pose copies, reuses pack-load validation and canonical hashing for verified loaded models, reads only the header for owned-model rig compatibility, and reuses validated animation keys while retaining the sampler's stricter first-time check. Owned/decoded draws retain full validation/hashing and unproven samples retain full node/channel checks. Standalone `Animations` byte assets do not establish provenance, although deduplicated bytes can share a separately verified model block. Replacement, unload and overlapping geometry writes invalidate loaded provenance; no pose cache or independently posed instancing was added. Full Windows debug validation passes 2,041 native and 2,014 web tests, including unchanged pose hashes and OpenGL/WebGL/WebGPU pixels. `bench/README.md` records timing boundaries, results and reproduction commands.
  - CPU comparison, 2026-10-10: preserved `b9faf142` and tested `c06de5f5` release executables, identical host/fixture/settings, three alternating before/after pairs, all 16 count/mode cases, 20 warmup and 100 measured frames each. All 96 rows, 9,600 CPU frames and 9,600 GPU samples completed. At 1,000 characters median-of-three-round CPU view/recording times are static 0.05660→0.05655 ms, rest 132.549→3.46265 ms, shared 139.436→2.66275 ms and independent 151.600→8.96385 ms. Median within-pair speedups are 1.01×/38.30×/52.37×/16.97× respectively. Draw counts/uploads match in every paired case; the final frame-119 independent-pose captures match PNG bytes and RGB pixels exactly. The instrumented baseline attributes about 44 ms/frame to validation and 52 ms/frame to combined hashing/influence capture at 1,000 characters, arithmetic means over ten frames with timing overhead, separate from the uninstrumented comparison. The gate/process guard followed a 90-second quiet window; two earlier attempts interrupted by external builds were excluded and the original ledger restored. Other user applications remained running. Whole-process 250 ms observations mostly show P8 before and P0 after, spanning 210–2,835 MHz graphics and 405–11,501 MHz memory, so GPU timing changes do not establish a GPU shader improvement or pipelined FPS. `bench/results/skinned-cpu/` retains paired rows, summaries, metadata, clocks, logs, baseline phase evidence, scripts and original/normalized artifact hashes; large binaries/captures remain in scratch and their identities are recorded. The main `skinned.jsonl` retains the original 48 rows plus the 96 paired rows.
  - Integration at `2dffac2d`, merging the image/audio import and format-diagnostic changes from `f23e5569`, builds all native and web debug targets and passes 113 native plus 106 web overlapping checks (219 total): importer/editor units, asset and animation diagnostics/e2e, decode/pack/provenance/borrow/default identity, and OpenGL/WebGL/WebGPU rendering including the benchmark smoke. The optimization's pre-merge full suites above are not repeated. Measurement identities remain `b9faf142`/`c06de5f5`; integration validation is separate from the performance comparison.
  - Second CPU improvement at `625596e8` (2026-10-10): frame-local exact palette memoization shares hierarchy/palette preparation across identical poses; validated sampling reads only binary-search times and selected keys, with contiguous scratch writes and rest initialization; renderer scratch integer textures reuse inactive matching GPU allocations within a frame while protecting every current draw reference. Distinct pose values retain separate captures/draws. Owned/unproven models retain checked fallback behavior, and pose/matrix arithmetic is unchanged. Full Windows debug validation passes 2,061 native and 2,032 web checks, including malformed-data/COW/cache lifecycle regressions and OpenGL/WebGL/WebGPU texture reuse/device-loss pixels. Comparison against preserved `77be2f24` uses three alternating pairs, unchanged settings and 96 rows covering 16 count/mode cases, with 9,600 CPU frames and GPU samples. At 1,000 characters median-of-round-medians CPU views are static 0.05655→0.05940 ms, rest 3.45065→0.64655 ms, shared 2.65005→0.67100 ms and independent 8.74930→4.82380 ms; median paired skinned speedups are 5.36×/3.95×/1.83×. Independent submission is 6.22325→3.26320 ms (1.91× median paired), with optimized round medians ranging 0.62095–3.27460 ms. Draw/upload counts match throughout and final frame-119 PNG/RGB captures match exactly. Instrumented diagnostics confirm one palette/hierarchy computation per rest/shared frame and zero warmed texture creates/frees plus 1,000 updates per independent frame. Those phase means include instrumentation overhead and are separate from paired medians. The gate/process guard followed a 90-second quiet window; one earlier guard-only timeout appended no rows, and no measured process was interrupted. Other desktop apps remained running. GPU clocks are mostly P0 (93/95 observations) with two initial P8 samples, spanning 210–2,835 MHz graphics and 405–11,501 MHz memory; GPU queries include on-demand palette uploads and do not establish shader speedup or pipelined FPS. `bench/results/skinned-cpu2/` retains rows, clocks, both diagnostics, validation summaries, scripts and original/normalized hashes; the ledger preserves its previous 144 rows plus 96 new rows.
  - Integration at `495a5c33`, merging release stripping from `1886fbb9`, passes 52 bounded overlapping CTests: 20 native and 21 web debug checks, plus 4 isolated native RelWithDebInfo and 7 web Release checks. Coverage includes asset-use analysis, skinned/runtime/default/draw-borrow paths and OpenGL/WebGL/WebGPU rendering/benchmark smoke. A standalone automatically pruned game removes a leading unused texture while retaining exactly its used zero-clip SkinnedMesh and separate Animations; a finite native/WASI verifier checks the shifted pack's current model proof, separate clip binding, repeated rest/posed palette reuse and scratch reclamation across three frames. The actual native standalone also completes three hidden frames. Benchmark HOST targets preserve their deliberate automatic-pruning exemption. The isolated release builds leave both recorded executable hashes unchanged. Full pre-merge validation and measured source identities remain those above; the integration adds no new timing comparison.
  - Third CPU improvement at `a10d2f4b` (2026-10-10): draw-list-owned palette pages plus hidden per-instance matrix bases batch compatible distinct poses; deferred pure palette calculations use up to three additional pool workers for sufficient work. Exact dependencies/TRS are captured immediately, sampling remains inline, and saturated shaders/owned or unproven models keep the immediate checked fallback. Full Windows debug validation passes 2,064 native and 2,034 web CTests, with 40 focused skinned runtime tests and unchanged deterministic bits. Native actual-pool preparation observes three worker callbacks and matches serial bytes; the initial served WebGPU round observes caller-only completion, and WebGL exercises NULL-pool fallback. Comparison against preserved `874422c0` uses the same host/fixture/settings and three alternating pairs: 96 rows, 9,600 CPU frames/GPU samples, a 90-second quiet window, no competing builds/tests or exclusions. At 1,000 independent poses, median-of-round-medians view/recording is 4.92020→2.96060 ms and CPU submission 3.33085→0.46480 ms. Palette math moved into submission: the median of per-round sums of those separate medians is a derived estimate, 8.17805→3.42540 ms, with 2.21× median paired ratio; it is not a median combined sample. Submission round medians vary before [3.49680, 0.64595, 3.33085] and after [0.46480, 0.42640, 0.65005]. Rest/shared derived CPU sums remain essentially unchanged (~0.680→0.682 and 0.710→0.711 ms), and their synchronized readback-frame medians slightly increase (~0.072/0.074 ms); no wins are claimed for them or for static submission's small alternating-process variation. Independent total draws fall 1,001→2 including clear; uploads remain 4,096,000 bytes/frame. Ten-pose row padding raises uploads 40,960→49,152 bytes, with 100/1,000 unchanged. Final frame-119 PNG/RGB captures match exactly. Independent synchronized readback-frame medians fall 13.588→4.7883 ms; GPU queries 6.37491→1.07110 ms include uploads and changed batching, not isolated shader arithmetic or pipelined FPS. Whole-process clocks have 65 P0/1 initial P8 observations, 210–2,835 MHz graphics and 405–11,501 MHz memory; other desktop apps remained running. `bench/results/skinned-cpu3/` retains both snapshot identities/source hashes, paired rows, clocks, validation/smoke summaries, scripts and original/normalized artifact hashes. The ledger retains its exact 240-row prefix plus 96 new rows (336 total).
  - Integration at `7c5566bf`, merging web release compression and private-header packaging through `a14dcb08`, passes 26 bounded overlapping CTests: 6 native and 7 web debug, plus 4 isolated native RelWithDebInfo and 9 web Release. Native palette preparation again matches serial bytes with three worker callbacks; served WebGPU completes on its caller, while the independent web-thread fixture passes 240 identical ticks with two participating threads. Staging verifies all 42 engine source/header copies and resolves 94 quoted includes, including `skinned_cache.h`. A standalone pruned game retains its used zero-clip model and separate Animations; native and served WebGL/WebGPU observers verify proven model data, matching-rig clip binding, four instances in one batch with bases 0/1, 21 pixels over three frames, and scratch cleanup. The release page uses the generated minified runtime, with exact decompression parity for separately compressed WASM (546,738→223,883 bytes) and pack (1,744→517 bytes). The actual native standalone also completes three hidden frames. All frozen source and measured executable identities remain unchanged; there are no production changes or new timing comparisons during integration. The scratch report aggregate is corrected from its four successful logged stage counts (6+7+4+9).
  - 10,000-character scaling (2026-10-10): the same preserved `a10d2f4b` executable/fixture measures fresh 1,000/10,000 counts in three processes, all four modes, 20 warmup/100 measured frames. All 24 saved rows, 2,400 CPU frames and GPU samples pass work-count checks, after the named gate/90-second quiet window with no competing builds/tests or excluded attempts. At 10,000, median-of-process-medians views/submission/GPU are static 0.65490/0.02985/4.72422 ms; rest 6.79750/0.04305/8.17869; shared 6.84595/0.04430/8.19200; independent 30.49150/5.08955/13.69750. Fresh 1,000 independent values are 2.95690/0.45375/1.07827. Independent GPU medians span 13.5444–13.9264 ms, with median-of-process-p95 15.4122 ms (not pooled). The derived per-process view/submission median-sum estimate scales 3.40350→35.57640 ms; synchronized readback-frame medians 4.92160→48.28100, not pipelined FPS. Independent drawing uses ten 65,536-matrix pages, ten mesh draws plus clear and 40,960,000 palette bytes/frame; other modes retain one mesh draw plus clear. Sampling remains serial and palette preparation caps at three workers plus caller. Fixed viewport/summed nominal screen coverage makes the 100×100 characters about 4.6 pixels tall, while submitting 30.72 million source vertices and 60.48 million triangles/frame without LOD, culling or match simulation. Whole-process clocks have 134 P0/3 initial P8 observations, 210–2,835 MHz graphics and 405–11,501 MHz memory; other desktop apps remain running with no settings changes. `bench/results/skinned-10k/` retains source/executable/fixture identities, rows, ranges, clocks and reproduction scripts; a final frame-119 capture has recorded PNG/RGB hashes and stays in scratch, with no pixel equality across different grids claimed. The ledger retains its exact previous 336 rows plus 24 (360 total). No production code changes or new correctness suites are needed for this measurement.
  - Fourth CPU improvement at `711af038` (2026-10-10): current pack proof permits private channel reads and preflight of each channel's first key time; all selected channels still pass before output mutation. A four-slot per-call memo reuses binary-search results only for exact short time-array equality, preserving interpolation and both rotation normalizations. Private pose indexing uses the existing wide hash, and synchronous capture borrows contiguous scratch TRS before the retained arena copy. Owned/unproven validation, proof invalidation, palette layout and shader work remain unchanged. Full frozen validation passes 2,067 native and 2,037 web debug CTests, with 42 focused native skinned checks and the unchanged `e5d23f82e5d18f26` pose assertion on native/WASI. Native preparation again matches serial bytes with three workers; WebGL/WebGPU palette and pixel checks pass. An isolated RelWithDebInfo release smoke passes all eight 1k/10k mode cases without saving rows. Three alternating pairs against immutable `a10d2f4b` add 48 rows, 4,800 CPU frames and completed GPU queries at counts 1,000/10,000, all four modes, 20 warmup/100 measured frames. At 10k, median-of-round-medians view recording is static 0.69395→0.70900 ms, rest 6.79140→4.71755, shared 6.69115→5.18675, independent 30.10100→26.70900; independent submission 4.83890→4.90365. Its derived per-round sum-of-interval-medians estimate falls 34.93990→31.61265 ms (about 9.5% lower), with a distinct median paired ratio of 1.115×. Independent view round medians are before [29.8186, 30.1010, 30.3239], after [26.8006, 26.3477, 26.7090]; median-of-process-p95 falls 30.5573→27.1997. Rest/shared derived sums fall 6.82880→4.75485 and 6.73065→5.22530. At 1k independent, view/submission are 2.93510→2.58355 / 0.43780→0.44060 ms, and derived sums 3.36680→3.02755. GPU queries at 10k independent are 13.8803→13.3345 ms and synchronized readback-frame medians 47.3672→43.5442; unchanged shaders/workload preclude a shader-speedup claim, and no pipelined FPS is inferred. Draws/uploads remain exact, including ten mesh draws plus clear and 40,960,000 palette bytes at 10k. Both 1k/10k frame-119 PNG/RGB captures match exactly. The named gate/90-second quiet guard detects no contention or excluded measurement attempts. Whole-process clocks have 252 P0/2 initial P8 observations, 210–2,820 MHz graphics and 405–11,501 MHz memory, with other desktop apps running and no settings changes. The separate instrumented baseline attributes sampling and pose hashing costs; its nested ten-frame means include material timing overhead and are not improvement measurements. `bench/results/skinned-cpu4/` retains sources/executable/fixture identities, exact two-`#line`-path generated-source comparison, rows, clocks, validation, attribution and reproduction scripts. Its sealed text verifier and the earlier CPU3/10k verifiers pass. The ledger preserves its exact previous 360 rows plus 48 (408 total).
  - Fifth CPU improvement at `42a7ba2d` (2026-10-11): omitted-pose draws reuse one owned rest capture per current pack proof identity/frame, avoiding repeated rest construction, TRS hashing and model-table comparisons. Explicit poses retain edit-safe capture. Pack loading also records whether every channel starts at a nonnegative time; current strong proof skips selected-channel preflight, while a false flag retains selected-clip checks and unproven/invalidated models retain full checks. No general read-only-local hoisting or new `mut` semantics is added. Full frozen validation passes 2,067 native and 2,037 web debug CTests, 46 focused skinned/30 pack checks and WASI 406/406 with exact pose bits unchanged. OpenGL/WebGL/WebGPU pixels pass; native preparation matches serial bytes with three workers. Three alternating pairs against immutable CPU4 `711af038`, all four modes at 1k/10k with 20 warmup/100 measured frames, add 48 final rows and 4,800 CPU/GPU samples. At 10k, median-of-round-medians views/submission/GPU are static 0.58940/0.02710/4.42061 -> 0.60380/0.02600/4.40883 ms; omitted rest 4.82325/0.04505/7.72966 -> 0.81085/0.03545/7.76602; shared 5.30550/0.04615/7.93242 -> 5.10155/0.04155/7.46189; independent 29.22050/6.50270/13.96680 -> 24.07120/6.45840/12.97460. Default recording is 5.95x faster; independent recording falls 17.6%, improving in each pair, with before [29.2047,29.2205,29.3683] and after [24.6077,24.0712,24.0413]. Independent median-of-process-p95 is 30.3179 -> 29.1059 ms, a smaller tail improvement. Derived median-of-round view/submission median sums fall 35.86385 -> 30.52960 ms (14.9% lower; median paired ratio 1.1747x); default/shared derived sums fall 4.86260 -> 0.84805 and 5.35595 -> 5.14345. At 1k, default views are 0.46535 -> 0.07720, independent views/submission 2.89725/0.49535 -> 2.34155/0.45605 and derived sums 3.39260 -> 2.80595; shared views are 0.52150 -> 0.53410. Independent synchronized readback-frame medians fall 48.11180 -> 42.11020 ms, default 13.02110 -> 9.02925; no pipelined FPS is inferred and CPU/GPU intervals are not added. Shaders and work are unchanged, so GPU observations do not establish a shader speedup. All four default/independent PNG/RGB pairs at 1k/10k match exactly. The 10k workload retains ten mesh draws plus clear, 40,960,000 palette bytes and 60.48M triangles/frame, tiny projected characters and no LOD/culling/match simulation; sampling stays serial with at most three palette workers plus caller. The final gate/90-second quiet guard has no detected contention, reset or excluded attempt; other desktop apps run with no settings changes. Whole-process clocks have 245 observations (243 P0/1 P5/1 initial P8), graphics 390–2,820 MHz and memory 405–11,501 MHz; these are not per-case clock-normalized results. The initial `000a5986` comparison is retained as an adverse result (10k independent views 27.08380 -> 30.48430), including all 48 rows and lightweight snapshot/validation identities. Its preflight quiet period reset once for unrelated Last Light tests before the full quiet window passed; measured runs did not overlap detected work. Private timer/layout diagnostics show placement sensitivity but do not establish a sole cause or contribute to final timings; the final measurement covers combined code and layout. `bench/results/skinned-cpu5/` seals final and superseded evidence, identities, ranges, clocks, validation and reproduction scripts with raw and normalized text hashes; BOM-marked PowerShell UTF-16 logs are decoded losslessly. The original 408-row history plus 48 adverse rows stays exact as the 456-row input; 48 distinct final-label rows bring the ledger to 504.
  - `.gitattributes` keeps the generated fixture and saved skinned measurement text in LF form across checkouts, preserving fixture reproduction and artifact hashes. The improvement export records original measured bytes separately from normalized committed text, including the expanded JSONL ledger.
- Runs are kept: measuring a change saves its runs (`--save <label>`, named for the change, with `-1024` for sand at that size) into `bench/results/`, a few of each so the noise shows, and commits them, so the files are a history of improvements, setbacks and noise. A comparison with an older build saves that build's runs too, under its own label. `node bench/compare.mjs [benchmark...] [--last <n>]` puts runs side by side.
- Tests run each one `--quick`, which fails when a client's world went wrong, a bot couldn't join, or the server's world ends with another hash than the one in `bench/CMakeLists.txt`, which every platform and thread count has to agree on. Timings aren't checked: machines differ too much.
- Sand is a game people play too: `sand` (`sand.html` on the web) is `sand/sand.tide` with `sand/play.tide`, its views, GUI and rooms, and the mouse through the input's `Sample`, which the benchmark's bots don't call. The docs site shows it (see Docs site).
- A benchmark holds its game as the engine can today, and says where that's not how it should be. It changes as the engine does (sand's cells were a list, then a grid that a chunk system stepped on threads, then 2 by 2 blocks that a parallel loop steps), and its expected hash with it.
- `bench/versus/` measures Tide against C: a concept written both ways, each as fast as its language allows, on the same work (`bench/versus/versus.h`). Each is a program, `versus_<concept>`: particles (an ECS system streaming through a component), wide (the same through a component of a dozen floats, half of which a tick changes, and with `--tagged 1`, by a system that leaves out the entities with a sparse tag, `--frozen` in a hundred of them), sand (`sand/sand.tide` itself), life (a grid stencil), churn (spawning and destroying), events, and nbody (every pair, through a list). Each takes `--kept 1`, which keeps the world before every tick, as a client keeps every tick it predicts: the snapshot shares the world's pages, so the tick writes none of them in place. `cmake --build --preset release --target versus` runs them all.
  - Every way starts from nothing and runs the same ticks, the first of which sets it up, and has to end with the same result, bit for bit: the same work, so the times compare. A digest adds up a hash of each thing a way holds (`versus_item`), so ways keep things in any order. Tests run each one `--quick` and fail when the ways disagree.
  - `versus_lookup` compares map/set hits, misses, score updates and ordered key replacement. Its Tide host reads entries directly from the world heap for the semantic digest: a signed 64-bit insertion-order header, then the primitive key and value, both map and set entries 16 bytes with the key at 8. It uses `tide_map_live` so negative removed-entry orders are skipped; storage IDs and padding are never included in the digest. This mirror must follow tidec's entry layout. Layout assertions pin its offsets and strides, and `versus_lookup`, `_empty`, `_single` and `_steady` compare live keys, values and insertion order against the independent C tables on one and several threads.
  - The C is what someone who knows the machine writes: arrays rather than structs where that's faster, branches turned into selects for SIMD, arrays staggered so they don't share their addresses' low bits (4K aliasing), and threads on the same pool Tide ticks on (`versus_for`), only once there's enough work for them to pay (`versus_pieces`; `--split` and `--quick` always split). A way with a trick Tide can't do yet is a way of its own (`C, lookup`, `C, SIMD`). The C builds with the flags generated code does, so the results stay bit-identical.
  - It times each tick after the warm-up ones, on one thread and then on every core, and reports medians, and how many times as long Tide takes. `--save <label>` adds a run to `bench/results/versus.jsonl`.
