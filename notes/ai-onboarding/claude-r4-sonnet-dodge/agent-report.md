# Dodge game report

## Built
`game.tide` (single file, Tide 0.4.0-dev nightly). 640x360 dodger: cyan square at the bottom moves with Left/Right or A/D, clamped to the screen. Red squares spawn at a random x above the top every 0.5 s and fall at 120 + 12*t px/s (t = seconds into the run). They are destroyed once below the bottom. Touching one sets `Run.over`: spawning, falling and the clock stop, and the screen shows "You survived N seconds" (whole seconds, floored). Space starts a new run (destroys hazards, resets the clock and the player). The HUD shows "Time: N" while playing. Shapes only, no assets.

## Commands / checks
- `tide version`, `tide build` (ok), `tide build --web` (ok; not opened in a browser, per instructions).
- `TIDE_HIDDEN=1 tide run --size 640 360 --script t.txt --frames 940` with captures: play (hazards falling, timer), game-over screen with the survival time, restart (clean state, Time: 0), and holding A at the left edge (player clamped at x=0, flush with the edge).
- Not tested: right edge clamp (same code, symmetric), arrow-key input (only D/A scripted), a real window, the web build in a browser, and the speed ramp numerically (only by code reading). Hazards leaving the bottom is untested beyond code; I did not log entity counts.

## Docs notes
- 640x360 is not a game setting: there is no window-size setting, so `--size 640 360` only applies to my test runs. The default window is 960x540. The game uses fixed world coordinates (x in -320..320), so a different window shows a different amount of world. I did not add a scaling camera.
- The docs did not make it obvious that arrow keys are `rightArrow`/`leftArrow` (found in input.md); the compiler error only said "no field 'right'".
- Systems need `Time time` as a parameter; `Time.dt` gives a helpful diagnostic, but the overview example uses it in a way that is easy to mis-copy.
- A `restart` bool in `input` read via `keys.space.down` worked as hoped; a single-player match's player entity gates the restart system, which is a design choice of mine.
