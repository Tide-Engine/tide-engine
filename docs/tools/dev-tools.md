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

## Scenarios

A scenario is a dev scene: a situation to start in, like a fight already under way or a range with one gun and a few targets. `--scene` starts the game in it, in place of its start scene, and can give it fields:

```csharp
dev scene Battle
{
    int side = 500;
    bool alone;
}

dev event(Spawned) Arena(Battle battle)
{
    for (int i = 0; i < battle.side / 50; i++) Spawn(Soldier { });
}
```

```sh
tide run --scene Battle
tide run --scene "Battle { side = 50, alone = true }"
```

The game's usual start doesn't load, so what only it sets up doesn't happen; what the game sets up elsewhere, like as players join, still does. A scenario that wants the usual start too loads it, `Scene.Load(Main { })`. A build without dev doesn't have dev scenes, so `--scene` can't name one there.

## Tweaks

A tweak is a value someone tunes without touching code: how hard a gun kicks, how fast a horse runs, the fog's color. The code declares it with its value, and `tweaks.json`, beside the game's code, gives it another:

```csharp
tweak float KickUp = 0.00012;
tweak Color Fog = Color(0.6, 0.65, 0.7, 1);

namespace Combat
{
    enum Rate { Slow, Fast }
    tweak Rate Pace = Rate.Slow;
}

system Recoil(mut Gun gun) { gun.kick += KickUp; }
```

```json
{
    "KickUp": 0.00015,
    "Fog": [0.5, 0.55, 0.6, 1],
    "Combat.Pace": "Fast"
}
```

- **The file holds only what differs from the code**, by each tweak's name with its namespace: a number, `true` or `false`, a list of numbers for a vector or a Color, or an enum member's name. A name no tweak has is a warning.
- **A tweak is a number, a bool, a vector, a Color or an enum.**
- **A type can have tweaks of its own**, which read like its fields but are the same for every value of it, so no entity holds one. `tweaks.json` names one with its type's name:

  ```csharp
  component Gun
  {
      int ammo = 30;
      tweak float kick = 0.1;

      float Kicked() { return kick * 2; } // By name in its methods
  }

  system Fire(Gun gun) { ... gun.kick ... Gun.kick ... }
  ```

  ```json
  { "Gun.kick": 0.3 }
  ```

  A value of the type, like `Gun { kick = 2 }`, can't give one, and code can't set one: it's tuned, not played.
- **Code reads it as a constant**, and a field's default can be one. What has to be known while compiling can't: an array's size, a `case`, a setting or a constant's value take a `const`.
- **A build without dev** has the file's values as constants: they cost what a constant does, and the file isn't shipped.
- **A dev build** reads them from the match, so changing one changes it for everyone in the match at once, through the server. GPU and audio code read the value the build had.
- **In the editor**, a tweak `tweaks.json` gives another value has a line above it with that value, which opens the file there, and its hover says so. In `tweaks.json`, a tweak's name goes to its declaration and hovers with the code's value.
- **Changing them while the game runs**: save `tweaks.json` in any editor and the running game has the new values within half a second, without building again. Or open the dev menu, whose Tweaks section has an editor for each, a switch for a bool, a field for a number, a vector or a color, and a button that steps through an enum's members, and which writes `tweaks.json` as a value changes, so it's kept.

## The dev menu

In a dev build with dev views or tweaks, the key under **Escape** (`` ` `` on a US keyboard, where Valve's games open their console) shows the dev menu, and again hides it. It lists the game's dev views, each with a switch, and its [tweaks](#tweaks). A dev view runs only while it's switched on, so a game's tools stay out of the way until someone wants one, and they don't each need a key of their own. What's on stays on across a hot reload.

The game sees the key too. Interactive runs list the menu's switches among the GUI's widgets, so an agent can click one by name (see [Interactive play](./cli#interactive-play)).
