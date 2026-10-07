# Namespaces and files

## Files

A game is every `.tide` file in its folder and its subfolders, and those of the [packages](../tools/packages.md) it uses. Every declaration is visible from every file, and there are no imports between files. How you split a game into files is up to you: the editors can move a declaration to a file of its own. A subfolder with a `tide.packages` of its own is a game or a package of its own, and isn't part of the game.

Files compile in order of their paths, and that's the default order of systems and views: by file, then as they're written in each file (see [Systems](../game/systems.md#order)). Packages' files come first.

## Namespaces

Large games keep names apart with namespaces. `namespace` at the top of a file puts everything in it in that namespace:

```csharp
// combat.tide
namespace Combat;

component Health { int value = 100; }
```

Code outside the namespace names its declarations with it, `Combat.Health`, or imports the namespace with `using`:

```csharp
// main.tide
using Combat;

scene Main { }

event(Spawned) Setup(with Main)
{
    Spawn(Health, Items.Health { value = 3 });
}
```

- A file has at most one namespace, and files without one are in the global namespace. Namespaces can be dotted: `namespace Game.Combat;`.
- `namespace` and `using` come before any declaration.
- A plain name is looked up in the file's namespace, then the namespaces around it out to the global namespace, then the `using` ones. So a name your game declares itself always means yours, even when a namespace you import has one too: write the namespace, `Physics.Body`, for the imported one. If two `using` namespaces both have a name that nothing of yours has, it's ambiguous: write the namespace.
- Qualified names work wherever a type is named: parameters (`mut Combat.Health health`), `with` and `without`, component values, `Spawn`, `Add` and `Remove`, and `[Before]` and `[After]` (`[After(Physics.Gravity)]`). They work for constants too: `Combat.CRIT_MULTIPLIER`.
- The same name can be declared in different namespaces.
- An [override](overrides.md) names what it replaces the same way: `override system Combat.Move(...)`, or `Move` alone after `using Combat;`.
- There's exactly one `Main` in a game, in any file or namespace.

## Code for other versions of tide

Tide still changes between versions. Code that has to build with several, like a [package](../tools/packages.md)'s, picks what each version reads with `#if`:

```csharp
#if TIDE_0_4_OR_NEWER
const int SLOTS = 8;
#elif TIDE_0_3_OR_NEWER
const int SLOTS = 4;
#else
const int SLOTS = 1;
#endif
```

- `TIDE_0_4_OR_NEWER` is true in tide 0.4 and every version after it, and `TIDE_0_4_2_OR_NEWER` in 0.4.2 and after. A nightly version counts as the version it comes before: 0.4.0-nightly.3 is 0.4.
- Conditions take `!`, `&&`, `||`, `==`, `!=`, parentheses, `true` and `false`. A symbol that starts with `TIDE_` and isn't one of tide's versions is an error, so a misspelled one doesn't quietly leave code out.
- `#if`, `#elif`, `#else` and `#endif` each go on a line of their own, which a `//` comment can end, and an `#if` can hold more of them.
- The lines a directive leaves out aren't read as code, only looked through for the directives that end them. They can hold code a version of tide you don't have reads, with syntax it doesn't know yet.

Editors gray out the lines left out, and the formatter leaves them, and the directives, as they are.

## Symbols of your own

An `#if` also reads symbols that you, the build or the game's packages define. A symbol is defined or it isn't: it has no value, and one that nothing defines is false.

```csharp
#define TRACE_HITS

#if TRACE_HITS && DEBUG
event(Hit hit) Trace(Health health)
{
    Debug.Log($"hit for {hit.damage}, {health.current} left");
}
#endif
```

- **`#define NAME`** defines a symbol for the file it's in, and **`#undef NAME`** takes one away there, the build's too. Both go at the top of the file, before its first declaration, as they're for all of it. Other files don't see them.
- **`--define NAME`** on `tide run`, `tide build`, `tide schedule` and `tide shaders` defines a symbol for every file of the game and its packages, as many times as you have symbols: `tide build --define DEMO --define STEAM`.
- **`DEBUG`** is defined in every build but a `--release` one.
- **`PACKAGE_<NAME>`** is defined for each package the game builds with, its name in capitals: `PACKAGE_BOX2D` when the game's `tide.packages` lists Box2D. So a package, or a game, can have code that's only there beside another package:

```csharp
#if PACKAGE_BOX2D
using Box2D;

system Splash(Body body, with Raindrop) { ... }
#endif
```

- Names that start with `TIDE_` and `PACKAGE_` are tide's: neither `#define` nor `--define` takes one.
- There are no symbols for platforms: a game is the same code on desktop, phones and the web, so their players can share a match.

::: warning Builds that differ are different games
Every machine of a match has to run the same code. So when a game's `#if` lines read `DEBUG`, a `--define` symbol or a package's, the answers are part of what the game is: a debug build and a release build of such a game, or two builds with different `--define`s, can't join each other's matches. A game whose conditions read none of them, or only tide's versions and its files' own `#define`s, isn't affected.
:::

Editors read `DEBUG` as defined, and know the game's packages. They don't know what a command line will define, so code under `#if DEMO` is grayed out there.
