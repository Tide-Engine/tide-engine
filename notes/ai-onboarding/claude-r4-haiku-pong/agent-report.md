# Two-player Pong in Tide: report

## What I built

`game.tide` (one file, no imports, no `tide.packages`). Simple shapes only.

- **Court:** 640 × 360 world units, centered. Y is up. Top and bottom walls bounce the ball.
- **Seats:** the first `PlayerJoined` takes the left paddle, the second takes the right. Later players get no paddle and watch. On `PlayerLeft`, the seat is freed and its paddle destroyed.
- **Paddles:** `Paddle { side; y }` entities with `Owner`. W/S and Up/Down (`keys.w`, `keys.s`, `keys.upArrow`, `keys.downArrow`) move them through the `Controls` input. They are clamped so they stay on screen. Your own paddle is yellow; the other is white.
- **Ball:** a `Ball { position; velocity }` entity. It bounces off the walls and off paddles. A paddle hit reflects the ball and sets its angle from the strike point. A point occurs when the ball's center passes the court edge.
- **Serve:** after a point the ball restarts at the center, moving toward the side that conceded. The initial serve goes toward the left.
- **Waiting:** with fewer than two seated players the ball is held at the center with zero velocity, and the HUD shows "Waiting for another player".
- **Scoring:** both scores are shown at the top. First to 5 wins. A "<side> player wins!" message shows with the ball held at the center for 3 seconds, then both scores reset and the next serve goes toward the player who conceded the last point.
- **Single state:** all rules live in the `Game` singleton, updated by `Referee`. `Steer` moves paddles. Views only read the match. `Draw.Text` is used for the HUD.

### Choices I made where the spec was silent

- When a player leaves mid-game, the scores are kept. The ball waits until a second player takes the free seat, then serves again.
- A paddle hit is detected by overlap, and the reflected angle is `normalize(-side, offset * 0.6) * 300`, where `offset` is the strike position clamped to −1..1.
- Serves are straight horizontal (no vertical component).
- The window is the `tide run` default of 960 × 540, so the 640 × 360 court is centered with a dark margin. `settings` has no window-size option, and `--size 640 360` is a run flag only. Testing used `--size 640 360`.
- Spectators (a third or later joiner) have no paddle and see the game.

## Commands run

All from `C:\Users\blenm\tide-ai-trials\trials\haiku-pong`:

| Command | Result |
|---|---|
| `tide version` | `0.4.0-dev (nightly)`, installed at `C:/Users/blenm/tide-ai-trials/sdk` |
| `tide build` | Failed first with 2 errors (`float2 == float2` unsupported; `conceded` needed `mut var`) and a warning (unused `game` param). Fixed; second build succeeded. Rebuilt after later edits: passes. |
| `tide build --web` | Passes (`build/haiku-pong.html`). **Not browser-tested.** |
| `TIDE_HIDDEN=1 tide run --frames 120 --size 640 360 --capture build/checks/smoke-1p.png` | Single player. Captured and inspected: yellow left paddle, ball held at the center, "Waiting for another player" message, scores 0-0. The message was moved after the first look because it overlapped the ball; the final capture confirms the fix. |
| `TIDE_HIDDEN=1 tide run --players 2 --script play.txt --size 640 360` (rally probe) | Captures at frames 80 and 400, inspected. Logs show an off-center paddle hit (`vel=(270, -130)`), a return off the right paddle (`vel=(-300, 0)`), then a point to the left player (score 1-0), with the next serve toward the right player (who conceded). |
| `--players 2` scoring probe (left paddle held at bottom) | Captured at frames 200, 360 and 700, inspected. Logs: five points to the right player, `win by 1`, then `reset scores` and a new serve. Frame 360 shows "Right player wins!" with 0-5 and the ball held at center. Frame 700 shows 0-2 after the reset. |
| Win-duration probe (`--players 2`, tick logs) | Win set at tick 336, reset at tick 517: 181 ticks, about 3.0 s at `tickRate = 60`. |

Probe copies with extra `Debug.Log` lines lived at `C:\Users\blenm\tide-ai-trials\probes\pong-probe`, outside the project, because the compiler includes subfolders (see below). The project itself has no probe code. Captures are in `build/checks/` and in the probe folder.

The multi-player logs repeat some lines (for example `point` appears three times for one point). With rollback prediction the client re-runs ticks, so `Debug.Log` inside a system can print more than once. The captures and the final scores are consistent, so I judged state from those rather than the log counts.

## Unverified

- **Browser / web behavior:** only that the web build compiles. No browser run, no WebGL/WebGPU check.
- **Real networking:** the two-player runs use the simulated `--players` network on this machine. No real transport.
- **Leaving mid-rally and rejoining:** `Leave` and the wait-for-second-player path were not exercised by any run. The seat-freeing code is untested.
- **Top wall bounce and top paddle clamp:** the bottom clamp was seen in a capture (paddle flush with the court bottom). The top wall and top clamp are symmetric in code but not observed.
- **Spectators (third player):** not tested; `maxPlayers` default (16) was left alone.
- **Paddle-miss timing, long rallies and speed:** only sampled, not measured against the spec.
- **Input feel:** keyboard input was scripted (`key w down` etc.), not real keyboard input.
- **Human visual check:** I inspected the captures myself; no one else has looked at the game.

## Documentation notes

These are what cost me time or where the docs were unclear, missing or inconsistent with what I saw:

1. **Subfolders are part of the build, and that isn't flagged where it matters.** `ai-context` and `cli.md` say a game includes `.tide` files in its subfolders. I first put probe copies under `build/checks/`, and the build failed with "'Hud' is already declared" in a file I had forgotten about. A one-line warning near the file-layout section, saying build/output folders with `.tide` copies will break the build, would help.
2. **`float2 == float2` is not allowed**, and the error says so, but the docs don't mention it. `Math.Length(v) == 0` worked. It would help to list that vector equality is not an operator, or say that `==` is for scalars.
3. **Logging under rollback.** The `Debug.Log` docs (basics.md) don't say that a system's log can repeat when a client re-runs a tick. I saw duplicates in multi-player runs and had to infer why. A note would help.
4. **Window size.** `settings` has no window size, and `--size` is run-only. A game that is designed at a fixed size has no in-game way to request it. The docs cover `--size` but not this design choice. (This may be intentional.)
5. **`mut var` requirement.** The docs say a reassigned local needs `mut var`, and the compiler said so. I missed it on the first write, though it is documented.
6. **Entities in singletons.** `Entity` defaults to null and `== default` works, as docs said. Both worked as documented. No issues.
7. **Consistency.** `TIDE.md` asks that design decisions be added there as they are agreed. I didn't edit it because you didn't ask me to record decisions there; I can add the seat, serve and win rules if you want them kept.

## Files

- `game.tide` — the game.
- `build/` — native and web build output and the `build/checks/` captures.
- `report.md` — this file.
- `C:\Users\blenm\tide-ai-trials\probes\pong-probe\` — probe copy and logs, outside the project.
