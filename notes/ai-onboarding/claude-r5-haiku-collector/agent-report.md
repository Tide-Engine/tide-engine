# Coin Collector: report

## What was built

`game.tide` is a single-file Tide game, 640×360 design size, with no external assets.

- **Board:** 5 columns × 3 rows, drawn as a checkered grid with row 0 at the top. Cells are 64 world units, centered on the origin.
- **Player:** starts at cell (0,1), one per joined player, with an `Owner`. Arrow keys move one cell per press, using the input's `.down` edge. Board edges block movement.
- **Coins:** at (2,1) and (4,2). Entering a coin's cell sets `collected` once and adds 1 to the `Score` singleton. Re-entering a collected coin does nothing.
- **HUD:** "Coins: N / 2" at the top left, and a control hint at the bottom left. At 2 coins, "Complete! Press R to play again" appears centered below the board.
- **R:** restores the initial state: player back to (0,1), score to 0, both coins uncollected.

Design choices I made (the task didn't specify them):
- Coordinates are `(x, y)` with `x` the column and `y` the row. So coin (2,1) is column 2, row 1, and coin (4,2) is column 4, row 2.
- Rendering uses the default world camera, set each frame to `Math.Max(180, 320 * Screen.height / Screen.width)` so the 640×360 layout scales with the window.
- Restart goes through a `Round` singleton. `Steer` sets `round.restart` from the input, and `ResetCoins` reads it in the same tick. The reset also clears the score.
- Everything is drawn in one view, `DrawBoard`, so I didn't depend on the drawing order between views.
- `game.tide` declares no local state. Nothing presentation-only needed it.

`TIDE.md` and `AGENTS.md` were not edited. AGENTS.md asks for design decisions to be recorded there once agreed. These decisions are my proposals, not agreed ones.

## Commands run

| Command | Result |
|---|---|
| `tide version` | `0.4.0-dev (nightly)`, installed at `C:/Users/blenm/tide-ai-trials/sdk` |
| `tide build` | Built `build/haiku-collector-r2.exe`. First compile, no diagnostics. |
| `tide build --web` | Built `build/haiku-collector-r2.html`. Build only, no browser test. |
| `TIDE_HIDDEN=1 tide run --script smoke.txt --frames 200 --size 640 360` | Exit 0. Captures: after-two-moves, complete, restarted, held-once, final. |
| `TIDE_HIDDEN=1 tide run --script edges.txt --frames 200 --size 640 360` | Exit 0. Capture: edges, final. |
| `TIDE_HIDDEN=1 tide run --script reentry.txt --frames 200 --size 640 360` | Exit 0. Captures: top-edge, on-coin-1, reentered, final. |

Scripts and captures are in `../haiku-collector-r2-checks/`, outside the game folder.

I inspected each capture by eye against the expected state:

| Check | Expected | Observed |
|---|---|---|
| Up at row 0, then up again | Stops at (0,0) | Top-edge capture: player at top-left, second press blocked |
| Right, down, right onto coin (2,1) | Score 1, coin gone | on-coin-1 and reentered: 1 / 2, coin gone |
| Left off the coin, then back onto it | Score stays 1 | reentered: 1 / 2 |
| Down to (2,2), then down twice more | Stops at row 2 | Edges script, final state consistent |
| Right to coin (4,2) | Score 2, completion message | complete.png and edges-final: 2 / 2, message shown |
| R after completion | Score 0, player (0,1), coins restored | restarted.png: 0 / 2, player at (0,1), both coins visible |
| Hold right for 30 frames (`key rightArrow down` … `up`) | Moves exactly one cell | held-once.png: player moved one cell, not repeating |

## Documentation notes

- **Confusing: the restart path.** The docs show how `Controls` reads one-shot edges, and how global commands send actions from UI. They don't show a clean way for an owner-scoped input to reset data across other entities. My answer, a `Round` singleton written by `Steer`, works, but it is a workaround. A global command handler or a documented pattern for this would be clearer.
- **Unclear: drawing order between views.** The views page has a "drawing order" section for 3D only. For 2D I couldn't confirm whether `Draw.Clear` in one view runs before others, so I used one view for the whole frame.
- **Confusing: `(x, y)` in the task.** It isn't a doc issue, but the task's cell coordinates needed a decision. I used column and row, as noted above.
- **Accurate and useful:** the `settings { ... }` block, `query (Player player, with Owner) players` in a system, `Draw.SetCamera` with `Math.Max` for responsive scaling, the `key rightArrow` script names, and `--size 640 360` all worked as documented on the first try.

## Not verified

- **Real browser:** the web build compiled, but I did not open it in Chrome or Edge.
- **Window sizes:** all captures used `--size 640 360`. I did not check a 960×540 window or a resize, although the camera math should scale.
- **Multiplayer:** `maxPlayers = 1`. The restart logic and the `Collect` query are written for multiple owners, but I did not test two players or `--players 2`.
- **Pixel-level detail:** images were inspected by eye, not compared pixel by pixel.
- **Keyboard edge timing:** holds were tested only with a single 30-frame `rightArrow down`. Simultaneous key combinations were not tested.
- **Build folder:** `tide build` also writes `build/` inside the game folder. The compiler compiles any `.tide` file there, so nothing was left in `build/` that matters, but the folder is generated output.
