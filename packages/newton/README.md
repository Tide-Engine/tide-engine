# Newton

Experimental **NVIDIA Newton** articulated physics for Tide, in the `Newton` namespace. It is a separate optional package alongside Box3D, with no silent Box3D fallback. It is unrelated to Newton Game Dynamics.

The package runs upstream Featherstone CPU kernels on native and WebAssembly. Its first collision profile supports spheres, compound spheres and static planes, using Newton's compliant rigid-contact response. API names remain provisional and physical fidelity is unvalidated. Box/mesh collisions, exact static sticking and continuous collision detection are not implemented.

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
`frictionDamping = 100`, `friction = 0.8`; finite nonnegative coefficients are
averaged between the two colliders. Contact geometry refreshes every substep.
These are compliant contacts with regularized friction, not hard positional
constraints or a validated real-world material model. Fast motion can tunnel;
there is no continuous collision detection.

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
substeps. Each duck has 61 original sphere proxies covering all 15 bodies,
including six sole colliders per foot. They collide with an infinite static
plane, the other duck and four shared balls; each duck excludes its own internal
contacts. The drawn floor is 4 × 3 m. These are approximate compound shapes,
not mesh collisions.

Hold the left mouse button to grab and drag any marked part of either duck or
a ball. W/S or stick Y chooses opposite joint step patterns, Q/E selects a
joint, and A/D or stick X trims its target. B toggles labelled balance, heading
and lane assistance, initially ON, with zero forward X or vertical Y assistance
force. R resets your duck, its controller/recording and all balls, without
resetting the other duck; body revisions cancel old grabs on reset bodies.
This remains **assisted physical stepping**, without reliable forward/backward
walking, self-balancing or BAM servo dynamics.
Use `tide run examples/robotics/microduck --release` for play; the unoptimized
CLI solver build is too slow for this full articulation.

Each duck's 64-transition ring records 44-value pre/post observations (joint
q/qd, trunk height/velocities, projected gravity, sole loads, step command/phase
and assistance flag), desired angles, clamped efforts, actual assistance wrench,
and that controller's grab request, accepted body and actual force/torque. Sole
loads sum the six colliders' mean vertical world contact forces per foot,
including friction and contacts with any object. Other-player interactions can
also affect a duck, so its ring is not a complete world dataset. The same
verified-tick export and human-view timing limits as the arm apply. Full authored
inertia tensors, XML pins and offline conversion are unchanged. Original
procedural casings and sphere proxies use approximate dimensions in source
visual frames, with no upstream STL geometry or photo textures.

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
import, CUDA execution, joint friction/velocity constraints, simulated sensors
and trainers remain unimplemented. Position limits and contacts are compliant;
contact friction has no static-sticking constraint. Physical fidelity and
sim-to-real transfer are not established.

All 33 runtime tests pass on native/WASI in debug and release, including nine
independent stock Newton contact state/force comparisons, analytic dynamics and
the original two-link reference. The contact-history hash is
`ddf1584fd2502ff1`; the noncontact pin `92249536c6268981` is unchanged.
Game replay checks pass, and the MicroDuck hash `f87f0902ec4db694` and arm hash
`db6cb5835d7c9b26` agree across all four configurations.

Installed CLI native/web release builds and a bounded native capture pass.
All 40 upstream hashes and 46 staged source/header files match the checkout.
Muted WebGL and WebGPU checks confirm ball grab/lift/release, head grab/lift/drop
with assistance off, full-body floor rest, and reset of the owner's duck and
balls with resumed recording. No page exceptions or GL/GPU validation errors
occur; only the known autoplay warning remains. Final images were inspected.

The full native debug workflow passes 2,554/2,554 tests, and web debug passes
2,416/2,416, including WebGPU drawing. All 24 MicroDuck fixture cases pass
across native/WASI debug and release. Active-grab fresh-process joins and
two-client rollback also pass; the four join files are byte-identical and
both clients complete thousands of replay ticks with zero resyncs.
Other-platform coverage, broader measurements and independent physical-fidelity
validation remain outstanding. Detailed evidence is recorded in
[`notes/newton.md`](../../notes/newton.md) and the example's README/`UPSTREAM.md`.
