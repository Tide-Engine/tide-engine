# Box3D

[Box3D](https://github.com/erincatto/box3d), Erin Catto's 3D physics engine, for a Tide match: bodies and their shapes are components, every tick steps them, and the match stays the same on every machine, through rollbacks, joins and reloads.

An experiment, as the Box2D package beside it is, and built the same way: what's here works and is tested, and what it costs is below. Its names and its shape are not settled, and Box3D itself is at 0.1.

The docs have a page for it, with an example to play: https://tide-engine.dev/packages/box3d

## Using it

A game lists it in its `tide.packages`:

```
box3d
```

```csharp
using Box3D;

scene Main { }

component Crate { }

component Hopper
{
    int ticks;
}

singleton Tally
{
    int touched;
}

event(Spawned) Build(with Main)
{
    Spawn(Body { type = BodyType.Static, position = float3(0, -0.5, 0) }, Box { size = float3(40, 1, 40) });
    Spawn(Body { position = float3(0, 4, 0) }, Box, Crate);
    Spawn(Body { position = float3(0.3, 6, 0), velocity = float3(0, -2, 0) }, Sphere { radius = 0.4, bounce = 0.5 }, Crate, Hopper);
    Spawn(Body { position = float3(2, 3, 0), rotation = quaternion.Euler(float3(0.4, 0, 0)) }, Capsule { radius = 0.3, height = 1.5 }, Crate);
}

// A body moves by what its component says
system Hop(mut Body body, mut Hopper hopper)
{
    hopper.ticks += 1;
    if (hopper.ticks % 40 == 0) body.impulse = float3(0, 1500, 0);
}

event(Touched touched) Meet(mut Tally tally, with Crate)
{
    if (touched.other != this) tally.touched += 1;
}
```

(`test/stack.tide` is a game like this one, which the package's tests run, and `test/course.tide` one with each of the things below, a few bodies at a time.)

- **`Body`** is a rigid body: `type` (`Static`, `Kinematic`, `Dynamic`), `position`, `rotation`, `velocity`, `angularVelocity`, and `gravityScale`, `linearDamping`, `angularDamping` and `bullet`. After each step the place and the speeds are where Box3D left the body. A system that writes them moves the body: the next step starts from what they say. `impulse` and `angularImpulse` are given to the body at the next step, once: the ones a body is spawned with at its first, once its shapes are on it. A rotation that isn't of length 1 is made so.
- **How a body is to be can be written at any time**: its `type`, any to any, and its `gravityScale`, `linearDamping`, `angularDamping` and `bullet`. The next step has it that way. A body made `Static` stops where it is; one made `Kinematic` goes by the `velocity` written with it. A type that changes leaves what the body touches as it was: nothing is sent for the shapes that still touch, and `Parted` for the ones that don't any more (a body that never moves touches no other that never does).
- A body at rest is asleep, and whatever code writes into it wakes it: a speed, an impulse, a `position` or a `rotation`, which also wake what was touching it, and how it's to be.
- **`asleep`** says which it is, after each step, and a system that writes it puts the body to sleep or wakes it at the next one: `body.asleep = true` on a body that's moving stops it where it is, with none of its speed, until something touches it or code wakes it. A body spawned with it starts asleep. Box3D sleeps and wakes an island at a time, the bodies that touch each other (through bodies that move, not through static ones), so one that's put to sleep takes what it touches with it, and one that's woken wakes what it fell asleep with. What's put to sleep in a tick is asleep as that tick's step starts, whatever else was written that tick: a push, a move, a new gravity. A static body is never asleep, and a body can be put to sleep where `Physics.sleeping` is off. (`test/sleep.tide` is a game that does all of it.)
- `position` is a `[Position]` and `rotation` a `[Rotation]`, for views: they see a body get to where the server says it is over a few frames, turning the short way, when this machine predicted otherwise.
- **`Box`**, **`Sphere`** and **`Capsule`** are shapes on the entity's body: `size`, or `radius`, or `radius` and `height` (a capsule stands along its body's y, `height` from end to end), with `offset`, `density`, `friction`, `bounce` and `sensor`. Density is in kilograms a cubic meter, 1000 unless a shape says, so a crate a meter wide weighs a ton and an impulse of 1000 changes its speed by a meter a second.
- **`Part`** puts an entity's shapes on another entity's body: `Spawn(Box { size = float3(0.2, 1, 0.2), offset = float3(0.8, 0.4, 0.8) }, Part { of = table });`. An entity has one shape of each kind at most, so a body of several boxes is a body and a part for each box past its first. The shapes are on the body from the next step. Destroying the part takes them off again, and what rested on them wakes; destroying the body's entity destroys its parts. A part whose `of` isn't an entity with a `Body` when its shapes would be made is destroyed. A part can be a sensor.
- **`Touched`** and **`Parted`** are sent to both bodies' entities when they start and stop touching, each with the `other`, once for each pair of shapes, and with which shapes: `part` is the entity this one's shape is a component of, itself or one of its parts, and `otherPart` the same of the other's. A shape with `sensor` stops nothing, and sends them for the shapes that come into it and leave it, static bodies' too; two sensors say nothing of each other. When the other entity was destroyed while they touched, `Parted` comes with `default` for it; when only its part was, with the entity and `default` for the part.
- **`Hit`** is sent to both when two bodies meet faster than `Physics.hitSpeed`: `other`, the `point`, the `speed` they closed at there, and the `normal`, which is the way this entity was pushed (from the other towards it). Once a step for each pair of shapes that meets that fast.
- **`Physics`** is the world: `gravity` and `substeps`, and how it steps, each a field with Box3D's own value unless a game writes another: `contactStiffness` (30 cycles a second), `contactDamping` (10) and `contactSpeed` (3 meters a second: the fastest two bodies that overlap are pushed apart), `bounceSpeed` (1: slower than this nothing bounces), `hitSpeed` (1) and `sleeping` (true). What's written is the next step's. A gravity that changes wakes every body, and so does `sleeping` turned off.
- An entity that's destroyed takes its body with it.

### Static mesh colliders

`MeshCollider` accepts indexed triangle geometry on a `BodyType.Static` body, or on a `Part` of one:

```csharp
Spawn(Body { type = BodyType.Static }, MeshCollider {
    vertices = [float3(-10, 0, -10), float3(-10, 0, 10), float3(10, 0, 10), float3(10, 0, -10)],
    indices = [0, 1, 2, 0, 2, 3]
});
```

Indices are zero-based triples, counterclockwise from the front (upwards in this example). The collider preserves concave surfaces and gaps. `scale` defaults to `float3(1)` and may be nonuniform or mirrored, with no zero component; `offset` is added after scaling, in body space. The body's position and rotation then place it in the world. `friction` defaults to 0.6 and `bounce` to 0. Mesh colliders have no density or sensor setting.

Geometry and material edits apply at the next physics step, like the other shapes. A mesh on a dynamic or kinematic body makes no shape; the same applies to a mesh part on an unsuitable body. Once a body has a mesh, requests to change its type are reset to `Static`. Its position and rotation can still be written.

Each collider accepts 3–65,536 vertices and 1–65,536 triangles. All indices must be in range, and the index count must be divisible by three. Coordinates after scale and offset must be finite and within ±1,000,000 on each axis; friction must be finite and nonnegative, bounce finite in [0, 1]. Invalid geometry makes no shape. Degenerate triangles are discarded; an entirely degenerate mesh makes no shape. Unchanged rejected data is not retried; editing it retries attachment.

Current and rewound rays, primitive casts and overlaps include meshes. Rays and casts meet triangle fronts; a mesh is a surface with no solid interior. `Body.rewind` includes its meshes too. Prepared rewind scenes retain triangle values and index them in their spatial tree, so they survive rollback and native-world replacement. Preparation and retained history cost memory proportional to triangle count; ordinary physics ticks capture none when rewind history is disabled.

Cooked geometry belongs to the match's resident state and is deduplicated by its bytes. As with box hulls, unused geometry remains until the world is destroyed. Imported rendering meshes need their positions and indices supplied explicitly; there is no automatic render-mesh collider conversion.

## Additional body controls

Add `BodySettings` beside `Body` for controls that a simple body does not need:

```csharp
Spawn(Body { position = float3(0, 3, 0) }, Box,
      BodySettings { lockRotationX = true, lockRotationZ = true, sleeping = false });
```

- `force` and `torque` are world-space vectors, applied at the center of mass for one tick and then cleared. Their units are newtons and newton meters. Write them every tick for a continuous force.
- `enabled` defaults to true. False removes the body from simulation and live queries while retaining its components and attachments; true puts it back.
- `sleeping` defaults to true and permits automatic sleeping for this body. `sleepSpeed` defaults to 0.05. This differs from `Body.asleep`, which requests or reports its current sleeping state.
- `lockPositionX`, `lockPositionY`, `lockPositionZ`, `lockRotationX`, `lockRotationY` and `lockRotationZ` default to false and lock motion on the corresponding world axes.
- `safetyFactor` defaults to 0.5, with a supported range of 0.01–0.5. It controls when Box3D considers motion fast enough to need continuous collision checks. `fastRotation` defaults to false and permits rotations beyond the usual per-step limit; `contactRecycling` defaults to true and reuses cached contacts.

These fields are live. Removing `BodySettings` restores the defaults at the next physics step. Nonfinite force/torque vectors are ignored and cleared; invalid `sleepSpeed` (nonfinite or negative) or `safetyFactor` leaves the previous native setting in effect.

## Convex hulls and height fields

`HullCollider` cooks the convex hull of supplied points. It works on static, kinematic and dynamic bodies, and on their parts:

```csharp
Spawn(Body { position = float3(0, 3, 0) }, HullCollider {
    vertices = [float3(-1, 0, -1), float3(1, 0, -1), float3(0, 0, 1), float3(0, 2, 0)],
    density = 500
});
```

It has `vertices`, `scale = float3(1)`, `offset`, `density = 1000`, `friction = 0.6`, `bounce = 0` and `sensor = false`. Scale, then offset, apply in body space. Scale can be mirrored but has no zero component. Supply 4–65,536 points. Box3D keeps a hull to 128 vertices, 128 faces and 128 edges, and a smooth shape reaches the edge limit first, at about 45 vertices: the native cooker keeps the furthest points that fit, so a pin turned on a lathe of 121 points cooks to 45 vertices within 8% of its volume, and a ball of 458 to 86% of a sphere's. Shapes that need more detail than that are several parts. Concavities are filled, so use a static `MeshCollider` or several convex parts for a concave object. Coplanar or otherwise uncookable data makes no shape.

`HeightFieldCollider` is a static terrain grid:

```csharp
Spawn(Body { type = BodyType.Static }, HeightFieldCollider {
    columns = 3, rows = 3,
    heights = [0, 0, 0, 0, 1, 0, 0, 0, 0],
    minimumHeight = 0, maximumHeight = 1,
    scale = float3(2, 1, 2)
});
```

`heights` is row-major, indexed by `x + z * columns`. Both dimensions are 2–4,096, and their product is at most 65,536. `scale` defaults to `float3(1)` and must be finite and positive: x/z set grid spacing and y scales height. `minimumHeight = 0` and `maximumHeight = 1` bound the native 16-bit height compression; finite samples outside that interval are clamped. Optional `holes` is a `List<byte>` with one entry per cell, indexed by `x + z * (columns - 1)`; nonzero removes that cell's two triangles. Leave it empty for solid terrain. `friction` defaults to 0.6 and `bounce` to 0. There is no density, sensor or offset; place the grid with its body's pose. Height fields can also be parts of a static body.

Both colliders require finite coordinates, with the cooked geometry within ±1,000,000 per axis. Density and friction are finite and nonnegative, and bounce is finite in [0, 1]. Height fields have the same static-body restriction as meshes: an attached surface keeps its body's type static. Invalid data makes no shape and can be repaired by editing its component.

Current and rewound rays, primitive casts, overlaps and all-hit queries include both. Hulls have a solid interior; height fields are triangle surfaces, including their holes. Rewind contexts retain geometry values and survive later edits, component removal and world restoration. Hull history stores the cooked convex geometry; terrain history costs memory proportional to triangle count. Cooked hull, mesh and height-field data belongs to the resident world, is deduplicated, and remains until that world is destroyed even if no collider still uses it.

## Live collider changes and removal

Every collider's geometry, offset, scale, material and supported sensor setting follows component edits at the next physics step, including edits to individual list elements. Invalid edits take the old shape out; correcting the component attaches it again. Unchanged rejected data is not repeatedly cooked.

Primitive and hull geometry edits preserve the native shape handle. Mesh and height-field geometry edits replace the shape, and changing `sensor` replaces a primitive or hull shape. Material-only edits keep the handle. Replacements use Box3D's usual contact removal and creation events. Geometry changes update mass and wake affected bodies.

Removing a collider component removes that shape while keeping its entity and other shapes. Removing `Body` removes the native body, its joints and its parts while keeping the body's entity. Removing `Part` takes its shapes off the former owner; with no `Body` of its own, the surviving entity has no physics attachments. Writing `Part.of` moves its shapes to the new body; an invalid owner destroys the part. Structural changes follow Tide's deferred component rules; the physics step after the removal has taken effect sees the new membership. Queries see the last completed physics step.

The `id`, `step…`, `settingsApplied`, `cache` and attachment bookkeeping fields are the package's own; game code should not write them.

## Collision and query filters

`CollisionFilter` applies to all colliders on its entity. A part uses its own filter, independently of its owner's:

```csharp
Spawn(Body, Sphere, CollisionFilter { category = 2, mask = 1 });
```

`category` and `mask` are 64-bit `long` bit sets, defaulting to 1 and -1 (all bits). Two colliders pass the mask test when both `(a.category & b.mask) != 0` and `(b.category & a.mask) != 0`. Equal nonzero `group` values override masks: positive groups always pass and negative groups never pass. Other native rules still apply, such as static bodies not colliding with each other. Sensors use the package's reserved group -1, so they never report other sensors. Filter changes are live; removing the component restores the defaults.

All `Raycast`, `Cast`, `RaycastAll`, `CastAll` and `Overlap` overloads, on both `Physics` and rewound `Query`, accept an optional final `filter: QueryFilter`. It has `category = 1` and `mask = -1` and uses the same mutual mask test against each collider; collision groups do not override query masks:

```csharp
physics.Raycast(from, to, out var hit, ignore: body, filter: QueryFilter { mask = 2 });
physics.RaycastAll(from, to, out var hits, includeSensors: true, filter: QueryFilter { mask = 2 });
```

An all-hit list can still be filtered by components, teams or other game rules afterward. `Inside` reports the sensor's actual overlaps, which already obey collision filters. Rewind captures filter bits with shape membership at the upper endpoint, rather than interpolating them.

## Joints

A joint is an entity with `Joint` and exactly one settings component. Its endpoints are entities with `Body`:

```csharp
var fixedBody = Spawn(Body { type = BodyType.Static, position = float3(0, 4, 0) }, Box);
var weight = Spawn(Body { position = float3(0, 2, 0) }, Sphere);
Spawn(Joint { a = fixedBody, b = weight }, DistanceJoint { length = 2 });
```

`anchorA` and `anchorB` default to zero and are local to each body's origin. `rotationA` and `rotationB` default to `quaternion.identity` and orient those local frames. Nonzero finite quaternions are normalized. `constraintHertz = 60` and `constraintDampingRatio = 2` tune the constrained axes. `collideConnected` defaults to false. After each step `force` and `torque` report the native constraint force and torque in newtons and newton meters; `id` is internal.

| Settings component | Constraint and live settings |
|---|---|
| `DistanceJoint` | Distance between anchors. `length = 1`; `enableSpring`, `hertz`, `dampingRatio`, `lowerSpringForce = -1e12`, `upperSpringForce = 1e12`; `enableLimit`, `minLength = 0`, `maxLength = 1e6`; `enableMotor`, `maxMotorForce`, `motorSpeed`. |
| `MotorJoint` | Drives the relative frames. `linearVelocity`, `angularVelocity`, `maxVelocityForce`, `maxVelocityTorque`; `linearHertz`, `linearDampingRatio`, `maxSpringForce`; `angularHertz`, `angularDampingRatio`, `maxSpringTorque`. All default to zero. |
| `FilterJoint` | Prevents the connected bodies colliding, without a motion constraint. Its `enabled = true` can independently disable the filter joint. |
| `ParallelJoint` | Keeps the local frame z axes parallel. `hertz = 1`, `dampingRatio = 1`, `maxTorque = 1e12`. |
| `PrismaticJoint` | Slides along local x. `enableSpring`, `hertz`, `dampingRatio`, `targetTranslation`; `enableLimit`, `lowerTranslation`, `upperTranslation`; `enableMotor`, `maxMotorForce`, `motorSpeed`. |
| `RevoluteJoint` | Hinges around local z. `enableSpring`, `targetAngle`, `hertz`, `dampingRatio`; `enableLimit`, `lowerAngle`, `upperAngle`; `enableMotor`, `maxMotorTorque`, `motorSpeed`. |
| `SphericalJoint` | Holds anchors together. `enableSpring`, `hertz`, `dampingRatio`, `targetRotation = quaternion.identity`; `enableConeLimit`, `coneAngle`; `enableTwistLimit`, `lowerTwistAngle`, `upperTwistAngle`; `enableMotor`, `maxMotorTorque`, `motorVelocity`. Cone and twist use local z (frame A for the cone, frame B for twist). |
| `WeldJoint` | Holds both frames together. `linearHertz`, `angularHertz`, `linearDampingRatio`, `angularDampingRatio`, all zero by default. |
| `WheelJoint` | Suspension along local x, wheel spin around local z. `enableSuspensionSpring = true`, `suspensionHertz = 1`, `suspensionDampingRatio = 0.7`; `enableSuspensionLimit`, `lowerSuspensionLimit`, `upperSuspensionLimit`; `enableSpinMotor`, `maxSpinTorque`, `spinSpeed`; `enableSteering`, `steeringHertz = 1`, `steeringDampingRatio = 0.7`, `targetSteeringAngle`, `maxSteeringTorque`; `enableSteeringLimit`, `lowerSteeringLimit`, `upperSteeringLimit`. |

Unless shown otherwise, switches default to false and scalars to zero. Lengths are meters, angles radians, linear speeds meters per second, angular speeds radians per second, spring frequencies cycles per second, forces newtons and torques newton meters. Damping ratios are dimensionless.

Settings and local frames apply at the next physics step. Normal edits preserve the native joint and its warm-start state; changing the endpoint bodies or joint type replaces it. `Joint.enabled = false` removes the native joint while retaining its entity, and true recreates it. Destroying the joint entity or removing `Joint` removes its native constraint. Missing endpoints, an endpoint without `Body`, no settings component or several settings components destroy the joint entity. Removing its sole settings component has that same effect.

Invalid settings on an enabled joint also destroy the joint entity. Scalars and anchor/vector components must be finite and within ±1e12; frequencies, damping, lengths and maximum force/torque settings must be nonnegative, and lower limits must not exceed upper limits. `DistanceJoint.length` must be positive; native distance lengths and limits have a minimum of 0.005 meters. A spherical cone angle is in [0, π/2]. These checks run before entering the native engine. Joints, their settings and solver impulses follow snapshots, rollback, packing, joins and reloads with the bodies.


### Asking the world

Functions that ask what's in the world and change nothing. Each takes the `Physics` singleton and says whether it found anything, with what it found through `out`:

```csharp
// Stands on something: a ray a little way down from the feet
system Land(Physics physics, Body body, mut Walker walker)
{
    walker.grounded = physics.Raycast(body.position, body.position - float3(0, 1.1, 0), out var under);
    if (walker.grounded) walker.on = under.entity;
}
```

- **`physics.Raycast(from, to, out RayHit hit)`**: the first thing on the way from one point to another. `RayHit` has the `entity`, the `part` its shape is on (the entity itself, for a shape of its own), the `point`, the shape's `normal` there, and the `fraction` of the way, from 0 to 1. A ray says nothing of a shape it starts inside.
- **`physics.Cast(shape, from, rotation, to, out RayHit hit)`**: the first thing a `Box`, a `Sphere` or a `Capsule` meets that's moved from one place to another, turned by `rotation` all the way; the shape is as one on a body is, with its size and its `offset`. `hit.point` is where the two touch. What the shape is in at `from`, or within a few millimeters of, is met there: a fraction of 0, and no normal.
- **`physics.Overlap(shape, at, rotation, out List<Entity> found)`**: the entities whose bodies a shape at a place overlaps or touches, each once.
- **`physics.Inside(sensor, out List<Entity> found)`**: the entities whose bodies have a shape in an entity's sensors, as the last step found them. `sensor` is the entity the sensor shape is a component of: a body's, or a part's.

They can be asked from a system, a handler, a view, and the functions those call, wherever the system is in the tick.

- **They see the world as the last step left it.** A system after the step is asked of this tick's step, and one that's `[Before(PhysicsStages.Step)]` of the last tick's. What code wrote into a body since isn't there yet, nor a body or a shape that was spawned since: each is in the world from the next step. An entity that was destroyed since is found no more, at once.
- **Sensors are skipped by default**: the all-hit methods below can include them. `Overlap` doesn't list sensors; `Inside` asks what's in one.
- **One body can be left out**: `Raycast` and `Cast` accept `ignore: body` after the output, excluding every shape on that body, including its parts.
- **The answer is the same on every machine**, for the same match: of two things as near, the one whose shape was made first is the one that's met, whatever order Box3D has them in. `Overlap` and `Inside` lists come in the order of their bodies' ids (`Body.id`).
- A view is asked of the world it draws. When the arena holds another state (a view of a confirmed world, while this machine runs ahead of it), that world's resident pages are restored before it's asked. The next tick restores its own state: see What it costs.

### All hits and game filtering

```csharp
physics.RaycastAll(from, to, out var hits, ignore: body);
physics.CastAll(Sphere { radius = 0.2 }, from, quaternion.identity, to, out var swept,
    ignore: body, includeSensors: true);
```

`RaycastAll` and `CastAll` return `out List<RayHit>` in increasing `fraction` order, with deterministic shape ties matching the closest-hit methods. Each collider contributes its first hit; separate colliders on one body stay separate, even when `entity` and `part` match. A mesh collider contributes one hit for the whole mesh, not every triangle or entry-and-exit intersection. They return whether any hit was found, replacing the output with an empty list otherwise.

`CastAll` accepts the same `Box`, `Sphere` or `Capsule` and arguments as `Cast`. Both all-hit methods support `ignore: body` (every shape on that body, including parts) and `includeSensors: true` (false by default), independently or together. Iterate the list to select the first hit accepted by game rules, or process all of them. The results are ordinary values that can be saved in match or local state. Rewound `Query` has the same methods and options. Use `Raycast` or `Cast` for just the closest hit; they retain early pruning. Starting overlaps have fraction 0 and no normal or unique surface point, as with closest-hit casts.

### Step ordering

The package's systems run before the game's, in this order: bodies and shapes that are new are made and what code wrote goes to Box3D, the world steps, bodies' components get where it left them, and the events are sent. So a game's systems read this tick's positions, and what they write is the next step's.

They're one stage, `PhysicsStages.Step`. A system that has to write into this tick's step says `[Before(PhysicsStages.Step)]`, and runs before all of them:

```csharp
[Before(PhysicsStages.Step)]
system Walk(PlayerInput input, mut Body body, with Player)
{
    body.velocity.x = input.move.x * 4;
    body.velocity.z = input.move.y * 4;
}
```

That's the one way. The package's own systems (`Enter`, `Simulate`, `Leave` and the rest) each say where they are in the stage, so nothing a game orders its systems by moves them, and they aren't for a game to name: a system put among them writes after bodies have gone to Box3D and before they come back, and the step's own is written over it. `test/order.tide` is a game that orders its systems each way, with the order that comes of it in `test/order.txt`.

## How it holds a match

Box3D's persistent memory belongs to the match through `Physics.state`, a `Resident` field the package's C uses. The engine allocates it in an arena at a fixed address for the process, so Box3D's pointers stay valid when an older state is restored. After a step, the package commits the arena's touched 16 KiB pages into the world; its snapshots share those pages. A normal tick writes no serialized image or geometry list.

- **Everything that changes Box3D's world goes through the match**, which is why code moves a body by writing its component, and never calls Box3D. The stamp is the stamp before it and everything done to the world since, hashed. Each commit also supplies Box3D's deterministic digest: its counts, awake bodies, contact impulses and persistent joint impulses. The match's hash covers the stamp and digest, not the arena's raw bytes, which contain pointers and may have another allocation order on another machine.
- **A commit copies what Box3D may have written.** Box3D marks its allocations and changed bodies, shapes, contacts, trees and sleeping sets; the bridge marks its own persistent bookkeeping. Every touched page is copied, without first comparing or serializing it, and an untouched page stays shared with the previous state. This is a conservative set: a touched page need not have different bytes. A change the marks don't follow, or a world with most bodies awake, can copy every page.
- **A rollback restores pages, without making Box3D's world again.** Before stepping a state, the bridge compares the resident's page identities with the pages last committed or restored into the arena, and copies the ones that differ. It checks those identities even when the stamp matches: two runs of the same simulation state can have different pointers and allocation order. Allocator state, the world's persistent arrays and geometry are restored together.
- **Canonical bytes are for sending a world and carrying it across builds.** A join, resync, host migration or reload that packs the world writes Box3D's snapshot and the geometry it names. The receiving machine builds its arena from those bytes and commits it. A network delta currently carries the resident's canonical state whole when it differs from the base. Raw arena pages never cross machines. `box3d/TIDE.md` describes the changes that keep the simulation and canonical bytes the same on desktop and the web.
- **What a box is made of is kept once.** Box3D keeps a box as a hull (its corners, edges and faces), shared by crates of that size. It lives in the resident arena with the rest of the persistent state, and is serialized with the snapshot only when the world is packed.
- **A query restores the state it asks about.** The process has one arena for Box3D. A view of a confirmed world can restore that world's pages while this machine predicts ahead; the next tick restores its own pages before it steps. Queries themselves change no match state.
- **A world at rest isn't stepped.** Nothing awake, nothing written to any body and the gravity it had: there's no step or commit, and nothing of the match is written. What's left is checking body controls and comparing collider source data for edits.
- **Sensors and contacts are resident too.** What a sensor has in it, and the contact speeds and impulses that the next step and `Hit` use, survive a rollback with the rest of the world.
- **Whether a body is asleep is told like its place**: `Body` has what the last step left (`stepAsleep`) beside `asleep`, so a write is the two differing, found with no call into Box3D, as a body's settings are. A body may already have been woken by another body earlier in the tick, so comparing against Box3D itself would mistake that for code putting it back to sleep. Both bytes fit beside `bullet`, so the component is as big as it was. Requests to sleep are applied last, before the step. `Leave` also reads back the bodies Box3D put to sleep with them.
- **A native hot reload can keep the arena.** It outlives the game's library, and the new build relinks callbacks before using it. A reload that carries the world through canonical bytes, as the web does, rebuilds its arena instead. The resident type and its engine interface are Provisional; for now they're for libraries that come with Tide.

## What it costs

A resident tick pays for copying touched pages and computing the digest, plus the bodies' components going in and out. Keeping a snapshot shares the resident's pages; hashing reads its stamp and digest. A rollback copies the pages that differ from the target state. A query of another state pays that restore too, and the next tick pays to restore its own. Packing a world still writes the full canonical snapshot and geometry.

These costs depend on how much of the arena is touched, not only on how many bodies move: trees, contacts, sleeping sets and allocator metadata can touch pages too. A body's type changing or many bodies waking can mark the whole state. A world at rest skips the commit.

### Live collider checking (2026-10-22)

Live geometry requires exact source comparisons each tick, including list contents. Unchanged shapes do not cook, allocate or wake bodies. An optimized Windows sample with 2,400 resting boxes took about 0.085 ms per tick, including about 0.07 ms checking boxes; no native step or resident commit was needed. This was a single contended-machine diagnostic, not a controlled comparison with the historical measurements below. Mesh and height-field checks scale with their input list sizes, and edits pay for cooking as well.

### Resident comparison (2026-10-07)

Three alternating runs of the image-list baseline (`0cec11e`) and resident build, Windows clang 23 release and WebAssembly release through Node's WASI, not a browser. The machine remained busy: CPU load was 63% before the rounds and 24% after. Each entry is the median (minimum–maximum) across those runs, in milliseconds. The costs combine the tick with keeping its snapshot and hashing on one thread, since the resident commit replaced work formerly split between the image, list and hash.

| 2,400 crates | Windows image list | Windows resident | Node image list | Node resident |
|---|---|---|---|---|
| Everything moving | 1.529 (1.521–1.916) | 1.531 (1.520–1.599) | 2.207 (2.204–2.208) | 2.466 (2.334–2.488) |
| One crate pushed | 0.121 (0.120–0.143) | 0.138 (0.136–0.147) | 0.172 (0.164–0.195) | 0.170 (0.169–0.180) |
| At rest | 0.015 (0.014–0.015) | 0.014 (0.014–0.015) | 0.029 (0.028–0.029) | 0.027 (0.027–0.029) |
| A type changed | 0.242 (0.242–0.272) | 0.249 (0.238–0.316) | 0.423 (0.339–0.474) | 0.292 (0.291–0.333) |
| A crate put to sleep and woken in turn | 0.257 (0.254–0.364) | 0.099 (0.099–0.101) | 0.417 (0.355–0.426) | 0.123 (0.118–0.131) |

Resident state cuts hashing, but isn't a cut to every workload's total. With one crate pushed it still conservatively touches 46% of the arena's pages natively, 45% on the web; the native median is slower. With everything moving it copies every page: the combined native cost is similar, and Node's is higher. The arena span is 7.08 MB, against the baseline's 3.48 MB canonical image plus 1.4 KB of geometry. Those are different representations, not total process memory or the memory retained by all snapshots: resident pages also preserve allocator state, spare buffers and bookkeeping omitted by the image.

In Sumo, four bots at a full ring cost 1.20 to 0.95 ms natively and 1.62 to 1.25 under Node, including snapshot/hash. Five ticks run again averaged 5.29 to 3.48 ms natively and 7.53 to 4.63 under Node; ten averaged 5.53 to 4.12 and 7.59 to 5.10. These are medians of each run's rollback mean, which includes keeping and hashing the rerun ticks. A single restored tick went from 0.82 to 0.15 ms natively and 0.93 to 0.20 under Node. Raising a ring's body-making tick was slower, 8.86 to 9.87 ms and 25.08 to 33.00; settling after it also cost more. Its arena span grows from about 7.1 to 9.1 MB where the canonical image occupied 3.8 to 4.8 MB. These contended runs describe the tradeoff; they aren't precise estimates of an idle machine's speedup.

### Before resident state

The following is the measured baseline for the previous image-list implementation, not the resident implementation above. It used a release build, 2,400 crates in stacks (an image of 3.5 MB, and 1.4 KB of geometry), one thread, with the world kept and hashed every tick as a client's is:

| A tick, in milliseconds | Everything moving | One crate pushed | At rest |
|---|---|---|---|
| The tick | 1.42 | 0.09 | 0.01 |
| of it, Box3D's step | 1.0 | 0.03 | 0 |
| of it, its state into an image | 0.17 | 0.02 | 0 |
| of it, the image into the match | 0.13 | 0.02 | 0 |
| of it, bodies in and out | 0.11 | 0.02 | 0.01 |
| Keeping and hashing the world, after it | 0.35 | 0.08 | 0.00 |
| Of the image, what changed | 68% | 10% | 0% |
| Box3D alone, the same scene in plain C: a step | 1.0 | 0.015 | 0.0002 |

In that baseline, Box3D alone was its upstream sources with its own flags, one thread, no Tide. Writing and comparing the image added work each tick; restoring it made Box3D's world again once per rollback, about a millisecond for this scene. Those image-list timings don't describe resident commits or restores.

Bodies still go in and out through two systems written in C (`extern system`, `tide/rows.h`), which read the arrays the match keeps and write only bodies the step changed. The step still runs on the tick's threads (`tide_job_start`, `tide/jobs.h`) once 512 bodies are awake, with the same result on any number of them. On the web it solves four contacts at a time with WebAssembly SIMD; a desktop with AVX2 solves eight. These paths give the same simulation to the bit.

## Provisional

The owner approved the all-hit and ignore-body additions on 2026-10-18. The following earlier choices remain provisional. The owner asked for these five things (2026-10-06): shapes on entities of their own, a body's type that can change, queries, what's in a sensor and hits, and tuning as fields of `Physics`.

- **`Part`**, with `of`; its own handles (`box`, `sphere`, `capsule`, `mesh`) in it. A part with no body to be on, when its shapes would be made or once its body goes, is destroyed: the one way both are handled. An entity with a `Body` and a `Part` has its shapes on the part's body.
- **`part` and `otherPart`** in `Touched` and `Parted`, each the entity itself for a shape of its own. The events still go to the bodies' entities, and a part's own entity hears nothing. A part that was destroyed still says its body, with `default` for the part.
- **A type that changes sends nothing for what still touches**, though Box3D lets go of it and finds it again.
- **A body's `gravityScale`, `linearDamping`, `angularDamping` and `bullet` follow what's written later too**, with its `type`: they were read when it was made, and silently not after.
- **`Raycast`, `Cast`, `Overlap`, `Inside`, and `RayHit`**: the names, a cast's `from`, `rotation`, `to` order, and a shape passed as its component (`Box { size = ... }`). That closest-hit queries pass sensors by; that a ray says nothing of what it starts in and a cast meets it at 0; that an entity destroyed since the step isn't found; that `Overlap` and `Inside` lists come in the order of `Body.id`.
- **`Hit`**, with `other`, `point`, `normal` and `speed`, and no parts. `normal` is from the other towards the entity that hears it, as Unity's is. It's on for every shape, at Box3D's own threshold.
- **`contactStiffness`, `contactDamping`, `contactSpeed`, `bounceSpeed`, `hitSpeed` and `sleeping`**, with Box3D's defaults. A tuning that changes while the world is at rest doesn't step it, but for `sleeping` turned off.
- **The package's own that a game can see**: `Body`'s `stepType`, `stepGravityScale`, `stepLinearDamping`, `stepAngularDamping` and `stepBullet`, `Physics`' `goneShapes`, `parts` and `sensors`, the structs `Contact`, `Strike`, `Found` and `Sensors`, and the functions `Ready`, `Met`, `Owners` and `PartOf`.

## Remaining limits

- Native compound-container assets are not exposed; assemble compound bodies with `Part` entities.
- A parting from a destroyed entity still identifies it as `default`.
- Automatic conversion from imported rendering meshes and Unity translations for the new colliders, joints and controls are not provided.
- Cooked geometry remains in the resident world after its last collider is removed. Continually creating unique geometry consumes that space.
- The resident arena has a fixed 64 MiB limit; exhaustion stops the program. One Box3D arena per process means switching worlds costs a page restore.


## Rewinding queries

`physics.Raycast(...)`, `physics.Cast(...)`, `physics.Overlap(...)` and `physics.Inside(...)` query current geometry. To validate a shot against confirmed presentation, prepare a separate context once:

```csharp
settings { maxRewind = 0.25; } // Seconds; 0 (the default) disables history

// On players or moving geometry that should rewind:
// Spawn(Body { rewind = true }, ...);

system Fire(Controls controls, Body body, Physics physics)
{
    if (!controls.fire.down) return;
    // Validate weapon, origin, aim and firing cadence from match state first.
    if (!physics.Rewind(controls.viewTime, out var query)) return;
    if (query.Raycast(body.position, controls.aim, out var hit, body))
    {
        // Validate the hit entity is still alive, then apply the game's damage.
        ...
    }
}
```

The context has every current query form, including `RaycastAll`, every `CastAll` shape, overlaps, sensor `Inside` queries and ignore-body overloads. All-hit methods also accept `includeSensors: true`. It is detached geometry: preparing and querying it moves no live body, changes no contacts and sends no events. Query output is temporary unless the game saves it.

`Body.rewind` opts the entire body and its parts in. Their poses interpolate between retained physics steps, with `Snap()` preventing interpolation across a teleport. Shape geometry, filter bits and membership use the upper endpoint. Non-opted bodies remain current, so a wall still blocks a rewound player. A moving door can opt in too. Despawned historical shapes can be hit; their entity handles retain their generations, and the game must check that a damage target is still alive. A newly spawned opted-in body has no shape in an older frame. An existing body that opts out uses its current geometry.

`Rewind` returns false for disabled history, invalid, stale or future times, or an unavailable endpoint. The output then contains no queryable geometry. No time is clamped and no current-world fallback is substituted. History begins after the first completed physics step and follows the same snapshot, rollback and serialization path as the match. Geometry is that of the physics step, as with current queries; write movement before `PhysicsStages.Step` when presentation and collider poses should agree.

The server validates the input timestamp, but the game remains responsible for authoritative shot validation. It must never accept a client-supplied collider pose, origin or hit just because the timestamp passed validation.
