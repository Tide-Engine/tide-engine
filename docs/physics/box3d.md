# 3D physics (Box3D)

[Box3D](https://github.com/erincatto/box3d) is Erin Catto's 3D physics engine: rigid bodies that fall, collide, stack and come to rest. It comes with `tide` as a [package](../tools/packages.md#packages-that-come-with-tide). A body and its shapes are components of an entity, every tick steps them, and the match stays the same on every machine, through rollbacks, joins and hot reloads, with nothing for the game to do.

It's the [Box2D package](./box2d.md) again, for 3D: the same components, with a third axis, and the same rules.

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

Its code is in the namespace `Box3D`, so a file that uses it says `using Box3D;`. From then on `Body`, `Box`, `Sphere`, `Capsule` and `Physics` name the package's, even in a game that declares a `Body` of its own: a game like that leaves the `using` out and writes the package's names in full, `Box3D.Body`.

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

After each step, `position`, `rotation`, `velocity` and `angularVelocity` are where Box3D left the body. Code reads them like any component's fields, and [writing them moves the body](#moving-a-body). A rotation that code writes is made of length 1 if it isn't.

[Views](../game/views.md) see a body between its last two ticks, as they see any component. The package says what two of the fields are, for that: `position` is a `[Position]` and `rotation` a `[Rotation]` (see [correction smoothing](../game/views.md#provisional-correction-smoothing)). So when the server corrects where this machine had predicted a body, a view sees it get there over a few frames, turning the short way, where it would jump. It changes nothing of the match.

`type` and the fields from `gravityScale` down are read when the body is made, so they're set where the entity is spawned. Changing them later does nothing yet.

The rest of the component is the package's own, and not for a game to write: `id` is Box3D's handle to the body, 0 until its first step, and `stepPosition`, `stepRotation`, `stepVelocity` and `stepAngularVelocity` are where the last step left it, which is how the package tells what code wrote since.

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

An entity has one of each shape at most, and they can all be on the same body, each at its `offset`. A shape can be added to a body that's already there: `this.Add(Sphere { radius = 0.3 });`.

A shape is read once, when it's put on its body, the tick after it's spawned or added. Changing its component later does nothing yet.

A shape with `sensor` touches nothing and reports what's in it: see [Sensors](#sensors).

`Draw3D` has a cube and a sphere to draw the first two with, as the example does. A capsule is a cylinder and a sphere at each end:

```csharp
view Capsules(Body body, Capsule capsule)
{
    var straight = capsule.height - 2 * capsule.radius;
    var end = Math.Rotate(body.rotation, float3(0, straight / 2, 0));
    Draw3D.Cylinder(body.position, capsule.radius, straight, body.rotation, Color.white);
    Draw3D.Sphere(body.position + end, capsule.radius, Color.white);
    Draw3D.Sphere(body.position - end, capsule.radius, Color.white);
}
```

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

### Bodies at rest

Box3D stops moving a body that has come to rest: it's asleep, and costs nothing, until something touches it or pushes it. Whatever code writes into a sleeping body wakes it:

- A `velocity` or an `angularVelocity` that isn't zero, an `impulse` or an `angularImpulse`.
- A `position` or a `rotation`: the body is moved and wakes, so a crate at rest that's put in the air falls. What was touching it wakes too: a platform that's moved away, a static one as well, drops what stood on it.

A world where everything is asleep and nothing was written isn't stepped at all (see [What it costs](#what-it-costs)).

## When things happen in a tick

The package's systems run before the game's, as every [package's](../tools/packages.md#writing-a-package) do, in this order:

1. Bodies and shapes that are new are made, and what code wrote into a `Body` goes to Box3D.
2. The world steps, by the tick's length, `Time.dt`.
3. Each body's `Body` gets where the step left it.
4. `Touched` and `Parted` are sent.

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

They're sent once for each pair of shapes that touch, so a body whose `Box` and `Sphere` are both on the floor hears `Touched` twice.

When the other entity was destroyed while they touched, `Parted` still comes, and its `other` is no entity: `parted.other == default`. Which entity it was isn't said.

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

## The world

`Physics` is a singleton, the world's. A game sets two of its fields:

```csharp
singleton Physics
{
    float3 gravity = float3(0, -10, 0);
    int substeps = 4; // The smaller steps Box3D solves each step in
}
```

More substeps make stacks and fast bodies steadier, and a step slower.

Any match code that takes it `mut` writes them, and the next step uses them:

```csharp
event(Spawned) Setup(mut Physics physics, with Main)
{
    physics.gravity = float3(0, -1.6, 0);
}
```

A new gravity wakes every body that's [asleep](#bodies-at-rest), so that what was at rest feels it. A game that changes it every tick has nothing at rest.

The other fields are the package's own. `moved` says whether the last tick stepped the world, and `bodies` and `shapes` how many were made so far; `stamp`, `size`, `image`, `geometry`, `gone` and `owners` are how the package keeps Box3D's state in the match.

## Destroying

An entity that's destroyed takes its body with it, at the next step: `this.Destroy()` is all there is to it. What it was touching gets `Parted`.

Removing `Body` from an entity that stays doesn't take its body out of the world yet. Destroy the entity.

## Multiplayer

There's nothing to add for a match with several machines. Box3D keeps its world in C's memory, which a rollback or a joining player wouldn't reach, so after every step the package writes Box3D's whole state into the match, as bytes in `Physics`. A client that predicted wrong and runs ticks again, a player who joins, a build that's reloaded and a machine that takes the match over all go on from that state.

That state is in the match's hash, and it's the same bytes on every machine: the same match gives the same result on desktop and on the web, so their players share one. See [Networking](../multiplayer/networking.md) and [Determinism](../multiplayer/determinism.md).

It's also why code never calls Box3D: everything that changes the physics world goes through the match, as components. The package itself talks to Box3D through [C functions](../c/functions.md), and reads the bodies back with [systems written in C](../c/functions.md#systems-written-in-c).

## What it costs

Measured on a release build, with 2,400 crates in stacks, on one thread, with the world kept and hashed every tick as a client's is:

| A tick, in milliseconds | Everything moving | One crate pushed | At rest |
|---|---|---|---|
| The tick | 1.44 | 0.10 | 0.02 |
| of it, Box3D's step | 1.0 | 0.03 | 0 |
| Keeping and hashing the world, after it | 0.35 | 0.09 | 0.00 |
| Box3D alone, the same scene in plain C: a step | 1.0 | 0.015 | 0.0002 |

- **A world at rest costs next to nothing.** It isn't stepped, and nothing of the match is written. What's left is looking at each body for what code wrote.
- **A tick goes by what's moving.** With one crate pushed among 2,400 it takes a tenth of a millisecond: the step is a third of that, and the rest is the package's.
- **What the package adds to Box3D alone** is 0.44 ms a tick with everything moving, 0.09 with one crate pushed, and 0.02 at rest. Most of it is keeping Box3D's state in the match: about a kilobyte and a half a body, 3.5 MB here. With most of the world awake all of it is written each tick, and only what changed is copied and hashed. With most of it asleep, only what's awake is written. A player who joins downloads it with the rest of the world.
- **A rollback** makes Box3D's world again from that state, once: about a millisecond for this world, before the ticks it runs again.
- **The step runs on the tick's threads** once 512 bodies are awake, and gives the same world on any number of them: 1.3 ms on one thread and 0.5 on twelve, with every crate awake.
- **On the web** Box3D has no SIMD, and the same step takes about 2.2 ms on one thread.
- **Building**: Box3D compiles in about 8 seconds, once, and adds about 800 KB to a game.

## Not there yet

- Hulls other than boxes, meshes, height fields, compounds, joints, ray casts and overlap tests.
- `Touched` and `Parted` don't say which shape of an entity's it was, and a parting from an entity that was destroyed doesn't say which entity.
- A body's `type` and the fields read when it's made don't follow later changes, and a shape's fields don't either.
- Removing `Body` from an entity that stays leaves its body in the world.
- Box3D keeps what a box of each size is made of, in `Physics`, and never lets one go: a game that makes boxes of ever new sizes keeps them all.
