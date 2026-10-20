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

For an empty scene, use `scene Arena;` as shorthand for `scene Arena { }`.
The same works for local scenes: `local scene Main;`. Both forms are complete
declarations, and braces remain valid.

## Authored prefabs

An authored `.tprefab` file describes a reusable rooted entity tree. The same
tree can be spawned as an entity or loaded as a scene:

```csharp
// Enemy.tprefab
prefab Enemy
{
    Transform;
    Health { value = 100 };
    Target { weapon = Weapon };
    entity Weapon
    {
        Transform { localPosition = float3(0, 1, 0) };
    }
}
```

Declare `Health` and `Target` in ordinary `.tide` code. A reference field can use
`[SceneRequired] Entity weapon;` to require a named entity in authored prefabs.
The compiler checks unused prefabs too; missing components, fields, references
and recursive inclusion are errors. Ordinary code-driven construction can still
leave these fields unset and initialize them later.

```csharp
var enemy = Spawn(Enemy, Health { value = 50 });
var level = Scene.Load(Level, LevelSettings { difficulty = 2 });
Scene.Unload(level);
```

Settings are normal root components. Component literals override only the fields
they name and can add root components. An existing component value replaces the
whole root component. A bare component argument resets that
component to its declared defaults. A match scene uses the ordinary root
`Visibility` component: `Scene.Load(Level, Visibility { group = group })`.

All entities, component values, references and parenting exist before the
instance's `Spawned` handlers run, root-first. Nested children move with their
parents and are destroyed with them. `entity Weapon : Pistol { ... }` reuses
another named prefab; fields in its block override that prefab's root. Paths such
as `Weapon.Muzzle` resolve through nested instances, and omitted values use the
authored prefab and component defaults.

`local Spawn(Enemy)` creates local entities, and `Scene.Load` from local code
loads into the local world. References must use the corresponding `Entity` or
`LocalEntity` type. Prefab names are factories, so filters use explicit root
components. Keep `scene Main` in `.tide` for startup. The initial limits are 1024
entities per instance and 128 nesting levels. Use `tide open Level.tprefab` for
the [native scene editor](../tools/editors.md#native-scene-editor).

## Main

The game starts in the scene called `Main`, and there's exactly one. It sets up the first scene, not the game loop: the engine runs the tick.

```csharp
scene Main;

event(Spawned) Setup(with Main)
{
    Spawn(Transform { localScale = float3(1, 1, 1) }, Player);
}
```

A match `Main`, like this one, starts a single-player match with it right away. A local `Main` starts the program outside any match, usually in a menu, which starts one later (see [Local state](./local-state.md) and [Matches](../multiplayer/matches.md)).

## Loading and unloading

```csharp
var arena = Scene.Load(Arena { size = 30 });
Scene.Unload(arena);
```

`Scene.Load` loads a scene into the current world and returns its entity. `Scene.Unload` unloads it, destroying every entity it owns, and so does `Destroy` on a scene's entity. `Scene.Unload` of an entity that isn't a scene does nothing. `Spawn` of a scene is an error that says to use `Scene.Load`.

Additional component arguments attach to the loaded root, for example
`Scene.Load(Arena { size = 30 }, Visibility { group = group })`. They use the
same component names, literals and existing values as `Spawn` and are present before `Spawned`
handlers run. Local scenes can also take root components, using local placement.

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

Scenes are public by default. Give a match scene root an ordinary `Visibility`
component to restrict the scene and every entity it owns:

```csharp
scene Hand
{
    PlayerID player;
}

event(PlayerJoined joined) DealIn()
{
    var group = Visibility.CreateGroup(Audience.Only(joined.player));
    Scene.Load(Hand { player = joined.player }, Visibility { group = group });
}
```

Use `Visibility.Set(group, audience)` to replace the scene's audience. The same
group can serve several scenes and ordinary entities. Audience changes are match
state, so only match code changes them. Local scenes belong to this machine and
do not take `Visibility`.

A scene whose group has `Audience.None` stays on the server. Including a player
supplies its current permitted state; excluding one conceals it. An entity's
group can restrict the scene audience further, and a component's policy can
restrict the entity audience further. These three scopes intersect; an inner
scope cannot broaden an outer one. See [State visibility](./visibility.md) for
shared groups and component policies. The server simulates the complete match
and corrects clients where hidden simulation affects their permitted state.
