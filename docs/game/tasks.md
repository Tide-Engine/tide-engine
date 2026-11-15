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
| `await Storage.Load(key)` | What the [server's store](./files.md#waiting-for-the-answer-in-a-task) keeps under the key, as a `List<byte>`, failing with `IOError`. Match code only: the task goes on in the tick the answer is given to, on every machine. |
| `await Storage.List(prefix)`, `(prefix, after)` | The store's keys that start with the prefix, a page of them, as a `StorageKeys`, failing with `IOError`. Match code only. |

A wait of 0, or less, doesn't wait at all. The store's two have no handle to keep: without `await`, they're answered by a `Loaded` or `Listed` event.

For now:

- Methods, extern functions and functions that take an `Action` can't be async.
- `await` can't go in a block written after a call (an `Action`).
- Tasks can't draw, or read this frame's `Devices`: that's what views do, every frame.
- The input's `Sample`, `Sanitize` and `Predict`, frags and verts, operators and `Interpolate` start no tasks. A plain function or method does, as the code that calls it could: see [Starting a task](../language/functions.md#starting-a-task).
- Awaiting an event isn't supported yet. An event can start an async handler, which then waits for time or other tasks.

`Storage.Flush()`, `Web.Fetch(url)` and `request.Send()` follow the same handle model. For example, `LocalTask<void fails IOError> flush = Storage.Flush();` starts one flush; `try await flush;` handles its eventual result. See [Files](./files.md) for the provider and storage APIs.

## Where tasks run

A task belongs to a world, like the code that starts it. A system's or a match handler's tasks are the match's: they're part of it, in snapshots, state hashes and rollback, they go to players who join, and they carry on when the match changes hands. A view's or a local handler's tasks are this machine's, in its local state.

An async function's world comes from what it does: changing the match (spawning its entities, changing its components through `mut`, sending its events) makes it match code, and using local state, the [local fields](#local-fields) of the match's components among it, or reading [`Frame.dt`](./local-state.md#time), makes it local. A [function it calls](../language/functions.md#sending-and-spawning) that sends or spawns counts as its own doing. One that does neither runs in whichever world starts it. Doing both is an error, as a task runs in one world.

## What a task keeps

While a task waits, it keeps its locals and its parameters, copied: text, lists and grids too, a grid a `target.Read()` gave and one of an asset's among them. A `foreach` over an asset reads the asset as it is each time round, so a loop that waits a frame per element of `asset List<Grid2<Color>> run` goes on through the files [tide run](../tools/cli.md) loaded again while it waited; the element it held as it waited is its own copy. Its **components and singletons** it gets again each time it goes on, as they are then, so it sees what changed while it waited. They have to be the caller's own, its parameters:

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

**A grid parameter** is a copy of the task's own, made where it's called, as a list parameter is: a system or a view that changes its grid after starting the task doesn't change the task's, and the task reads its copy where it is after every wait, however many, with no copy made again. The pack's grid is copied whole, so a pack [tide run](../tools/cli.md) loads again while the task waits leaves the task's copy as it was. An async function the task awaits with the grid gets a copy of its own too. A match task's copy is part of the match, in snapshots, hashes and what a joining player gets, like its other grids; a local task's is this machine's. A grid parameter can't be `mut`: to change a field's grid, take the component or singleton it's in.

```csharp
singleton Field
{
    Grid2<int> cells = Grid2(64, 64);
    bool watching;
    int changed;
}

int Differences(Grid2<int> now, Grid2<int> before)
{
    mut var count = 0;
    foreach (var at in now) count += now[at] != before[at] ? 1 : 0;
    return count;
}

async void Settle(mut Field field, Grid2<int> before)
{
    await Wait.Ticks(30);
    field.changed = Differences(field.cells, before); // The cells now, against the copy as they were 30 ticks ago
}

system Watch(mut Field field)
{
    if (field.watching) return;
    field.watching = true;
    Settle(field, field.cells);
}
```

### Local fields

A local task reads and writes the [local fields](./local-state.md#local-fields) of the match components and singletons it takes, as a view does. They come with the component each time the task goes on, from where this machine keeps the entity's, so the task sees what views wrote while it waited, and what it writes is there for the next frame's views:

```csharp
component Chunk
{
    float3 corner;
    Grid3<Block> blocks = Grid3(16, 16, 16);
    int edits;
    local Mesh<Vertex3> mesh;
    local int drawn;        // The edits its mesh was made at, plus one
    local LocalTask job;    // The task making it, while it does
}

async void Remesh(mut Chunk chunk, int version)
{
    var built = await Faces(chunk.blocks); // A local worker
    chunk.mesh = Mesh(built.vertices, built.indices);
    chunk.drawn = version;
}

view Chunks(mut Chunk chunk)
{
    if (!chunk.job.isRunning && chunk.drawn != chunk.edits + 1) chunk.job = Remesh(chunk, chunk.edits + 1);
    if (chunk.drawn > 0) Draw.Mesh(chunk.mesh, float4x4.Translate(chunk.corner));
}
```

- **It's this machine's task.** Using local fields makes an async function local, so a view or a local handler starts it, never a system. `mut` on a match component lets it write the local fields alone: a task that also changes the component's match fields is an error, as it would run in both worlds.
- **The view and the task share them.** What the view wrote before it started the task, the task reads, and the other way round. They're the entity's own from then on.
- **Handing it the component changes nothing of the match.** A view that starts the task with its `mut Chunk chunk` changes the chunk's local fields, never its match fields, so a loop in that view over the other chunks that skips them by a match field of its own chunk, like `if (row.other.at.x != chunk.at.x + 1) continue;`, is still an [indexed loop](./systems.md#only-the-rows-that-can-pass), and it can hand the task the neighbour's grid, `Remesh(chunk, row.other.blocks)`, which is the task's own copy. The same goes for a function the view hands it to that writes its local fields alone. A function written in C, or one that hands the component on to C, counts as changing all of it: what C writes isn't the compiler's to know.
- **It ends with the entity**, once the world this machine draws no longer has it, and the entity's local fields go once neither that world nor the one the server confirmed has it. So a task never writes for an entity that's gone. If the server undoes a destroy this machine predicted, the local fields come back as the task left them, but the task doesn't. That's why the example keeps the task's handle rather than a flag the task clears: `job.isRunning` is false once the task has ended, however it ended, so the view starts another.
- **Hidden or without the component**, the same: an entity this machine's player can no longer see is gone, with its local fields and its task, and comes back with its defaults; a task that takes a component the entity loses ends at the next frame, whether it was waiting or due to go on, and that component's local fields go. If the component comes back, it comes with new local fields and no task.
- **Rollback and reloads** leave them as they are: a task that goes on after a correction reads its component as corrected, and its local fields as this machine left them. A task that carries over a hot reload finds them where the new build keeps them.

## When tasks go on, and when they end

Tasks whose time has come go on at the end of the tick (or frame), after its changes and events, in the order they started; what they change, spawn and send then applies too. A task ends when its code does, and:

- **with what started it**: a task started by code that runs for an entity belongs to that entity, and ends when the entity is destroyed. A task started by a task belongs to the same entity, and one a function starts belongs to what the code that called the function runs for. Code that runs for no entity starts tasks that belong to the world, which last as long as it does.
- **without its components**: a task whose entity no longer has the components it takes ends.
- **with its match**, for a local task of one of the match's entities, or one that takes the match's components or singletons: once this machine leaves the match, it ends at the next frame, even if the next match has an entity with the same handle.

Immediate nested awaits can share a private frame. A kept handle identifies a separate started operation; its existing entity/world ownership still applies. An async function can't immediately await itself, as its private frame would hold itself; it can start another operation and keep its handle.

## Cancellation and lifetime

`build.Cancel()` cancels that operation and ends every task awaiting it. Cancellation ends an awaiting continuation without inventing a success value or a declared error. Cancelling one awaiting task preserves a shared build for its other consumers. A task that ends while it waits, cancelled or with its owner, lets go of what it waited for at once: a file's or a fetch's, and a GPU read's, whose slot and pixels go free for the next read.

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
and schedule the visible chunks first. This example uses a local singleton; for
one of the match's entities, the task can keep the result in the entity's
[local fields](#local-fields) instead.

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
stall a frame on a host without threads. Small parallel loops run serially:
small is the steps' reckoned work in all, counting the functions they call, so
a short loop of costly steps still shares out.

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
