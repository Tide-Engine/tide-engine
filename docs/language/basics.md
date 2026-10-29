---
description: "Learn Tide language syntax, types, variables, operators and control flow, plus enums, attributes and settings for your game."
---

# Basics

Tide is a language of its own. Its basics are the familiar kind, braces and semicolons, so if you've programmed before, most of this page will read as you expect. It covers what's the same everywhere in the language: values, variables and control flow. What's special to Tide starts with [components and entities](../game/entities.md).

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
| `byte` | An unsigned 8-bit integer, from 0 to 255; binary file contents use `List<byte>` |
| `int` | A 32-bit integer |
| `long` | A 64-bit integer, for what outgrows an `int`: scores, money, tick counts over months |
| `float` | A 32-bit float |
| `double` | A 64-bit float, for values needing more precision |
| `double2`, `double3`, `double4`, `float2`, `float3`, `float4`, `int2`, `int3`, `int4` | Vectors (see [Math](./math.md)) |
| `quaternion`, `float2x2`, `float3x3`, `float4x4`, `double2x2`, `double3x3`, `double4x4` | Rotations and matrices (see [Math](./math.md)) |
| `Color` | A color, with `r`, `g`, `b` and `a` from 0 to 1 (see [Views](../game/views.md)) |
| `Rect` | A rectangle on the screen: `x`, `y`, `width` and `height` (see [GUI](../game/gui.md)) |
| `string` | Text (see [Text and lists](./text-and-lists.md)) |
| `char` | One character of text, like `'a'` (see [Characters](./text-and-lists.md#characters)) |
| `List<T>` | A list of values (see [Text and lists](./text-and-lists.md)) |
| `T[N]`, like `int[32]` or `string[3]` | A fixed number of values, stored side by side (see [Arrays](./text-and-lists.md#arrays)) |
| `Map<K, V>`, `Set<K>` | Values by key, and keys each once, in the order they were added (see [Text and lists](./text-and-lists.md#maps-and-sets)) |
| `Grid2<T>`, `Grid3<T>` | Cells at `int2` or `int3` positions, in fields (see [Grids](./grids.md)) |
| `Entity` | A handle to an entity (see [Components and entities](../game/entities.md)) |
| `LocalEntity` | A handle to one of this machine's own entities (see [Local state](../game/local-state.md)) |
| `PlayerID` | A player (see [Input](../game/input.md)) |
| `T?`, like `int?` | A value or nothing (see [Errors](./errors.md)) |

The engine has enums of its own too, on the pages they belong to: `Anchor` ([GUI](../game/gui.md)), `Filter` ([Meshes and textures](../game/meshes-and-textures.md#textures)), `SessionState`, `DisconnectReason` and `Prediction` ([Matches](../multiplayer/matches.md)).

Everything is a value: assigning copies it, and nothing is shared, text and lists included. That's what keeps the whole world plain data, which the engine copies to take snapshots.

The type arguments in `List<T>`, `Map<K, V>` and the other containers pick what they hold. Containers can nest, such as `Map<string, List<int>>`. Declaring your own generic types or functions isn't supported yet; a struct or function names the concrete types it uses.

Give a concrete type another name with a [type alias](./namespaces.md#type-aliases), declared at namespace scope: `using Points = List<float3>;`. `Points` is the same type as `List<float3>`, with the same behavior and restrictions. Aliases don't declare type parameters of their own.

### Numbers

- `byte` stores one byte. It converts to `int`, `long` and `float` by itself, and arithmetic promotes it to `int`. Integer constants from 0 to 255 fit a byte: `byte value = 255; List<byte> bytes = [0, 128, 255];`. Other numbers need `byte(x)`, which keeps their low 8 bits; a float first converts with `int(x)`. Compound assignments and `++`/`--` wrap back into the byte range.
- `1.5` is a `float`, and the `f` suffix is optional. The zero before the point can be left out: `.5f` is `0.5`. An `int` converts to `float` by itself, never the other way: `int(x)` truncates toward zero.
- `1.25d`, `.5D` and `1e100d` are `double` literals. Unsuffixed decimals remain floats, even in `double value = 1.25;`: use `d` when the literal needs double precision. `byte`, `char`, `int`, `long` and `float` widen to `double` implicitly. Int and float vectors widen to the matching double vector. A long can round when converted to double, whose integer precision is 53 bits.
- Double arithmetic and mixed float/double arithmetic give doubles. Narrowing is explicit: `float(value)`, `float3(position)`, `int(value)` or `long(value)`. Integer conversions truncate, saturate at the destination's range and map NaN to zero. Double to float rounds to the nearest float, overflowing to a signed infinity.
- A decimal number ending in lowercase `deg` is a `float` in radians: `180deg`, `.5deg`, `1e2deg`, `1_8_0deg` and `-90deg`. The compiler converts the number from degrees once; use it anywhere a float is accepted, such as `quaternion.Euler(0, 180deg, 0)`.
- An `int` converts to `long` by itself too, and the two mix: `total + 1` is a `long` when `total` is. A `long` and a `float` don't: write `float(x)` or `long(x)` to say which way the value goes, and `int(x)` to cut a `long` down to its low 32 bits. `5L` is a `long` literal, and so is any integer literal past the `int` range.
- Integers can be written in decimal (`255`), hex (`0xFF`) or binary (`0b1010`), and `_` separates digits: `1_000_000`.
- Integer arithmetic wraps on overflow, and dividing by zero gives 0. No input can crash the simulation.
- Bitwise operators `& | ^ ~ << >>` work on `int` and `long`. `>>` keeps the sign, and a shift uses its count's low 5 bits, or 6 for a `long`: `1 << 33` is `2`.

## Variables

Everything is read-only unless it's `mut`, locals included:

```csharp
var speed = player.speed * 2;    // read-only, type inferred
mut var total = 0.0;             // mutable, type inferred
float limit = 10;                // read-only, explicit type
mut float scale = 1;             // mutable, explicit type
```

A local with an explicit type can be assigned later:

```csharp
Animation current;
switch (zombie.state)
{
    case ZombieState.Run: current = movementAnims.rootRun; break;
    case ZombieState.Jump: current = movementAnims.rootJump; break;
    default: current = movementAnims.rootRun; break;
}
```

Every path that reaches a read must assign the local first. Without `mut`, each path initializes it once; choosing its first value in different branches needs no `mut`. Declare `mut Animation current;` if the code changes it again. A read-only local declared inside a loop can be initialized once each iteration; one declared outside can't be repeatedly initialized by the loop.

`var` and `mut var` need an initializer to infer their type. A local can't reuse the name of another local or parameter in scope.

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

- The type is written out. The value is worked out from literals, other constants, constructors, `Math` and operators, like a field's default. It can't read fields, singletons or `Time`, nor an [asset](../game/assets.md), which only a field's default names.
- Constants are named in FULL_CASE.
- A constant is the same on every machine. Any code can read one: systems, views, handlers, functions and other constants. Reading one never makes a system wait for another.
- They work anywhere a constant value is needed: field defaults, `[Clamp]` bounds, `case` labels and the values of enum members. In the last two, int constants and operators on them work: `case MAX_LEVEL + 1:`.
- A constant can be a number, vector, matrix, quaternion, `bool`, `Color`, `Rect`, `PlayerID`, text, struct or enum. It can't be a list or an entity.
- A constant in a namespace belongs to it: code outside writes `Combat.CRIT_MULTIPLIER` (see [Namespaces and files](./namespaces.md)).
- Changing a constant under `tide run` reloads the game and keeps the match where it is.

## Settings

`settings` sets the engine's settings for the game. It goes at the top level of any of the game's files:

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
| `startScene` | The prefab or scene declaration the program starts in, such as `Levels.Start` | `Main` |
| `tickRate` | How many times a second the match ticks, from 1 to 1000: `Time.dt` is 1 / `tickRate`, and code reads the rate as `Time.tickRate` | 60 |
| `maxPlayers` | How many players a match takes, from 1 to 65536. Builds with different values can't play together (see [Matches](../multiplayer/matches.md#how-many-players)) | 16 |
| `hostMigration` | When the machine running a room's match goes, another player's takes it over (see [Host migration](../multiplayer/matches.md#host-migration)) | `false` |
| `prediction` | How clients keep up with the match: `Prediction.Rollback` runs ahead with no input delay and runs ticks again when a guess was wrong; `Prediction.Delay` delays inputs instead and runs every tick once (see [Input delay](../multiplayer/matches.md#input-delay)) | `Prediction.Rollback` |
| `appId` | The app's ID on phones, like `com.studio.game`: what Android and iOS, and their app stores, know it by (see [Android](../tools/cli.md#android)) | `dev.tide.<the game's name>`, to test with |
| `version` | The version people see, like `1.2.0`: one to three numbers between dots. Each build's version code, which app stores order updates by, goes up by itself | `1.0` |
| `stats` | Shows the stats in the window's bottom right corner: the frame rate, and at a click on it or F8, the rest (see [The tide command](../tools/cli.md#stats)). It's in what `tide build` makes too, so take it out before players get the game, unless the frame rate is theirs to see | `false` |
| `debugViews` | The engine's [debug views](../tools/debug-views.md) are on: F7 shows the profiler, which says where a frame's time and a tick's go. `--debug-views` turns them on too, whatever this says | `false` |

- Settings are set without a type. `startScene` names a prefab or scene declaration, without quotation marks; other settings take constant expressions, which can name constants. A game's own values go in constants, not settings.
- A game can have several `settings` blocks, in any of its files, so a setting can be written beside the code it's about: each block sets what the others don't. A setting that's set twice, in one block or in two, is an error that says where the other one is.
- The editors complete the names of the settings the game doesn't set yet, and show what each one does.
- Settings are part of the build. The machine that runs a match decides its tick rate, and every player ticks at that rate. Under `tide run`, a new `tickRate` takes effect when a match starts.
- `--title` goes over the `title` setting (see [The tide command](../tools/cli.md)).
- Startup uses `--scene` first, then `startScene`, then `Main` (see [Matches](../multiplayer/matches.md#starting-a-match-from-tide-run)).

## Logging and asserting

`Debug.Log(text)` prints a line: to the terminal under `tide run`, to the browser's console on the web, to the system's log on Android. `Debug.Assert(condition)`, or `Debug.Assert(condition, text)`, stops the program when the condition is false and says where, with the source line and the text. Both go anywhere: systems, views, handlers, functions.

Standalone [release builds](../tools/cli.md#run-and-build) keep assertions and messages you wrote, but replace the source path with an ID and omit the copied source line. Keep the build's private `game.names` map to resolve reported source IDs and line numbers.

```csharp
system Fall(Time time, mut Body body)
{
    body.velocity.y -= 9.8 * time.dt;
    Debug.Assert(body.velocity.y > -1000, $"{body.velocity.y} is too fast to be right");
    if (time.tick % 60 == 0) Debug.Log($"tick {time.tick}: {body.position}");
}
```

Neither changes the match, so the simulation stays the same on every machine with or without them. A client runs a tick again when a guess about another player's input was wrong (see [Networking](../multiplayer/networking.md#prediction-and-rollback)), and a log in it prints again: prefix what you log of the match with `Time.tick` to tell the runs apart. `tide run --stats`, or the `stats` setting, shows the three systems that took longest in the last tick, beside the frame rate, and the profiler shows where the rest of a tick and a frame go (see [Debug views](../tools/debug-views.md)).

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

From the ones that bind tightest to the loosest:

1. `-x`, `!x`, `~x`, `try` and `await`
2. `value switch { ... }`
3. `*`, `/`, `%`
4. `+`, `-`
5. `<<`, `>>`
6. `<`, `<=`, `>`, `>=`, `is`
7. `==`, `!=`
8. `&`
9. `^`
10. `|`
11. `&&`
12. `||`
13. `??`
14. `cond ? a : b`

Operators on one line group from the left: `a - b - c` is `(a - b) - c`. `??` and `?:` group from the right: `a ?? b ?? 0` is `a ?? (b ?? 0)`.

The arithmetic and bitwise operators have compound assignments: `+=`, `<<=` and the rest. `i++`, `i--`, `++i` and `--i` are the same as `i += 1` and `i -= 1`. Inside an expression they're a value too: `++i` is `i` after it changed and `i++` what it was before, so `(++count % 2 == 0) ? a : b` counts and picks in one line. In a frag or a vert, the statement they're in reads the variable nowhere else, and they don't go on a side of `?:` or after `&&` or `||`: the compiler says so, and what to write instead.

`??`, `is`, `!` after a value and `try` unwrap what a function that can fail gives, and `T?` values (see [Errors](./errors.md)).

Expressions run left to right: operands, arguments and field values in the order they're written, on every platform. That's part of what makes every machine spawn the same entities with the same IDs, and it holds for calls that change what they're given too: in `Pack(Next(counter), Next(counter))` the first `Next` runs first, and in `Pack(counter.n, Next(counter))` `counter.n` is read before `Next` changes it (see [Functions](./functions.md#order)).

So what happens in order, `Spawn`, `Scene.Load` and the GUI's widgets, can't go where it would only run sometimes, or again and again: on the right of `&&` or `||`, in a side of `?:`, or in a loop's condition or a `for`'s step. Spawn into a local first, or use `if` and `else`.

## Control flow

Tide has `if` and `else`, `switch`, and the loops `for`, `foreach` and `while`, with `break`, `continue` and `return`.

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

A `for` loop's variable is read-only in its body unless it's declared `mut var`; the loop's step can change it either way. `foreach` goes through a list in order, and each element is a read-only copy; `foreach (int score in scores)` names their type. `foreach` and `parallel` go through a grid's cells too (see [Grids](./grids.md)), and `foreach` through a text's characters (see [Characters](./text-and-lists.md#characters)). There's no `do ... while` yet.

## Enums and switch

An enum is a set of named values, and `switch` picks code by a value:

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
- Write a member with its enum, `Phase.Playing`, or use `.Playing` when the expected enum type is clear: `match.phase = .Playing;`. Typed declarations, field defaults, function arguments and returns, container elements and `case .Warmup:` inside an enum switch supply that type. Comparisons can use it too: `match.phase == .Playing`. A typed conditional can use shorthand in both branches: `Phase next = ready ? .Playing : .Warmup;`.
- Without an expected enum type, keep the qualified spelling: `var phase = Phase.Playing;`. If overloads could take different enums, qualify the argument; the compiler does not pick an enum by searching for its member name.
- `==` and `!=` compare two values of the same enum.
- An enum and its number convert both ways, written out: `int(phase)` gives a value's number, and `Phase(5)` the value with that number. `Combat.Stance(2)` does it for an enum in a namespace. See [From a number](#from-a-number) below.
- Write conversions as `int(phase)`, not `(int)phase`. The compiler explains the replacement when it encounters a parenthesized cast.
- `Phase.Count()` is the number of declared members, as a compile-time `int`. The `Phase` above has three members despite its numeric gaps. Members sharing the same numeric value each count. The call takes no arguments; a member you declare named `Count` remains `Phase.Count`.
- An enum takes four bytes, like an int. `enum Voxel : byte { ... }` takes one, for members from 0 to 255, and `: ushort` two, for 0 to 65535: worth it where there are a lot of them, like a grid's cells.
- `switch` works on ints, longs, [chars](./text-and-lists.md#characters) and enums. A case is an int (or a long, for a long), a char like `'a'`, an enum's member, or a constant. Every section ends with `break` or `return`, so none runs into the next.

### Switch expressions

A switch expression chooses a value, so it can initialize a local directly:

```csharp
Animation current = zombie.state switch
{
    ZombieState.Run => movementAnims.rootRun,
    ZombieState.Jump => movementAnims.rootJump,
    _ => movementAnims.rootRun,
};
```

The selector is evaluated once, and only the selected arm's value runs. Labels are constants of the same kinds a statement switch accepts. Each label appears once; the final `_` arm handles every other value and is required, including for enums, which can hold unnamed numbers. A trailing comma is allowed.

The arm values must have compatible types. A typed destination supplies the type of `default`, `null`, list literals and enum shorthand such as `.Jump`; otherwise the other arms supply it. An enum selector also supplies the type for shorthand labels such as `.Run`. Each arm has its own scope for locals declared with `out`. Patterns and guards aren't supported yet.

`switch` binds tighter than arithmetic: `(a + b) switch { ... }` switches on the sum. It works in functions, tasks and GPU code as well as systems, subject to the types those places support.

### From a number

`Phase(number)` is the enum's value with that number, the other way from `int(phase)`:

```csharp
enum Card { Vitality, RapidFire, Scatter, Ricochet }
const int CARDS = Card.Count();

// One of the cards, picked by a hash: the same on every machine
Card Offer(int round, int slot)
{
    return Card(Math.Hash(int2(round, slot)) % CARDS);
}
```

- It takes an `int` (or a `byte`). A `float` or a `long` goes through `int(...)` first, `Phase(int(x))`, and so does another enum's value. The compiler says which when it's something else.
- A number none of the members has is kept as it is: with the `Phase` at the top of this section, `Phase(3)` is a `Phase` whose number is 3. It equals no member, a `switch` on it runs its `default`, and text shows `3`. So bound the number first when it comes from somewhere that can be wrong, as the `% CARDS` does, or handle the rest in a `default`.
- An enum stored as a `byte` or a `ushort` keeps the number's low 8 or 16 bits, as `byte(x)` does: `Voxel(300)` is `Voxel(44)`.
- With a constant number it's a constant, so it works in a `const`, a field's default and a `case`.

For consecutive values starting at zero, advance and wrap with `%`:

```csharp
enum Direction { North, East, South, West }

Direction Next(Direction direction)
{
    return Direction((int(direction) + 1) % Direction.Count());
}
```

This assumes the starting value is a declared member. Member count is not a numeric range: this formula does not cycle through enums with gaps, negative values or duplicate numeric values. `Count()` works in shaders and constant expressions too.

## Comments and attributes

Comments are `//` and `/* */`.

Attributes, in square brackets, are only metadata: when a system runs, or the bounds of an input's field, never what code does. The engine can enforce what one declares, as it clamps an input's field to its `[Clamp]`.

```csharp
[After(Gravity)]
system Move(mut Body body) { ... }
```
