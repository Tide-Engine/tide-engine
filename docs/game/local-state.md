# Local state

The **match** is what every machine simulates the same way. **Local state** belongs to one machine, and lives outside the match: menus, settings, connection status, animation timers, particles. It's never sent, rolled back or checked against other machines.

`local` in front of a declaration makes it local:

```csharp
local singleton Settings
{
    float volume = 1;
    bool showFps;
}

local component Spark
{
    float2 position;
    float age;
}
```

`local singleton`, `local component`, `local scene` and `local event` are the local kinds. Everything else belongs to the match. Structs and functions belong to neither, and both sides use them.

A local singleton exists once per machine, for the whole program. Local components make local entities.

## Views change local state

Views run every frame, in a match or not. They read the match, and read and write local state: `mut` local parameters, and `Spawn`, `Add`, `Remove` and `Destroy` of local entities.

```csharp
// Reads the match and leaves a trail of local sparks behind every ball.
view Trail(Body body, with Ball)
{
    Spawn(Spark { position = body.position });
}

view DrawSparks(mut Spark spark)
{
    Draw.Circle(spark.position, 0.1, Color.yellow);
    spark.age += Frame.dt;
    if (spark.age >= 0.5) this.Destroy();
}
```

The example below has no match at all: its `Main` is a local scene. Moving the pointer spawns local entities, and a view moves, fades and destroys them, by `Frame.dt`.

::: tide-example local-state Local entities, and no match

<<< @/examples/local-state.tide

:::

`LocalEntity` is an entity of the local world: local `Spawn` returns one, and in a view of local components, `this` is one. Local code can hold and read an `Entity` of the match, like the unit a player selected, but never change it.

Local structural changes and local events apply at the end of the frame, as the match's do at the end of the tick.

A view only runs when everything it reads exists, so a view of the match's components or singletons doesn't run outside a match. A view runs for the entities of one world: its components are all local, or all the match's.

## Time

Local state has no ticks: views run once a frame, at the display's rate. `Frame.dt` is the seconds since the previous frame started, so what a machine animates for itself takes the same time on every display. The sparks above last half a second whether that's 30 frames or 72.

```csharp
struct Puff
{
    float2 position;
    float2 velocity;
    float age;
}

local singleton Smoke
{
    List<Puff> puffs;
}

view Drift(mut Smoke smoke)
{
    var dt = Math.Min(Frame.dt, 0.05); // A stall isn't a leap
    mut var i = 0;
    while (i < smoke.puffs.count)
    {
        mut var puff = smoke.puffs[i];
        puff.age += dt;
        if (puff.age >= 1.5)
        {
            smoke.puffs.RemoveAt(i);
            continue;
        }
        puff.velocity = puff.velocity * Math.Max(0, 1 - 2 * dt); // Drag
        puff.position += puff.velocity * dt;
        smoke.puffs[i] = puff;
        Draw.Circle(puff.position, 0.2 + puff.age * 0.3, Color(1, 1, 1, 0.4 * (1 - puff.age / 1.5)));
        i++;
    }
}
```

- **All local code reads it**: views, local handlers, local tasks, and the functions they call. A task that waits a frame at a time moves on by it, as a view does:

```csharp
local singleton Banner
{
    float shown; // 0 to 1
}

async void Reveal(mut Banner banner)
{
    while (banner.shown < 1)
    {
        banner.shown = Math.Min(1, banner.shown + Frame.dt * 3);
        await Wait.Frames(1);
    }
}
```

- **The match can't.** A frame's length is another on every machine, so a system or a match handler that reads it is an error, which says to take `Time time` and read `time.dt`, a tick's length. A function that reads it is local code, and the match can't call it.
- **It needs no match.** It's there in a local scene and while connecting, and it's 0 only before the first frame, in a handler that runs as the program starts.
- **It's the frame's real length**, never negative, and long after a stall, so clamp it where that would show. There's no clock of seconds beside it: a float of seconds gets coarser the longer a program runs, so add `Frame.dt` up into a field of your own and wrap it where it repeats (see [A frame's length](./views.md#a-frame-s-length)).

## Local fields

Some of what a machine keeps is about the match's own entities: a mesh baked for each, an animation timer, a smoothed position, a C library's handle. It isn't part of the match, since the simulation never reads it, and every machine can work it out for itself. `local` before a field of a match component or singleton makes that field this machine's:

```csharp
component Box
{
    float3 pos = float3(0, 0, 5);
    local List<Vertex3> verts;
    local List<int> faces;
}

view DrawBox(mut Box box)
{
    if (box.verts.count == 0) Bake(box);
    Draw.Mesh(box.verts, box.faces, float4x4.Translate(box.pos));
}

void Bake(mut Box box)
{
    box.verts = [Vertex3 { position = float3(0, 0, 0) }, Vertex3 { position = float3(0, 1, 0) }, Vertex3 { position = float3(1, 1, 0) }];
    box.faces = [0, 1, 2];
}
```

A local field lives outside the match. It's never sent, hashed, snapshotted or rolled back, and the match is exactly what it would be without it: the same world, the same hash, the same download for a player who joins. A mesh of a hundred thousand corners costs a joining player nothing; their machine bakes its own.

- **Every entity with the component has them**, on every machine, however that machine learned of the entity: it ran the spawn, joined later, took over as host, or hot reloaded. Nothing attaches or removes them. Each starts at its default, and takes memory only once a view writes it.
- **Views read and write them through their parameter.** `Box box` reads them, and `mut Box box` writes them, and only them: the match's fields stay a blended copy the view can't change. A function that takes the component gets them with it, like `Bake` above, and then only views, local handlers and functions like it can call it.
- **A local handler of one of the match's events writes them too**, through `mut`, for the entity the event was sent to: a hit's handler sets the flash the entity's view then fades (see [Events](./events.md#local-handlers)).
- **The match can't see them.** Reading or writing one in a system, a match handler or a task is an error, and so is naming one in a `Spawn` or a component value. A component's value is the match's part: a copy, `var b = box;`, has no local fields.
- **They go with the entity**, once it's in neither the world views draw nor the last one the server confirmed. So a destroy this machine predicted wrong, which the server undoes, takes nothing with it, and a spawn it predicted that never happened leaves nothing behind. Losing the component loses them, and a match that's over takes them all.

Singletons have them too, like a texture painted from the match's state:

```csharp
singleton Arena
{
    float size = 8;
    local Grid2<Color> floor = Grid2(64, 64);
    local bool painted;
}
```

A local field holds what a local component's field can: text, lists, maps, sets, grids and a `LocalEntity` among them. `local` on a field of a struct, the input, an event, or a declaration that's local already is an error that says why.

### After a rollback

A client predicts, and the server can turn out to have had a tick otherwise. This machine spawned a rocket, and in the server's order the entity with that handle is another player's bullet: one handle, two entities. Local fields don't pass from one to the other. Each entity remembers what it was spawned as, the tick and the values it was spawned with, and local fields are kept for the entity they were written for:

- A replay that spawns **the same** under the handle (the server had the tick as this machine predicted it) is the same entity, and its local fields stay.
- A replay that spawns **something else** there is another entity. What was kept for the first goes, with [`Forgotten`](#letting-go) where a handler asked for it, and the new one starts from the defaults: a view that bakes a mesh bakes it again, once.

A spawn that comes out with other values, like a position a wrong guess had moved, counts as something else.

::: info
What an entity was spawned as is this machine's own knowledge, from the spawns it ran: it's never sent, and costs the match nothing. Entities that came with the world from the server, to a player who joins, are known by their handles alone.
:::

### Letting go

When local fields hold something to let go of, like a C library's handle, a local handler of `Forgotten` hears that they're going, with the fields still there:

```csharp
component Model
{
    int kind;
    local int handle;
}

local event(Forgotten) FreeModel(Model model)
{
    ReleaseModel(model.handle);
}
```

It takes the one component or singleton whose local fields are going, and local singletons. The entity is gone from the match by then, so only its local fields are there to read: what the handler needs of the match goes in a local field while the entity is there. It runs for the entities something was written for, and a singleton's runs once, as the match goes.

## The compiler keeps them apart

No mistake here can reach a running game, because the compiler enforces the boundary:

- **Match code** (systems, match event handlers, match scenes) can't read or write anything local, a component's [local fields](#local-fields) included, and can't read this machine's `Devices`: it takes a `Devices` parameter, the owner's, which the input sends.
- **Local code** (views, local event handlers, local scenes) can read the match, but never change it directly: `mut` on one of the match's components or singletons only writes its local fields. It can send [commands](./commands.md) for a match handler to check and act on; it cannot spawn match entities or send match events. Views read the match as it's drawn, or as the server [confirmed](./views.md#confirmed-state) it, and a local handler [hears its events](./events.md#local-handlers).
- The input's `Sample` reads this machine's devices and local singletons, and writes only the input.

Input and commands carry local actions into the match. Starting, joining and leaving a match are session calls, which never touch a running match (see [Matches](../multiplayer/matches.md)). The errors say where to go instead:

```csharp
// Error: match code can't read local state.
system Count(Spark spark) { }
```

## Local scenes and events

A local `Main` starts the program outside any match, usually in a menu:

```csharp
local scene Main { }
```

`local event` declares an event of this machine's own, which local code sends and handles. A local handler is `local` before a handler, `local event(...)`, and what it takes depends on what it hears:

- **Local events**, and `Spawned` and `Destroyed` of local entities: local state only.
- **The match's events**, to show what happened: what it reads of the match, as the event left it, and local state. Through `mut` it writes the [local fields](#local-fields) of the entity the event was sent to. See [Events](./events.md#local-handlers).
- **[`Forgotten`](#letting-go)**: the component or singleton whose local fields are going, and local singletons.

### A frame, in order

1. The local handlers of the match's events run, for each tick this machine simulated since the last frame.
2. The local fields of entities that are gone go, with [`Forgotten`](#letting-go) first where a handler asked for it.
3. The views run.
4. What handlers and views spawned, changed and sent locally is handled.

So a view draws what a handler left in the same frame. What a handler wrote into the local fields of an entity that's gone by then goes in that frame too, before the views.
