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

A package can mark a `[Partition]` of its own, as the physics packages do on `Body.position`, so a body goes where it is. An entity with the game's `[Partition]` component goes by that, and one with a package's by the package's. Every `[Partition]` in a game has the same number of axes.

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

### Keeping the world

A dedicated server can keep its world in a folder, so a crash or a restart doesn't lose it:

```sh
tide run --server --anonymous --shards 4 --shard-journal world
```

It writes a checkpoint now and then, and every tick of every shard since, to the disk before anything one shard sends reaches another. Started again with the same folder, it goes on where the world was, to the tick, and nothing that crossed a border is lost or happens twice. Players have 20 seconds to reconnect as themselves before the world sends `PlayerLeft`.

Tide's hosting service runs a sharded game's world across machines. It splits a region whose shard is steadily too busy, merges quiet neighbours back, and starts a shard whose machine fails again elsewhere, where it was.

## Physics

[Box3D](../packages/box3d.md) and [Box2D](../packages/box2d.md) work in a sharded world. Each shard steps its own physics world of the bodies it owns. A body that crosses a border is made anew on the next shard from its `Body`, where it was and as fast. Bodies another shard owns near the border are there too, as kinematic copies where their owner last had them, so a crate runs into a wall across the border instead of passing through it. Each side tells its own entities of a touch or a hit.

The physics settings, such as `gravity`, are each shard's, as its world is. Set them from a system that runs on every shard: one for no entity that takes `Physics`.

```csharp
[Before(PhysicsStages.Step)]
system Moon(mut Physics physics)
{
    physics.gravity = float3(0, -1.6, 0);
}
```

A joint holds two bodies only while one shard owns both, so keep jointed bodies together, in one `Transform` tree for instance.

## A game's own C state

A C library that keeps state for the entities a shard has, as the physics packages do, declares where that state lives:

- `[PerShard] singleton` is a singleton each shard has its own of. Nothing copies it to other shards or borrows it. Code for no entity that takes one runs on every shard, for that shard; other code for no entity runs once, on the first shard.
- `[PerShard]` on a component's number or bool field marks something that means something on one shard alone, like a handle into a C library. It's 0 wherever its entity arrives or is copied, so the library makes its own there.
- `[Ghosts]` on a system runs it for the read-only copies a shard holds of other shards' entities near its border, instead of its own entities. It can't change their components. In a game that isn't sharded it isn't there.
- `Shards.moves` counts the times an entity came to this shard or left it, so a library can notice what left.

## Visibility

[Visibility](../game/visibility.md) works across shards. A group means the same on every shard, so an entity in a group is shown to the same players wherever it is. The groups are kept with the first shard, and code on another shard that creates, changes or destroys one waits on the border, like any change of something its shard doesn't own. Other shards see a group's change a tick or two later. Each shard sends its own players only what they may see.

## Hot reload

`tide run --shards` reloads every shard at once, at a moment nothing the old build wrote is on its way between shards, which is most moments. A build with another data layout carries every shard's world over, as an unsharded match's is. If the shards never stop sending each other the old build's data within three seconds, the game starts over and says so.
