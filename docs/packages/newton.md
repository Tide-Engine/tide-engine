---
description: "Experiment with NVIDIA Newton articulated bodies, joints and effort control in the same Tide match used for games and robotics policies."
---

# Newton — articulated physics

`newton` is an optional, experimental package for **NVIDIA Newton** articulated physics, separate from [Box3D](./box3d.md). It exposes a small part of Newton's Featherstone solver for bodies, joints and effort control. This is not Newton Game Dynamics, and the package never substitutes Box3D for Newton.

::: warning Experimental scope
The package runs upstream Featherstone CPU kernels, with focused native/WebAssembly debug/release parity and rollback tests. The Tide API names below remain provisional and physical fidelity is unvalidated. There are no colliders or contacts; a body falls through other bodies and anything you draw as a floor.
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

On failure, no new body poses, joint coordinates or `appliedEffort` values publish. Retained values are not fresh observations; check status after every step. Correct the state or reset the episode before treating it as usable data. An `Ok` status is a numerical step result, not a physical-accuracy certificate.

## Games, policies and demonstrations

Observe actual `Joint.position`/`velocity` and `Body.position`/`rotation`/`velocity`/`angularVelocity` after a successful step. Choose observation normalization, rewards and action mapping in ordinary game code. There are no packaged cameras, IMUs, encoders, contact sensors or measurement-noise models.

The runnable `examples/robotics/arm/` example is a two-player target-reaching task using [Rooms](./rooms.md). Each arm uses its owner's keyboard/gamepad input to choose desired shoulder/elbow angles; an ordinary PD controller maps those angles to bounded effort before the same physics step a policy would use. Each `Arm` stores a ring of at most 128 transitions containing:

- The simulation tick and pre-step joint positions/velocities.
- The action as desired joint angles and the actual clamped actuator efforts.
- The successful post-step joint positions/velocities and target-reaching reward.

These records describe executed match state around a step. The example does not export a dataset or record the exact view and timing a human used to choose input. For that kind of human demonstration, capture the observation **used to choose the action** and associate it with the action's match-time application. Route local actions through normal input or commands, and export verified ticks in tick order so prediction and rollback do not leave duplicate or superseded samples. A transform animation or pose-only replay does not capture this control history.

Use Tide's standard world snapshot/restore path for an episode reset. Bodies, joints, actuators and any controller/reward state you put in the match then reset together. There is no separate Newton simulation to reset or training-only solver to switch to. The package keeps persistent state in ordinary Tide components, lists and maps, with no `Resident` arena; model arrays and native solver scratch are rebuilt on each call. Replay, alternate worlds, snapshots, packing and two-client rollback have focused test coverage.

## Anchored MicroDuck

`examples/robotics/microduck/` is a second two-player fixture using the published
MicroDuck walking model: 15 bodies and 14 driven hinges. Its trunk is fixed to
the world. Q/E or the arrow keys select a joint, A/D or the gamepad stick moves
its target, and R resets the duck. Controls, observations and its bounded
64-transition recording ring are ordinary match state and use the same Newton
step during play and replay.

The example converts the pinned MJCF descriptions offline, preserving masses,
centres of mass and complete inertia tensors through principal-axis body frames.
Game builds need no Python or MuJoCo. Its original primitive visuals do not use
Pollen's noncommercial STL meshes; the robot descriptions and generated data
retain their Apache-2.0 provenance. See the example's README and `UPSTREAM.md`
for controls, regeneration and the source pin.

It steps at 200 Hz with one physics substep per tick. The small PD controller is
illustrative: it does not implement BAM servo dynamics, run upstream walking
policies, balance, or make ground contacts. The recorded observation is 14 joint
angles followed by 14 velocities; actions are desired angles and efforts are
actual clamped controls. The same verified-tick and human-view timing limits
described above apply.

## Current scope and validation

This first integration covers free/static roots, fixed/revolute/prismatic joints, body forces and torques, effort controls and solver body/joint state. It does not yet provide:

- Collider shapes, collision detection, contacts, contact friction or collision queries.
- General URDF/MJCF import, automatic robot construction or shape-derived mass properties.
- CUDA execution, a Python runtime, differentiable-training APIs or a policy-training framework.
- Joint friction or velocity constraints, simulated sensors, validated realistic robot dynamics or sim-to-real guarantees.

The upstream Featherstone CPU kernels are pinned to Newton `ea4899864046dcd5981f8ba0bc3d8c25e65dcb96` and Warp `b346b941a4c0cd19c510db67d6506d781464ed5b` (`1.19.0.dev20261020`). Source regeneration uses `packages/newton/tools/export.py` to produce `kernels/featherstone.inl`; Python is required for that maintainer operation only. The selected solver path uses Tide's deterministic math with explicit FMA removed and contraction/fast math disabled. The package's `UPSTREAM.md` also records its WebAssembly native-half guard, private launch wrappers, LP64-compatible integer types and value-initialized generated temporaries.

The 22 runtime tests pass natively and under WASI, including one two-link stock Newton Python reference comparison at 1, 8 and 32 ticks within `5e-5` absolute tolerance, analytic dynamics, validation atomicity, topology and replay. Six Tide arm fixture tests pass on native/web debug and release: replay/alternate worlds, snapshots, raw/delta packing, stale entities, thread-count equality and actual two-client rollback with latency, jitter and loss. The fixture's 200-tick hash agrees across all four builds. Fresh-process native-to-WASI and WASI-to-native canonical joins also pass.

The full native and web debug suites also pass. Installed-source native/web builds and a bounded desktop arm run pass; games omitting Newton compile none of it. Other-platform CI, performance measurements and independent physical-fidelity validation remain outstanding. These checks support articulated-control experiments with the same numerical build in games and training; they do not establish realistic robot dynamics or stock Newton CUDA equivalence.
