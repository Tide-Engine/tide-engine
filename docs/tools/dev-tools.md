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

A tweak is a value someone tunes without touching code: how hard a gun kicks, how fast a horse runs, the fog's color. The code declares it with its value, and a `.tweaks` file beside the game's code gives it another:

```csharp
// combat.tide
tweak float KickUp = 0.00012;
tweak Color Fog = Color(0.6, 0.65, 0.7, 1);

namespace Combat
{
    enum Rate { Slow, Fast }
    tweak Rate Pace = Rate.Slow;
}

system Recoil(mut Gun gun) { gun.kick += KickUp; }
```

```csharp
// combat.tweaks
KickUp = 0.00015;   // Tuned with the new rifles
Fog = Color(0.5, 0.55, 0.6, 1);
Combat.Pace = Combat.Rate.Fast;
```

- **A `.tweaks` file holds only what differs from the code**: a line `Name = value;` for each, the tweak's name with its namespace. Comments are as in code.
- **A value is worked out**, as a constant's is: from numbers, other tweaks and constants, operators, constructors like `float3(...)` and `Color(...)`, and Math's functions. `Gun.kick = KickUp * 2000;` follows `KickUp`. A tweak worked out from itself, through others or not, is an error.
- **The game can have as many `.tweaks` files as suits it**: every one in its folder and the folders in it is read, so each person on a team can tune in a file of their own. A tweak set in two of them is an error, and a name no tweak has is a warning.
- **A tweak is a number, a bool, a vector, a Color or an enum.**
- **A type can have tweaks of its own**, which read like its fields but are the same for every value of it, so no entity holds one. A `.tweaks` file names one with its type's name:

  ```csharp
  component Gun
  {
      int ammo = 30;
      tweak float kick = 0.1;

      float Kicked() { return kick * 2; } // By name in its methods
  }

  system Fire(Gun gun) { ... gun.kick ... Gun.kick ... }
  ```

  ```csharp
  Gun.kick = 0.3;
  ```

  A value of the type, like `Gun { kick = 2 }`, can't give one, and code can't set one: it's tuned, not played.
- **Code reads it as a constant**, and a field's default can be one. What has to be known while compiling can't: an array's size, a `case`, a setting or a constant's value take a `const`.
- **A build without dev** has the files' values as constants: they cost what a constant does, and the files aren't shipped.
- **A dev build** reads them from the match, so changing one changes it for everyone in the match at once, through the server. GPU and audio code read the value the build had.
- **Changing them while the game runs**: under `tide run`, save a `.tweaks` file in any editor and the running game has the new values within about a second, without its code built again. Or open the dev menu, whose Tweaks section has an editor for each tweak written plainly, a switch for a bool, a field for a number, a vector or a color, and a button that steps through an enum's members. It writes the value into the `.tweaks` file that sets it, or into the one beside the code that declares the tweak (`combat.tide`'s `combat.tweaks`), keeping everything else in the file. A tweak worked out from others is shown with its value, and changes when they do. A new `.tweaks` file counts as soon as it's saved. All of this works the same in the browser under `tide run --web`, where tide writes the menu's changes into the files. A dev build run another way takes a saved file at its next build.
- **In the editor**, a `.tweaks` file is read with the game's code: a wrong value or an unknown name is underlined as you type, a tweak's name completes at a line's start, and a value completes like code. A tweak's name goes to its declaration and hovers with the code's value, and renaming a tweak renames it in its `.tweaks` files too. In the code, a tweak a `.tweaks` file gives another value has a line above it with that value, which opens the file there, and its hover says so.

## The dev menu

In a dev build, the key under **Escape** (`` ` `` on a US keyboard, where Valve's games open their console) shows the dev menu, and again hides it. Its **Dev** tab lists the game's dev views, each with a switch, its [tweaks](#tweaks) and its [checkpoints](#checkpoints); its **Match** and **Local** tabs are the [inspector](#the-inspector). A dev view runs only while it's switched on, so a game's tools stay out of the way until someone wants one, and they don't each need a key of their own. What's on stays on across a hot reload.

The game sees the key too. Interactive runs list the menu's switches among the GUI's widgets, so an agent can click one by name (see [Interactive play](./cli#interactive-play)).

## Pausing and going back

At the dev menu's top, whatever its tab, are the match's controls: **Pause** stops it, **Step** runs it a tick at a time, **Back** goes back a tick, showing the match as it was then, and the bar under them goes to any tick since the match began. **Go on from here** goes on from the tick shown, and what came after it is gone; **Play** goes on from the latest.

- While the match runs, a dev build keeps its last 600 ticks (10 seconds at 60 a second) to show at once, and any tick since it began is made again from what went into it.
- The game's views draw the tick shown. What they send or ask for meanwhile goes nowhere, and the [inspector](#the-inspector) shows that tick's fields as they were, without changing them.
- With players of other machines in the match, it goes on for them: this machine's frames stay on the tick shown, and its player lets go of everything meanwhile. **Go on from here** takes every machine back to that tick.
- It's the match this machine runs: a client of another machine's match, or a sharded one, doesn't pause, and the menu says why.

### Recordings

**Save recording**, in the Dev tab, keeps what the match was from its start: `checkpoints/recording-<n>.recording` in the project, beside the [checkpoints](#checkpoints). A click on one replays it in the match's place, playing at its rate, with the same controls: **Pause** and **Play**, **Back**, **Step** and the bar. Under them, the menu says whether this build makes every tick the way it was recorded, or which tick first goes otherwise: a change to the code shows there as the first tick it changes. **Stop replay** goes back to the match, which waited, paused, meanwhile (or went on, for other machines' players).

A recording replays in a build with the same data layout as the one that made it; one of another is refused, saying why.

## The inspector

The dev menu's **Match** and **Local** tabs are its inspector, which shows the game's state while it runs, and changes it. Nothing has to be written for it: every dev build has one, made from the game's own types.

- **Match** shows the match as a tree: its singletons by name, then its entities, each under its parent (`SetParent`). An entity's row says the components it has and a value of one of them: a text field's if it has one (`Room "Hall"`), else its first field's (`Lamp level 2`), so the field to see first is the one declared first. A row opens in place to its fields, and an entity's to its number, its components' fields, its local fields, then the entities under it. Every entity is listed; a long list scrolls, and a deep one scrolls sideways too (Shift with the wheel). The engine's own state (a command's sequence numbers, say) isn't listed.
- **Local** shows this machine's local state the same way: its local singletons, and the local entities its views spawned.
- **A field changes where it's shown**: a switch for a bool, a field for a number, a vector or a Color, a button that steps through an enum's members. Text, lists and maps are shown as they are, and an entity a field holds is a link, saying what it's to as its row does, that opens it in the tree and scrolls to it.
- **A change of the match's goes through the server**, so every machine of the match has it, and the field shows what was sent until it's back. The game's [`Changed<...>` handlers](../game/events.md#when-a-field-changes) hear it as they would a change of the game's own, so what follows from a field follows from an edit too:

```csharp
// The inspector sets a unit's hp; its handler does the rest, as for a hit
event(Changed<Unit.hp> change) Down(mut Unit unit)
{
    unit.down = unit.hp <= 0;
}
```

- **A change of the local world's** is this machine's, made at once.
- Any player of a dev build can change a field. Only a value its field can hold is taken: a member of the enum, a bool that's true or false, a number that's a number. An entity that's gone by then is left alone.

## Checkpoints

A checkpoint is the match saved as it is, to start from again: the boss fight at low health, the base with every building, the moment a bug shows. Save one, play on, and load it to be back there; a change to the code doesn't lose it.

- **Save** in the dev menu's Dev tab saves the match on this machine's server as the next `checkpoint-<n>`. The tab lists the checkpoints, newest first, and a click on one loads it.
- **Loading one starts the game over from it**, as Ctrl+R starts it over: the match is the one saved, and this machine's player is the one who saved it. Local state (the camera, menus) starts fresh, and the game's local code makes it from the match as it would at any start. Ctrl+R then starts over from that checkpoint again, until **Start fresh** forgets it.
- **They're files in the project**, `checkpoints/<name>.checkpoint`, so they go into version control with the code, and a teammate starts where you were. A file's name is its checkpoint's: rename one to name it.
- **A later build loads it**: what was saved is carried over to the new code by name, as [hot reload](cli.md#hot-reload) carries a running match. Fields keep their values, new ones get their defaults, and loading says what was reset or dropped. One whose scene is gone is refused, saying why; **Load anyway** (`--force`, `checkpoint force-load`) loads the rest of it, without that scene's entities.
- **Saved by the machine that runs the match**: a client of another machine's match has no server world to save.
- What a game keeps in its own C, outside the match, isn't in a checkpoint.

For a script, an agent or a terminal, the same goes by name ([Without a window](cli.md#without-a-window)):

```sh
tide run --from boss-fight                # Starts from checkpoints/boss-fight.checkpoint
tide run --from boss-fight --force        # ...with what of it carries over
```

```text
# In a script or an interactive run: saved once the frame drew, and loaded
300 checkpoint save boss-fight
400 checkpoint load boss-fight
```

A run started `--from` a checkpoint that won't load ends at once, saying why, so a script never plays on from the wrong place. On the web, `tide run --web` keeps the page's checkpoints in the same folder.
