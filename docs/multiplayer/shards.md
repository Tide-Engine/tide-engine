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
    reach = 64;
}
```

That's all a game declares. `reach` is how far, in world units, anything in the game looks or acts: sight, an attack's range, a query for what's nearby. Code near a border sees that far across it, and regions are never narrower than twice it.

Where an entity is comes from the position it already has: a `[Position]` field, the one that's [smoothed](./networking.md#provisional-correction-smoothing) when prediction is corrected, or its `Transform`. An entity in a `Transform` hierarchy lives with its root. An entity without a position goes with the first entity its fields name, so a bag with an `owner` follows its owner from shard to shard, or else with the entity whose code spawned it.

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

Views draw everything the machine follows, each entity once, from the shard that owns it. A view for each entity runs as it always does; a view's query has every entity the machine sees, and its `World` finds them too. Local handlers hear what happens on every shard the machine follows, and `Shown` and `Hidden` come as an entity comes into view and leaves it, whichever shard has it.

### Watching an area

A camera zoomed out over the world, a spectator or a map sees more than what's near the player. Say what it looks at, from a view or a local handler, as it moves:

```csharp
local singleton Overview
{
    bool zoomedOut;
    float3 center;
}

view Watch(Overview overview)
{
    if (overview.zoomedOut) Session.Watch(overview.center, float3(400, 100, 400));
    else Session.Unwatch();
}
```

`Session.Watch(center, size)` makes this machine follow every shard whose region is near that box too, and views draw what they own. It holds until `Session.Unwatch()`, wherever the player goes; calling it again with another box moves it. Take a `float2` center and size in a 2D game. In a world that isn't sharded, there's nothing to do: the machine has the whole world already.

Each shard watched is one more world this machine downloads and keeps up with, so watch what the camera shows, not the whole world.

### Seeing the split

To show players how the world is divided, as a debug view or a demo does, `Shards.Of(entity)` in a view is the number of the shard that owns an entity, from 0, and -1 for one this machine has no world of. Color what each shard owns:

```csharp
view Rock(Rock rock)
{
    var shard = Shards.Of(this);
    Draw.Circle(rock.position, 1, shard == 0 ? Color.red : shard == 1 ? Color.green : Color.blue);
}
```

It's for what this machine shows, so match code can't ask it: the match is one world wherever its entities are. In a game that isn't sharded it's always 0. The [home page](https://tide-engine.dev/)'s planet paints its walkers and cars this way under **Shards**.

## Running one

`tide run` runs every shard of a sharded game's match on this machine. To try borders out, split it into regions along x:

```sh
tide run --shards 2 --shard-width 100 --stats
```

`--shard-width` is each region's width in world units, 16 times `reach` without it.

A [dedicated server](./matches.md#dedicated-servers) runs each shard as one of its matches:

```sh
tide run --server --anonymous --shards 4 --shard-width 200 --shard-most 5000
```

`--shard-most` splits a region in two when its shard owns more entities than that, and merges two neighbouring regions back into one when together they own a quarter of it or less. Without `--shards`, the world starts as one shard owning all of it, splits as it fills, and comes back together into one shard as it empties. Players connect to match 0, `Session.Connect("host:7777/0")` or through the server's room with `Session.Join("K7QF2M/0")`, and the server takes them into the world. Each player follows the shards near their avatar over that one connection, on the web too.

### Keeping the world

A dedicated server can keep its world in a folder, so a crash or a restart doesn't lose it:

```sh
tide run --server --anonymous --shards 4 --shard-journal world
```

It writes a checkpoint now and then, and every tick of every shard since, to the disk before anything one shard sends reaches another. Started again with the same folder, it goes on where the world was, to the tick, and nothing that crossed a border is lost or happens twice. Players have 20 seconds to reconnect as themselves before the world sends `PlayerLeft`.

Tide's hosting service runs a sharded game's world across machines. The world starts as one shard; the service splits a region whose shard is steadily too busy, merges quiet neighbours back until the world is one shard again, and starts a shard whose machine fails again elsewhere, where it was. A world nobody is in stops after the project's empty timeout, as a match does, and goes on where it was when a player comes back.

## Physics

[Box3D](../packages/box3d.md) and [Box2D](../packages/box2d.md) work in a sharded world. Each shard steps its own physics world of the bodies it owns. A body that crosses a border is made anew on the next shard from its `Body`, where it was and as fast. Bodies another shard owns near the border are there too, as copies where their owner last had them, as heavy as they are, with all their shapes and parts, so a crate runs into a wall across the border instead of passing through it, and one crate running into another shares its speed as it would on one shard. Two bodies that touch go to one shard together while they do, and a static body that reaches far past where it is, like a long floor or a terrain, is on every shard it reaches. Each side tells its own entities of a touch or a hit.

Jointed bodies go together: the shard that has one has the other and the joint, so a joint holds across borders, and so does a chain or a ring of them. A body's parts go with it.

The physics settings, such as `gravity`, are the world's: set them as in any game, and every shard has them.

## A C library's state

This is for whoever writes a package around a C library, as the physics packages do; a game that uses the package writes none of it. The library says what its fields are, and the engine works out what that means across shards:

- `[Native]` marks a field holding the library's state, like a handle. On a component it's a number or a bool, 0 wherever its entity arrives or is copied, so the library makes its own there. On a singleton it can be anything, and each shard keeps its own; the singleton's other fields are the world's, the first shard's on every shard, like a physics world's gravity. The package's own code for no entity that takes such a singleton runs on every shard, for that shard; a game's runs once, as any code for no entity does, so a setting the game writes reaches every shard.
- `[Together]` on a component's `Entity` field keeps what it names on one shard with the entity that has it, as a joint keeps its two bodies. Everything tied together this way, a chain or a ring included, goes as one.
- `Shards.Spans(entity, center, size)` from the library's match code says an entity covers that box this tick, past where it is, so every shard near any of it has a copy, as the physics packages say of a floor. `Shards.on` is whether the game is sharded at all, so the library can leave the work out when it isn't.
- `Shards.Together(a, b)` from the library's match code says two entities act on each other for now, as two bodies do while they touch, and keeps them on one shard the same way, until as many `Shards.Apart(a, b)` say they don't. In a world that isn't sharded they do nothing.
- The copies a shard holds of other shards' entities near its border are what a query finds and `Shards.Owns` says this shard doesn't own: a library makes its own copies of them from that.

## Visibility

[Visibility](../game/visibility.md) works across shards. A group means the same on every shard, so an entity in a group is shown to the same players wherever it is. The groups are kept with the first shard, and code on another shard that creates, changes or destroys one waits on the border, like any change of something its shard doesn't own. Other shards see a group's change a tick or two later. Each shard sends its own players only what they may see.

## Hot reload

`tide run --shards` reloads every shard at once, at a moment nothing the old build wrote is on its way between shards, which is most moments. A build with another data layout carries every shard's world over, as an unsharded match's is. If the shards never stop sending each other the old build's data within three seconds, the game starts over and says so.
