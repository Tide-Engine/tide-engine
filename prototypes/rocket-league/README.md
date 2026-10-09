# Afterburn

A Rocket League inspired car-soccer prototype: a five-minute blue/orange duel
in a floodlit stadium, with a bot opponent in solo play and Tide rooms for
two human players. It uses original procedural vehicles, arena, materials and
sounds. See [DESIGN.md](DESIGN.md) for the target and reference sources.

## Run

From the repository root:

```powershell
cmake --preset release -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset release --target rocket_league
.\build\release\bin\rocket_league.exe
```

Choose **Play** for a bot duel, or **Host a room** and share the room code with
a friend. A human joining takes the bot's seat; the bot returns when they leave.
Menus do not pause the match.

For the browser:

```powershell
cmake --preset web-release -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset web-release --target rocket_league_page
```

The self-contained page is `build/web-release/bin/rocket_league.html`; serve
it through HTTP. The page target also stages
`docs/static/prototypes/rocket-league/index.html`. After the site's next
deployment, its shareable address is
[/prototypes/rocket-league/](https://tide-engine.dev/prototypes/rocket-league/).
The admin prototype catalog links to it. This game is outside Samples and
package installation, and normal builds leave it disabled.

## Controls

| Action | Keyboard/mouse | Gamepad |
|---|---|---|
| Accelerate / brake / reverse | W / S or up / down | Right / left trigger |
| Steer | A / D or left / right | Left stick horizontal |
| Jump / second jump | Space | South face button |
| Boost | Left Shift or left mouse | East face button |
| Powerslide / free air roll | Left Ctrl | West face button |
| Aerial pitch / yaw | WASD | Left stick |
| Air roll left / right | Q / E | Left / right shoulder |
| Ball camera | C | North face button |

Hold jump for more height. Release and press again while airborne for a
second jump; add a direction to dodge. Boost pushes along the car's nose in
the air. Use air roll to land wheels down. Drive up a curved perimeter ramp
to reach the walls and ceiling, and jump away to set up an aerial.

Small pads give 12 boost and return after four seconds. The six large pads
fill the tank and return after ten seconds. A full tank lasts about three
seconds of continuous boost. Supersonic head-on hits can demolish the other
car, which respawns after three seconds.

Goals require the whole ball to cross the line through the goal mouth. The
clock waits during kickoff and celebrations. At 0:00 a ball still in flight
stays live until it hits the pitch or a goal is scored. A tie goes to sudden
death. Touch devices show driving and action controls.

## Verify

```powershell
cmake --preset debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset debug --target rocket_league rocket_league_tests rocket_league_render
ctest --preset debug -R '^rocket_league_'
cmake --preset web-debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset web-debug --target rocket_league rocket_league_tests rocket_league_render
ctest --preset web-debug -R '^rocket_league_'
```

Windows web tests require Node 24.16 or newer. Set `TIDE_NODE` at configure
time if the supported runtime is outside `PATH`. All browser test launches
are muted. The test hosts are separated from playable sources by the nested
`test/tide.packages` manifest.

Implementation details, validation and remaining limits are in
[notes/rocket-league.md](../../notes/rocket-league.md). Regenerate all six
original sounds with `python prototypes/rocket-league/sfx/make_sounds.py`.
