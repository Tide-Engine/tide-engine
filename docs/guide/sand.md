# Falling sand

Sand, water and walls that everyone in a room paints into at once, built for the web from the code below. Click it to give it the keyboard. On a touchscreen, tap it first: until then a swipe on it scrolls this page, and after, it paints.

<iframe class="demo-frame square-ish" src="/sand/?embedded" title="Tide sand" allow="fullscreen"></iframe>

| | Mouse and keyboard |
|---|---|
| Paint | Left mouse button |
| Erase | Right mouse button |
| Pick sand, water, wall or the eraser | `1` to `4`, or the buttons |

[Open it on a page of its own](/sand/).

## Paint together

Press **Invite**, and the room's code shows beside it, with **Copy** to put it on the clipboard. Whoever opens this page on another machine pastes the code next to **Join** (Ctrl+V, or Cmd+V) and presses it, and you're both painting into the same sand. Up to 16 players can join.

The bottom right corner shows the frame rate, and your ping once you've joined someone's room. Click or tap it, or press `F8`, and it lists the rest: what goes over the network each second, up and down, the tick, and how long the sand's step takes. Those are the engine's [stats](../tools/cli.md#stats).

Only the brushes go over the network: where each one is, what it paints, and whether it's down. Every machine runs the same simulation from them, and gets the same sand, bit for bit. A player who joins gets the world as it is then, and only the parts of it that changed since the match started.

## The code

The game is `bench/sand/sand.tide` in the repo. Its cells are a [grid](../language/grids.md) of a byte each, and `Step` moves them with a parallel loop, in 2 by 2 blocks, on every core the machine has. It's also one of the engine's benchmarks, which bots play to measure it.

<<< @/../bench/sand/sand.tide

`bench/sand/play.tide` draws it, a rect for each run of cells of one material along a row, and has the GUI. Views only change this machine's local state (`Canvas`, `Lobby`), never the sand.

<<< @/../bench/sand/play.tide
