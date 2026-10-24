---
description: "Write Tide async functions and tasks that wait for time, ticks, frames or other async calls, in the match or local presentation."
---

# Async and tasks

Some code takes longer than a tick: a countdown before a round, an ability that ends after a moment, a sign-in that waits for a service. An `async` function can wait, with `await`, and pick up where it stopped. Calling one starts a **task**, which goes on by itself, tick after tick or frame after frame, until it's done.

```csharp
singleton Round
{
    int count;
}

event RoundStarted { }

// Counts down a second at a time, then the round begins.
async event(RoundStarted) Countdown(mut Round round)
{
    for (var i = 3; i > 0; i--)
    {
        round.count = i;
        await Wait.Seconds(1);
    }
    round.count = 0;
}
```

The round below is one task: it counts down a second at a time, lets the runners go for five seconds, and ends. While they run, tap a runner. Each dash is a task of its own, which speeds its runner up, waits, and slows it down again.

::: tide-example tasks A countdown, and a dash

<<< @/examples/tasks.tide

:::

## Async functions

`async` before a function lets it wait. It returns its value like any function, with no `Task<T>` to write: `async int`, `async string`, or `async void` for nothing.

```csharp
async int Doubled(int x)
{
    await Wait.Ticks(1);
    return x * 2;
}
```

A call of an async function is used in one of two ways:

- **`await` it** for its value, from async code: `var n = await Doubled(3);`. The code waits there until the call is done, then goes on with its value. `await` binds like `try`, so `await Doubled(3) + 1` adds 1 to the value.
- **Call it as a statement** to start a task: `Doubled(3);`. The task goes on by itself, and the code that started it doesn't wait. A started task's value is dropped.

Using an async call's value without `await` is an error that says which of the two to write.

An async function that can fail works as any function that fails (see [Errors](../language/errors.md)): `await Fetch(name) ?? "nobody"`, `if (await Fetch(name) is string token) { ... }`, `try await Fetch(name)`, and `await Fetch(name)!`.

## Async handlers

`async` before an event handler makes each event start a task, which can wait like an async function:

```csharp
async event(Spawned) Arm(mut Turret turret)
{
    await Wait.Seconds(2); // Ready two seconds after it's spawned
    turret.armed = true;
}
```

Local handlers can be async too: `local async event(...)` (see [Local state](./local-state.md)). A local handler of one of the match's events can't be yet: it starts a task by calling an async function (see [Events](./events.md#what-isn-t-there-yet)).

Systems and views can't wait, as they run again every tick or frame: they start tasks instead, by calling an async function.

## What tasks wait for

`await` waits for an async function's call, or for one of these:

| | |
|---|---|
| `await Wait.Ticks(n)` | `n` of the match's ticks. Match code only. |
| `await Wait.Frames(n)` | `n` of this machine's frames. Local code only. |
| `await Wait.Seconds(s)` | `s` seconds: in the match, the nearest whole number of ticks (at least one), the same on every machine; in local code, this machine's time. |

A wait of 0, or less, doesn't wait at all.

For now:

- Methods, extern functions and functions that take an `Action` can't be async.
- `await` can't go in a block written after a call (an `Action`), and `Wait` only goes after `await`.
- Tasks can't draw, or read this frame's `Devices`: that's what views do, every frame.
- The input's `Sample`, `Sanitize` and `Predict`, frags and verts, operators and `Interpolate` start no tasks. A plain function or method does, as the code that calls it could: see [Starting a task](../language/functions.md#starting-a-task).
- Awaiting an event and cancelling a task through a handle aren't supported yet. An event can start an async handler, which then waits for time or other async calls.

## Where tasks run

A task belongs to a world, like the code that starts it. A system's or a match handler's tasks are the match's: they're part of it, in snapshots, state hashes and rollback, they go to players who join, and they carry on when the match changes hands. A view's or a local handler's tasks are this machine's, in its local state.

An async function's world comes from what it does: changing the match (spawning its entities, changing its components through `mut`, sending its events) makes it match code, and using local state, or reading [`Frame.dt`](./local-state.md#time), makes it local. A [function it calls](../language/functions.md#sending-and-spawning) that sends or spawns counts as its own doing. One that does neither runs in whichever world starts it. Doing both is an error, as a task runs in one world.

## What a task keeps

While a task waits, it keeps its locals and its parameters, copied. Its **components and singletons** it gets again each time it goes on, as they are then, so it sees what changed while it waited. They have to be the caller's own, its parameters:

```csharp
async void Dash(mut Body body)
{
    body.speed = 10;
    await Wait.Seconds(0.2);
    body.speed = 1; // The entity's Body as it is now
}

system StartDash(mut Body body, Controls controls)
{
    if (controls.dash.down) Dash(body);
}
```

An async function's `mut` parameters are its components and singletons; it can't keep the caller's other variables while it waits, so it returns what it works out instead. For the same reason it has no `out` parameters: a struct returns several values.

## When tasks go on, and when they end

Tasks whose time has come go on at the end of the tick (or frame), after its changes and events, in the order they started; what they change, spawn and send then applies too. A task ends when its code does, and:

- **with what started it**: a task started by code that runs for an entity belongs to that entity, and ends when the entity is destroyed. A task started by a task belongs to the same entity, and one a function starts belongs to what the code that called the function runs for. Code that runs for no entity starts tasks that belong to the world, which last as long as it does.
- **without its components**: a task whose entity no longer has the components it takes ends.

A task waiting for an async call waits in the same place: the call is part of the task, and ends with it. An async function can't await itself, as its task would hold itself; it can start itself again instead, as a task of its own.

## Local workers

Use a `local worker` for expensive CPU calculations such as chunk meshing.
Capture its input from local code, await it from an async function, and publish
the completed result there. The view can keep drawing the previous mesh.

```csharp
struct MeshData
{
    List<Vertex3> vertices;
    List<int> indices;
}

local singleton ChunkVisual
{
    List<byte> voxels;
    MeshData mesh;
    int generation;
    bool building;
}

local worker MeshData BuildMesh(List<byte> voxels)
{
    return GreedyMesh(voxels); // The game's ordinary calculation helper
}

async void Rebuild(mut ChunkVisual visual)
{
    visual.building = true;
    var generation = visual.generation;
    var mesh = await BuildMesh(visual.voxels);
    if (visual.generation == generation)
        visual.mesh = mesh;
    visual.building = false;
}
```

Start `Rebuild(visual)` from a view or local handler when the input changes and
`building` is false. Advance the local generation whenever that meshing input
changes, including neighboring boundaries and rollback corrections; discard an
outdated result and request the latest input. For many chunks, keep a local queue
and schedule the visible chunks first. This example uses a local singleton;
local tasks retain their existing component and local-field access rules.

Workers receive independent copies of their inputs and return independent
values. Numbers, text, lists, maps, sets, ordinary structs, fixed arrays and
optional/error results follow their normal value rules. A worker takes no
`mut`, `out`, component/singleton binding, World, query, devices, resource handle,
asset borrow, callable/shader binding or grid. Pass the CPU data it needs.
Its helpers have the same restrictions: no live world, drawing, frame services,
file/network operations,
task starts or implicit match randomness. C remains trusted; C called by workers
must be safe to run on their threads.

Loop semantics stay the same. Workers additionally permit `parallel` over
their own lists of plain values, including local variables:

```csharp
local worker List<float3> NormalizePoints(List<float3> points)
{
    mut List<float3> result = points;
    parallel (var i in result)
    {
        result[i] = Math.Normalize(result[i]);
    }
    return result;
}
```

Each step changes only its own output element or block. Code after the loop
waits for all steps. Ordinary `for` and list `foreach` loops remain ordered;
automatic grid-`foreach` analysis is unchanged, and workers do not take grids.
Jobs and their parallel steps share the host's bounded pool. With no background
provider, a worker runs synchronously when polled, so expensive work can still
stall a frame on a host without threads. Small parallel loops run serially.

Only local async code awaits workers; bare `worker` is reserved for a future
match contract and is currently an error. An owner disappearing drops its
continuation; a running calculation can finish privately and its output is
discarded. Reload drains old calculations before their code is unloaded.
Saved or retained waiting tasks restart their pending worker after restoration
or reload. No worker state enters match snapshots, hashes or networking.

## In C

Hosts tell local code how long each frame is, before `tide_frame`: `tide_local_frame_time(local, seconds)`, which local tasks' waits count down and `Frame.dt` reads. `tide/run.h` does. Under `tide run`, a waiting task carries over to a new build when its code is the same; when it changed, the task is dropped, and tide says so.

Custom hosts set `gui.jobs` to their `tide_jobs` provider before `tide_frame`
to enable local background workers. The standard host supplies
`tide_platform_jobs()`. Providers implement the optional `background` and
`help` callbacks from `tide/jobs.h`; `tide/worker.h` describes item ownership
and completion. A zeroed GUI retains the synchronous fallback.
