---
description: "Define components, entities and singletons in Tide's ECS, then spawn, add, remove and destroy data to build your game's world."
---

# Components and entities

Tide is an ECS: the world is made of **entities**, entities are made of **components**, and **systems** run code over them. This page is about the data; [Systems](./systems.md) is about the code.

## Components

A component is plain data, declared with `component`:

```csharp
component Health
{
    int value = 100;
    int max = 2 * 2 * 25;
}

component Poisoned { }
```

A component with no fields, like `Poisoned`, is still useful: systems can pick the entities that have it, or don't.

### Defaults

Fields can have a default. It must be a constant: literals, constructors of built-in types, struct values of constants, `Math` functions, built-in constants like `quaternion.identity`, and operators on those. It can name an [asset](./assets.md#a-field-s-default) too, like `Texture face = logo;`. A default can't read other fields, singletons or `Time`.

```csharp
component Turret
{
    float angle = Math.Radians(45);
    float3 offset = float3(0, 1.5, 0);
    Color tint = Color.red;
}
```

Fields without a written default take their type's default: zero for numbers, `false` for bools, empty text and collections, and a struct's own field defaults for a struct. `Entity` fields always start as the null entity, and can't have another default. [Default values](../language/basics.md#default-values) explains the `default` expression.

## Singletons

A singleton holds state there's one of per world, like the rules, the score or a random seed. Other ECSs call them resources.

```csharp
singleton Physics
{
    float3 gravity = float3(0, -9.81, 0);
    int collisionCount;
}
```

Singletons start with their defaults when the world is created. Systems take them as parameters, like components (see [Systems](./systems.md)).

`Time` is a built-in singleton. Systems can read it, but not write it:

| Field | What it is |
|---|---|
| `float dt` | The length of a tick in seconds |
| `int tick` | The tick, which counts from 0 |
| `float t` | The seconds since the match started: `tick * dt` |
| `int tickRate` | The ticks in a second: 1 / `dt` to the nearest whole number |
| `float maxRewind` | Configured collider history in seconds, from `settings`; 0 disables it |

`t` is worked out from the tick each time, so it never drifts, and [views](./views.md#smooth-at-any-tick-rate) see it move every frame. A float holds about 7 digits, so it gets coarser as the match goes on (after a day, its steps are about 8 milliseconds): a timer that has to be exact counts in `tick`.

What makes one match unlike another is its seed, [`Random.seed`](../language/math.md#the-match-s-seed), which the match's [random numbers](../language/math.md#random-numbers) come from.

## Spawning entities

`Spawn` makes an entity from a list of component values:

```csharp
var e = Spawn(Transform { localScale = float3(1, 1, 1) }, Player);
```

A component value is written like `Health { max = 200 }`, without `new`. Fields left out take their defaults, and a bare type name, like `Player`, takes every default.

An existing value or a helper's result works too: `Spawn(savedHealth, MakeInventory())` and `e.Add(savedHealth)`. Each argument is evaluated once in written order and copied when the change is recorded, including owned lists and maps. Editing the source afterward leaves that recorded copy intact. A component copy carries ordinary fields; each new entity gets fresh local-field defaults.

An entity has at most one component of each type. When it needs several of something, use a list inside one component, one entity per item that points back at its owner, or separate component types.

## Changing entities

```csharp
e.Add(Stunned { duration = 2 });
e.Remove(Stunned);
e.Destroy();
```

- `Add` of a component the entity already has replaces its value.
- `Remove` of a component the entity doesn't have does nothing.
- `Add`, `Remove` and `Destroy` on an entity that's already gone do nothing.

These are **structural changes**, and they don't happen right away. They're recorded, and applied together at the end of the tick, in the order they were made. The handle `Spawn` returns can be used at once, for example to store in a component, but the entity's data can only be read once the changes apply.

In a system that splits its entities across threads, the handle is temporary until the system is done, when the entity gets its ID and every handle the system kept becomes the real one (see [The schedule](./schedule.md#splitting-across-threads)). Text shows it as `Entity(new)` meanwhile.

That's what lets systems run side by side: nothing they do changes which entities exist until they're all done.

A [function](../language/functions.md#sending-and-spawning) can `Spawn`, `Add`, `Remove` and `Destroy` too, for the code that calls it. It takes the entity it changes as a parameter:

```csharp
Entity Drop(float3 at)
{
    return Spawn(Transform { localPosition = at }, Crate);
}

void Stun(Entity target)
{
    target.Add(Stunned { duration = 2 });
}
```

Each ball below is an entity with a `Ball` and a `Health`. Tap one to `Add` a `Poisoned` to it: a system that takes `with Poisoned` then drains its health, and destroys it when none is left. Tap it again to `Remove` the component, and it recovers. Tapping the ground spawns a ball. A view can't change the match itself, so each tap goes in as a [command](./commands.md).

::: tide-example entities Spawn, Add, Remove and Destroy

<<< @/examples/entities.tide

:::

## Transform and parenting

`Transform` is built in, with `float3 localPosition` (zero), `quaternion localRotation` (identity), and `float3 localScale` (one). Each is relative to the entity's parent; a root's values are its world placement. Add it like any other component.

Match `Transform` fields have [correction smoothing](./views.md#provisional-correction-smoothing) built in: `localPosition` behaves as `[Position]`, and `localRotation` as `[Rotation]`. Views preserve their displayed values across prediction corrections and let the error decay. Scale keeps ordinary tick interpolation; local entities use their current values. Use `entity.Snap()` for an intentional jump. This smooths view bindings; `World` queries and the ancestors read by `position` still use stored state.

Its computed `float3 position` property reads world position. Setting it converts the requested position into parent space and changes `localPosition`; it needs a mutable Transform and leaves the parent unchanged:

```csharp
system Move(mut Transform transform)
{
    transform.position += float3(1, 0, 0);
}

view Mark(Transform transform)
{
    var screen = Draw.WorldToScreen(transform.position);
    Draw.Screen() { Draw.Circle(screen.xy, 10, Color.red); }
}
```

The getter uses the binding's current `localPosition` and stored ancestors, including after changing that local value in the same routine. Helpers retain the attached binding when it is passed directly. An explicit copy is detached, so use `localPosition` on copies. Singular parent transforms and nonfinite setter results leave the local position unchanged. Properties are also [user-definable](../language/functions.md#properties).

`world.ParentToWorld(entity)` gives the matrix from parent space into world space. It includes retained placement when visibility hides a parent, and gives identity for an ordinary root or dead entity. Thus `Math.Transform(world.ParentToWorld(entity), localPosition)` is the same computation available to a component's own property. `world.LocalToWorld(entity)` includes the entity's own stored transform as well; its `c3.xyz` is the stored world position.

```csharp
event(Spawned) Assemble(with Main)
{
    var parent = Spawn(Transform { localPosition = float3(10, 0, 0) });
    var child = Spawn(Transform { localPosition = float3(2, 0, 0) });
    child.SetParent(parent, worldPositionStays: false);
}
```

After those changes apply, the child's world position is `(12, 0, 0)`. `SetParent(parent)` preserves its world placement by adjusting its local values; `false` keeps its local values. `SetParent(null)` detaches it. These changes apply at the end of the tick or local frame, in recorded order.

Read `world.LocalToWorld(entity)` for its composed `float4x4`, and `world.Parent(entity)` for its parent's handle. They read the latest stored state, as other `World` methods do. An entity without a Transform contributes identity; a missing entity gives identity or a null parent.

With [state visibility](./visibility.md#spatial-parents), a hidden spatial parent does not hide its public child. The projected hierarchy cuts private links and preserves placement only for subtrees containing permitted `Transform` state; a parent can therefore read as null on a client.

Destroying a parent destroys its descendants, children before their parent. A cycle, a dead handle or a parent from the other world is rejected. Runtime rejections print their reason and leave the hierarchy unchanged. Preserving world placement requires a Transform on the child and an invertible parent transform. Nonuniform scales combined with rotation can create shear; three local TRS values cannot preserve that matrix exactly.

The same `Transform` works on local entities: use `local Spawn(Transform)` and `local Transform transform` in a view's parameters. `Entity` and `LocalEntity` still prevent crossing the match/local boundary. Loaded scene entities remain independent roots. Reparenting ordinary entities into a different scene transfers their descendants into that scene; detaching retains scene ownership.

## Entity handles

`Entity` is a handle to an entity. Components can hold them, to point at each other:

```csharp
component Weapon
{
    Entity owner;
}
```

`default` is the null entity, so `weapon.owner == default` tells whether it points anywhere, and `weapon.owner = default;` clears it.

A handle leads to the other entity's components through the `World`: `world.Get<Health>(weapon.owner)` (see [Other entities](./systems.md#other-entities-the-world)).

In a system, view or event handler that runs for an entity, `this` is that entity:

```csharp
system Expire(mut Lifetime life)
{
    life.ticks -= 1;
    if (life.ticks <= 0) this.Destroy();
}
```

In local code that runs for a local entity, like a view of local components, `this` is a `LocalEntity`. Code that runs once per tick, frame or event runs for no entity, so it has no `this`, and neither do functions. In a [frag](./shaders.md#where-the-pixel-is), `this` is the pixel it runs for.

A component's methods have `this` too: the entity whose component they're called on. Methods are specialized for the placement used by the caller, so `this` is an `Entity` for a match component and a `LocalEntity` for a local component. Explicit parameter and return types keep their meaning.

```csharp
component Health
{
    int value;

    bool IsMine(Entity attacker) { return attacker == this; }
}
```

So a method that uses `this` can only be called on a component the code runs for, like a system's `Health health`. A copy of one, in a local variable or a function's parameter, belongs to no entity.

## Archetypes

An archetype is a set of components that entities share, and entities with the same set are stored together. There are no archetype declarations: the compiler finds every one from the code, from what each `Spawn` makes and what `Add` and `Remove` can turn it into. It follows the code as written: `this.Add(Stunned)` in a system only reaches the entities that system runs for, and `e.Add(Stunned)` on an `e` a `Spawn` in the same code made reaches that one, so toggling a component on a few kinds of entity makes a few archetypes. An `Add` on an entity read from a field or a parameter could be on any entity, so it reaches every archetype.

## Limits

A component that entities gain and lose all the time, like `Stunned` or `Selected`, would still double the archetypes it can reach. Mark it `[Sparse]`: it's kept as a presence bit and its value in every archetype it can reach, so `Add` and `Remove` of it move no entity, and systems that take or filter on it skip the rows that haven't got it. A tag (a component with no fields) that any `Add` or `Remove` names is sparse by itself; one only set at `Spawn` isn't, so `with Rock` costs nothing per entity.

```csharp
[Sparse]
component Stunned
{
    float left;
}

system Recover(Time time, mut Stunned stunned)
{
    stunned.left -= time.dt;
    if (stunned.left <= 0) this.Remove(Stunned);
}
```

For now, a game has at most 256 components and 1024 archetypes. Entities, the rows of each archetype, and the structural changes and events in a tick grow as they need, with no limit but memory (see [C hosts](../c/hosts.md#memory)).
