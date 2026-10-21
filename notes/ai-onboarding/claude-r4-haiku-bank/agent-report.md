# Shared Pool: report

## What I built

A two-player Tide game in `game.tide` (single file, no imports, no external assets). Window layout is drawn from `Screen.width`/`Screen.height`, so it fits 640×360 and other sizes.

- **Match state**
  - `component Wallet { int credits = 3; int refused; }`, spawned per player in `event(PlayerJoined)` with `Owner { player = joined.player }`.
  - `singleton Pool { int credits; }`, starts at 0.
  - `settings { maxPlayers = 2; }`, `scene Main { }` (match starts directly).
- **Command**
  - `command Deposit { int amount; }`, with handler `command(Deposit deposit, PlayerID sender) DepositCredits(Owner owner, mut Wallet wallet, mut Pool pool)`.
  - The handler returns silently if `sender != owner.player`.
  - It counts a refusal (`wallet.refused += 1`) and returns if the amount is outside 1–3 or exceeds `wallet.credits`.
  - Otherwise it moves credits from the wallet into the pool.
- **Local state**
  - `local singleton Selector { int amount = 1; }`, changed by an `IntSlider` (1–3) in the player's own panel. It only changes that client's preview.
- **Views**
  - `Backdrop(Pool)`: clears, sets screen-pixel drawing, draws the pool count and coins.
  - `YourWallet(Wallet, with ownership, mut Selector, Pool)`: balance, slider, live preview ("Deposit N: you keep …, pool becomes …" or a red "Not enough credits" message), the Deposit button (`this.Send(Deposit { amount = selector.amount })`), and the refusal count.
  - `TheirWallet(Wallet, without ownership)`: the opponent's balance and refusal count. No controls.
  - `Coins(...)` helper: filled and outlined circles.
- **Design decisions** are recorded in `AGENTS.md` as the project asks.

## Commands and checks run

All with the installed compiler `tide 0.4.0-dev (nightly)`.

| Command | Result |
|---|---|
| `tide version` | `0.4.0-dev (nightly)` |
| `tide build` | Built `build/haiku-bank.exe`, no diagnostics on the first attempt |
| `tide build --web` | Built `build/haiku-bank.html`, exit 0 |
| `TIDE_HIDDEN=1 tide run --frames 120 --size 640 360 --capture checks/smoke.png` | Exit 0, 640×360 capture inspected: pool 0, 3 credits, preview correct, no opponent panel (one player) |
| `TIDE_HIDDEN=1 tide run --players 2 --script checks/two-player.txt --size 640 360` | Exit 0. Captures at frames 55, 90 and 150 for each player, inspected |

Script `checks/two-player.txt` and the observed results:

1. Frame 30: player 1 clicks Deposit at amount 1. Frame 55: pool 1, player 1 at 2, player 2 at 3 (both machines agree).
2. Frame 60: player 1 clicks the selector track to 3 and clicks Deposit with 2 credits. Frame 90: refused. Player 1 shows "Refused deposits: 1", pool stays 1, and the red "Not enough credits" preview shows. Player 2's view shows the opponent's refusal count as 1.
3. Frame 100: player 2 sets amount 2 and clicks Deposit. Frame 150: pool 3, player 1 at 2, player 2 at 1. Totals are 3 + 2 + 1 = 6, which is conserved. Each player's selector shows its own value (player 1: 3, player 2: 2).

Note: all numeric checks are read from the rendered text in the captures. I did not add `Debug.Log` or `Debug.Assert` lines to the game.

## Documentation notes

- **Window size is not a setting.** The `settings` table (`language/basics.md`) has no window size. 640×360 is only achieved with `tide run --size 640 360`. The built program opens at the default size, which the CLI doc gives as 960×540; I did not run it to confirm.
- **Helper function syntax.** The docs show `float2 Seat(int)` but no `void` helper. `void Coins(...)` compiled and drew correctly, so it works.
- **GUI signatures.** The GUI table lists `IntSlider(label, mut int value, min, max)` for `GUILayout`. For `GUI`, the Rect comes first, and I used `GUI.IntSlider(Rect, label, selector.amount, 1, 3)`. It compiled and the slider responds to clicks, so the form is correct.
- **Revision mismatch.** The docs mirror is at source revision `16dcab2` and says it is not matched to an installed release. The compiler is `0.4.0-dev (nightly)`. Everything I used compiled, so there was no visible mismatch in practice.
- **Provisional docs.** The `--players` script format and rooms are marked "Provisional" in `tools/cli.md`. They worked as documented for this run.
- **Local mirror.** The docs were read from `./tide-docs/`. The website's `.md` pages were not used.

## Unverified

- **Web target.** I built the web page but did not open it in a browser, so no WebGL/WebGPU or browser input check.
- **Rejection paths not reachable from the UI.** The sender-ownership check, and amounts outside 1–3, can only be reached by a modified client. The UI never sends them, so I did not exercise them. They are checked only by reading the code.
- **Real networking.** `--players` uses the simulated network. Real transports, latency, packet loss and host migration are untested.
- **Other run modes.** I did not run a release build, hot reload (`tide run` without `--frames`), or a window-mode run on this machine.
- **Player leaving.** There is no `PlayerLeft` handling. If a player leaves, their wallet stays in the match. This is a design gap, not an error.
- **Selector drag.** The slider was set with a single click on the track (`button left` at a point). Dragging a knob with the mouse was not scripted.
