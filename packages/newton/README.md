# Newton

Experimental **NVIDIA Newton** articulated physics for Tide, in the `Newton` namespace. It is a separate optional package alongside Box3D, with no silent Box3D fallback. It is unrelated to Newton Game Dynamics.

The package runs upstream Featherstone CPU kernels on native and WebAssembly. Its collision profile supports spheres, compound spheres and static planes, using Newton's compliant normal response and either default regularized friction or optional coupled Coulomb impulses. API names remain provisional and physical fidelity is unvalidated. Box/mesh collisions and continuous collision detection are not implemented; finite impulse sweeps do not guarantee exact global sticking.

MicroDuck's integrated 174-collider model preserves W/S travel and passes three
seeded get-ups on native and WASI. **D/right turning remains weak.** Random
falls, reliable directional turning and hardware fidelity remain unvalidated.
Its new mesh-derived collision data is CC BY-NC-SA 4.0, separate from the
Apache-2.0 XML/model and policy/code; see the example's
[`LICENSE-COLLISIONS.md`](../../examples/robotics/microduck/LICENSE-COLLISIONS.md).

The full [Newton package guide](../../docs/packages/newton.md) covers fields, status handling, observations, demonstration recording and current gaps. The website route is https://tide-engine.dev/packages/newton once these docs are published.

## Use

With a checkout or Tide installation containing this experimental package, run `tide add newton`, or put this line in the game's `tide.packages`:

```text
newton
```

This source example makes an anchored pendulum and applies bounded PD torque toward zero. Its gains and authored mass/inertia are illustrative. No shape or shape-derived inertia is implied.

```csharp
using Newton;

scene Main;
component Pendulum { }

event(Spawned) Build(with Main)
{
    var pivot = float3(0, 2, 0);
    var tilt = quaternion.AxisAngle(float3(0, 0, 1), 30deg);
    var anchor = Spawn(Body {
        type = BodyType.Static, position = pivot
    });
    var bob = Spawn(Body {
        position = pivot + Math.Rotate(tilt, float3(0, -1, 0)),
        rotation = tilt,
        mass = 1, inertia = float3(0.1)
    });
    Spawn(Joint {
        parent = anchor, child = bob,
        type = JointType.Revolute,
        childPosition = float3(0, 1, 0),
        axis = float3(0, 0, 1),
        position = 30deg
    }, Actuator { effortLimit = 10 }, Pendulum);
}

[Before(PhysicsStages.Step)]
system Control(Joint joint, mut Actuator actuator, with Pendulum)
{
    actuator.effort = Math.Clamp(
        -20 * joint.position - 4 * joint.velocity,
        -actuator.effortLimit, actuator.effortLimit);
}
```

`Joint` lives on a separate entity. Its local frames locate the joint relative to each body's origin; a default parent anchors to the world. A static parent root with a dynamic child is supported; an explicit joint with a static child is rejected. The graph must be acyclic with at most one inbound joint per child. Remove joints that reference a body when destroying it; stale references fail the step.

Write controls, body forces/torques and resets before `PhysicsStages.Step`. After that phase, check `Physics.status == Status.Ok` before reading new `Joint.position`/`velocity`, body poses/velocities or `Actuator.appliedEffort`. Failure retains the previous published results and pending body forces/torques. Forces/torques clear after success; actuator effort persists until changed. Reset jointed motion through joint coordinates, and free roots through body poses/velocities.

Actuator effort is Nm for revolute joints and N for prismatic joints. `effortLimit` is a finite, nonnegative package control clamp. Passive forces are outside the clamp; `appliedEffort` is the clamped control, **not a torque sensor**. Fixed joints have no driven coordinate. Body mass, diagonal inertia about `centerOfMass`, and that body's local centre of mass are authored values.

`Joint.friction` is a finite, nonnegative dry-friction stopping-effort budget
(Nm for revolute, N for prismatic), defaulting to zero. Each substep applies
bounded impulses through the full articulated inverse mass, including free
roots and armature. The legacy joint-only path uses eight deterministic
projected Gauss–Seidel sweeps; active Coulomb contacts select 16 sweeps that
interleave joint and contact impulses through the same Newton H/Cholesky L.
The impulse magnitude is at most `substepDt * friction`; it can hold a stopped
joint within that budget. Solver scratch starts from zero each substep. A zero
budget bypasses joint-friction work; active Coulomb contacts still solve.
`Joint.load` reports the final substep's signed generalized nonmotor load,
including gravity, inertial bias, contacts and position limits, excluding
actuator effort, passive joint damping and joint friction. It describes the
state before that substep's integration, is output only, and publishes only
after the complete tick succeeds. Fixed joints report zero. Controllers reading
it on the next tick therefore use a previous-state load estimate.

## Colliders

Put a `Collider` on a body, or reference a body explicitly from separate
collider entities for compound geometry. `Sphere` is the default type and uses
a positive `radius` in metres. `Plane` requires a static body and uses local
+Y as its outward normal. Shape `position`/`rotation` are relative to the body
origin. Colliders do not infer or change mass or inertia.

```csharp
Spawn(Body { type = BodyType.Static }, Collider { type = ColliderType.Plane });
Spawn(Body { position = float3(0, 0.3, 0) }, Collider { radius = 0.05 });
```

`enabled` defaults to true. Matching nonzero `exclusionGroup` values suppress
a pair; same-body and static/static pairs are ignored. This allows a robot's
compound spheres to collide with the floor and props without colliding with
its own joints/casings. All references include entity generations.

Contact settings default to `stiffness = 2000`, `damping = 5`,
`frictionDamping = 100`, `friction = 0.8`, and
`frictionModel = ContactFriction.Regularized`. The enum is
`ContactFriction { Regularized, Coulomb }`. Both colliders must select
`ContactFriction.Coulomb` for the impulse model; a mixed pair retains the
upstream regularized response. Regularized material coefficients are averaged.
Coulomb keeps upstream normal stiffness/damping averaging and normal force
unchanged, and uses `sqrt(muA) * sqrt(muB)` for pair friction. Either zero
friction coefficient gives zero tangential budget.

Coulomb bounds the two-tangent impulse by `substepDt * muPair * normalForce`.
The quadratic contact block uses a fixed 32-iteration multiplier bracket in
the saturated full-rank case, within 16 interleaved joint/contact sweeps through
the same articulated mass matrix. Impulses start from zero each substep, with
no persistent contact/warm-start state. This supports bounded stopping/sticking,
not exact global sticking or hard nonpenetration. Contact geometry refreshes
every substep. These settings are not a calibrated material model. Fast motion
can tunnel; there is no continuous collision detection. Public defaults and
the legacy regularized path are unchanged.

After a successful tick, `contactForce` is the mean world force on that
collider over all substeps, including friction. It is not a sensor model.
Disabled colliders report zero on success. Invalid configurations fail with
`InvalidCollider`; failed steps retain all previously published results.

## One simulation for games and policies

A game, a human controller and a robotics policy use the same components and physics step. Reset episodes through Tide's ordinary world snapshot/restore path, including controller state in the match.

The runnable [two-player arm example](../../examples/robotics/arm/) uses Rooms and each arm owner's keyboard/gamepad input for target reaching. Each `Arm` retains at most 128 transitions: simulation tick, pre-step joint observation, desired joint angles, actual clamped effort, next observation and reward. These describe executed match state before/after the step; they are not an exported dataset or a record of the exact view a human saw when choosing input. Export verified ticks in tick order to account for rollback; exact human-view timing needs additional capture.

All persistent state is ordinary Tide components/lists/maps, with no `Resident` arena. Solver model arrays and scratch are reconstructed per call, so separate worlds retain no shared solver state.

The [MicroDuck physics sandbox](../../examples/robotics/microduck/) uses the
published 15-body, 14-hinge model with a free trunk at 200 Hz and four physics
substeps. The model has 174 sphere colliders per duck:
131 mesh-derived support spheres and 26 source-inscribed cores for all 13
nonfoot bodies, 16 original foot proxies including 12 soles, and one appended
procedural trunk-casing core. The full scene has 353 colliders.
One constant model serves walking, get-up and grabbing. They collide with an infinite static
plane, the other duck and four shared balls; each duck excludes its own internal
contacts. The drawn floor is 4 × 3 m. These are approximate compound shapes,
not runtime triangle-mesh collisions. Sampled support error of the nonfoot mesh-derived subset is
approximately 5.5 mm (maximum 5.542759 mm); feet remain approximate, and the fit
is not an exact source hull. Each nonfoot body has two interior cores at least
2 mm inward of the baseline support envelope. The extra body-0 visual core
at `(.004, 0, -.006)` m, radius `.0115` m, closes the visible lower-trunk
pick/prop hole in a targeted regression. It is not mesh-inscribed and
expands selected support by 6.67484 mm, outside the source-fit certificate.
That regression covers its specific region; some visible trunk/head positions
still miss the collider volumes, so full visual picking coverage is unvalidated.
Collision provenance and fixed data are
described in the example's `README-COLLISIONS.md`.

Hold the left mouse button to grab and drag any marked part of either duck or
a ball. The default controller runs the released VelStand v8 weights at 50 Hz:
W/S requests speed and A/D requests left/right turning. Holding either Shift
boosts signed forward/reverse requests from 0.4 to 0.5 m/s and yaw requests
from 1 to 1.25 rad/s, including diagonals. Learned turning is `turn = -drive`;
manual trimming is unchanged.
No external balance or lane forces
are applied in learned mode. M switches to the older manual controller, where
Q/E selects a joint, A/D trims its target and B toggles labelled balance help.
R resets your duck, its controller/recording and all balls, without
resetting the other duck; body revisions cancel old grabs on reset bodies.
The weights, normalizer and inference order are preserved; the game implements
the hardware action-filter profile and pinned BAM electrical/dry-friction
parameters. Gearbox friction uses previous successful `Actuator.appliedEffort`
and `Joint.load`; battery sag uses separate raw motor history before the
effort ceiling. Reset clears motor histories, requested/applied effort and load
before controls run on that tick. The game opts all shapes into Coulomb, with
sole/floor/ball friction 0.8 and nonsole duck friction 0, compliant stops 55/0.6
and a 0.4 m/s normal command scale. The tested 0.5 reverse boost fell, and
boosted turning can stumble. Boost changes the requested velocity/yaw in the
same physics, with no fast-forward or artificial assistance; it guarantees
neither stable motion nor a particular real speed.

The fd6f-basis 174-collider native/WASI hosts measure W/S signed travel of
0.786932/0.863201 m over 7 s, with 15/14 and 11/11 loaded left/right foot cycles
above 5 mm clearance, zero assistance and slower motion after release. Prone
recovery confirms a one-second upright dwell at 1.340 s; side and supine confirm
at 2.115/1.905 s. All three are upright for the final 2 s. Shift+W travels
1.155245 m, 0.368313 m (46.8%) above normal W, with 14/14 loaded cycles and
0.004675 m/s final release speed. Full A/left turns -3.316523 rad, while D/right
reaches only +0.196558 rad. Both hosts pass 7/8; all nine emitted final
whole-world hashes match, including the repeated W baseline and known failure.
These trials do not establish reliable directional turning, arbitrary-fall
recovery or hardware fidelity. The earlier no-BAM/61-sphere failures do not
describe this model or isolate a general cause. The default game retains
hardware/BAM control and unchanged weights. The manual controller retains its assisted-step
mode. See the example's policy README for provenance and numerical evidence.
Use `tide run examples/robotics/microduck --release` for play; the unoptimized
CLI solver build is too slow for this full articulation.

Right-turn acceptance is explicitly disabled in CTest with label
`known-limitation`; its strict 0.25 rad assertion remains directly runnable in
the full eight-case host, which measures 7/8. Seven supported cases register
normally. Missing unrelated `bench/` blocks CMake configuration, so registration
is inspected and host checks are separate; no clean full-suite pass is claimed.

Each duck's 64-transition ring records 44-value pre/post observations (joint
q/qd, trunk height/velocities, projected gravity, sole loads, step command/phase
and assistance flag), desired angles, clamped efforts, actual assistance wrench,
and that controller's grab request, accepted body and actual force/torque. Sole
loads sum the six colliders' mean vertical world contact forces per foot,
including friction and contacts with any object. Other-player interactions can
also affect a duck, so its ring is not a complete world dataset. The same
verified-tick export and human-view timing limits as the arm apply. Full authored
inertia tensors, XML pins and offline conversion are unchanged. Original
procedural casings use approximate dimensions in source visual frames, with
no STL geometry or photo textures used for rendering. Collision support data
was privately derived from upstream STL meshes; raw STL files are not shipped.
That data's CC BY-NC-SA 4.0 license is separate from the Apache-2.0 XML/model
and policy/code.

## Upstream CPU kernels

- Newton: `ea4899864046dcd5981f8ba0bc3d8c25e65dcb96`.
- Warp: `b346b941a4c0cd19c510db67d6506d781464ed5b`, version `1.19.0.dev20261020`.
- [`tools/export.py`](tools/export.py) generates the checked-in `kernels/featherstone.inl` and `kernels/contacts.inl` and vendors their headers. Regeneration needs the pinned source checkouts and Python/Warp environment; game builds do not run Python.
- Native and WebAssembly compile the same scalar CPU source without a C++ runtime, Warp JIT or CUDA dependency. The selected solver path uses Tide's deterministic math with FMA/fast math disabled, a WebAssembly-safe native-half guard, private launch wrappers, LP64-compatible integer types and value-initialized generated temporaries. [`UPSTREAM.md`](UPSTREAM.md) records the patches.
- Upstream and third-party notices are beside the vendored source in [`vendor/`](vendor/); generated-source/header hashes are in [`UPSTREAM.sha256`](UPSTREAM.sha256).

## Scope and validation

The first integration covers authored body properties, free/static roots,
fixed/revolute/prismatic joints, effort actuation, body forces/torques and
sphere/plane contacts. Collision queries, other shape types, general URDF/MJCF
import, CUDA execution, joint velocity constraints, simulated sensors
and trainers remain unimplemented. Position limits and contacts are compliant;
optional Coulomb friction uses bounded impulses with finite sweeps. Physical
fidelity and sim-to-real transfer are not established.

Final runtime SHA-256 prefix `4f7a9110` passes **63 runtime cases on
native and WASI**, including three new stability regressions. Coverage includes independent
stock Newton state/force comparisons for the legacy path, analytic dynamics,
coupled stopping/sticking, floating-root response, load/force readback, malformed
inputs, failure atomicity, replay and interleaved worlds. Reference agreement
for the legacy path does not establish upstream equivalence for the extension.

The final installed CLI native/web builds pass. The all-direction boost
host passes **13 common cases on native and WASI** and
**14 on threaded native**, with 1,215 actual worker callbacks, including
Shift held/release, reverse/turn/diagonal full/delta inputs and actor history. All four cadence
replay hashes match between targets. Fresh-process joins in both directions
produce identical 128,254-byte files, SHA-256
`570cc830abd1ca6f0170f31a2d149060cc0027d321791c635e553688a31347fe`.
Browser W/Shift+W/A/R and F8/F7 checks pass without errors, with screenshots
inspected. A final 12-second steady WebGL observation shows 32.08 ms/frame
(31 FPS), 2.35 ms/tick and 1.77 ms Newton work, 401 ticks/sec and 12.9 ticks/frame
under combined client/server load. These snapshots are not a paired benchmark
or evidence of 60 FPS or fully resolved browser lag. Full visual-surface picking
coverage remains unvalidated.
The physical locomotion failures above remain separate from these contract
and replay successes. No complete repository-suite pass is claimed for the
current extension.

The stability basis's 23 native manual behavior cases pass, and both target pin reruns pass 1/1
after observed agreement on the 280-tick hash `90ed0aaf9bb442c8`. The full WASI
24-case suite has not been repeated on the final source. Current policy/run/join
evidence is in `build/microduck-run-check/all-boost-jtp4-test-summary.json`;
the full eight-case basis is `final-test-summary.json` in that directory.

Free-root virtual parent frames are now centered at the current body origin
with zero initial translation coordinates. This prevents float cancellation
in Cholesky factorization for tiny rotational inertia far from the origin,
preserving public COM/world conventions without new state or API. A previously
failing full-scene close-duck/ball/grab replay reaches 4,000 ticks on the fd6f
stability basis on both
targets, hash `4cb7f65b76088097`; ordinary W with both ducks and balls active
also reaches 4,000 basis ticks, hash `658515fe75a71d1d`.
Reverse boost falling or boosted turns stumbling is a learned-controller
capability limit, distinct from the corrected Newton numerical `Unstable` error.

A six-column mass-times-Jacobian block optimization skips structural zeros in
the same ordered nonzero arithmetic. A paired native benchmark on a frozen
previous-control idle scene improved 3.38 to 1.72 ms/tick, with three arrays
byte-identical over 1,020 ticks. Separate basis loaded headless W runs measure
1.594 ms/tick native and 3.185 ms/tick WASI; these are not browser FPS or a
paired cross-target speedup. Centering intentionally changes the observed
state/contact pins to `be6cce7a3d5e1828` / `445004fca1d80fa4`.

The final JᵀP four-column scalar accumulation keeps all terms and their order,
without FMA or reassociation. A separate paired native `nt_step` comparison
measures 1.459854 to 1.088080 ms/call (25.47%), with status/three record arrays
byte-identical over 1,020 calls. One captured 35/28/353 full-scene snapshot
matches native/WASI SIMD bytes, hash `d267a59f35210b5f`. This bounded result is
not browser performance or a precise cumulative gain with the older benchmark.
Final-source walk/Shift+W checks pass 1/1 per target and retain the basis
world hashes; the full eight-case basis remains 7/8, with weak D/right.

The following full-workflow counts and game hashes predate joint friction,
Coulomb and the learned policy; they are historical validation of the preceding
manual sandbox. The full native
debug workflow passed 2,554/2,554 tests, and web debug passed
2,416/2,416, including WebGPU drawing. The historical MicroDuck/arm hashes were
`f87f0902ec4db694` / `db6cb5835d7c9b26`. All 24 MicroDuck fixture cases passed
across native/WASI debug and release. Active-grab fresh-process joins and
two-client rollback also passed; the four join files were byte-identical and
both clients completed thousands of replay ticks with zero resyncs. That revision's
installed CLI native/web release builds, 40 upstream hashes and 46 staged
source/header comparisons also passed.
Other-platform coverage, broader measurements and independent physical-fidelity
validation remain outstanding. Detailed evidence is recorded in
[`notes/newton.md`](../../notes/newton.md) and the example's README/`UPSTREAM.md`.
