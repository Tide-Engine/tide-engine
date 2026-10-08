---
description: "Write Tide ECS systems that query entities each tick, declare data access and control execution order in a deterministic simulation."
---

# Systems

A system is code that runs every tick. Its parameters say which entities it runs for, and what it does with their data.

```csharp
system ClampPlayer(mut Transform trs, with Player)
{
    if (trs.position.y < 0)
        trs.position.y = 0;
}
```

## Parameters are the query

A system's component parameters describe the entities it runs for. Each parameter's modifier says how the system relates to that component or singleton:

| Modifier | Meaning |
|---|---|
| none | Reads it |
| `mut` | Writes it |
| `with` | The entity must have it. No data, so no data dependency. |
| `without` | The entity must not have it. |

So `ClampPlayer` runs for every entity that has a `Transform` and a `Player`, and changes its `Transform`.

Keeping what a system touches (its access) apart from which entities it picks (its filters) is what lets the engine run systems side by side safely: two systems that only filter on the same component never wait for each other.

Only the amber row has `Moving` without `Frozen`, so only those entities match `Move`. Their positions change by `time.dt` in the match, and views draw the result. The button sends a command to pause or resume the match's movement.

::: tide-example systems Component filters in motion

<<< @/examples/systems.tide

:::

Other parameters:

- **Singletons**, read or `mut`, like components: `system Score(mut Stats stats, Rules rules)`.
- **The input**, or **`Devices`**: what the player who owns the entity does (see [Input](./input.md)).

- **The `World`**: any other entity's components (below).
- **A read-only `query`**: another set of entities to inspect (below).

Views take the same parameters, and one filter more: `with ownership` and `without ownership`, the entities this machine's player owns, or doesn't (see [Your own entities](./views-and-the-match.md#your-own-entities)). It's for views only: a system runs the same on every machine, so there it's an error, which points to `with Owner`, the entities any player owns.

The entity itself isn't a parameter: it's `this` (see [Entity handles](./entities.md#entity-handles)).

## Another set of entities

A bullet system runs for bullets, but needs to inspect fighters too. A `query` parameter selects that second set:

```csharp
system BulletHits(Body shot, Bullet bullet, query (Body body, with Fighter) fighters)
{
    foreach (var fighter in fighters)
    {
        if (Intersects(shot, fighter.body))
            fighter.entity.Send(Hit { damage = bullet.damage });
    }
}
```

`Intersects` is a function the game defines. The outer `Body` and `Bullet` parameters select each bullet; `Body` and `with Fighter` inside the query select the candidates it inspects. The query never requires a bullet to be a fighter.

Inside `foreach`, `fighter.body` is read-only component data and `fighter.entity` is that fighter's entity handle. Add more component bindings when their values are needed, and `with` or `without` filters when only presence matters:

```csharp
query (Body body, Health health, with Fighter, without Invulnerable) fighters
```

Sparse components and tags are checked for each candidate, and an empty query runs no iterations. A system with only query parameters runs once per tick.

Queries use normal deterministic ECS traversal. They do not sort by entity ID, distance or hit time. For a swept projectile, calculate each candidate's intersection and keep the earliest one before sending a hit.

Query data reads the current world and participates in [the schedule](./schedule.md). A system that also changes a queried component runs on one thread, following the same rule as reading its own writes through `World`: it sees rows changed earlier in that run. Events and structural changes still wait for the end of the tick, so sending a hit or recording a destroy through `fighter.entity` does not change what this iteration reads.

Queries have read-only component bindings and component filters. Queries and their whole rows are used directly in `foreach` in the system's ordinary control flow, outside parallel loops and callback blocks; they cannot be stored or passed to functions. A component value such as `fighter.body` can be copied or passed to a function. It follows the usual component copy rules, so a method that uses `this` needs the entity's own component parameter.

Systems, views and local handlers take queries. A view's rows are the match as it draws it, blended between ticks, or [`verified`](./views-and-the-match.md#confirmed-state): see [Views and the match](./views-and-the-match.md#other-entities). A local handler's are the match as the tick that sent its event left it: see [Events](./events.md#other-entities). The match's own event handlers, command handlers and tasks take none yet.

## Other entities: the World

A system often needs another entity's component in the same tick: the target it attacks, the leader it follows. A `World` parameter gives it, by the entity's handle:

```csharp
system Attack(Attacker attacker, World world, mut Stats stats)
{
    if (!world.Has<Health>(attacker.target)) return;
    var target = world.Get<Health>(attacker.target);
    if (target.value <= attacker.damage) stats.kills += 1;
}
```

- `world.Get<Health>(e)` is a copy of that entity's `Health`, or its zero value when it has none.
- `world.Has<Health>(e)` tells whether it has one.
- `world.TryGet<Health>(e, out var health)` is both in one: whether it has one, and the copy, or the component's default when it has none (see [Out parameters](../language/functions.md#out-parameters)).
- `world.Set<Health>(e, value)` puts one back, and needs `mut World`. It does nothing for an entity without one.
- A system that takes `Health` itself can't `Set<Health>`: its own entity's goes through its parameter. The exception is a parameter it only reads, beside which `world.Set<Health>(this, value)` sets this entity's own. That's for a system that looks at every entity and changes a few: the parameter stays as the system came to the entity.

What `Get` gives is a copy, as a list's element is, so changing another entity's component is taking it out, changing it and putting it back:

```csharp
system Heal(Healer healer, mut World world)
{
    mut var health = world.Get<Health>(healer.patient);
    health.value += 1;
    world.Set<Health>(healer.patient, health);
}
```

The signature doesn't say which components a system reaches this way: the engine reads it off the calls. Each counts as access to every entity's component, so the system is scheduled like one that touches them all, and one that calls `Set` runs on one thread. [The schedule](./schedule.md#through-the-world) and your editor show what each system reads and writes through its `World`. Where the change can wait for the end of the tick, an event (`e.Send(Hit { ... })`) keeps systems running side by side; see [Events](./events.md).

A function or method can take the `World` too, and the system passes its own:

```csharp
bool TryFind(Entity target, World world, out Health health)
{
    return world.TryGet<Health>(target, out health);
}

system Aim(mut Attacker attacker, World world)
{
    if (TryFind(attacker.target, world, out var health)) attacker.threat = health.value;
}
```

What the function reaches through it counts as the system's own, through however many functions it's passed, so nothing about the schedule changes: `Aim` reads every entity's `Health`. A function that calls `Set` takes `mut World`, and so does the system that passes it. The rules above still hold, said at the call: a system that takes `Health` can't call a function that sets `Health`, and a view can't call one that changes the match.

Views and event handlers take a `World` too; a view only reads the match through it, and can change local entities with `mut World`. A [local handler](./events.md#local-handlers) of one of the match's events takes none: it hears the match as the event left it, and the `World` is the match as it is now.

## Once per entity, or once per tick

A system with component parameters runs once for every matching entity, and `this` is the entity it's running for. One without them runs once per tick, for no entity, so it has no `this`:

```csharp
system CountDown(mut Round round, Time time)
{
    round.timeLeft -= time.dt;
}
```

`return;` ends the system for the current entity, and it goes on with the next one.

## Order

Systems run in one order, the same on every machine. By default, it follows the files, sorted by path, then the order of the systems in each file. A game's [packages](../tools/packages.md) come first, each after the packages it needs.

`[Before]` and `[After]` change that. They take any number of systems:

```csharp
[After(Gravity, Collisions)]
system Move(mut Body body, Time time)
{
    body.position += body.velocity * time.dt;
}
```

Of the systems whose constraints are met, the earliest in the default order runs next. Constraints that form a loop are an error that names the loop. Hover a system in your editor to see its place in the order.

Systems can share a name when their parameters differ, as [functions can](../language/functions.md#default-values-names-and-overloads). Nothing calls a system, so where one of them is named, it's written with its parameters' types, in order, with its filters and without names or `mut`:

```csharp
system Move(mut Body body, Speed speed, with Player) { ... }
system Move(mut Body body, Speed speed, with Enemy) { ... }

[After(Move(Body, Speed, with Player)), Before(Move(Body, Speed, with Enemy))]
system Aim(mut Turret turret, Body body) { ... }
```

A name alone is fine while only one system has it. When several share it, `[After(Move)]` is an error that lists them as they're written, and so is a parameter list that isn't exactly one's. Views and event handlers work the same way, and the schedule and the profiler show a shared name with its parameters.

A system marked `virtual` can be replaced by another package's `override`, which runs where it would have: at its place in this order, with its attributes, and under its name where others name it (see [Virtual and override](../language/overrides.md)).

### Named phases

An enum gives shared names to parts of the tick. Assign systems to its phases with `Stage`:

```csharp
enum TickStages { Input, Simulation, Late }

[Stage(TickStages.Simulation)]
system Move(mut Body body, Time time)
{
    body.position += body.velocity * time.dt;
}

[Stage(TickStages.Simulation, 10)]
system Collisions(...) { ... }

[After(TickStages.Simulation)]
system Finish(...) { ... }
```

Phases follow the enum members' declaration order. The optional comma-separated offset orders systems within a phase: lower first, with 0 as the default. Negative offsets work too, and the enum needs no numeric spacing. Equal offsets keep the usual order and can use `Before` or `After` on particular systems.

`After(TickStages.Simulation)` follows every system in that phase, including its offsets; `Before` precedes all of them. Empty phases keep their position. A system in a later phase waits for the earlier phases to finish, while systems at the same offset can still run alongside each other. Unassigned systems keep their default order wherever the constraints allow. Separate enums have independent phase orders.

The same attributes order views among views and event handlers among the handlers of one event. They don't move code between the tick, the frame and event handling.

## Running side by side

Two systems that write the same component isn't a mistake: it's normal. Such systems run one after the other, in the order above. Systems that share nothing they write can run at the same time. The compiler works this out from the parameters, and can tell you why each system waits (see [The schedule](./schedule.md)):

```
stage 2  Move  (per entity)
         after Steer: both write Body
```

Declaring more than a system uses makes others wait for nothing, so it's a warning: a parameter that's never used, or a `mut` one that's never written (a `mut World` that never calls `Set` too). A component that's only there to pick entities belongs in `with`. The editors offer quick fixes for these.

## Written in C

A system's code can be C's: a library's, or a loop over every entity's numbers that's worth writing by hand. `extern system Move(mut Position position, Velocity velocity);` declares one, with no body, and C is handed the components as the arrays the match keeps them in, rather than an entity at a time. It's a system like the others to everything on this page: its parameters pick its entities, and it's ordered and scheduled by them. See [Systems written in C](../c/functions.md#systems-written-in-c).

## What systems can't do

Systems run the match, so they can't read this machine's local state, and they can't draw: drawing happens every frame, in [views](./views.md).
