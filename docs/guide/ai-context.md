---
description: "Tide on one page: a complete game, the essential rules of the language and engine, and links to each topic's full reference."
---

# Quick reference

Tide on one page: a complete game, the rules most games rely on, and links to each topic's full reference. It covers the common path; follow the links when a task needs more. It's also the page to give an AI agent: [Using Tide with AI](./ai.md) explains how.

## Find the project and version

A Tide game is a folder of `.tide` files, including its subfolders. Declarations are visible across files without imports. An ordinary new game needs no project generator, CMake project or C host: create a folder and a `game.tide`. Optional packages are listed in `tide.packages`; [authored scenes and prefabs](../game/scenes.md) also use `.tprefab` files. Every `.tide` file under the folder is compiled, except in hidden folders, whose names start with a dot: `tide build` writes to `.build/`, and scratch or probe copies of the game belong in a hidden folder or outside the game folder.

Inspect the existing game and its instructions before changing it. Run `tide version` to identify the installed compiler and `tide help build` or `tide help run` for its actual CLI. These docs describe the website's source revision, which can be newer than an installed release. Prefer documentation supplied with the project's pinned toolchain when available. On a mismatch, report the versions and use compiler diagnostics to check the relevant feature; do not silently upgrade the user's installation.

## A complete game

Save the following as `game.tide` in an otherwise empty game folder. It is the same source used by [Your first game](./first-game.md): a bouncing ball and a WASD-controlled player for each joining player. The built-in host starts the match and joins a player even in single-player.

<<< @/examples/first-game.tide

Build with `tide build`. For a human's interactive session, `tide run --stats` watches files and reloads when they change. Automated checks (a script, CI or an AI agent) use a bounded hidden run instead, as shown below.

## The model

- **Components** hold each entity's data. A component with no fields is a tag. **Singletons** hold one value per world; declare one with `singleton Score { int value; }` and request it as a parameter.
- **Systems** run each fixed tick for every entity matching their component parameters. `mut Body body` writes data, `Body body` reads it, and `with Ball` or `without Ball` filters without reading data. Repeat the modifier for each filter: `with Ball, without Frozen`. A system with only singleton/input parameters runs once per tick. Use `this` for its current entity, not an invented entity parameter.
- **Views** run each rendered frame, in declaration order, and later views draw on top. They read the presented match and draw; they can change local state, but cannot change match state. `Time` is a singleton: a system takes a `Time time` parameter and moves by `time.dt`, the fixed tick interval. Views animate local effects by `Frame.dt`, the frame interval. `Frame` and `Screen` are not parameters: views read `Frame.dt`, `Screen.width` and `Screen.height` directly. [Views](../game/views.md) explains interpolation and drawing order.
- **Events** notify code inside the game. `scene Main;` is the initial scene; `event(Spawned) Setup(with Main)` initializes it once. `event(PlayerJoined joined) ...` initializes a joining player's entities. [Events](../game/events.md) explains handlers and their data parameters.
- Every game is a networked **match**, including single-player. Keep gameplay state in match components and singletons. Menus, camera preferences and presentation-only effects belong in `local singleton`, `local` fields or local entities. [Local state](../game/local-state.md) explains their lifetimes and access.

Systems declare access so the compiler can schedule them. Component filters create no data dependency. Conflicting systems follow deterministic declaration order unless explicit ordering changes it; put a system that sets velocity before one that integrates it. See [Systems](../game/systems.md) and [The schedule](../game/schedule.md).

## Syntax that matters

Values are read-only unless declared `mut`, including locals. `var value = ...;` infers a read-only type; use `mut var value = ...;` for an accumulator that changes. A `for (var i = 0; i < n; i++)` loop permits its step to change `i`; changing it inside the body requires `mut var`. Fields are writable through a mutable component or singleton binding. Pass singletons explicitly through helper parameters instead of looking them up by type.

Construct components with `Body { radius = 12 }`, without `new`. Fields not specified use defaults or zero. `float2(x, y)` constructs a vector; arithmetic is component-wise. `Math` supplies deterministic operations such as `Math.Normalize`, `Math.Length`, `Math.Clamp` and `Math.Sin`. Angles are radians; `90deg` is a radians literal. String interpolation is `$"Score: {score.value}"`. Read [Basics](../language/basics.md), [Functions](../language/functions.md), [Math](../language/math.md) and [Text and lists](../language/text-and-lists.md) for signatures, collections and copy/borrow rules.

`Spawn(Body { position = at }, Ball)` returns an `Entity` handle. `entity.Add(Component { ... })`, `entity.Remove(Component)` and `entity.Destroy()` record structural changes. Those changes apply at the end of the tick, not immediately: a spawned entity cannot be queried yet, and recording a destroy does not remove a row from a running query. Guard against counting the same pending removal twice. Entity changes in local code apply to local entities at the local frame boundary. See [Entities](../game/entities.md).

For a second set of entities, declare a query parameter:

```csharp
system CountBalls(query (with Ball) balls, mut Score score)
{
    mut var count = 0;
    foreach (var ball in balls) count += 1;
    score.value = count;
}
```

This fragment assumes the `Ball` tag and `Score` singleton above. Query bindings are read-only match data; a row exposes its bound values and `.entity`. An ordinary explicit `World world` parameter supports handle lookups, for example `world.Get<Body>(entity)` (a copy) and `world.Has<Body>(entity)`. A `mut World world` parameter permits `world.Set<Body>(entity, replacement)`. Look up the [query and World rules](../game/systems.md#another-set-of-entities) before building cross-entity changes; do not invent `world.Entities`, `GetComponent` or writable query bindings.

## Input and submitted actions

Use a declared `input` for per-tick controls, as in the complete game. `Sample(Devices devices)` reads this machine's devices; gameplay systems receive the input of the entity's `Owner`. `Owner.player` is a `PlayerID`. The input sampler starts from field defaults each tick. Keys are named as in Unity's Input System: `a`, `space`, `r`, `leftArrow`, `upArrow`, `digit1`, `leftShift`.

Input carries whether keys are **held**: a late player's input is guessed by repeating it, and a repeated "just pressed" would press twice. In `Sample`, record `.pressed`, not a key's `.down`. Match code gets one-shot edges from the input's own `bool` fields, which have `.down` and `.up`:

```csharp
input Controls
{
    bool restart;
    Sample(Devices devices) { restart = devices.keyboard.space.pressed; }
}

system Restart(Controls input, mut Score score, with Owner)
{
    if (input.restart.down) score.value = 0;
}
```

A view can read local devices, including `.down` edges, but its device reads cannot directly alter the match: a view turns a press into a command, below. See [Input](../game/input.md).

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

Spawn `Ready` alongside each player's `Owner` to use this fragment. The connection supplies the sender; never trust a sender ID in the payload. Validate ownership and game rules before writing state. Handlers return void, with ordinary guard clauses. They have no acceptance method or transaction. A global command uses `Send(...)`; an entity-targeted command uses `entity.Send(...)`. See [Commands](../game/commands.md) and [GUI](../game/gui.md).

Initialize multiplayer entities in `PlayerJoined`, and clean up their entities/state in `PlayerLeft` when the game needs it. **Treat `PlayerID` as an opaque identity, not a display index:** retain `joined.player` or read an entity's `Owner.player`; do not assume the players are `PlayerID(1)` and `PlayerID(2)`. When the game needs ordered seats (left and right paddle, wallet slots), assign them in the `PlayerJoined` handler from a singleton, such as `mut Seats seats` holding each seat's `PlayerID`, and free them in `PlayerLeft`. Do not count the entities spawned for earlier players instead: spawns apply at the end of the tick, and two players can join in the same tick. `with ownership` selects this machine's entities in local code; match systems use `with Owner`. Write separate filters as `with Wallet, with ownership`.

All machines in a match must execute identical simulation rules. Read [Matches](../multiplayer/matches.md) before implementing room setup, admission or migration. Optional [Rooms](../packages/rooms.md) supplies a Play/Host/Join menu; it is not automatically included in a new folder.

## Draw and validate

The default 2D view uses world coordinates centered on the window, with Y up and one world unit per pixel. **`Draw.SetScreen()` changes subsequent drawing to logical pixels from the top left, with X right and Y down**, the same coordinates as GUI rectangles and script `mouse x y`. It also affects later views in that frame until another camera is selected; `Draw.Screen() { ... }` limits the change to a block. For a grid whose row 0 is at the top, screen Y increases with the row. `Draw.Circle(position, radius, color)` and `Draw.Rect(center, size, color)` use the shape's center; a GUI `Rect(x, y, width, height)` uses its top-left corner. `Draw.Text(text, position, size, color)` accepts an `Anchor` to align text.

There is no window-size setting: players resize the window, which opens at 960×540 on desktop. For a game designed at a fixed size such as 640×360, draw the play area in world units and set the camera in the first view, so it scales with the window: `Draw.SetCamera(float2(0, 0), 180)` shows 360 units of height (its size is half the visible height), and `Math.Max(180, 320 * Screen.height / Screen.width)` instead also keeps all 640 units of width visible. The camera stays for later views in that frame.

Device pointer positions use bottom-left, Y-up coordinates even when drawing in screen mode; use `Screen.Flip(devices.pointer.position)` before screen-space hit tests. Read [Views and drawing](../game/views.md), [Cameras](../game/cameras.md) and [GUI](../game/gui.md) for signatures and layout. Do not assume the default world camera and GUI use the same axes. For 3D, read [Drawing in 3D](../game/3d.md) before choosing meshes, depth or lighting.

Compile early, then check behavior. These are game commands; building the engine repository is a different workflow.

```sh
tide version
tide build
tide build --web
```

For automated native runs, set `TIDE_HIDDEN=1` in that shell before running the game. PowerShell:

```powershell
$env:TIDE_HIDDEN = '1'
tide run --frames 120 --capture smoke.png --stats
```

POSIX:

```sh
TIDE_HIDDEN=1 tide run --frames 120 --capture smoke.png --stats
```

This finishes after 120 frames and captures the final picture. Add `--size 640 360` to test a particular logical window size; use `Screen.width` and `Screen.height` for responsive layout. Open the PNG and inspect it; a successful build alone does not establish that the game draws or behaves correctly. If inspection is inconclusive, take a later capture or check simulation values with `Debug.Log`/`Debug.Assert` before concluding the game is blank or broken. For input, use `tide run --script smoke.txt --frames 180 --capture smoke.png --stats`, with a script such as:

```text
1 key d down
60 key d up
120 capture after-move.png
150 quit
```

Script times are frames, and lines go in frame order; key actions use `down`/`up`, or neither for a one-frame tap. A `capture` path in a script is relative to the script's folder. `--players 2` runs two game processes in one match over a simulated network on this machine, with `player 2` after a frame number to target the second player; each capture name gains the player's number, `after-move-1.png` and `after-move-2.png`. `300 player 2 leave`, `drop` and `rejoin` test what happens when a player goes and comes back. It does not exercise real network transports. Read [CLI capture and scripts](../tools/cli.md#without-a-window) for the supported actions, output naming and exit codes. A web build still needs a browser test; native captures do not verify WebGL/WebGPU. Launch automated Chrome/Edge with `--mute-audio` and a bounded test, not an unbounded `tide run --web`.

For numerical behavior, supplement pictures with `Debug.Log` of state changes or `Debug.Assert` of invariants. A multiplayer money transfer can assert that balances never go negative and that the pool plus all balances stays constant; a collector can log cell and score after each move. With several players, a client runs a tick again after a wrong guess about another player's input, and logs in it print again: include `time.tick` in the line. Check the actual log as well as the captures. A failed or unclear image inspection is not evidence of a replication bug.

Fix diagnostics using the current compiler and relevant reference rather than guessing APIs. Report the builds, behavior checks and platforms actually tested. If a tool or backend is unavailable, say which check remains unverified.

## References on demand

The [documentation index](/llms.txt) links to clean Markdown pages. The optional [full documentation download](/llms-full.txt) is for offline searching, not required reading on every task. The [language spec](../spec.md) separates decided behavior from provisional implementation and open proposals; an open design is not an available API.

Use [Assets](../game/assets.md) for imports, [Packages](../packages/index.md) for optional dependencies and their pinned documentation, [Determinism](../multiplayer/determinism.md) for simulation constraints, and [Visibility](../game/visibility.md) for secrets and recipient state. Keep gameplay state deterministic and use Tide's math/random APIs; C reached through `extern` is trusted and can violate those guarantees.
