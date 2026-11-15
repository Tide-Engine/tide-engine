---
description: "Build an endless world of blocks in Tide: chunks as entities with grids, each player sent only the chunks near them, dimensions as scenes, the server's store for what nobody is near, read off the tick's thread and listed by prefix, and far ground drawn from the seed."
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
}

// On each chunk: who wants it
component Seen
{
    VisibilityGroup group;
    Audience audience;
    int emptied;            // The tick its last player left
}
```

Both are restricted to `Audience.None` where they're added (`visibility.Set<Streamer>(Audience.None)`). A player's moves tell their dimension which chunk they're in, and its handler adds them to the chunks their box of chunks took in and removes them from those it left. A chunk nobody has yet is spawned with an audience of nobody, and the group's audience is set once its blocks are made, so no player ever has a chunk that isn't.

The server finds its chunks in `Streamer.chunks`, not with a query: a spawn joins the tables only once its tick's changes are applied, so two players who come near one place in one tick would otherwise make two chunks there. `Spawn` gives the entity at once.

## Making chunks on every core

A system that fills each new chunk from its dimension's seed, and then shows it to the players waiting for it, runs across every core, in a game that hides state as in any other ([The schedule](./schedule.md#splitting-across-threads)):

```csharp
system Generate(mut Chunk chunk, mut Seen seen)
{
    if (chunk.stage != Stage.Missing) return;
    // ...the chunk's blocks, from Math.Perlin of the seed
    chunk.stage = Stage.Made;
    Visibility.Set(seen.group, seen.audience);
}
```

Each chunk's blocks are its own, and the groups' audiences its rows set are set after them, in the chunks' order, as one thread would. Making or ending groups in the same system keeps it on one thread, so a game makes a chunk's group where it spawns the chunk.

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

`Idle` writes nothing while it waits: `emptied` is a tick, not a counter. A system that counts in every chunk every tick changes every chunk every tick, and the server's checks cost what changes.

The server reads and writes the chunks' files on a thread of their own, after the tick that asked: a player's first view of fresh ground asks for hundreds of chunks in one tick, and the tick doesn't wait for the disk. Their answers come over the next ticks, 64 a tick.

What a dimension has saved can be listed, a page of keys at a time, for a map of where players built or to move a world's saves elsewhere: `Storage.List($"chunks/{dimension.number}/", this)` answers with a `Listed` event, and the next page is asked for after its last key ([Saving the match](./files.md#saving-the-match)). Keys made of folders (`chunks/<dimension>/...`) keep a listing to the folder its prefix names.

## Drawing what's far

A chunk's faces are made into a mesh by a `local worker`, off the frame, and kept in the chunk's `local` fields ([Local state](./local-state.md), [Async and tasks](./tasks.md)); they're made again when its blocks change. Meshes outside the view aren't drawn ([frustum culling](./3d.md#frustum-culling)).

Past the chunks a player has, the ground can be drawn from the dimension's seed alone: every machine has the seed, so the same hills the server would make are drawn there with nothing sent for them. What players built far away isn't in it; a coarse summary of each far region, on entities with a wider audience than the chunks', is how a game would send that.

## What it costs

`bench/voxels`, 4 bots flying across fresh terrain with 845 chunks each, 60 ticks a second, 50 ms each way: the server's update takes 0.37 ms (median), each bot is sent about 20 KB a second, and a bot's machine holds the 850 entities near it. Standing still, the server spends 1 ms a second checking what each player has. With its store as files, read on the files' own thread, the server's longest update is about 7 ms where it was 15 with them read as each tick ended. The engine's notes (`notes/voxels.md`) have the rest, and what's still missing.
