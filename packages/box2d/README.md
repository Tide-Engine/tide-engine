# Box2D

[Box2D](https://box2d.org), Erin Catto's 2D physics engine, for a Tide match: bodies and their shapes are components, every tick steps them, and the match stays the same on every machine, through rollbacks, joins and reloads.

An experiment: what's here works and is tested, and what it costs is below. Its names and its shape are not settled.

The docs have a page for it, with an example to play: https://tide-engine.dev/physics/box2d

## Using it

A game lists it in its `tide.packages`:

```
box2d
```

```csharp
using Box2D;

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
    Spawn(Body { type = BodyType.Static, position = float2(0, -0.5) }, Box { size = float2(40, 1) });
    Spawn(Body { position = float2(0, 4) }, Box, Crate);
    Spawn(Body { position = float2(0.3, 6), velocity = float2(0, -2) }, Circle { radius = 0.4, bounce = 0.5 }, Crate, Hopper);
}

// A body moves by what its component says
system Hop(mut Body body, mut Hopper hopper)
{
    hopper.ticks += 1;
    if (hopper.ticks % 40 == 0) body.impulse = float2(0, 5);
}

event(Touched touched) Meet(mut Tally tally, with Crate)
{
    if (touched.other != this) tally.touched += 1;
}
```

(`test/stack.tide` is a game like this one, which the package's tests run, and `test/course.tide` one with a sensor, a body spawned with an impulse, a system before the step and bodies at rest that code moves.)

- **`Body`** is a rigid body: `type` (`Static`, `Kinematic`, `Dynamic`), `position`, `angle`, `velocity`, `spin`, and `gravityScale`, `linearDamping`, `angularDamping`, `fixedRotation` and `bullet`, which are read when the body is made. After each step the first four are where Box2D left the body. A system that writes them moves the body: the next step starts from what they say. `impulse` and `turn` are given to the body at the next step, once: the ones a body is spawned with at its first, once its shapes are on it.
- A body at rest is asleep, and whatever code writes into it wakes it: a speed, an impulse, and a `position` or an `angle` too, which also wake what was touching it.
- **`asleep`** says which it is, after each step, and a system that writes it puts the body to sleep or wakes it at the next one: `body.asleep = true` on a body that's moving stops it where it is, with none of its speed, until something touches it or code wakes it. A body spawned with it starts asleep. Box2D sleeps and wakes an island at a time, the bodies that touch each other (through bodies that move, not through static ones), so one that's put to sleep takes what it touches with it, and one that's woken wakes what it fell asleep with. What's put to sleep in a tick is asleep as that tick's step starts, whatever else was written that tick: a push, a move, a new gravity. A static body is never asleep. (`test/sleep.tide` is a game that does all of it.)
- `position` is a `[Position]` and `angle` a `[Rotation]`, for views: they see a body turn the short way through half a turn, where its angle goes from pi to -pi, and get to where the server says it is over a few frames when this machine predicted otherwise.
- **`Box`** and **`Circle`** are shapes on the entity's body: `size` or `radius`, `offset`, `density`, `friction`, `bounce`, `sensor`.
- **`Touched`** and **`Parted`** are sent to both entities when their bodies start and stop touching, each with the `other`, once for each pair of shapes. A shape with `sensor` stops nothing, and sends them for the shapes that come into it and leave it, static bodies' too; two sensors say nothing of each other. When the other entity was destroyed while they touched, `Parted` comes with `default` for it.
- **`Physics`** is the world: `gravity` and `substeps`. A gravity that changes wakes every body.
- An entity that's destroyed takes its body with it.

The package's systems run before the game's, in this order: bodies and shapes that are new are made and what code wrote goes to Box2D, the world steps, bodies' components get where it left them, and the events are sent. So a game's systems read this tick's positions, and what they write is the next step's.

They're one stage, `PhysicsStages.Step`. A system that has to write into this tick's step says `[Before(PhysicsStages.Step)]`, and runs before all of them:

```csharp
[Before(PhysicsStages.Step)]
system Walk(PlayerInput input, mut Body body, with Player)
{
    body.velocity.x = input.move.x * 4;
}
```

That's the one way. The package's own systems (`Enter`, `Simulate`, `Leave` and the rest) each say where they are in the stage, so nothing a game orders its systems by moves them, and they aren't for a game to name: a system put among them writes after bodies have gone to Box2D and before they come back, and the step's own is written over it. `test/order.tide` is a game that orders its systems each way, with the order that comes of it in `test/order.txt`.

## How it holds a match

Box2D keeps its world in C's memory, outside the match, where rollbacks, joins, host migration and hot reloads don't reach. So after each step the package writes Box2D's whole state into the match, as bytes (`Physics.image`, from Box2D's own snapshot), with a stamp that names it. Before each step, C's world is either at the match's stamp already, or it's made from the image again. Nothing else is needed: a client that predicted wrong, a player who joined, a reloaded build and a new host each find no world at their stamp and get one.

- **Everything that changes Box2D's world goes through the match**, which is why code moves a body by writing its component, and never calls Box2D. The stamp is the stamp before it and everything done to the world since, hashed: Box2D's next state is its last and what's done to it, so the image is never read to name it.
- **The image is in the match's hash**, so it's the same bytes on every machine for the same state, 32-bit web builds and 64-bit desktops alike, whatever their SIMD. Box2D doesn't promise that by itself: `box2d/TIDE.md` lists the changes that took. `tide_box2d_stack` pins the simulation's hash and every tick's image on each platform.
- **The image is in sections that keep their place**, so what changes of it from tick to tick is what changed in the world, and the match's snapshots share the rest. As Box2D writes it, a contact that starts or ends moves every byte after it.
- **A world at rest isn't stepped.** Nothing awake, nothing written to any body and the gravity it had: there's no step and no image, and nothing of the match is written. What's left is looking at each body's fields for what code wrote.
- **Whether a body is asleep is told like its place**: `Body` has what the last step left (`stepAsleep`) beside `asleep`, so a write is the two differing, found with no call into Box2D. Asking Box2D whether the body is awake, and comparing, wouldn't do: by the time a body is looked at, the tick may have woken it already (a body that was destroyed beside it, a body before it that code moved), and what Box2D says then would read as code having put it back to sleep. Both bytes are in the two a `Body` had to spare, so it's as big as it was, and a world where nothing sleeps is the same bytes. Sleeping is done by the step, last (`put_to_sleep` in `bridge.c`), and the bodies Box2D took along are found in that tick alone, by asking of each that was awake and that the step says nothing of.
- **What a sensor has in it is in the image too**, so a world made from one says the same of its sensors after its next step. Box2D writes it with two bytes of every entry that are whatever was in memory: the package's copy writes zeros there (`box2d/TIDE.md`).

## What it costs

Measured on a release build, 2,400 crates in stacks (an image of 2.5 MB), one thread, with the world kept and hashed every tick as a client's is:

| A tick, in milliseconds | Everything moving | One crate pushed | At rest |
|---|---|---|---|
| Box2D's step | 0.31 | 0.01 | 0 |
| Its state into an image | 0.14 | 0.13 | 0 |
| The image into the match, and bodies in and out | 0.16 | 0.08 | 0.02 |
| Keeping and hashing the world | 0.26 | 0.04 | 0.00 |
| Of the image, what changed | 77% | 7% | 0% |

- The step runs on the tick's threads (`tide_job_start`, `tide/jobs.h`) once 2,048 bodies are awake (4,096 on the web), and gives the same world on any number of them: 0.45 ms on one thread and 0.27 on twelve, where every crate is awake. The step alone is quicker shared from a few hundred bodies, but a tick whose C shares work out runs its systems on threads too, and a whole tick only comes out ahead from about there. Measured on a machine that was doing other things, a tick on one thread against twelve: 0.53 ms against 0.56 with 1,200 crates, 0.61 against 0.69 with 1,800, 1.04 against 0.93 with 2,400, 2.3 against 1.8 with 4,800, 4.4 against 3.4 with 9,600. On the web (Node, 24 threads): 1.47 against 1.63 with 2,400, 3.6 against 3.1 with 4,800, 6.4 against 5.2 with 9,600.
- On the web the step takes about 0.6 ms on one thread, and 0.55 shared out over the 24 threads a page gets on a machine of 12 cores, one for each thread its cores run (measured under Node; a tick, 1.7 ms against 1.5 on one thread, and 1.2 on 12 threads, whose step takes 0.34: Box2D's solver gains nothing from a core's second thread). Box2D's workers wait for each stage of a step in a loop, which on the web holds a processor for as long as it goes on, so one that has waited long sleeps there until the next stage (`box2d/TIDE.md`): before that, the same step took 1.3 to 2.2 ms on those 24 threads, and 4.3 on a machine with four other busy threads, where it now takes 0.57.
- The image is every body, shape and contact, about a kilobyte a body, whatever is awake. Box2D writes it straight into memory the match's list is then compared with, chunk by chunk (`out List<byte>`), so only what changed is copied and hashed.
- What the package adds to the step is memory moved, not arithmetic: the image is written (2.5 MB), compared with the last one, what changed of it copied into the match and hashed, and each body's place read back into its component. So it goes by how big the world is, where the step goes by what's awake.
- Bodies go in and out through two systems written in C (`extern system`, `tide/rows.h`), which read and write the bodies' fields as the arrays the match keeps them in. Every body is looked at every tick, and a system written in Tide copies each one out to look at it. With every crate moving, reading the 2,400 back took 0.17 ms that way (a call, a copy and a `world.Set` for each) and takes 0.015: Box2D says which bodies it moved and where they are, and those are written, 12 fields of a body's 22. Finding what code wrote took 0.034 ms and takes 0.015, or 0.006 at rest, where it took 0.024. Giving new bodies their entities stayed in Tide, as the match keeps that in a map: it asks C of each body whether it's new, and takes 0.009 to 0.012. A tick went from 0.75 ms to 0.61 with everything moving, from 0.26 to 0.21 with one crate pushed, and from 0.031 to 0.017 at rest (seven runs of each, old and new in turn, on a machine that was building other things meanwhile: the runs agree to within a tenth).
- Making C's world from an image, which a rollback does once, takes about half a millisecond.
- Compiling Box2D takes about 6 seconds once (9 optimized), and adds about 350 KB to a game.

## Not there yet

- Joints, chains, polygons other than boxes, capsules, ray casts and overlap tests.
- `Touched` and `Parted` don't say which shape of an entity's it was, and a parting from an entity that was destroyed doesn't say which entity: that would take keeping whose each body was for a tick after it's gone.
- A body's `type` and the fields read when it's made don't follow later changes, and a shape's don't either.
- Removing `Body` from an entity that stays leaves its body in the world.
- Reading the world from views.
