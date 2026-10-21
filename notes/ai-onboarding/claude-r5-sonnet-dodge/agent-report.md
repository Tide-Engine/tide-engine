# Dodge game report

## Built
`game.tide` (single file, no assets): 640x360 play area (camera `Draw.SetCamera(0,0, Max(180, 320*h/w))`). Green square player at the bottom, moved with A/D or arrows, clamped to +-320. A red hazard spawns every 0.5 s at a random x along the top (`Random.NextFloat`), falling at 100 + 12*t units/s and destroyed below the bottom. AABB collision ends the run: all systems freeze, the screen shows "You survived N.N seconds". Space restarts (clears hazards, recentres player, resets timer). HUD shows "Time: N.N" while playing.

## Checks
- `tide version` -> 0.4.0-dev (nightly). `tide build` and `tide build --web` both succeed, no warnings in the final build.
- Hidden native runs (`TIDE_HIDDEN=1 tide run --size 640 360 --script ... --capture`): play capture (player moves, hazards fall, timer counts), game-over capture (4.4 s, frozen, gray player), restart capture (timer 0.3, hazards cleared, player centred). Scripts in `t/`.
- Found and fixed a bug via capture: two players were spawned (Spawned handler + PlayerJoined); now only PlayerJoined spawns.

## Docs notes
- The overview's example spawns a Ball in Spawned and the player in PlayerJoined; easy to double-spawn the player if copying both. Not wrong, just worth a warning.
- Not obvious from the docs where `Screen` is usable: `Screen.width` works inside a view body as a static, but not as a parameter (`Screen screen` is an error).
- `this.Destroy()` in a filter-only system worked as expected.

## Unverified
- Web build was compiled only; not run in a browser. No multiplayer test (`--players 2`). Fall speed ramp and spawn rate are only checked visually/by construction, no numeric logs. Restart relies on the Owner player's input (so it works only for a joined player). Timer shows one decimal; the final time is shown as float with one decimal.
