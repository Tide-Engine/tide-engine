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

(`test/stack.tide` is a game like this one, which the package's tests run, `test/course.tide` one with a sensor, a body spawned with an impulse, a system before the step and bodies at rest that code moves, and `test/kit.tide` one with parts, bodies that change type, every question code can ask, hits and each field the world is tuned by.)

- **`Body`** is a rigid body: `type` (`Static`, `Kinematic`, `Dynamic`), `position`, `angle`, `velocity`, `spin`, and `gravityScale`, `linearDamping`, `angularDamping`, `sleepSpeed`, `fixedRotation` and `bullet`, which are read when the body is made. After each step the first four are where Box2D left the body. A system that writes them moves the body: the next step starts from what they say. `impulse` and `turn` are given to the body at the next step, once: the ones a body is spawned with at its first, once its shapes are on it.
- **A body's `type` can be written**, any to any: the next step has it as the new kind, with its shapes. What touched it parts and is found again, and what rested on it wakes.
- A body at rest is asleep, and whatever code writes into it wakes it: a speed, an impulse, a `type`, and a `position` or an `angle` too, which also wake what was touching it. `sleepSpeed` is how slow it has to be to fall asleep.
- **`asleep`** says which it is, after each step, and a system that writes it puts the body to sleep or wakes it at the next one: `body.asleep = true` on a body that's moving stops it where it is, with none of its speed, until something touches it or code wakes it. A body spawned with it starts asleep. Box2D sleeps and wakes an island at a time, the bodies that touch each other (through bodies that move, not through static ones), so one that's put to sleep takes what it touches with it, and one that's woken wakes what it fell asleep with. What's put to sleep in a tick is asleep as that tick's step starts, whatever else was written that tick: a push, a move, a new gravity. A static body is never asleep. (`test/sleep.tide` is a game that does all of it.)
- `position` is a `[Position]` and `angle` a `[Rotation]`, for views: they see a body turn the short way through half a turn, where its angle goes from pi to -pi, and get to where the server says it is over a few frames when this machine predicted otherwise.
- **`Box`** and **`Circle`** are shapes on the entity's body: `size` or `radius`, `offset`, `density`, `friction`, `bounce`, `sensor`. An entity has one of each at the most.
- **`Part`** is a shape on another entity's body: `Spawn(Box { offset = float2(1, 1) }, Part { of = brick });`. The entity has a `Box` or a `Circle` and no `Body`; its shape is on the body of the entity it names from the next step. Destroying the part takes that one shape off, destroying the body's entity takes its parts with it, and a part that names no entity with a body is destroyed. A part can be a sensor.
- **`Touched`** and **`Parted`** are sent to both entities when their bodies start and stop touching, each with the `other`, once for each pair of shapes, and with which shapes they were: `part` and `otherPart`, the body's own entity for a shape on it and the part's entity for a part. A shape with `sensor` stops nothing, and sends them for the shapes that come into it and leave it, static bodies' too; two sensors say nothing of each other. When the other entity was destroyed while they touched, `Parted` comes with `default` for it; when only a part was, with the right `other` and `default` for the part.
- **`Hit`** is sent to both entities when their bodies meet faster than `Physics.hitSpeed`: the `other`, the parts, the `point`, the `normal` (from this body into the other) and the `speed` they closed at.
- **Asking the world**: `Raycast(physics, from, to, out hit)`, `Cast(physics, shape, from, angle, to, out hit)` for a `Box` or a `Circle` moved along a way, `Overlap(physics, shape, at, angle, out found)`, and `Inside(physics, sensor, out found)` for who's in an entity's sensors, a body's or a part's. They return whether they found anything; `RayHit` is the `entity`, its `part`, the `point`, the `normal` and the `fraction` of the way. `Raycast` and `Cast` take one `Body` to leave out, last (`ignore: body`). They work from the match anywhere in a tick, and from views.
- **`Physics`** is the world: `gravity`, `substeps`, and how it's tuned: `contactStiffness`, `contactDamping` and `contactSpeed`, `bounceSpeed`, `hitSpeed` and `sleeping`. Each is Box2D's own default, and a change is the next step's. A gravity that changes wakes every body.
- An entity that's destroyed takes its body with it.

### What a question sees

The world as its last step left it: a system before the step asks of the last tick's world, which is what it reads in the bodies' components too; a system after it, and a handler, of this tick's; a view of the world it draws, as its last tick left it. What code wrote into a body since isn't in the answer, nor a body or a shape made since: they're the next step's. An entity that was destroyed since isn't met any more (the match says which bodies and parts are gone, and the question leaves them out).

Sensors are never met, and `Overlap` doesn't find them. A ray or a shape that starts in something meets it there, at `fraction` 0 with no normal, and a shape that only touches something where it starts counts as in it: Box2D takes two shapes within about 6 mm of each other for touching, so a crate's own box, cast along the floor it rests on, meets the floor at once. A slightly smaller shape slides.

### The same answer on every machine

Match code acts on the answers, so they have to be the same everywhere, in a world that was stepped here and in one made from its image:

- **Everything a question reads is in the image**: the bodies' places, the shapes, the trees and what's in each sensor. The image is the same bytes on every machine for the same state, and a world made from it answers from those bytes.
- **Nothing depends on the order Box2D comes to things in.** Its trees can be walked in another order in a world that was restored. So the first thing met is the least by how far along the way it is and then by which shape was made first (the shape's slot in Box2D, which is in the image), and Box2D is told to go on looking a hair past the best so far, so that one as near isn't cut off before it's compared. `Overlap` and `Inside` sort what they found by the bodies' slots and give each once.
- **The arithmetic is Box2D's own**, compiled with the engine's flags like its step: no fused multiply-adds, the same on x64, ARM and the web.
- `box2d_answers_what_code_asks` pins a hash of every answer of 150 ticks, the same in debug, release and on the web, asks each tick from a system before the step, one after it and a view, and again of a world that's made from the image to answer.

Systems that ask run on the tick's threads like any others: a question changes nothing of C's world, each thread has its own scratch memory, and finding the world for a stamp (or making it) is behind a lock. What changes the world takes `Physics` to write, so the schedule never has it beside a question.

### Picks

The owner decided what there is; these are the names and the edges picked while building it, for them to confirm:

- **Names**: `Part` and its `of`; `Hit`; `RayHit`; `Raycast`, `Cast`, `Overlap`, `Inside`; `part` and `otherPart` on the events; `Physics.contactStiffness`, `contactDamping`, `contactSpeed`, `bounceSpeed`, `hitSpeed`, `sleeping`; `Body.sleepSpeed`. They're the Box3D package's names wherever both have the thing, as the two were built side by side from one list: `Inside` takes the sensor's entity in both, and the three `contact` fields are spelled as Box3D's.
- **A part that names no body is destroyed**, in the tick that finds it so, with nothing said: an error would stop a match over an entity that went a tick before its part was made.
- **`Inside` takes the entity whose shape the sensor is**, a body's or a part's, and looks in that entity's own sensors: a body's parts are asked one by one. The match keeps which shapes those are (`Physics.sensors`, an entry for each entity that has a sensor), as a function is handed no `World` to look an entity's components up in.
- **Where this package has more than Box3D's:** `Hit` says which parts met (`part`, `otherPart`), `Raycast` and `Cast` leave a `Body` out, and a `Body` has `sleepSpeed`. **And less:** of a body, only its `type` follows what's written later; Box3D's `gravityScale`, dampings and `bullet` do too.
- **One body can be left out of a ray or a cast**, as a `Body` after the `out`. No layers or masks: Towers needed none.
- **A body that turns kinematic keeps its `velocity`.**
- **`Hit` goes to both bodies**, each with the normal pointing from itself, and its threshold is the world's alone.
- **Tuning is the world's, but for `sleepSpeed`**, which is a body's. Whether one body may sleep at all, and a shape's fields after it's made, were left out.

### When its systems run

The package's systems run before the game's, in this order: bodies and shapes that are new are made and what code wrote goes to Box2D, the world steps, bodies' components get where it left them, and the events are sent. So a game's systems read this tick's positions, and what they write is the next step's.

They're one stage, `PhysicsStages.Step`: `Begin`, `Enter`, `Own`, `AttachBoxes`, `AttachCircles`, `AttachPartBoxes`, `AttachPartCircles`, `DropParts`, `Simulate`, then `Leave` and `Report`. A system that has to write into this tick's step says `[Before(PhysicsStages.Step)]`, and runs before all of them:

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
- **A world at rest isn't stepped.** Nothing awake, nothing written to any body and the gravity and tuning it had: there's no step and no image, and nothing of the match is written. What's left is looking at each body's fields for what code wrote.
- **Whether a body is asleep is told like its place**: `Body` has what the last step left (`stepAsleep`) beside `asleep`, so a write is the two differing, found with no call into Box2D. Asking Box2D whether the body is awake, and comparing, wouldn't do: by the time a body is looked at, the tick may have woken it already (a body that was destroyed beside it, a body before it that code moved), and what Box2D says then would read as code having put it back to sleep. A body's type is told the other way, by asking Box2D once and keeping its answer (below), which holds because only code changes a type: whether a body is awake changes with every step. Both bytes are in the six a `Body` had to spare, so it's as big as it was, and a world where nothing sleeps is the same bytes. Sleeping is done by the step, last (`put_to_sleep` in `bridge.c`), and the bodies Box2D took along are found in that tick alone, by asking of each that was awake and that the step says nothing of.
- **What a sensor has in it is in the image too**, so a world made from one says the same of its sensors after its next step. Box2D writes it with two bytes of every entry that are whatever was in memory: the package's copy writes zeros there (`box2d/TIDE.md`).
- **A body's type is asked of Box2D, not kept in the match**: `Body` gained no field for it. `Enter` compares what the component says with what Box2D has, which is in the image, so it's right wherever the world came from. Asking each body every tick tripled what `Enter` takes at rest, so each of C's worlds remembers Box2D's answer (`kinds` in `bridge.c`), forgotten when the world is made again.
- **How the world is tuned is in the image**, as its gravity is: the step compares `Physics`' fields with what C's world has and sets the ones that differ, each noted into the stamp. So a game that sets none has the stamps it had.
- **Every shape is made with hit events on**, which is a byte of each shape in the image, and has Box2D keep how fast each contact closed. `Hit`'s threshold is the world's.

## What it costs

Measured on a release build, 2,400 crates in stacks (an image of 2.5 MB), one thread, with the world kept and hashed every tick as a client's is:

| A tick, in milliseconds | Everything moving | One crate pushed | At rest |
|---|---|---|---|
| Box2D's step | 0.31 | 0.01 | 0 |
| Its state into an image | 0.14 | 0.13 | 0 |
| The image into the match, and bodies in and out | 0.16 | 0.08 | 0.02 |
| Keeping and hashing the world | 0.26 | 0.04 | 0.00 |
| Of the image, what changed | 77% | 7% | 0% |

- The step runs on the tick's threads (`tide_job_start`, `tide/jobs.h`) once 2,048 bodies are awake (4,096 on the web), and gives the same world on any number of them: 0.45 ms on one thread and 0.27 on twelve, where every crate is awake. The step alone is quicker shared from a few hundred bodies, but a tick whose C shares work out runs its systems on threads too, and a whole tick only comes out ahead from about there. Measured on a machine that was doing other things, a tick on one thread against twelve: 0.53 ms against 0.56 with 1,200 crates, 0.61 against 0.69 with 1,800, 1.04 against 0.93 with 2,400, 2.3 against 1.8 with 4,800, 4.4 against 3.4 with 9,600. On the web (Node, 24 threads): 1.32 against 1.60 with 2,400, 2.6 against 2.75 with 4,800, 5.9 against 5.7 with 9,600. Those are on WebAssembly's SIMD (below), which left less of a step to share out: on the scalar path the web had before they were 1.47 against 1.63, 3.6 against 3.1 and 6.4 against 5.2, so sharing at 4,096 paid there, and is now about even until twice that.
- On the web Box2D solves 4 contacts at a time on WebAssembly's own SIMD, and gives, bit for bit, what its SSE2 code gives on x64 (`box2d/TIDE.md`: upstream has a path for Emscripten's WebAssembly only, so on Tide's web builds it took its scalar one). The step takes about 0.46 ms there on one thread, where it took 0.6 before, and a tick 1.2 where it took 1.3 (measured under Node, the least of seven runs of each in turn; in headless Chrome, the step went from 0.61 ms to 0.47). That's a quarter, not the half that four at a time sounds like: clang had already made SIMD of most of the scalar path by itself, as its wide float is a struct of four floats, so what the path adds is what clang missed (warm starting, bodies gathered and scattered a lane at a time, masks as numbers, and the face search of two polygons).
- Shared out over the 24 threads a page gets on a machine of 12 cores, one for each thread its cores run, the step takes about 0.6 ms on either path, and a tick of 2,400 crates 1.6 against 1.3 on one thread (under Node), which is why a page shares a step out later than a desktop does. On the scalar path, 12 threads were quicker than 24 (a tick 1.2, whose step took 0.34): Box2D's solver gains nothing from a core's second thread. Box2D's workers wait for each stage of a step in a loop, which on the web holds a processor for as long as it goes on, so one that has waited long sleeps there until the next stage (`box2d/TIDE.md`): before that, the same step took 1.3 to 2.2 ms on those 24 threads, and 4.3 on a machine with four other busy threads, where it now takes 0.57.
- The image is every body, shape and contact, about a kilobyte a body, whatever is awake. Box2D writes it straight into memory the match's list is then compared with, chunk by chunk (`out List<byte>`), so only what changed is copied and hashed.
- What the package adds to the step is memory moved, not arithmetic: the image is written (2.5 MB), compared with the last one, what changed of it copied into the match and hashed, and each body's place read back into its component. So it goes by how big the world is, where the step goes by what's awake.
- Bodies go in and out through two systems written in C (`extern system`, `tide/rows.h`), which read and write the bodies' fields as the arrays the match keeps them in. Every body is looked at every tick, and a system written in Tide copies each one out to look at it. With every crate moving, reading the 2,400 back took 0.17 ms that way (a call, a copy and a `world.Set` for each) and takes 0.015: Box2D says which bodies it moved and where they are, and those are written, 12 fields of a body's 22. Finding what code wrote took 0.034 ms and takes 0.015, or 0.006 at rest, where it took 0.024. Giving new bodies their entities stayed in Tide, as the match keeps that in a map: it asks C of each body whether it's new, and takes 0.009 to 0.012. A tick went from 0.75 ms to 0.61 with everything moving, from 0.26 to 0.21 with one crate pushed, and from 0.031 to 0.017 at rest (seven runs of each, old and new in turn, on a machine that was building other things meanwhile: the runs agree to within a tenth).
- Making C's world from an image, which a rollback does once, takes about half a millisecond. The first question asked where C has no world at the match's stamp does the same: the tick after a rollback, or a view of a world that isn't the last one stepped. For a world of 17 KB it's 0.008 ms.
- A question costs what Box2D's query does: 23 of them, rays, casts, overlaps and sensors, asked by a frame's views of a small world take 0.011 ms.
- What parts, types, hits and tuning added to a tick of these 2,400 crates, old and new in turn, the least of seven runs each: 0.601 ms to 0.611 with everything moving, 0.213 to 0.214 with one crate pushed, 0.017 to 0.018 at rest (of which `Enter`, 0.006 to 0.008: it looks at each body's type too).
- Compiling Box2D takes about 6 seconds once (9 optimized), and adds about 350 KB to a game.

## Not there yet

- Joints, chains, polygons other than boxes, and capsules.
- Rays and casts give the first thing met, not everything along the way, and leave out one body at the most. No layers or masks, for collisions or for questions, and a sensor is never met.
- `Touched` and `Parted` don't say which of an entity's own `Box` and `Circle` it was, and a parting from an entity that was destroyed doesn't say which entity: that would take keeping whose each body was for a tick after it's gone.
- The fields of a body that are read when it's made don't follow later changes, and a shape's don't either.
- Removing `Body` from an entity that stays leaves its body in the world, and removing `Box` or `Circle` leaves its shape on.
