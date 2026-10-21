# Pong (haiku-pong-r2): report

## What I built

`game.tide`, one file, no packages, no external assets. Simple shapes only.

- **Court**: 640 × 360 world units. The camera is set in the first view to `Math.Max(180, 320 * Screen.height / Screen.width)`, so the whole court stays visible at any window size.
- **Seats**: `maxPlayers = 2`. The first player to join gets the left paddle, the second the right one. Joins are handled in `event(PlayerJoined)`, and each paddle is a `Paddle` entity with an `Owner`.
- **Controls**: an `input Controls` reads W/S or the up/down arrows. `Steer` moves each paddle from its owner's input and clamps it to the court.
- **Ball**: waits at the center until both seats are filled. It bounces off the top and bottom walls and off the paddles. Contact angle depends on where the ball hits the paddle.
- **Scoring**: when the ball passes a paddle, the other side scores. The next serve goes toward the side that conceded, from the center. The first serve (both seats filled) goes left.
- **Win**: first to 5. The win message ("LEFT PLAYER WINS" / "RIGHT PLAYER WINS") shows for 3 seconds (180 ticks at 60 Hz), then both scores reset and the ball serves toward the side that conceded the last point.
- **Presentation**: both scores, a dashed center line, and a "Waiting for a second player" caption while the ball waits.

I also added a "Design decisions" and "Validation" section to `AGENTS.md`, as that file asks. It records the decisions above and the commands below.

## Choices I made where the request was silent

- A player leaving destroys their paddle, resets the score and returns the ball to waiting. The request did not cover this.
- The first serve goes left.
- After a win reset, the serve goes toward the side that conceded the last point. The request only said "the scores reset".
- The third and later joiners are refused by `maxPlayers = 2`.

## Commands and checks I ran

All runs used `TIDE_HIDDEN=1`. Scratch scripts and probe copies live in the temp directory, outside the game folder.

| Check | Result |
|---|---|
| `tide version` | `0.4.0-dev (nightly)` |
| `tide build` (native) | Built `build/haiku-pong-r2.exe`. First attempt failed: `float2 ==` is not allowed. Fixed by testing `Math.Length(ball.velocity) == 0`. |
| `tide build --web` | Built `build/haiku-pong-r2.html`. Not opened in a browser. |
| `tide run --frames 120 --capture smoke.png` | Single player. Picture shows both scores at 0, the paddle on the left, the center ball, and "Waiting for a second player". |
| `tide run --players 2 --script move.txt --size 640 360` | Two players. Each held their key: left paddle W/S, right paddle up/down arrow. Pictures show both paddles clamped at the top and bottom, and both players' views agree. |
| `tide run --players 2 --script real-run.txt --size 640 360` | Real game, two players, 900 frames. Captured mid-rally, scores 2–1, ball in play. |
| Probe (scratch copy, logs added) | Left paddle tracked the ball at 60% of its y, right paddle at 60% too, to force rallies. |

### The probe found a bug

The first probe showed no paddle contacts. The cause was the approach test. The sign was wrong: a ball moving toward the left paddle has `velocity.x * side > 0`, not `< 0`. The ball passed through the paddles, so the left side lost every rally. The first capture did not exercise a contact, so it did not catch this. The sign is fixed in `game.tide`, and the rebuilt game and the probe both behave correctly.

### What the probe showed

These are from logs of the probe over 3000 and 9000 frames, with paddle tracking added in the probe only:

- 52 unique paddle contacts, on both sides.
- 12 points, with serves going toward the side that conceded.
- A win at tick 3331 (right side reached 5). The reset came at tick 3512, about 181 ticks, which is 3 seconds at 60 Hz. The serve after reset went left, the side that conceded the last point.
- A second win and reset occurred the same way at ticks 6842 and 7023.

Each log line appeared three times in the output. I deduped with `sort -u` and did not establish the cause.

## Tide docs: what confused me, what was wrong, what was missing

- **`float2 == float2` is an error.** The docs say vector arithmetic is component-wise but do not say whether equality is allowed. The compiler's error was clear. I used `Math.Length(v) == 0` instead.
- **Ternary inside string interpolation.** The compiler requires parentheses: `{(a ? b : c)}`, not `{a ? b : c}`. The error message explains this, but the docs I read do not mention it.
- **Mixed int and float.** I wrote `side * BALL_SPEED` with `side` as `float`, avoiding int/float mixing in arithmetic. I did not test an int version, so I cannot say what the compiler does with it.
- **Scripted runs cannot make a player leave.** The script language has no leave action, so the `PlayerLeft` handler (score reset, paddle destroyed, ball waiting) is compiled but not exercised.
- **Multi-player log duplication** is not documented in what I read. Each `Debug.Log` line appeared three times with `--players 2`.
- The docs were otherwise usable: `settings`, `Draw.SetCamera`, `Anchor`, `query` rows and `PlayerID` seat assignment all matched the compiler.

## What remains unverified

- **Web and browser behavior.** I ran `tide build --web` only. I did not open a browser and did not check WebGL/WebGPU, the page's input handling, or audio. The game has no audio.
- **Real network.** `--players 2` runs a simulated network on one machine. It does not test real transports, lag, or packet loss.
- **Player leave and rejoin.** Not exercised by a script (see above).
- **Edge cases not run:** a third joiner (refused by `maxPlayers`, not run), both players leaving mid-win, and a ball hitting a paddle corner (the contact test uses a box, not exact corners).
- **Window sizes other than 640 × 360 and the default 960 × 540.** The camera-fit formula is only checked at those two sizes.
- **Feel.** The serve speed (240 units/s), the paddle speed (240 units/s), and the contact angle (vertical speed = offset × 4) are untuned guesses. I did not play the game interactively.
