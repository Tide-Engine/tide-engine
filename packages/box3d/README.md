# Box3D

[Box3D](https://github.com/erincatto/box3d), Erin Catto's 3D physics engine, for a Tide match: bodies and their shapes are components, every tick steps them, and the match stays the same on every machine, through rollbacks, joins and reloads.

An experiment, as the Box2D package beside it is, and built the same way: what's here works and is tested, and what it costs is below. Its names and its shape are not settled, and Box3D itself is at 0.1.

The docs have a page for it, with an example to play: https://tide-engine.dev/physics/box3d

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
- `position` is a `[Position]` and `rotation` a `[Rotation]`, for views: they see a body get to where the server says it is over a few frames, turning the short way, when this machine predicted otherwise.
- **`Box`**, **`Sphere`** and **`Capsule`** are shapes on the entity's body: `size`, or `radius`, or `radius` and `height` (a capsule stands along its body's y, `height` from end to end), with `offset`, `density`, `friction`, `bounce` and `sensor`. Density is in kilograms a cubic meter, 1000 unless a shape says, so a crate a meter wide weighs a ton and an impulse of 1000 changes its speed by a meter a second.
- **`Part`** puts an entity's shapes on another entity's body: `Spawn(Box { size = float3(0.2, 1, 0.2), offset = float3(0.8, 0.4, 0.8) }, Part { of = table });`. An entity has one shape of each kind at most, so a body of several boxes is a body and a part for each box past its first. The shapes are on the body from the next step. Destroying the part takes them off again, and what rested on them wakes; destroying the body's entity destroys its parts. A part whose `of` isn't an entity with a `Body` when its shapes would be made is destroyed. A part can be a sensor.
- **`Touched`** and **`Parted`** are sent to both bodies' entities when they start and stop touching, each with the `other`, once for each pair of shapes, and with which shapes: `part` is the entity this one's shape is a component of, itself or one of its parts, and `otherPart` the same of the other's. A shape with `sensor` stops nothing, and sends them for the shapes that come into it and leave it, static bodies' too; two sensors say nothing of each other. When the other entity was destroyed while they touched, `Parted` comes with `default` for it; when only its part was, with the entity and `default` for the part.
- **`Hit`** is sent to both when two bodies meet faster than `Physics.hitSpeed`: `other`, the `point`, the `speed` they closed at there, and the `normal`, which is the way this entity was pushed (from the other towards it). Once a step for each pair of shapes that meets that fast.
- **`Physics`** is the world: `gravity` and `substeps`, and how it steps, each a field with Box3D's own value unless a game writes another: `contactStiffness` (30 cycles a second), `contactDamping` (10) and `contactSpeed` (3 meters a second: the fastest two bodies that overlap are pushed apart), `bounceSpeed` (1: slower than this nothing bounces), `hitSpeed` (1) and `sleeping` (true). What's written is the next step's. A gravity that changes wakes every body, and so does `sleeping` turned off.
- An entity that's destroyed takes its body with it.

### Asking the world

Functions that ask what's in the world and change nothing. Each takes the `Physics` singleton and says whether it found anything, with what it found through `out`:

```csharp
// Stands on something: a ray a little way down from the feet
system Land(Physics physics, Body body, mut Walker walker)
{
    walker.grounded = Raycast(physics, body.position, body.position - float3(0, 1.1, 0), out var under);
    if (walker.grounded) walker.on = under.entity;
}
```

- **`Raycast(physics, from, to, out RayHit hit)`**: the first thing on the way from one point to another. `RayHit` has the `entity`, the `part` its shape is on (the entity itself, for a shape of its own), the `point`, the shape's `normal` there, and the `fraction` of the way, from 0 to 1. A ray says nothing of a shape it starts inside.
- **`Cast(physics, shape, from, rotation, to, out RayHit hit)`**: the first thing a `Box`, a `Sphere` or a `Capsule` meets that's moved from one place to another, turned by `rotation` all the way; the shape is as one on a body is, with its size and its `offset`. `hit.point` is where the two touch. What the shape is in at `from`, or within a few millimeters of, is met there: a fraction of 0, and no normal.
- **`Overlap(physics, shape, at, rotation, out List<Entity> found)`**: the entities whose bodies a shape at a place overlaps or touches, each once.
- **`Inside(physics, sensor, out List<Entity> found)`**: the entities whose bodies have a shape in an entity's sensors, as the last step found them. `sensor` is the entity the sensor shape is a component of: a body's, or a part's.

They can be asked from a system, a handler, a view, and the functions those call, wherever the system is in the tick.

- **They see the world as the last step left it.** A system after the step is asked of this tick's step, and one that's `[Before(PhysicsStages.Step)]` of the last tick's. What code wrote into a body since isn't there yet, nor a body or a shape that was spawned since: each is in the world from the next step. An entity that was destroyed since is found no more, at once.
- **Sensors are in nothing's way**: a ray and a cast go through them, and `Overlap` doesn't list them. `Inside` is what's asked of a sensor.
- **The answer is the same on every machine**, for the same match: of two things as near, the one whose shape was made first is the one that's met, whatever order Box3D has them in, and lists come in the order of their bodies' ids (`Body.id`).
- A view is asked of the world it draws. When C has no world at that state (a view of a confirmed world, while this machine runs ahead of it), one is made from the match's image, as for a tick that goes back: see What it costs.

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

Box3D keeps its world in C's memory, outside the match, where rollbacks, joins, host migration and hot reloads don't reach. So after each step the package writes Box3D's whole state into the match, as bytes (`Physics.image`), with a stamp that names it. Before each step, C's world is either at the match's stamp already, or it's made from the image again. Nothing else is needed: a client that predicted wrong, a player who joined, a reloaded build and a new host each find no world at their stamp and get one.

- **Everything that changes Box3D's world goes through the match**, which is why code moves a body by writing its component, and never calls Box3D. The stamp is the stamp before it and everything done to the world since, hashed: Box3D's next state is its last and what's done to it, so the image is never read to name it.
- **The image is in the match's hash**, so it's the same bytes on every machine for the same state, 32-bit web builds and 64-bit desktops alike, whatever their SIMD. Box3D doesn't promise that by itself: `box3d/TIDE.md` lists the changes that took. `tide_box3d_stack` pins the simulation's hash and every tick's image on each platform.
- **The image is in sections that keep their place**, and a shape and a contact take the same room in it whatever they are, so what changes of it from tick to tick is what changed in the world, and the match's snapshots share the rest.
- **Only what changed of the image is written**, while most of the world is asleep: Box3D says which bodies, shapes and contacts may be other than its last image has them, and the match's list keeps the rest as it is. The image that makes is, byte for byte, the image written whole, which the package's tests compare after every step.
- **What a box is made of is in the match once.** Box3D keeps a box as a hull (its corners, edges and faces), which its image names by a number. The hulls are in `Physics.geometry`, each once, however many crates share one, and that list only changes when a box of a new size is made: a tick's image has none of it.
- **A world at rest isn't stepped.** Nothing awake, nothing written to any body and the gravity it had: there's no step and no image, and nothing of the match is written. What's left is looking at each body's fields for what code wrote.
- **What a sensor has in it is in the image too**, so a world made from one says the same of its sensors after its next step.
- **Waking a body isn't something an image in parts follows**, so the image after code moves a sleeping body, or changes gravity, is written whole. It's only asked of a body that's asleep.
- **What an image in parts follows of the rest**: a part's shapes made and destroyed, on any body, asleep or static; a body's damping, gravity scale and bullet, written while it's awake; and all of the world's tuning but `sleeping`. A body's `type` that changes, `sleeping` turned off or on, and a setting written into a body that's asleep (which wakes it) each write that tick's image whole. Asking the world changes nothing, so it writes nothing.
- **What two bodies that meet hard are told is Box3D's**, which keeps how fast each point of a contact closed as the step began, for the shapes that ask: all of them do. It's in the image: a bit of each shape, a bit of each contact and that speed in each point that touches, the same bytes whatever the CPU.

## What it costs

Measured on a release build, 2,400 crates in stacks (an image of 3.5 MB, and 1.4 KB of geometry), one thread, with the world kept and hashed every tick as a client's is:

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

- Box3D alone is its sources as they came, built with its own flags, one thread, no Tide. The package's copy, with its changes and Tide's flags, steps as fast to the hundredth of a millisecond, so everything a tick takes beyond that row is the package's: 0.42 ms more than Box3D alone with everything moving, 0.08 more with one crate pushed, 0.01 at rest.
- The step runs on the tick's threads (`tide_job_start`, `tide/jobs.h`) once 512 bodies are awake, and gives the same world on any number of them: 1.3 ms on one thread and 0.5 on twelve, where every crate is awake. The step alone is quicker shared from a couple of hundred bodies, but a tick whose C shares work out runs its systems on threads too, and under about 500 bodies that costs more than the step saves.
- The image is every body, shape and contact, about a kilobyte and a half a body, whatever is awake. With most of the world awake Box3D writes all of it, straight into memory the match's list is then compared with, chunk by chunk (`out List<byte>`), so only what changed is copied and hashed.
- With half of the bodies asleep or more, **the image goes by what's awake**: Box3D writes the bodies, shapes and contacts that may have changed, and the list keeps the rest without reading it (`tide_list_out_keep`, `tide_list_out_part`, `tide/list.h`). With one crate pushed among 2,400 that's 0.04 ms for the image, written and in the match, where writing and comparing all of it was 0.24. The other parts of the image are small or cheap: what Box3D keeps in arrays of its own (the trees, the fat bounds) isn't copied but compared where it is, and the sleeping sets and the pairs aren't looked at while nothing was done to them.
- Bodies go in and out through two systems written in C (`extern system`, `tide/rows.h`), as the Box2D package's do: they read the bodies' fields as the arrays the match keeps them in, and write the fields of the bodies there's something to write for. Every body is looked at every tick, whatever is awake, and a system written in Tide copies each one out to look at it. With every crate moving, reading the 2,400 back took 0.29 ms that way (a call, a copy and a `world.Set` for each) and takes 0.04: Box3D says which bodies it moved and where they are, and those are written, 26 fields of a body's 40. The step says which bodies have anything to write, so of the others only which body each is gets read: with one crate pushed that's 0.009 ms, where it took 0.060. Finding what code wrote took 0.045 ms and takes 0.020, or 0.006 at rest, where it took 0.033. Giving new bodies their entities stayed in Tide, as the match keeps that in a map: it asked C of each body whether it's new, which took 0.009 to 0.013, and asks once a tick now for the ones that are, which takes nothing when there are none. A tick went from 1.70 ms to 1.44 with everything moving, from 0.17 to 0.10 with one crate pushed, and from 0.039 to 0.017 at rest (the medians of seven to nine runs of each, old and new in turn, the quieter ones of a machine that was building other things meanwhile).
- With what was added since (a body's type and settings looked at for what code wrote, the world's tuning, hits, parts), a tick takes 1.42, 0.09 and 0.010 (the least of six runs, in turn with the build before, which took 1.45, 0.095 and 0.017): finding what code wrote looks at nine more of a body's fields and takes 0.009 at rest where it took 0.007, and giving new bodies their entities takes 0.009 less. Keeping how fast contacts close, for `Hit`, isn't seen in the step's time.
- **A body's type that changes costs that tick's image written whole**: 0.25 ms a tick with 2,400 bodies asleep and one crate's type changed every tick, of which 0.14 is the image and 0.10 putting it in the match, against 0.010 at rest. Box3D moves the body between its sets and lets go of everything it touches, which an image in parts doesn't follow.
- **Asking the world** takes about a microsecond a question in a world of a dozen bodies (13 questions, rays, casts and overlaps, in 9 us), from a system or a view. When C has no world at the state that's asked about, it's made from the match's image first, which is what a rollback pays: 15 us for an image of 17 KB, and about a millisecond for this one of 3.5 MB.
- What's left with little moving is spread thin: of a tick's 0.09 ms with one crate pushed, the step is 0.03, the image 0.04 and the bodies in and out 0.02. And the world's hash, outside the tick, which is of every 16 KiB of the image that has a byte changed: a tenth of it, with one crate moving, as what changes of an image is spread over it.
- About one image in twenty is still written whole with one crate pushed (11 of 240): a stack wakes or falls asleep, the image's part for what's awake outgrows its place or leaves too much of it, and the parts after it move, which is all of the image again.
- Making C's world from an image, which a rollback does once, takes about a millisecond.
- On the web Box3D has no SIMD (it only knows Emscripten's WebAssembly): the same step takes about 2.2 ms there on one thread, and 0.85 shared out over the 24 threads a page gets on a machine of 12 cores, one for each thread its cores run (measured under Node; a tick, 2.4 ms against 3.3 on one thread). Box3D's workers wait for each stage of a step in a loop, which on the web holds a processor for as long as it goes on, so one that has waited long sleeps there until the next stage (`box3d/TIDE.md`): before that, the same step took 1.9 ms or more on those 24 threads, and 5.5 on a machine with four other busy threads, where it now takes 0.9. Half as many threads are still quicker (0.7, and a tick 2.1), as the solver gains nothing from a core's second thread.
- Compiling Box3D takes about 8 seconds of one core once (14 optimized), and adds about 800 KB to a game.

## Provisional

The owner asked for these five things (2026-10-06): shapes on entities of their own, a body's type that can change, queries, what's in a sensor and hits, and tuning as fields of `Physics`. The names and the details are picks they haven't confirmed, made to match what the Box2D package is to get:

- **`Part`**, with `of`; its own handles (`box`, `sphere`, `capsule`) in it. A part with no body to be on, when its shapes would be made or once its body goes, is destroyed: the one way both are handled. An entity with a `Body` and a `Part` has its shapes on the part's body.
- **`part` and `otherPart`** in `Touched` and `Parted`, each the entity itself for a shape of its own. The events still go to the bodies' entities, and a part's own entity hears nothing. A part that was destroyed still says its body, with `default` for the part.
- **A type that changes sends nothing for what still touches**, though Box3D lets go of it and finds it again.
- **A body's `gravityScale`, `linearDamping`, `angularDamping` and `bullet` follow what's written later too**, with its `type`: they were read when it was made, and silently not after.
- **`Raycast`, `Cast`, `Overlap`, `Inside`, and `RayHit`**: the names, the `Physics` parameter first, a cast's `from`, `rotation`, `to` order, and a shape passed as its component (`Box { size = ... }`). That they pass sensors by; that a ray says nothing of what it starts in and a cast meets it at 0; that an entity destroyed since the step isn't found; that lists come in the order of `Body.id`.
- **`Hit`**, with `other`, `point`, `normal` and `speed`, and no parts. `normal` is from the other towards the entity that hears it, as Unity's is. It's on for every shape, at Box3D's own threshold.
- **`contactStiffness`, `contactDamping`, `contactSpeed`, `bounceSpeed`, `hitSpeed` and `sleeping`**, with Box3D's defaults. A tuning that changes while the world is at rest doesn't step it, but for `sleeping` turned off.
- **The package's own that a game can see**: `Body`'s `stepType`, `stepGravityScale`, `stepLinearDamping`, `stepAngularDamping` and `stepBullet`, `Physics`' `goneShapes`, `parts` and `sensors`, the structs `Contact`, `Strike`, `Found` and `Sensors`, and the functions `Ready`, `Met`, `Owners` and `PartOf`.

## Not there yet

- Hulls other than boxes, meshes, height fields, compounds and joints. Box3D's image holds meshes, height fields and joints already, and `Physics.geometry` what they're made of; a world with a compound has no image, as a compound's bytes aren't the same on every machine.
- A hull no shape has any more stays in `Physics.geometry`: a game that makes boxes of ever new sizes keeps them all.
- A parting from an entity that was destroyed doesn't say which entity: that would take keeping whose each body was for a tick after it's gone.
- A shape's fields don't follow later changes: its size, its `density`, `friction` and `bounce`, and whether it's a sensor are read when it's put on its body. A part that's destroyed and spawned again is how a shape changes.
- Removing `Body` from an entity that stays leaves its body in the world, and removing a shape's component leaves the shape on its body: destroy the entity, or make the shape a part and destroy that.
- Queries take no filter: everything solid is found, and every sensor passed by. They find the nearest thing, and not everything on the way.
- A per-body setting for whether it may sleep, locks on the axes a body moves and turns on, and joints.
