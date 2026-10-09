# Dead Signal — cooperative prototype

A fast first-person cooperative expedition for **two to four players**, with PSX
visual cues and an in-run relic build. Descend through six authored sectors,
sever the purge relays, destroy the transmission's custodian, and extract
together. [DESIGN.md](DESIGN.md) explains the experience, pacing and tradeoffs.

The campaign targets **30–60 minutes for a first human session**. Human pacing
and balance have not been measured yet; this is a playable prototype with a
complete start-to-ending route, rather than a verified duration claim.

## Play and share

After a docs deployment, share
[Dead Signal](https://tide-engine.dev/prototypes/dead-signal/) with your crew.
The game opens without admin access; use its room menu to host or join.
The catalog remains behind admin sign-in. The source repository stays private,
the build option defaults off, and the game remains outside public Samples,
docs examples and the Tide package. Local builds use the same room flow.

## Build and play

From the repository root:

```powershell
cmake --preset debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset debug --target dead_signal dead_signal_tests dead_signal_render_tests
```

Run `build/debug/bin/dead_signal.exe`, choose **Host a room**, and share the room
code with a second player running the same build. Both players choose **Ready
up** before the expedition starts. The menu's solo Play entry lets you inspect
the lobby; the expedition still requires a second player.

For two machines on a LAN, the first can run:

```powershell
build/debug/bin/dead_signal.exe --host 7777
```

The second connects with `--connect <host-address>:7777`. On the same machine,
use `--connect 127.0.0.1:7777` in a second terminal. These are user play commands;
agent validation uses hidden, counted or scripted runs.

The browser build uses the same game and networking:

```powershell
cmake --preset web-debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset web-debug --target dead_signal dead_signal_tests dead_signal_render_tests
```

Open `build/web-debug/bin/dead_signal.html` locally. Host and join through the
room menu. It is a self-contained page. With the opt-in option enabled,
`cmake --build --preset web-debug --target private_games` stages it at
`docs/static/prototypes/dead-signal/index.html`. After a docs deployment,
players open `/prototypes/dead-signal/` before hosting or joining a room.
The screenshot and playable page share that public prototype directory.

## Controls and rules

| Action | Mouse and keyboard | Gamepad |
|---|---|---|
| Move | WASD / arrows | Left stick |
| Look | Mouse; click to capture it | Right stick |
| Fire | Left mouse | Right trigger |
| Use / revive / charge relay | Hold E | Hold south button |
| Choose weapon | 1–4, Q to cycle | Right shoulder to cycle |
| Sector map | Hold M | — |
| Menu / release cursor | Escape | East button |

Start with the scattergun and nail driver. Foundry unlocks the launcher;
Reservoir unlocks the arc cutter. Empty ammunition and being downed leave an
emergency sidearm available. Friendly fire is off. Kills, pickups and shared
cache discoveries replenish resources.

Keep moving and split firing angles. A second player's hit on a recently hit
enemy earns crossfire damage and breaks frontal armor. Hold Use near a downed
ally to revive them. Two living players charge a cleared relay together, and
at least two must hold Use together at the exit. After a sector, each player
chooses one relic.

A team wipe retries the current sector with its relic build intact. Losing the
second player suspends the expedition until another joins. After victory,
**Another expedition** returns the crew to readiness for a fresh run.

## Validate

```powershell
ctest --preset debug -R dead_signal --output-on-failure
ctest --preset web-debug -R dead_signal --output-on-failure
```

The headless simulation tests cover readiness, real player ownership, suspension,
revives, checkpoint retries, command validation, objective cooperation, wall
occlusion, projectile flight, aiming, map connectivity and collision, deterministic
replay, and two clients under latency, jitter and loss. A test-only navigator
uses real movement, weapons, ammunition and objectives to reach the ending.
Its full-route run is explicitly invulnerable, so it establishes progression
without claiming normal difficulty or human completion time.

The renderer test draws thirteen actual game states, including every weapon,
the full character cast, relic, downed, victory and sector-map screens, twice
each. It checks pixel readback and the seven shader paths on native OpenGL,
browser WebGL and WebGPU. The two-player capture
script also exercises readiness, camera input, shooting and the map in the
standard host.

Validated on Windows clang and WASI: all 16 simulation checks pass, including a
normal-health opening fight with 30 kills and no downs or retries. Both targets
match the active-combat simulation hash `1fe52c6bf4f307fe`. The full-route structural
run reaches victory through all six sectors with 1,647 kills. Normal difficulty
and the intended human session duration still require a two-person playtest.

For reproducible two-player visual captures:

```powershell
$env:TIDE_HIDDEN = '1'
build/debug/bin/dead_signal.exe --players 2 --script prototypes/dead-signal/test/smoke.txt --size 1280 720
```

That script produces lobby, intake, combat and map pictures beside itself, one
for each player. Generated captures are local validation artifacts. Original
sound assets can be regenerated with `python prototypes/dead-signal/sfx/make_sounds.py`.

For the full arsenal and cast, create a local output folder and run
`build/debug/bin/dead_signal_render_tests.exe --capture-directory <folder>`.
This counted headless run saves thirteen fixture images; it does not change
the game or add fixtures to a normal play session.

## Implementation boundaries

`world.tide` owns the authored plans and shared deterministic circle/tile
collision, line of sight and wall rays. `game.tide` owns the match, input, guns,
AI, objectives and checkpoint/relic flow. `render.tide` owns the local camera,
mesh batches, shaders, UI and audio. `models.tide`, `weapons.tide` and
`environment.tide` supply the original faceted cast, first-person arsenal and
industrial architecture. Separate weapon depth, local limb animation and
contact shadows are presentation only. None requires a new engine runtime API.

The simulation is planar with vertical aiming; there are no stairs or jumping.
There is no permanent progression, procedural level generator, built-in voice
chat or saved expedition. Play uses a mouse/keyboard or gamepad. These boundaries
keep the prototype focused on shooting, cooperation and a complete expedition.
