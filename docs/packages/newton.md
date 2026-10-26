---
description: "Experiment with NVIDIA Newton articulated bodies, sphere and plane collisions, joints and effort control in the same Tide match used for games and robotics policies."
---

# Newton — articulated physics

`newton` is an optional, experimental package for **NVIDIA Newton** articulated physics, separate from [Box3D](./box3d.md). It exposes a small part of Newton's Featherstone solver for bodies, joints and effort control. This is not Newton Game Dynamics, and the package never substitutes Box3D for Newton.

::: warning Experimental scope
The package runs upstream Featherstone CPU kernels and a small collision profile for spheres and static planes. Regularized friction remains the default; coupled Coulomb impulses require explicit opt-in on both shapes. The Tide API names below remain provisional and physical fidelity is unvalidated. Compound spheres approximate robot casings; these are not mesh collisions or a validated model of the real robot's ground contact.

MicroDuck's integrated 174-collider model supports W/S travel, an all-direction Shift boost, and seeded prone, side and supine get-ups on native and WASI. Forward boost has a measured travel gain; reverse boost fell in its trial, and boosted turns may stumble. D/right turning remains weak. These results do not establish arbitrary-fall recovery, reliable directional turning or hardware fidelity. Collision volumes do not cover every rendered surface.
:::

Game controls and robotics policies write actions into the same match components and use the same physics step. There is no separate training engine or training-only physics path. The package supplies dynamics and observable state; it supplies no policy trainer or automatic robot model.

## Add the package

In a Tide checkout or installation containing this experimental package:

```sh
tide add newton
```

Its line in `tide.packages` is `newton`, and its namespace is `Newton`:

```csharp
using Newton;
```

Games that omit it compile and ship none of its code. Ordinary game builds use checked-in, ahead-of-time generated CPU kernels, with no Python, Warp JIT, CUDA or C++ runtime dependency. Native and WebAssembly compile the same scalar CPU source using Tide's shared deterministic math.

## An anchored pendulum and bounded controller

This source example creates a static anchor, a dynamic body one metre below its joint, and a separate revolute-joint entity. The authored mass, inertia and controller gains are illustrative; no collision shape is created or used to calculate them. It starts at 30 degrees and applies PD torque toward zero.

```csharp
using Newton;

scene Main;
component Pendulum { }

component Observation
{
    Status status;
    float angle;
    float speed;
    float appliedControl;
}

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
    }, Actuator { effortLimit = 10 }, Pendulum, Observation);
}

[Before(PhysicsStages.Step)]
system Control(Joint joint, mut Actuator actuator, with Pendulum)
{
    actuator.effort = Math.Clamp(
        -20 * joint.position - 4 * joint.velocity,
        -actuator.effortLimit, actuator.effortLimit);
}

[After(PhysicsStages.Step)]
system Observe(Joint joint, Actuator actuator, Physics physics,
    mut Observation observation, with Pendulum)
{
    observation.status = physics.status;
    if (physics.status != Status.Ok) return;
    observation.angle = joint.position;
    observation.speed = joint.velocity;
    observation.appliedControl = actuator.appliedEffort;
}
```

`Control` reads the joint's current coordinate and speed and writes this tick's effort. `Observe` records the completed step only when it succeeds. A game can instead derive the effort from its [input](../game/input.md) or [commands](../game/commands.md); a policy uses the same write and ordering. Keep actions that affect the match in match state, with outside-service decisions entering through input.

## Bodies

`Body` contains the complete authored rigid-body state. Its default type is `BodyType.Dynamic`; `BodyType.Static` anchors a root.

| Fields | Meaning |
|---|---|
| `position`, `rotation` | World-space body origin and orientation; rotation defaults to `quaternion.identity` |
| `velocity` | World-space centre-of-mass velocity, in metres/second |
| `angularVelocity` | World-space angular velocity, in radians/second |
| `mass` | Authored mass in kilograms; default 1 |
| `inertia` | Authored diagonal inertia about the centre of mass in body space, in kg·m²; default `(1, 1, 1)` |
| `centerOfMass` | Centre of mass relative to the body origin; default zero |
| `force`, `torque` | World-space applied force in newtons and torque in newton-metres |

Dynamic bodies need positive finite mass and inertia. Static roots may have zero mass. Supply finite values and unit quaternions. Mass, inertia and the centre of mass are authored directly, never inferred from a mesh or shape.

A dynamic body with no inbound joint is a free root. Root poses and velocities are writable resets. For a jointed child, the solver reconstructs pose and velocity from the root and joint coordinates, so reset `Joint.position` and `Joint.velocity` to change its articulated state. Body parenting through `Transform` does not create a physics joint.

Forces and torques last through all substeps of one tick and clear after a successful step. Write them before each tick that should apply them. A failed step retains them for a later attempt.

## Collision shapes and floors

Add a `Collider` to a body, or create separate collider entities whose `body`
references that body to build a compound shape. A body without colliders still
has dynamics but does not collide. Colliders never replace its authored mass,
centre of mass or inertia.

```csharp
Spawn(Body { type = BodyType.Static }, Collider { type = ColliderType.Plane });
Spawn(Body {
    position = float3(0, 0.3, 0), mass = 0.1, inertia = float3(0.0001)
}, Collider { radius = 0.05 });
```

The first entity is a static floor through the origin; the second is a falling
sphere. `Collider.type` defaults to `Sphere`.

| Fields | Meaning |
|---|---|
| `body` | Referenced body; default means this collider's own entity |
| `enabled` | Whether to collect the collider; defaults to true |
| `type`, `radius` | `Sphere` with a positive radius in metres, or `Plane` |
| `position`, `rotation` | Shape transform relative to the body's origin; rotation defaults to identity |
| `exclusionGroup` | Nonnegative group; equal nonzero groups do not collide |
| `stiffness`, `damping` | Contact spring and damping coefficients; defaults 2000 and 5 |
| `frictionDamping`, `friction` | Tangential damping and friction settings; defaults 100 and 0.8 |
| `frictionModel` | `ContactFriction.Regularized` (default) or `ContactFriction.Coulomb`; both shapes must select Coulomb for the impulse solve |
| `contactForce` | Mean world-space contact force on this collider over the last successful tick, including friction |

A plane's local +Y normal points out of its solid half-space. Planes require a
static body; their transform may translate or rotate the floor. Sphere pairs
and sphere/plane contacts are supported. Same-body pairs and static/static
pairs are ignored. Give a robot's colliders one nonzero exclusion group to
disable internal collisions while preserving collisions with other robots,
props and the floor.

Material coefficients must be finite and nonnegative. The public enum is
`ContactFriction { Regularized, Coulomb }`. A mixed pair retains upstream
regularized friction and coefficient averaging. A Coulomb pair retains the
unchanged upstream compliant normal force and normal stiffness/damping
averaging, with tangential coefficient `sqrt(muA) * sqrt(muB)`. Either zero
coefficient makes the tangential budget zero. Existing friction/default values
and the regularized path remain unchanged.

Coulomb uses a circular two-tangent impulse budget
`substepDt * muPair * normalForce`. Joint-friction and contact impulses
interleave for 16 fixed sweeps through Newton's same H/Cholesky L mass solve,
including floating roots and armature. The saturated full-rank quadratic
contact block uses a fixed 32-iteration multiplier bracket. Impulses start at
zero each substep; there is no hidden persistent contact or warm-start state.
This permits bounded stopping/sticking, without exact global sticking or hard
nonpenetration guarantees. Contact geometry and forces refresh at each physics
substep. Stiffer contacts and lighter bodies may require more substeps. There
is no continuous collision detection: sufficiently fast objects can pass
through each other between steps.

Contact force is a simulation readback, not a real sensor. Read it after
`PhysicsStages.Step` and only when status is `Ok`. Disabled colliders report
zero after a successful tick. Keep referencing collider entities consistent
with body lifetime; stale body references fail the step.

## Joints and actuators

A `Joint` is a component on a **separate entity** connecting `child` to `parent`. A default `parent` means the world. An explicit static parent root with a dynamic child is supported; an explicit joint whose child is static fails the step.

| Fields | Meaning |
|---|---|
| `type` | `JointType.Fixed`, `Revolute` or `Prismatic` |
| `parentPosition`, `parentRotation` | Joint frame relative to the parent's body origin; world space for a default parent |
| `childPosition`, `childRotation` | Joint frame relative to the child's body origin |
| `axis` | Motion axis in the joint frame; default `(1, 0, 0)`; supply a finite nonzero direction for moving joints |
| `position`, `velocity` | Actual joint coordinate and speed, also writable resets; radians and radians/second for revolute, metres and metres/second for prismatic |
| `damping` | Nonnegative joint damping; default zero |
| `armature` | Nonnegative additional joint inertia; default 0.01 |
| `friction` | Nonnegative dry-friction stopping budget; default zero, in Nm for revolute or N for prismatic |
| `load` | Read-only last successful final-substep nonmotor generalized load, excluding control, passive damping and dry friction |
| `limited`, `lower`, `upper` | Enable a position limit and its bounds, in the coordinate's units; disabled by default |
| `limitStiffness`, `limitDamping` | Nonnegative position-limit response settings; defaults 10000 and 100 |

Frame rotations default to identity. Position limits use a spring/damper response, not a hard positional stop. Damping, armature, friction and limit settings must be finite, and enabled bounds must satisfy `lower <= upper`. Dry friction applies bounded impulses through the coupled articulated mass matrix, including static holding. The legacy joint-only path uses eight fixed sweeps per substep; active Coulomb contacts select the 16 interleaved sweeps described above. Zero joint friction bypasses joint-friction work while contact friction can still solve. There is no velocity-limit constraint. `load` is model-derived telemetry from before the final integration, including contact impulses, not a sensor or tick average.

Put an optional `Actuator` on the joint entity to supply generalized effort: torque in Nm for a revolute joint, force in N for a prismatic joint. Fixed joints have no driven coordinate.

- `effort` is the requested control and persists until game code changes it.
- `effortLimit` defaults to 100 and must be finite and nonnegative. The package clamps control to `[-effortLimit, effortLimit]` before passing it to Newton. This is a package control clamp, not a Newton solver constraint on total effort: passive damping and limit forces are outside it.
- `appliedEffort` is the last successful step's clamped control, excluding passive forces. It is **not a torque or force sensor**.

Each child may have at most one inbound joint. The graph must be acyclic. Missing or stale body references fail the step; destroying a body does not silently delete its joints. Remove referencing joint entities as part of the same structural change. Normal [deferred entity changes](../game/entities.md) still apply at tick end.

## Step order, results and errors

Write controls, forces and resets in `[Before(PhysicsStages.Step)]` systems. Read `Physics.status` and completed body/joint results in `[After(PhysicsStages.Step)]` systems. Order against the whole phase, rather than the package's internal collection or publication systems.

`Physics.gravity` defaults to `(0, -9.81, 0)` and `Physics.substeps` to 4. A tick steps for `Time.dt` total, split into that many substeps. Use a positive substep count and finite gravity.

| `Status` | Meaning |
|---|---|
| `Ok` | The step succeeded and published results |
| `InvalidBody` | Invalid body state |
| `InvalidJoint` | Invalid joint/control values or body references, including a static child |
| `InvalidTopology` | Invalid articulation graph, such as a cycle or multiple inbound joints |
| `TooLarge` | Size or scratch-allocation limits prevented the step |
| `Unstable` | Invalid step settings or a nonfinite solver result |
| `InvalidCollider` | Invalid shape/material values, missing body reference or a plane on a dynamic body |

On failure, no new body poses, joint coordinates, `appliedEffort` or `contactForce` values publish. Retained values are not fresh observations; check status after every step. Correct the state or reset the episode before treating it as usable data. An `Ok` status is a numerical step result, not a physical-accuracy certificate.

## Games, policies and demonstrations

Observe actual `Joint.position`/`velocity` and `Body.position`/`rotation`/`velocity`/`angularVelocity` after a successful step. Choose observation normalization, rewards and action mapping in ordinary game code. There are no packaged cameras, IMUs, encoders, contact sensors or measurement-noise models.

The runnable `examples/robotics/arm/` example is a two-player target-reaching task using [Rooms](./rooms.md). Each arm uses its owner's keyboard/gamepad input to choose desired shoulder/elbow angles; an ordinary PD controller maps those angles to bounded effort before the same physics step a policy would use. Each `Arm` stores a ring of at most 128 transitions containing:

- The simulation tick and pre-step joint positions/velocities.
- The action as desired joint angles and the actual clamped actuator efforts.
- The successful post-step joint positions/velocities and target-reaching reward.

These records describe executed match state around a step. The example does not export a dataset or record the exact view and timing a human used to choose input. For that kind of human demonstration, capture the observation **used to choose the action** and associate it with the action's match-time application. Route local actions through normal input or commands, and export verified ticks in tick order so prediction and rollback do not leave duplicate or superseded samples. A transform animation or pose-only replay does not capture this control history.

Use Tide's standard world snapshot/restore path for an episode reset. Bodies, joints, actuators and any controller/reward state you put in the match then reset together. There is no separate Newton simulation to reset or training-only solver to switch to. The package keeps persistent state in ordinary Tide components, lists and maps, with no `Resident` arena; model arrays and native solver scratch are rebuilt on each call. Replay, alternate worlds, snapshots, packing and two-client rollback have focused test coverage.

## MicroDuck physics sandbox

`examples/robotics/microduck/` is a two-player physics sandbox with the released
**VelStand v8 pretrained policy** and an optional manual/assisted controller,
using the published 15-body, 14-hinge MicroDuck model. Its trunk is
a free dynamic root. Package colliders provide contacts at a 200 Hz tick rate
with four physics substeps. The model has 174 compound
sphere colliders per duck: 131 mesh-derived support spheres and 26 interior
source-inscribed cores on all 13 nonfoot bodies, 16 original foot-body proxies
including six soles per foot, and one appended procedural trunk-casing core.
The full scene has 353 colliders. One constant model serves walking, recovery and
grabbing. A static infinite
plane supports both ducks and four shared balls; the drawn floor is 4 × 3 m.
Each duck's exclusion group suppresses internal collisions while retaining
contacts with the floor, the other duck and balls.
Run it with `tide run examples/robotics/microduck --release --stats`; the unoptimized
CLI solver build is too slow for this full articulation.

The default learned controller runs at 50 Hz without external balance or lane
forces. Its 61-value observation and previous raw-action/filter/motor history
are ordinary rollback state. Native and WASI execute the same fixed C actor and
unchanged weights. Normal forward/reverse requests use 0.4 m/s and yaw requests
use 1 rad/s. Holding either Shift sets the match `run` flag, selecting signed
0.5 m/s translation and 1.25 rad/s yaw, including reverse and diagonal input. Bounded probes
of the 174-collider geometry show unassisted travel in both W/S directions and all three
seeded recoveries. **D/right turning remains weak; arbitrary-fall recovery and
reliable directional turning are not
established.** The
repository's `examples/robotics/microduck/README.md` describes the hardware
target-filter profile, measured limits and missing training-plant features.

| Control | Action |
| --- | --- |
| Hold left mouse button and drag | Grab a marked part of either duck or a shared ball |
| Hold W/S, Up/Down or left stick Y | Request learned forward/backward velocity; manual mode selects opposite step patterns |
| Hold either Shift | Boost learned forward/reverse requests to ±0.5 m/s and turning to ±1.25 rad/s; stable motion is not guaranteed |
| Q/E, Left/Right or D-pad Left/Right | Select a joint |
| A/D or left stick X | Request left/right turning; D/right remains weak; manual joint trimming is unchanged |
| M | Switch learned/manual mode and clear policy feedback |
| B or gamepad North | Toggle balance, heading and lane assistance in manual mode |
| R or gamepad South | Reset your duck, its controller/ring and all balls |

Reset places all bodies coherently at STAND with a 0.12 m trunk height. Joint
selection persists; controls still run on the reset tick. The other duck is
not reset. Reset clears raw/applied motor histories, joint requested/applied
effort and load before controls run on that tick. Body revisions change on reset, invalidating either player's old
grab anchors on those bodies. The illustrative PD gains are 4/0.03 with a
0.96 Nm motor-control clamp, retaining authored damping and armature in manual
mode. Learned mode uses the pinned BAM electrical and load-dependent friction
parameters, with no external balancing forces. Gearbox friction uses preceding
successful `Actuator.appliedEffort` and `Joint.load`; battery sag uses separate
raw motor torque history before the effort ceiling. Physical joint stops use
compliant gains 55/0.6; these are not hard clamps or an exact MuJoCo limit mapping.

All fixture colliders use `stiffness = 5600`, `damping = 3`,
`frictionDamping = 1`, `frictionModel = ContactFriction.Coulomb`. Soles, floor
and balls use friction 0.8; nonsole duck spheres use 0. The package retains
upstream Newton's compliant normal response and couples tangential impulses
with joint friction. The nonfoot support data was derived privately from
upstream STL geometry, with approximately 5.5 mm sampled support error for that nonfoot subset
(maximum 5.542759 mm). This does not
provide exact source hulls or an all-direction surface guarantee. The 26
interior cores add volume, with two in each nonfoot body at least 2 mm inward
of the baseline support envelope. Those 26 cores retain their source-inscribed
certificates. The appended body-0 visual-casing core is at `(.004, 0, -.006)` m
with radius `.0115` m. It is not mesh-inscribed and expands selected support
by 6.67484 mm, outside that certificate. It closes the visible black-case
pick/prop hole in the targeted regression, which failed on 173 spheres. That
check covers its specific region, rather than every rendered surface: some
visible trunk/head pointer positions still miss. Feet retain their original approximation.

**The mesh-derived collision data is CC BY-NC-SA 4.0**, separate from the
Apache-2.0 XML/inertial model and policy/code. Raw STL files are not
redistributed. The example's `LICENSE-COLLISIONS.md` and
`README-COLLISIONS.md` record attribution, fixed source hashes, frozen data
and export provenance. These identify the data's origin, rather than exact
training collision identity; the 17 named training material overrides cannot
be applied indiscriminately to all 70 upstream robot collision geoms.

Mouse picking uses displayed body poses and a local drag plane. Bounded absolute
input carries the chosen collider, body-local sphere anchor and world target.
Match code checks full entity generations, the enabled `Grabbable` sphere,
anchor position and `SandboxBody` revision. A changing serial supplies a
0.25-second lease measured from elapsed match ticks. The grab damper uses COM
velocity plus `angularVelocity × arm`; torque is `arm × force`, with the arm
from COM to anchor. Force is scaled to respect both 12 N and 0.6 Nm caps,
preserving a wrench applied at that point. Releasing the button stops the grab.

Each duck retains at most 64 transitions. Its 44-value observation order remains:
14 joint angles,
14 joint velocities, trunk height, world COM linear velocity XYZ, world angular
velocity XYZ, world-down direction in the original trunk frame XYZ, left/right
sole loads, smoothed step command, sine/cosine of phase, assistance
flag. The pre-observation follows any reset but precedes controls; the next
follows successful physics. The two sole fields sum each foot's six colliders'
mean vertical world contact forces over the tick, including friction and
contacts with any object. Pre contains the previous cached loads (zero after
reset); post contains this successful tick's readback.
Actions are 14 desired angles; efforts are 14 successful clamped controls.
Actual assistance force/torque and this controller's grab request, accepted body
and applied grab force/torque are separate fields. Another player's interactions
can also affect the duck, so one controller's ring is not a complete world
dataset. The negative summed squared joint-target error is an illustrative
tracking score, not a walking reward. The same verified-tick and human-view
timing limits above apply; there is no exporter or trainer.

The unchanged XML pins and offline conversion preserve masses, centres of mass
and full inertia tensors through principal-axis frames; game builds need no
Python or MuJoCo. Original procedural casings, camera/servos and feet use
[official photo references](https://pollen-robotics.com/microduck/press-kit/)
and Apache-2.0 XML frames/metadata. Photo-estimated dimensions are approximate,
not exact CAD. No upstream STL geometry or photo textures are used for the
rendered visuals; the collision support data has separate mesh-derived provenance. The
example's README and `UPSTREAM.md` detail controls, provenance and the source pin.

The full eight-case native/WASI acceptance basis uses runtime `fd6f4839`,
released weights and the hardware/BAM profile, with
zero assistance and preserved policy history during recovery seeds:

This matrix covers ordinary W/S/turns, forward boost and three seeded recoveries;
it does not establish stable boosted reverse or turning.
The final `4f7a9110` walk/Shift+W checks pass 1/1 on each target and preserve
the basis whole-world hashes `af41801b0af94572` / `dc74c754ef1b6356`.

| Probe | Measured result |
| --- | --- |
| W / S, 7 s command | 0.786932 / 0.863201 m signed travel in the requested direction |
| W / S loaded sole cycles, left/right | 15/14 and 11/11, requiring >5 mm clearance and touchdown dwell |
| Shift+W, 7 s command | 1.155245 m, 0.368313 m (46.8%) above W; 14/14 loaded cycles; minimum up 0.994069 and height 0.112773 m |
| Prone recovery | 1 s upright dwell confirmed at 1.340 s; final 2 s upright 100% |
| Side recovery | 1 s upright dwell confirmed at 2.115 s; final 2 s upright 100% |
| Supine recovery | 1 s upright dwell confirmed at 1.905 s; final 2 s upright 100% |
| Full A / D turning | -3.316523 / +0.196558 rad unwrapped yaw; D/right fails the 0.25 rad minimum |

W/S remain upright and slow after the one-second release, with mean final
absolute body-forward speeds 0.002281/0.002169 m/s. Shift+W's final horizontal
speed after release is 0.004675 m/s. Both hosts pass **7/8**, with all nine
emitted final whole-world hashes matching, including the repeated walk baseline
and D/right failure. Learned turning now uses `turn = -drive`; manual drive
is unchanged. The 0.5 reverse trial fell and boosted turns may stumble. Shift
exposes all-direction velocity/yaw requests through the same physics, without
fast-forward or artificial assistance. These trials do not establish exact speed
tracking, reliable directional turning or arbitrary-fall recovery. A separate
fully fitted 150-sphere alternative failed walking and is not the selected
model. The earlier no-BAM/61-sphere recovery failures describe the old geometry;
they do not isolate a general cause. Backlash, delays, self-contact filtering,
contact geometry and solver differences still prevent claims of exact training
identity or sim-to-real transfer.

CTest registers seven supported locomotion cases normally and explicitly disables
right-turn acceptance with label `known-limitation`. The strict 0.25 rad assertion
remains directly runnable in the target/full host, which measures 7/8. The
unrelated missing `bench/` directory blocks CMake configuration, so registration
inspection and installed-host checks do not constitute a clean full-suite pass.

The final source builds with the installed CLI on native/web. Browser
W/Shift+W/A/R and F8/F7 checks pass without errors, with screenshots inspected.
A final 12-second steady WebGL observation shows 32.08 ms/frame, about 31 FPS,
2.35 ms/tick and 1.77 ms Newton work, with 401 ticks/sec and 12.9 ticks/frame
under combined client/server load. Earlier fd6f and preoptimization snapshots
show 45.58/2.74/2.35 and 86.09/5.23/4.83 ms for frame/tick/Newton respectively.
These browser snapshots are not a paired benchmark; they do not establish
60 FPS or fully resolved browser lag.
Complete rendered-surface picking coverage remains unvalidated.
The earlier assisted-step, unassisted-topple and browser-interaction measurements
in the example's README are historical evidence for the preceding regularized
manual sandbox, rather than current learned-controller acceptance.

## A sharded world

In a [sharded world](../multiplayer/shards.md#physics), each shard steps the bodies it owns, with the ones its neighbours own near the border in the step as they are, a dynamic one as heavy as it is, so a ball runs into one across the border as it would on one shard. What the step does to a neighbour's body is that body's own shard's to work out. `Body.position` decides which shard has a body, a joint keeps its two bodies on one shard, so an articulation crosses whole, and a collider goes with its body. A plane goes on for ever, so its body is copied to every shard. `Physics`' settings are the world's: set them as in any game, and every shard has them; its `status` is each shard's own step's.

## Current scope and validation

Final runtime SHA-256 prefix `4f7a9110` passes **63 runtime tests on native
and WASI**, including three new stability regressions that failed with the old
adapter. Legacy stock-Newton reference
agreement does not establish upstream equivalence for the new extension. The
VelStand actor preserves 73 independent ONNX Runtime fixtures and a 512-step
native/WASI feedback stream. The final all-direction boost game host passes
**13 common tests on both targets and 14 on threaded native**, including
1,215 actual worker callbacks,
covering the interface, raw/applied motor history, filter state, reset, failure,
WASD and replay, including sampled full/delta Shift held/release, reverse,
boosted yaw and diagonal inputs with actor history. All four cadence-residue
replay hashes match. Fresh-process
learned-policy and active-grab joins in both native/WASI directions produce
identical 128,254-byte files (SHA-256
`570cc830abd1ca6f0170f31a2d149060cc0027d321791c635e553688a31347fe`).
These focused results do not imply a full repository-suite pass, complete
training plant or validated hardware simulation. Physical failures remain above.

The stability-basis native's 23 manual behavior cases pass; both target pin reruns pass 1/1 after
observed agreement on the 280-tick hash `90ed0aaf9bb442c8`. The full WASI 24-case
suite has not been repeated on the final source. Current controls/run/policy
results are in `build/microduck-run-check/all-boost-jtp4-test-summary.json`;
the full eight-case basis is `final-test-summary.json` in that directory.

The runtime centers each transient virtual free-joint parent frame at the
current body origin, with zero initial translation coordinates. This avoids
float cancellation in Cholesky factorization for small rotational inertia
far from the world origin. Public COM/world conventions and API are preserved,
with no new persistent state. A full-scene close-duck/ball/grab replay that
failed at tick 1734, substep 3 completes 4,000 ticks on both targets on the
fd6f stability basis, hash `4cb7f65b76088097`. Ordinary W with both ducks and
balls active also reaches 4,000 basis ticks, hash `658515fe75a71d1d`. Reverse
boost falling or boosted turns stumbling is a learned-controller capability
limit, distinct from this corrected Newton numerical `Unstable` failure.

A six-column block mass-times-Jacobian optimization skips structural zeros in
the same ordered nonzero arithmetic. A paired native benchmark on a frozen
previous-control idle scene improves from 3.38 to 1.72 ms/tick, with three
output arrays byte-identical over 1,020 ticks. Separate basis loaded headless
W runs measure 1.594 ms/tick native and 3.185 ms/tick WASI. Those host timings
are not browser FPS or a paired cross-target speedup. Centering intentionally
changes two old pins to the observed native/WASI state `be6cce7a3d5e1828` and
contact `445004fca1d80fa4`. Backend final logs are under
`build/microduck-interaction-fix/`.

The final runtime also computes JᵀP with four ordered scalar accumulators and
the unchanged tail, retaining every term and finite check without FMA or
reassociation. A separate paired native comparison against the frozen fd6f
reference measures `nt_step` calls at 1.459854 to 1.088080 ms (25.47%), with
status and all three output record arrays byte-identical over 1,020 calls.
One captured 35-body/28-joint/353-collider snapshot matches native/WASI SIMD
bytes, hash `d267a59f35210b5f`. This bounded result is not browser performance
or a precise cumulative gain with the older block benchmark. Details are in
`build/microduck-perf-audit/jtp4/RESULTS.md`.

This first integration covers free/static roots, fixed/revolute/prismatic joints,
body forces and torques, effort controls, solver body/joint state and compliant
sphere/plane contacts. It does not yet provide:

- Other collider shapes, mesh collisions, collision queries or continuous collision detection. Optional Coulomb impulses support bounded sticking; finite sweeps do not guarantee exact global sticking.
- General URDF/MJCF import, automatic robot construction or shape-derived mass properties.
- CUDA execution, a Python runtime, differentiable-training APIs or a policy-training framework.
- Velocity-limit constraints, simulated sensors, validated realistic robot dynamics or sim-to-real guarantees.

The upstream CPU kernels are pinned to Newton `ea4899864046dcd5981f8ba0bc3d8c25e65dcb96` and Warp `b346b941a4c0cd19c510db67d6506d781464ed5b` (`1.19.0.dev20261020`). Source regeneration uses `packages/newton/tools/export.py` to produce `kernels/featherstone.inl` and `kernels/contacts.inl`; Python is required for that maintainer operation only. The selected solver path uses Tide's deterministic math with explicit FMA removed and contraction/fast math disabled. The package's `UPSTREAM.md` also records its WebAssembly native-half guard, private launch wrappers, LP64-compatible integer types and value-initialized generated temporaries.

The original regularized-contact profile passed 33 runtime tests on native/WASI
in debug and release, including nine
independent stock Newton CPU contact state/force comparisons, the original
two-link reference, analytic dynamics, validation atomicity, topology and replay.
Its historical contact-history hash `ddf1584fd2502ff1` agreed across all four
configurations; the noncontact pin then was `92249536c6268981`.

The following full-suite evidence is historical: it predates the learned controller,
joint friction and Coulomb addition and describes the preceding manual sandbox
revision. Game replay checks passed, and the prior MicroDuck hash
`f87f0902ec4db694` and arm hash `db6cb5835d7c9b26` agreed across native/WASI
debug and release. Installed CLI native/web release builds passed; all 40
upstream hashes and 46 staged source/header files matched the checkout.
The example's README and `UPSTREAM.md`
record detailed validation results.

That revision's full native debug workflow passed 2,554/2,554 tests, and web debug passed
2,416/2,416, including WebGPU drawing. These runs include the 24 MicroDuck
fixture cases, 33 runtime checks, six arm cases, system-order checks and both
fresh-process joins. All 24 MicroDuck cases also passed on native/WASI release.
Active-grab joins produced four byte-identical files across the configurations;
both rollback clients completed thousands of replay ticks with zero resyncs.
Other-platform coverage, broader performance measurements and independent
physical-fidelity validation remain outstanding. These checks support
articulated-control experiments with the same numerical build in games and
training; they do not establish realistic robot dynamics or stock Newton CUDA
equivalence.
