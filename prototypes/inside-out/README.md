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

The top-down scene is shared: both screens show the workshop, every pocket
room and both makers. Amber and Teal keep their identity colours wherever
they travel. Each room has a coloured number, a live miniature in its shell,
and an enlarged interior. Connection paths show where its shell currently
lives. Nested and recursive previews stop drawing after a few layers;
the underlying room remains the same shared space.

| Action | Keyboard |
| --- | --- |
| Walk and push a parcel | WASD or arrow keys |
| Enter a room | Walk into an open doorway |
| Leave a room | Walk outward through an edge doorway |
| Push a room shell | Hold Shift while moving |
| Undo the last shared move | Z or Backspace |
| Reset this puzzle | R |
| Show / hide hints | H |
| Continue after solving | Enter |

The **Push rooms** button toggles the same modifier for touch controls.
Turn it off to enter a room again. Directions match the screen: W is up,
D right, S down and A left. After undo or reset, release a held direction
before pressing it again.

The shelf can revisit or skip any of the twelve puzzles. A plus beside a
title marks one you have completed in this match. Hints start with a small
nudge; ask for more to reveal an arrangement and then a more specific plan.
They are optional. Music, sound and reduced motion are local settings.

## The rules

- Both makers can enter any open room. People in the same room block each
  other, even if they arrived through different recursive doorways.
- A moving shell carries its entire interior without changing anything's
  position within that room. A shell can contain another shell.
- Holding Shift pushes one room instead of entering it. Parcels are pushed
  normally. Nothing can be pulled, and pushes cannot move a chain.
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
- Fill every brass parcel target, park each shell on its matching numbered
  dock in the correct dimension, and put both makers on distinct leaf exits.
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
