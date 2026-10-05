# Box3D

[Box3D](https://github.com/erincatto/box3d), Erin Catto's 3D physics engine, for a Tide match: bodies and their shapes are components, every tick steps them, and the match stays the same on every machine, through rollbacks, joins and reloads.

An experiment, as the Box2D package beside it is, and built the same way: what's here works and is tested, and what it costs is below. Its names and its shape are not settled, and Box3D itself is at 0.1.

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

(`test/stack.tide` is a game like this one, which the package's tests run.)

- **`Body`** is a rigid body: `type` (`Static`, `Kinematic`, `Dynamic`), `position`, `rotation`, `velocity`, `angularVelocity`, and `gravityScale`, `linearDamping`, `angularDamping` and `bullet`, which are read when the body is made. After each step the first four are where Box3D left the body. A system that writes them moves the body: the next step starts from what they say. `impulse` and `angularImpulse` are given to the body at the next step, once. A rotation that isn't of length 1 is made so.
- **`Box`**, **`Sphere`** and **`Capsule`** are shapes on the entity's body: `size`, or `radius`, or `radius` and `height` (a capsule stands along its body's y, `height` from end to end), with `offset`, `density`, `friction`, `bounce` and `sensor`. Density is in kilograms a cubic meter, 1000 unless a shape says, so a crate a meter wide weighs a ton and an impulse of 1000 changes its speed by a meter a second.
- **`Touched`** and **`Parted`** are sent to both entities when their bodies start and stop touching, each with the `other`.
- **`Physics`** is the world: `gravity` and `substeps`.
- An entity that's destroyed takes its body with it.

The package's systems run before the game's, in this order: bodies and shapes that are new are made and what code wrote goes to Box3D, the world steps, and bodies' components get where it left them. So a game's systems read this tick's positions, and what they write is the next step's.

## How it holds a match

Box3D keeps its world in C's memory, outside the match, where rollbacks, joins, host migration and hot reloads don't reach. So after each step the package writes Box3D's whole state into the match, as bytes (`Physics.image`), with a stamp that names it. Before each step, C's world is either at the match's stamp already, or it's made from the image again. Nothing else is needed: a client that predicted wrong, a player who joined, a reloaded build and a new host each find no world at their stamp and get one.

- **Everything that changes Box3D's world goes through the match**, which is why code moves a body by writing its component, and never calls Box3D. The stamp is the stamp before it and everything done to the world since, hashed: Box3D's next state is its last and what's done to it, so the image is never read to name it.
- **The image is in the match's hash**, so it's the same bytes on every machine for the same state, 32-bit web builds and 64-bit desktops alike, whatever their SIMD. Box3D doesn't promise that by itself: `box3d/TIDE.md` lists the changes that took. `tide_box3d_stack` pins the simulation's hash and every tick's image on each platform.
- **The image is in sections that keep their place**, and a shape and a contact take the same room in it whatever they are, so what changes of it from tick to tick is what changed in the world, and the match's snapshots share the rest.
- **What a box is made of is in the match once.** Box3D keeps a box as a hull (its corners, edges and faces), which its image names by a number. The hulls are in `Physics.geometry`, each once, however many crates share one, and that list only changes when a box of a new size is made: a tick's image has none of it.
- **A world at rest isn't stepped.** Nothing awake and nothing written to any body: there's no step, no image, and no call into C.

## What it costs

Measured on a release build, 2,400 crates in stacks (an image of 3.5 MB, and 1.4 KB of geometry), one thread, with the world kept and hashed every tick as a client's is:

| A tick, in milliseconds | Everything moving | One crate pushed | At rest |
|---|---|---|---|
| Box3D's step | 1.1 | 0.03 | 0 |
| Its state into an image | 0.24 | 0.17 | 0 |
| The image into the match, and bodies in and out | 1.3 | 0.39 | 0.04 |
| Keeping and hashing the world | 0.52 | 0.10 | 0.00 |
| Of the image, what changed | 68% | 10% | 0% |

- The step runs on the tick's threads (`tide_job_start`, `tide/jobs.h`) once 512 bodies are awake, and gives the same world on any number of them: 1.3 ms on one thread and 0.5 on twelve, where every crate is awake. The step alone is quicker shared from a couple of hundred bodies, but a tick whose C shares work out runs its systems on threads too, and under about 500 bodies that costs more than the step saves.
- The image is every body, shape and contact, about a kilobyte and a half a body, whatever is awake. Box3D writes it straight into memory the match's list is then compared with, chunk by chunk (`out List<byte>`), so only what changed is copied and hashed.
- What's left, with everything moving, is mostly that: each chunk of the image that changed gets a page of its own, which the tick before's snapshot can't share.
- Making C's world from an image, which a rollback does once, takes about a millisecond.
- On the web Box3D has no SIMD (it only knows Emscripten's WebAssembly): the same step takes about 2.2 ms there on one thread, and 0.8 shared out over the 24 threads a page gets on a machine of 12 cores, one for each thread its cores run (measured under Node; a tick, 3.2 ms against 3.9 on one thread). Box3D's workers wait for each stage of a step in a loop, which on the web holds a processor for as long as it goes on, so one that has waited long sleeps there until the next stage (`box3d/TIDE.md`): before that, the same step took 2.2 ms on those 24 threads, and far longer on a machine with anything else to do. Half as many threads are still quicker (0.7), as the solver gains nothing from a core's second thread.
- Compiling Box3D takes about 8 seconds of one core once (14 optimized), and adds about 800 KB to a game.

## Not there yet

- Hulls other than boxes, meshes, height fields, compounds, joints, ray casts and overlap tests. Box3D's image holds meshes, height fields and joints already, and `Physics.geometry` what they're made of; a world with a compound has no image, as a compound's bytes aren't the same on every machine.
- A hull no shape has any more stays in `Physics.geometry`: a game that makes boxes of ever new sizes keeps them all.
- A body's `type` and the fields read when it's made don't follow later changes, and a shape's don't either.
- Removing `Body` from an entity that stays leaves its body in the world.
- Reading the world from views.
