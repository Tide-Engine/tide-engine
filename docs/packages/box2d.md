---
description: "Use Tide's Box2D package for deterministic 2D rigid bodies, collisions, queries and multiplayer physics that survives rollback and joins."
---

# Box2D: 2D physics

[Box2D](https://box2d.org) is Erin Catto's 2D physics engine: rigid bodies that fall, collide, stack and come to rest. It comes with `tide` as a [package](../packages/using-and-writing.md#packages-that-come-with-tide). A body and its shapes are components of an entity, every tick steps them, and the match stays the same on every machine, through rollbacks, joins and hot reloads, with nothing for the game to do.

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

The [Towers source](https://github.com/Tide-Engine/tide-engine/tree/dev/examples/towers) shows how the package steers a falling brick as a kinematic body, turns it into a dynamic body when it lands, and casts shapes to find space for movement. It also covers absolute input, the room lobby and the views that present the physics match.

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
    bool rewind;                      // Opt this body and its parts into collider history
    bool asleep;                      // At rest, and not stepped until something wakes it
    float gravityScale = 1;           // 0 for a body that floats
    float linearDamping;              // How fast it slows by itself
    float angularDamping;             // And how fast its spin does
    float sleepSpeed = 0.05;          // Slower than this for a while, it falls asleep
    bool fixedRotation;               // It never turns
    bool bullet;                      // For a small, fast body
}
```

- A **static** body never moves: a floor, a wall. Its weight doesn't matter.
- A **kinematic** body moves by its `velocity` whatever is in its way, and nothing pushes it: a platform, a door.
- A **dynamic** body is moved by gravity, by what it touches and by what code pushes it with.
- A **bullet** is checked against the other moving bodies all along its way, so that a small fast body doesn't pass through a thin one between two steps. It costs more, so it's for the few bodies that need it.

After each step, `position`, `angle`, `velocity` and `spin` are where Box2D left the body, with `angle` between -π and π, and `asleep` says whether it's [at rest](#bodies-at-rest). Code reads them like any component's fields, and [writing them moves the body](#moving-a-body), or puts it to sleep, or wakes it.

[Views](../game/views.md) see a body between its last two ticks, as they see any component. The package says what two of the fields are, for that: `position` is a `[Position]` and `angle` a `[Rotation]` (see [correction smoothing](../game/views.md#provisional-correction-smoothing)). So a view sees a body turn the short way through half a turn, where its angle goes from π to -π, and what it reads there can be a little past π. And when the server corrects where this machine had predicted a body, a view sees it get there over a few frames, where it would jump. Neither changes anything of the match.

`type` can be written at any time: see [Changing what a body is](#changing-what-a-body-is). Gravity scale, dampings, sleep speed, fixed rotation and bullet mode also follow later writes, waking the body when their native value changes.

The rest of the component is the package's own, and not for a game to write: `id` is Box2D's handle to the body, 0 until its first step, and `stepPosition`, `stepAngle`, `stepVelocity`, `stepSpin` and `stepAsleep` are how the last step left it, which is how the package tells what code wrote since.

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

A shape attaches at the next physics step. Later geometry, material and sensor edits rebuild its fixture at the next step too.

A shape with `sensor` touches nothing and reports what's in it: see [Sensors](#sensors).

### More shapes on a body

A body of more shapes than one box and one circle has the others as entities of their own. Each has its `Box` or its `Circle`, and a `Part`, which names the entity whose body it's on:

```csharp
// An L: a bar of three cells, and a fourth on its end
var brick = Spawn(Body { position = float2(0, 4) }, Box { size = float2(3, 1) }, Brick);
Spawn(Box { offset = float2(1, 1) }, Part { of = brick });
```

- **A part's shape is on the body from the next step**, like any shape, at its `offset` from the body's place. The body weighs what all its shapes weigh, and turns around their middle.
- **Destroying a part's entity takes that one shape off.** The body is lighter from the next step, and what was resting on the shape falls.
- **Destroying the body's entity takes its parts with it.** Their entities go the tick after.
- **A part can be a sensor,** on a body whose other shapes are solid: a zone beside a platform, a trigger on a door.
- **A part names an entity with a `Body`.** One that names none, or an entity that has no body, is destroyed in the tick that finds it so.
- The events say which shape met which: see [Touching](#touching).

A part's entity has no `Body` of its own. An entity that has both a `Body` and a `Part` is a body, with its shapes on itself.

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

### Changing what a body is

A body's `type` is written like its place, and the next step has it as the new kind, with the shapes it had: a door that's locked, a crate that's frozen where it stands, a piece that's carried and then let go.

```csharp
component Frozen { }

// Frozen where it is, and let go again: the same body all along
event(Froze) Freeze(mut Body body)
{
    body.type = BodyType.Static;
    this.Add(Frozen);
}

event(Thawed) Thaw(mut Body body)
{
    body.type = BodyType.Dynamic;
    this.Remove(Frozen);
}
```

- **To static:** it stops where it is, and its `velocity` and `spin` read zero from the next step.
- **To kinematic:** it goes on at the `velocity` it has, whatever is in its way: write the speed it should have with the type.
- **To dynamic:** gravity and whatever it touches move it from the next step, with the weight its shapes give it.
- **What touched it is let go and found again** at that step, so `Parted` and then `Touched` come for each. What rested on it is awake, and stays where it was when the body under it only turned static.

### Bodies at rest

Box2D stops moving a body that has come to rest: it's asleep, and costs nothing, until something touches it or code wakes it. `asleep` says which it is, after each step, and code that writes it puts the body to sleep or wakes it, at the next step:

```csharp
component Boulder { }

// The boulder hangs over the path, asleep from the tick it's spawned
event(Spawned) Build(with Main)
{
    Spawn(Body { position = float2(4, 9), asleep = true }, Circle { radius = 1 }, Boulder);
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

- **A body that's put to sleep stops where it is**, in the air too: its `velocity` and its `spin` are zero from then on, and none of its speed comes back when it wakes.
- **It takes what it touches with it.** Box2D sleeps and wakes bodies an *island* at a time: the bodies that touch each other, and the ones those touch, as the last step found them. So the bottom crate of a stack that's put to sleep takes the stack with it, and a crate that's sliding along a plank stops where it is when the plank is put to sleep. A kinematic body is one of them: a lift goes to sleep with what stands on it. A static body isn't: two crates on the same floor are two islands.
- **What hasn't touched it yet stays awake.** A crate that's still on its way down to a sleeping stack goes on falling, and wakes the stack when it lands.
- **Waking is the same**: `asleep = false` wakes the body and all of what it fell asleep with.
- **A static body is never asleep.** Its `asleep` reads false, whatever is written into it, and so does a sleeping body's once it's made `Static`.
- **It works where bodies don't fall asleep by themselves** ([`sleeping`](#the-world) off): one that code puts to sleep sleeps all the same, until something wakes it.

A sleeping body wakes by itself, with its island, when something awake touches it. And whatever else code writes into it wakes it too:

- A `velocity` or a `spin` that isn't zero, an `impulse` or a `turn`.
- A `position` or an `angle`: the body is moved and wakes, so a crate at rest that's put in the air falls. What was touching it wakes too: a platform that's moved away, a static one as well, drops what stood on it.
- A `type`: the body wakes, and what was touching it.

So does a body beside it that's destroyed, and [a new gravity](#the-world). After the step, `asleep` reads false.

How readily a body falls asleep by itself is its `sleepSpeed`: once it has been slower than that for half a second, with everything it touches, it sleeps. 0.05 meters a second is Box2D's own, and a body that should rest sooner, like the bricks of a tall stack, says more. `Physics.sleeping` turns that off for the whole world (see [The world](#the-world)).

**What code puts to sleep in a tick is asleep as that tick's step starts**, whatever else was written that tick, into it or into a body it touches. A push or a speed written with it is lost. A `position` written with it is where it sleeps: a body that's put somewhere else and to sleep at once hangs there. A new gravity in that tick doesn't wake it. What does is another body running into it in that very step.

That's for a body that was awake. Writing `asleep = true` into one that's asleep already writes nothing, so a sleeping body that code moves wakes like any other: `asleep` is false after that tick, and code that wants it asleep where it put it says so then.

A body that's **spawned asleep** starts where it's spawned, with none of the speed or the push it was spawned with. A gravity change or automatic sleeping being turned off in its creation tick does not override that sleep. Bodies spawned asleep on each other are each asleep by themselves, as no step has seen them touch: one that's hit wakes, and the ones resting on it a step after each other. That's fine for a few. For a great many stacked on each other it's better to spawn them awake and put them to sleep a tick later, once they have had a step: they then sleep and wake a stack at a time.

A world where everything is asleep and nothing was written isn't stepped at all (see [What it costs](#what-it-costs)), and a body that's put to sleep costs what one that fell asleep does: nothing.

## When things happen in a tick

The package's systems run before the game's, as every [package's](../packages/using-and-writing.md#writing-a-package) do, in this order:

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

When two bodies start touching, the package sends `Touched` to each one's entity, and `Parted` when they stop. Each has the `other` entity, and which shapes met:

```csharp
event Touched
{
    Entity other;     // Whose body it met
    Entity part;      // Which shape of this body's
    Entity otherPart; // ...and which of the other's
}
```

`part` and `otherPart` are entities too: the body's own entity for a shape that's on it, and the part's entity for a [part](#more-shapes-on-a-body). `Parted` has the same three.

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

They're sent once for each pair of shapes that touch, so a body whose `Box` and `Circle` are both on the floor hears `Touched` twice, each time with its own entity for `part`: which of an entity's own two shapes it was isn't said. A part says, as it's an entity.

When the other entity was destroyed while they touched, `Parted` still comes, and its `other` is no entity: `parted.other == default`. Which entity it was isn't said. When only a part was destroyed, its body is still there to say: `Parted` comes to both bodies with the right `other`, and `default` for the part that went.

### Hits

`Touched` says that two bodies met. `Hit` says how hard: it's sent to each body's entity when they meet faster than `Physics.hitSpeed`, which is a meter a second unless the game says otherwise.

```csharp
event Hit
{
    Entity other;
    Entity part;      // Which shapes met, as in Touched
    Entity otherPart;
    float2 point;     // Where
    float2 normal;    // The way from this body into the other
    float speed;      // How fast they closed, in meters a second
}

// Dust where a crate lands hard, on each machine
local event(Hit hit) Thud(mut Dust dust, with Crate)
{
    Puff(dust, hit.point, hit.speed);
}
```

- It comes in the tick whose step they met in, once for each pair of shapes, to both entities: each with the normal pointing away from itself.
- A crate that settles onto another sends none, nor does anything that goes into a sensor.
- It's for what shows and sounds of a collision, so its handler is often a [local](../game/events.md#local-handlers) one. An event that nothing in the match handles [costs the match nothing](../game/events.md#events-only-local-code-handles).

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

`Touched` and `Parted` say who comes and who goes. `Inside` says who's there: the entities whose bodies have a shape in an entity's sensors, as the last step found them.

```csharp
// How many are standing on the plate
system Weigh(mut Plate plate, Physics physics)
{
    physics.Inside(this, out var found);
    plate.load = found.count;
}
```

- `physics.Inside(sensor, out found)` takes the entity the sensor shape is a component of: a body's, or a [part's](#more-shapes-on-a-body). An entity with no sensor has nobody in it.
- It returns whether anybody is there. `found` has each entity once, a static body's too, in an order that's the same on every machine.
- It's one of the functions that ask the world something: see [Asking the world](#asking-the-world) for what they see and when.

## Asking the world

A ray, a shape that's moved along a way, a shape that's looked into: methods on the `Physics` singleton that ask the world something and change nothing of it. Each returns whether it found anything, and gives what it found through [`out`](../language/functions.md#out-parameters).

```csharp
struct RayHit
{
    Entity entity;   // Whose body was met
    Entity part;     // ...and which shape: that entity, or a part's
    float2 point;    // Where
    float2 normal;   // The way out of what was met, there
    float fraction;  // How far along the way: 0 at its start, 1 at its end
}
```

**`Raycast`** is the first thing a ray meets on its way from one point to another:

```csharp
// A shot that's a ray: what's in its way this tick
system Fly(mut Shot shot, Physics physics, Time time)
{
    var to = shot.at + shot.velocity * time.dt;
    if (physics.Raycast(shot.at, to, out var hit))
    {
        hit.entity.Send(Struck { at = hit.point });
        this.Destroy();
        return;
    }
    shot.at = to;
}
```

**`Cast`** moves a shape along a way, and says where it first touches something: `fraction` is how much of the way it went before it did. The shape is a `Box`, `Circle`, `Capsule`, `Polygon` or `Segment` as it would be on a body at `from`, turned by `angle`: its `size` or its `radius`, around its `offset`. So a body's own box, cast from the body's `position` at its `angle`, is the box where it is.

```csharp
// Whether there's ground just under the player
system Stand(Body body, mut Player player, Physics physics)
{
    var feet = Circle { radius = 0.3 };
    var under = body.position + float2(0, -0.4);
    player.grounded = physics.Cast(feet, body.position, 0, under, out _, ignore: body);
}
```

**`Overlap`** is the entities whose bodies have a shape in a shape at a place, or touching it, each once:

```csharp
// Everything within three meters is thrown outwards
event(Exploded) Blast(Body body, Physics physics)
{
    physics.Overlap(Circle { radius = 3 }, body.position, 0, out var found);
    foreach (var caught in found) caught.Send(Thrown { from = body.position });
}
```

- **One body can be left out.** `Raycast` and `Cast` take a last argument, the `Body` they don't meet: `physics.Raycast(body.position, target, out var hit, ignore: body)`. A ray from a body's own middle meets that body at once otherwise.
- **Sensors aren't met by default**, nor found by `Overlap`: they stop nothing. The all-hit methods below can include them. [`Inside`](#sensors) asks what's in a sensor.
- **A ray or a shape that starts in something meets it there**, at `fraction` 0, with no `normal` or unique surface `point`. A shape that only touches something where it starts is in it, for this: a crate at rest touches the floor, so its own box cast along the floor meets the floor at once. Cast a slightly smaller one to slide it along what it rests on.
- **Of two things as near as each other, the one that was made first is the answer**, and `Overlap` and `Inside` give their entities in the order their bodies were made in: the same on every machine, whatever order Box2D comes to them in.

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

physics.CastAll(Circle { radius = 0.2 }, from, 0, to, out var swept,
    ignore: body, includeSensors: true);
```

`CastAll` accepts the same convex shapes, `from`, `angle` and `to` as `Cast`. Both all-hit methods accept `ignore: body`, which excludes every shape on that body, including its parts. `includeSensors` defaults to false and can be used with or without an ignored body.

Each collider contributes its first hit. Separate colliders on one body remain separate results, even if their `entity` and `part` are the same; these are not unique-entity lists or entry-and-exit intersections. The ordinary `RayHit` values can be saved in match or local state. The same methods and options work on a rewound `Query`.

Use `Raycast` or `Cast` when only the nearest hit matters: they keep their early-pruning path. There are no query layers or masks; all-hit results let game code apply its own component, team or other rules afterward.

### What they see

A question is asked of the world as its last step left it:

- **A system that's [before the step](#before-the-step)** sees where the last tick's step left things. That's what the system reads in `body.position` too, so the two agree.
- **A system after the step**, and a handler, see where this tick's step left things.
- **What code wrote since isn't in the answer:** a body whose `position` was written this tick is still where the step left it, to a ray, until the next step. Neither is a body that was spawned since.
- **An entity that was destroyed isn't met any more**, from the moment it's gone.
- **In a world nothing has stepped yet**, the first tick's, there's nothing to find.

A [view](../game/views.md) asks too, with the `Physics` it takes: where the mouse points, where a piece would land. It's asked of the world the view draws, as the last tick left it, not between two ticks as the view sees bodies' places.

```csharp
// What's under the pointer
view Hover(Devices devices, Physics physics, mut Pointed pointed)
{
    var at = Draw.ScreenToWorld(Screen.Flip(devices.pointer.position));
    pointed.over = default;
    if (physics.Overlap(Circle { radius = 0.1 }, at, 0, out var found)) pointed.over = found[0];
}
```

The answers are the same on every machine, like everything of the match: match code can act on them. They cost what Box2D's own queries do, about half a microsecond each in a small world. The one cost to know about: where this machine has no Box2D world at the match's state, the first question makes it from the state the match keeps, as a rolled-back tick does (see [What it costs](#what-it-costs)).

A system that asks runs on the tick's threads like any other.

## The world

`Physics` is a singleton, the world's. A game sets these fields of it:

```csharp
singleton Physics
{
    float2 gravity = float2(0, -10);
    int substeps = 4;            // The smaller steps Box2D solves each step in
    float contactStiffness = 30; // How stiff contacts are, in cycles a second
    float contactDamping = 10;   // ...and how little they spring back
    float contactSpeed = 3;      // The fastest bodies that overlap are pushed apart, in meters a second
    float bounceSpeed = 1;       // Shapes with `bounce` only bounce when they meet faster than this
    float hitSpeed = 1;          // Bodies that meet faster than this send Hit
    bool sleeping = true;        // Bodies at rest fall asleep
}
```

Each is what Box2D comes with, so a game that sets none gets the same world as before there were any.

- **`substeps`**: more make stacks and fast bodies steadier, and a step slower.
- **`contactStiffness`** is how hard two bodies that touch push back as they sink into each other. Stiffer contacts hold a tall stack: a tower of thirty crates that sheds half of them when a heavy crate lands on it stands whole at twice the stiffness. It's no more than an eighth of the substeps a second, which at four substeps and 60 ticks a second is 30, the default: **to go stiffer, raise `substeps` with it**, 8 for 60.
- **`contactDamping`** is how gently they come back out. Under 1 they spring; the higher, the less a pile that's been squeezed pops.
- **`contactSpeed`** is the fastest two bodies that are in each other are moved apart: lower, and something that's spawned inside something else eases out of it.
- **`bounceSpeed`** and **`hitSpeed`** are the speeds two bodies have to meet at for a `bounce` to count and for [`Hit`](#hits) to be sent.
- **`sleeping`**, off, keeps bodies from falling asleep by themselves. Code can still put a body to sleep with `asleep`, and a world with every body asleep waits until something wakes it.

Any match code that takes it `mut` writes them, and the next step uses them:

```csharp
event(Spawned) Setup(mut Physics physics, with Main)
{
    physics.gravity = float2(0, -3);
}
```

A new gravity wakes every body that's [asleep](#bodies-at-rest), so that what was at rest feels it. A game that changes it every tick has nothing at rest. A change to any of the others is the next step's too, and that tick steps even when everything is asleep.

The other fields are the package's own. `moved` says whether the last tick stepped the world, and `bodies` and `shapes` how many were made so far; `stamp`, `size`, `image`, `gone`, `shed`, `dropped`, `owners`, `parts` and `sensors` are how the package keeps Box2D's state in the match.

## Destroying

An entity that's destroyed takes its body with it, at the next step: `this.Destroy()` is all there is to it. What it was touching gets `Parted`. Its [parts](#more-shapes-on-a-body) go with it, and a part that's destroyed by itself takes its one shape off the body.

Removing `Body` from an entity that stays doesn't take its body out of the world yet. Destroy the entity.

## In Unity

In a Unity project ([Unity](../tools/cli.md#unity)), Unity's own 2D physics components on a TideEntity GameObject are Box2D's: tide translates them as it reads the scene or prefab. A `Rigidbody2D` is a `Body` (its body type, gravity scale and dampings), a `BoxCollider2D` a `Box` and a `CircleCollider2D` a `Circle`, sized by the GameObject's scale as Unity sizes them, with their density and `isTrigger` as `sensor`. A collider with no `Rigidbody2D` is on a static body, as in Unity. A body starts where the GameObject is in x and y, turned as it's turned about z, and Unity shows it where Box2D has it.

Only what's one to one is translated, and what isn't is left out with a warning that says why, such as a collider under another GameObject's `Rigidbody2D` (a part in Box2D). A Box2D component of the GameObject's own always wins over the translation of a Unity one.

## Multiplayer

There's nothing to add for a match with several machines. Box2D keeps its world in C's memory, which a rollback or a joining player wouldn't reach, so after every step the package has Box2D's whole state in the match, as bytes in `Physics`: it writes what changed of them. A client that predicted wrong and runs ticks again, a player who joins, a build that's reloaded and a machine that takes the match over all go on from that state.

That state is in the match's hash, and it's the same bytes on every machine: the same match gives the same result on desktop and on the web, so their players share one. See [Networking](../multiplayer/networking.md) and [Determinism](../multiplayer/determinism.md).

It's also why code never calls Box2D: everything that changes the physics world goes through the match, as components. [Asking](#asking-the-world) changes nothing, so it's a function. The package itself talks to Box2D through [C functions](../c/functions.md), and reads the bodies back with [systems written in C](../c/functions.md#systems-written-in-c).

### A sharded world

In a [sharded world](../multiplayer/shards.md#physics), each shard has its own `Physics` and steps its own Box2D world of the bodies it owns. `Body.position` decides which shard has a body. A body that crosses a border is made anew on the next shard from its `Body`, where it was and as fast, and its shapes are put back on it there. Bodies another shard owns near the border are kinematic copies here, static for a static one, with their own box or circle, where their owner last had them. This shard's bodies run into them, and each side tells its own entities of a touch or a hit. Set `Physics`' settings from a system for no entity that takes `Physics`, which runs on every shard. A joint holds its bodies only while one shard owns both. A body's parts and other shapes aren't copied across a border yet.

## What it costs

Measured on a release build, with 2,400 crates in stacks, on one thread, with the world kept and hashed every tick as a client's is:

| A tick, in milliseconds | Everything moving | One crate pushed | At rest |
|---|---|---|---|
| The tick | 0.51 | 0.06 | 0.02 |
| of it, Box2D's step | 0.30 | 0.01 | 0 |
| Keeping and hashing the world, after it | 0.26 | 0.04 | 0.00 |

- **A world at rest costs next to nothing.** It isn't stepped, and nothing of the match is written. What's left is looking at each body for what code wrote.
- **The step goes by what's moving.** With one crate pushed among 2,400 it takes a hundredth of a millisecond.
- **Keeping Box2D's state in the match goes by what's moving too**, while no more than half of the world is awake. It's about a kilobyte a body, 2.5 MB here, and Box2D says which bodies, shapes and contacts may have changed since the last tick: only those are written, which with one crate pushed takes 0.01 ms of a tick's 0.06.
- **With more than half of the world awake, all of that state is written** and compared with the last tick's, which goes by how big the world is: 0.06 ms of the 0.51 here, before what changed of it is put into the match. A stack that wakes or falls asleep can have one tick's written whole too.
- **Only what changed of it is copied and hashed**, either way, and a player who joins downloads it with the rest of the world.
- **A rollback** makes Box2D's world again from that state, once: about half a millisecond for this world, before the ticks it runs again. So does the first [question](#asking-the-world) a view or a system asks when this machine has no Box2D world at the match's state, which is the tick after a rollback, or a view of a world other than the last one that ticked. In a world of a few hundred bodies that's a few hundredths of a millisecond.
- **The step runs on the tick's threads** once 2,048 bodies are awake, 4,096 on the web, and gives the same world on any number of them.
- **On the web** the step takes about 0.46 ms for this world with everything moving. Box2D solves four contacts at a time there, on WebAssembly's SIMD, where a desktop with AVX2 solves eight, and gives the same world to the bit.
- **Building**: Box2D compiles in about 6 seconds, once, and adds about 350 KB to a game.

## Joints

A joint is an entity with `Joint` and exactly one settings component. It names
two different entities with `Body`; anchors and frame angles are local to each
body's origin. Distances are meters, angles are radians, forces are newtons and
torques are newton meters.

```csharp
var ceiling = Spawn(Body { type = BodyType.Static, position = float2(0, 5) });
var weight = Spawn(Body { position = float2(0, 4) }, Box);
Spawn(Joint { a = ceiling, b = weight, anchorA = float2(0, -1) },
      RevoluteJoint { enableMotor = true, motorSpeed = 2, maxMotorTorque = 10 });
```

The package exposes every joint type in its vendored Box2D:

- `DistanceJoint`: fixed distance, or a spring with length limits and a linear motor.
- `RevoluteJoint`: a hinge, with angular spring, limits and motor.
- `PrismaticJoint`: a slider along frame A's local x-axis, with spring, limits and motor.
- `WheelJoint`: sliding suspension along frame A's x-axis and an angular motor.
- `WeldJoint`: fixes both local frames, with optional linear and angular softness.
- `MotorJoint`: drives relative linear/angular velocity or local-frame position with bounded forces.
- `MoverJoint`: drives linear velocity with independent x/y force limits.
- `PogoJoint`: a linear suspension along `normal`, with tension/compression limits.
- `FilterJoint`: prevents the two bodies colliding without constraining movement.

Write the settings component to change a joint at the next step. `Joint.enabled`
turns its native constraint off and on; `collideConnected` allows collisions
between constrained bodies (a filter joint always prevents them). `Joint.force`
and `torque` contain the last step's reaction load. Removing `Joint`, destroying
its entity, or removing either body removes the constraint. A joint with missing
bodies, the same body twice, an invalid settings count, or invalid numeric
settings is destroyed. Disabled joints retain their entity and settings.
Numeric settings must be finite and within ±1e12, with nonnegative stiffness,
damping and force/torque magnitudes and ordered limits. Revolute limits clamp to
±0.99π. `PogoJoint.normal` must be a unit vector.

Motor, spring and limit changes retain the native joint and its warm-start state.
Changing a Pogo's normal, force bounds or initial impulse/velocity recreates that
constraint because this Box2D has no setters for those fields. Rebinding bodies
or replacing the settings type also recreates it. Joints, their loads and all
native solver state survive snapshots, packing, rollback and joins.

## Additional shapes and live edits

`Capsule` has local endpoints `a`, `b` and a `radius`. `Polygon.points` supplies
three to eight local vertices; the package computes their convex hull, with an
optional rounded `radius`. `Segment` has local endpoints `a` and `b` and is
two-sided. These shapes have the same `friction`, `bounce` and `sensor` settings
as the original shapes; capsules and polygons also have `density`.

`Chain.points` supplies an open path (at least two points) or a closed path
(`loop = true`, at least three). Chains are one-sided: the front is to the right
of each directed edge. Open endpoints use extrapolated ghost vertices. They
belong to static or kinematic bodies, have `friction` and `bounce`, and cannot be
sensors. Neighboring points must be more than 5 mm apart. The final point of a
loop is distinct from its first. Self-intersections are the game's responsibility.

Every shape works on a body's own entity or on a `Part`. Geometry, materials,
sensor status and collision filters follow later writes. An edited fixture is
rebuilt, updates the body's mass and can generate new contact begin/end events.
Removing a shape component removes that fixture. Removing `Body` removes its
native body and constraints while retaining the entity; adding a body again
reattaches its shapes. Removing `Part` detaches its shapes, and changing `of`
reattaches them to the new body. A part whose owner has no body is destroyed.

Invalid geometry produces no fixture or query result. Coordinates, radii and
material values must be finite and within ±1e6; sizes/radii must be positive
(a polygon's rounding radius may be zero), density/friction/bounce nonnegative,
and polygons must form a nondegenerate hull. Repairing a shape attaches it again.

`Cast`, `CastAll` and `Overlap` accept `Capsule`, `Polygon` and `Segment` too,
including ignore-body and sensor options where applicable. Chains are query
targets, rather than shapes to sweep. Current and rewound queries use the same
shape geometry, including polygon surfaces and one-sided chain rays.

## Collision and query filters

An optional `CollisionFilter` applies to every shape on that entity, including a
part's shapes. `category` defaults to 1, `mask` to -1 (all 64 bits) and `group` to
0. Two shapes collide when each category intersects the other's mask. Equal
nonzero groups override masks: positive groups collide and negative groups do
not. Sensors never report other sensors. Filter edits apply at the next step.

All current and rewound rays, casts and overlaps accept a trailing optional
`QueryFilter filter`, with the same `category` and `mask` defaults. Query filters
use both masks and ignore collision groups. For example:

```csharp
physics.Raycast(from, to, out var hit, filter: QueryFilter { mask = 4 });
physics.CastAll(Capsule, from, 0, to, out var hits,
                includeSensors: true, filter: QueryFilter { mask = 4 });
```

`Inside` reports the sensor's actual visitors and therefore follows collision
filtering; detached contexts retain the filter values captured at their endpoint.

## Remaining limits

- `Touched` and `Parted` don't say which of an entity's own `Box` and `Circle` it was, and a parting from an entity that was destroyed doesn't say which entity.

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

`Body.rewind` opts the entire body and its parts in. Their poses interpolate between retained physics steps, with `Snap()` preventing interpolation across a teleport. Shape geometry and membership use the upper endpoint. Non-opted bodies remain current, so a wall still blocks a rewound player. A moving door can opt in too. Despawned historical shapes can be hit; their entity handles retain their generations, and the game must check that a damage target is still alive. A newly spawned opted-in body has no shape in an older frame. An existing body that opts out uses its current geometry.

`Rewind` returns false for disabled history, invalid, stale or future times, or an unavailable endpoint. The output then contains no queryable geometry. No time is clamped and no current-world fallback is substituted. History begins after the first completed physics step and follows the same snapshot, rollback and serialization path as the match. Geometry is that of the physics step, as with current queries; write movement before `PhysicsStages.Step` when presentation and collider poses should agree.

The history window covers the full age of the frame when the server executes the shot: confirmation delivery, time since the last drawn frame, input lead and delivery of the input all contribute. Low frame rates, stalls, jitter and packet loss can push a shot outside `maxRewind` even when its ping seems small. The engine uses the actual presented tick and fraction, rather than estimating the shot time by subtracting ping.

The server validates the input timestamp, but the game remains responsible for authoritative shot validation. It must never accept a client-supplied collider pose, origin or hit just because the timestamp passed validation.
