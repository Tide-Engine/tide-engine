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
- **`asleep`** says which it is, after each step, and a system that writes it puts the body to sleep or wakes it at the next one: `body.asleep = true` on a body that's moving stops it where it is, with none of its speed, until something touches it or code wakes it. A body spawned with it starts asleep. Box3D sleeps and wakes an island at a time, the bodies that touch each other (through bodies that move, not through static ones), so one that's put to sleep takes what it touches with it, and one that's woken wakes what it fell asleep with. What's put to sleep in a tick is asleep as that tick's step starts, whatever else was written that tick: a push, a move, a new gravity. A static body is never asleep, and a body can be put to sleep where `Physics.sleeping` is off. (`test/sleep.tide` is a game that does all of it.)
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
- **Sensors are in nothing's way**: a ray and a cast go through them, and `Overlap` doesn't list them. `Inside` is what's asked of a sensor.
- **The answer is the same on every machine**, for the same match: of two things as near, the one whose shape was made first is the one that's met, whatever order Box3D has them in, and lists come in the order of their bodies' ids (`Body.id`).
- A view is asked of the world it draws. When the arena holds another state (a view of a confirmed world, while this machine runs ahead of it), that world's resident pages are restored before it's asked. The next tick restores its own state: see What it costs.

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

- **Everything that changes Box3D's world goes through the match**, which is why code moves a body by writing its component, and never calls Box3D. The stamp is the stamp before it and everything done to the world since, hashed. Each commit also supplies Box3D's deterministic digest: its counts, awake bodies and their contacts' impulses. The match's hash covers the stamp and digest, not the arena's raw bytes, which contain pointers and may have another allocation order on another machine.
- **A commit copies what Box3D may have written.** Box3D marks its allocations and changed bodies, shapes, contacts, trees and sleeping sets; the bridge marks its own persistent bookkeeping. Every touched page is copied, without first comparing or serializing it, and an untouched page stays shared with the previous state. This is a conservative set: a touched page need not have different bytes. A change the marks don't follow, or a world with most bodies awake, can copy every page.
- **A rollback restores pages, without making Box3D's world again.** Before stepping a state, the bridge compares the resident's page identities with the pages last committed or restored into the arena, and copies the ones that differ. It checks those identities even when the stamp matches: two runs of the same simulation state can have different pointers and allocation order. Allocator state, the world's persistent arrays and geometry are restored together.
- **Canonical bytes are for sending a world and carrying it across builds.** A join, resync, host migration or reload that packs the world writes Box3D's snapshot and the geometry it names. The receiving machine builds its arena from those bytes and commits it. A network delta currently carries the resident's canonical state whole when it differs from the base. Raw arena pages never cross machines. `box3d/TIDE.md` describes the changes that keep the simulation and canonical bytes the same on desktop and the web.
- **What a box is made of is kept once.** Box3D keeps a box as a hull (its corners, edges and faces), shared by crates of that size. It lives in the resident arena with the rest of the persistent state, and is serialized with the snapshot only when the world is packed.
- **A query restores the state it asks about.** The process has one arena for Box3D. A view of a confirmed world can restore that world's pages while this machine predicts ahead; the next tick restores its own pages before it steps. Queries themselves change no match state.
- **A world at rest isn't stepped.** Nothing awake, nothing written to any body and the gravity it had: there's no step or commit, and nothing of the match is written. What's left is looking at each body's fields for what code wrote.
- **Sensors and contacts are resident too.** What a sensor has in it, and the contact speeds and impulses that the next step and `Hit` use, survive a rollback with the rest of the world.
- **Whether a body is asleep is told like its place**: `Body` has what the last step left (`stepAsleep`) beside `asleep`, so a write is the two differing, found with no call into Box3D, as a body's settings are. A body may already have been woken by another body earlier in the tick, so comparing against Box3D itself would mistake that for code putting it back to sleep. Both bytes fit beside `bullet`, so the component is as big as it was. Requests to sleep are applied last, before the step. `Leave` also reads back the bodies Box3D put to sleep with them.
- **A native hot reload can keep the arena.** It outlives the game's library, and the new build relinks callbacks before using it. A reload that carries the world through canonical bytes, as the web does, rebuilds its arena instead. The resident type and its engine interface are Provisional; for now they're for libraries that come with Tide.

## What it costs

A resident tick pays for copying touched pages and computing the digest, plus the bodies' components going in and out. Keeping a snapshot shares the resident's pages; hashing reads its stamp and digest. A rollback copies the pages that differ from the target state. A query of another state pays that restore too, and the next tick pays to restore its own. Packing a world still writes the full canonical snapshot and geometry.

These costs depend on how much of the arena is touched, not only on how many bodies move: trees, contacts, sleeping sets and allocator metadata can touch pages too. A body's type changing or many bodies waking can mark the whole state. A world at rest skips the commit.

### Resident comparison (2026-10-07)

Three alternating runs of the image-list baseline (`14df837`) and resident build, Windows clang 23 release and WebAssembly release through Node's WASI, not a browser. The machine remained busy: CPU load was 63% before the rounds and 24% after. Each entry is the median (minimum–maximum) across those runs, in milliseconds. The costs combine the tick with keeping its snapshot and hashing on one thread, since the resident commit replaced work formerly split between the image, list and hash.

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

- Hulls other than boxes, meshes, height fields, compounds and joints. Box3D's canonical snapshot holds meshes, height fields and joints already, with their geometry, but the package exposes none of them. A world with a compound has no canonical snapshot, as a compound's bytes aren't the same on every machine.
- A hull no shape has any more stays in the resident arena: a game that makes boxes of ever new sizes keeps them all.
- A parting from an entity that was destroyed doesn't say which entity: that would take keeping whose each body was for a tick after it's gone.
- A shape's fields don't follow later changes: its size, its `density`, `friction` and `bounce`, and whether it's a sensor are read when it's put on its body. A part that's destroyed and spawned again is how a shape changes.
- Removing `Body` from an entity that stays leaves its body in the world, and removing a shape's component leaves the shape on its body: destroy the entity, or make the shape a part and destroy that.
- Queries take no filter: everything solid is found, and every sensor passed by. They find the nearest thing, and not everything on the way.
- A per-body setting for whether it may sleep, locks on the axes a body moves and turns on, and joints.
- The resident arena is fixed in size (64 MiB for Box3D), and exhaustion stops the program. There is one arena for the package in a process; switching between worlds restores their pages. Resident support for arbitrary user C libraries and deltas of the canonical state against a base aren't built.

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

The context has every current query form, including all shape casts, overlaps, sensor `Inside` queries and existing ignore-body overloads. It is detached geometry: preparing and querying it moves no live body, changes no contacts and sends no events. Query output is temporary unless the game saves it.

`Body.rewind` opts the entire body and its parts in. Their poses interpolate between retained physics steps, with `Snap()` preventing interpolation across a teleport. Shape geometry and membership use the upper endpoint. Non-opted bodies remain current, so a wall still blocks a rewound player. A moving door can opt in too. Despawned historical shapes can be hit; their entity handles retain their generations, and the game must check that a damage target is still alive. A newly spawned opted-in body has no shape in an older frame. An existing body that opts out uses its current geometry.

`Rewind` returns false for disabled history, invalid, stale or future times, or an unavailable endpoint. The output then contains no queryable geometry. No time is clamped and no current-world fallback is substituted. History begins after the first completed physics step and follows the same snapshot, rollback and serialization path as the match. Geometry is that of the physics step, as with current queries; write movement before `PhysicsStages.Step` when presentation and collider poses should agree.

The server validates the input timestamp, but the game remains responsible for authoritative shot validation. It must never accept a client-supplied collider pose, origin or hit just because the timestamp passed validation.
