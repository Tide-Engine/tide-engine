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

`async` before a function lets it wait. Its declaration names the completion value: `async int`, `async string`, or `async void` for nothing. Calling it returns a started task handle.

```csharp
async int Doubled(int x)
{
    await Wait.Ticks(1);
    return x * 2;
}
```

A call can be awaited immediately, kept, or discarded:

- **`await` it** for its value, from async code: `var n = await Doubled(3);`. The code waits there until the call is done, then goes on with its value. `await` binds like `try`, so `await Doubled(3) + 1` adds 1 to the value.
- **Keep its handle**: `var build = BuildMesh(voxels);`. The operation has already started. `await build` gets its result; `build.Cancel()` requests cancellation.
- **Call it as a statement** to start a task: `Doubled(3);`. The task goes on by itself, and the code that started it doesn't wait. A started task's value is dropped.

Match calls produce `Task<T>`; local calls and local workers produce `LocalTask<T>`. Void uses `Task` or `LocalTask`. The type preserves the complete declared result: an `async MeshData Build(...) fails MeshError` call produces `Task<MeshData fails MeshError>` or `LocalTask<MeshData fails MeshError>` in its world. Optional results use `Task<T?>` or `LocalTask<T?>`.

Copied handles name the same operation. Multiple tasks can await it, and repeated awaits get independent ordinary value copies of the same result. Awaiting a handle never restarts its operation.

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

`await` waits for a task handle or an immediate async call. These engine operations also return started handles, so a timer can be kept and awaited later:

| | |
|---|---|
| `await Wait.Ticks(n)` | `n` of the match's ticks. Match code only. |
| `await Wait.Frames(n)` | `n` of this machine's frames. Local code only. |
| `await Wait.Seconds(s)` | `s` seconds: in the match, the nearest whole number of ticks (at least one), the same on every machine; in local code, this machine's time. |

A wait of 0, or less, doesn't wait at all.

For now:

- Methods, extern functions and functions that take an `Action` can't be async.
- `await` can't go in a block written after a call (an `Action`).
- Tasks can't draw, or read this frame's `Devices`: that's what views do, every frame.
- The input's `Sample`, `Sanitize` and `Predict`, frags and verts, operators and `Interpolate` start no tasks. A plain function or method does, as the code that calls it could: see [Starting a task](../language/functions.md#starting-a-task).
- Awaiting an event isn't supported yet. An event can start an async handler, which then waits for time or other tasks.

`Storage.Flush()`, `Web.Fetch(url)` and `request.Send()` follow the same handle model. For example, `LocalTask<void fails IOError> flush = Storage.Flush();` starts one flush; `try await flush;` handles its eventual result. See [Files](./files.md) for the provider and storage APIs.

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

Immediate nested awaits can share a private frame. A kept handle identifies a separate started operation; its existing entity/world ownership still applies. An async function can't immediately await itself, as its private frame would hold itself; it can start another operation and keep its handle.

## Cancellation and lifetime

`build.Cancel()` cancels that operation and ends every task awaiting it. Cancellation ends an awaiting continuation without inventing a success value or a declared error. Cancelling one awaiting task preserves a shared build for its other consumers.

`isRunning` is true while an operation is queued, running or waiting. `isCompleted` includes cancellation; `isCancelled` also describes default or stale handles. Cancelling a completed or stale handle has no effect. Old handles never refer to a newer operation that reused its storage. Equality compares operation identity.

Running Tide code stops cooperatively at generated safe points in loops, statements and helper calls. Trusted C finishes its current call. An unused worker can be abandoned once no retained handle or awaiting task needs it; discarded ordinary async calls continue by themselves. Completed results are retained while reachable handles need them and reclaimed afterward, including unreachable cycles.

Local saves and compatible reloads preserve handles and cached results. A task dropped by an incompatible reload leaves cancelled handles. Local jobs restart from saved owned inputs; native callbacks and provider pointers aren't saved. Match handles, results and cancellation remain deterministic match state. Local code can inspect a match handle's status; cancelling it belongs to match code, usually through a command.

Match handles aren't supported in sharded games yet. Local handles and workers,
and existing directly awaited or discarded match async calls, remain available.
Distributed task identity, result retention and cancellation need their own
ownership contract.

With per-recipient visibility, match tasks remain authoritative server work.
Their registries, continuations and cached results stay private. A projected
client cannot start a retained match operation, and a published handle whose
operation is absent there is stale. Publish permitted ordinary progress and
results for views.

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
asset reference/borrow or callable/shader binding. Pass the CPU data it needs.
Its helpers have the same restrictions: no live world, drawing, frame services,
file/network operations, task starts or implicit match randomness. C remains trusted; C called by workers
must be safe to run on their threads.

A worker can also take read-only `Grid2<T>` and `Grid3<T>` snapshots:

```csharp
local worker List<int3> FindSolidCells(Grid3<byte> voxels)
{
    mut List<int3> cells = [];
    foreach (var at in voxels)
        if (voxels[at] != 0) cells.Add(at);
    return cells;
}
```

Await it with a grid field or grid asset. Capture copies its directory and
existing chunks before dispatch; later source edits or pack replacement cannot
change the worker's input. Capture costs time on the calling thread, so pass a
chunk's grid rather than a whole voxel world's when possible. Ordinary helpers
can read and forward the snapshot. Grid parameters stay read-only, including
through those helpers; grid locals and return values remain unsupported.
GPU `Sample` operations remain unavailable in workers.
Arguments are captured in their written order. If a later argument changes the
source or awaits another task, an earlier grid capture keeps its original cells.

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
waits for all steps. Ordinary `for` and list `foreach` loops remain ordered.
Grid `foreach` uses the existing independence analysis inside workers too;
appending to an output list or reducing a total keeps the loop ordered. Explicit
list-parallel steps can read the captured grids while writing their own output.
Jobs and their parallel steps share the host's bounded pool. With no background
provider, a worker runs synchronously when polled, so expensive work can still
stall a frame on a host without threads. Small parallel loops run serially.

Local code can start a worker and keep its `LocalTask<T>` handle; only local
async code awaits its result. Bare `worker` is reserved for a future match
contract and is currently an error. An owner disappearing drops its
continuation. Abandoned queued calculations are skipped, and running Tide
calculations stop cooperatively. Reload drains old callbacks before unloading
their code; trusted C must return before that drain can finish.
Saved or retained waiting tasks restart their pending worker after restoration
or reload. No worker state enters match snapshots, hashes or networking.

## In C

Hosts tell local code how long each frame is, before `tide_frame`: `tide_local_frame_time(local, seconds)`, which local tasks' waits count down and `Frame.dt` reads. `tide/run.h` does. Under `tide run`, a waiting task carries over to a new build when its code is the same; when it changed, the task is dropped, and tide says so.

Custom hosts set `gui.jobs` to their `tide_jobs` provider before `tide_frame`
to enable local background workers. The standard host supplies
`tide_platform_jobs()`. Providers implement the optional `background` and
`help` callbacks from `tide/jobs.h`; `tide/worker.h` describes item ownership
and completion. A zeroed GUI retains the synchronous fallback.

`gui.worker_limits` optionally supplies `tide_worker_limits`. Zero fields use
the current defaults: 64 pending private jobs, 64 MiB of retained input
captures, and 4 MiB of capture/result transfers per frame. Direct worker awaits
wait for admission before evaluating their arguments. Nested awaited arguments
don't hold a reservation while waiting for another worker.

Transfer limits are soft: one transfer that crosses the budget finishes, then
later transfers wait for another frame. This lets an oversized job make
progress. Started handle calls evaluate and snapshot their arguments when
called; their saved async input frames are outside the private-job capture
limit. These controls aren't a hard per-job or process memory cap. Feed very
large bursts from an async queue when input snapshots themselves are costly.

The profiler reports worker compute and queue wait per completed job, with
input-capture and result-transfer totals. Frame-thread copying remains part of
the frame's cost; worker compute runs on the background pool.
