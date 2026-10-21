---
title: Tide overview for AI agents
description: A compact Tide language and engine overview for AI agents, with a
  complete game, essential contracts and links to exact API references.
source_url: https://tide-engine.dev/guide/ai-context
source_file: docs/guide/ai-context.md
source_revision: 16dcab23f81d8aa4b3a98b180978a6ecccae6683
source_worktree: modified
documentation_sha256: d5cbf1c1b0ab0cf7d7b9a78508836fa50c2a64006ea23763ec6ff869201de21c
documentation_scope: Website source checkout; not matched to an installed Tide
  release. Check tide version and the project toolchain.
---

<a id="tide-overview-for-ai-agents"></a>

# Tide overview for AI agents

Read this before writing a Tide game. It covers the common path; consult the linked topic when a task needs more. [Using Tide with AI](https://tide-engine.dev/guide/ai.md) explains installation, project instructions and the optional skill.

<a id="find-the-project-and-version"></a>

## Find the project and version

A Tide game is a folder of `.tide` files, including its subfolders. Declarations are visible across files without imports. An ordinary new game needs no project generator, CMake project or C host: create a folder and a `game.tide`. Optional packages are listed in `tide.packages`; [authored scenes and prefabs](https://tide-engine.dev/game/scenes.md) also use `.tprefab` files.

Inspect the existing game and its instructions before changing it. Run `tide version` to identify the installed compiler and `tide help build` or `tide help run` for its actual CLI. These docs describe the website's source revision, which can be newer than an installed release. Prefer documentation supplied with the project's pinned toolchain when available. On a mismatch, report the versions and use compiler diagnostics to check the relevant feature; do not silently upgrade the user's installation.

<a id="a-complete-game"></a>

## A complete game

Save the following as `game.tide` in an otherwise empty game folder. It is the same source used by [Your first game](https://tide-engine.dev/guide/first-game.md): a bouncing ball and a WASD-controlled player for each joining player. The built-in host starts the match and joins a player even in single-player.

```tide
component Body
{
    [Position] float2 position;
    float2 velocity;
    float radius = 20;
}

component Ball { }

input Controls
{
    float2 move;

    Sample(Devices devices)
    {
        var keys = devices.keyboard;
        if (keys.d.pressed) move.x += 1;
        if (keys.a.pressed) move.x -= 1;
        if (keys.w.pressed) move.y += 1;
        if (keys.s.pressed) move.y -= 1;
    }
}

scene Main { }

event(Spawned) Setup(with Main)
{
    Spawn(Body { velocity = float2(160, 120) }, Ball);
}

event(PlayerJoined joined) AddPlayer()
{
    Spawn(Body { radius = 16 }, Owner { player = joined.player });
}

system Steer(Controls input, mut Body body, with Owner)
{
    body.velocity = Math.Normalize(input.move) * 250;
}

system Move(mut Body body, Time time)
{
    body.position += body.velocity * time.dt;
}

system Bounce(mut Body body, with Ball)
{
    if (Math.Abs(body.position.x) > 160) body.velocity.x = -body.velocity.x;
    if (Math.Abs(body.position.y) > 160) body.velocity.y = -body.velocity.y;
}

view DrawBalls(Body body, with Ball)
{
    Draw.Circle(body.position, body.radius, Color.red);
}

view DrawPlayers(Body body, Owner owner, Session session)
{
    var color = owner.player == session.player ? Color.yellow : Color.gray;
    Draw.Circle(body.position, body.radius, color);
}
```

Build with `tide build`. For a human's interactive session, `tide run` watches files and reloads when they change. An agent's validation uses a bounded hidden run instead, as shown below.

<a id="the-model"></a>

## The model

- **Components** hold each entity's data. A component with no fields is a tag. **Singletons** hold one value per world; declare one with `singleton Score { int value; }` and request it as a parameter.
- **Systems** run each fixed tick for every entity matching their component parameters. `mut Body body` writes data, `Body body` reads it, and `with Ball` or `without Ball` filters without reading data. Repeat the modifier for each filter: `with Ball, without Frozen`. A system with only singleton/input parameters runs once per tick. Use `this` for its current entity, not an invented entity parameter.
- **Views** run each rendered frame. They read the presented match and draw; they can change local state, but cannot change match state. `Time.dt` is the fixed tick interval; `Frame.dt` is the frame interval for local animation. [Views](https://tide-engine.dev/game/views.md) explains interpolation and drawing order.
- **Events** notify code inside the game. `scene Main;` is the initial scene; `event(Spawned) Setup(with Main)` initializes it once. `event(PlayerJoined joined) ...` initializes a joining player's entities. [Events](https://tide-engine.dev/game/events.md) explains handlers and their data parameters.
- Every game is a networked **match**, including single-player. Keep gameplay state in match components and singletons. Menus, camera preferences and presentation-only effects belong in `local singleton`, `local` fields or local entities. [Local state](https://tide-engine.dev/game/local-state.md) explains their lifetimes and access.

Systems declare access so the compiler can schedule them. Component filters create no data dependency. Conflicting systems follow deterministic declaration order unless explicit ordering changes it; put a system that sets velocity before one that integrates it. See [Systems](https://tide-engine.dev/game/systems.md) and [The schedule](https://tide-engine.dev/game/schedule.md).

<a id="syntax-that-matters"></a>

## Syntax that matters

Values are read-only unless declared `mut`, including locals. `var value = ...;` infers a read-only type; use `mut var value = ...;` for an accumulator that changes. A `for (var i = 0; i < n; i++)` loop permits its step to change `i`; changing it inside the body requires `mut var`. Fields are writable through a mutable component or singleton binding. Pass singletons explicitly through helper parameters instead of looking them up by type.

Construct components with `Body { radius = 12 }`, without `new`. Fields not specified use defaults or zero. `float2(x, y)` constructs a vector; arithmetic is component-wise. `Math` supplies deterministic operations such as `Math.Normalize`, `Math.Length`, `Math.Clamp` and `Math.Sin`. Angles are radians; `90deg` is a radians literal. String interpolation is `$"Score: {score.value}"`. Read [Basics](https://tide-engine.dev/language/basics.md), [Functions](https://tide-engine.dev/language/functions.md), [Math](https://tide-engine.dev/language/math.md) and [Text and lists](https://tide-engine.dev/language/text-and-lists.md) for signatures, collections and copy/borrow rules.

`Spawn(Body { position = at }, Ball)` returns an `Entity` handle. `entity.Add(Component { ... })`, `entity.Remove(Component)` and `entity.Destroy()` record structural changes. Those changes apply at the end of the tick, not immediately: a spawned entity cannot be queried yet, and recording a destroy does not remove a row from a running query. Guard against counting the same pending removal twice. Entity changes in local code apply to local entities at the local frame boundary. See [Entities](https://tide-engine.dev/game/entities.md).

For a second set of entities, declare a query parameter:

```csharp
system CountBalls(query (with Ball) balls, mut Score score)
{
    mut var count = 0;
    foreach (var ball in balls) count += 1;
    score.value = count;
}
```

This fragment assumes the `Ball` tag and `Score` singleton above. Query bindings are read-only match data; a row exposes its bound values and `.entity`. An ordinary explicit `World world` parameter supports handle lookups, for example `world.Get<Body>(entity)` (a copy) and `world.Has<Body>(entity)`. A `mut World world` parameter permits `world.Set<Body>(entity, replacement)`. Look up the [query and World rules](https://tide-engine.dev/game/systems.md#another-set-of-entities) before building cross-entity changes; do not invent `world.Entities`, `GetComponent` or writable query bindings.

<a id="input-and-submitted-actions"></a>

## Input and submitted actions

Use a declared `input` for per-tick controls, as in the complete game. `Sample(Devices devices)` reads this machine's devices; gameplay systems receive the input of the entity's `Owner`. `Owner.player` is a `PlayerID`. Keyboard `.pressed` means held; `.down` and `.up` are edges. The input sampler starts from field defaults each tick. A view can read local devices, but its device reads cannot directly alter the match. See [Input](https://tide-engine.dev/game/input.md).

Use typed **commands** for submitted actions from local UI. A minimal entity-targeted action is:

```csharp
component Ready { bool value; }
command SetReady { bool value; }

view ReadyButton(Ready ready, with ownership)
{
    if (GUILayout.Button(ready.value ? "Unready" : "Ready"))
        this.Send(SetReady { value = !ready.value });
}

command(SetReady action, PlayerID sender) ApplyReady(Owner owner, mut Ready ready)
{
    if (sender != owner.player) return;
    ready.value = action.value;
}
```

Spawn `Ready` alongside each player's `Owner` to use this fragment. The connection supplies the sender; never trust a sender ID in the payload. Validate ownership and game rules before writing state. Handlers return void, with ordinary guard clauses. They have no acceptance method or transaction. A global command uses `Send(...)`; an entity-targeted command uses `entity.Send(...)`. See [Commands](https://tide-engine.dev/game/commands.md) and [GUI](https://tide-engine.dev/game/gui.md).

Initialize multiplayer entities in `PlayerJoined`, and clean up their entities/state in `PlayerLeft` when the game needs it. **Treat `PlayerID` as an opaque identity, not a display index:** retain `joined.player` or read an entity's `Owner.player`; do not assume the players are `PlayerID(1)` and `PlayerID(2)`. Store a separate seat/index when the UI needs ordered slots. `with ownership` selects this machine's entities in local code; match systems use `with Owner`. Write separate filters as `with Wallet, with ownership`.

All machines in a match must execute identical simulation rules. Read [Matches](https://tide-engine.dev/multiplayer/matches.md) before implementing room setup, admission or migration. Optional [Rooms](https://tide-engine.dev/packages/rooms.md) supplies a Play/Host/Join menu; it is not automatically included in a new folder.

<a id="draw-and-validate"></a>

## Draw and validate

The default 2D view uses world coordinates centered on the window, with Y up and one world unit per pixel. **`Draw.SetScreen()` changes subsequent drawing to logical pixels from the top left, with X right and Y down**, the same coordinates as GUI rectangles and script `mouse x y`. It also affects later views in that frame until another camera is selected; `Draw.Screen() { ... }` limits the change to a block. For a grid whose row 0 is at the top, screen Y increases with the row. `Draw.Circle(position, radius, color)` and `Draw.Rect(center, size, color)` use the shape's center; a GUI `Rect(x, y, width, height)` uses its top-left corner. `Draw.Text(text, position, size, color)` accepts an `Anchor` to align text.

Device pointer positions use bottom-left, Y-up coordinates even when drawing in screen mode; use `Screen.Flip(devices.pointer.position)` before screen-space hit tests. Read [Views and drawing](https://tide-engine.dev/game/views.md), [Cameras](https://tide-engine.dev/game/cameras.md) and [GUI](https://tide-engine.dev/game/gui.md) for signatures and layout. Do not assume the default world camera and GUI use the same axes. For 3D, read [Drawing in 3D](https://tide-engine.dev/game/3d.md) before choosing meshes, depth or lighting.

Compile early, then check behavior. These are game commands; building the engine repository is a different workflow.

```sh
tide version
tide build
tide build --web
```

For an agent's native runs, set `TIDE_HIDDEN=1` in that shell before running the game. PowerShell:

```powershell
$env:TIDE_HIDDEN = '1'
tide run --frames 120 --capture smoke.png
```

POSIX:

```sh
TIDE_HIDDEN=1 tide run --frames 120 --capture smoke.png
```

This finishes after 120 frames and captures the final picture. Add `--size 640 360` to test a particular logical window size; use `Screen.width` and `Screen.height` for responsive layout. Open the PNG and inspect it; a successful build alone does not establish that the game draws or behaves correctly. If inspection is inconclusive, take a later capture or check simulation values with `Debug.Log`/`Debug.Assert` before concluding the game is blank or broken. For input, use `tide run --script smoke.txt --frames 180 --capture smoke.png`, with a script such as:

```text
0 key d down
60 key d up
120 capture after-move.png
150 quit
```

Script times are frames; key actions use `down`/`up`. `--players 2` runs two game processes in one match over a simulated network on this machine, with `player 2` after a frame number to target the second player. It does not exercise real network transports. Read [CLI capture and scripts](https://tide-engine.dev/tools/cli.md#without-a-window) for the supported actions, output naming and exit codes. A web build still needs a browser test; native captures do not verify WebGL/WebGPU. Launch automated Chrome/Edge with `--mute-audio` and a bounded test, not an unbounded `tide run --web`.

For numerical behavior, supplement pictures with `Debug.Log` of state changes or `Debug.Assert` of invariants. A multiplayer money transfer can assert that balances never go negative and that the pool plus all balances stays constant; a collector can log cell and score after each move. Check the actual log as well as the captures. A failed or unclear image inspection is not evidence of a replication bug.

Fix diagnostics using the current compiler and relevant reference rather than guessing APIs. Report the builds, behavior checks and platforms actually tested. If a tool or backend is unavailable, say which check remains unverified.

<a id="references-on-demand"></a>

## References on demand

The [documentation index](https://tide-engine.dev/llms.txt) links to clean Markdown pages. The optional [full documentation download](https://tide-engine.dev/llms-full.txt) is for offline searching, not required reading on every task. The [language spec](https://tide-engine.dev/spec.md) separates decided behavior from provisional implementation and open proposals; an open design is not an available API.

Use [Assets](https://tide-engine.dev/game/assets.md) for imports, [Packages](https://tide-engine.dev/packages.md) for optional dependencies and their pinned documentation, [Determinism](https://tide-engine.dev/multiplayer/determinism.md) for simulation constraints, and [Visibility](https://tide-engine.dev/game/visibility.md) for secrets and recipient state. Keep gameplay state deterministic and use Tide's math/random APIs; C reached through `extern` is trusted and can violate those guarantees.
