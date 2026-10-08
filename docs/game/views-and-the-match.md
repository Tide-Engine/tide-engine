---
description: "What a Tide view reads of the match: other entities through a query, this machine's player's own with ownership filters, and what the server has confirmed with verified."
---

# Views and the match

A [view](./views.md) runs for the entities its parameters pick, and reads the match as this machine predicts it, [blended](./views.md#smooth-at-any-tick-rate) between its last two ticks. This page is what else it reads: entities other than its own, the ones this machine's player owns, and what the server has confirmed.

## Other entities

A view runs for its own entities, and often needs the others too: a shadow falls on the highest platform under a fighter, an aim line ends at whoever it would hit. A `query` parameter is that second set, as in a [system](./systems.md#another-set-of-entities):

```csharp
// A fighter's shadow falls on the highest platform under it.
view DrawFighter(Body body, with Fighter, query (Platform box) platforms)
{
    mut var ground = -10000.0;
    foreach (var entry in platforms)
    {
        var top = entry.box.center.y + entry.box.half.y;
        var over = Math.Abs(body.position.x - entry.box.center.x) < entry.box.half.x;
        if (over && top <= body.position.y && top > ground) ground = top;
    }
    Draw.Ellipse(float2(body.position.x, ground), float2(body.radius, 2), Color(0, 0, 0, 0.5));
    Draw.Circle(body.position, body.radius, Color.white);
}
```

The view's own parameters still pick the entities it runs for, and the query picks the ones it goes through for each: neither filters the other. Inside the `foreach`, `entry.box` is that platform's `Platform`, to read, and `entry.entity` is its handle. The syntax and the filters are a system's: bindings, `with` and `without`. A view with only queries runs once a frame.

- **Rows are what the view sees.** They're [blended](./views.md#smooth-at-any-tick-rate) between the last two ticks like the view's own parameters, with corrections smoothed the same way: the row of the view's own entity has the values its parameters have. An entity that wasn't there last tick is as it is, and one that's gone is in no row.
- **`verified` works in a query** as it does on a view's own parameters (see [Confirmed state](#confirmed-state)). `query (verified Health health) confirmed` goes through the confirmed world's entities, as its last tick has them. A query with bindings or filters of both worlds, `query (Body body, verified Health sure) both`, takes the entities that are in both: the same entity, and not only the same handle. `with verified Dead` and `without verified Dead` filter there too.
- **A query is all the match's or all this machine's.** A view goes through local entities too, like its own sparks: `query (Spark spark) sparks`. Those are as they are now, with what views earlier in the frame wrote, and `entry.entity` is a `LocalEntity`.
- **Rows are read-only, and have the match's fields.** A binding can't be `mut`, and an entity's [local fields](./local-state.md#local-fields) are read and written by a view that runs for that entity.
- **A view that queries the match only runs in a match**, like any view that reads it. What should go on outside one stays in a view of its own.
- **Going through a lot stays cheap.** A query's rows are made once a frame, however many entities the view runs for: a view over thousands of entities that goes through thousands of others blends those others once, not once for each.

The rest is as in a system: queries and rows are used directly in `foreach`, in the view's own code, and can't be stored or passed on, where a component value like `entry.box` can. The order is the ECS's, not by distance or by entity ID. The editor says what a view's queries read above the view and on hover, as it does for a system.

A view takes a `World` as a system does, to read another entity's component by its handle: `world.Get<Health>(follower.leader)` (see [Other entities](./systems.md#other-entities-the-world)). It reads the match through it and never changes it, and what it gets is the component as of the latest tick, not blended between ticks.

## Your own entities

"My camera, my HUD, my health bar" is in every multiplayer game, so views have a filter for it. `with ownership` picks the entities this machine's player owns, and `without ownership` every other one:

```csharp
view RenderGame(Player player, with ownership)
{
    Draw.SetCamera(player.pos, player.rot, 75);
}

// Everyone else's body: a first-person camera doesn't draw its own
view Bodies(Player player, Scenery scenery, without ownership)
{
    Draw.Mesh(scenery.crate, scenery.faces, float4x4.TRS(player.pos, player.rot, float3(1)), scenery.wood);
}
```

- `with ownership`: the entity has an [`Owner`](./input.md#who-gets-which-input), and its `player` is `Session.player`, this machine's player.
- `without ownership`: every other entity the view's parameters admit, which is another player's and those with no `Owner`. With `with Owner` beside it, only those whose owner is someone else.

They're filters, beside `with` and `without`: the view takes neither `Owner` nor `Session` for them, and `if (owner.player != session.player) return;` goes. A view that wants both kinds at once, like one that draws your player yellow and the others gray, still takes `Owner` and `Session` and compares.

Only views have them. The match runs the same on every machine, so a system can't ask whose machine it's on: there it's an error, and `with Owner` picks the entities that any player owns. A local handler doesn't filter by them either: one of a match event takes `Owner owner` and `Session session`, and compares (see [Events](./events.md#what-isn-t-there-yet)). `ownership` is a keyword only right after `with` and `without`, so it still names anything else.

## Confirmed state

In a match with other machines, a client runs ahead of the server: its views draw the world as this machine predicts it, and a prediction can be wrong for a moment, until the server says how the tick went (see [Networking](../multiplayer/networking.md#prediction-and-rollback)). That suits what has to answer at once, like movement. What must not be wrong, like a health bar, a score or a death, is read `verified`: from the world the server has confirmed.

`verified` goes before a parameter's type, like `mut`:

```csharp
// Movement as predicted, the health bar only what's confirmed.
view DrawFighter(Body body, verified Health health)
{
    Draw.Circle(body.position, 0.5, Color.white);
    Draw.Rect(body.position + float2(0, 0.8), float2(health.value * 0.01, 0.1), Color.red);
}

// The same component twice: what's predicted, and what's confirmed so far.
view HealthBar(Body body, Health health, verified Health confirmed)
{
    var at = body.position + float2(0, 1);
    Draw.Rect(at, float2(confirmed.value * 0.01, 0.1), Color.gray);
    Draw.Rect(at, float2(health.value * 0.01, 0.1), Color.red);
}

// A filter, and a singleton.
view Corpse(Body body, with verified Dead)
{
    Draw.Circle(body.position, 0.5, Color.gray);
}

view Board(verified Duel duel)
{
    Draw.SetScreen();
    Draw.Text($"Round {duel.round}", float2(20, 20), 24, Color.white);
}
```

- **A view runs for an entity when each parameter exists in the world it names.** A view with components of both worlds needs the entity in both, so it skips one whose spawn isn't confirmed yet. A game that wants it drawn sooner splits the view in two: one that draws what's predicted, and one that adds what's confirmed.
- **A view whose components are all `verified`** runs for the confirmed world's entities, and draws them as they are there: `this` is the confirmed entity, and corrections aren't smoothed.
- **Filters take it**: `with verified Dead`, `without verified Dead`. Either asks for the entity in the confirmed world: `without verified Dead` is "confirmed, and not `Dead` there", so it skips an entity whose spawn isn't confirmed yet too.
- **Singletons take it**: `verified Duel duel`.
- **A `verified` value is whole.** Its text, lists, maps, sets and grids are the confirmed world's, and read like any other's: `sure.build.cards.count`, `foreach (var card in sure.build.cards)`, a copy of the value, a function it's passed to. They're to read, like everything a view takes of the match: to change one, take a copy.
- **A `verified` value isn't blended.** It's as the last confirmed tick has it, where the rest of what a view reads is [blended](./views.md#smooth-at-any-tick-rate) between two ticks.
- **`mut` goes with it.** `mut verified Health health` reads the confirmed `Health` and writes the entity's [local fields](./local-state.md#local-fields), which are the entity's whichever world a parameter reads. A view that takes a component both ways reaches its local fields through the one that isn't `verified`.
- **The same entity** in both worlds is the entity, not only its handle: running a tick again can give a handle to another entity, which is then in one world only (see [After a rollback](./local-state.md#after-a-rollback)).
- **It's the view's to say.** A function's parameter can't say `verified`: the view takes the value `verified` and passes it on. Nor can a [frag or vert](./shaders.md), which reads what its view is given.
- **The match has no "confirmed".** It's the same on every machine, so `verified` in a system, a match handler or a task is an error, which points to views and local handlers.

Outside a match, a view with a `verified` parameter doesn't run, like any view that reads the match. A local handler takes `verified` on its event, to hear it once it's certain: see [Events](./events.md#verified-handlers).

### Holding a death until it's confirmed

With a predicted `Body`, an entity is gone from a view the moment the prediction destroys it, whatever a `verified` parameter beside it says: the view needs the entity in both worlds. There are three ways to hold a death until the server has confirmed it:

- **Make the death a state of a living entity**, a field or a tag like `Dead`, and destroy the entity later. Views read the state `verified`, as `Corpse` does above.
- **Take everything `verified` in that view.** It then runs for the confirmed world's entities, so the entity is drawn, as the server last confirmed it, until the server says it's gone.
- **Show it from a local handler**: `local event(verified Destroyed) ...` hears the entity go once it's certain, with its components as they were (see [Events](./events.md#verified-handlers)).

### What isn't there yet

- **`verified World world`**, which is an error that points to `verified` component parameters. A view's `World` is the world it draws.
- **Blending confirmed state.** A `verified` value steps at the tick rate. Drawing other players from confirmed state, blended between confirmed ticks rather than predicted ahead, isn't there yet.
