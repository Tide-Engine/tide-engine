# Inside Out

A cooperative puzzle workshop for **two friends in separate browsers**.
Both makers begin in the same world. Either can step into a pocket room,
push a room with their partner inside, or put a room inside another room.
Some doorways lead back into the very room that contains them. Those visits
share one set of parcels, switches and people.

Play at <https://tide-engine.dev/prototypes/inside-out/>. The admin prototype
catalog links to the same public page; friends need no admin account.
Choose **Host a workshop**, then share the invite link or six-character room
code. Both players must connect before either can move. Use a voice call or
chat alongside the game to discuss plans.

## Playing

Each screen follows its own maker in one continuous scene. Step inside a
room and the camera zooms into its actual doorway. Your whole current room
stays in view, with a wider fixed rim of containing-world context around it.
You can see part of a maker standing beside your box.
The camera keeps that framing when your partner moves or leaves the screen.
Your maker walks and shrinks through the doorway; leaving grows them back
into the surrounding world. Your partner can watch that passage from outside.
The outside follows the doorway you used: a moved shell changes your surroundings,
and a recursive doorway can put the same room
around itself. Amber and Teal keep their identity colours wherever they
travel. A room shell contains a live miniature of its shared interior;
nested and recursive preview details collapse to markers after a few layers.

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
another object or a closed latch stops a room, walking into its open doorway
enters it. Directions match the screen: W is up, D right, S down and A left.
After undo or reset, release a held direction before pressing it again.

The shelf can revisit or skip any of the twelve puzzles. A plus beside a
title marks one you have completed in this match. Hints start with a small
nudge; ask for more to reveal an arrangement and then a more specific plan.
They are optional. Music, sound and reduced motion are local settings.

## The rules

- Both makers can enter any blocked room through an open doorway. People in
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
- An infinity doorway is a fixed entrance into an existing room. A self
  doorway returns to the same interior from a different side. Moving a
  parcel during one visit changes it for every visit.
- A maker's return path remembers the entrance used. Leaving through an
  edge door returns beside that entrance; if a shell has moved, the return
  moves with it. Up to eight entrance layers are available.
- A latched room moves only while a maker or parcel holds its brass switch.
  There is no timing requirement. Moving it may need one of you to stay
  inside while the other finds the correct pushing side.
- Fill every box-marked parcel target, park each shell on its matching numbered
  dock in the correct dimension, and put both makers on distinct player goals.
  Parcels must finish on targets. A filled target can be emptied while
  arranging the solution.

Shared undo takes back the last successful action, whoever made it, and
retains 128 actions. Reset restores the complete starting arrangement.
Completion waits for you to continue. Losing a participant stops gameplay
until both seats are present again.

## Development

The prototype uses Tide and the existing Rooms package. Its deterministic
match rules are in `game.tide`; campaign layouts and optional hints are in
`levels.tide`. Presentation, controls and original audio are local state.
There are no new engine or runtime APIs.

Enable `TIDE_PRIVATE_PROTOTYPES` when configuring the repository. Build
`inside_out` and `inside_out_tests` with the native and web debug presets.
`inside_out_page` stages the web build into the public prototype directory.
`inside_out_play_webgl` and `inside_out_play_webgpu` test two real browser
players with muted audio. The campaign test replays checked-in witnesses
through production commands; it establishes solvability, not optimality
or measured human difficulty. Contracts and validation: `notes/inside-out.md`.
