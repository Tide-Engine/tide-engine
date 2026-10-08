---
description: "Group Tide entities into scenes, start with Main, load and unload levels, and manage scene ownership and multiple instances."
---

# Scenes

A scene is a group of entities that load and unload together: a menu, a level, an arena. Several scenes can be loaded at once, and several copies of the same one.

```csharp
scene Arena
{
    int size = 20;
}
```

A loaded scene is an entity like any other. The declaration is its component, and its fields are the scene's state, so a system that takes `Arena` runs once per loaded arena.

## Main

The game starts in the scene called `Main`, and there's exactly one. It sets up the first scene, not the game loop: the engine runs the tick.

```csharp
scene Main { }

event(Spawned) Setup(with Main)
{
    Spawn(Transform { scale = float3(1, 1, 1) }, Player);
}
```

A match `Main`, like this one, starts a single-player match with it right away. A local `Main` starts the program outside any match, usually in a menu, which starts one later (see [Local state](./local-state.md) and [Matches](../multiplayer/matches.md)).

## Loading and unloading

```csharp
var arena = Scene.Load(Arena { size = 30 });
Scene.Unload(arena);
```

`Scene.Load` loads a scene into the current world and returns its entity. `Scene.Unload` unloads it, destroying every entity it owns, and so does `Destroy` on a scene's entity. `Scene.Unload` of an entity that isn't a scene does nothing. `Spawn` of a scene is an error that says to use `Scene.Load`.

Loading and unloading happen at the end of the tick, like `Spawn` and `Destroy`. A scene is set up by `Spawned` handlers, and its entities get `Destroyed` when it unloads, both within that tick.

`Main` is the fallback. When the last scene of its world unloads, `Main` loads again at the end of that tick, set up by its `Spawned` handlers as at the start: a match `Main` in the match, a local one in the local state. Unloading one scene and loading another in the same tick never brings it back, and entities in no scene don't count. `Main` comes back at most once a tick: one that unloads itself as it loads leaves the world without a scene until the next.

A match can't load a local `Main`, so when `Main` is local, a match whose last scene unloads ends there. Every machine in it goes back offline, to its `Main`, and gets `Disconnected` with the reason `Ended` (see [Matches](../multiplayer/matches.md)). Local state carries on as it was.

```csharp
// Sets up each arena. The floor joins that arena.
event(Spawned) SetupArena(Arena arena)
{
    Spawn(Floor { size = arena.size });
}

system Collapse(Arena arena)
{
    if (arena.size <= 0) Scene.Unload(this);
}
```

Each button below loads a `Room`, or unloads the one that's there. A `Spawned` handler furnishes a room with four balls as it loads. They're spawned for the room, so they join it, and unloading it takes them with it.

::: tide-example scenes Rooms that load and unload

<<< @/examples/scenes.tide

:::

## Which scene owns what

A spawn joins the scene of the entity the code is running for. In a handler, that's the entity the event was sent to, so the floor above joins the arena it was spawned for. A spawn into a scene that's unloaded by the spawn's turn is never made, like the loot an enemy drops while its arena unloads. Code that isn't running for an entity, like a system that runs once per tick, spawns into no scene, and those entities live until they're destroyed.

A scene itself is never owned: it lives until it's unloaded, whoever loaded it.

Loaded scenes share their world: the same systems, singletons and `Time`.

## Private scenes

Scenes have visibility and membership metadata. They are public by default; `SceneVisibility.Private` and `Scene.AddPlayer` record who a scene is intended for:

```csharp
scene Hand
{
    PlayerID player;
}

event(PlayerJoined joined) DealIn()
{
    var hand = Scene.Load(Hand { player = joined.player }, SceneVisibility.Private);
    Scene.AddPlayer(hand, joined.player);
}
```

`Scene.AddPlayer(scene, player)` and `Scene.RemovePlayer(scene, player)` change that membership. It is part of the match, so only match code changes it.

::: warning Visibility filtering isn't built yet
Every player still receives the whole world, including scenes marked private and their entities. These calls record metadata; they do not hide cards, random seeds or other secrets from another player's machine. Selective state delivery is planned (see [Networking](../multiplayer/networking.md#who-sees-what)).
:::
