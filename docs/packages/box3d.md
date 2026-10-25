---
description: "Use Tide's Box3D package for deterministic 3D rigid bodies, collisions, compound shapes and world queries in multiplayer matches."
---

# Box3D: 3D physics

[Box3D](https://github.com/erincatto/box3d) is Erin Catto's 3D physics engine: rigid bodies that fall, collide, stack and come to rest. It comes with `tide` as a [package](../packages/using-and-writing.md#packages-that-come-with-tide). A body and its shapes are components of an entity, every tick steps them, and the match stays the same on every machine, through rollbacks, joins and hot reloads, with nothing for the game to do.

It's the [Box2D package](./box2d.md) again, for 3D: the same components, with a third axis, and the same rules. A few things are here first, which the Box2D package is to get: [parts](#parts), a body's type that [changes](#changing-how-a-body-is), what's [asked of the world](#asking-the-world), [hits](#hits) and the world's tuning.

::: warning An experiment, around an alpha
The package works and is tested, but its names and its shape aren't settled: they may change from one version of `tide` to the next. Box3D itself is at version 0.1, an alpha, and still changes too.
:::

## Getting it

`tide add` puts it in the game in the current folder:

```
tide add box3d
```

That writes the package's name in the game's `tide.packages`, which is all it takes:

```
box3d
```

Its code is in the namespace `Box3D`, so a file that uses it says `using Box3D;`. From then on `Body`, `Box`, `Sphere`, `Capsule`, `MeshCollider`, `HullCollider`, `HeightFieldCollider`, `Joint` and `Physics` name the package's. A name the game declares itself still means the game's: one with a `Body` of its own writes the package's in full, `Box3D.Body`.

A game that doesn't list the package compiles and ships none of it. One that does ships Box3D, which is under the MIT license, so it ships Box3D's notice with it: the `LICENSE` file in the package's `box3d` folder, `src/packages/box3d/box3d/LICENSE` of the folder `tide version` says `tide` is installed in.

## A first world

A floor, and a wall of twenty crates. Tap to throw a ball at it.

::: tide-example physics-3d A wall of crates, and balls to throw

<<< @/examples/physics-3d.tide

:::

- `Build` makes the world out of entities: each has a `Body`, and a `Box` for its shape. The floor is `Static`, and the crates are dynamic, as a body is unless it says otherwise.
- Nothing in the game moves a crate. The package's systems step the world every tick, and write where each body is into its `Body`.
- The views draw from the same components: `body.position` and `body.rotation`, with the size its `Box` or `Sphere` says.
- A tap is a [command](../game/commands.md), with the ray through the pixel it was on. `ThrowBall` spawns a body at its start, with a `velocity` along it.
- `Fall` destroys what fell off the floor, and its body goes with it.

The [Sumo source](https://github.com/Tide-Engine/tide-engine/tree/dev/examples/sumo) shows how impulses drive the balls and blasts, how static floor tiles become falling bodies, and how sensors detect ring-outs and pickups. It also includes deterministic arena construction, the room lobby and instanced drawing.

## Bodies

`Body` is a rigid body. An entity that has one is in the physics world from the next tick on. These are the fields a game uses:

```csharp
component Body
{
    BodyType type = BodyType.Dynamic; // Static, Kinematic or Dynamic
    float3 position;                  // In meters
    quaternion rotation = quaternion.identity;
    float3 velocity;                  // In meters a second
    float3 angularVelocity;           // Radians a second, by axis
    float3 impulse;                   // A push at the next step, once
    float3 angularImpulse;            // The same, around
    bool rewind;                      // Opt this body and its parts into collider history
    bool asleep;                      // At rest, and not stepped until something wakes it
    float gravityScale = 1;           // 0 for a body that floats
    float linearDamping;              // How fast it slows by itself
    float angularDamping;             // And how fast its spin does
    bool bullet;                      // For a small, fast body
}
```

- A **static** body never moves: a floor, a wall. Its weight doesn't matter.
- A **kinematic** body moves by its `velocity` whatever is in its way, and nothing pushes it: a platform, a door.
- A **dynamic** body is moved by gravity, by what it touches and by what code pushes it with.
- A **bullet** is checked against the other moving bodies all along its way, so that a small fast body doesn't pass through a thin one between two steps. It costs more, so it's for the few bodies that need it.

After each step, `position`, `rotation`, `velocity` and `angularVelocity` are where Box3D left the body, and `asleep` says whether it's [at rest](#bodies-at-rest). Code reads them like any component's fields, and [writing them moves the body](#moving-a-body), or puts it to sleep, or wakes it. A rotation that code writes is made of length 1 if it isn't.

[Views](../game/views.md) see a body between its last two ticks, as they see any component. The package says what two of the fields are, for that: `position` is a `[Position]` and `rotation` a `[Rotation]` (see [correction smoothing](../game/views.md#provisional-correction-smoothing)). So when the server corrects where this machine had predicted a body, a view sees it get there over a few frames, turning the short way, where it would jump. It changes nothing of the match.

`type` and the fields from `gravityScale` down say how the body is to be. They're set where the entity is spawned, and [can be written later](#changing-how-a-body-is).

The rest of the component is the package's own, and not for a game to write: `id` is Box3D's handle to the body, 0 until its first step; `stepPosition`, `stepRotation`, `stepVelocity`, `stepAngularVelocity` and `stepAsleep` are how the last step left it, which is how the package tells what code wrote since; and `stepType`, `stepGravityScale`, `stepLinearDamping`, `stepAngularDamping` and `stepBullet` are what Box3D has of how the body is to be.

## Shapes

A shape is what a body touches with, and what gives it its weight. It's a component on the body's entity:

```csharp
Spawn(Body { position = float3(0, 4, 0) }, Box { size = float3(2, 0.5, 1) });
Spawn(Body { position = float3(3, 4, 0) }, Sphere { radius = 0.4, bounce = 0.5 });
Spawn(Body { position = float3(6, 4, 0) }, Capsule { radius = 0.3, height = 1.8 });
```

These are their fields, with what each is unless a shape says otherwise:

```csharp
component Box
{
    float3 size = float3(1, 1, 1); // Its full width, height and depth
    float3 offset;                 // From the body's position
    float density = 1000;          // Kilograms a cubic meter: water's
    float friction = 0.6;          // How much it grips: 0 for ice
    float bounce;                  // 0 for no bounce, 1 for all of it
    bool sensor;                   // Bodies pass through it, and it says which
}

component Sphere
{
    float radius = 0.5;
    // And offset, density, friction, bounce and sensor, as a box's
}

component Capsule
{
    float radius = 0.5;
    float height = 2; // From end to end, round ends included
    // And the same
}
```

A capsule stands along its body's `y`. Sizes are in meters, so with the default density a crate a meter wide weighs a ton, and it takes an impulse of 1000 to change its speed by a meter a second.

An entity has one of each shape at most, and they can all be on the same body, each at its `offset`. A shape can be added to a body that's already there: `this.Add(Sphere { radius = 0.3 });`. A body of several boxes, or of shapes that come and go, has [parts](#parts).

A shape attaches at the next physics step after it is spawned or added. Later edits to geometry and material apply at the next step too; see [live collider changes](#live-collider-changes-and-removal).

A shape with `sensor` touches nothing and reports what's in it: see [Sensors](#sensors).

`Draw` has a cube and a sphere to draw the first two with, as the example does. A capsule is a cylinder and a sphere at each end:

```csharp
view Capsules(Body body, Capsule capsule)
{
    var straight = capsule.height - 2 * capsule.radius;
    var end = Math.Rotate(body.rotation, float3(0, straight / 2, 0));
    Draw.Cylinder(body.position, capsule.radius, straight, body.rotation, Color.white);
    Draw.Sphere(body.position + end, capsule.radius, Color.white);
    Draw.Sphere(body.position - end, capsule.radius, Color.white);
}
```

### Mesh colliders

`MeshCollider` puts an indexed triangle surface on a static body, including concave geometry and gaps:

```csharp
Spawn(Body { type = BodyType.Static }, MeshCollider {
    vertices = [float3(-10, 0, -10), float3(-10, 0, 10), float3(10, 0, 10), float3(10, 0, -10)],
    indices = [0, 1, 2, 0, 2, 3]
});
```

Each triple names zero-based vertex indices, counterclockwise from the front. These triangles face up. `scale` defaults to `float3(1)`; nonuniform and mirrored scales work, but no component may be zero. `offset` is added after scaling in body space, then the body's position and rotation place the surface. `friction` defaults to 0.6 and `bounce` to 0. There is no density or sensor setting.

A mesh can also be a [part](#parts), with `Part { of = terrain }`. Its body must be `BodyType.Static`. A mesh on a dynamic or kinematic body makes no shape, including one on a part. Once a body has a mesh, a write requesting another body type is reset to `Static`. Position and rotation writes still move it.

Geometry and material edits apply at the next physics step. Supply positions and indices explicitly, including when they come from an imported rendering mesh; automatic conversion from a rendering mesh isn't provided.

Each collider accepts 3–65,536 vertices and 1–65,536 triangles. Indices must be in range and come in complete triples. Scaled and offset coordinates must be finite and within ±1,000,000 on each axis; friction is finite and nonnegative, bounce finite in [0, 1]. Invalid data makes no shape. Degenerate triangles are discarded, and an entirely degenerate mesh makes no shape. An unchanged failed attachment is not retried; editing the data retries it.

Current and [rewound](#rewinding-queries) rays, primitive casts and overlaps include meshes. Rays and casts meet triangle fronts; these are thin surfaces with no solid interior. `Body.rewind` includes its meshes. Detached queries retain triangles in their spatial tree, so preparing a scene and keeping mesh history costs memory proportional to triangle count. Ordinary physics ticks capture none with history disabled.

Cooked mesh geometry lives in the match's resident state, shared by identical geometry, and follows snapshots, rollback, joins and reloads. Unused geometry stays until the world is destroyed, as box hulls do.

### Parts

A part is an entity whose shapes are on another entity's body. It has a shape, and a `Part` that names the body's entity:

```csharp
component Table { }
component Leg { }

event(Spawned) Build(with Main)
{
    // The top is the body's own shape, and each leg an entity of its own.
    var table = Spawn(Body { position = float3(0, 2, 0) }, Box { size = float3(2, 0.2, 2), offset = float3(0, 1, 0) }, Table);
    for (var i = 0; i < 4; i++)
    {
        var x = i % 2 == 0 ? -0.8 : 0.8;
        var z = i / 2 == 0 ? -0.8 : 0.8;
        Spawn(Box { size = float3(0.2, 1, 0.2), offset = float3(x, 0.4, z) }, Part { of = table }, Leg);
    }
}
```

- A part's shapes are on its body from the next step, at their `offset` from the body's `position`, and weigh on it as its own do. A part has no `Body`: it's somewhere only as part of the body it's on.
- **Destroying a part takes its shapes off the body**, at the next step, and the body stays. What rested on them wakes.
- **Destroying the body's entity destroys its parts**, the tick after.
- **A part with no body to be on is destroyed**: one whose `of` isn't an entity with a `Body` when its shapes would be made.
- A part can be a [sensor](#sensors), and a part can have a shape of each kind, like any entity.
- [`Touched`, `Parted`](#touching) and what [the world is asked](#asking-the-world) say which part a shape is on.

Changing `Part.of` moves its shapes to another body at the next step. Removing `Part` detaches them; its entity survives.

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

It has `vertices`, `scale = float3(1)`, `offset`, `density = 1000`, `friction = 0.6`, `bounce = 0` and `sensor = false`. Scale, then offset, apply in body space. Scale can be mirrored but has no zero component. Supply 4–65,536 points; the native cooker reduces the result to at most 128 vertices. Concavities are filled, so use a static `MeshCollider` or several convex parts for a concave object. Coplanar or otherwise uncookable data makes no shape.

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


## Moving a body

Code moves a body by writing its `Body`, and never by calling Box3D. After each step the package writes where every body is into its component, and before the next one it looks at each for what code changed since, and tells Box3D.

A push is an `impulse`. It's given once, at the next step, and the field is zero again after it:

```csharp
system Jump(PlayerInput input, mut Body body, with Player)
{
    if (input.jump.down) body.impulse = float3(0, 5000, 0);
}
```

An entity can be spawned with one, which pushes it at its first step, as soon as its shapes are on it:

```csharp
Spawn(Body { position = muzzle, impulse = aim * 40 }, Sphere { radius = 0.1 }, Shot);
```

A speed that code decides is written every tick. The rest of the vector stays Box3D's:

```csharp
system Walk(PlayerInput input, mut Body body, with Player)
{
    body.velocity.x = input.move.x * 4;
    body.velocity.z = input.move.y * 4;
}
```

A body that's somewhere else all at once has its `position` written. `Snap` has views draw it there without sliding (see [Jumps](../game/views.md#jumps)):

```csharp
event(Fell) Restart(mut Body body, Arena arena)
{
    body.position = arena.start;
    body.rotation = quaternion.identity;
    body.velocity = float3(0, 0, 0);
    this.Snap();
}
```

A kinematic body goes where its `velocity` takes it, and carries what stands on it:

```csharp
component Lift { }

event(Spawned) Build(with Main)
{
    Spawn(Body { type = BodyType.Kinematic, position = float3(0, 2, 0), velocity = float3(1, 0, 0) },
          Box { size = float3(3, 0.4, 3) }, Lift);
}

system Sweep(mut Body body, with Lift)
{
    if (body.position.x > 2) body.velocity = float3(-1, 0, 0);
    if (body.position.x < -2) body.velocity = float3(1, 0, 0);
}
```

### Changing how a body is

A body's `type`, and its `gravityScale`, `linearDamping`, `angularDamping` and `bullet`, are fields like the others: what code writes is how the body is from the next step.

```csharp
component Brick { int still; }

// A brick that has lain still for a second is set solid: nothing pushes it any more.
system Set(mut Body body, mut Brick brick)
{
    brick.still = Math.Length(body.velocity) < 0.01 ? brick.still + 1 : 0;
    if (brick.still == 60) body.type = BodyType.Static;
}

// In the water, things fall slower and stop sooner.
event(Touched touched) Sink(mut Body body, World world)
{
    if (!world.Has<Water>(touched.other)) return;
    body.gravityScale = 0.3;
    body.linearDamping = 2;
}
```

- **Any type to any other.** A body made `Static` stops where it is, whatever speed it had. One made `Kinematic` goes by the `velocity` written with it, and one made `Dynamic` is gravity's again.
- **What it touches stays as it was.** Nothing is sent for the shapes it still touches. Two bodies that never move don't touch each other, so a body made `Static` on a static floor gets [`Parted`](#touching) from the floor, and `Touched` again if it's made to move again.
- A change of type costs more than the other fields do: see [What it costs](#what-it-costs).

### Bodies at rest

Box3D stops moving a body that has come to rest: it's asleep, and costs nothing, until something touches it or code wakes it. `asleep` says which it is, after each step, and code that writes it puts the body to sleep or wakes it, at the next step:

```csharp
component Boulder { }

// The boulder hangs over the path, asleep from the tick it's spawned
event(Spawned) Build(with Main)
{
    Spawn(Body { position = float3(4, 9, 0), asleep = true }, Sphere { radius = 1 }, Boulder);
}

// ...until the game's own event says the trap was sprung
event(Sprung) Drop(mut Body body, with Boulder)
{
    body.asleep = false;
}

// The round is over: everything stops where it is
system Freeze(mut Body body, Round round)
{
    if (round.over && !body.asleep) body.asleep = true;
}
```

- **A body that's put to sleep stops where it is**, in the air too: its `velocity` and its `angularVelocity` are zero from then on, and none of its speed comes back when it wakes.
- **It takes what it touches with it.** Box3D sleeps and wakes bodies an *island* at a time: the bodies that touch each other, and the ones those touch, as the last step found them. So the bottom crate of a stack that's put to sleep takes the stack with it, and a crate that's sliding along a plank stops where it is when the plank is put to sleep. A kinematic body is one of them: a lift goes to sleep with what stands on it. A static body isn't: two crates on the same floor are two islands.
- **What hasn't touched it yet stays awake.** A crate that's still on its way down to a sleeping stack goes on falling, and wakes the stack when it lands.
- **Waking is the same**: `asleep = false` wakes the body and all of what it fell asleep with.
- **A static body is never asleep.** Its `asleep` reads false, whatever is written into it, and so does a sleeping body's once it's made `Static`.
- **It works where bodies don't fall asleep by themselves** ([`sleeping`](#the-world) off): one that code puts to sleep sleeps all the same, until something wakes it.

A sleeping body wakes by itself, with its island, when something awake touches it. And whatever else code writes into it wakes it too:

- A `velocity` or an `angularVelocity` that isn't zero, an `impulse` or an `angularImpulse`.
- A `position` or a `rotation`: the body is moved and wakes, so a crate at rest that's put in the air falls. What was touching it wakes too: a platform that's moved away, a static one as well, drops what stood on it.
- Its `type`, `gravityScale`, `linearDamping`, `angularDamping` or `bullet`: a ball asleep in the air with no gravity on it falls once it has some.

So does a body beside it that's destroyed, and [a new gravity](#the-world). After the step, `asleep` reads false.

**What code puts to sleep in a tick is asleep as that tick's step starts**, whatever else was written that tick, into it or into a body it touches. A push or a speed written with it is lost. A `position` written with it is where it sleeps: a body that's put somewhere else and to sleep at once hangs there. A new gravity in that tick doesn't wake it. What does is another body running into it in that very step.

That's for a body that was awake. Writing `asleep = true` into one that's asleep already writes nothing, so a sleeping body that code moves wakes like any other: `asleep` is false after that tick, and code that wants it asleep where it put it says so then.

A body that's **spawned asleep** starts where it's spawned, with none of the speed or the push it was spawned with. Bodies spawned asleep on each other are each asleep by themselves, as no step has seen them touch: one that's hit wakes, and the ones resting on it a step after each other. That's fine for a few. For a great many stacked on each other it's better to spawn them awake and put them to sleep a tick later, once they have had a step: they then sleep and wake a stack at a time, which costs less when a lot of them wake at once. [Sumo](https://github.com/Tide-Engine/tide-engine/tree/dev/examples/sumo) does that with the two thousand crates of its ring, which took a second to come to rest by themselves:

```csharp
[Sparse]
component Fresh { }

// Spawned with Fresh, and awake for the one step in which Box3D finds what rests on what
[After(PhysicsStages.Step)]
system Settle(Body body, mut World world, with Fresh)
{
    if (body.id == 0) return; // Spawned this tick: its body is made in the next
    mut var lulled = body;
    lulled.asleep = true;
    world.Set<Body>(this, lulled);
    this.Remove(Fresh);
}
```

It reads `Body` and puts the few it changes back through the [`World`](../game/systems.md#other-entities-the-world), as it runs for none of them in nearly every tick: a system that takes every crate's `Body` as `mut` has them all looked over in every tick.

A world where everything is asleep and nothing was written isn't stepped at all (see [What it costs](#what-it-costs)), and a body that's put to sleep costs what one that fell asleep does: nothing.

## When things happen in a tick

The package's systems run before the game's, as every [package's](../packages/using-and-writing.md#writing-a-package) do, in this order:

1. What was destroyed since the last step goes from the world: bodies, with their parts, and the shapes of parts that went by themselves.
2. Bodies and shapes that are new are made, and what code wrote into a `Body` goes to Box3D.
3. The world steps, by the tick's length, `Time.dt`.
4. Each body's `Body` gets where the step left it.
5. `Touched`, `Parted` and `Hit` are sent.

So a system of the game's reads this tick's positions, and what it writes goes into the next tick's step. The same goes for what an event's or a command's handler writes, at the end of the tick. An entity spawned in a tick has its body made at the start of the next, and is in that tick's step.

### Before the step

A system that has to write into this tick's step says so. The package's systems are one [named phase](../game/systems.md#named-phases), `PhysicsStages.Step`, and a system that's `Before` it runs before everything in the list above:

```csharp
[Before(PhysicsStages.Step)]
system Walk(PlayerInput input, mut Body body, with Player)
{
    body.velocity.x = input.move.x * 4;
    body.velocity.z = input.move.y * 4;
}
```

What `Walk` writes is the speed this tick steps with, a tick sooner than without the attribute. What it reads of a body is where the last tick's step left it.

That's the one way to get in ahead of the step, and a few things follow from how [systems are ordered](../game/systems.md#order):

- **A system that doesn't say runs after the step**, in a game where none says `Before`. Once one does, the systems written above it in the game's files run before the step too, as the package now waits for a system that comes after them. `[After(PhysicsStages.Step)]` on a system holds it after the step whatever the others say: write it on one that has to read where this tick's step left things.
- **`[Before(Step)]` is an error**, as no system has that name: the phase is what to name.
- **The package's own systems aren't for a game to name.** `tide schedule` lists them, `Box3D.Enter`, `Box3D.Simulate`, `Box3D.Leave` and the rest, and they keep their order whatever a game orders its systems by. But a system that's put among them, with `[Before(Box3D.Simulate)]` say, runs after what code wrote has gone to Box3D and before the bodies come back: where it writes a body's place or its speed, the step's own is written over it.

A tick is one step, so a match at the default 60 ticks a second steps a 60th of a second at a time. A lower `tickRate` makes each step longer and the world less steady.

## Touching

When two bodies start touching, the package sends `Touched` to each one's entity, and `Parted` when they stop. Each has the `other` entity:

```csharp
component Fragile { int hits; }

// Runs for the entity that touched something: `this` is that entity.
event(Touched) Crack(mut Fragile fragile)
{
    fragile.hits += 1;
    if (fragile.hits == 3) this.Destroy();
}

// What it touched is `touched.other`: the World has its components.
event(Touched touched) Land(World world, with Ball)
{
    if (world.Has<Floor>(touched.other)) this.Destroy();
}
```

They're [events](../game/events.md) like any other: a handler runs for the entity the event was sent to, when that entity has the components the handler takes, at the end of the tick whose step found the touch. A [local handler](../game/events.md#local-handlers) hears them too, on each machine, for a sound or a spark.

They're sent once for each pair of shapes that touch, so a body whose `Box` and `Sphere` are both on the floor hears `Touched` twice. Each says which shapes:

```csharp
event Touched
{
    Entity other;     // The other body's entity
    Entity part;      // Whose shape of this entity's it was: its own, or a part's
    Entity otherPart; // ...and whose of the other's
}
```

`part` is the entity the shape is a component of: this entity itself, for a shape of its own, or one of its [parts](#parts). The events go to the bodies' entities either way, and a part's own entity hears nothing.

```csharp
// The table hears which of its legs met the floor.
event(Touched touched) Stand(World world, with Table)
{
    if (world.Has<Leg>(touched.part)) Debug.Log("a leg is down");
}
```

When the other entity was destroyed while they touched, `Parted` still comes, and its `other` is no entity: `parted.other == default`. Which entity it was isn't said. When only a part of it was destroyed, `other` is the entity, which is still there, and `otherPart` is `default`.

### Sensors

A shape with `sensor = true` stops nothing: bodies pass through it. It sends the same two events, `Touched` when a body's shape comes into it and `Parted` when it leaves, to its own entity and to the other's:

```csharp
component Goal { int score; }

event(Spawned) Build(with Main)
{
    Spawn(Body { type = BodyType.Static, position = float3(8, 1, 0) }, Box { size = float3(1, 2, 4), sensor = true }, Goal);
}

// Runs for the goal: what came in is `touched.other`.
event(Touched touched) Score(mut Goal goal, World world)
{
    if (world.Has<Ball>(touched.other)) goal.score += 1;
}
```

- A sensor reports every body's shape that's in it: one on a body that never moves too, so a sensor under a character's feet says when it stands on the ground. A body that's in it from the start is reported at its first step.
- It can be on a body that moves, alone or beside the shapes the body touches with. It reports nothing of its own body's shapes.
- A sensor doesn't report another sensor.
- It has a weight like any shape, by its `density`: give it 0 where it's beside the shapes its body touches with, and shouldn't add to them.
- What's in a sensor is looked at once a step, where things are after it. A small body that's fast enough to cross a thin sensor between two steps may come and go unreported: make such a sensor thick.

What's in a sensor now is `Inside`'s to say, with no counting of who came and went:

```csharp
component Lift { }

// The lift goes up while anything stands in the sensor over it.
system Rise(Physics physics, mut Body body, with Lift)
{
    body.velocity.y = physics.Inside(this, out _) ? 1 : 0;
}
```

`physics.Inside(sensor, out found)` gives the entities whose bodies have a shape in an entity's sensors, each once, and whether there's any. `sensor` is the entity the sensor shape is a component of: a body's, or a [part's](#parts). It's one of the things [the world is asked](#asking-the-world).

### Hits

Two bodies that meet hard are sent `Hit`, each with the other:

```csharp
event Hit
{
    Entity other;
    float3 point;  // Where they met
    float3 normal; // The way this entity was pushed: from the other towards it
    float speed;   // How fast they closed on each other there, in meters a second
}
```

```csharp
component Glass { }

event(Hit hit) Shatter(with Glass)
{
    if (hit.speed > 6) this.Destroy();
}
```

- It's sent when they meet faster than `Physics.hitSpeed`, a meter a second unless a game [says](#the-world): things that only come to rest on each other say nothing.
- Once a step for each pair of shapes that meets that fast, so a ball that bounces hits each time it lands. A crate that lands flat hits once, at the corner that closed fastest.
- A [local handler](../game/events.md#local-handlers) hears it on each machine, for a sound or a spark, as the match doesn't need to.

## Asking the world

Methods on the `Physics` singleton that ask what's in the world, and change nothing. Each says whether it found anything, and gives what it found through [`out`](../language/functions.md):

```csharp
component Walker
{
    bool grounded;
    Entity on;
}

// What's under the feet: a ray a little way down from the body's place
system Land(Physics physics, Body body, mut Walker walker)
{
    walker.grounded = physics.Raycast(body.position, body.position - float3(0, 1.1, 0), out var under);
    walker.on = under.entity;
}
```

```csharp
struct RayHit
{
    Entity entity;  // The body's entity
    Entity part;    // The entity its shape is on: the same, or a part
    float3 point;   // Where
    float3 normal;  // The shape's surface there
    float fraction; // How far along the way, from 0 to 1
}
```

- **`physics.Raycast(from, to, out hit)`**: the first thing on the way from one point to another.
- **`physics.Cast(shape, from, rotation, to, out hit)`**: the first thing a shape meets that's moved from one place to another, turned by `rotation` all the way. The shape is a `Box`, a `Sphere` or a `Capsule`, as one on a body is: `physics.Cast(Sphere { radius = 0.5 }, from, quaternion.identity, to, out var hit)`. `hit.point` is where the two touch.
- **`physics.Overlap(shape, at, rotation, out found)`**: the entities whose bodies a shape at a place overlaps or touches, each once.
- **`physics.Inside(sensor, out found)`**: what's [in a sensor](#sensors).

```csharp
// Everything within three meters of a blast is pushed away from it.
event(Burst burst) Blast(Physics physics, mut World world)
{
    if (!physics.Overlap(Sphere { radius = 3 }, burst.at, quaternion.identity, out var near)) return;
    foreach (var entity in near)
    {
        if (!world.TryGet<Body>(entity, out mut var body)) continue;
        body.impulse = Math.Normalize(body.position - burst.at) * 4000;
        world.Set<Body>(entity, body);
    }
}
```

They can be asked from a system, an event's or a command's handler, a [view](../game/views.md) and the functions those call:

```csharp
// A view asks too: where the player's aim meets the world, for a dot.
view Aim(Physics physics, Devices devices)
{
    var screen = Screen.Flip(devices.pointer.position);
    var from = Draw.ScreenRayOrigin(screen);
    if (physics.Raycast(from, from + Draw.ScreenRayDirection(screen) * 100, out var hit))
        Draw.Sphere(hit.point, 0.05, Color.white);
}
```

A few rules hold for all of them:

- **They see the world as the last step left it.** A system after the step is asked of this tick's step, and one that's [before it](#before-the-step) of the last tick's. What code wrote into a body since isn't there yet, nor a body or a shape that was spawned since: each is in the world from the next step. An entity that was destroyed since is found no more, at once.
- **Sensors are skipped by default.** The all-hit methods below can include them. `Overlap` doesn't list sensors; `Inside` asks what's in one.
- **A ray says nothing of a shape it starts inside**, and a cast meets what its shape is in at `from` right there: a `fraction` of 0, and no `normal` or unique surface `point`. A few millimeters count as touching, so a shape cast from where a body rests is in what the body rests on: start a cast a little off it.
- **The answer is the same on every machine**, as everything in the match is. Of two things as near, the one whose shape was made first is the one that's met. `Overlap` and `Inside` lists come in the order of their bodies' `id`.
- **One body can be left out.** `Raycast` and `Cast` accept `ignore: body` after the output, excluding every shape on that body, including its parts: `physics.Raycast(body.position, target, out var hit, ignore: body)`.

### All hits and game filtering

`RaycastAll` and `CastAll` return an `out List<RayHit>`, sorted by increasing `fraction`. Equal fractions use the same deterministic shape order as the closest-hit methods. They return true if the list has any hits; otherwise the output is an empty list, replacing its previous contents.

```csharp
physics.RaycastAll(from, to, out var hits, ignore: body);
foreach (var hit in hits)
{
    if (!world.Has<Target>(hit.entity)) continue;
    // This is the nearest hit accepted by the game's rules.
    hit.entity.Send(Struck { at = hit.point });
    break;
}

physics.CastAll(Sphere { radius = 0.2 }, from, quaternion.identity, to, out var swept,
    ignore: body, includeSensors: true);
```

`CastAll` accepts the same `Box`, `Sphere` and `Capsule` shapes, `from`, `rotation` and `to` as `Cast`. Both all-hit methods accept `ignore: body`, which excludes every shape on that body, including its parts. `includeSensors` defaults to false and can be used with or without an ignored body.

Each collider contributes its first hit. Separate colliders on one body remain separate results, even if their `entity` and `part` are the same. A mesh collider contributes one hit for the whole mesh, not every triangle or entry-and-exit intersection. The ordinary `RayHit` values can be saved in match or local state. The same methods and options work on a rewound `Query`.

Use `Raycast` or `Cast` when only the nearest hit matters: they keep their early-pruning path. The optional `filter: QueryFilter` applies category/mask filtering before hits are collected; all-hit results let game code apply further component, team or other rules afterward.

## The world

`Physics` is a singleton, the world's. These are the fields a game sets:

```csharp
singleton Physics
{
    float3 gravity = float3(0, -10, 0);
    int substeps = 4;            // The smaller steps Box3D solves each step in
    float contactStiffness = 30; // How stiffly two bodies that touch are held apart, in cycles a second
    float contactDamping = 10;   // How fast that springing dies down: 1 just stops it
    float contactSpeed = 3;      // The fastest two bodies that overlap are pushed apart, in meters a second
    float bounceSpeed = 1;       // Slower than this, nothing bounces
    float hitSpeed = 1;          // Faster than this, two bodies that meet are sent Hit
    bool sleeping = true;        // Bodies at rest fall asleep
}
```

More substeps make stacks and fast bodies steadier, and a step slower. The rest are Box3D's own values, and a game that writes none of them steps as Box3D does:

- **`contactStiffness`** and **`contactDamping`** are the spring that holds touching bodies apart. A softer one (fewer cycles a second) lets them sink into each other a little as they land; less damping lets them spring back.
- **`contactSpeed`** is how fast bodies that are in each other come apart: lower, for things spawned overlapping that shouldn't fly off.
- **`bounceSpeed`** is the speed under which a `bounce` does nothing, so that things come to rest. A ball that should bounce off the gentlest touch wants it low.
- **`sleeping`** off keeps every body awake, and the world stepping every tick: for a few bodies that should never rest, and a cost with many.

Any match code that takes it `mut` writes them, and the next step uses them:

```csharp
event(Spawned) Setup(mut Physics physics, with Main)
{
    physics.gravity = float3(0, -1.6, 0);
}
```

A new gravity wakes every body that's [asleep](#bodies-at-rest), so that what was at rest feels it, and so does `sleeping` turned off. A game that changes gravity every tick has nothing at rest. The other fields wake nothing: they're how the next step that has something to step goes.

The other fields are the package's own. `moved` says whether the last tick stepped the world, and `bodies` and `shapes` how many were made so far; `stamp`, `state`, `gone`, `owners`, `goneShapes`, `parts` and `sensors` are how the package keeps Box3D's state in the match, and whose each body and shape is. `state` is a `Resident` field for the package's C; game code has nothing to read or write in it.

## Destroying

An entity that's destroyed takes its body with it, at the next step: `this.Destroy()` is all there is to it. What it was touching gets `Parted`. Its [parts](#parts) are destroyed with it, and a part that's destroyed by itself takes only its own shapes.

Removing `Body` removes its native body, parts and joints at the next physics step while keeping its entity. Removing a collider component removes just that shape. These changes follow Tide's deferred component rules, as described in [live collider changes and removal](#live-collider-changes-and-removal).

## In Unity

In a Unity project ([Unity](../tools/cli.md#unity)), Box3D's components go on a TideEntity GameObject like any of the game's, from Add Component's Tide/Box3D menu: a `Body`, and a `Box`, `Sphere` or `Capsule` for its shape. Unity's own `Rigidbody` and colliders aren't Box3D's, and do nothing in the match.

- A shape's size is by the GameObject's scale, as a collider's is: a `Box` of size 1 on a cube scaled (12, 1, 12) is 12 by 1 by 12 in the match. A `Sphere`'s radius is by the largest axis of the scale, and a `Capsule`'s radius by the larger of x and z, its height by y. In Play the component shows what you authored, the match's value divided by the scale.
- A selected shape is drawn in the Scene view, and its Edit Shape button (also in the Scene view's tools) gives it handles, as Edit Collider does.
- A body starts where its GameObject is, and the GameObject follows the body.
- A body of several shapes is a `Body` with a shape, and a child GameObject for each other shape with that shape and a `Part`, as colliders on a Rigidbody's children are. A `Part` whose `Of` is empty is a part of the nearest GameObject above it with a `Body`. Its shape is on the body where the child puts it, which the Scene view shows. A shape can't turn on its body, so a part turned against its body is a warning (a `Sphere` turns any way).
- A `MeshCollider` or `HullCollider` takes its triangles from a Unity Mesh: added to a GameObject with a MeshFilter, it starts with the filter's mesh, and its Inspector has a Mesh field and Copy to take another. Its `scale` is by the GameObject's scale, so a mesh scaled in the Scene collides as it's drawn.
- Joints go on a GameObject of their own with `Joint` and one kind's component, such as `RevoluteJoint`; `A` and `B` are the bodies' GameObjects. A joint's `force` and `torque` show in Play under From its package, and aren't authored.
- The fields Box3D keeps for itself, such as a body's `id` and its `step` values, aren't in the Inspector or the scene.

## Multiplayer

There's nothing to add for a match with several machines. Box3D's memory is resident in the match: `Physics.state` holds snapshots of the engine's arena, including the bodies, contacts, sleeping sets and geometry. After a step the package commits the 16 KiB pages it may have written; older worlds share the untouched pages. A client that predicted wrong restores the pages that differ from its earlier world, by page identity, and runs its ticks again. It doesn't serialize the world or build Box3D again for a rollback.

The arena's raw bytes hold this machine's pointers, so they aren't sent or hashed. The match's hash includes the state's stamp and Box3D's deterministic digest, which covers its counts, awake bodies, contact impulses and persistent joint impulses. When a player joins, a world is resynced, a machine takes over the match, or a reload packs its state, the package writes a canonical Box3D snapshot and its geometry. The receiving machine builds its own arena from those bytes. A network delta currently carries that canonical state whole when it differs from its base. The same match gives the same simulation on desktop and on the web, so their players share one. See [Networking](../multiplayer/networking.md) and [Determinism](../multiplayer/determinism.md).

A query, including one from a view of a confirmed world while this machine predicts ahead, restores the resident state it asks about. The process has one Box3D arena, so the next tick restores its own state before stepping it. A native hot reload can keep the arena and relink the new build's callbacks; a reload through canonical bytes, as on the web, makes the arena from those bytes.

It's also why code never calls Box3D: everything that changes the physics world goes through the match, as components. The package itself talks to Box3D through [C functions](../c/functions.md), and reads the bodies back with [systems written in C](../c/functions.md#systems-written-in-c).

### A sharded world

In a [sharded world](../multiplayer/shards.md#physics), each shard has its own `Physics` and steps its own Box3D world of the bodies it owns. `Body.position` decides which shard has a body. A body that crosses a border is made anew on the next shard from its `Body`, where it was and as fast, and its shapes are put back on it there. Bodies another shard owns near the border are copies here, of their own type and as heavy, with all their shapes, their parts' included, where their owner last had them. This shard's bodies run into them as into the bodies, and each side tells its own entities of a touch or a hit. Two dynamic bodies that touch go to one shard while they do, so a stack or a push across the border is one shard's. A static body that reaches far past where it is, like a long floor, is copied to every shard it reaches. A part goes with its body, and jointed bodies go together with their joint, so the shard that has one has them all and the joint holds. `Physics`' settings are the world's: set them as in any game, and every shard has them.

## What it costs

A resident tick copies the pages Box3D may have written and computes its digest. Keeping a snapshot shares the pages, and hashing reads the stamp and digest. A rollback copies the pages that differ from its target state; packing a world for another machine still writes the full canonical snapshot and geometry.

- **A world at rest costs next to nothing.** It isn't stepped or committed, and nothing of the match is written. What's left is checking body controls and comparing collider source data for edits.
- **Commit cost goes by touched pages.** Bodies, contacts, trees, sleeping sets and allocator metadata can touch pages, so one moving body can cost more than its own bytes. The marks are conservative: each touched page is copied without comparing it first. A body's type changing or many bodies waking can mark the whole state.
- **A rollback and a query of another world restore pages.** They avoid making Box3D again from a serialized image. A view that restores a confirmed world also leaves the next tick to restore its predicted world.
- **The step runs on the tick's threads** once 512 bodies are awake, with the same result on any number of them. On the web it solves four contacts at a time with WebAssembly SIMD; a desktop with AVX2 solves eight. Their simulations agree to the bit.

### Live collider checking (2026-10-22)

Live geometry requires exact source comparisons each tick, including list contents. Unchanged shapes do not cook, allocate or wake bodies. An optimized Windows sample with 2,400 resting boxes took about 0.085 ms per tick, including about 0.07 ms checking boxes; no native step or resident commit was needed. This was a single contended-machine diagnostic, not a controlled comparison with the historical measurements below. Mesh and height-field checks scale with their input list sizes, and edits pay for cooking as well.

### Resident comparison (2026-10-07)

Three alternating runs against the image-list baseline (`0cec11e`), using Windows clang 23 release and WebAssembly release under Node's WASI, not a browser. The machine was contended: CPU load was 63% before the rounds and 24% after. These are medians (minimum–maximum) in milliseconds for 2,400 crates, combining the tick with keeping its snapshot and hashing on one thread. That combined cost accounts for work moved from writing and hashing the image into resident commits.

| Workload | Windows image list | Windows resident | Node image list | Node resident |
|---|---|---|---|---|
| Everything moving | 1.529 (1.521–1.916) | 1.531 (1.520–1.599) | 2.207 (2.204–2.208) | 2.466 (2.334–2.488) |
| One crate pushed | 0.121 (0.120–0.143) | 0.138 (0.136–0.147) | 0.172 (0.164–0.195) | 0.170 (0.169–0.180) |
| At rest | 0.015 (0.014–0.015) | 0.014 (0.014–0.015) | 0.029 (0.028–0.029) | 0.027 (0.027–0.029) |
| A type changed | 0.242 (0.242–0.272) | 0.249 (0.238–0.316) | 0.423 (0.339–0.474) | 0.292 (0.291–0.333) |
| A crate put to sleep and woken in turn | 0.257 (0.254–0.364) | 0.099 (0.099–0.101) | 0.417 (0.355–0.426) | 0.123 (0.118–0.131) |

The resident arena spans 7.08 MB, where the former canonical image was 3.48 MB plus 1.4 KB of geometry. Arena span and serialized size measure different representations; neither is total process memory or the memory retained by all snapshots. Conservative marks touch 45–46% of the arena even with one crate pushed. Hashing costs less, but the native total for that case is higher, and everything moving costs more under Node.

The [Sumo example](https://github.com/Tide-Engine/tide-engine/tree/dev/examples/sumo) benefits most in rollback: five ticks run again averaged 5.29 to 3.48 ms natively and 7.53 to 4.63 under Node; ten averaged 5.53 to 4.12 and 7.59 to 5.10. These are medians of the three runs' rollback means, with snapshots and hashes included. Four bots at a full ring cost 1.20 to 0.95 ms a tick natively and 1.62 to 1.25 under Node, including snapshot/hash. Raising the ring's body-making tick was slower, 8.86 to 9.87 ms and 25.08 to 33.00; settling after it also cost more. Sumo's arena span grows from about 7.1 to 9.1 MB where its canonical image occupied 3.8 to 4.8 MB. The remaining contention limits what these comparisons say about an idle machine.

### Before resident state

This is the measured baseline for the previous implementation, which wrote a serialized image into a match list each tick. These timings don't measure the resident implementation above. It was a release build with 2,400 crates in stacks, on one thread, with the world kept and hashed every tick as a client's is:

| A tick, in milliseconds | Everything moving | One crate pushed | At rest |
|---|---|---|---|
| The tick | 1.42 | 0.09 | 0.01 |
| of it, Box3D's step | 1.0 | 0.03 | 0 |
| Keeping and hashing the world, after it | 0.35 | 0.08 | 0.00 |
| Box3D alone, the same scene in plain C: a step | 1.0 | 0.015 | 0.0002 |

In that implementation the serialized state was 3.5 MB, about a kilobyte and a half a body. Most of the extra work was writing it into the match, comparing its chunks and hashing what changed. A rollback made Box3D's world again from it, once, taking about a millisecond before the ticks it ran again. Resident commits and page restores replace that work.

## Remaining limits

- Native compound-container assets are not exposed; assemble compound bodies with `Part` entities.
- A parting from a destroyed entity still identifies it as `default`.
- Automatic conversion from imported rendering meshes is not provided outside Unity. In Unity a `HeightFieldCollider` isn't filled from a Terrain, and joints and mesh shapes have no Scene-view handles.
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

The history window covers the full age of the frame when the server executes the shot: confirmation delivery, time since the last drawn frame, input lead and delivery of the input all contribute. Low frame rates, stalls, jitter and packet loss can push a shot outside `maxRewind` even when its ping seems small. The engine uses the actual presented tick and fraction, rather than estimating the shot time by subtracting ping.

The server validates the input timestamp, but the game remains responsible for authoritative shot validation. It must never accept a client-supplied collider pose, origin or hit just because the timestamp passed validation.
