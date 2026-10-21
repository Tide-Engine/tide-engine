# Pong report

## Built
`game.tide` (single file): 640x360 two-player Pong, simple shapes only. A `Pong` singleton holds seats
(`PlayerID left/right`, assigned in `PlayerJoined`, freed in `PlayerLeft`), paddle Y, ball, scores and win timer.
Each player gets a `Pad` entity with `Owner`; the `Controls` input (W/S or Up/Down) drives the paddle of the
seat matching `Owner.player`, clamped to the court. The ball bounces off top/bottom and paddles (with slight speedup and
spin from hit offset). On a miss the other side scores and the ball is served from the center toward the
conceder. At 5 points a win message shows for 3 s (ball hidden), then scores reset. With fewer than two players the
ball waits in the center and a "Waiting for a second player" label shows. Camera keeps the 640x360 court visible.

## Commands/checks run
- `tide version` (0.4.0-dev nightly); `tide build`, `tide build --web`: both succeeded (final re-run after the last small edit also built).
- `tide run --frames 60 --capture` (hidden, single player): ball in center, 0-0, waiting text. Inspected.
- `tide run --players 2 --script ...` up to 3800 frames with captures every 60 frames: both paddles moved and clamped at the
  screen edges, scores incremented (2-1, 4-2, 5-3), "Left player wins!" shown for ~3 captures (~3 s), then scores returned to 0.
  Captures inspected visually (a few of them; the rest compared only by file size).
- The ball-hidden-during-win tweak was made after those runs; only compiled afterwards, not re-captured.

## Docs notes
- Nothing wrong found. `PlayerID` null: docs say it "has a null value" but only `default` for entities is shown; `== default` worked for PlayerID.
- Which key names exist for arrows (`upArrow`, `downArrow`) is only inferred from the `leftArrow`/`upArrow` examples; it compiled.
- Capture size is 960x540 by default; `--size 640 360` not tried.

## Unverified
- Ball/paddle collision edge cases (corner hits) and bounce from paddle hit were only seen indirectly through scoring/captures.
- Player leaving mid-game/rejoin, 3+ players (extras get a paddle entity that is destroyed by Cleanup, never controlling anything).
- Keyboard W/S for the first player was scripted only for W; arrow/W-S equivalence in real input untested.
- Web build was compiled but not run in a browser (not permitted). Real network play untested.
