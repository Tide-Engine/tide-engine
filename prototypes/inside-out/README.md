# Inside Out

A cooperative puzzle game for **two friends in separate browsers**.
You are two test dummies packed in a small box, itself inside another room
in a crash-test lab. Work your way out.
Push rooms with your partner inside, fit them through tight passages, and
move boxes into their own interiors. Repeated views of a room share one
set of parcels, boxes and people.

The 3D rooms keep a fixed, gently tilted view of a bright test lab: pale
flooring, boldly painted boxes, yellow and black striped doorways and soft
shadows, with quiet ventilation.
There are no threats or jumpscares.

Play at <https://tide-engine.dev/prototypes/inside-out/>. The admin prototype
catalog links to the same public page; friends need no admin account.
Choose **Host a room**, then share the invite link or six-character room
code. Both players must connect before either can move. Use a voice call or
chat alongside the game to discuss plans.

## Playing

The scene fills the window, with the controls laid over it. Each screen
follows its own dummy in one continuous scene. Step inside a
room and the camera zooms into its actual doorway. Your whole current room
stays in view, with a wider fixed rim of containing-world context around it.
You can see part of a dummy standing beside your box.
The camera keeps that framing when your partner moves or leaves the screen.
Your dummy walks and shrinks through the doorway; leaving grows them back
into the surrounding world. Your partner can watch that passage from outside.
The outside follows the doorway you used: a moved shell changes your surroundings,
and a recursive doorway can put the same room
around itself. Amber and Teal keep their identity colours wherever they
travel. A room shell contains a live miniature of its shared interior;
nested and recursive preview details collapse to markers after a few layers.
Floors, walls, parcels and dummies are real 3D geometry. The orthographic view
keeps the grid and movement directions familiar; there is no camera rotation.

| Action | Keyboard |
| --- | --- |
| Walk and push a parcel or room | WASD or arrow keys |
| Enter a room | Walk into its doorway when the room cannot be pushed |
| Leave a room | Walk outward through an edge doorway |
| Undo the last shared move | Z or Backspace |
| Reset this puzzle | R |
| Show / hide hints | H |
| Continue after solving | Enter |

Touch uses the same directional arrows. Rooms move first; when a wall,
another box or your partner stops a room, walking into its open doorway
enters it. Directions match the screen: W is up, D right, S down and A left.
After undo or reset, release a held direction before pressing it again.

Beside a box, the game shows what your next step will do before you take
it: a yellow bar against a box you would push, a green arrow into a doorway
you would enter. The direction buttons show the same for every direction,
with a bar across the arrow where you cannot go.

The shelf can revisit or skip any of the twelve puzzles. A plus beside a
title marks one you have completed in this match. Hints start with a small
nudge; ask for more to reveal an arrangement and then a more specific plan.
They are optional. Ambience, sound and reduced motion are local settings.

## The rules

- Both dummies can enter any blocked room through an open doorway. People in
  the same room block each other, even if they arrived through different
  recursive doorways.
- A moving shell carries its entire interior without changing anything's
  position within that room. A shell can contain another shell.
- Walking into a room first tries to push it. If that push cannot happen,
  the same move tries its doorway instead. Parcels are pushed normally.
  Nothing can be pulled, and pushes cannot move a chain.
- A parcel or room pushed through another room's doorway enters its
  interior. Parcels pushed outward through an edge door emerge beside the
  containing room's physical shell.
- Two boxes side by side are joined only where their open doorways face
  each other at the same place along the shared edge; the floor between
  them is drawn where they do. Dummies, parcels and boxes all cross a
  joined doorway directly, and a dummy walks through it instead of pushing
  the neighbor. Where the doorways do not line up, a parcel or box pushed
  out is blocked by the neighbor, and a dummy stepping out pushes the
  neighbor if it can move and is blocked if it cannot.
- A repeated box is another movable entrance into an existing room. Boxes
  can enter themselves, and two boxes can contain one another. Moving a
  parcel during one visit changes it for every visit. Every room still has
  one designated primary box: parcels and boxes pushed out of that room
  emerge beside this primary box, even if you entered through a copy.
  A small number marks the primary box; a prime mark (`1′`) marks its copy.
- A dummy's return path remembers the entrance used. Leaving through an
  edge door returns beside that entrance; if a shell has moved, the return
  moves with it. Up to eight entrance layers are available.
- Room sizes, wall shapes and edge doorway positions vary. There are no
  switch locks. Make space and use walls, boxes or your partner as a
  backstop when you need to enter a box instead of pushing it.
- Fill every box-marked parcel target, park each shell on its matching numbered
  dock in the correct dimension, and put both dummies on distinct player goals.
  Parcels must finish on targets. A filled target can be emptied while
  arranging the solution.

Shared undo takes back the last successful action, whoever made it, and
retains 128 actions. Reset restores the complete starting arrangement.
Completion waits for you to continue. Losing a participant stops gameplay
until both seats are present again.

## Development

The prototype uses Tide and the existing Rooms package. Its deterministic
match rules are in `game.tide`; campaign layouts and optional hints are in
`levels.tide`. `geometry.tide` builds the local 3D scene and `render.tide`
handles lighting, shadow mapping and screen-space ambient occlusion.
Presentation, controls and original audio are local state.
There are no new engine or runtime APIs.

Enable `TIDE_PRIVATE_PROTOTYPES` when configuring the repository. Build
`inside_out` and `inside_out_tests` with the native and web debug presets.
`inside_out_page` stages the web build into the public prototype directory.
`inside_out_play_webgl` and `inside_out_play_webgpu` test two real browser
players with muted audio. The campaign test replays checked-in witnesses
through production commands; it establishes solvability, not optimality
or measured human difficulty. Contracts and validation: `notes/inside-out.md`.
