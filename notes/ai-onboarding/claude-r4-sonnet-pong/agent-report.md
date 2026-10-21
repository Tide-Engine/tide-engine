# Pong report

## Built
`game.tide` (single file): 640x360 two-player Pong. Camera is `Draw.SetCamera(0, 180)`, so the court is 640x360 world units at a 640x360 window.
- First joiner gets seat 0 (left), second gets seat 1 (right), via `PlayerJoined` with a query over existing `Paddle`s. A leaving player's paddle is destroyed, so the seat frees up. `maxPlayers = 2`.
- W/S or Up/Down arrows move the paddle (`input Controls`); the paddle is clamped on screen.
- The ball is kept in a `Game` match singleton. It bounces off the top and bottom edges and off paddles (speed +5% per hit, deflection by hit offset). When it passes a paddle the other side scores, and the ball serves from the center toward the conceding player, with a deterministic `Math.Hash` serve angle.
- Scores are shown. First to 5 shows a "wins!" message for 3 s, then scores reset and play continues.
- With fewer than 2 paddles the ball stays at the center and stationary.

## Commands and checks
- `tide version` -> 0.4.0-dev (nightly).
- `tide build` and `tide build --web`: both succeeded (no browser launched, as instructed).
- `TIDE_HIDDEN=1 tide run --size 640 360 --script ... --capture`, 1 player: the paddle appears on the left and the ball waits at the center. This confirms the ball waits with one player.
- `--players 2` scripted runs: W on player 1 and S on player 2 moved the paddles, and both clamped at the screen edges (captures). Scores incremented (1-0 seen).
- 2500-frame idle run: capture showed "5 - 0, Left player wins!"; a later capture showed 0-1, then 0-0 with the ball in play. Reset after the win works. I did not measure the 3 s precisely, only that it was shown at ~frame 1500 and gone by 1800.

## Unverified
- Paddle bounce was not explicitly observed (paddles were idle/centered). The code path is untested by a capture.
- Serve direction was not checked against the conceder beyond code reading.
- Real keyboard input, a real network, and the web build in a browser were not tested.
- Mid-match leave/rejoin was not run.

## Docs notes
- `tide-docs` matched the compiler well; the game compiled first try.
- The docs list no setting for window size; I used `--size` for tests and a height-based camera. It would help to say how to pin a 640x360 window.
- Script lines must be in ascending frame order (my first script failed on that). This is only mentioned implicitly.
- `--players 2` capture names get `-1`/`-2` suffixes; I found this by running it.
- Environment: `python -` with a heredoc hung in this shell, unrelated to Tide.
