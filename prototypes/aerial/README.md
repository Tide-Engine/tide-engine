# Aerial

Rocket-powered car soccer, built to the sport's own numbers: a second Rocket
League prototype beside [Afterburn](../rocket-league/README.md), with a
rigid-body car on four sprung wheels where Afterburn has a point that follows
the ground. One, two or three cars a side; bots drive every car nobody has.
The car, the stadium, the ball and the sounds are made in code. See
[DESIGN.md](DESIGN.md) for what it aims at and where its numbers come from.

## Run

From the repository root:

```powershell
cmake --preset release -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset release --target aerial
.\build\release\bin\aerial.exe
```

**Play** starts a match against bots, two a side. **Host a room** gives a
code for up to five friends; each takes a bot's seat, on the side with fewer
players, and the bot is back when they leave. The **2v2** button beside the
menu asks for one, two or three a side from the next kickoff. Menus don't
pause the match: a player in one is driven by a bot meanwhile.

For the browser:

```powershell
cmake --preset web-release -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset web-release --target aerial_page
```

The page is `build/web-release/bin/aerial.html`, served over HTTP. The target
also stages `docs/static/prototypes/aerial/index.html`, which the site serves
as `/prototypes/aerial/` after its next deployment. It's outside Samples and
package installation, and ordinary builds leave it out.

## Controls

| Action | Keyboard and mouse | Gamepad | Touch |
|---|---|---|---|
| Drive, brake and reverse | W / S or the arrows | Right / left trigger | Stick up / down |
| Steer; yaw in the air | A / D or the arrows | Left stick | Stick |
| Pitch in the air | W / S | Left stick | Stick |
| Jump, second jump, dodge | Space or right mouse | South button | JUMP |
| Boost | Left Shift or left mouse | East button | BOOST |
| Powerslide; air roll with the stick | Left Ctrl or X | West button | SLIDE |
| Air roll left / right | Q / E | Shoulders | |
| Ball camera | C or middle mouse | North button | CAM |

- Hold jump for height. In the air, jump again with no direction for a second
  jump, or with one for a dodge that way: forward into the ball is the shot.
  The second jump is there for 1.25 seconds after the first; a car that left
  the ground without jumping, off a wall or the ceiling, keeps it until it
  lands.
- Boost pushes along the nose, so pointed up it flies. Small pads give 12 and
  are back in 4 seconds; the six big ones fill the tank and take 10.
- At 22 m/s a car is supersonic: nose first into a rival, it wrecks them for
  three seconds.
- Walls are driven on. Hit a ramp head on at full speed and it costs speed;
  come at it at an angle. A ceiling doesn't hold a car for long.
- On its roof, a car rolls back over at a jump.

A goal needs the whole ball over the line. The clock starts at a kickoff's
first touch; at 0:00 the ball stays in play until it touches the pitch, and
level scores go to overtime, where the next goal wins. A new match follows
the result by itself.

## Verify

```powershell
cmake --preset debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset debug --target aerial aerial_tests aerial_render
ctest --preset debug -R '^aerial_'
cmake --preset web-debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset web-debug --target aerial aerial_tests aerial_render
ctest --preset web-debug -R '^aerial_'
```

On Windows the web tests need Node 24.16 or newer (`-DTIDE_NODE=<node>` at
configure time, when the one on `PATH` is older). Every browser a test starts
is muted. `test/tide.packages` keeps the C test hosts out of the game.

What's built, what its code relies on and what's been checked are in
[notes/aerial.md](../../notes/aerial.md). `python
prototypes/aerial/sfx/make_sounds.py` writes the seventeen sounds again, the
same bytes every time.
