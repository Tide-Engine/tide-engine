# Virtual and override

A [package](../tools/packages.md) can replace code of the game it's added to, and a game can replace code of a package it uses: an expansion that changes how units move, a variant of a rule, a mod a developer builds into their game. What can be replaced is marked `virtual`, and what replaces it is marked `override`.

```csharp
// The game
namespace Combat;

virtual system Move(mut Position position, Velocity velocity, Time time)
{
    position.value += velocity.value * time.dt;
}
```

```csharp
// A package, Fast
namespace Fast;

using Combat;

override system Move(mut Position position, Velocity velocity, Time time)
{
    position.value += velocity.value * time.dt * 2;
}
```

Both words are always written. Nothing is replaced by having the same name, and nothing can be replaced that its developer didn't mark.

It happens while compiling. What was replaced isn't in the build, so an override costs what the code it replaced would: there's no table of functions, and nothing is looked up as the game runs.

## What can be replaced

Declarations with code: systems, views, handlers (an event's, a command's, and local ones), functions, methods, frags and verts.

```csharp
virtual view Show(Body body) { ... }
virtual event(Hit hit) TakeHit(mut Health health) { ... }
virtual float Damage(float base, float multiplier = 1) { ... }
virtual frag Shade(BoxVertex vertex, mut Color color) { ... }

struct Stats
{
    float health = 100;

    virtual bool IsDead() { return health <= 0; }
}
```

Data isn't replaced: components, singletons, structs, events, commands, enums, scenes, the input, constants and the settings. Neither are `extern` functions, whose code is C, nor operators and `Interpolate`. A package that needs more data adds components of its own to the game's entities, as any code does.

`virtual` and `override` are words of their own only before a declaration. Anywhere else they're names like any other.

## Naming what's replaced

An override has the name of what it replaces, written as any reference to it is: with its namespace, or alone where the file imports the namespace with `using`.

```csharp
override system Combat.Move(mut Combat.Position position, Combat.Velocity velocity, Combat.Time time) { ... }
```

A method's override is written outside its type, in the package that replaces it, with the type before the method's name. The type's fields are in scope, as they are in the type's body:

```csharp
override bool Stats.IsDead()
{
    return health <= 10;
}
```

An override's code reads names as its own file does: its namespace and its `using` lines, not those of the file it replaces something of.

## The same name and parameters

The name alone doesn't say which declaration is meant, since several can [share a name](functions.md#default-values-names-and-overloads). An override also has the parameters of what it replaces: their types in order, with a system's filters (`with`, `without`), which is what tells declarations that share a name apart.

```csharp
virtual system Rest(mut Health health, with Player) { ... }
virtual system Rest(mut Health health, without Player) { ... }

// Replaces the second
override system Rest(mut Health health, without Player) { ... }
```

What can differ: the parameters' names, `mut`, and a function's default values. Calls use the override's defaults, and the schedule is worked out from what the override reads and writes, since that's what's built.

What has to be the same, each an error that says what the declaration it replaces has:

- What it is: a view's override is a view, a frag's a frag.
- A handler's event, whether it's `local`, and `verified` on its event.
- A function's or method's return type, what it `fails` with, and which parameters are `out`.
- Whether it's `async`.

An override that matches nothing is an error that lists what the name has.

## In its place

An override takes the place of what it replaces:

- **In the order.** It runs where the other would have: at its place in the [default order](../game/systems.md#order), with its `[Stage]`, `[Before]` and `[After]`. An override writes none of its own.
- **Under its name.** Everything that names the declaration means the override: `[After(Move)]` on another system, a call of a function, a method called on a value, a mesh that draws through a frag.

```csharp
// The game
[After(Gravity)]
virtual system Move(mut Position position, Velocity velocity, Time time) { ... }

[After(Move)]
system Clamp(mut Position position) { ... }
```

With the package above, the tick runs `Gravity`, then the package's `Move`, then `Clamp`. `tide schedule` shows it under the name `Combat.Move`.

Nothing calls the code that was replaced: an override is the whole of it.

## One override, from another package

- A declaration is replaced once. Two overrides of the same one are an error that names both, so the order a game lists its packages in changes nothing.
- An override is in another package than what it replaces, the game counting as one: a package replaces the game's code or another package's, and a game a package's. Code in the same package is changed where it is.
- A package can mark its own declarations `virtual`, for the game or for other packages.

## What was replaced

It's still checked for errors, as the code it is: its developer reads it and changes it, and a game is built without its packages too. It warns about nothing, and nothing of it is built: what it spawned, sent or drew counts for nothing.

A virtual declaration's parameters are what its overrides get, and an override's are those of what it replaces, so neither is warned about a parameter it doesn't use. That makes a hook possible: a virtual system that does nothing, for a package to fill in.

```csharp
virtual system OnRoundStart(mut Score score, Time time)
{
}
```

## In the editor

- Above a declaration that was replaced, a line says which package replaced it, and above an override, what it replaces. Hovering either says the same.
- Go to definition on an override's name goes to what it replaces, and go to implementation on a virtual declaration goes to its override.
- Find usages and rename treat the two as one name.
- After `override`, completion lists what's left to replace, each written out with its parameters.
