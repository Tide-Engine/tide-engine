# Dodge: report

## What I built

A small dodging game in Tide, in two files:

- `match.tide`: settings, constants, components (`Body`, `Player`, `Hazard`), the `Run` singleton, the `Pad` input, the player spawn on `PlayerJoined`, and the systems `Steer`, `Clock`, `Fall`, `Collide` and `Start`.
- `view.tide`: the letterboxed background, the player and hazard shapes, and the HUD.

Behavior:

- The player is a 32 px yellow square near the bottom. Left and right move it (arrow keys or A/D) at 300 px/s. It is clamped to the playfield.
- A 28 px red hazard spawns at a random x along the top every 0.5 s. Hazards fall at `120 + 8 × t` px/s, where `t` is seconds into the run. Hazards that leave the bottom are destroyed.
- A touch (AABB overlap) ends the run. Time, spawns, hazard movement and player movement all stop. The screen shows "HIT" and "You survived N.N seconds".
- Space starts a run from the ready screen, and starts a new run after a hit. A restart clears the old hazards and resets the player and the clock.
- "Survived: N.N s" is shown in the top left at all times.
- Only rectangles and text are used. No assets.

Design choices I made (not specified in the request):

- The game opens on a "Press Space to start" screen rather than starting a run immediately.
- The playfield is 640×360 logical units (y down, origin top left). It is fitted into any window with letterboxing, using a camera size of `max(180, 320·h/w)`.
- Fall speed is linear and uncapped. Tuning it (a cap, or a slower ramp) is a one-line change in `Clock`.
- Input: the player is a `Player` entity with `Owner`, so its systems read that player's `Pad` input. Systems with no owner would read the server's input.

## Commands run

All with `TIDE_HIDDEN=1` set in the shell for the runs.

- `tide version`: `0.4.0-dev (nightly)`, installed in `C:/Users/blenm/tide-ai-trials/sdk`.
- `tide build`: built `build/haiku-dodge.exe`. Passed on the first try, and again after each change.
- `tide build --web`: built `build/haiku-dodge.html`. Passed after each change. I did not open it in a browser, as instructed.
- `tide run --size 1100 650 --frames 60 --capture build/checks/ready.png`: inspected the picture. Title, letterboxing and player are correct.
- Scripted runs with `--script` (scripts in `build/checks/run1.txt` to `run5.txt`). I inspected each capture:
  - `run1`: Space starts the run. Hazards fall and the timer counts. Holding A moves the player to the left wall. A hit ends the run at 7.8 s.
  - `run2`/`run3`: after a hit, the field stays frozen. `stand-b.png` and `stand-c.png` are pixel-identical, so everything stops. Space restarts the run.
  - `run4`: holding D clamps the player at the right wall. A run survived 20.1 s. Hazard spacing matches a 0.5 s spawn interval at the expected speed.
  - `run5`: the left and right arrow keys move the player.
- `tide run --size 640 360 --frames 30 --capture build/checks/size640.png`: the native playfield fills the window exactly. `run3` was repeated at 640×360 (`stand-c.png`), with the same behavior.

## Bugs found and fixed during checks

1. **Restart ended the run at once.** `Start` ran before `Collide` in the same tick. It moved the player to the center and marked the old hazards for destruction, but they still existed that tick, so the player overlapped one and the run ended with "0.0 seconds". Fix: `Start` now has `[After(Collide)]`, so the old hazards are gone before the next collision check. Confirmed by `restarted.png` (0.8 s, then 3.3 s, field cleared).
2. **The game-over text was hard to read over frozen hazards.** I added a dark rounded panel behind the message. `[After(DrawHazards)]` on `Hud` did not change the order, so the panel is what fixed it. Confirmed by `stand-c.png`.

## Doc problems and confusions

1. **Window size limit is documented but not enforced.** `tools/cli.md` says `--size W H` accepts only 1100..8192 by 650..8192. `--size 640 360` was accepted by `tide run --capture` and produced a 640×360 picture, so the documented minimum does not hold here (at least for captures). I did not try a windowed run at that size.
2. **`--check` is documented but was rejected.** `tools/cli.md` says `--check` "runs the same authoring validation... returns 1 for project errors." `tide run --check` replied "run doesn't take '--check'; see `tide help run`". I did not check `tide build --check` or `tide help build`, so I don't know where the flag lives in this build.
3. **Draw order did not match what I expected.** `game/views.md` says views run in declaration order and later ones draw on top. In my test, a hazard drawn by an earlier view appeared over text from a later view, even with `[After(DrawHazards)]` on the HUD. I didn't isolate the cause. The workaround (a panel drawn before the text) worked, but I could not confirm the docs' claim in this build.
4. **Nothing in the docs was missing for the core APIs.** `Random.NextFloat(min, max)`, `[Clamp]` on input fields, query rows with `.entity.Destroy()`, `this.Destroy()`, `Owner`, `Draw.RoundRect` and `Anchor` all worked as documented on the first try.

## Not verified

- **Web build in a browser.** Only that it compiles. I did not run it in Chrome or Edge.
- **Real keyboard input and feel.** All inputs were scripted with `key ... down/up`. I did not play it interactively.
- **Window sizes other than 640×360 and 1100×650.** Letterboxing is checked visually at both sizes. The default 960×540 window and non-16:9 shapes beyond these were not captured.
- **Speed and difficulty.** The "falls faster over time" behavior was judged from capture spacing, not measured against the formula. No difficulty tuning was done.
- **Timing precision.** Survival time is `run.time` in ticks at 60 Hz. The captured values match frame counts to within about 0.1 s, but I did not measure them against wall time.
- **Multiplayer and other platforms.** `maxPlayers = 1`. Nothing was run over a network or on Android.
- **Determinism beyond scripted runs.** Each script was run once per version. I did not compare repeated runs for identical random spawns.
