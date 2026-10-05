# Box2D

[Box2D](https://box2d.org), Erin Catto's 2D physics engine, for a Tide match: bodies and their shapes are components, every tick steps them, and the match stays the same on every machine, through rollbacks, joins and reloads.

An experiment: what's here works and is tested, and what it costs is below. Its names and its shape are not settled.

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

(`test/stack.tide` is a game like this one, which the package's tests run.)

- **`Body`** is a rigid body: `type` (`Static`, `Kinematic`, `Dynamic`), `position`, `angle`, `velocity`, `spin`, and `gravityScale`, `linearDamping`, `angularDamping`, `fixedRotation` and `bullet`, which are read when the body is made. After each step the first four are where Box2D left the body. A system that writes them moves the body: the next step starts from what they say. `impulse` and `turn` are given to the body at the next step, once.
- **`Box`** and **`Circle`** are shapes on the entity's body: `size` or `radius`, `offset`, `density`, `friction`, `bounce`, `sensor`.
- **`Touched`** and **`Parted`** are sent to both entities when their bodies start and stop touching, each with the `other`.
- **`Physics`** is the world: `gravity` and `substeps`.
- An entity that's destroyed takes its body with it.

The package's systems run before the game's, in this order: bodies and shapes that are new are made and what code wrote goes to Box2D, the world steps, and bodies' components get where it left them. So a game's systems read this tick's positions, and what they write is the next step's.

## How it holds a match

Box2D keeps its world in C's memory, outside the match, where rollbacks, joins, host migration and hot reloads don't reach. So after each step the package writes Box2D's whole state into the match, as bytes (`Physics.image`, from Box2D's own snapshot), with a stamp that names it. Before each step, C's world is either at the match's stamp already, or it's made from the image again. Nothing else is needed: a client that predicted wrong, a player who joined, a reloaded build and a new host each find no world at their stamp and get one.

- **Everything that changes Box2D's world goes through the match**, which is why code moves a body by writing its component, and never calls Box2D. The stamp is the stamp before it and everything done to the world since, hashed: Box2D's next state is its last and what's done to it, so the image is never read to name it.
- **The image is in the match's hash**, so it's the same bytes on every machine for the same state, 32-bit web builds and 64-bit desktops alike, whatever their SIMD. Box2D doesn't promise that by itself: `box2d/TIDE.md` lists the changes that took. `tide_box2d_stack` pins the simulation's hash and every tick's image on each platform.
- **The image is in sections that keep their place**, so what changes of it from tick to tick is what changed in the world, and the match's snapshots share the rest. As Box2D writes it, a contact that starts or ends moves every byte after it.
- **A world at rest isn't stepped.** Nothing awake and nothing written to any body: there's no step, no image, and no call into C.

## What it costs

Measured on a release build, 2,400 crates in stacks (an image of 2.5 MB), one thread, with the world kept and hashed every tick as a client's is:

| A tick, in milliseconds | Everything moving | One crate pushed | At rest |
|---|---|---|---|
| Box2D's step | 0.31 | 0.01 | 0 |
| Its state into an image | 0.14 | 0.13 | 0 |
| The image into the match, and bodies in and out | 0.34 | 0.12 | 0.03 |
| Keeping and hashing the world | 0.28 | 0.05 | 0.00 |
| Of the image, what changed | 77% | 7% | 0% |

- The step runs on the tick's threads (`tide_job_start`, `tide/jobs.h`) once 128 bodies are awake, and gives the same world on any number of them: 0.45 ms on one thread and 0.27 on twelve, where every crate is awake.
- The image is every body, shape and contact, about a kilobyte a body, whatever is awake. Box2D writes it straight into memory the match's list is then compared with, chunk by chunk (`out List<byte>`), so only what changed is copied and hashed.
- What the package adds to the step is memory moved, not arithmetic: the image is written (2.5 MB), compared with the last one, what changed of it copied into the match and hashed, and each body's place read back into its component. So it goes by how big the world is, where the step goes by what's awake.
- Making C's world from an image, which a rollback does once, takes about half a millisecond.
- Compiling Box2D takes about 6 seconds once (9 optimized), and adds about 350 KB to a game.

## Not there yet

- Joints, chains, polygons other than boxes, capsules, ray casts and overlap tests.
- A body's `type` and the fields read when it's made don't follow later changes, and a shape's don't either.
- Removing `Body` from an entity that stays leaves its body in the world.
- Reading the world from views.
