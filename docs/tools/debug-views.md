# Debug views

Debug views are panels the engine draws over a running game, for the people making it. There's one so far: the profiler, which shows where a frame's time goes, and a tick's.

## Turning them on

They're off until the game, or whoever starts it, asks for them:

- The game asks with a setting (see [Settings](../language/basics.md#settings)):

  ```csharp
  settings
  {
      debugViews = true;
  }
  ```

- Whoever starts it asks with `--debug-views`: `tide run --debug-views`, or a built game's own command line, like `mygame --debug-views`. On a web page, it's `debug-views` in the page's address: `https://example.com/game.html?debug-views`. This works whatever the setting says, so a build made to ship can be looked into without building it again.

With either, **F7** shows the profiler, and F7 again hides it. Nothing is measured while it's hidden, so having debug views on costs a game nothing until the key is pressed. The game sees the key too.

Under `tide run`, changing the setting takes effect at the next reload. A setting is part of the build, so `tide build` keeps it: take `debugViews` out before players get the game, unless they may see the profiler too.

## The profiler

The profiler has two columns: a frame on the left, a tick on the right. Its numbers are averages over the last half second, so they hold still long enough to read, and each row's bar is its share of its column's first row.

```
Frame  60 fps, longest 21.4 ms   16.67 ms     Tick  180 a second          0.93 ms
  Input                           0.02          Systems                   0.78
  Match                           3.20            Movement.Integrate      0.23
    Ticks  3.0 a frame, 2.0 again 2.80            Movement.Collide        0.19
    Netcode                       0.40            Combat.ApplyDamage      0.07
  Views                           1.37            3 more                  0.01
    DrawArena                     0.63          Changes, events           0.16
    Hud                           0.23
    GUI, tasks                    0.13
  Render                          0.60
  Other                           0.00
  Swap, idle                     11.48
```

**A frame**, in milliseconds a frame:

| Row | What it is |
|---|---|
| Frame | From one frame's start to the next, with the frame rate and the longest frame of the half second |
| Input | Reading the keyboard, the mouse, gamepads and touches |
| Match | The match's share of the frame: its ticks, and its netcode |
| Ticks | The game's ticks, and how many a frame ran. On a client, `again` is how many of them were predicted ticks run again, after another player's input turned out not to be what was guessed (see [Networking](../multiplayer/networking.md#prediction-and-rollback)) |
| Netcode | What the match took beside its ticks: packets, snapshots, and hashing and packing worlds |
| Views | Every view, then the six that took longest, by name, and the others together |
| GUI, tasks | What the frame's views took beside the views themselves: the GUI, local tasks, and applying local changes |
| Render | Drawing what the views recorded, as far as the CPU does it. What the GPU takes shows in the last row |
| Other | The rest of what the frame did |
| Swap, idle | The rest of the frame: waiting for the display, the GPU or the browser |

**A tick**, in milliseconds a tick:

| Row | What it is |
|---|---|
| Tick | One tick, and how many ran a second: more than the tick rate when ticks run again |
| Systems | The tick's systems, from the first one's start to the last one's end, then the ten that took longest, by name with their namespace, and the others together |
| Changes, events | What follows the systems in a tick: spawns, destroys and other structural changes, the events' handlers, and tasks |

- A system's time is from the start of its first piece of work to the end of its last. Systems that ran at the same time on several threads add up to more than `Systems`, which is what the clock says. `tide schedule` shows which systems wait for which, and why (see [The schedule](../game/schedule.md)).
- Outside a match, no tick runs, and the tick's column says so.
- A view that didn't run, like one that reads the match while there's none, has no row.
- When `Ticks` is most of a frame and many of them run again, clients are paying for prediction: see [Input delay](../multiplayer/matches.md#input-delay).

The [stats](cli.md#stats) are separate, and always shown once asked for: a few numbers in a corner, with the three systems that took longest in the last tick.

A C host shows the profiler itself with `tide/profiler.h` (see [C hosts](../c/hosts.md#the-standard-host)).
