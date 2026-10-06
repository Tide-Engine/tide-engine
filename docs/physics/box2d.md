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

Its code is in the namespace `Box2D`, so a file that uses it says `using Box2D;`. From then on `Body`, `Box`, `Circle` and `Physics` name the package's. A name the game declares itself still means the game's: one with a `Body` of its own writes the package's in full, `Box2D.Body`.

A game that doesn't list the package compiles and ships none of it. One that does ships Box2D, which is under the MIT license, so it ships Box2D's notice with it: the `LICENSE` file in the package's `box2d` folder, `src/packages/box2d/box2d/LICENSE` of the folder `tide version` says `tide` is installed in.

## A first world

A floor, two walls and three stacks of crates. Tap a crate to kick it, and anywhere else to drop a ball.

::: tide-example physics-2d Crates, balls and a floor

<<< @/examples/physics-2d.tide

:::

- `Build` makes the world out of entities: each has a `Body`, and a `Box` or a `Circle` for its shape. The floor and the walls are `Static`, and the crates are dynamic, as a body is unless it says otherwise.
- Nothing in the game moves a crate. The package's systems step the world every tick, and write where each body is into its `Body`.
- The views draw from the same components: `body.position` and `body.angle`, with the size its `Box` or `Circle` says. A ball has a spoke, so that its turning shows.
- A tap is a [command](../game/commands.md). `DropBall` spawns a body where the tap was. `KickCrate` writes the crate's `impulse`, and the next step pushes it.
- `Expire` destroys a ball after ten seconds, and its body goes with it.
- `Count` hears `Touched`, which the package sends to a body's entity when it starts touching another.

[Towers](../guide/towers.md) is a whole game made with the package, for up to four players in a match: bricks that are steered as kinematic bodies of sensors and land as dynamic ones, plinths that shake and sink, impulses, and most of what this page describes.

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

[Views](../game/views.md) see a body between its last two ticks, as they see any component. The package says what two of the fields are, for that: `position` is a `[Position]` and `angle` a `[Rotation]` (see [correction smoothing](../game/views.md#provisional-correction-smoothing)). So a view sees a body turn the short way through half a turn, where its angle goes from π to -π, and what it reads there can be a little past π. And when the server corrects where this machine had predicted a body, a view sees it get there over a few frames, where it would jump. Neither changes anything of the match.

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
    bool sensor;                // Bodies pass through it, and it says which
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

A shape with `sensor` touches nothing and reports what's in it: see [Sensors](#sensors).

## Moving a body

Code moves a body by writing its `Body`, and never by calling Box2D. After each step the package writes where every body is into its component, and before the next one it looks at each for what code changed since, and tells Box2D.

A push is an `impulse`. It's given once, at the next step, and the field is zero again after it:

```csharp
system Jump(PlayerInput input, mut Body body, with Player)
{
    if (input.jump.down) body.impulse = float2(0, 5);
}
```

An entity can be spawned with one, which pushes it at its first step, as soon as its shapes are on it:

```csharp
Spawn(Body { position = muzzle, impulse = aim * 3 }, Circle { radius = 0.1 }, Shot);
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

### Bodies at rest

Box2D stops moving a body that has come to rest: it's asleep, and costs nothing, until something touches it or pushes it. Whatever code writes into a sleeping body wakes it:

- A `velocity` or a `spin` that isn't zero, an `impulse` or a `turn`.
- A `position` or an `angle`: the body is moved and wakes, so a crate at rest that's put in the air falls. What was touching it wakes too: a platform that's moved away, a static one as well, drops what stood on it.

A world where everything is asleep and nothing was written isn't stepped at all (see [What it costs](#what-it-costs)).

## When things happen in a tick

The package's systems run before the game's, as every [package's](../tools/packages.md#writing-a-package) do, in this order:

1. Bodies and shapes that are new are made, and what code wrote into a `Body` goes to Box2D.
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
}
```

What `Walk` writes is the speed this tick steps with, a tick sooner than without the attribute. What it reads of a body is where the last tick's step left it.

That's the one way to get in ahead of the step, and a few things follow from how [systems are ordered](../game/systems.md#order):

- **A system that doesn't say runs after the step**, in a game where none says `Before`. Once one does, the systems written above it in the game's files run before the step too, as the package now waits for a system that comes after them. `[After(PhysicsStages.Step)]` on a system holds it after the step whatever the others say: write it on one that has to read where this tick's step left things.
- **`[Before(Step)]` is an error**, as no system has that name: the phase is what to name.
- **The package's own systems aren't for a game to name.** `tide schedule` lists them, `Box2D.Enter`, `Box2D.Simulate`, `Box2D.Leave` and the rest, and they keep their order whatever a game orders its systems by. But a system that's put among them, with `[Before(Box2D.Simulate)]` say, runs after what code wrote has gone to Box2D and before the bodies come back: where it writes a body's place or its speed, the step's own is written over it.

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

They're sent once for each pair of shapes that touch, so a body whose `Box` and `Circle` are both on the floor hears `Touched` twice.

When the other entity was destroyed while they touched, `Parted` still comes, and its `other` is no entity: `parted.other == default`. Which entity it was isn't said.

### Sensors

A shape with `sensor = true` stops nothing: bodies pass through it. It sends the same two events, `Touched` when a body's shape comes into it and `Parted` when it leaves, to its own entity and to the other's:

```csharp
component Goal { int score; }

event(Spawned) Build(with Main)
{
    Spawn(Body { type = BodyType.Static, position = float2(8, 1) }, Box { size = float2(1, 2), sensor = true }, Goal);
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

A new gravity wakes every body that's [asleep](#bodies-at-rest), so that what was at rest feels it. A game that changes it every tick has nothing at rest.

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
- `Touched` and `Parted` don't say which shape of an entity's it was, and a parting from an entity that was destroyed doesn't say which entity.
- A body's `type` and the fields read when it's made don't follow later changes, and a shape's fields don't either.
- Removing `Body` from an entity that stays leaves its body in the world.
