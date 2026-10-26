# MicroDuck physics sandbox

A two-player physics sandbox for the optional `newton` package, using Pollen
Robotics' published MicroDuck model: 15 rigid bodies and 14 hinge joints per
duck, each with a free dynamic trunk. Newton package colliders support contacts
with the floor, the other duck and four loose balls. Hold the left mouse button
to drag a marked duck part or ball; either player can grab either duck.

The default controller runs Pollen Robotics' released **VelStand v8 weights**
locally at 50 Hz. WASD supplies desired forward speed and yaw rate; the network
controls the joints. Learned mode applies no balance or lane assistance.
Transfer to this Newton model is experimental. Integrated native/WASI probes
of the 174-collider model preserve W/S walking and recover from the three
seeded prone, side and supine poses. **D/right turning remains weak.**
These bounded trials do not establish random-fall recovery, reliable
directional turning or real-robot fidelity. Press M for the older manual
joint and assisted-step controller.

The new mesh-derived collision data is **CC BY-NC-SA 4.0**, separately from
the Apache-2.0 XML/model and policy/code. Upstream STL geometry was read and
converted privately; raw STL files are not redistributed. See
[`LICENSE-COLLISIONS.md`](LICENSE-COLLISIONS.md) and
[`README-COLLISIONS.md`](README-COLLISIONS.md). Rendered visuals remain original
procedural geometry based on photographs and XML metadata.

Build and run from the repository root with an installation containing Newton:

```sh
tide run examples/robotics/microduck --release
tide build examples/robotics/microduck --release --web
```

Use `--release` for play: the unoptimized CLI Newton build is too slow for
this full articulation. It uses the same simulation and contact code.

Escape closes the Rooms menu. Joint controls apply to your own duck:

| Control | Action |
| --- | --- |
| Hold left mouse and drag | Pull a marked part of either duck or one of the shared balls; release to let go |
| Hold W/S, Up/Down, or left stick Y | Request forward/backward velocity from VelStand; opposite step patterns in manual mode |
| Hold either Shift | Boost learned forward/reverse requests to ±0.5 m/s and turning to ±1.25 rad/s; stability and actual speed are not guaranteed |
| Q/E, Left/Right, or D-pad Left/Right | Previous/next leg, neck or head joint |
| A/D or left stick X | Request left/right turning; A/left passes the bounded turn probe, D/right remains weak; manual joint trimming is unchanged |
| M | Switch between VelStand and manual controls; clear policy feedback |
| R or gamepad South | Reset your duck and all four shared balls; leave the other duck unchanged |
| B or gamepad North | Toggle upright, heading and lane assistance in manual mode |
| F8 or click the FPS readout | Open/close the stats drawer: frame rate, CPU/render/GPU work where available, memory, networking and slow systems |
| F7 | Open/close the detailed frame/tick profiler, including the Newton simulation system |

The stats line is enabled by default in the bottom-right corner. It measures
detailed timings while open; click it or press F8 to expand it. These local
diagnostics do not change physics or recorded match state. Use `tide run
examples/robotics/microduck --release --stats` to start the drawer open.

Reset restores the authored STAND pose with coherent body placement, a trunk
height of 0.12 m, zero velocities/forces and cleared trims/step phase. It also
clears raw-action, target-filter, raw/applied motor and cadence history, joint
effort/load and applied-effort readback. Learned mode keeps
assistance off; manual mode restores assistance. It clears your recording ring and restores the balls to their home
poses. Body revisions invalidate either player's grabs on those reset bodies;
release and press again to grab them. Joint selection persists. Controls still
apply on the reset tick, and its successful step starts the new ring. The
readout shows actual joint state, clamped control, sole contact loads and the
accepted grab wrench.

The match runs at 200 Hz with four 1.25 ms Newton substeps per tick. The learned
controller uses the pinned BAM XL330 voltage/back-EMF and load-dependent dry
friction parameters. Gearbox friction uses previous applied motor effort and
joint load; battery sag uses separate previous raw motor torque before the
effort ceiling. Newton interleaves joint-friction and Coulomb-contact impulses
through the same articulated mass matrix in 16 fixed sweeps per substep when
Coulomb contacts are active; the legacy joint-only path retains eight sweeps.
Physical joint stops use `limitStiffness = 55`, `limitDamping = 0.6`; these are
compliant stops, not hard range clamps or an exact MuJoCo limit mapping.
Normal forward/reverse commands use 0.4 m/s and turning uses 1 rad/s. Holding
either Shift sets a match `run` flag, selecting signed `runSpeed = 0.5` for W/S
and 1.25 rad/s for turns, including diagonal input. Learned turning uses `turn = -drive`, swapping
A/D relative to the earlier profile; manual `drive` and trimming are unchanged.
Manual PD gains remain
4/0.03 with a 0.96 Nm motor-control clamp, passive damping 0.053 and armature
0.0018.

The neural interface is 61 observations and 14 raw actions. Targets use the
official hardware profile: rounded HOME + 0.9 × action, head/leg lowpass
0.5/0.7 after the first inference, then a ±π travel clamp. Previous **raw**
actions, pre-clamp filtered targets, raw/applied motor history and cadence are
match state and follow rollback. The original ONNX and exact weights are checked in;
ordinary builds require neither Python nor ONNX Runtime. See
[policy provenance and numerical checks](policy/README.md).

This is a compatibility port, not the complete robot daemon or training plant.
The training backlash, sensor/actuator delays, mesh contacts and daemon's
limp/pose recovery wrapper are absent. Contact proxies, self-collision filtering
and the Coulomb contact/stop solve differ from MuJoCo. The default 1.75 A
current limit follows the pinned BAM constructor; upstream CPU rehearsal
explicitly disables that limiter.
These differences are compatibility gaps, not proven causes of the remaining
turning limitations. A 0.6 m/s forward trial and the 0.5 m/s reverse trial fell.
Shift exposes the requested boost in all directions; reverse and boosted turns
may stumble. It changes the policy's requested velocity/yaw, not time scale,
external assistance or a guaranteed real speed.
An earlier scratch fixed official CPU no-BAM profile
failed side/supine recovery on the preceding 61-sphere geometry; it does not
describe the new model, replace the default hardware/BAM controller or establish
exact training-plant identity.

The physical floor is a static infinite `Plane` at world Y=0; its drawn surface
is only 4 × 3 m. The model has 174 compound `Sphere`
colliders per duck: 131 mesh-derived support spheres and 26 internal core
spheres covering all 13 nonfoot bodies, one procedural trunk-casing core, and
the original 16 foot-body proxies, including six marked sole spheres per foot.
The scene has 353 colliders, including the floor and four balls.
The same constant model serves
walking, recovery and interaction; it does not swap geometry by situation.
Matching exclusion groups
disable collisions within one duck while retaining contacts with the other
duck, balls and floor. All game colliders use `stiffness = 5600`, `damping = 3`,
`frictionDamping = 1` and `frictionModel = ContactFriction.Coulomb`. Sole,
floor and ball friction is 0.8; nonsole duck spheres use 0. The package's
default remains `ContactFriction.Regularized`; Coulomb requires both shapes
to opt in and mixes their friction coefficients geometrically. The upstream
normal contact force is unchanged; tangential impulses can hold within the
Coulomb budget but do not impose hard nonpenetration. Sampled nonfoot support
error for the mesh-derived subset is approximately 5.5 mm (maximum 5.542759 mm);
this is not an exact mesh hull or an all-direction surface guarantee. Foot
geometry remains the original approximation. The 26
interior cores add internal volume, with two in every nonfoot body, at least
2 mm inward of the baseline support envelope. The added body-0 visual-casing
core is at `(.004, 0, -.006)` m with radius `.0115` m. It is not mesh-inscribed:
it expands selected support by 6.67484 mm and is outside that certificate.
It closes the visible black-case pick/prop hole in the targeted regression,
which failed on 173 spheres. The wider interaction checks are listed below.

Assistance applies upright and world +X heading torques and a lateral Z force
of up to 4 N toward the duck's lane. Its forward X and vertical Y force
components are exactly zero. `Duck.assisted` is an ordinary match field
controlled by B.

Mouse picking uses the displayed body poses and `Draw.ScreenRayOrigin` /
`Draw.ScreenRayDirection` in a view. The initial hit defines a body-local
sphere anchor and a fixed drag plane; local pointer state never drives the
solver directly. Bounded absolute match input carries the collider, anchor,
target, body revision and frame serial. Match code validates the full entity
generation, enabled sphere, surface anchor and revision before applying a pull.
Point velocity includes COM linear velocity plus angular velocity crossed with
the COM-to-anchor arm; torque is that arm crossed with the applied force.
Each controller's wrench is capped at 12 N and 0.6 Nm, scaling the force when
the torque cap is reached to keep it acting at the same point. Grabs add to
assistance and other players' forces. A serial heartbeat expires after 0.25 s
of elapsed match ticks without a new pointer frame. Release, reset or invalid
selection stops that pull; game-owned external wrenches are replaced every
tick, including after a failed physics step.

Picking follows collider volumes, not every rendered surface. Some visible
trunk/head positions can miss. The lower-casing point/ray/prop regression
checks its specific region; it is not a certificate of complete visual coverage.

The checked-in model is generated offline from the pinned XML descriptions in
[`upstream/`](upstream/). Conversion preserves mass, centre of mass and full
inertia tensors by rotating each body into its principal axes and transforming
its joint frames consistently. Simulation builds need no Python, MuJoCo or
asset download. [`UPSTREAM.md`](UPSTREAM.md) records provenance, licensing and
regeneration instructions.

The robot casings, dark camera/servos and broad feet are original procedural
geometry informed by [Pollen's official photos](https://pollen-robotics.com/microduck/press-kit/)
and the Apache-2.0 XML frames and metadata. Dimensions estimated from photos
are approximations, not exact CAD. No STL geometry or photo textures are used
for these rendered visuals; the collision data has separate mesh-derived provenance.
The imported XML and converted model retain their Apache-2.0 provenance; see
`UPSTREAM.md` for the separate upstream mesh license and appearance references.
The visual jaw moves with the head; this 14-joint model has no mouth actuator.
Geometry is cached locally and follows the body transforms; its vertex lighting
is baked from the first pose.

Each duck keeps a bounded ring of 64 transitions in ordinary match state:

- Simulation tick.
- Observation and next observation: 44 values in order—14 joint angles,
  14 joint velocities, trunk height, world COM linear velocity XYZ, world angular
  velocity XYZ, gravity direction in the original trunk frame XYZ, left/right
  sole world-Y contact force, smoothed step command, sine/cosine of step phase,
  and assistance flag (0/1).
- Action: 14 desired joint angles.
- Effort: 14 clamped actuator controls.
- Actual `assistanceForce` and `assistanceTorque` applied during the transition.
- This controller's grab request, requested collider/body-local anchor/world
  target, accepted body and actual `grabForce` / `grabTorque`. An unaccepted
  request has no accepted body or applied grab wrench.
- Reward: negative summed squared post-step joint-target error, an illustrative
  tracking score rather than a walking reward.

The pre-step observation is captured after any reset but before this tick's
controls, assistance toggle, phase update and physics step. Its foot
loads therefore describe the preceding step (zero after reset). The next
observation follows successful physics. Each foot value sums the mean world-Y
`Collider.contactForce` across its six marked sole spheres, including any
vertical friction contribution or contact with another object. It is not a
floor-only normal force or a real sensor reading. Failed steps append nothing.

Records follow snapshots and rollback. An exporter should consume verified ticks
in order; this fixture has no exporter, trainer or record of the exact rendered
observation a human used to choose an action. Grab records describe only that
duck controller; another player's grab can affect either duck or shared balls,
so a duck's ring is not a complete world dataset.

For a quiet, bounded desktop capture, save a script such as:

```text
3 key escape
80 key w down
200 capture duck-sandbox.png
220 key w up
```

Then run with `TIDE_HIDDEN=1` in the agent's shell:

```sh
tide run examples/robotics/microduck --release --script duck.txt --frames 260 --size 1280 720 --capture duck-final.png
```

Repository CMake targets include `newton_microduck`, `tide_newton_microduck`,
`tide_microduck_policy` (independent inference) and
`tide_newton_microduck_policy` (observations, controls and replay).

The final 174-collider model builds through the installed CLI for native and
web. Its all-direction boost policy host passes 13 deployment/replay tests on native and WASI,
with all four cadence-residue replay hashes matching. A 14-test native host
exercises 1,215 actual worker callbacks and matches serial world histories. Tests cover
the exact 61 inputs, first/subsequent filters, raw motor history at high back-EMF,
applied-effort friction history, reset, failures, WASD, no assistance, snapshot
ownership and canonical/delta transfer, including sampled Shift held/release,
reverse, boosted turns and diagonal inputs, plus actor inference history.
Fresh-process native→WASI and WASI→native
joins with policy and active grabs produce identical 128,254-byte files (SHA-256
`570cc830abd1ca6f0170f31a2d149060cc0027d321791c635e553688a31347fe`)
and continue through tick 600. Browser
W/Shift+W/A/R and F8/F7 smoke checks pass without errors, with screenshots
inspected. The final 12-second steady WebGL observation shows 32.08 ms/frame
(31 FPS), 2.35 ms/tick and 1.77 ms Newton work, with 401 ticks/sec and
12.9 ticks/frame under combined client/server load. Earlier fd6f and
preoptimization frame/tick/Newton snapshots were 45.58/2.74/2.35 and
86.09/5.23/4.83 ms. These are browser observations, not a paired benchmark or
evidence of 60 FPS or fully resolved browser lag. Full
rendered-surface picking coverage is not established. The inference-only
test preserves all 73 ONNX Runtime fixtures and the 512-step feedback hash
`1780e8e7b6265eef`. CMake regeneration in the supplied workspace is blocked by
the unrelated missing `bench/` directory; these checks used separately linked
hosts and installed CLI builds.

Physical acceptance is separate. The `fd6f4839` stability-basis native and WASI
hosts both report **7/8 passing**. All nine reported final whole-world hashes
match, including the repeated walk baseline in the run comparison and the
D/right failure. Measurements and source hashes are in
`build/microduck-run-check/final-test-summary.json` and
`locomotion-final-{native,wasi}.log` in that directory:

This matrix covers ordinary W/S/turns, forward boost and the three seeded
recoveries; it does not establish stable boosted reverse or turning.
Final `4f7a9110` walk/Shift+W checks pass 1/1 on each target and preserve the
earlier whole-world hashes `af41801b0af94572` / `dc74c754ef1b6356`.
Their current-source summary is `all-boost-jtp4-test-summary.json`.

| Default learned controller probe | Measured result |
| --- | --- |
| W, 7 s at +0.4 m/s request | +0.786932 m signed travel; 15/14 left/right loaded foot cycles; 16.984/17.089 mm peak sole clearance |
| S, 7 s at -0.4 m/s request | +0.863201 m travel in the requested backward direction; 11/11 cycles; 19.575/22.328 mm peak sole clearance |
| Shift+W, 7 s at +0.5 m/s request | +1.155245 m travel, 0.368313 m (46.8%) above W; 14/14 loaded cycles; minimum up 0.994069 and height 0.112773 m |
| Prone recovery | 1 s upright dwell confirmed at 1.340 s; final 2 s upright 100% |
| Side recovery | 1 s upright dwell confirmed at 2.115 s; final 2 s upright 100% |
| Supine recovery | 1 s upright dwell confirmed at 1.905 s; final 2 s upright 100% |
| Full A / D turning | Unwrapped yaw -3.316523 / +0.196558 rad; D/right fails the 0.25 rad minimum |

W/S probes settle for 2 s, command for 7 s, then release for 1 s. Their foot
cycles require >5 mm whole-sole clearance plus loaded touchdown dwell. Both
remain upright without assistance or grabs and slow after release: mean final
|body-forward speed| is 0.002281 m/s for W and 0.002169 m/s for S. Shift+W ends
at 0.004675 m/s horizontal speed after release. This supports
bounded unassisted travel, not exact speed tracking, reliable directional
turning, or recovery from arbitrary falls. Recovery seeds preserve policy history.
The same runtime `fd6f4839`, released weights, hardware/BAM controller and
constant geometry are used throughout. A separate fully fitted
150-sphere model changed the feet and failed walking; its results are not
evidence for this 174-collider model. The fit does not reproduce every source
collision or training material rule; the 17 named training overrides do not
apply to all 70 upstream robot collision geoms.

CTest registers the seven supported locomotion cases normally. The right-turn
acceptance case is explicitly disabled and labelled `known-limitation`; its
strict 0.25 rad assertion remains runnable directly in the target/full host.
The full eight-case host still measures 7/8. CMake configuration is blocked by
the unrelated missing `bench/` directory, so registration is inspected rather
than claimed as executed CTest coverage. This is not a clean full-suite pass.

The final runtime source SHA-256 is
`4f7a91102e1e4cd87bc3906885c02d1bb5e3cbb4578b8cee9495dff14abeb4fa`.
All **63 runtime cases pass on native and WASI**, including three new
stability regressions that failed with the old adapter. The contact block uses
a fixed 32-step quadratic bracket in the 16 interleaved sweeps; scratch is
rebuilt without persistent contact or warm-start state. These focused results
do not imply that the full repository suite passes with the extension.

The stability basis's 23 native manual behavior cases pass; focused pin reruns pass 1/1 on native
and WASI after the actual 280-tick hash `90ed0aaf9bb442c8` was observed before
repinning. The full WASI 24-case suite has not been repeated on the final source.
Earlier complete 24-case and browser-grab results are historical.

The adapter now anchors each virtual free-joint parent at the body's current
origin, with zero translation coordinates, to avoid float cancellation during
Cholesky factorization for small rotational inertia far from the world origin.
World poses and COM velocity conventions, public API and persistent state are
unchanged. A full-scene close-duck/ball/grab replay that failed at tick 1734,
substep 3 completes 4,000 ticks on the `fd6f4839` basis with both-target hash
`4cb7f65b76088097`. Ordinary W with both ducks and balls active also completes 4,000 basis ticks with
matching hash `658515fe75a71d1d`.
Reverse boost falling or boosted turns stumbling is a learned-controller
capability limitation, distinct from the corrected Newton numerical
`Unstable` failure.

The six-column block mass-times-Jacobian calculation skips structurally zero
mass blocks while preserving ordered nonzero arithmetic. A paired native-only
benchmark on a frozen previous-control idle scene improves from 3.38 to
1.72 ms/tick, with all three output arrays byte-identical over 1,020 ticks.
This bounded comparison does not imply universal equivalence. Separate basis ordinary-W runs measure
1.594 ms/tick native and 3.185 ms/tick WASI under load; these are host timings,
not browser FPS or a paired cross-target speedup. Centering intentionally changes
two old history pins, observed on both targets before pinning: state
`be6cce7a3d5e1828`, contact `445004fca1d80fa4`.

The final runtime also computes JᵀP with four ordered scalar accumulators and
an unchanged tail, without skipping terms, FMA or reassociation. A separate
paired native `nt_step` comparison against the frozen fd6f reference measures
1.459854 to 1.088080 ms per call (25.47%), with status and all three record
arrays byte-identical over 1,020 calls. One captured 35-body/28-joint/353-collider
snapshot also matches native/WASI SIMD output bytes, hash `d267a59f35210b5f`.
These are bounded checks. The 25.5% result is not a browser measurement and
must not be multiplied with the earlier block benchmark as a precise cumulative gain.

The following results are historical evidence for the preceding regularized
contact/manual controller, before the current Coulomb/stops/motor-history changes.
They do not validate the current learned locomotion profile.

All 33 backend runtime tests passed on native and WASI debug and release,
including nine independent stock Newton contact-reference samples checking
both state and forces. The historical contact and noncontact hashes were
`ddf1584fd2502ff1` and `92249536c6268981`. Independent integration review at
that milestone found no actionable issues.

With the earlier 5600/3/1/0.8 regularized contact settings and four substeps,
both signed patterns passed the unchanged >5 mm sole-envelope clearance and
contact-dwell checks. Each foot completed five lift/touchdown cycles in the ten-second probe.
Peak left/right clearances, measured from the actual sole spheres, are:

| Pattern | Left | Right |
| --- | ---: | ---: |
| +1 | 19.101 mm | 19.505 mm |
| -1 | 11.245 mm | 11.207 mm |

An unassisted topple initialized at a 0.35 m trunk height reached a minimum
sphere bottom of -15.692 mm, within the unchanged 20 mm transient penetration
budget. The settled minimum is -0.581 mm and the final trunk COM speed is
0.000097 m/s. These checks exercise full-body contact and assisted foot motion;
they do not demonstrate controlled travel or validate real-robot fidelity.

The following full-suite results describe the preceding manual sandbox revision,
before adding the learned controller and joint-friction fields. They are not a
full-suite validation of the policy port.

Game replay checks passed. The prior MicroDuck world hash `f87f0902ec4db694`
and arm hash `db6cb5835d7c9b26` agreed across native/WASI debug and release.
All 24 MicroDuck fixture cases passed in those four configurations, including
the final two-client rollback scenario. Both clients completed thousands of
replay ticks with zero resyncs. Active-grab fresh-process joins also passed,
with all four join files byte-identical. The full native debug workflow passed
2,554/2,554 tests and web debug passed 2,416/2,416, including WebGPU drawing.
Installed CLI native/web release builds and a bounded native capture passed.
Muted WebGL and WebGPU checks at that stiffness confirmed ball
grab/lift/release, head grab/lift/drop with assistance off, full-body floor
rest, reset of the owned duck and shared balls, and resumed recording. There
are no page exceptions or WebGL/WebGPU validation errors; only the known
autoplay warning remains. All 40 upstream hashes and 46 staged source/header
files matched. Other-platform coverage and independent physical fidelity remain
unvalidated; current learned locomotion measurements and limitations are above.
