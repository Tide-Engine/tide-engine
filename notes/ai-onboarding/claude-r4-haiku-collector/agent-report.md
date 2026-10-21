# Haiku Collector: report

## What I built

A single-file Tide game, `game.tide`, for a 5×3 board (row 0 at the top).

- **Start state:** player at (0,1). Coins at (2,1) and (4,2).
- **Match state:** one `singleton Board` holds the player cell, a `collected` count, and one "taken" flag per coin. The singleton's field defaults are the initial game.
- **Input:** a view (`ReadKeys`) reads the keyboard and submits a typed command per press (`Move { Direction direction; }`, or `Restart {}` for R). Only the command handler `StepPlayer` writes the match.
- **Rules in `StepPlayer`:** a move outside the board is ignored. Entering a coin's cell sets its taken flag and increments `collected` only if the flag was clear, so each coin counts once even if the player re-enters its cell.
- **R:** `ResetBoard` restores the singleton's initial values.
- **Presentation:** `DrawBoard` draws the grid, the coins that remain, and the player as simple shapes. The HUD shows `Coins: n / 2`, the controls hint, and at two coins the message "All coins collected! Press R to play again." Layout is centered on `Screen.width`.
- **Settings:** `maxPlayers = 1`. No external assets.

Design choice: I kept the coins as singleton flags rather than entities. The Tide docs present entity-per-coin as the usual shape, but a fixed two-coin board makes restart and single-collection simple to reason about. A larger board would be better served by coin entities.

## Commands run

All run from the project folder, with `TIDE_HIDDEN=1` set in the shell for native runs.

| Command | Result |
|---|---|
| `tide version` | `0.4.0-dev (nightly)`, installed in `C:/Users/blenm/tide-ai-trials/sdk` |
| `tide build` | Built `build/haiku-collector.exe`, no diagnostics after fixes (see below) |
| `tide build --web` | Built `build/haiku-collector.html`. Not run in a browser. |
| `tide run --script checks/play.txt --size 640 360 --frames 200` | Exit 0. Captures `checks/01`–`04`. |
| `tide run --script checks/reenter.txt --size 640 360 --frames 200` | Exit 0. Captures `checks/05`–`08`. |
| `tide run --script checks/reenter2.txt --size 640 360 --frames 100` | Exit 0. Capture `checks/09`. |

Build diagnostics I fixed along the way:
1. Command handlers require a `PlayerID sender` parameter, even when the handler doesn't use it. Added to both handlers.
2. The compiler needs `scene Main { }` as the entry point. Added.

I inspected each PNG by eye:
- **01 (edge):** pressing Left from (0,1) left the player at column 0. The edge block works.
- **02 (first coin):** player at (2,1), HUD `Coins: 1 / 2`, the (2,1) coin gone, the (4,2) coin still there.
- **03 (both coins):** `Coins: 2 / 2`, player at (4,2), completion message shown and not overlapping the hint line. This is after moving the message to y=300.
- **04 (reset):** after R, player back at (0,1), `Coins: 0 / 2`, both coins back.
- **05 (collected once):** `Coins: 1 / 2` at (2,1).
- **06 (leave and re-enter):** my script moved one cell further than intended, so this capture shows the player at (1,1), not (2,1). The count stayed at 1.
- **07 (bottom edge):** player at (1,2) after extra Down presses. The count and the edge block are consistent with the expected behavior.
- **08 (top edge):** player at (1,0) after extra Up presses. The edge block is consistent with the expected behavior.
- **09 (re-enter the coin cell):** leave (2,1) to (1,1) and come back to (2,1). `Coins: 1 / 2`, so the coin is not counted twice.

Not done: I did not add `Debug.Log` or `Debug.Assert` checks. The count and position checks rest on the captures.

## Doc notes

- **Window size is a CLI option, not a game setting.** The task asks for 640×360. The `settings` table has no window size. `tide run --size 640 360` sets it. I only confirmed that for `tide run`. The docs say a built game "takes the same options", but I did not run the built `.exe` with `--size`, so I can't say the built game opens at 640×360.
- **Command handler signatures:** the doc examples include `PlayerID sender`, but the snippets are easy to copy without it. The compiler message ("expected ',' and the command sender") was clear enough to fix.
- **Script capture paths:** `capture` paths resolve beside the script, as the doc says. My first try doubled the folder name, which was my mistake, not a doc problem.
- **Version mismatch:** the docs say they describe a website revision that may be newer than the installed compiler (`0.4.0-dev nightly`). I found nothing in this game that depended on a newer feature, but I did not compare the two systematically.

## Unverified

- **Web build:** compiled only. Not opened in a browser, no WebGL/WebGPU check, no `--mute-audio` run (the game has no audio anyway).
- **Built executable:** only `tide run` was exercised, not the `build/haiku-collector.exe` launched directly.
- **Real keyboard:** all input came from scripted key taps (`key <name>`), which are one-frame presses. I did not test holding an arrow key, key-repeat behavior, or interactive play.
- **Feel and timing:** command handling happens at the tick end, so the visual response lags by about a tick. I didn't measure or judge this.
- **Multiplayer:** the game is `maxPlayers = 1`. Multi-player was not tested and is not meaningful for this board.
- **Numerical state:** verified through screen readouts only, not logs or assertions.
