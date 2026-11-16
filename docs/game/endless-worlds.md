---
description: "Build an endless world of blocks in Tide: chunks as entities with grids, each player sent only the chunks near them, dimensions as scenes, the server's store for what nobody is near, read off the tick's thread and listed by prefix, chunk faces meshed across their edges, and far ground drawn from the seed with far regions of what players built."
---

# Endless worlds

A world of blocks that goes on as far as players walk, in several dimensions, played together: more than any machine holds at once, and more than any player should be sent. Tide has no world type for it. It's made of what every game has, put together as this page shows: entities, grids, [State visibility](./visibility.md), [Scenes](./scenes.md) and [the server's store](./files.md#saving-the-match).

`bench/voxels` in the engine's repository is such a game, with bots that fly, dig and build across it, and a version people play. The code below is shortened from it. The names of the store's calls and of visibility are provisional.

## Chunks are entities

A chunk is an entity with a grid of its own, found by its place:

```csharp
component Chunk
{
    int3 at;                // In chunks
    int dimension;
    Grid3<Block> blocks = Grid3(16, 16, 16);
    Stage stage;            // Asked for, missing from the store, or made
    int edits;              // Changes players made
}
```

An entity is what the engine already serves one at a time: each player is sent only the entities they may have, `Destroy` unloads one, an event can go to one, and a sharded world splits its entities by place. One grid for a whole world would have none of that.

A chunk is found by its place with an indexed loop: the loop's guard is one key of the place's three numbers and the dimension, and the loop visits only the chunks there ([Systems](./systems.md)).

```csharp
Block At(int dimension, int3 cell, query (Chunk chunk) chunks)
{
    var at = int3(cell.x >> 4, cell.y >> 4, cell.z >> 4);
    mut var found = Block.Air;
    indexed foreach (var row in chunks)
    {
        if (row.chunk.at.x != at.x || row.chunk.at.y != at.y || row.chunk.at.z != at.z) continue;
        if (row.chunk.dimension == dimension) found = row.chunk.blocks[cell.x & 15, cell.y & 15, cell.z & 15];
    }
    return found;
}
```

## Dimensions are scenes

Each dimension is a scene, loaded once, with a seed of its own. A chunk spawned by code that runs for the dimension's entity is in that scene, so unloading a dimension destroys its chunks.

```csharp
scene Dimension
{
    int number;
    int seed;
}
```

## Who has which chunks

Each chunk has a visibility group of its own, whose audience is the players near it. A player is sent the chunks they come near and nothing of the rest, and a player's machine drops a chunk once its group no longer has them.

The code that decides it is the server's alone. It runs for components no player may have, so a player's machine runs none of it and has nothing of it to get wrong:

```csharp
// On each dimension's entity: who is where, and its chunks by their places
component Streamer
{
    Map<PlayerID, int3> watching;
    Map<long, Entity> chunks;
    List<Entity> making;    // Chunks asked for and not yet made, in the order they were asked for
    Map<PlayerID, int> asking;  // Players whose box isn't all asked for yet: how far through it they are
    int asked;              // Chunks spawned this tick
}

// On each chunk: who wants it
component Seen
{
    VisibilityGroup group;
    Audience audience;
    bool shown;             // Made, and its group has its audience
    bool dear;              // It has ground in it: what making it costs
    bool ready;             // The store had nothing for it...
    bool go;                // ...and it may be made this tick (Pace, below)
    int emptied;            // The tick its last player left
}
```

Both are restricted to `Audience.None` where they're added (`visibility.Set<Streamer>(Audience.None)`). A player's moves tell their dimension which chunk they're in, and its handler adds them to the chunks their box of chunks took in and removes them from those it left. A chunk nobody has yet is spawned with an audience of nobody, and the group's audience is set once its blocks are made, so no player ever has a chunk that isn't.

The box is asked for nearest first: a ring of chunks around the player's own at a time, the player's own height before the ones above and below. The store answers in the order it's asked, so what a player stands in and looks at is there before the edge of their view; and around each ring, each chunk beside the one before it, because a player is sent new entities in the order they were spawned and chunks side by side are alike, which makes what's sent smaller.

A whole box isn't spawned in one tick. A view of 96 cells each way is 845 chunks; at twice that it's 3,125 and at three times 6,845, and a tick that spawns them all, each with a group and a load asked of the store, is as long as they are many (9 ms for 3,125). The handler spawns up to an allowance of new chunks for its dimension each tick and remembers how far through the box it got; the player's move in the next tick (which says where they are every tick) goes on from there:

```csharp
event(Want want) Watch(mut Streamer streamer, Worlds worlds, Time time, mut World world)
{
    var known = streamer.watching.TryGet(want.player, out var from);
    var going = streamer.asking.TryGet(want.player, out var gone);
    var moved = !(known && Same(from, want.to));
    if (!moved && !going) return;               // Where they were, with their whole box asked for
    // ...moved: the chunks the old box had and the new one hasn't lose the player
    mut var place = 0;                          // Places of the box gone through, nearest first
    mut var full = false;
    // for each place of the box, nearest first, while !full:
    //     place++; if (place <= (moved ? 0 : gone)) continue;
    //     a chunk that's there: the player joins its audience
    //     none, and streamer.asked < worlds.ask: Spawn it, ask the store for it, streamer.asked += 1
    //     none, and the tick's allowance is spent: place--; full = true;
    if (full) streamer.asking[want.player] = place;
    else streamer.asking.Remove(want.player);
    streamer.watching[want.player] = want.to;
}
```

The allowance costs a player nothing while it's more than the store answers in a tick (256): the box is asked for faster than it can be made, in the same order. `bench/voxels` spawns 1,024 a tick, so the default view is still asked for at once. A chunk that's already there costs none of it: a player who comes where others are joins every chunk's audience in the first tick.

The server finds its chunks in `Streamer.chunks`, not with a query: a spawn joins the tables only once its tick's changes are applied, so two players who come near one place in one tick would otherwise make two chunks there. `Spawn` gives the entity at once.

## Making chunks on every core

A system that fills each new chunk from its dimension's seed, and then shows it to the players waiting for it, runs across every core, in a game that hides state as in any other ([The schedule](./schedule.md#splitting-across-threads)):

```csharp
system Generate(mut Chunk chunk, mut Seen seen)
{
    if (chunk.stage != Stage.Missing || !seen.go) return;
    // ...the chunk's blocks, from Math.Perlin of the seed
    chunk.stage = Stage.Made;
    Visibility.Set(seen.group, seen.audience);
}
```

Each chunk's blocks are its own, and the groups' audiences its rows set are set after them, in the chunks' order, as one thread would. Making or ending groups in the same system keeps it on one thread, so a game makes a chunk's group where it spawns the chunk.

Today the chunks of one box are still made by one thread: a system's entities are handed to threads in the order they were spawned, a range of them each, and chunks spawned together are next to each other, so one thread gets them all (the engine's notes have what would change that). A tick that made every chunk the store answered for would be as long as they are many, so the game says how much a tick makes (`seen.go` above):

```csharp
// On each dimension's entity: the chunks the store had nothing for, in the order they were asked for, up to
// `make` a tick by what each costs. One of air costs 1, one with ground in it (caves' noise, cell by cell) 10
system Pace(mut Streamer streamer, Worlds worlds, mut World world)
{
    mut var left = worlds.make;
    mut var kept = 0;
    for (var i = 0; i < streamer.making.count; i++)
    {
        var chunk = streamer.making[i];
        mut var seen = world.Get<Seen>(chunk);
        if (seen.go || seen.shown) continue;        // Made, or let be: off the line
        if (seen.ready && left > 0)                 // The store answered: nothing kept of it
        {
            left -= seen.dear ? 10 : 1;
            seen.go = true;
            world.Set<Seen>(chunk, seen);
            continue;
        }
        streamer.making[kept] = chunk;              // Still asked for, or over this tick's allowance
        kept++;
    }
    while (streamer.making.count > kept) streamer.making.RemoveAt(streamer.making.count - 1);
}
```

`bench/voxels` makes 480 a tick: 48 chunks with ground, about 7 ms of one thread.

## Keeping what nobody is near

A chunk is asked for from the store as it's spawned, and made from the dimension's seed only if the store has nothing:

```csharp
var made = Spawn(Chunk { at = at, dimension = dimension.number }, visibility, Seen { group = group });
streamer.chunks[KeyOf(at)] = made;
Storage.Load(SavedAs(dimension.number, at), made);

event(Loaded loaded) Fill(mut Chunk chunk, mut Seen seen)
{
    if (chunk.stage != Stage.Asked || loaded.failed) return;   // Unreadable: asked again later, never made over
    if (loaded.found && Grid3<Block>.FromBytes(loaded.bytes) is Grid3<Block> saved)
    {
        chunk.blocks = saved.Copy();
        chunk.stage = Stage.Made;
        Visibility.Set(seen.group, seen.audience);
        return;
    }
    chunk.stage = Stage.Missing;                                // Made from the seed by a system
}
```

A chunk nobody has had for a while goes. It's let go in an event, which runs after the tick's moves, so a player who came back to it in the same tick keeps it:

```csharp
system Idle(Seen seen, Chunk chunk, Time time)
{
    if (seen.audience == Audience.None && chunk.stage == Stage.Made && time.tick - seen.emptied >= LINGER) this.Send(Expire { });
}

event(Expire) Drop(Seen seen, Chunk chunk)
{
    if (seen.audience != Audience.None) return;
    if (chunk.edits > 0) Storage.Save(SavedAs(chunk.dimension, chunk.at), chunk.blocks.ToBytes());
    Visibility.DestroyGroup(seen.group);
    this.Destroy();
}
```

`ToBytes` writes only a grid's chunks that have something in them, and runs of one block short: a chunk of air is a few bytes ([Grids as bytes](../language/grids.md#grids-as-bytes)). A system that writes a chunk players changed into the store every while, as well as when it goes, keeps a server that stops from losing more than that while. A load whose answer never comes (the server stopped first) leaves the chunk waiting: ask again after a while, as `bench/voxels` does.

A box a player left empties in one tick, so all of its chunks would go in one tick, a linger later: saved, forgotten and destroyed, 12 ms for 3,125 of them. Where the handler takes a player out of the box they left, `bench/voxels` gives each tick's allowance of the dimension's emptied chunks an `emptied` a tick later than the ones before (a tick and a count on the dimension's entity), so a box goes over as many ticks as it was asked for in. A border's row of chunks still goes together: spread by where each chunk is instead, every tick would have chunks going, and a tick in which entities come or go costs the server more than one in which none do.

`Idle` writes nothing while it waits: `emptied` is a tick, not a counter. A system that counts in every chunk every tick changes every chunk every tick, and the server's checks cost what changes.

The server reads and writes the chunks' files on a thread of their own, after the tick that asked: a player's first view of fresh ground asks for hundreds of chunks in one tick, and the tick doesn't wait for the disk. Their answers come over the next ticks, 256 a tick in a game that hides state (64 in one that doesn't, whose answers go to every player).

What a dimension has saved can be listed, a page of keys at a time, for a map of where players built or to move a world's saves elsewhere: `Storage.List($"chunks/{dimension.number}/", this)` answers with a `Listed` event, and the next page is asked for after its last key ([Saving the match](./files.md#saving-the-match)). Keys made of folders (`chunks/<dimension>/...`) keep a listing to the folder its prefix names.

A match task can wait for the store instead of handling its events, which keeps a job of several steps in one place. A dimension's map of saved chunks, a page at a time:

```csharp
async void Survey(mut Atlas atlas, int dimension)
{
    mut var after = "";
    mut var going = true;
    while (going)
    {
        var page = await Storage.List($"chunks/{dimension}/", after);
        if (page is StorageKeys keys)
        {
            atlas.saved += keys.keys.count;
            going = keys.more;
            if (going) after = keys.keys[keys.keys.count - 1];
        }
        else going = false; // The store couldn't be listed
    }
}
```

The task goes on in the tick each answer is given to, the same on every machine, and what it waits for is kept in it: a task whose entity goes ends with it, its answer changing nothing, and a rewind asks again for what the rewound tasks wait for ([Waiting for the answer in a task](./files.md#waiting-for-the-answer-in-a-task)).

## Drawing what's far

A chunk's faces are made into a mesh by a `local worker`, off the frame, and kept in the chunk's `local` fields ([Local state](./local-state.md), [Async and tasks](./tasks.md)); they're made again when its blocks change. Meshes outside the view aren't drawn ([frustum culling](./3d.md#frustum-culling)).

### Faces across a chunk's edges

A worker reads only what it's handed. Given a chunk's own grid, everything past its edge reads as air, so every block on its edge has a face there: two faces between every pair of solid chunks, hidden and drawn all the same. Hand it the neighbours' grids too, and those faces aren't made:

```csharp
local worker Built Faces(Grid3<Block> blocks, Grid3<Block> east, Grid3<Block> west, Grid3<Block> above,
                         Grid3<Block> below, Grid3<Block> north, Grid3<Block> south)
{
    // ...for each block, the one beside it, in this chunk or across its edge
    var right = x < 15 ? blocks[int3(x + 1, y, z)] : east[int3(0, y, z)];
    if (Shows(block, right)) Face(vertices, indices, ...);
    // ...and each of the chunk's own sides as one number, for its neighbours
}

async void Remesh(mut Chunk chunk, Grid3<Block> east, Grid3<Block> west, Grid3<Block> above, Grid3<Block> below,
                  Grid3<Block> north, Grid3<Block> south)
{
    var built = await Faces(chunk.blocks, east, west, above, below, north, south);
    chunk.mesh = Mesh(built.vertices, built.indices);
}

// The blocks of the chunk at a place, or no grid's, all air
Grid3<Block> Near(query (Chunk other) chunks, int3 at, int dimension)
{
    indexed foreach (var row in chunks)
    {
        if (row.other.at.x != at.x || row.other.at.y != at.y || row.other.at.z != at.z) continue;
        if (row.other.dimension != dimension) continue;
        return row.other.blocks.Copy();
    }
    return default;
}

view Chunks(mut Chunk chunk, query (Chunk other) chunks)
{
    // ...when its faces are to be made
    var d = chunk.dimension;
    chunk.job = Remesh(chunk, Near(chunks, chunk.at + int3(1, 0, 0), d), Near(chunks, chunk.at - int3(1, 0, 0), d),
                       Near(chunks, chunk.at + int3(0, 1, 0), d), Near(chunks, chunk.at - int3(0, 1, 0), d),
                       Near(chunks, chunk.at + int3(0, 0, 1), d), Near(chunks, chunk.at - int3(0, 0, 1), d));
}
```

The view finds each neighbour by its place, an indexed loop over the chunks it has, and hands the task the neighbours' grids, which are the task's own copies ([Async and tasks](./tasks.md#what-a-task-keeps)): a neighbour that changes while the faces are made doesn't change them. Handing the task its own chunk as `mut` changes only the chunk's local fields, so the loop is still indexed by the chunks' places. Each mesh gives back its chunk's six sides and its neighbours' sides toward it as it read them, each as one number, kept in a map of this machine's. Its neighbours' meshes have been made from blocks of their own time, so whichever of two neighbours is made last compares the two: a neighbour made from another side of this chunk than this mesh's has its faces made again, and so has this chunk where it read another side of the neighbour than the neighbour's mesh was made from. That holds whichever was begun first, and when a side changes and changes back while one of them is being made. A change at the edge makes them again at once; a chunk that came or went, once its other neighbours have stopped coming, since a view's worth of chunks comes over many frames and the faces between them are hidden ones. Chunks that come together read each other's blocks, and each one's faces are made once. A chunk whose side is all air has no faces its neighbour hides: a chunk of sky has sides of air, its coming changes nobody's faces, and a block built under it changes none of its faces. In `bench/voxels` this draws fewer than a third of the triangles it did, with the same picture.

When a player arrives somewhere, hundreds of chunks want faces at once. The chunks around the player go first: each call says its `priority:` ([Async and tasks](./tasks.md#which-call-starts-first)), and the same `Faces` serves them all.

```csharp
// In Remesh, which the view hands a priority
var built = await Faces(chunk.blocks, east, west, above, below, north, south, priority: priority);

// In the view: how far the chunk is from the player's, in chunks
var away = Math.Max(Math.Max(Math.Abs(chunk.at.x - sight.chunk.x), Math.Abs(chunk.at.z - sight.chunk.z)),
                    Math.Abs(chunk.at.y - sight.chunk.y));
var priority = !changed ? WorkerPriority.Background   // Only a neighbour came or went: mostly hidden faces
             : away <= 2 ? WorkerPriority.Urgent : WorkerPriority.Normal;
```

The ground far off (below) is one big mesh, made with `WorkerPriority.Background`: after the chunks' faces, and never so late that it's missing for long, as a background call waits for at most sixteen others to start.

Two things keep that picture whole. A block dug at a chunk's edge uncovers a face of the neighbour's, which only the neighbour's new mesh has: the chunk keeps its own new mesh aside until the neighbour's is made, and the two show in the same frame, rather than a hole into the neighbour for a frame or two. And a chunk that leaves this machine has the faces toward it made again, in a `local event(Forgotten)` handler, which keeps its place in a local field, as the match's fields are gone by then. `Forgotten` comes whichever way the chunk leaves this machine: hidden from its player, or destroyed, which is the only way on the hosting machine, whose views see the whole match.

### What players built far off

Past the chunks a player has, the ground can be drawn from the dimension's seed: every machine has the seed, so the same hills the server would make are drawn there with nothing sent for them. What players built is sent as **far regions**: an entity for each 8 by 8 chunk columns, with a summary of each column of 8 by 8 cells that players changed, its highest block and how high it is.

```csharp
struct Column
{
    byte at;                // In its region
    Block top;
    int height;
}

component Far
{
    int2 at;
    int dimension;
    List<Column> columns;   // Only those unlike the seed's
    int version;            // Counts the times they changed
}
```

A far region is an entity of its own, not a part of its chunks, because its audience is wider than theirs: the players within its far distance (768 cells in `bench/voxels`, where the chunks go out about a hundred). A component can be restricted further than its entity, never wider. Each has a visibility group, kept as a chunk's is, whose audience is set only while it has columns, so a world nobody built in sends nobody anything for it. The server's side is in components restricted to nobody, as the chunks' is: what the region is as it changes, and who wants it.

The server keeps the summaries as players change chunks. A handler of the change that only the server runs, declared after the one that changes the block, tells the region the top of the chunk's part of that column; the region works out the column's top from it (higher where something was built above it, looked for below where it was dug out), and keeps it only where it isn't what the seed makes there:

```csharp
// ...after Apply, which changes the block: handlers of one event run in the order they're declared
event(Change change) Note(Chunk chunk, mut Seen seen, query (Far far, Summary summary) regions)
{
    // The quarter of the chunk the cell is in, whose top goes to its far region as a Top event
    Summarise(chunk, seen, (change.cell.z & 15) / 8 * 2 + (change.cell.x & 15) / 8, regions);
}
```

What changed goes to players at a pace, so building doesn't flood anyone. A region whose columns changed goes into its dimension's line once (`queued`), and every half second each dimension sends the regions in its line, in the order they changed, 64 columns at most in all: a player far off gets at most two corrections a second of it, however much is built.

```csharp
system Publish(mut Streamer streamer, Worlds worlds, Time time, mut World world)
{
    if (streamer.line.count == 0 || time.tick % worlds.gap != 0) return;
    mut var budget = worlds.pace;
    while (streamer.line.count > 0 && budget > 0)
    {
        var region = streamer.line[0];
        mut var done = true;
        world.Edit<Summary>(region, summary), world.Edit<Far>(region, far), world.Edit<Seen>(region, seen)
        {
            // Far.columns, a step nearer the server's own: `all` once they're alike
            var changes = Follow(far.columns, summary.columns, budget, out var all);
            budget -= changes;
            if (changes > 0) far.version += 1;
            done = all;
            if (all) summary.queued = false;    // Into the line again at its next change
            // Shown once it has columns; hidden once it has none left to send, not while a removal has used
            // the last of the budget before an addition
            var shown = far.columns.count > 0 || (seen.shown && !all);
            if (shown != seen.shown)
            {
                seen.shown = shown;
                Visibility.Set(seen.group, shown ? seen.audience : Audience.None);
            }
        }
        if (!done) break;
        streamer.line.RemoveAt(0);
    }
}
```

A far region comes and goes as chunks do, a while after nobody is within its distance, and is kept in the server's store: `Storage.Save($"far/{dimension}/{at.x}_{at.y}", Json.Write(summary.columns).ToBytes())`, read back with `Json.Read<List<Column>>(loaded.bytes)`. It's saved apart from its chunks, so a server that stops between the two saves can leave a region with a build its chunk didn't keep: a region read back looks again at the chunks the server has, and a chunk made from the seed tells its region what it has where the region has a column, so what the region says comes back to what the chunks have. A player's machine draws a column where a far region it has gives one, in place of the seed's square, with walls down to the seed's ground around it, and the seed's ground everywhere else; where the player's own chunks are, it draws only what's above or below them.

## What it costs

`bench/voxels` on one machine (a 12-core desktop, one core of it: the tick is one thread's work), 60 ticks a second, 50 ms each way with a datagram in a hundred lost, bots that fly, dig, build and change dimension every 20 seconds (`notes/voxels.md`, At scale, has every table and how each was measured; 2026-11-16):

| | 8 players, 845 chunks each | 3,125 chunks each | 6,845 chunks each | 32 players | 32, far apart | 64 players |
|---|---|---|---|---|---|---|
| Chunks the server holds | 6,490 | 16,941 | 32,827 | 12,669 | 28,549 | 17,598 |
| Server's update, median / 99th percentile / longest, ms | 0.28 / 2.9 / 6 | 0.70 / 7.6 / 12 | 1.4 / 10.5 / 20 | 1.5 / 8.2 / 15 | 5.4 / 16.5 / 24 | 3.6 / 16.2 / 28 |
| Server, ms of every second | 28 | 66 | 119 | 104 | 300 | 214 |
| Sent to each player, KB a second | 22 | 57 | 116 | 25 | 24 | 28 |
| A player's machine's ticks, 99th percentile, ms | 0.9 | 2.5 | 4.4 | 1.4 | 1.4 | 1.9 |
| A whole view on a player's machine after a dimension hop, ticks | 25 | 43 | 52 | 26 | 30 | 24 |

(The first three columns and 32 far apart are of the build that asks for a box over a few ticks and packs a whole view off the tick's thread, on every core; the two others are of the build before it.)

- **What holds 60 ticks a second**: 32 players at the default view (96 cells each way) with room, 64 at the edge of a frame when they start together; 8 players at twice the radius with room, and at three times but for its longest updates (20 ms: the tick a player joins or leaves every chunk of a view others are in). Players far apart, who share no chunk, cost the server more than players together: what it pays goes with the chunks it holds. 32 apart hold 60 Hz while nobody hops (11.6 ms at the 99th percentile). Threads change none of it.
- **A player is sent** 17 KB a second flying over fresh ground, 22 to 25 with a dimension hop every 20 seconds, 5 standing, whatever the number of players. A chunk is about 140 bytes as sent, a thirtieth of its cells. Players who turn every tick, as a mouse does, add 8 to 19 KB a second to what each is sent.
- **A whole view arrives in a third of a second**: one player hopping into a dimension nobody is in has its 845 chunks 20 ticks after the hop (21 with a datagram in a hundred lost, 29 with five), nearest first; at twice the radius, 40. Seven of those ticks are the player's own lead on the server.
- **A dimension nobody is in costs nothing**, and eight players a dimension each cost what eight far apart in one do. Leaving a dimension frees its chunks once nobody has been near them for the linger; the server's memory stays at the most it held.
- **A long session is level**: 8 players flying outward for 20 minutes hold 9,000 chunks and far regions in 24 MB from the fourth minute on, the server's update 1.2 ms on average throughout.
- **A player's own frame** at 1920 by 1080, paced at 60 Hz, takes 0.7 ms of CPU at the default view of the version to play (405 chunks) and 2.1 ms with 3,125. The chunks around the player have their faces 0.12 s after a hop at any view, and all of them are there after 0.15 s (405 chunks), 0.45 s (1,445) and 1 s (3,125).
- **What breaks first**: the one thread for what isn't chunks being made. A tick in which a player joins or leaves the audiences of a whole view of chunks that are already there is 18 ms for 6,845 of them, and what a player is sent of a whole view is read on the tick's thread, 2 to 4 ms a view (its packing is another thread's, and a view of more than 4 MB goes out a tick later for it). Eight players who hop together have the last tenth of their views a few ticks later than one who hops alone: the server starts as many whole views in an update as fit in it.

The engine's notes (`notes/voxels.md`) have the rest, and what's still missing.
