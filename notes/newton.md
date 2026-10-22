# The Newton package

## Arm fixture hash after joint-state additions (2026-10-22)

Nightly run `39837828628` at `d93f8e3e` failed only the arm fixture's
200-tick world-hash assertion in all nine simulation suites: Windows,
Linux and macOS native/web, MinGW, Android and Linux sanitizers. Every
target reported `122d2b87150ba000`. The next run, `39840272210` at
`ae6a0899`, reproduces that same failure in its completed Linux native job.

`4f62aec7` intentionally added `Joint.friction`/`load`, the matching
`JointState` fields and collider friction-model state, and publishes joint
load after each successful step. The arm's whole-world pin still described
the previous package state. Update only that expected hash and its comment;
the physics, game, inputs and behavioral assertions are unchanged.

Before repinning, fresh local native/WASI debug/release builds all reproduce
the same new hash, with only that assertion failing. After repinning, all
four selected Newton CTests pass in each configuration: runtime (63 cases),
package ordering, robot (six cases, including threaded equality and lossy
two-client rollback), and fresh-process join. Validation uses Clang 23.1.2
and Node 24.19.0; logs are under `build/ci-inspection/`. No full-repository
suite or browser/GPU coverage is claimed for this test-only correction.
Other operating systems, Android and sanitizers were observed in the failed
run, not rerun locally. No workflow was manually dispatched or retried.

## Dev integration (2026-10-22)

The 45-file local Newton/MicroDuck delta is integrated with current `dev`.
The existing per-case MicroDuck CTest discovery, platform guard, configure
dependency, 180-second budgets and nonempty-run assertions are preserved.
Fresh Release builds and the focused Newton/MicroDuck CTest selections pass
on native and WASI; the known right-turn acceptance remains explicitly
disabled. Collision export and all 40 upstream manifest checks also pass.
These are focused checks, not a new full-repository-suite claim. The missing
`bench/` limitation recorded below applied to the earlier detached source
snapshot; the complete integration checkout configures successfully.

The recorded `4f7a9110` SHA below identifies the frozen Windows working
bytes used for the earlier experiments. Git normalizes this C++ file from
CRLF to LF without changing any other byte. The normalized repository source
SHA-256 is `b7a046761629b2f30c4f5c7b1d810b6fca97d0df3eba68cd53affee8e980a3e9`.

## Final all-direction boost, stability and performance (2026-10-22)

Final runtime SHA-256 is
`4f7a91102e1e4cd87bc3906885c02d1bb5e3cbb4578b8cee9495dff14abeb4fa`.
It retains the free-root centering and block M×J changes from the validated
fd6f stability basis and adds ordered four-column JᵀP. The far-pair trial was
discarded and is not integrated. The existing fixed
174-collider/353-scene model, released weights, hardware/BAM motors, four
substeps, friction materials and separate CC BY-NC-SA collision-data license
remain unchanged.

**D/right is now the weak direction.** Input sets `turn = -drive`, swapping
learned A/D turning while preserving manual drive/trim behavior. Either held
Shift sets ordinary match `bool run`, selecting signed `runSpeed = .5` for
forward/reverse requests and 1.25 rad/s for yaw, including diagonals. Normal
translation/yaw requests are .4 m/s and 1 rad/s. This follows the user's
all-direction boost preference. The .5 reverse trial fell, and boosted turns
may stumble; boost is a requested velocity/yaw, not guaranteed stability or
real speed. It uses the same physics with no fast-forward or artificial forces.
Seeded recovery success is not general randomized-fall reliability, sim-to-real
fidelity or complete directional turning.

### Current controls and physical measurements

The fd6f stability-basis `build/microduck-run-check/final-test-summary.json` and
`locomotion-final-{native,wasi}.log` record **7/8** on both targets. All nine
emitted final whole-world hashes match; the run comparison emits the W baseline
again. The only failure is strict D/right turning.
Final-source walk/Shift+W checks pass 1/1 per target with the same whole-world
hashes `af41801b0af94572` / `dc74c754ef1b6356`.
`all-boost-jtp4-test-summary.json` records the current source and controls.

| Probe | Result |
| --- | --- |
| W / S, 7 s command | 0.786932 / 0.863201 m; left/right loaded cycles 15/14 and 11/11 |
| Shift+W | 1.155245 m, +0.368313 m (46.8%) over W; 14/14 loaded cycles |
| Shift+W stability / release | Minimum up .994069, height .112773 m; final horizontal speed .004675 m/s |
| Prone / side / supine | One-second upright dwell confirms 1.340 / 2.115 / 1.905 s; each final 2 s upright 100% |
| A/left / D/right | -3.316523 / +.196558 rad; D/right fails the unchanged .25 rad minimum |

Seven supported cases register normally in CTest. The strict **right-turn**
case is explicitly disabled with label `known-limitation`; direct execution
still fails its original assertion. The missing unrelated `bench/` directory
blocks CMake configuration, so there is no clean repository-suite CTest claim.

### Stability correction and performance scope

The adapter anchors a free root's virtual parent at its current body origin,
initializing translation coordinates to zero for each model construction.
Large world-origin coordinates had caused float cancellation of small
rotational inertia during Cholesky factorization. Existing COM velocities,
body-origin poses and world force conventions remain; no public API or
persistent state widens. Three new regressions fail on the old source and
pass on this basis; final backend logs in
`build/microduck-run-check/jtp4-backend-{native,wasi}.log` report
**63/63** on each target. State `be6cce7a3d5e1828` and contact
`445004fca1d80fa4` were observed on both targets before intentional repinning.

The actual close-duck/ball/grab full-scene reproduction failed at tick 1734,
substep index 3 on old source. Corrected fd6f-basis native/WASI hosts complete 4,000 ticks,
both hash `4cb7f65b76088097`. Ordinary W with both ducks and balls active also
completes 4,000 ticks, both hash `658515fe75a71d1d`; headless timings under load
are 1.594 ms/tick native and 3.185 ms/tick WASI. This is bounded regression
coverage, not a claimed 4,000-tick rerun of the final JᵀP source, indefinite
stability or hardware validation. Reverse boost falling or boosted turns
stumbling is a learned-controller capability limit, distinct from the corrected
Newton numerical `Unstable` error.

The six-column mass-times-Jacobian block optimization skips structural zeros
while preserving ordered nonzero arithmetic, with a dense nonfinite fallback.
`build/microduck-perf-audit/RESULTS.md` records the paired native-only frozen
previous-control idle-scene comparison: 3.381 to 1.720 ms/tick, and 1,020 ticks
with all three output arrays byte-identical. It is not universal equivalence,
final-control/browser performance or a WASI speedup measurement.

The final ordered JᵀP implementation uses four independent scalar accumulators,
retains every term in increasing order and the existing tail/finite checks,
and introduces no FMA or reassociation. The separate paired native comparison
in `build/microduck-perf-audit/jtp4/RESULTS.md` times only `nt_step` calls on a
frozen previous-control idle scene: 1.459854 to 1.088080 ms/call (25.47%).
Status and every body/joint/collider output record byte match for 1,020 calls.
One captured 35-body/28-joint/353-collider snapshot also matches native and
WASI SIMD output bytes, hash `d267a59f35210b5f`. That one snapshot is not full
WASI-history proof. Do not multiply the separate 25.5% and older 49.1% runs
to claim a precise cumulative gain or use them as browser measurements.

Browser W/Shift+W/A/R and F8/F7 checks pass without errors and screenshots were
inspected. The final 12-second steady WebGL image
`web-profiler-steady-final.png` shows frame 32.08 ms (31 FPS), tick 2.35 ms,
Newton 1.77 ms, 401 ticks/sec and 12.9 ticks/frame under combined client/server
load. Earlier fd6f steady frame/tick/Newton values were 45.58/2.74/2.35 ms;
the preoptimization snapshot was 86.09/5.23/4.83 ms. These are observations,
not paired benchmarks. Do not claim 60 FPS, fully resolved browser lag or
confound them with controlled native call/tick measurements.

### Final deterministic checks and coverage limits

- Final all-direction boost policy tests pass 13/13 native, 13/13 WASI and
  14/14 actual threaded native, with 1,215 callbacks. All four hashes match:
  `0d537d58ad19871e`, `3666b22b823de44c`, `f4e94516f0c7b212`,
  `d677ec1511e1be3d`. Full/delta sampled inputs cover run false/true/held/release,
  Shift+S -.5, boosted A/D ±1.25 and diagonals. Actor inference/history checks
  include reverse and custom .55 requests; manual drive is unchanged.
- `build/microduck-run-check/all-boost-jtp4-join.log` reports both fresh-process
  native/WASI directions passing, with byte-identical 128,254-byte files,
  SHA-256 `570cc830abd1ca6f0170f31a2d149060cc0027d321791c635e553688a31347fe`.
- On the fd6f basis, native passes 23 manual behavior cases; its old pin alone failed. Actual
  native/WASI 280-tick hash `90ed0aaf9bb442c8` was observed then pinned, and
  focused pin reruns pass 1/1 on both targets. The full WASI 24-case behavior
  suite has not been repeated on the final source. Previous 24-case/browser-grab
  validation below is historical. Full rendered-surface coverage remains
  unvalidated.

## Historical: initial 174-collider model and seeded get-ups (2026-10-22)

The integrated fixed 174-collider-per-duck model preserves the prior W/S gait
and passes the three seeded prone/side/supine get-ups on native and WASI.
**Left turning remains weak, and collider coverage does not match every rendered
surface.** The full seven-case host passes 6/7 on both targets. This is not evidence
of arbitrary-fall recovery, reliable directional turning or sim-to-real fidelity.

### Data, model and license boundary

The frozen production data is
`examples/robotics/microduck/collision_spheres.json`, exported to `collisions.tide`.
Its private predecessor is `build/microduck-walk-fix/fitted-visible-casing/candidate.json`. It has 131
mesh-derived support spheres over 13 nonfoot bodies, 26 internal core spheres
covering every one of those bodies, the original 16 foot-body proxies including
12 marked sole spheres, then one appended procedural trunk-casing core.
The complete scene has 353 colliders. One constant model serves walking, recovery
and interaction. It does not select different geometry for each situation.
Each nonfoot body has two internal cores at least 2 mm inward of the baseline
support envelope and retains the source-inscribed certificate. Sampled support
error of the mesh-derived nonfoot subset is approximately 5.5 mm (maximum 5.542759 mm);
it is not an exact source hull, all-direction surface bound or a calibrated
real-robot casing. The procedural visual core is outside that source-hull
certificate and expands selected support by 6.67484 mm. Feet retain their original approximation. The separately
tested fully fitted 150-sphere model failed walking and is not selected.

The frozen JSON, exporter, `collisions.tide`, provenance and license
files are separate from the XML/inertial converter. **Derived collision data
is CC BY-NC-SA 4.0**, with source attribution and hashes in the example's
`README-COLLISIONS.md` and `LICENSE-COLLISIONS.md`. Raw upstream STL meshes were
read, sampled and converted privately; raw STL files are not redistributed.
Original rendered visuals remain procedural/photo-based and do not use STL
geometry or photo textures. Apache-2.0 XML/inertial data and released neural
weights/code retain their separate license provenance. The old blanket claim
that no upstream STL was read or converted no longer describes collision data.

Source/hash provenance does not establish an exact training collision model.
The actual 17 named training contact-dimension overrides cannot be extended
to all 70 source robot collision geoms. Game self-contact filtering, compliant
contacts/limits, missing backlash/delays and the absent daemon recovery wrapper
remain differences from training and hardware.

The runtime remains `2464103a`; weights, hardware/BAM controller, four-substep
200 Hz physics, 50 Hz inference, Coulomb materials, 55/0.6 stops and 0.4 forward
command scale are unchanged. Gearbox friction still uses successful applied
effort/load, and battery sag still uses separate raw motor history. This
follow-up changes fixed geometry rather than policy weights or episode control.

### Visible lower-trunk coverage correction

Independent review found a visible black-case hole in the 173-sphere candidate:
a ray misses by 7 mm and a 22 mm ball ghosts through it. The new
`microduck_visible_lower_trunk_has_pick_and_prop_collision_coverage` regression
fails all three coverage assertions on that candidate. Source support bounds
and body-level core counts do not establish visual pick/prop coverage.

The 174-sphere model adds one original procedural body-0 core at
`(.004, 0, -.006)` with radius `.0115` m inside the visible black case. This
core is not source-hull-inscribed and changes the envelope; the 26 other cores
retain their source-inscribed bounds. The one constant model passes the private
targeted coverage case 1/1, including the point 2.5 mm inside the case and
the ray hit, and retains the seven-episode 6/7 locomotion result on native/WASI.
This is a narrow static coverage claim. Some visible trunk/head pixel clicks
still miss collider volumes. Successful physical-center/lens grabs and the
targeted regression do not establish complete visual-surface picking coverage.

### Integrated 174-collider measurements

`build/microduck-walk-fix/integrated174-locomotion-{native,wasi}.log` both record
**6/7**, with all seven final whole-world hashes matching, including weak left turning:

| Probe | Measured result |
| --- | --- |
| W / S, 7 s command | 0.774143 / 0.843499 m signed travel, unchanged from the old foot model |
| W / S left/right loaded cycles | 15/14 and 11/10, each requiring >5 mm clearance and touchdown dwell |
| Prone | Upright starts 0.340 s, 1 s dwell confirmed 1.340 s; final 2 s upright 100% |
| Side | Upright starts 1.120 s, confirmed 2.120 s; final 2 s upright 100% |
| Supine | Upright starts 0.905 s, confirmed 1.905 s; final 2 s upright 100% |
| Full A / D | +0.196556 / -3.292314 rad unwrapped yaw; left fails the 0.25 rad minimum |

All three recovery seeds preserve policy history and apply no assistance.
These are measured seeded trials, not a randomized-fall reliability study.
These are the actual installed-source game hosts. Earlier private candidate
results agree, while the older completed matrix below applies to 61 spheres.

### CTest registration and known left-turn limitation

The seven locomotion cases are individually registered in CMake. Six supported
cases register normally; left-turn acceptance is explicitly `DISABLED` with
label `known-limitation`. Its strict signed 0.25 rad assertion remains in the
directly runnable target/full host. There is no `WILL_FAIL` conversion or
assertion weakening: the full seven-case host continues to measure 6/7.
The unrelated missing `bench/` directory currently blocks CMake configuration,
so registration is checked by source inspection and behavior by installed
hosts. This is not a clean full-suite CTest validation.

### Current focused production validation

- Installed CLI native and web builds pass for the integrated 174-collider game.
  The Newton runtime remains SHA-256 prefix `2464103a`, with its existing 60/60
  native/WASI runtime cases; XML/model, neural weights and hardware/BAM control
  are unchanged.
- `integrated174-policy-matrix.log` passes 13/13 native, 13/13 WASI and 14/14
  threaded native, with 1,586 actual worker callbacks. Four cadence-residue
  hashes match: `cc280af1b0570a9e`, `1bd70bbe49487073`, `617201957caf9e6c`,
  `30dd115e59b04f7f`.
- `integrated174-join.log` passes fresh-process native-to-WASI and
  WASI-to-native policy/active-grab joins, with unchanged join-host logic.
  Both 128,411-byte files are identical, SHA-256
  `a4ffd504d3fa71dad82ed162f1fd3c59841b345c93611bc0bef94fb991676647`.
- All 24 retained manual cases are validated. The full WASI host passes 24/24
  in `integrated174-manual-wasi.log`, including the new casing point/ray/prop
  check, grabbing, topple, 200 Hz gait and two-player rollback with ball/child
  grabs, explicit release and zero resyncs. Native passes the 23 behavior
  cases in its full pre-pin run and the focused pin rerun 1/1 after the actual
  native/WASI 280-tick hash `d753ef2f51722611` was observed and then pinned.
  This is split native validation, not a claimed clean 24/24 native full run.
- WebGL and WebGPU W/M/R, F7/F8 smoke checks pass with screenshots inspected
  and no page exceptions or GPU errors. Inspected physical grabs show trunk
  2.4 N / .115 Nm with body movement, head 1.5 N / .051 Nm with tilt, and ball
  .2 N / .003 Nm with lift; releases are captured. Images include
  `web-grab-trunk-physical.png` and `web-grab-head-physical.png`. Some initial
  white-hood/base-edge pixels miss; physical-center/lens regions are pickable,
  while full rendered-surface coverage remains unvalidated.

Logs above are under `build/microduck-walk-fix/`. These focused results do not
claim a full repository-suite pass, arbitrary-fall recovery or sim-to-real fidelity.

## Historical: Coulomb contacts, stops and 61-sphere VelStand travel (2026-10-22)

The preceding hardware/BAM controller and 61-sphere geometry produced bounded
unassisted W/S travel and prone recovery. Side/supine recovery failed throughout
18 seconds, and full-input left turning was weak. That physical host passed 4/7;
runtime and policy-contract passes below must not be presented as all physical
acceptance passing. Physical fidelity and sim-to-real transfer remain unvalidated.

### Runtime and game contract

`ContactFriction { Regularized, Coulomb }` and `Collider.frictionModel` expose
the optional tangential impulse model. The default remains Regularized, with
the existing public material defaults (2000/5/100/0.8) and regularized path
unchanged. Both shapes must opt into Coulomb; a mixed pair uses regularized
friction. Coulomb pair mu is `sqrt(muA) * sqrt(muB)`, zero if either side is
zero. The upstream compliant normal force and normal coefficient averaging
are unchanged; the adapter disables its tangential damping for Coulomb pairs.

The Coulomb circular two-tangent impulse budget is
`substepDt * muPair * normalForce`. Joint friction and contact blocks interleave
through the same upstream H/Cholesky L mass solve, including floating roots
and armature, for 16 fixed sweeps. The saturated full-rank quadratic block uses
a fixed 32-iteration multiplier bracket, without a convergence stopping rule.
The legacy joint-only path retains eight sweeps. Impulses cold-start each
substep; model/contact scratch is local to the step, with no hidden persistent
state or warm start. Finite sweeps do not promise exact global sticking or hard
nonpenetration. Load and collider-force readbacks include contact impulses.

MicroDuck opts all colliders into Coulomb. Sole/floor/ball friction is 0.8;
nonsole duck spheres use 0, with normal stiffness/damping 5600/3 and authored
friction damping 1. Physical joint stops use compliant gains 55/0.6; they do
not clamp coordinates or claim an exact MuJoCo limit mapping. The default
forward-command scale is 0.4 m/s. The 15-body/14-joint model, full inertias,
source XML pins, converter and released policy weights remain unchanged.

Gearbox friction now reads previous successful `Actuator.appliedEffort` and
`Joint.load`, while voltage sag retains separate raw motor torque history
before the effort ceiling. Reset clears requested effort, applied effort,
joint load and motor/filter/cadence histories before controls run on the reset
tick. The root game still uses the hardware/BAM profile, without a specialist
policy or a controller switch to the scratch no-BAM profile.

### Historical 61-sphere measured physical results

`build/microduck-walk-fix/locomotion-final-native.log` records the actual
generated-game acceptance host with the final `2464103a` runtime, unchanged
weights and zero assistance/grabs. Each W/S episode settles for 2 s, commands
for 7 s, then releases for 1 s. A separate idle world supplies drift. Foot
cycles require >5 mm whole-sole clearance plus loaded touchdown dwell.

| Probe | Result |
| --- | --- |
| W | 0.774143 m signed travel, 15/14 left/right cycles, 16.821/16.728 mm peak sole lift |
| S | 0.843499 m signed travel in the backward direction, 11/10 cycles, 19.773/22.116 mm peak sole lift |
| Prone | Upright interval starts 0.470 s after fall; 1 s dwell confirmed at 1.470 s; final 2 s upright 100% |
| Side / supine | Both fail during 18 s after fall; final 2 s upright 0% |
| Full A / D turn | +0.196556 / -3.292314 rad unwrapped yaw; left fails the 0.25 rad minimum |

W/S remain upright and slow after release: mean final absolute body-forward
speeds are 0.001674/0.001420 m/s, versus late commanded speeds
0.139368/0.152486 m/s. This is bounded unassisted travel, not exact velocity
tracking or reliable directional turning. Recovery seeds preserve policy
history and cadence; they do not reset into STAND. Overall **4/7 pass** on
native and WASI. All seven final whole-world hashes match between targets,
including the three failed physical criteria; the prone final-world hash is
`db79dc087a269762`. The final matrix, known failures and reproduction paths
are recorded in `build/microduck-walk-fix/validation.json`.

`build/microduck-walk-fix/xml-rehearsal/RESULTS.md` records a scratch fixed
official CPU no-BAM actuator profile on Newton's current proxies/contact/stops:
HOME + raw action, scale 1, no lowpass, XML +/-10 target clamp, `.55` position
gain, `.96` motor clamp, `.053` damping, `.0018` armature and `.0048` joint
friction. That profile also walks and recovers prone, but fails side/supine
through 18 s. It is not the default game or an exact training replay. Official
MuJoCo CPU reference behavior and Newton's proxy rehearsal differ in geometry,
self-contact, friction and solver/reset contracts. These scratch comparisons
do not isolate a cause. Missing backlash, delays, full collision geometry and
the daemon's limp/pose wrapper remain compatibility gaps, not proven diagnoses.

### Historical 61-sphere focused validation

- Final `packages/newton/runtime.cpp` SHA-256 is
  `2464103ab2fe94816913682643c2a6a2a0e4ccacccf0a07509feb64330d7e24d`,
  including normalized contact eigenvectors and the small-impulse underflow fix.
  All 60 runtime cases pass on native and WASI: original 33, joint-friction 14,
  Coulomb 13. Legacy stock-Newton oracle agreement does not establish extension
  equivalence. Finite-sweep, replay, readback, validation and failure tests are
  distinct from the physical failures above.
- `build/microduck-policy-game-validation/policy-{native,wasi,threaded}-run.log`
  records 13/13 native, 13/13 WASI and 14/14 threaded native. The worker path
  executes 1,531 actual callbacks. All four cadence-residue hashes match:
  `abb4d9aed75dd736`, `98c70347e5692511`, `1b7b53b5c81c36e8`,
  `7ad386f022317524`. Applied-effort friction history and reset clearing are
  covered alongside raw high-back-EMF history, filters, failure, WASD and replay.
- Latest fresh-process native-to-WASI and WASI-to-native policy/active-grab joins
  produce byte-identical 103,117-byte files, SHA-256
  `f971259580c045db6855a4e3e930d8ae2c5db2b528934fb92c30220ca7a567dd`.
- Latest restarted-host WebGL/WebGPU smoke checks pass, including F8 stats and
  F7 profiling. These UI and deterministic-contract checks do not convert the
  4/7 physical acceptance result into an all-pass result.
- The retained native manual host passes 23/23, including two-player grab
  rollback with 3,962/3,920 replay ticks and zero resyncs, gait clearance, reset
  and topple. The updated manual 280-tick hash `4dc3b90fb13136ce` was pinned
  after observed native/WASI agreement.

The full-suite counts, older game hashes, installed-source comparisons and
manual stepping measurements below are **historical milestones**, not a
full-repository-suite pass for the Coulomb/stops/applied-history extension.

## Historical: released VelStand weights before Coulomb (2026-10-22)

MicroDuck now defaults to the original pretrained VelStand v8 model, HF revision
`665ae4eb20bfe13bca1ef4ea6d4bb6b4a096e448`, SHA-256
`2fc7f8ee6acd7d0d3f66a99927b5d5a56589aeed02ec714de329b252d5ccf492`.
The fixed C actor preserves its 61-input normalizer and 61→512→256→128→14
ELU network. It uses ordered float operations and Tide exponential; Python/ORT
are offline export/reference tools, not game dependencies. The original ONNX,
license, strict exporter and independent reference fixtures are checked in.

`policy.tide` owns raw actions, pre-safety filtered targets, previous raw motor
torques, voltage sag and four-tick cadence as match values. It follows the
hardware target profile (.9 scale, .5 head/.7 leg filtering, first inference
unfiltered, ±pi final target clamp), the rounded policy HOME and original
trunk-frame gravity/gyro. WASD supplies desired speed/yaw. M switches to the old
manual controller. Learned mode applies no external balance/lane wrench.

`Joint.friction` is a stopping-effort budget resolved through the existing H/L
matrix with eight fixed PGS sweeps per substep; zero bypasses it. `Joint.load`
publishes final-substep nonmotor load excluding electrical control, passive
damping and friction. Both are ordinary rollback values, with no hidden cache.
The BAM motor code applies current bounds before PWM bounds, saves raw torque
before the separate effort ceiling, and uses that history for battery sag.

Inference and deployment-contract tests pass, but physical transfer remains
unsuccessful: default commands mostly lean, larger commands eventually fall,
and seeded side/prone/supine recovery has not reached standing. No artificial
balance forces or altered weights are used to conceal this. Missing backlash,
delays, full collision geometry/self-contact and different ground friction are
compatibility gaps, not proven root causes. The complete daemon's limp/pose
wrapper is also absent. Earlier manual-sandbox validation below is historical;
do not present its full-suite counts as validation of the learned controller.

## Historical: collision floor and mouse sandbox (2026-10-22)
## MicroDuck CTest case budgets (2026-10-22)

Build run 39800191824 clears the JetBrains/docs failures, but Android and Linux
sanitizers time out the combined tide_newton_microduck invocation at 180 seconds.
All other 2,383 Android and 2,436 sanitizer CTests pass. Ordinary native/web and
MinGW suites pass the fixture; the original optimized WASI invocation passes
24/24 locally in 31.40 seconds. This is an aggregate test-budget failure, not
evidence of a failed physics assertion or a changed deterministic result.

The fixture's CMake now registers every TIDE_TEST as its own CTest, using the
existing executable's name filter. Each retains the 180-second deadline and
must print 1/1 passed, so an empty filter cannot silently pass. Source edits
trigger reconfiguration to discover added cases. The threads case is omitted
only when tide_platform is absent, matching NEWTON_TEST_JOBS; the separate
fresh-process join test is unchanged. No solver, simulation length, network
conditions, assertions, pinned hashes, compiler flags or workflow gate changed.

Native debug passes all 24 cases, runtime and join (26 CTests, 104.87 seconds),
and WASI debug passes the same set (121.65 seconds). The unchanged rollback case
takes 54.61/60.15 seconds respectively. Package order passes on both targets.
CTest metadata matches all 24 compiled cases on native/web and all 23 without
the platform, with unchanged deadlines and nonempty-run checks. Optimized WASI
passes all 29 Newton CTests in 36.14 seconds, including both arm hosts and the
unchanged MicroDuck history pin. Hosted Android/sanitizer confirmation remains
the next automatic Build run's coverage; no workflow was manually dispatched.

## Collision floor and mouse sandbox (2026-10-22)

The owner wanted a physics floor and objects to play with, then requested mouse
manipulation. The package now exposes `Collider`, initially Sphere and static
Plane, and MicroDuck uses those colliders instead of its old custom sole-only
spring system. This extends the same Featherstone runtime; no other engine or
training-only simulation is selected.

`Collider.body` defaults to its own entity; separate entities reference a body
for compound shapes. Shape position/rotation are relative to the body origin.
A plane's local +Y is its outward normal and its negative half-space is solid.
Planes require static bodies. Radius is positive for spheres, nonnegative and
unused for planes. Materials are finite and nonnegative; defaults are
stiffness 2000, damping 5, friction damping 100 and friction 0.8. Equal nonzero
exclusion groups disable a pair, with negative groups rejected. Same-body and
static/static pairs are ignored. Disabled colliders are omitted from collection
and report zero force on a successful tick. Colliders do not infer mass/inertia.

The exporter additionally isolates the unchanged upstream `eval_body_contact`
entry point and transitive force functions into `kernels/contacts.inl`, avoiding
unrelated particle/hash-grid services. The original Featherstone and all Warp
header bytes remain unchanged. The adapter uses upstream's analytic sphere and
plane geometry/contact-writer formulas in sorted collider-identity pair order;
coincident sphere centers use +X. Geometry uses current FK and public COM twists
every substep, before upstream generalized-force evaluation. Force response is
Newton's compliant normal spring/approach damping and Huber-regularized friction,
with smoothing fixed at 0.01. It is not a static-friction constraint, and the
Huber scalar limiter does not strictly bound the vector's Euclidean magnitude
by a textbook Coulomb cone. There is no contact persistence or warm start.

All native model/contact storage is transient. Per-pair wrenches are zeroed and
accumulated in canonical order. `Collider.contactForce` is the mean world force
over successful substeps, including friction and static reactions. Bodies,
joints and collider outputs publish together only after the whole tick succeeds.
Invalid shapes/references fail as `InvalidCollider`; overflow/allocation checks
precede native array access. Private ColliderState is 84 bytes versus native
nt_collider's 88; bridge copies use their separate strides and full Entity IDs.
Physics is now 104 bytes. Generated-header/bridge size and offset checks pass
on native and WASI; no engine/compiler runtime API was added.

The sandbox has 61 original sphere estimates per duck covering all 15 authored
bodies, including six sole spheres per foot, plus four loose balls and a static
infinite floor plane: 127 colliders total. Robot-internal collisions are disabled
by separate nonzero exclusion groups; the ducks, props and floor interact.
Approximate collision geometry follows the original source frames, converted
consistently to principal physics frames. It uses no upstream STL geometry.
Model XML, conversion, masses/full tensors and joints are unchanged. The match
still ticks at 200 Hz and now uses four substeps. Both contacting surfaces use
stiffness 5600, damping 3 and friction damping 1; the default 100 produced
excessive slip response for these small masses in the backend probe.

Mouse picking uses interpolated displayed body poses and analytic ray/sphere
intersection in views. The local gesture keeps a camera-facing drag plane.
Sample submits bounded absolute collider identity, body-local anchor, world
target, body reset revision and frame serial, plus a held-grab action. Match
code validates Grabbable, live full-generation references, dynamic body,
revision and an anchor within 2 mm of the sphere surface. Both owners may grab
any marked body; their springs add in deterministic system order. Each spring
uses world point velocity and COM torque, with limits 12 N and 0.6 Nm; scaling
force for the torque cap preserves the point-force relation. No body pose is
written by grabbing. A fresh nonzero frame serial grants a 0.25 s lease, measured
from integer tick difference rather than repeated float subtraction. Releasing,
resetting or stale/reused references stop the grab; missing serial updates expire
it even when authoritative input repeats its last value.

`ClearSandboxForces` precedes Drive, then ApplyDuckGrab precedes the complete
physics phase and Record follows it. This prevents failed steps accumulating
another external wrench. R resets the owner's duck and shared balls, leaving
the other duck; per-body revisions invalidate either player's prior grab.
B retains explicit balance/heading/lane help; W/S remains a joint pattern,
not demonstrated controlled travel. The camera is fixed for mouse manipulation.
The 44 observation slots remain, but foot load is now the sum of mean world-Y
contact forces on sole colliders, not an isolated floor-normal sensor. Each ring
also retains its controller's grab request, accepted body and actual wrench.
Another player's grab may affect the duck, so this is still not a complete
world dataset or a verified-tick exporter.

Validation:

- All 33 runtime tests pass in native/WASI debug and release, including nine
  independent stock-Newton contact samples at 1/8/32 steps. State tolerance is
  `5e-5`, mean-force tolerance `0.002 N`. Analytic cases cover support and COM
  torque, rotated/local frames, friction, pair momentum, compound accumulation,
  filters, articulated contact/reset coupling, malformed inputs, overflow and
  late-substep failure atomicity. Contact history `ddf1584fd2502ff1` was observed
  on native/WASI before pinning and agrees in all four builds; the no-contact
  hash `92249536c6268981` is unchanged. Contact oracle regeneration/check passes.
- The generated fixture has 24 tests, covering all-body proxy coverage, all
  four loose balls falling/resting, physical full-body impact, actual input
  grabs on free/COM-offset and articulated bodies, bounded point force/torque,
  additive two-player springs, invalid requests, revisions, release and the
  heartbeat lease boundary. Active-grab snapshots, raw/delta repair, threads,
  alternating worlds and two-client rollback are included. The fresh-process
  host restores during contacts and accepted ball/child grabs; it reads lists
  with an explicit world heap after unpacking. An implicit TLS heap count in
  that trusted C host was corrected; no runtime defect was involved.
  Expanded rollback assertions initially assumed client creation order matched
  assigned owner order. They now resolve each controller's Owner against the
  actual connected client player ID, retaining both ball/child acceptance,
  recorded-wrench, lease-expiry and active-release assertions.
- The final 5600/3/1 material passes the unchanged 20 mm transient-penetration
  budget for a 0.35 m unassisted toppled drop: minimum collider bottom is
  -15.692 mm, settling to -0.581 mm with root speed 0.000097 m/s. All four balls
  settle after four seconds from 0.12–0.15 m drops; maximum penetrations are
  3.45/4.08/1.71/3.97 mm. These are compliant sphere-proxy measurements, not a
  guarantee of zero penetration or calibrated hardware contact.
- Both signed joint patterns retain the original >5 mm whole-foot clearance
  and sustained support/touchdown criteria: five cycles per foot in ten seconds,
  with actual sole-sphere-envelope clearances 19.101/19.505 mm for +1 and
  11.245/11.207 mm for -1. Mean sole loads are 7.232199/7.231452 N, minimum trunk
  heights 85.029/99.343 mm and peak motor effort 0.675635/0.689867 Nm. This still
  establishes assisted stepping, not command-aligned travel or self-balance.
- The intentionally changed 280-tick MicroDuck hash is `f87f0902ec4db694`;
  the arm's 200-tick hash is `db6cb5835d7c9b26`. Both were observed across
  native/WASI debug/release before pinning. The arm test now uses real encoded
  player IDs and asserts that both owners' controls move their arms.
- Installed-source CLI native/web release builds pass without Python or
  downloads. All 46 staged source/header files match checkout bytes, and all
  40 upstream manifest hashes match. A bounded hidden native run and muted
  WebGL/WebGPU checks demonstrate ball lift/release, head grab/lift/drop with
  assistance disabled, body-floor rest, reset and resumed recording. No page
  exceptions or graphics validation errors occurred; only the known browser
  audio autoplay warning remains. Final captures were inspected on both web
  backends. The local server remains available for the owner's play test;
  reloading starts a fresh scene rather than retaining old hot-reloaded values.

Fresh-process native-to-WASI and WASI-to-native continuation passes in debug
and release, restoring during actual contacts and active ball/child grabs.
All four transfer files are byte-identical, SHA256
`1c43d950e7a369177706750c478e8e908ad236cbac2aede4a7460d06dd21ed07`.
Independent review closed the multiplayer grab/record/expiry and live-release
coverage gaps; the final scoped review reports no findings. Production docs
build passes on Node 24.16.0 with existing allowed unstaged-example 404s; all
16 local documentation links and scoped whitespace checks pass.

All 24 MicroDuck cases, 33 runtime cases, six arm cases, package ordering and
both join CTests pass in native/WASI debug/release. Release runs rechecked the
final corrected rollback case after the other 23 cases passed unchanged;
native/WASI debug full suites execute the complete final source. The two
clients reach 393/394 verified ticks with 3845/4049 replay ticks and zero
resyncs, including ball/child grabs, recorded wrenches, early expiry, renewal
just before release and cleared authority/verified-client grab states.

The complete native debug workflow passes 2,554/2,554 tests (691.22 s), and
the complete web debug workflow passes 2,416/2,416 (802.59 s). Both first runs
are clean, including WebGL/WebGPU draw tests; the final MicroDuck CTests take
117.06 s native and 123.97 s WASI. Reproduction and measurements are under
`build/newton-collision-*-full.log`, `build/microduck-collision-preview/`,
`build/newton-sandbox-{release,web-release}-final.log` and the corresponding
`*-rollback-final.log` files. The release `*-final.log` files retain the initial
test-host owner-order failure; the corrected case passes in the latter files.

Final integration: feature commit `d6b4bd99` merged `origin/dev` cleanly at
`8b577e46`. Incoming work changes printed schedule grouping, Unity CLI/platform
integration and docs navigation; Newton/package/example sources do not overlap.
Rebuilt the affected native/web robotics targets and native CLI/tests. All seven
selected native CTests and six web CTests pass, including package ordering,
the complete 24-case fixture and both join hosts, with unchanged history pins.
Post-merge native-to-WASI and WASI-to-native active-grab joins pass again and
produce the same canonical file hash above. Production docs build passes with
the incoming navigation changes. Logs are
`build/newton-collision-{debug,web-debug}-postmerge-{build,tests}.log`,
`build/newton-collision-postmerge-cross-join.log` and
`build/newton-collision-docs-postmerge.log`.

## Historical: MicroDuck physical stepping and closer appearance (2026-10-22)

The owner asked for walking or a useful first motion milestone, and a closer
model of the real robot. The fixture now uses a free Newton trunk and simple
foot-plane forces. The achieved milestone is explicitly assisted physical
stepping, not reliable forward/backward walking or a learned balance policy.
Both signed sweep patterns in the bounded research probe drifted backward;
changing joint angles alone is not evidence of controlled locomotion.

All 15 authored masses/full inertias, 14 hinges, the pinned XML and principal
frame conversion are unchanged. No Newton package/runtime/compiler API changed.
`microduck.tide` implements the floor and controller as ordinary match code
writing the existing world COM `Body.force`/`torque`. The same Newton solver
then advances gameplay, replay and any recorded demonstrations.

- Four original proxy points per foot form a 64 x 30 mm rectangle around the
  source foot site, in its original ankle frame. They are illustrative contact
  geometry, not extracted mesh boundaries. Point velocity is COM velocity plus
  angular velocity crossed with the COM-to-point arm. Normal force is
  `clamp(-1000*y - 4*vy, 0, 12)` N below the plane, zero above; tangential
  damping has gain at most 20 N s/m and is bounded by `0.8 * normal`.
  Each point contributes its world force and `arm x force` torque. Iteration
  order is fixed. This is a compliant, sampled contact approximation, with
  no self/body/inter-robot contacts or general collision/query backend.
- The initial/reset pose includes coherent FK for every body before contacts
  run. Reset clears root/body speeds and wrenches, q/qd, trims, phase, command,
  contact readback and the recording ring. A fixed world joint no longer holds
  the trunk. The two ducks use separate visual lanes.
- Joint PD is 4/0.03, clamped to the authored 0.96 Nm limit; passive damping
  and armature remain authored values. The gait uses a 0.625 Hz phase, signed
  0.6 rad sweep and nonnegative 0.5 rad lift, with a smoothed command after a
  one-second settling interval. W/S or stick Y select opposite step patterns;
  they are not a velocity-tracking forward/backward command. Q/E selects a
  joint, A/D adjusts its trim, R resets and B toggles assistance.
- Assistance is explicit and starts enabled: upright torque, weak world +X
  heading torque and a lateral lane spring/damper, capped at 4 N. It supplies
  exactly zero forward or vertical force. The HUD labels it and the complete
  applied assistance wrench is recorded. Disabling it can cause falls; no
  realistic unassisted balance or BAM motor behavior is claimed.
- The ring remains bounded at 64 transitions. Its 44-value observation is
  q[14], qd[14], root height, world linear velocity[3], world angular
  velocity[3], projected unit gravity[3], applied foot-normal loads[2], gait
  command, sin/cos phase and assistance flag. Pre-state is captured before
  control changes (after any reset); post-state follows successful physics.
  Actions remain the 14 joint targets; efforts are actual clamped motor
  controls. Assistance force/torque are separate fields. The tracking reward
  is illustrative, not a walking objective. Verified-tick export and exact
  human-view timing remain unimplemented.

`visuals.tide` draws original shaded meshes per source body: a rounded camera
hood and lens, thin jaw, exposed servo cases/hubs, brackets, leg covers and
broad soles. Joint/site anchors come from the Apache-2.0 XML; enclosure extents
are estimates from official photos, not CAD. No upstream STL or image texture
is included or used to derive the geometry. The original/principal frame
relationship is unchanged. References and licensing are in the example's
`UPSTREAM.md`; game builds require no Python or authoring download.
The 30 visual meshes are built once in the local `MicroduckMeshes` singleton;
warm draws borrow stable geometry and apply each body's current transform.
Vertex lighting is baked from the first pose. This cache is outside match
state, snapshots and hashes. Rebuilding it every frame took 99.88 seconds for
a 480-frame installed debug capture; caching reduced that to 75.67 seconds,
exposing the unoptimized CLI solver cost. The same final cached fixture in
the installed `--release` build completes 480 frames in 2.53 seconds, timed
with `Start-Process -Wait` (the Windows release executable uses the GUI
subsystem). The normal play/build instructions use `--release`. CMake's
Newton backend already uses O2 in debug, unlike the installed CLI's default
O0 game/package source profile. No optimization flags or CLI rules changed.

Research artifacts are under `build/research-microduck-walk/` and
`build/research-microduck-look/`. The bounded C probe using this Newton runtime
supported the 7.232 N weight, produced 11–20 mm whole-foot clearance and
completed ten seconds with identical native/WASI history hashes. Those are
probe measurements, not a calibration claim for the finished fixture.

Final validation:

- All 14 generated-game tests plus fresh-process joins pass in native/WASI
  debug and release. The intentionally changed 280-tick match hash
  `d39953ac3eb20462` was observed on native/WASI before pinning and agrees in
  all four builds. Both signed patterns complete five sustained lift/touchdown
  cycles per foot in ten seconds. Final whole-foot clearances are 42.20/41.78
  mm for +1 and 5.97/6.22 mm for -1; mean floor loads are 7.23245/7.23203 N.
  Minimum trunk heights are 82.56/89.09 mm; peak motor controls 0.755/0.645 Nm.
  The original >5 mm clearance acceptance was retained while lift was tuned.
- Numeric and rotated-frame checks cover contact point velocity, COM torque,
  friction bounds, caps/no attraction and force replacement after failure.
  Assistance toggle and actual physics coupling, full observations/continuity,
  coherent reset, ring/failure behavior, snapshots/alternating worlds, raw/delta
  transfer, threads and two-client latency/jitter/loss rollback all pass.
  The host tests' older raw player-ID mistake was corrected to
  `tide_player_from_index`; both actual owners now receive their test actions.
- Native-to-WASI and WASI-to-native joins pass in debug and release after 360
  gait ticks plus 240 continuation ticks. All four snapshot files match bytes.
  The original full-tensor stock Newton oracle still agrees within 4.543e-6;
  converter and stock oracle regeneration checks pass with unchanged outputs.
- Installed CLI native/web builds pass. The optimized desktop capture and
  muted WebGL/WebGPU play checks confirm stepping, trim, assistance off/on,
  reset and resumed recording, with no page/graphics errors. Original photo
  references, source frames, mechanical silhouette and poses were inspected.
  The closer meshes are cached outside match state; no simulation result
  depends on rendering or cache lifetime.
- Independent read-only integration review found no remaining implementation
  issues. The final production docs build passes on Node 24.16.0, with existing
  allowed unstaged-example 404s. Scoped whitespace/diff checks pass.

Reproduction/results: `build/microduck-stepping-test-RESULTS.txt`,
`build/microduck-stepping-validation-r2/`, `build/microduck-step-cli/` and
`build/microduck-visuals-check/`. No compiler, engine/platform or package
runtime source changed, so validation is scoped to the repository game on
native/web plus the docs build. Other operating systems and physical hardware
remain CI/independent validation work.

Final integration: feature commit `44f230ec` merged `origin/dev` cleanly at
`bfe135d7`. Incoming compiler fixes concern FBX asset handling and audio header
inclusion; the fixture and Newton sources did not overlap. Rebuilt the affected
native/web debug game, test and join targets; both focused CTests pass on each
target. Comparing regenerated C/header files with their saved pre-merge hashes
confirms that only the expected compiler/runtime compatibility fingerprint
changed; generated behavior and layouts are unchanged. Logs are
`build/microduck-step-debug-postmerge-{build,tests}.log` and the corresponding
`web-debug` files.

## Historical: anchored MicroDuck fixture (2026-10-22)

The owner selected Pollen Robotics' MicroDuck for the next fixture and approved
the anchored joint-control milestone. `examples/robotics/microduck/` contains
two owner-controlled ducks using the existing Newton package. All 15 bodies
remain dynamic with authored mass and inertia; a world-parent Fixed joint
anchors each trunk. The 14 leg/neck/head hinges retain the original DFS order,
local axes and ranges. No package, compiler or engine runtime API changed.

The source is `microduck_rl` revision
`b02fdea4902c2b93b7d34a83a6f05da4ececb7d3`. `upstream/` preserves the exact
`robot_walk.xml`, `scene_walk.xml` and Apache-2.0 license bytes. The XML/data are
Apache-2.0; no upstream noncommercial STL meshes or mesh-derived visuals are
included. The game draws its own primitive ducks, joints and stands.

`tools/convert.py` is a pinned, standard-library-only authoring tool, not a
general MJCF importer. It rejects changed source hashes or unsupported XML,
diagonalizes complete symmetric inertia tensors with fixed-order Jacobi sweeps,
and rotates body COMs and both joint attachment frames consistently. Body
origins are unchanged. `principalRotation` maps the new principal frame into
the original link frame; original visuals use its inverse. The fixture maps
source Z-up to Tide Y-up with a -90-degree X rotation at the trunk anchor.
Generated float32 values reconstruct full inertia within `1.46e-7` relative
error and source zero/STAND COM positions within `8.37e-9` m in the converter's
double-precision checks. Source/model contracts and exact hashes are in the
example's `UPSTREAM.md`. Game builds run no converter, Python or MuJoCo.

The match ticks at 200 Hz with one 5 ms Newton step, matching the selected
reference integration interval. Q/E, arrows or gamepad D-pad select a joint;
A/D or the left stick moves its target at 0.8 rad/s within its range. R/South
resets joint coordinates/velocities, targets and recording to the authored
STAND pose. The illustrative PD controller uses gains 0.55/0.03 plus authored
passive damping 0.053 and armature 0.0018; package controls clamp to 0.96 Nm.
Soft position limits use gains 0.55/0.03. These choices do not implement BAM
motor voltage/friction/delay behavior or reproduce upstream walking policies.

`Duck` keeps body/joint identities, targets, selection and a 64-transition
ring in ordinary match state. Each transition holds tick, pre-step q[14]/qd[14],
desired angles[14], actual clamped efforts[14], post-step q/qd and negative
summed squared joint-target error. Reset starts a new ring; failed steps append
nothing. These are executed-state records, with the same verified-tick export
and human-view timing limitations as the arm fixture. No dataset exporter,
policy runner, contacts, balance or walking is added.

Validation:

- Converter regeneration/check passes on Python 3.11/3.13/3.14. Native and web
  generated model records agree byte-for-byte. Source hashes reject mutation.
- Independent stock Newton CPU reference uses the original full tensors and
  frames, with anchored trunk, STAND q, zero qd, no contacts/limits/friction/PD,
  and alternating +0.02/-0.015 Nm effort. Runtime q/qd and all COMs at 1/8/32
  steps agree within `4.67524e-6`, below the `5e-5` absolute tolerance, on
  native/WASI. Dropping tensor products changes qd by `7.55e-4`, so this check
  detects that approximation. A separate original-XML double FK agrees with
  stock Newton initial COMs within `1.97e-8` m.
- Eight game tests plus the fresh-process join CTest pass on native and web
  debug and release. They cover each body's mass/COM/full tensor, all-joint observations,
  bounded records, reset, snapshots/alternating worlds, raw/delta transfer,
  thread equality and two-client rollback with latency/jitter/loss. Trusted C
  host reads use explicit heaps when alternating worlds. The 280-tick world
  hash `5e74bf9e0f8ba7ca` agrees in all four builds and is pinned. The join
  host uses the same 200 Hz timestep; separate cross-target file checks pass
  native-to-WASI and WASI-to-native at 40 ticks plus 20 continuation ticks.
- A hidden, muted desktop capture confirms both procedural ducks, joint
  selection, target movement and status/readout. Captures are under
  `build/microduck-capture/`.
- Installed CLI native/web builds pass using only the checked-in game/model
  and Newton sources, with no Python or downloads. Desktop, WebGL and WebGPU
  smoke checks at 1280x720 confirm selection, target movement and reset; the
  recording count restarts at 3/64 and resumes to 36/64. No Newton error HUD,
  page exceptions or graphics validation errors occur. All browser launches
  were muted. Commands, logs and captures are in `build/microduck-cli/`.

Independent integration review found only a copied 120 Hz timestep in the join
host. It was corrected to 200 Hz and checked on native/web debug/release and
both cross-target transfers. No other actionable findings remain.

The full native debug suite passes 2,553/2,553 tests. The full web debug run
passes 2,414/2,415 initially: the existing `tide_platform_draw_webgpu` test
reports two background pixels in its maximum-uniform shader checks. Two exact
reruns pass without source or assertion changes. Read-only review finds that
the test waits for `picked_shader` but then also checks the independently
compiled `many_shader`; a still-pending second pipeline fits the blank pixels,
although individual pipeline timing was not traced. No drawing source,
browser command or draw-target object changes are part of this fixture. All
Newton and MicroDuck tests pass in the full runs. Production docs build passes
on Node 24.16.0.
Full-run and recheck logs are under `build/microduck-*-full-*` and
`build/microduck-web-draw-*.log`. Other operating systems and physical robot
validation remain unavailable locally.

The final clean merge from `origin/dev` brought Unity HLSL/embedding and
alternate-camera drawing changes, with no Newton or MicroDuck source changes.
Because the platform library is a dependency, both debug targets were rebuilt.
Focused native checks (Vulkan/OpenGL drawing, MicroDuck and its join) and web
checks (WebGL/WebGPU drawing, MicroDuck and its join) pass 4/4 on each target.
Commands/results are in `build/microduck-*-postmerge-*.log`. The installed CLI
and MinGW/web platform libraries were rebuilt and restaged too. Installed
native/web builds, a bounded 110-frame desktop capture and muted WebGPU
controls/reset smoke all pass; source hashes are unchanged and 200 Hz/one
substep remains confirmed. The report is
`build/microduck-cli/postmerge-RESULTS.md`.

## Historical: Featherstone CPU integration and documentation (2026-10-22)

The owner requested NVIDIA Newton as a separate optional package like Box3D,
for robotics and multiplayer/training using the same physics. A separate
training engine is outside that contract. `packages/newton`, dependency name
`newton`, namespace `Newton`, is an experimental first integration; the Tide
API names and first-build behavior below are provisional. This is NVIDIA
Newton, not Newton Game Dynamics. There is no silent Box3D fallback.

The backend runs the actual upstream Featherstone kernels; native/WASI runtime
tests and native/web debug/release Tide fixtures pass as detailed below.
The full native and web debug suites also pass (2,512/2,512 and 2,413/2,413).
Installed-source native/web builds and a bounded desktop arm run pass too.
Physical fidelity remains unvalidated.

### Source and build path

- Newton revision: `ea4899864046dcd5981f8ba0bc3d8c25e65dcb96`.
- Warp revision: `b346b941a4c0cd19c510db67d6506d781464ed5b`, wheel/version
  `1.19.0.dev20261020`.
- `tools/export.py` requires those source revisions and the pinned Warp
  version. It statically exports upstream Featherstone CPU kernels with
  backward generation, fused floating-point operations and fast math disabled,
  and block dimension 1. Checked-in source is `kernels/featherstone.inl`;
  transitive Warp native headers and upstream/license notices are in `vendor/`.
  `UPSTREAM.sha256` records generated-source/header hashes.
- Game builds compile that same scalar CPU source on native and WASM. Python
  and Warp are regeneration-time dependencies only; there is no game Python,
  JIT, CUDA or C++ runtime dependency. The package's C++ is a source port of
  generated CPU kernels, not a second solver implementation.
- `math_bindings.h`/`math.c` route kernel transcendental operations, including
  revolute trigonometry, to Tide's shared float/double math. Builds disable FMA
  contraction/fast math; the selected solver path's explicit `fmaf`/`fma`
  scalar `muladd` specializations are replaced with separate multiplication
  and addition. Unused vendored intersection helpers are outside that path.
- Audited export patches exclude WebAssembly from Warp's Clang native-half
  guard, make CPU launch wrappers translation-unit-local, and reuse system
  `stdint` typedefs to avoid LP64 conflicts. Generated primal temporaries are
  value-initialized: Warp's dead loop-carried copies after possibly empty loops
  can otherwise read uninitialized values. Defined solver arithmetic/control
  flow is unchanged. `UPSTREAM.md` records patch details and regeneration checks.

### State and API contracts

- `Body`: free dynamic or static roots; world-space body-origin pose, world
  centre-of-mass linear velocity and angular velocity; authored mass, body-space
  diagonal inertia about an authored local centre of mass; world force/torque.
  Dynamic mass and inertia must be positive and finite. Static roots may have
  zero mass. No shape or shape-derived mass properties exist.
- `Joint`: a separate entity connects a body child to a body parent or the
  world (`parent = default`). Fixed, revolute and prismatic are the first
  supported types. Frames are relative to body origins (world space for a
  world parent); the axis is in joint-frame space. Coordinates/velocities are
  radians/radians per second or metres/metres per second. Body poses/speeds
  for jointed children are reconstructed from root and joint state. Writable
  joint coordinates and free-root body poses/speeds are resets.
- Each child has at most one inbound joint; the graph is acyclic. An explicit
  static parent root with a dynamic child is allowed. An explicit joint with
  a static child is rejected as `InvalidJoint`. Missing/stale references fail
  the step rather than deleting a joint. Destroy referencing joints when
  destroying bodies, respecting ordinary tick-end structural application.
- Damping and armature are nonnegative finite joint settings. Optional position
  limits use spring/damper response, not hard positional stops. There are no
  friction or velocity constraints.
- Optional `Actuator` on the joint entity supplies generalized effort: torque
  for revolute, force for prismatic. `effortLimit` is the package clamp before
  Newton control input, not a native solver total-effort constraint. Passive
  damping and limit forces are excluded. `appliedEffort` reports that last
  successful clamped control, not a torque/force sensor; fixed joints report
  no driven effort. `effort` persists until code changes it.
- `[Before(PhysicsStages.Step)]` writes this tick's actions, forces and resets.
  `[After(PhysicsStages.Step)]` reads status and completed results. Physics
  defaults are gravity `(0,-9.81,0)`, four substeps, total duration `Time.dt`.
  Games order against the whole phase, not internal collection/publication
  systems.
- `Physics.status`: `Ok`, `InvalidBody`, `InvalidJoint`, `InvalidTopology`,
  `TooLarge`, `Unstable`. Failure publishes no new body/joint/applied-effort
  results; pending forces/torques remain. They clear only after success and
  are held throughout all substeps. Check status before accepting observations.
- All persistent state is ordinary Tide components, lists and maps. `Physics`
  owns value collection buffers and generation-bearing entity maps. Native
  scratch/model storage is rebuilt per call and retains no pointers or hidden
  state between ticks; no `Resident` arena. BodyState/JointState and internal
  collection/map fields are package implementation records, not game controls.

### Games, robotics policies and demonstrations

Human input, game code and a policy all write ordinary match actions before
the same physics phase and read real joint coordinates/body poses afterwards.
Outside-service answers must enter through normal match input. Episode reset
uses standard Tide world snapshot/restore, with controller/reward state kept
in the match too. There is no separate Newton/training simulation to reset.

`examples/robotics/arm/` is a runnable two-player target-reaching example using
Rooms and owner keyboard/gamepad input. Each `Arm` retains a bounded ring of
128 transitions: tick, pre-step joint observation, action as desired joint
angles, actual clamped effort, next observation and reward. `Drive` captures
pre-step state/actions; `Record` appends only after a successful physics step.
The ring is ordinary match state and participates in restore and rollback.

These are executed-state pre/post-step records, not an exported dataset or
exact human-view observation timing. Human demonstrations needing that timing
must also associate the observation used to choose input with its match-time
application. Pose-only animation is insufficient. Export verified ticks in tick
order to avoid duplicate or superseded prediction samples. No exporter, dataset
format or trainer is implemented.

### Documentation and discovery

`packages/newton/README.md` and `docs/packages/newton.md` describe the scoped
API, failure behavior and unsupported features. Both have an anchored revolute
pendulum and bounded Tide PD example; the site page also shows status-aware
post-step observation. Authored inertia and illustrative gains are explicit;
the runnable arm example and its executed-state recording limits are described.
The package appears in the Packages sidebar, overview, usage/authoring guide,
root README, repository layout and AGENTS notes index. Touched package lists
also retain/include `voice`; no unrelated discovery refactor is intended.

### Validation and remaining work

- All 22 runtime tests pass natively and under WASI. They include one two-link
  stock Newton Python reference model's joint coordinates/velocities and body
  COM state at 1, 8 and 32 ticks within `5e-5` absolute tolerance, analytic
  dynamics, validation atomicity, topology, replay and alternate worlds.
  Runtime hash `92249536c6268981` is pinned on both.
- All six Tide arm fixture tests pass in native debug, native release, web
  debug and web release. Coverage includes replay/alternating worlds,
  snapshot restore, raw packing and delta repair, stale entities, and equality
  across thread counts for 180 ticks. The 200-tick fixture hash is
  `b3a702e67f351594` in all four builds.
- The fixture's actual two-client loopback rollback test passes in those four
  builds for 720 frames at 0.035 s latency, 0.01 s jitter and 0.03 loss. Each
  client executes over 6,000 replay ticks with zero resyncs in the observed
  runs; the test asserts replay activity and zero resyncs.
- Separate fresh-process cross-target checks pass native-to-WASI and
  WASI-to-native, using a 40-tick snapshot followed by 20 continuation ticks.
  The registered join CTest exercises write/read with the same executable.
- Full native debug passes 2,512/2,512 (497.90 s), and full web debug passes
  2,413/2,413 (669.18 s), with the latest exporter patches. The four focused
  Newton CTests pass again in native/web debug/release with unchanged hashes.
  Test sources and join hosts are in `packages/newton/test/`.
- Installed CLI validation passes with clang 23.1.2 and Node 24.16.0. The arm
  builds natively and for web from installed bridge/math/runtime sources,
  without a prebuilt Newton library. All 45 source/header files match the
  checkout. Deliberately breaking the staged `.inl` invalidates both builds;
  restoring its bytes recovers both. A hidden, muted 120-frame desktop run
  captures both arms responding. Minimal native/web games compile zero Newton
  objects; native PDB modules also confirm exclusion. Browser execution of the
  installed arm was not run. Reproduction script, logs and captures are under
  `build/newton-cli/`; the example README documents controls and bounded runs.
- Documentation dependencies were already installed with
  `npm ci --no-audit --no-fund`. The final `npm run build` passes with local
  Node 24.16.0 from `build/tools/node/node-v24.16.0-win-x64`, including
  page/sidebar, link and anchor checks. Existing allowed 404s for unstaged
  game/example pages remain. The three documentation files pass whitespace
  checks, all 12 local links resolve, and scoped `git diff --check` passes.

Final integration: commit `87608d55` merged the current `origin/dev` cleanly
at `e1b1d2c6`. Incoming work changed compiler/platform dependencies and separate
voice/Unity build sections; Newton sources and fixture behavior were unchanged.
Rebuilt the affected native/web Newton targets and native CLI/editor tools;
all four Newton CTests pass on both targets with unchanged pinned hashes. The
post-merge production docs build passes. Rebuilt the MinGW platform library,
restaged the installed CLI, and repeated native/web arm/minimal builds and the
120-frame desktop run: all pass, captures match their pre-merge bytes, package
source/header bytes and all 39 upstream hashes agree, and omitted-package games
still compile no Newton objects. An independent second review confirms both
the LP64 typedef and generated empty-loop read fixes; no findings remain.

Other-platform CI, performance
measurements and independent physical-fidelity validation remain outstanding.
Stock Newton reference and analytic checks do not establish realistic robot
behavior, CUDA equivalence or sim-to-real transfer.

Not implemented: collider shapes, collision detection/contacts/contact friction,
collision queries, URDF/MJCF import, CUDA execution, joint friction or velocity
constraints, automatic realistic robots, sensors/noise, trainer or differentiable
training interface. No realistic sim-to-real transfer claim is supported.
