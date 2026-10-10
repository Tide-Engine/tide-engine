# Aerial car soccer

The owner asked on 2026-10-10 for another Rocket League prototype, better than
Afterburn (`notes/rocket-league.md`) and as close to the real game as could be
made, in how it plays and how it looks. `prototypes/aerial/` is it. Its name,
bots, look and what its sources don't give are the agent's choices, none
confirmed by the owner. `DESIGN.md` there says what it aims at, which numbers
were checked against which source and which are from memory; `README.md` has
how to build it, play it and test it.

No engine, compiler or package change was needed.

The same day another session reworked Afterburn after the owner rejected its
controls, camera and physics (`prototypes/rocket-league/RESEARCH.md`). What
that research found is in Aerial too: the game's two key layouts, keyboard
aerial safety, the camera's angle, RocketSim's hitbox, and two of RocketSim's
own numbers as tests.

## The match

`game.tide` is the match, at 120 Hz, for up to six players with host
migration. `MatchState` is a singleton: six `CarState`s, the ball, 34 pad
timers, each seat's last controls, the score, the clock and who touched the
ball last. Nothing else is in the world but a `Participant` for each player,
which ties their input to a seat.

- **Seats.** Even seats are blue and odd ones orange. A player who joins gets
  the first free car of the side with fewer players. `size` is cars a side;
  `wanted` (the `Teams` command, from the button beside Menu) is what it
  becomes at the next kickoff, never fewer than the players' seats need. A
  seat past `size` whose player joins grows the teams at once, and the new
  cars come on as wrecked ones come back.
- **Bots** drive every car in play that has no player's input this tick
  (`manned`), which includes a player who's in a menu: the input's `away`
  default is true, and `Sample` clears it only while `Lobby.playing`.
- **Phases.** `Countdown` (3 s, nobody driven), `Playing`, `Goal` (3.6 s:
  the ball is gone and cars near it are thrown clear, and everyone still
  drives), `Finished` (14 s, then `NewMatch` by itself). The clock holds
  while `waiting`, until a kickoff's first touch. At 0:00 play goes on until
  `ball.floored`; level scores start overtime, where the clock counts up and
  the next goal wins.
- **Kickoffs.** Five places a side, mirrored through the centre spot; which
  one a kickoff uses is `Math.Hash` of how many there have been and
  `Random.seed`, and teammates take every second place from it.
- **The scoresheet.** `last` and `before` are who touched the ball last on
  each side and who before them. A goal is the scoring side's last toucher's
  (nobody's when none of them touched it), and the one before is an assist
  within five seconds. Each tick `PredictBall` runs the ball three seconds
  ahead; when the side whose goal it ends in changes, within half a second of
  a touch, that's a shot (it now ends in the other side's goal) or a save (it
  no longer ends in the toucher's own, an epic one with under half a second
  to go).
- **Events** the match sends for this machine to show and sound carry small
  whole numbers only (a seat, a loudness from 0 to 3), so a tick run again
  sends the same event and it's heard once.

## The car, the ball and the arena

Metres, y up, x across, z from blue's goal to orange's. A hundredth of the
game's units.

- **`arena.tide`: one function, `Arena(point)`**, gives the distance to the
  nearest surface and its normal. Walls, floor and ceiling are half-spaces
  joined by quarter pipes (`Fillet`, which takes walls that aren't square to
  each other, for the 45 degree corners); a goal is cut from the end wall
  between the posts and under the bar, with the wall's edge round the mouth
  and the ramp's flat end beside each post as part of the shape. The ball,
  the wheels' rays, the box's corners, the camera and the stadium's mesh all
  ask this one function or are built from its constants, so they can't
  disagree about where a wall is.
- **`car.tide`: a rigid body.** Position, quaternion, velocity, spin about
  the world's axes. `DriveCar` is a tick of what the driver and the wheels
  do to velocity and spin; `MoveCar` integrates; `CarOnArena` is the box
  against the arena; `LimitCar` holds 23 m/s and 5.5 rad/s, and says whether
  it's supersonic.
  - A wheel's ray is stepped along `Arena`'s distance three times
    (`TraceWheel`). The spring pushes along the surface's normal at the
    contact; the tyre's grip pushes level with the body's centre, so a car
    doesn't lean in a bend or dip under braking.
  - `grounded` is three wheels or more. Air control is for no wheels at all;
    with one or two, or the box on a surface, the car leans towards its
    wheels.
  - **The box under the wheels is a bump stop**: where two wheels or more are
    down and a corner of the box meets a surface that faces the car's own up,
    that contact has no friction and no bounce (`belly` in `CarOnArena`). See
    `DESIGN.md` for why.
  - `struck` is the tick the ball last took this car's punch: a ball that's
    touched every tick, carried on a roof, is punched once.
  - The box is RocketSim's, with its collision margin: 1.205 by 0.867 by
    0.387 m, and the moments of inertia are that box's. With the game's bare
    1.180 by 0.842 by 0.362 the reference shot touched a tick late.
  - `DriveInput.level` is keyboard aerial safety: the pitch turns nothing,
    and still aims a dodge.
  - The scoresheet is in `CarState`, and `Fresh` carries it over when a car
    is put back on its wheels.
- **`ball.tide`.** `MoveBall` is flight, then the arena: a bounce that keeps
  0.6, grip by how the surface and the ball's spin slip, and no bounce at all
  under 0.3 m/s, so it settles. It turns at 6 rad/s at the most, so a ball
  rolling faster than 5.5 m/s keeps scrubbing speed. `CarMeetsBall` is the
  contact, and `PunchBall` the second push, from the velocities the tick
  began with.
  - **The contact settles its push and its grip together, over eight
    rounds.** A push at the nose turns the car, which moves the point of
    contact; settled once each, the reference shot's ball left 0.4 m/s too
    fast downward.
- **Two cars at the ball in one tick** are settled over three rounds, in an
  order that turns round each round and each tick (`Simulate`). Settled in
  seat order, the side with the higher seats won nearly every contest: bots
  scored 20 to 164 over twelve matches. With the order turning it's 62 to 49.
- **An array's element can't be handed to a function to change**, so
  `Simulate` takes each car out, changes it and puts it back. Reads of
  `state.cars[seat].field` copy nothing.

## Bots

`bots.tide`. A bot keeps nothing between ticks: what it's doing is read off
its own car (`jumped`, `jumpTime`, `flipped`). `PredictBall` gives it the
ball's next three seconds, once a tick for all of them.

- The car of a side that can be at the ball soonest (`Eagerness`) goes for
  it; the next holds back behind the ball, and a third stays in front of its
  goal. With the ball heading into their goal, two go.
- Going for it: the first moment it can be where the ball will be, low enough
  for its wheels or a jump; a place behind the ball on the line to a spot in
  the other goal; a dodge into a low ball, a held jump and a dodge for a
  higher one. Chasing a ball back towards its own goal it goes for the ball's
  side, as a hit from behind helped it in.
- **Bots differ by kickoff**: each hesitates by up to an eighth of a second
  at a kickoff, and aims at a part of the goal that changes every two
  seconds, both from `Math.Hash` of the kickoff's number, the seat and the
  match's seed. Without it every kickoff from the same place played out the
  same way to the same goal, 43 goals of 113 within seven seconds of one.

## What's drawn and heard

All local: `Look`, `Sun`, `Models`, `Board`, `Ears` and `Hand` are local
singletons, and nothing here is in the match's hash (the render test checks).

- `models.tide` builds the car (hulls lofted through ribs, `Hull`), the wheel
  and the simple shapes; `stadium.tide` sweeps the walls, ramps, roof, kerb,
  seats and canopy round `Rim`, the wall's line for a quarter of the pitch,
  from `arena.tide`'s constants.
- `render.tide`: a 2048 shadow map that follows the camera's car and the
  ball, the scene into a four-sample target, a quarter-size glow, and one
  frag to the window. The pitch is one quad whose frag cuts it to the
  arena's shape and draws the mowing and the lines. The crowd is a frag on
  the seats' risers. Particles are cards a vert turns to the eye
  (`SparkPlace`), added as light or laid over as smoke.
- **A hull's triangles are wound clockwise from outside** (`Hull`). Wound the
  other way the car drew its far side's inside: dark paint, with normals
  that looked right from behind.
- The camera (`AimCamera`) sits 2.7 m behind a point a metre over the car, on
  the line from the ball with the ball camera on and along the nose with it
  off, at 77.55 degrees up and down on every screen. It goes out through the
  glass walls behind a car that's driving up one; the pitch, the kerb and the
  roof hold it in. From the grass it looks up only as far as keeps the car in
  the picture. A held key looks back, and a gamepad's right stick swivels it.
- **Keys are in one of the game's two layouts** (`KeyLayout` in `hand.tide`,
  Classic unless the button beside Menu switches it): the input's `Sample`
  and `Prepare`'s camera keys both read it. `Hand.held` is the throttle key
  held since the ground, which the HUD's view keeps from the car's wheels,
  and `Sample` turns into the input's `level`.
- `effects.tide` and `sound.tide` are local handlers of the match's events.
  A goal, a wreck, a feat and the result wait for `verified`; a touch, a
  bounce, a pad and a jump are heard as predicted.
- `hud.tide` draws the score, the boost dial, callouts, names, the result's
  table and the touch controls, and fills `Hand` from fingers for the
  input's `Sample`. Under 620 pixels wide the Menu and teams buttons move
  under the score.
- `sfx/make_sounds.py` writes the seventeen WAVs with the standard library
  and a fixed seed.

## Build and catalog

`TIDE_PRIVATE_PROTOTYPES=ON` adds `aerial`, `aerial_tests` and
`aerial_render`, and on the web `aerial_page`, which stages the page at
`docs/static/prototypes/aerial/index.html` and is one of `private_games`.
The catalog card is `aerial`, for one to six players, with a native capture
as its thumbnail; the site's patterns and tests have the id
(`notes/docs-site.md`). The existing manual docs workflow stages it. Nothing
was deployed and no workflow was dispatched.

## Tests

- **`aerial_tests`** (`test/test.c`, 28 tests, native and WASI): the car at
  rest on its springs; throttle, boost and their top speeds; coasting,
  braking and reverse; the turning circle at 12 and 23 m/s; a powerslide;
  jump heights (0.90, 2.34 and 4.87 m for a tap, a hold and two); one dodge
  and its window, and the dodge a car keeps when it never jumped; air
  control and flight; wall, ceiling and the drop from it; righting a car on
  its roof; the ball's bounce, roll, rest and top speed; a hit and a dodged
  hit (18.2 and 26.6 m/s off a car at 12.5); a ball carried on a roof; goals,
  posts, the bar, the line and the kickoff after; an assist; a shot and a
  save; the clock, overtime and the next match; pads; a wreck, a bump and no
  wrecking one's own side; seats, sides, team sizes and who may restart; a
  whole bot match with every car and the ball in bounds; six cars on random
  controls for 150 seconds; input bounds and NaN; snapshot, pack and replay;
  two clients on a network with 30 ms of latency, 20 of jitter and 5% loss;
  keyboard aerial safety; and RocketSim's shot and its powerslides (1.6 cm/s
  from its ball's velocity; within 0.08 m, 0.15 m/s, 0.02 rad and 0.04 rad/s
  of its car).
- **The scripted match's hash is `0x66d4ba38f380cd9b`**, the same on native
  debug and WASI. Any change to the match changes it: update it from one
  platform and confirm the other.
- **`aerial_render`** (`test/render.c`; OpenGL, WebGL and WebGPU): draws at
  1280 by 720 and 390 by 844, samples both key layouts, a gamepad, the touch
  stick and each touch button through the views into the input, keeps each side's car in the
  lower middle of the picture with the ball ahead, behind at 23 m/s and with
  the car camera, and checks the match's hash is what it was.
- **`aerial_play_webgl` and `aerial_play_webgpu`** (`test/browser_test.mjs`):
  two players in headless Chrome through a relay on this machine, driving,
  boosting and jumping by keys and by touch, with captures. They're
  `RUN_SERIAL`, as each draws two software-rendered players.

Validation on 2026-10-10, on this Windows machine: `ctest --preset debug -R
'^aerial_'` passed 2 of 2 and `ctest --preset web-debug -R '^aerial_'` 5 of 5,
the web ones with Node 24.19. With the page staged, the site's build and
Svelte check passed (no errors), and so did its 31 analytics, admin, access,
HTTP and SEO tests. Native runs with no window (`--script`, under
`TIDE_HIDDEN`) were looked at as captures: the title, a kickoff, driving and
boosting, a wall, goals and the last seconds of a bot match. A run's captured
audio is stereo at 48 kHz and neither silent nor clipped (peak 20,725 of
32,767).

## Not built, and not checked

- Replays, spectating, camera settings, customising, more than one stadium.
- Bots that fly, dribble or pass on purpose.
- The curves of the game's own arena, and its goals' rounded insides.
- Nobody has played it: every check here is a scripted run, a bot match or a
  capture looked at by the agent. How it feels in the hand, on a gamepad or
  a phone, is untested, and so are the sounds, which were never listened to.
- A release build and its frame time; two machines through the public relay;
  Linux, macOS, MinGW and Android, which need a requested CI run.
