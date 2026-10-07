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

- A file has at most one `namespace Name;` line, and files without one are in the global namespace. Namespaces can be dotted: `namespace Game.Combat;`.
- `namespace Name;` and `using` come before any declaration.
- A plain name is looked up in the namespace of the code that writes it, then the namespaces around it out to the global namespace, then the `using` ones. So a name your game declares itself always means yours, even when a namespace you import has one too: write the namespace, `Physics.Body`, for the imported one. If two `using` namespaces both have a name that nothing of yours has, it's ambiguous: write the namespace.
- Qualified names work wherever a type is named: parameters (`mut Combat.Health health`), `with` and `without`, component values, `Spawn`, `Add` and `Remove`, and `[Before]` and `[After]` (`[After(Physics.Gravity)]`). They work for constants too: `Combat.CRIT_MULTIPLIER`.
- The same name can be declared in different namespaces.
- An [override](overrides.md) names what it replaces the same way: `override system Combat.Move(...)`, or `Move` alone after `using Combat;`.
- There's exactly one `Main` in a game, in any file or namespace.

### Several namespaces in one file

A namespace can also be a block, which puts only what's between its braces in it. A file can have several, so small things that belong together can stay in one file and still keep their names apart:

```csharp
namespace Combat
{
    component Health { int value = 100; }

    namespace Rules
    {
        int Cap(int value) { return Math.Min(value, 100); }
    }

    system Heal(mut Health health)
    {
        health.value = Combat.Rules.Cap(health.value + 1);
    }
}

namespace Items
{
    component Bag { int coins; }
}

scene Main { }

event(Spawned) Setup(with Main)
{
    Spawn(Combat.Health, Items.Bag);
}
```

- A block goes wherever a declaration does, and one can be inside another: `Rules` above is `Combat.Rules`. A block's name can be dotted, `namespace Game.Items { }`.
- A block is inside the namespace it's written in. In a file that starts with `namespace Game;`, the block `namespace Combat { }` is `Game.Combat`.
- The same namespace can be a block more than once, in one file or in several, and a file's own namespace somewhere else: it's one namespace, and each sees what the others declare by its plain name.
- Code in a block finds plain names in its block's namespace first, then the ones around it, then the file's `using` ones. `using` lines stay at the top of the file, and are for every block in it.
- A name with a namespace is written whole, from the outermost one: `Combat.Rules.Cap`, inside `Combat` too.
- A block changes no order: its systems run where they're written in the file, like any others.
- Attributes, `local`, `virtual` and `override` go before the declarations inside a block, not before the block.

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
- Conditions take `!`, `&&`, `||`, `==`, `!=`, parentheses, `true` and `false`. A symbol that isn't one of tide's versions is an error, so a misspelled one doesn't quietly leave code out.
- `#if`, `#elif`, `#else` and `#endif` each go on a line of their own, which a `//` comment can end, and an `#if` can hold more of them.
- The lines a directive leaves out aren't read as code, only looked through for the directives that end them. They can hold code a version of tide you don't have reads, with syntax it doesn't know yet.
- There are no symbols for platforms, or for debug and release builds: every player of a match has to run the same code, on every platform.

Editors gray out the lines left out, and the formatter leaves them, and the directives, as they are.
