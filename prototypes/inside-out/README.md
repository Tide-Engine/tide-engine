# Inside Out

A cooperative puzzle game for **two friends**, designed for native desktop
play. The browser version is a convenient way to preview the same game in
two tabs without installing a player.
You are two small test robots packed in a small box, itself inside another
room, on a model that sits on a table. Work your way out.
Push rooms with your partner inside, fit them through tight passages, and
move boxes into their own interiors. Repeated views of a room share one
set of batteries, boxes and people.

The 3D rooms keep a fixed, gently tilted view of a painted model: pale
floors, matte painted boxes, soft shadows, marks that glow, and a lens that
keeps your room sharp while the larger rooms around it fall out of focus,
with quiet music and soft wooden, brass and robot sounds.
There are no threats or jumpscares.

Choose **Host a room**, then share **Copy invite** or the six-character room
code. Opening a browser invite joins that room directly; **Join a friend**
also accepts its code. Both players must connect before either can move.
Use a voice call or chat alongside the game to discuss plans. The current
review build runs locally as described below; the public prototype at
<https://tide-engine.dev/prototypes/inside-out/> is the earlier deployed build.

## Playing

The scene fills the window, with the controls laid over it. Each screen
follows its own robot in one continuous scene. Step inside a
room and the camera zooms into its actual doorway. Your whole current room
stays in view, with a wider fixed rim of containing-world context around it.
You can see part of a robot standing beside your box.
The camera keeps that framing when your partner moves or leaves the screen.
Your robot walks and shrinks through the doorway; leaving grows them back
into the surrounding world. Your partner can watch that passage from outside.
The outside is your room's box, wherever it stands: a moved box changes your
surroundings, and a box inside itself puts the same room around itself. Amber and Teal keep their identity colours wherever they
travel. A room shell contains a live miniature of its shared interior;
nested and recursive preview details collapse to markers after a few layers.
Floors, walls, batteries and robots are real 3D geometry. The orthographic view
keeps the grid and movement directions familiar; there is no camera rotation.

| Action | Keyboard |
| --- | --- |
| Walk and push a battery or room | WASD or arrow keys |
| Enter a room | Walk into its doorway when the room cannot be pushed |
| Leave a room | Walk outward through an edge doorway |
| Undo the last shared move | Z or Backspace |
| Reset this puzzle | R |
| Open / close the menu | Escape |

On touchscreens, swipe in a direction to take one step. Rooms move first; when a wall
or your partner stops a row, walking into a stopped box's open doorway
enters it. Directions match the screen: W is up, D right, S down and A left.
After undo or reset, release a held direction before pressing it again.

Beside a box, the game shows what your next step will do before you take
it: a yellow bar against a box you would push, a green arrow into a doorway
you would enter. There is no directional button overlay.

The menu's chapter selector can revisit or skip any puzzle. A sage tick beside a
title marks one you have completed. The game teaches through its puzzles;
there are no hint panels, guide pages or lesson cards. Settings include
master, music, effects and interface volumes, mute, reduced motion and
step previews. Settings and
completion ticks are saved on this machine. Escape opens the in-game menu;
your partner's match keeps running. Reset and changing an active puzzle ask
before clearing the shared moves.

## The rules

One rule moves everything: robots, batteries and boxes.

- Whatever you walk into is pushed, and whatever is beyond it is pushed too.
  A row of any length moves as one. Nothing can be pulled.
- Only a wall or the other robot stops a row. A robot is never pushed.
- When a row is stopped, the thing against the stop stays put, and the thing
  behind it goes into it instead, if it is a box with a doorway facing that
  way. So you get into a box only when it cannot be pushed, and so does a
  battery or a box you push at it. The one stop you can put in place and
  take away again is your partner.
- A moving box carries everything inside it.
- A room has one box that is its outside. Walk out through an edge door and
  you come out beside that box; so does anything you push out.
- Every room has exactly one box. A box can stand inside its own room: leave
  that room and you come out beside its box, in the same room. Two boxes can
  stand inside each other: leave one and you come out beside it, in the
  other. A room is one place, so what is in it shows in every view of it.
- Pressure plates and shutters come in two colours, each with its own
  shape: magenta with a ring and lime with a diamond. A plate is pressed
  while anything stands on it: a robot, a battery, or a box on a plate of the
  room it stands in. While any plate of a colour is pressed, in any room or
  on the table, every shutter of that colour, in every room, is open floor.
  Closed, a shutter is a wall to anything walking, pushed or going into a box
  through it. Whatever it closed on stays: a robot can step out, and a push
  moves what is in it on, while whatever pushed stays where it is. Each step
  is worked out from where everything stood before it, so you can step off a
  plate and push a battery through its shutter in one move. Plates and
  shutters are not goals.
- Every room is a square grid, so a box always fills the cell it stands in.
- Seat every battery in a socket, park each box on its numbered outline, and
  put both robots on goals. A seated battery can be moved again while you
  arrange things.

Shared undo takes back the last successful action, whoever made it, and
retains 128 actions. Reset restores the complete starting arrangement.
After solving, the game holds the completed arrangement briefly, then
opens the next puzzle automatically. Each new arrangement is revealed before
movement begins. Undo or restart cancels an outstanding transition. The last
puzzle remains on its completed ending. Losing a participant stops gameplay
and advancement until both seats are present again.

## Development

For native desktop play, configure and build from the repository root:

```powershell
cmake --preset debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset debug --target inside_out
build/debug/bin/inside_out.exe
```

Two native players can host and join using the same room code. To preview
locally in two browser tabs without deploying the website, run these
commands from the repository root (Node 24.16 or newer on Windows):

```powershell
cmake --preset web-release -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset web-release --target inside_out_page
node prototypes/inside-out/serve.mjs
```

Open <http://127.0.0.1:8790/prototypes/inside-out/> in two tabs. Choose
**Host a room** in one, copy its invite, and open that URL in the other.
You can also choose **Join a friend** and enter the six-character code.
The server runs its own
local relay on port 8791 and serves the page with the isolation headers
needed by Tide. It reads the latest staged page on each reload. Ctrl+C
stops both services; optional HTTP and relay ports are the script's two
positional arguments. The server binds to this machine only.

The prototype uses Tide and the existing Rooms package. Its deterministic
match rules are in `game.tide`; the puzzles, drawn as text maps (their
legend is at the top of the file), with their chapter grouping,
are in `levels.tide`. `geometry.tide` builds the local 3D scene and `render.tide`
handles lighting, shadow mapping and screen-space ambient occlusion.
Presentation, controls and original audio are local state.
There are no new engine or runtime APIs.

Enable `TIDE_PRIVATE_PROTOTYPES` when configuring the repository. Build
`inside_out` and `inside_out_tests` with the native and web debug presets.
`inside_out_page` stages the web build into the public prototype directory.
`inside_out_play_webgl` and `inside_out_play_webgpu` test two real browser
players with muted audio. The campaign test replays checked-in witnesses
through production commands; it establishes solvability, not optimality
or measured human difficulty. `inside_out_solve`, a native tool for drawing
puzzles, moves through the same Move handler: `inside_out_solve 3` finds
puzzle 3's shortest solution, `--print` shows its maps, `--alone <seat>`
asks whether one robot can do it alone, `--play "<moves>"` plays moves, and
`inside_out_solve all` tabulates every puzzle (usage at the top of
`test/solve.c`). Contracts and validation: `notes/inside-out.md`.
