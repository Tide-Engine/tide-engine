# 3D physics (Box3D)

[Box3D](https://github.com/erincatto/box3d) is Erin Catto's 3D physics engine: rigid bodies that fall, collide, stack and come to rest. It comes with `tide` as a [package](../tools/packages.md#packages-that-come-with-tide). A body and its shapes are components of an entity, every tick steps them, and the match stays the same on every machine, through rollbacks, joins and hot reloads, with nothing for the game to do.

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

Its code is in the namespace `Box3D`, so a file that uses it says `using Box3D;`. From then on `Body`, `Box`, `Sphere`, `Capsule` and `Physics` name the package's. A name the game declares itself still means the game's: one with a `Body` of its own writes the package's in full, `Box3D.Body`.

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

The [Sumo walkthrough](../guide/sumo.md) shows how impulses drive the balls and blasts, how static floor tiles become falling bodies, and how sensors detect ring-outs and pickups. It also covers deterministic arena construction, the room lobby, instanced drawing and the cost of rollback.

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

`type` and the fields from `gravityScale` down say how the body is to be. They're set where the entity is spawned, and [can be written later](#changing-how-a-body-is).

The rest of the component is the package's own, and not for a game to write: `id` is Box3D's handle to the body, 0 until its first step; `stepPosition`, `stepRotation`, `stepVelocity` and `stepAngularVelocity` are where the last step left it, which is how the package tells what code wrote since; and `stepType`, `stepGravityScale`, `stepLinearDamping`, `stepAngularDamping` and `stepBullet` are what Box3D has of how the body is to be.

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

A shape that has to change is a part too: destroy it, and spawn another.

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

Box3D stops moving a body that has come to rest: it's asleep, and costs nothing, until something touches it or pushes it. Whatever code writes into a sleeping body wakes it:

- A `velocity` or an `angularVelocity` that isn't zero, an `impulse` or an `angularImpulse`.
- A `position` or a `rotation`: the body is moved and wakes, so a crate at rest that's put in the air falls. What was touching it wakes too: a platform that's moved away, a static one as well, drops what stood on it.
- Its `type`, `gravityScale`, `linearDamping`, `angularDamping` or `bullet`: a ball asleep in the air with no gravity on it falls once it has some.

A world where everything is asleep and nothing was written isn't stepped at all (see [What it costs](#what-it-costs)).

## When things happen in a tick

The package's systems run before the game's, as every [package's](../tools/packages.md#writing-a-package) do, in this order:

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
    body.velocity.y = Inside(physics, this, out _) ? 1 : 0;
}
```

`Inside(physics, sensor, out found)` gives the entities whose bodies have a shape in an entity's sensors, each once, and whether there's any. `sensor` is the entity the sensor shape is a component of: a body's, or a [part's](#parts). It's one of the things [the world is asked](#asking-the-world).

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

Functions that ask what's in the world, and change nothing. Each takes the `Physics` singleton, says whether it found anything, and gives what it found through [`out`](../language/functions.md):

```csharp
component Walker
{
    bool grounded;
    Entity on;
}

// What's under the feet: a ray a little way down from the body's place
system Land(Physics physics, Body body, mut Walker walker)
{
    walker.grounded = Raycast(physics, body.position, body.position - float3(0, 1.1, 0), out var under);
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

- **`Raycast(physics, from, to, out hit)`**: the first thing on the way from one point to another.
- **`Cast(physics, shape, from, rotation, to, out hit)`**: the first thing a shape meets that's moved from one place to another, turned by `rotation` all the way. The shape is a `Box`, a `Sphere` or a `Capsule`, as one on a body is: `Cast(physics, Sphere { radius = 0.5 }, from, quaternion.identity, to, out var hit)`. `hit.point` is where the two touch.
- **`Overlap(physics, shape, at, rotation, out found)`**: the entities whose bodies a shape at a place overlaps or touches, each once.
- **`Inside(physics, sensor, out found)`**: what's [in a sensor](#sensors).

```csharp
// Everything within three meters of a blast is pushed away from it.
event(Burst burst) Blast(Physics physics, mut World world)
{
    if (!Overlap(physics, Sphere { radius = 3 }, burst.at, quaternion.identity, out var near)) return;
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
    var from = Draw3D.ScreenRayOrigin(screen);
    if (Raycast(physics, from, from + Draw3D.ScreenRayDirection(screen) * 100, out var hit))
        Draw3D.Sphere(hit.point, 0.05, Color.white);
}
```

A few rules hold for all of them:

- **They see the world as the last step left it.** A system after the step is asked of this tick's step, and one that's [before it](#before-the-step) of the last tick's. What code wrote into a body since isn't there yet, nor a body or a shape that was spawned since: each is in the world from the next step. An entity that was destroyed since is found no more, at once.
- **A sensor is in nothing's way.** A ray and a cast go through sensors, and `Overlap` doesn't list them. What's in one is `Inside`'s to say.
- **A ray says nothing of a shape it starts inside**, and a cast meets what its shape is in at `from` right there: a `fraction` of 0, and no `normal`. A few millimeters count as touching, so a shape cast from where a body rests is in what the body rests on: start a cast a little off it.
- **The answer is the same on every machine**, as everything in the match is. Of two things as near, the one whose shape was made first is the one that's met, and lists come in the order of their bodies' `id`.
- **Everything solid is found**: there's no filter yet, and no way to leave the asking body's own shapes out. A ray from inside a body's own shape says nothing of it, which is what a ray down from a body's middle relies on.

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

The other fields are the package's own. `moved` says whether the last tick stepped the world, and `bodies` and `shapes` how many were made so far; `stamp`, `size`, `image`, `geometry`, `gone`, `owners`, `goneShapes`, `parts` and `sensors` are how the package keeps Box3D's state in the match, and whose each body and shape is.

## Destroying

An entity that's destroyed takes its body with it, at the next step: `this.Destroy()` is all there is to it. What it was touching gets `Parted`. Its [parts](#parts) are destroyed with it, and a part that's destroyed by itself takes only its own shapes.

Removing `Body` from an entity that stays doesn't take its body out of the world yet, and removing a shape's component doesn't take the shape off. Destroy the entity: a shape that has to go by itself is a part.

## Multiplayer

There's nothing to add for a match with several machines. Box3D keeps its world in C's memory, which a rollback or a joining player wouldn't reach, so after every step the package writes Box3D's whole state into the match, as bytes in `Physics`. A client that predicted wrong and runs ticks again, a player who joins, a build that's reloaded and a machine that takes the match over all go on from that state.

That state is in the match's hash, and it's the same bytes on every machine: the same match gives the same result on desktop and on the web, so their players share one. See [Networking](../multiplayer/networking.md) and [Determinism](../multiplayer/determinism.md).

It's also why code never calls Box3D: everything that changes the physics world goes through the match, as components. The package itself talks to Box3D through [C functions](../c/functions.md), and reads the bodies back with [systems written in C](../c/functions.md#systems-written-in-c).

## What it costs

Measured on a release build, with 2,400 crates in stacks, on one thread, with the world kept and hashed every tick as a client's is:

| A tick, in milliseconds | Everything moving | One crate pushed | At rest |
|---|---|---|---|
| The tick | 1.42 | 0.09 | 0.01 |
| of it, Box3D's step | 1.0 | 0.03 | 0 |
| Keeping and hashing the world, after it | 0.35 | 0.08 | 0.00 |
| Box3D alone, the same scene in plain C: a step | 1.0 | 0.015 | 0.0002 |

- **A world at rest costs next to nothing.** It isn't stepped, and nothing of the match is written. What's left is looking at each body for what code wrote.
- **A tick goes by what's moving.** With one crate pushed among 2,400 it takes a tenth of a millisecond: the step is a third of that, and the rest is the package's.
- **What the package adds to Box3D alone** is 0.42 ms a tick with everything moving, 0.08 with one crate pushed, and 0.01 at rest. Most of it is keeping Box3D's state in the match: about a kilobyte and a half a body, 3.5 MB here. With most of the world awake all of it is written each tick, and only what changed is copied and hashed. With most of it asleep, only what's awake is written. A player who joins downloads it with the rest of the world.
- **A rollback** makes Box3D's world again from that state, once: about a millisecond for this world, before the ticks it runs again.
- **A body's `type` that changes** has all of that state written again in that tick, where a tick usually writes what's awake: a quarter of a millisecond for this world. The other fields of a body cost nothing more than writing them.
- **Asking the world** takes about a microsecond a question in a small world. A view that asks of a world Box3D isn't at (a `verified Physics`, while this machine runs ahead of what's confirmed) has it made from the match's state first, as a rollback does.
- **The step runs on the tick's threads** once 512 bodies are awake, and gives the same world on any number of them: 1.3 ms on one thread and 0.5 on twelve, with every crate awake.
- **On the web** the same step takes about 1.4 ms on one thread. Box3D solves four contacts at a time there, on WebAssembly's SIMD, where a desktop with AVX2 solves eight, and gives the same world to the bit.
- **Building**: Box3D compiles in about 8 seconds, once, and adds about 800 KB to a game.

## Not there yet

- Hulls other than boxes, meshes, height fields, compounds and joints.
- A parting from an entity that was destroyed doesn't say which entity.
- A shape's fields don't follow later changes: its size, its `density`, `friction` and `bounce`, and whether it's a sensor. A [part](#parts) that's destroyed and spawned again is how a shape changes.
- Removing `Body` from an entity that stays leaves its body in the world, and removing a shape's component leaves the shape on it.
- What's asked of the world takes no filter, and finds the nearest thing only, not everything on the way.
- Whether one body may sleep, and locks on the axes a body moves and turns on.
- Box3D keeps what a box of each size is made of, in `Physics`, and never lets one go: a game that makes boxes of ever new sizes keeps them all.
