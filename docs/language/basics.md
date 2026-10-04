# Basics

Tide's syntax is C#-like. If you know C#, C or Java, most of it will read as you expect. This page covers what's the same everywhere in the language: values, variables and control flow. What's special to Tide starts with [components and entities](../game/entities.md).

## Style

- Types, systems and methods use PascalCase: `Transform`, `MovePlayer`, `Spawn(...)`.
- Fields, parameters and locals use camelCase: `trs.position`, not `trs.Position`. So do public properties, even static ones: `Color.red`, `quaternion.identity`.
- True constants use FULL_CASE: `Math.PI`, `Math.TAU`.
- Names are ASCII. Other characters go in comments and text. Names starting with `tide_` are the engine's, for the C it generates.
- Braces go on lines of their own, and so does an `else` after a block. A block that fits on one line can stay there: `scene Main { }`, `if (dead) { return; }`.

The editors' formatter lays code out this way (see [Editors](../tools/editors.md)).

## Types

| Type | What it holds |
|---|---|
| `bool` | `true` or `false` |
| `int` | A 32-bit integer |
| `long` | A 64-bit integer, for what outgrows an `int`: scores, money, tick counts over months |
| `float` | A 32-bit float. There's no `double`. |
| `float2`, `float3`, `float4`, `int2`, `int3`, `int4` | Vectors (see [Math](./math.md)) |
| `quaternion`, `float2x2`, `float3x3`, `float4x4` | Rotations and matrices (see [Math](./math.md)) |
| `Color` | A color, with `r`, `g`, `b` and `a` from 0 to 1 (see [Views](../game/views.md)) |
| `Rect` | A rectangle on the screen: `x`, `y`, `width` and `height` (see [GUI](../game/gui.md)) |
| `string` | Text (see [Text and lists](./text-and-lists.md)) |
| `List<T>` | A list of values (see [Text and lists](./text-and-lists.md)) |
| `Map<K, V>`, `Set<K>` | Values by key, and keys each once, in the order they were added (see [Text and lists](./text-and-lists.md#maps-and-sets)) |
| `Grid2<T>`, `Grid3<T>` | Cells at `int2` or `int3` positions, in fields (see [Grids](./grids.md)) |
| `Entity` | A handle to an entity (see [Components and entities](../game/entities.md)) |
| `LocalEntity` | A handle to one of this machine's own entities (see [Local state](../game/local-state.md)) |
| `PlayerID` | A player (see [Input](../game/input.md)) |
| `T?`, like `int?` | A value or nothing (see [Errors](./errors.md)) |

The engine has enums of its own too, on the pages they belong to: `Anchor` ([GUI](../game/gui.md)), `Filter` ([Views and drawing](../game/views.md#textures)), `SceneVisibility` ([Scenes](../game/scenes.md)), `SessionState`, `DisconnectReason` and `Prediction` ([Matches](../multiplayer/matches.md)).

Everything is a value: assigning copies it, and nothing is shared, text and lists included. That's what keeps the whole world plain data, which the engine copies to take snapshots.

### Numbers

- `1.5` is a `float`, and the `f` suffix is optional. An `int` converts to `float` by itself, never the other way: `int(x)` truncates toward zero.
- An `int` converts to `long` by itself too, and the two mix: `total + 1` is a `long` when `total` is. A `long` and a `float` don't: write `float(x)` or `long(x)` to say which way the value goes, and `int(x)` to cut a `long` down to its low 32 bits. `5L` is a `long` literal, and so is any integer literal past the `int` range.
- Integers can be written in decimal (`255`), hex (`0xFF`) or binary (`0b1010`), and `_` separates digits: `1_000_000`.
- Integer arithmetic wraps on overflow, and dividing by zero gives 0. No input can crash the simulation.
- Bitwise operators `& | ^ ~ << >>` work on `int` and `long`, as in C#.

## Variables

Everything is read-only unless it's `mut`, locals included:

```csharp
var speed = player.speed * 2;    // read-only, type inferred
mut var total = 0.0;             // mutable, type inferred
float limit = 10;                // read-only, explicit type
mut float scale = 1;             // mutable, explicit type
```

A local needs a value when it's declared, and can't reuse the name of another local or parameter in scope.

A local that's never read is a warning. It's usually a value worked out and then forgotten:

```csharp
var move = Math.Normalize(float3(input.horizontal, 0, input.vertical));
var fwd = Math.Mul(player.rotation, move);   // Warning: local 'fwd' is never used
player.position += move * 5 * time.dt;       // ...this meant fwd
```

A local that's only ever assigned counts as never read. The warning says what to do: use the local or remove it, and when its value is a call that does something, like `var enemy = Spawn(Enemy);`, keep the call as a statement, `Spawn(Enemy);`. The editors offer both as quick fixes.

The names a loop needs don't count: `foreach (var touch in touches) count += 1;` is fine without reading `touch`.

## Constants

`const` declares a value that code reads by name. Constants go at the top level of a file:

```csharp
const int MAX_HEALTH = 100;
const float REGEN_PER_SECOND = MAX_HEALTH / 20.0;

component Health
{
    float value = MAX_HEALTH;
}

system Regenerate(mut Health health, Time time)
{
    health.value = Math.Min(health.value + REGEN_PER_SECOND * time.dt, MAX_HEALTH);
}
```

- The type is written out. The value is worked out from literals, other constants, constructors, `Math` and operators, like a field's default. It can't read fields, singletons or `Time`.
- Constants are named in FULL_CASE.
- A constant is the same on every machine. Any code can read one: systems, views, handlers, functions and other constants. Reading one never makes a system wait for another.
- They work anywhere a constant value is needed: field defaults, `[Clamp]` bounds, `case` labels and the values of enum members. In the last two, int constants and operators on them work: `case MAX_LEVEL + 1:`.
- A constant can be a number, vector, matrix, quaternion, `bool`, `Color`, `Rect`, `PlayerID`, text, struct or enum. It can't be a list or an entity.
- A constant in a namespace belongs to it: code outside writes `Combat.CRIT_MULTIPLIER` (see [Namespaces and files](./namespaces.md)).
- Changing a constant under `tide run` reloads the game and keeps the match where it is.

## Settings

`settings` sets the engine's settings for the game. It goes at the top level of any one of the game's files:

```csharp
settings
{
    title = "Asteroids";
    tickRate = 30;
}
```

| Setting | What it is | Without it |
|---|---|---|
| `title` | The window's title | The game's folder's name |
| `tickRate` | How many times a second the match ticks, from 1 to 1000: `Time.dt` is 1 / `tickRate` | 60 |
| `maxPlayers` | How many players a match takes, from 1 to 256. Builds with different values can't play together (see [Matches](../multiplayer/matches.md#how-many-players)) | 16 |
| `hostMigration` | When the machine running a room's match goes, another player's takes it over (see [Host migration](../multiplayer/matches.md#host-migration)) | `false` |
| `prediction` | How clients keep up with the match: `Prediction.Rollback` runs ahead with no input delay and runs ticks again when a guess was wrong; `Prediction.Delay` delays inputs instead and runs every tick once (see [Input delay](../multiplayer/matches.md#input-delay)) | `Prediction.Rollback` |
| `appId` | The app's ID on phones, like `com.studio.game`: what Android and iOS, and their app stores, know it by (see [Android](../tools/cli.md#android)) | `dev.tide.<the game's name>`, to test with |
| `version` | The version people see, like `1.2.0`: one to three numbers between dots. Each build's version code, which app stores order updates by, goes up by itself | `1.0` |

- Settings are set without a type, to constant expressions, which can name constants. A game's own values go in constants, not settings.
- A game has one `settings` block. The editors complete the settings' names and show what each one does.
- Settings are part of the build. The machine that runs a match decides its tick rate, and every player ticks at that rate. Under `tide run`, a new `tickRate` takes effect when a match starts.
- `--title` goes over the `title` setting (see [The tide command](../tools/cli.md)).

## Logging and asserting

`Debug.Log(text)` prints a line: to the terminal under `tide run`, to the browser's console on the web, to the system's log on Android. `Debug.Assert(condition)`, or `Debug.Assert(condition, text)`, stops the program when the condition is false and says where, with the source line and the text. Both go anywhere: systems, views, handlers, functions.

```csharp
system Fall(Time time, mut Body body)
{
    body.velocity.y -= 9.8 * time.dt;
    Debug.Assert(body.velocity.y > -1000, $"{body.velocity.y} is too fast to be right");
    if (time.tick % 60 == 0) Debug.Log($"tick {time.tick}: {body.position}");
}
```

Neither changes the match, so the simulation stays the same on every machine with or without them. A client runs a tick again when a guess about another player's input was wrong (see [Networking](../multiplayer/networking.md#prediction-and-rollback)), and a log in it prints again: prefix what you log of the match with `Time.tick` to tell the runs apart. `tide run --stats` shows the three systems that took longest in the last tick, beside the frame rate.

## Default values

`default` is the default value of the type where it goes, so the type doesn't have to be spelled out:

```csharp
Draw.Circle(default, 5, Color.yellow);  // At float2(0, 0)
float2 center = default;
if (target == default) { return; }      // The null entity
```

It's the value a field of that type starts at: zero for numbers, vectors, matrices and colors, `false`, empty text, an empty list, the null entity, no player, and for a struct or component, its fields' defaults, as `Stats { }` makes them.

Its type comes from where it goes: a typed local, an assignment, an argument, a `return`, a field's value, or the other side of `==`, `!=` or `?:`. `var x = default;` says no type, so it's an error. `default` is only ever compared, never added or multiplied.

## Operators

Operators and their precedence follow C#, including compound assignments (`+=`, `<<=` and the rest) and `cond ? a : b`. `i++`, `i--`, `++i` and `--i` are statements, the same as `i += 1` and `i -= 1`, not expressions.

`??`, `is`, `!` after a value and `try` unwrap what a function that can fail gives, and `T?` values (see [Errors](./errors.md)).

Expressions run left to right: operands, arguments and field values in the order they're written. That's part of what makes every machine spawn the same entities with the same IDs.

So what happens in order, `Spawn`, `Scene.Load` and the GUI's widgets, can't go where it would only run sometimes, or again and again: on the right of `&&` or `||`, in a side of `?:`, or in a loop's condition or a `for`'s step. Spawn into a local first, or use `if` and `else`.

## Control flow

`if` and `else`, `switch`, and the loops `for`, `foreach` and `while`, with `break`, `continue` and `return`, as in C#.

```csharp
for (var i = 0; i < 10; i++)
{
    if (i == 3) continue;
    total += i;
}

foreach (var score in scores)
{
    best = Math.Max(best, score);
}

while (fuel > 0)
{
    fuel -= burn;
}
```

A `for` loop's variable is read-only in its body unless it's declared `mut var`; the loop's step can change it either way. `foreach` goes through a list in order, and each element is a read-only copy; `foreach (int score in scores)` names their type. `foreach` and `parallel` go through a grid's cells too (see [Grids](./grids.md)). There's no `do ... while` yet.

## Enums and switch

Enums and `switch` follow C#:

```csharp
enum Phase
{
    Warmup,
    Playing = 5,
    Over,
}

system Advance(mut Match match)
{
    switch (match.phase)
    {
        case Phase.Warmup:
            match.phase = Phase.Playing;
            break;
        case Phase.Playing:
        case Phase.Over:
            break;
    }
}
```

- A member without a value is one more than the one before it, and the first is 0. A value is an int, which can come from constants: `Playing = FIRST_LEVEL + 1`.
- Members are always written with their enum: `Phase.Playing`.
- `==` and `!=` compare two values of the same enum, and `int(phase)` gives a member's value. There's no way from an int to an enum yet.
- An enum takes four bytes, like an int. `enum Voxel : byte { ... }` takes one, for members from 0 to 255, and `: ushort` two, for 0 to 65535: worth it where there are a lot of them, like a grid's cells.
- `switch` works on ints, longs and enums. A case is an int (or a long, for a long), an enum's member, or a constant. Every section ends with `break` or `return`, so none runs into the next.

## Comments and attributes

Comments are `//` and `/* */`.

Attributes, in square brackets, are only metadata: when a system runs, or the bounds of an input's field, never what code does. The engine can enforce what one declares, as it clamps an input's field to its `[Clamp]`.

```csharp
[After(Gravity)]
system Move(mut Body body) { ... }
```
