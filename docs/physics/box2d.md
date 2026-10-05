# 2D physics (Box2D)

[Box2D](https://box2d.org) is Erin Catto's 2D physics engine: rigid bodies that fall, collide, stack and come to rest. It comes with `tide` as a [package](../tools/packages.md#packages-that-come-with-tide). A body and its shapes are components of an entity, every tick steps them, and the match stays the same on every machine, through rollbacks, joins and hot reloads, with nothing for the game to do.

For 3D, see [3D physics (Box3D)](./box3d.md), which is the same package around Box3D.

::: warning An experiment
The package works and is tested, but its names and its shape aren't settled: they may change from one version of `tide` to the next.
:::

## Getting it

`tide add` puts it in the game in the current folder:

```
tide add box2d
```

That writes the package's name in the game's `tide.packages`, which is all it takes:

```
box2d
```

Its code is in the namespace `Box2D`, so a file that uses it says `using Box2D;`. From then on `Body`, `Box`, `Circle` and `Physics` name the package's, even in a game that declares a `Body` of its own: a game like that leaves the `using` out and writes the package's names in full, `Box2D.Body`.

A game that doesn't list the package compiles and ships none of it. One that does ships Box2D, which is under the MIT license, so it ships Box2D's notice with it: the `LICENSE` file in the package's `box2d` folder, `src/packages/box2d/box2d/LICENSE` of the folder `tide version` says `tide` is installed in.

## A first world

A floor, two walls and three stacks of crates. Tap a crate to kick it, and anywhere else to drop a ball.

::: tide-example physics-2d Crates, balls and a floor

<<< @/examples/physics-2d.tide

:::

- `Build` makes the world out of entities: each has a `Body`, and a `Box` or a `Circle` for its shape. The floor and the walls are `Static`, and the crates are dynamic, as a body is unless it says otherwise.
- Nothing in the game moves a crate. The package's systems step the world every tick, and write where each body is into its `Body`.
- The views draw from the same components: `body.position` and `body.angle`, with the size its `Box` or `Circle` says.
- A tap is a [command](../game/commands.md). `DropBall` spawns a body where the tap was. `KickCrate` writes the crate's `impulse`, and the next step pushes it.
- `Expire` destroys a ball after ten seconds, and its body goes with it.
- `Count` hears `Touched`, which the package sends to a body's entity when it starts touching another.

## Bodies

`Body` is a rigid body. An entity that has one is in the physics world from the next tick on. These are the fields a game uses:

```csharp
component Body
{
    BodyType type = BodyType.Dynamic; // Static, Kinematic or Dynamic
    float2 position;                  // In meters
    float angle;                      // In radians, counterclockwise
    float2 velocity;                  // In meters a second
    float spin;                       // In radians a second
    float2 impulse;                   // A push at the next step, once
    float turn;                       // The same, around
    float gravityScale = 1;           // 0 for a body that floats
    float linearDamping;              // How fast it slows by itself
    float angularDamping;             // And how fast its spin does
    bool fixedRotation;               // It never turns
    bool bullet;                      // For a small, fast body
}
```

- A **static** body never moves: a floor, a wall. Its weight doesn't matter.
- A **kinematic** body moves by its `velocity` whatever is in its way, and nothing pushes it: a platform, a door.
- A **dynamic** body is moved by gravity, by what it touches and by what code pushes it with.
- A **bullet** is checked against the other moving bodies all along its way, so that a small fast body doesn't pass through a thin one between two steps. It costs more, so it's for the few bodies that need it.

After each step, `position`, `angle`, `velocity` and `spin` are where Box2D left the body, with `angle` between -π and π. Code reads them like any component's fields, and [writing them moves the body](#moving-a-body).

`type` and the fields from `gravityScale` down are read when the body is made, so they're set where the entity is spawned. Changing them later does nothing yet.

The rest of the component is the package's own, and not for a game to write: `id` is Box2D's handle to the body, 0 until its first step, and `stepPosition`, `stepAngle`, `stepVelocity` and `stepSpin` are where the last step left it, which is how the package tells what code wrote since.

Box2D is tuned for meters, kilograms and seconds, and for moving things between a tenth of a meter and ten meters across. So a crate is 1 wide, not 100: the view's camera makes it as big on screen as the game wants.

## Shapes

A shape is what a body touches with, and what gives it its weight. It's a component on the body's entity:

```csharp
Spawn(Body { position = float2(0, 4) }, Box { size = float2(2, 0.5) });
Spawn(Body { position = float2(3, 4) }, Circle { radius = 0.4, bounce = 0.5 });
```

These are their fields, with what each is unless a shape says otherwise:

```csharp
component Box
{
    float2 size = float2(1, 1); // Its full width and height
    float2 offset;              // From the body's position
    float density = 1;          // Its weight for its area
    float friction = 0.6;       // How much it grips: 0 for ice
    float bounce;               // 0 for no bounce, 1 for all of it
    bool sensor;                // Bodies pass through it
}

component Circle
{
    float radius = 0.5;
    float2 offset;
    float density = 1;
    float friction = 0.6;
    float bounce;
    bool sensor;
}
```

With the default density a box of 1 by 1 weighs 1, and an impulse of 1 changes its speed by a meter a second.

An entity has one `Box` at most, and one `Circle`, and both can be on the same body, each at its `offset`. A shape can be added to a body that's already there: `this.Add(Circle { radius = 0.3 });`.

A shape is read once, when it's put on its body, the tick after it's spawned or added. Changing its component later does nothing yet.

## Moving a body

Code moves a body by writing its `Body`, and never by calling Box2D. After each step the package writes where every body is into its component, and before the next one it looks at each for what code changed since, and tells Box2D.

A push is an `impulse`. It's given once, at the next step, and the field is zero again after it:

```csharp
system Jump(PlayerInput input, mut Body body, with Player)
{
    if (input.jump.down) body.impulse = float2(0, 5);
}
```

A speed that code decides is written every tick. The other half of the vector stays Box2D's:

```csharp
system Walk(PlayerInput input, mut Body body, with Player)
{
    body.velocity.x = input.move.x * 4;
}
```

A body that's somewhere else all at once has its `position` written. `Snap` has views draw it there without sliding (see [Jumps](../game/views.md#jumps)):

```csharp
event(Fell) Restart(mut Body body, Arena arena)
{
    body.position = arena.start;
    body.velocity = float2(0, 0);
    this.Snap();
}
```

A kinematic body goes where its `velocity` takes it, and carries what stands on it:

```csharp
component Lift { }

event(Spawned) Build(with Main)
{
    Spawn(Body { type = BodyType.Kinematic, position = float2(0, 2), velocity = float2(1, 0) },
          Box { size = float2(3, 0.4) }, Lift);
}

system Sweep(mut Body body, with Lift)
{
    if (body.position.x > 2) body.velocity = float2(-1, 0);
    if (body.position.x < -2) body.velocity = float2(1, 0);
}
```

Two things to know:

- **A new body's `impulse` is lost.** A body is made before its shapes are on it, when it weighs nothing, so an `impulse` or a `turn` in the `Spawn` itself does nothing. Give a new body a `velocity` instead, or its impulse a tick later.
- **A body at rest is asleep.** Box2D stops moving a body that has come to rest, until something touches it or pushes it. Writing its `velocity`, its `spin`, an `impulse` or a `turn` wakes it. Writing its `position` or its `angle` moves it and leaves it asleep, so a crate at rest that's put in the air hangs there until it's woken: give it a small `impulse` along with its new place.

## When things happen in a tick

The package's systems run before the game's, as every [package's](../tools/packages.md#writing-a-package) do, in this order:

1. Bodies and shapes that are new are made, and what code wrote into a `Body` goes to Box2D.
2. The world steps, by the tick's length, `Time.dt`.
3. Each body's `Body` gets where the step left it.
4. `Touched` and `Parted` are sent.

So a system of the game's reads this tick's positions, and what it writes goes into the next tick's step. The same goes for what an event's or a command's handler writes, at the end of the tick. An entity spawned in a tick has its body made at the start of the next, and is in that tick's step.

::: warning Leave the package's systems where they are
`tide schedule` lists them as `Box2D.Enter`, `Box2D.Step`, `Box2D.Leave` and the rest. Don't name one in a `[Before]` of a system of the game's, to get in ahead of the step: they keep their order among themselves only by where they're declared, and `[Before(Step)]` moves `Step` itself to after the systems that follow it.
:::

A tick is one step, so a match at the default 60 ticks a second steps a 60th of a second at a time, which is what Box2D is tuned for. A lower `tickRate` makes each step longer and the world less steady.

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

When the other entity was destroyed while they touched, `Parted` still comes, and its `other` is no entity: `parted.other == default`.

## The world

`Physics` is a singleton, the world's. A game sets two of its fields:

```csharp
singleton Physics
{
    float2 gravity = float2(0, -10);
    int substeps = 4; // The smaller steps Box2D solves each step in
}
```

More substeps make stacks and fast bodies steadier, and a step slower.

Any match code that takes it `mut` writes them, and the next step uses them:

```csharp
event(Spawned) Setup(mut Physics physics, with Main)
{
    physics.gravity = float2(0, -3);
}
```

A new gravity doesn't wake bodies that are [asleep](#moving-a-body): they stay where they are until something wakes them.

The other fields are the package's own. `moved` says whether the last tick stepped the world, and `bodies` and `shapes` how many were made so far; `stamp`, `size`, `image`, `gone` and `owners` are how the package keeps Box2D's state in the match.

## Destroying

An entity that's destroyed takes its body with it, at the next step: `this.Destroy()` is all there is to it. What it was touching gets `Parted`.

Removing `Body` from an entity that stays doesn't take its body out of the world yet. Destroy the entity.

## Multiplayer

There's nothing to add for a match with several machines. Box2D keeps its world in C's memory, which a rollback or a joining player wouldn't reach, so after every step the package writes Box2D's whole state into the match, as bytes in `Physics`. A client that predicted wrong and runs ticks again, a player who joins, a build that's reloaded and a machine that takes the match over all go on from that state.

That state is in the match's hash, and it's the same bytes on every machine: the same match gives the same result on desktop and on the web, so their players share one. See [Networking](../multiplayer/networking.md) and [Determinism](../multiplayer/determinism.md).

It's also why code never calls Box2D: everything that changes the physics world goes through the match, as components. The package itself talks to Box2D through [C functions](../c/functions.md), and reads the bodies back with [systems written in C](../c/functions.md#systems-written-in-c).

## What it costs

Measured on a release build, with 2,400 crates in stacks, on one thread, with the world kept and hashed every tick as a client's is:

| A tick, in milliseconds | Everything moving | One crate pushed | At rest |
|---|---|---|---|
| The tick | 0.61 | 0.21 | 0.02 |
| of it, Box2D's step | 0.31 | 0.01 | 0 |
| Keeping and hashing the world, after it | 0.26 | 0.04 | 0.00 |

- **A world at rest costs next to nothing.** It isn't stepped, and nothing of the match is written. What's left is looking at each body for what code wrote.
- **The step goes by what's moving.** With one crate pushed among 2,400 it takes a hundredth of a millisecond.
- **Keeping Box2D's state in the match goes by how big the world is**, while anything moves: it's about a kilobyte a body, 2.5 MB here, written and compared with the last one every tick, which is most of the 0.21 ms with one crate pushed. Only what changed of it is copied and hashed. A player who joins downloads it with the rest of the world.
- **A rollback** makes Box2D's world again from that state, once: about half a millisecond for this world, before the ticks it runs again.
- **The step runs on the tick's threads** once 2,048 bodies are awake, 4,096 on the web, and gives the same world on any number of them.
- **On the web** the step is slower, about 0.6 ms for this world with everything moving: Box2D has no SIMD there.
- **Building**: Box2D compiles in about 6 seconds, once, and adds about 350 KB to a game.

## Not there yet

- Joints, chains, polygons other than boxes, capsules, ray casts and overlap tests.
- A sensor reports nothing: no `Touched` or `Parted` is sent for a shape with `sensor`.
- A body's `type` and the fields read when it's made don't follow later changes, and a shape's fields don't either.
- Removing `Body` from an entity that stays leaves its body in the world.
- Views see a `Body` blended between ticks like any component, with nothing more: its fields have no [correction smoothing](../game/views.md#provisional-correction-smoothing), and `angle` blends as a plain number. For the frame in which a turning body passes half a turn, where its angle goes from π to -π, the blend is somewhere between the two and the body is drawn turned wrong.
