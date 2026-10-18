---
description: "Send and handle Tide events in the match and local presentation, with predicted and verified handlers that work through rollback."
---

# Events

An event is something that happened, sent from one part of the game to the rest: a hit, a pickup, a round ending. Events carry their context in fields, like a struct's.

```csharp
event Hit
{
    Entity attacker;
    int damage;
}
```

## Sending

`entity.Send(...)` sends an event to an entity, and `Send(...)` sends it to the whole world:

```csharp
system Explode(Bomb bomb)
{
    if (bomb.timer > 0) return;
    bomb.target.Send(Hit { attacker = bomb.owner, damage = 50 });
    this.Destroy();
}

system EndRound(Round round)
{
    if (round.timeLeft <= 0) Send(RoundOver { winner = round.leader });
}
```

The sender isn't recorded. When handlers need it, put it in a field: the entity that sends is often not the one that matters, like a bomb sending a `Hit` for whoever threw it.

An event is a value, so a local can hold one, a handler can send its event on (`other.Send(hit)`), and ordinary functions and methods can take or return the whole payload (`int Damage(Hit hit) { return hit.damage; }`). Factories can return `Hit { damage = amount }`; the caller chooses whether to send it. Command payloads follow the same value rules. Passing one grants no world or sending access and supplies no authenticated sender; handlers pass that separately when needed. Payloads cannot be stored fields. Local handlers still receive only the match data their event journal captures. The engine's own events, below, are only the engine's to send.

A [function](../language/functions.md#sending-and-spawning) can send too, for the code that calls it:

```csharp
// Called from a system or a handler, which could send Hit themselves
void Splash(Entity target, int damage)
{
    target.Send(Hit { damage = damage });
    Send(Splashed { });
}
```

A view that calls it gets an error, as a view can't send the match's events.

## Handling

A handler is code that runs when an event is sent. It's declared with `event` too: the first parentheses hold the event that triggers it, and the second hold the same parameters as a system's, including read-only [queries](./systems.md#another-set-of-entities) in synchronous handlers.

```csharp
// Health and this come from the entity the Hit was sent to.
event(Hit hit) TakeHit(mut Health health)
{
    health.value -= hit.damage;
    if (health.value <= 0) this.Destroy();
}

// Takes nothing from the entity, so it runs once per Hit.
event(Hit hit) CountHits(mut Stats stats)
{
    stats.hits += 1;
}
```

- A handler's components and `this` come from the entity the event was sent to. If that entity doesn't match its parameters, the handler doesn't run for it.
- A handler that takes no components runs once per event, whether it was sent to an entity or not, and has no `this`.
- A handler that needs an entity needs every `Send` of its event to name one. The compiler checks: `Send(Hit { ... })` is an error when a `Hit` handler takes components, and says to write `entity.Send(...)`.
- A handler that doesn't read the event can leave its name out: `event(Spawned) Arm(...)`.

### Querying other entities

A match handler can inspect another set of entities with the same query syntax as a system:

```csharp
event(RoundOver) ResetPlayers(query (with Player) players)
{
    foreach (var player in players)
        player.entity.Send(Reset {});
}
```

This runs once per `RoundOver`. The query selects the players to visit; it does not require the event to target a player. Ordinary component parameters still select the event's target.

Each `foreach` reads the current match at the handler's turn in the tick-end queue. Earlier handlers' completed writes and structural changes already applied are visible. Sends, spawns, adds, removes and destroys recorded by the handler remain deferred, including changes recorded inside a query loop. `Spawned` queries include the new entity; `Destroyed` queries include the entity before it goes.

Rows are read-only. To act on an entity, send it an event or use an explicit `mut World` with the existing `Set` rules. A `Set` is visible to later query reads. The handler's own `mut` component parameter retains its usual copy/write-back behavior: a query reads its stored value until that handler finishes, and following handlers see the written-back value.

Async handlers and tasks cannot take queries yet. Local handlers' [recorded query rows](#other-entities) retain their own timing rules.

## When they run

Events are handled at the end of the tick, along with structural changes, in the order they were all recorded. A `Send` before a `Destroy` of the same entity is handled while the entity still exists. An event sent to an entity that's gone by its turn is dropped.

Handlers can send events and change entities in turn. Those are handled next, until nothing is left, so everything settles within the tick. Systems later in the same tick don't see an event's effects yet, as with `Spawn`.

The handlers of one event run in the order they're declared, and `[Before]` and `[After]` order them as they do systems, among the handlers of the same event.

Handlers can also use [named phases](./systems.md#named-phases): `[Stage(MyStages.Apply)]`, optionally with an offset such as `[Stage(MyStages.Apply, 10)]`. `Before` and `After` can name a whole phase. Each event has its own order, even when different events use the same phase enum.

A handler of an event nothing sends is a warning: it would never run. So is declared access a handler doesn't use, as for systems (see [Systems](./systems.md)).

Events waiting to be handled are part of the world: a snapshot holds them, and running a tick again sends and handles them again the same way. What a machine shows of an event is heard [once](#once-under-rollback) all the same, by a local handler.

## Built-in events

The engine sends these. They cost nothing where no handler takes them. The match's handlers take the first four, and so do [local handlers](#the-engine-s-events), on each machine.

| Event | Sent to | When |
|---|---|---|
| `Spawned` | The entity | It's spawned, or its scene is loaded |
| `Destroyed` | The entity | It's destroyed, while its components can still be read |
| `PlayerJoined` | The world | A player joins, with their `PlayerID` in `player` |
| `PlayerLeft` | The world | A player leaves, with their `PlayerID` in `player` |
| `Shown` | The entity, on this machine | The entity and the components a local handler needs become available |
| `Hidden` | The entity, on this machine | The entity or components a local handler needs become unavailable |
| `Forgotten` | The entity, on this machine | Its [local fields](./local-state.md#letting-go) go: a local event, for local handlers |

```csharp
// Spawned has no fields, so it needs no name.
event(Spawned) Arm(with Player)
{
    Spawn(Weapon { owner = this });
}

event(PlayerJoined joined) GiveBody()
{
    Spawn(Body, Owner { player = joined.player });
}
```

The server picks the tick a player joins or leaves on, so every machine handles it on the same one.

Handlers can't draw: drawing happens every frame, in [views](./views.md). What a machine shows of an event goes through a local handler, which leaves it in local state for the views.

## Local handlers

A match handler changes the match. What a machine shows of an event is something else: a flash, a burst of sparks, a line in the chat. That's a **local handler**'s, which hears the match's events on each machine. It's `local` before the same `event(...)`, with no keyword of its own:

```csharp
component Fighter
{
    int health = 100;
    local float flash; // This machine's: how long the hit still shows
}

struct Spark
{
    float2 position;
    float2 velocity;
    float age;
}

local singleton Fx
{
    List<Spark> sparks;
    float shake;
}

event Hit
{
    int damage;
}

// The match handles the hit...
event(Hit hit) TakeHit(mut Fighter fighter)
{
    fighter.health -= hit.damage;
}

// ...and each machine shows it.
local event(Hit hit) ShowHit(Body body, mut Fighter fighter, mut Fx fx)
{
    fighter.flash = 0.2; // A local field of the fighter that was hit
    fx.sparks.Add(Spark { position = body.position, velocity = float2(0, hit.damage * 0.1) });
}

// Views draw what the handler left.
view DrawFighter(Body body, mut Fighter fighter)
{
    fighter.flash = Math.Max(0, fighter.flash - Frame.dt);
    Draw.Circle(body.position, 0.5, fighter.flash > 0 ? Color.white : Color.red);
}
```

A local handler changes [local state](./local-state.md) and never the match. Like every handler it can't draw: it leaves what happened where views find it, in a local singleton, in local entities, or in the [local fields](./local-state.md#local-fields) of the entity the event was sent to.

Tap a ball below, three times. Each tap reaches the match as a command, whose handler sends the ball a `Hit`. The match's handler of `Hit` takes a health off, and a local one makes the ball flash, through a local field. A ball that meets a wall sends `Bounced` to the world: nothing in the match handles it, and a local handler makes sparks of it. The third hit destroys the ball, and `Destroyed` has two handlers too: the match's spawns another ball, and a local one bursts where the ball was.

::: tide-example events Events, and what a machine shows of them

<<< @/examples/events.tide

:::

### What they take

- **The event**, as a match handler does. One that doesn't read it leaves its name out.
- **The components of the entity the event was sent to**, to read. `mut` on one reaches only its local fields, as in a view: `fighter.flash` above. `with` and `without` filter as they do for a match handler.
- **The match's singletons**, to read. `Time` is the tick the event was sent in.
- **Local singletons**, which it changes.
- **Queries**, to go through other entities than the one the event was sent to ([below](#other-entities)).

A handler that takes no components runs once per event, as a match handler does, and one that takes components needs every `Send` of its event to name an entity.

What it takes of the match is a copy from the event's turn: as the match's handlers of that event left it, and for `Destroyed`, before the entity goes. So it's all there even when the entity is gone by the frame, which is what lets `Destroyed` say where something ended:

```csharp
// Where a shot ended: its Body as it was when it went
local event(Destroyed) ShotEnded(Body body, mut Fx fx, with Bullet)
{
    fx.sparks.Add(Spark { position = body.position });
}
```

An event sent to an entity that's gone by its turn isn't heard, as it isn't handled. Local fields are the entity's whether or not it's still there: what a handler writes for an entity that's gone goes with the rest of its local fields, at that frame.

### Other entities

A local handler takes `query` parameters, as a [view](./views-and-the-match.md#other-entities) does, to go through entities other than the one its event was sent to:

```csharp
event Blast
{
    float2 at;
    float radius;
}

// Every fighter the blast reached throws a spark
local event(Blast blast) ShowBlast(mut Fx fx, query (Body body, with Fighter) fighters)
{
    foreach (var fighter in fighters)
    {
        if (Math.DistanceSq(fighter.body.position, blast.at) > blast.radius * blast.radius) continue;
        fx.sparks.Add(Spark { position = fighter.body.position, velocity = float2(0, 3) });
    }
}
```

- **Rows are from the tick that sent the event**, like what the handler takes of the entity the event was sent to: the match as that tick left it, whatever it is by the frame the handler runs in. They aren't blended. A handler of a [`verified`](#verified-handlers) event gets the confirmed tick's.
- **They're the tick's once it's done**, after all its events and changes. An entity the tick destroyed is in no row, the one the event was sent to included, and an entity it spawned is in them. The handler's own components are still as the event's turn left them.
- **Nothing in it says `verified`**: the handler's event says which tick. A binding can't be `mut`, and rows have the match's fields, without its text, lists, maps, sets and grids, which aren't heard yet.
- **A query of this machine's entities**, `query (Spark spark) sparks`, is the local world as it is when the handler runs. It's the only kind a handler of a local event takes.
- **The entities are recorded once for a tick**, however many of its events have a handler that goes through them, and only for ticks that sent one.

### At the start of a frame

Local handlers run at the start of a frame, before the views, for each tick this machine simulated since the last frame, in order. What they spawn, change and send locally is handled at the end of the frame, like a view's, and they send [commands](./commands.md) as views do. [Local state](./local-state.md#a-frame-in-order) has a frame's whole order.

An event's local handlers run in the order they're declared, and `[Before]` and `[After]` order them among themselves, apart from the match's handlers of the event. An event has 64 of them at most. A local handler of an event nothing sends is a warning, like a match handler's, and `tide schedule` lists each event's local handlers after the match's (see [the schedule](./schedule.md#reading-it)).

### Once, under rollback

In a match with other machines, a client predicts ticks ahead of the server, and runs some of them again when the server turns out to have had them otherwise (see [Networking](../multiplayer/networking.md#prediction-and-rollback)). The match's handlers run again with their tick. A local handler doesn't: it hears each event once.

- An event the tick sends again as it runs again **stays silent**. It's the same event when it's of the same tick, with the same fields, sent to the same entity.
- An event **only the corrected tick has** is heard then, late.
- An event **the correction removed** isn't taken back: the spark it made has been shown.

Two events alike in one tick are two, and each is heard once. The same entity is the entity, not only its handle: when running a tick again gives a handle to another entity, an event sent to it is another event, and it's heard (see [After a rollback](./local-state.md#after-a-rollback)).

### `verified` handlers

Where what's shown must not be wrong, like a death or a goal, the handler waits for the server. `verified` before the event hears it once the server has confirmed the tick that sent it:

```csharp
event Died
{
    Entity killer;
}

// The spark as it's predicted, the death once it's certain
local event(verified Died) ShowDeath(Body body, mut Fx fx)
{
    fx.sparks.Add(Spark { position = body.position, velocity = float2(0, 4) });
    fx.shake = 0.3;
}
```

- **It's never wrong, and never taken back.** It runs once, for what the tick sent as the server ran it.
- **It's late**, by as far as this machine runs ahead of what the server has confirmed.
- **What it takes of the match is that confirmed tick's**, as the event's turn left it, so nothing else in it is marked `verified`.
- **Both kinds can hear one event**: a handler of `Hit` for the spark, at once, and one of `verified Hit` for the damage number, once it's certain.

`verified` is local code's word. On a match handler it's an error, as the match is the same on every machine and has no "confirmed". Views take it on what they read: see [Confirmed state](./views-and-the-match.md#confirmed-state).

### Events only local code handles

The match can send an event no match handler takes, purely for what's shown:

```csharp
// Nothing in the match handles Impact: it's sent for whoever shows it
event Impact
{
    float2 at;
    float2 normal;
}

system Fly(mut Body body, Arena arena, Time time, with Bullet)
{
    body.position += body.velocity * time.dt;
    if (Math.Abs(body.position.x) > arena.halfSize.x)
    {
        Send(Impact { at = body.position, normal = float2(body.position.x > 0 ? -1 : 1, 0) });
        this.Destroy();
    }
}

local event(Impact impact) ShowImpact(mut Fx fx)
{
    fx.sparks.Add(Spark { position = impact.at, velocity = impact.normal * 3 });
}
```

It costs the match nothing. The world's bytes, its hash, its snapshots and what's sent are what they'd be without the event, and a machine with nobody to show it to does no work for it.

### The engine's events

`Spawned`, `Destroyed`, `PlayerJoined` and `PlayerLeft` have local handlers too:

```csharp
local event(PlayerJoined joined) Greet(mut Chat chat)
{
    chat.lines.Add($"{joined.player} joined");
}

// It takes a component of the match's, so it hears the match's entities
local event(Spawned) CrateLanded(Body body, with Crate)
{
    Spawn(Puff { position = body.position }); // A local entity
}
```

- **`Spawned` and `Destroyed` are sent in both worlds.** A local handler of one hears the match's when it takes something of the match, a component, a filter or a singleton, or when it's `verified`. One that takes only local state hears this machine's local entities'. One that takes a local component and something of the match is an error.
- **A machine hears what's sent from the tick it joins in**: its own `PlayerJoined`, and every later one. Nothing is made up for what happened before: the hits, the joins and the entities that came and went.
- **`Shown` initializes what this machine comes to have.** It runs for newly available entities and components: those in the world when a machine joins, those revealed later, and genuine spawns. `Spawned` describes a genuine spawn in a tick; joining or receiving an existing entity doesn't make up another spawn. Initialize [local fields](./local-state.md#local-fields) in `Shown`:

  ```csharp
  asset Texture crate = "art/crate.png";

  component Box
  {
      float3 position;
      local Texture skin; // This machine's
  }

  // For every Box this machine comes to have: spawned in a tick, there as
  // the match started, or already in the match it joined
  local event(Shown) Dress(mut Box box)
  {
      box.skin = crate;
  }
  ```

  Keep a puff caused by a genuine spawn in `local event(Spawned)`. That effect doesn't run for crates already present when a player joins.
- **`Hidden` distinguishes concealment from destruction.** It runs with the last state this machine was permitted to know, before its local fields are discarded. It can hide a model or move presentation data into a game-owned cache. Concealing an entity doesn't invoke match destruction handlers, and destroying an already hidden entity doesn't automatically disclose its destruction.
- **Availability follows each handler's requirements.** Revealing an `Inventory` on an entity that already has a visible `Body` runs `Shown` handlers needing that inventory. It doesn't run a `Shown(Body body)` handler again. Required and excluded components both count, as they do in ordinary queries. A whole visible component includes all its fields.
- **Availability is reconciled at presentation delivery boundaries (provisional).** Transient add/remove/re-add changes coalesce to the final state. `Hidden` reads the previously delivered endpoint and its query rows, then `Shown` reads the new endpoint. Predicted and verified endpoints are tracked separately. If a correction restores something that became unavailable, `Shown` prepares it again; unchanged replay emits nothing. A local rebuild starts fresh availability history, so initialization should safely reuse or replace existing local resources.
- **These are local notifications of match availability.** Write `local event(Shown)` or `local event(Hidden)`, using match components and read-only match singletons, local fields and local singletons as other local handlers do. They can't take local-world components or change the match. `verified Shown` and `verified Hidden` wait for confirmed availability. `Forgotten` remains the resource-release hook when local fields are discarded.
- **Ticks that were skipped say nothing**: when a client's world went wrong and the server sent it again, the ticks in between were never run there. Existing entities that are newly available receive `Shown` as that world arrives; nothing a machine already had is shown again.
- **A match that changes hands** goes on from the new host's last confirmed tick (see [Host migration](../multiplayer/matches.md#host-migration)), so the ticks after it are heard again on a machine that had heard them. The entities it had before aren't new to it, and aren't heard again.

### What isn't there yet

- **Text, lists, maps, sets and grids of the match** aren't heard. Naming one, in the event or in a component or singleton the handler takes, is an error that says so, and a copy of a component that has one has it empty. Local state has them as ever.
- **The `World`**, which is the match as it is now and not as the event left it. Nor `Devices` or the input.
- **`with ownership`.** A handler takes `Owner owner` and `Session session`, and compares:

  ```csharp
  // Only this machine's own fighter shakes the screen
  local event(Hit) Shake(Owner owner, Session session, mut Fx fx)
  {
      if (owner.player != session.player) return;
      fx.shake = 0.2;
  }
  ```

- **`async`.** A local handler of one of the match's events starts a [task](./tasks.md) instead, by calling an async function with what the task needs.
- **`verified` components beside a predicted event.** A handler that hears a tick as it's predicted takes the match as that tick left it.

Local state has events of its own too, `local event`, sent and handled on one machine: see [Local state](./local-state.md#local-scenes-and-events).
