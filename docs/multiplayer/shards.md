---
description: "Split one Tide match world across servers by position, for worlds bigger than one server holds, without changing the game's code."
---

# Shards

A match is one world on one server. A world too big for that, such as a shared canvas with thousands of players or a planet, can be split by position into regions, each simulated by its own server: a **shard**. The game is still written as one world. Entities move between shards as they move through the world, and code that reaches across a border still works.

This is new, and its names and details may change ([spec](../spec.md#shards)).

## Turning it on

```csharp
settings
{
    shards = true;
    shardRange = 64;
}

component Avatar
{
    [Partition] float2 position;
}
```

`shardRange` is how far, in world units, the game's code looks around an entity: sight, reach, a query for what's nearby. A shard keeps read-only copies of its neighbours' entities that far past its border, so that code sees them. Regions are never narrower than twice the range.

`[Partition]` marks the field that decides where an entity lives: one `float2`, `float3`, `double2` or `double3` field, on one component. Without it, the world position of an entity's `Transform` decides. An entity in a `Transform` hierarchy lives with its root. An entity without a position goes with the entity whose code spawned it: a bag spawned by a player's avatar follows the avatar from shard to shard.

## What changes in the game

Mostly nothing. Systems and handlers run for the entities a shard owns, on that shard. Queries and `world.Get` also read the copies near the border, a tick or two behind.

When code changes something its shard doesn't own, such as an entity across the border or a singleton, the change is not made from a stale copy. Instead the code waits: the tick runs again without it, the shard borrows what the code touched, and the code runs once, in full, with fresh state, on the first tick it can. A trade between two players either side of a border happens once or not at all, and nothing is duplicated or lost. An event sent to an entity on another shard runs there.

A game can tell when this happens:

```csharp
event(Trade trade) Settle(mut Wallet wallet)
{
    if (Shards.delay > 0)
    {
        // This ran Shards.delay ticks late, waiting for a border
    }
}
```

`Shards.Owns(entity)` is whether this shard owns an entity now. In a game that isn't sharded, it's always true and `Shards.delay` is always 0.

A split world doesn't give bit-identical results to the same game on one server, because results depend on when messages crossed between shards. Each shard is still deterministic and checked by its clients every tick.

## Players

Players join the world at its first shard, which sends `PlayerJoined`. A player's machine plays in the shard that owns their entities, and also follows the shards near them. When their avatar walks across a border, the machine switches to the next shard at once, with nothing to download.

## Running one

`tide run` runs every shard of a sharded game's match on this machine. To try borders out, split it into regions along x:

```sh
tide run --shards 2 --shard-width 100
```

`--shard-width` is each region's width in world units, 16 times `shardRange` without it.

A [dedicated server](./matches.md#dedicated-servers) runs each shard as one of its matches:

```sh
tide run --server --anonymous --shards 4 --shard-width 200 --shard-most 5000
```

`--shard-most` splits a region in two when its shard owns more entities than that, and merges two neighbouring regions back into one when together they own a quarter of it or less. Players connect to match 0, `Session.Connect("host:7777/0")` or through the server's room with `Session.Join("K7QF2M/0")`, and the server takes them into the world. Each player follows the shards near their avatar over that one connection, on the web too.

Tide's hosting service will run shards across machines, splitting and merging regions by load.

## Limits for now

- Physics state doesn't move with its entities.
- Visibility and hot reload don't apply to sharded games yet.
