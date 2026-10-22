# Newton

Experimental **NVIDIA Newton** articulated physics for Tide, in the `Newton` namespace. It is a separate optional package alongside Box3D, with no silent Box3D fallback. It is unrelated to Newton Game Dynamics.

The package runs upstream Featherstone CPU kernels on native and WebAssembly. Focused tests cover exact cross-target/configuration results, snapshots, packing and two-client rollback. API names remain provisional and physical fidelity is unvalidated. There are **no colliders or contacts**: bodies do not collide with one another or with a drawn floor.

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

## One simulation for games and policies

A game, a human controller and a robotics policy use the same components and physics step. Reset episodes through Tide's ordinary world snapshot/restore path, including controller state in the match.

The runnable [two-player arm example](../../examples/robotics/arm/) uses Rooms and each arm owner's keyboard/gamepad input for target reaching. Each `Arm` retains at most 128 transitions: simulation tick, pre-step joint observation, desired joint angles, actual clamped effort, next observation and reward. These describe executed match state before/after the step; they are not an exported dataset or a record of the exact view a human saw when choosing input. Export verified ticks in tick order to account for rollback; exact human-view timing needs additional capture.

All persistent state is ordinary Tide components/lists/maps, with no `Resident` arena. Solver model arrays and scratch are reconstructed per call, so separate worlds retain no shared solver state.

## Upstream CPU kernels

- Newton: `ea4899864046dcd5981f8ba0bc3d8c25e65dcb96`.
- Warp: `b346b941a4c0cd19c510db67d6506d781464ed5b`, version `1.19.0.dev20261020`.
- [`tools/export.py`](tools/export.py) generates the checked-in `kernels/featherstone.inl` and vendors its headers. Regeneration needs the pinned source checkouts and Python/Warp environment; game builds do not run Python.
- Native and WebAssembly compile the same scalar CPU source without a C++ runtime, Warp JIT or CUDA dependency. The selected solver path uses Tide's deterministic math with FMA/fast math disabled, a WebAssembly-safe native-half guard, private launch wrappers, LP64-compatible integer types and value-initialized generated temporaries. [`UPSTREAM.md`](UPSTREAM.md) records the patches.
- Upstream and third-party notices are beside the vendored source in [`vendor/`](vendor/); generated-source/header hashes are in [`UPSTREAM.sha256`](UPSTREAM.sha256).

## Scope and validation

The first integration covers body mass/diagonal inertia/centre of mass/world force and torque, free/static roots, fixed/revolute/prismatic joints, effort actuation and actual joint/body state. It has no colliders, contacts or queries; no URDF/MJCF importer, CUDA runtime, joint friction or velocity constraints; and no automatic realistic robot, sensors or trainer. Position limits use spring/damper responses. Physical fidelity and sim-to-real transfer are not established.

The 22 runtime tests pass natively and under WASI, including a two-link stock Newton Python reference comparison within tolerance and analytic dynamics. Six Tide arm fixture tests pass in native/web debug and release, covering replay, snapshots, raw/delta packing, stale entities, thread-count equality and two-client rollback under latency, jitter and loss. Exact hashes agree across those four builds; fresh-process canonical joins also pass from native to WASI and back.

The full native and web debug suites also pass. Installed-source native/web builds and a bounded desktop arm run pass; games omitting Newton compile none of it. Other-platform CI, performance measurements and independent physical-fidelity validation remain outstanding. Detailed evidence and documentation build results are in [`notes/newton.md`](../../notes/newton.md).
