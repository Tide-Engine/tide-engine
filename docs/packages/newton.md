---
description: "Experiment with NVIDIA Newton articulated bodies, sphere and plane collisions, joints and effort control in the same Tide match used for games and robotics policies."
---

# Newton — articulated physics

`newton` is an optional, experimental package for **NVIDIA Newton** articulated physics, separate from [Box3D](./box3d.md). It exposes a small part of Newton's Featherstone solver for bodies, joints and effort control. This is not Newton Game Dynamics, and the package never substitutes Box3D for Newton.

::: warning Experimental scope
The package runs upstream Featherstone CPU kernels and a small collision profile for spheres and static planes. The Tide API names below remain provisional and physical fidelity is unvalidated. Compound spheres approximate robot casings; these are not mesh collisions or a validated model of the real robot's ground contact.
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
| `contactForce` | Mean world-space contact force on this collider over the last successful tick, including friction |

A plane's local +Y normal points out of its solid half-space. Planes require a
static body; their transform may translate or rotate the floor. Sphere pairs
and sphere/plane contacts are supported. Same-body pairs and static/static
pairs are ignored. Give a robot's colliders one nonzero exclusion group to
disable internal collisions while preserving collisions with other robots,
props and the floor.

Material coefficients must be finite and nonnegative. Contact response uses
Newton's compliant rigid-contact forces, including its regularized friction;
this is not a hard nonpenetration constraint or exact static sticking. The
shape materials are averaged for a pair. Contact geometry and forces refresh
at each physics substep. Stiffer contacts and lighter bodies may require more
substeps. There is no continuous collision detection: sufficiently fast objects
can pass through each other between steps.

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
| `limited`, `lower`, `upper` | Enable a position limit and its bounds, in the coordinate's units; disabled by default |
| `limitStiffness`, `limitDamping` | Nonnegative position-limit response settings; defaults 10000 and 100 |

Frame rotations default to identity. Position limits use a spring/damper response, not a hard positional stop. Damping, armature and limit settings must be finite, and enabled bounds must satisfy `lower <= upper`. There is no joint-friction model or velocity-limit constraint.

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

`examples/robotics/microduck/` is a two-player **assisted physical stepping**
prototype using the published 15-body, 14-hinge MicroDuck model. Its trunk is
a free dynamic root. Package colliders provide contacts at a 200 Hz tick rate
with four physics substeps. Each duck has 61 original compound sphere proxies
across all 15 bodies, including six sole colliders per foot. A static infinite
plane supports both ducks and four shared balls; the drawn floor is 4 × 3 m.
Each duck's exclusion group suppresses internal collisions while retaining
contacts with the floor, the other duck and balls.
Run it with `tide run examples/robotics/microduck --release`; the unoptimized
CLI solver build is too slow for this full articulation.

**Controlled forward/backward walking and self-balancing are not demonstrated.**
W/S selects opposite joint patterns rather than reliable walking directions.
Upright, world +X heading and lateral Z lane assistance starts enabled and is
labelled in the HUD. It applies torques and a lane force, with exactly zero
forward X or vertical Y assistance force.

| Control | Action |
| --- | --- |
| Hold left mouse button and drag | Grab a marked part of either duck or a shared ball |
| Hold W/S, Up/Down or left stick Y | Opposite joint step patterns after one second of settling; not velocity tracking |
| Q/E, Left/Right or D-pad Left/Right | Select a joint |
| A/D or left stick X | Trim its target |
| B or gamepad North | Toggle balance, heading and lane assistance |
| R or gamepad South | Reset your duck, its controller/ring and all balls; assistance returns ON |

Reset places all bodies coherently at STAND with a 0.12 m trunk height. Joint
selection persists; controls still run on the reset tick. The other duck is
not reset. Body revisions change on reset, invalidating either player's old
grab anchors on those bodies. The illustrative PD gains are 4/0.03 with a
0.96 Nm motor-control clamp, retaining authored damping and armature. No BAM
servo model or learned policy is included.

All fixture colliders use `stiffness = 5600`, `damping = 3`,
`frictionDamping = 1`, `friction = 0.8`. The package runs upstream Newton's
compliant contact response with regularized friction, without a static-friction
constraint. The original sphere estimates follow source visual frames and
approximate casings and soles; they are not STL-derived or calibrated geometry.

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
not exact CAD. No upstream STL geometry or photo textures are used. The
example's README and `UPSTREAM.md` detail controls, provenance and the source pin.

Both signed patterns pass the unchanged >5 mm clearance threshold and dwell
checks using each foot's sole sphere envelope, with five lift/touchdown cycles
per foot and peak clearances of 11.207–19.505 mm. An unassisted topple from 0.35 m reaches
15.692 mm transient penetration (within the 20 mm budget), settles to 0.581 mm
penetration and ends with a 0.000097 m/s root speed. Controlled travel and
physical calibration remain unvalidated.

A bounded native capture and muted WebGL/WebGPU checks pass with the final
contact profile. Both browser backends confirm ball grab/lift/release, head
grab/lift/drop with assistance off, full-body floor rest, and reset of the
owner's duck and balls with resumed recording. Final images were inspected.
There are no page exceptions or GL/GPU validation errors; only the known
autoplay warning remains.

## Current scope and validation

This first integration covers free/static roots, fixed/revolute/prismatic joints,
body forces and torques, effort controls, solver body/joint state and compliant
sphere/plane contacts. It does not yet provide:

- Other collider shapes, mesh collisions, collision queries, continuous collision detection or a static-friction constraint.
- General URDF/MJCF import, automatic robot construction or shape-derived mass properties.
- CUDA execution, a Python runtime, differentiable-training APIs or a policy-training framework.
- Joint friction or velocity constraints, simulated sensors, validated realistic robot dynamics or sim-to-real guarantees.

The upstream CPU kernels are pinned to Newton `ea4899864046dcd5981f8ba0bc3d8c25e65dcb96` and Warp `b346b941a4c0cd19c510db67d6506d781464ed5b` (`1.19.0.dev20261020`). Source regeneration uses `packages/newton/tools/export.py` to produce `kernels/featherstone.inl` and `kernels/contacts.inl`; Python is required for that maintainer operation only. The selected solver path uses Tide's deterministic math with explicit FMA removed and contraction/fast math disabled. The package's `UPSTREAM.md` also records its WebAssembly native-half guard, private launch wrappers, LP64-compatible integer types and value-initialized generated temporaries.

All 33 runtime tests pass on native/WASI in debug and release, including nine
independent stock Newton CPU contact state/force comparisons, the original
two-link reference, analytic dynamics, validation atomicity, topology and replay.
The contact-history hash `ddf1584fd2502ff1` agrees across all four configurations;
the noncontact pin `92249536c6268981` is unchanged.

Game replay checks pass, and the MicroDuck hash `f87f0902ec4db694` and arm hash
`db6cb5835d7c9b26` agree across native/WASI debug and release. Installed CLI
native/web release builds pass; all 40 upstream hashes and 46 staged
source/header files match the checkout. The example's README and `UPSTREAM.md`
record detailed validation results.

The full native debug workflow passes 2,554/2,554 tests, and web debug passes
2,416/2,416, including WebGPU drawing. These runs include the 24 MicroDuck
fixture cases, 33 runtime checks, six arm cases, system-order checks and both
fresh-process joins. All 24 MicroDuck cases also pass on native/WASI release.
Active-grab joins produce four byte-identical files across the configurations;
both rollback clients complete thousands of replay ticks with zero resyncs.
Other-platform coverage, broader performance measurements and independent
physical-fidelity validation remain outstanding. These checks support
articulated-control experiments with the same numerical build in games and
training; they do not establish realistic robot dynamics or stock Newton CUDA
equivalence.
