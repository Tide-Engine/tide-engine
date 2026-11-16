---
description: "Mark a game's own tools, cheats and panels 'dev': they're in what tide run builds and left out of what players get, and the key under Escape opens a menu of the dev views."
---

# Dev tools

A game makes its own tools: a panel to tune a gun, a command that gives ammo, a view that draws where enemies are headed. Mark them `dev`, and they're in the builds the team works with and left out of the ones players get.

```csharp
// Only a dev build has the command, so a player's server can't be sent it
dev command GiveAmmo { int amount; }

dev command(GiveAmmo give, PlayerID sender) Refill(mut Gun gun, with ownership)
{
    gun.ammo += give.amount;
}

// A panel only a dev build has, shown from the dev menu
dev view GunTools(Gun gun, with ownership)
{
    if (GUI.Button(Rect(12, 12, 160, 32), "Full ammo")) this.Send(GiveAmmo { amount = 999 });
}
```

`dev` goes before any declaration: a function, a system, a view, a handler, an event or a command, a component, a singleton, a struct, a scene, an enum or a constant. It goes before a type's method too, which then only dev builds have:

```csharp
struct Magazine
{
    int rounds;

    dev int Doubled() { return rounds * 2; }
}
```

A field can't be `dev`: a field is in every build. Keep what dev code needs in a dev component or singleton of its own.

## Dev builds

- **`tide run` builds with dev**, optimized or not (`--release`), native or for the web (`--web`), so a playtest build has its tools. `--no-dev` runs the game without them, and `--distribution` leaves them out too.
- **`tide build` leaves them out** unless it's given `--dev`.
- In CMake, `tide_add_game(... DEV ...)` builds with them.

## What code can use them

Code without `dev` can't use what's `dev`: not call a dev function or method (in a lambda too), send a dev command, handle a dev event, take a dev component or name a dev type. That holds in a dev build too, so a build without dev never fails where a dev build passed:

```csharp
dev command GiveAmmo { int amount; }

system Reload(Gun gun)
{
    if (gun.ammo == 0) this.Send(GiveAmmo { amount = 30 });
    // error: 'GiveAmmo' is 'dev', which builds without dev leave out, so 'Reload' can't use it
}
```

Mark the code that uses it `dev` too, or leave it to dev code. Dev code can use anything.

## The dev menu

In a dev build with dev views, the key under **Escape** (`` ` `` on a US keyboard, where Valve's games open their console) shows the dev menu, and again hides it. It lists the game's dev views, each with a switch. A dev view runs only while it's switched on, so a game's tools stay out of the way until someone wants one, and they don't each need a key of their own. What's on stays on across a hot reload.

The game sees the key too. Interactive runs list the menu's switches among the GUI's widgets, so an agent can click one by name (see [Interactive play](./cli#interactive-play)).
