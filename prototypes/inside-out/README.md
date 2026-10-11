# Inside Out

A cooperative puzzle prototype for **two friends in separate browsers**.
One of you rearranges little room boxes on a workshop table. The other walks
inside those same rooms, pushing parcels and travelling through their doors.
The workshop is warm and quiet, with colourful miniatures and original soft
movement sounds. There are no timers, lives or score penalties.

Open `/prototypes/inside-out/` on the deployed Tide site. The admin prototype
catalog links to the same public page; invited players need no admin account.
One player chooses **Host a workshop** and shares the invite link or room code.
The other opens that link or chooses **Join a friend** and enters the code.
Both players must be connected before
either can move. Use a voice call or chat alongside the game to discuss plans.

## Playing

Both screens show the large workshop and a room interior in a fixed
isometric cutaway view. Raised room boxes have live miniatures in their open
tops; wooden parcels and little maker figures occupy the same puzzle grids.
High rear walls and low front walls keep the room's contents visible. Both
boards face the same way, so switching scales preserves your bearings.
The small partner's occupied room follows them automatically; room tabs on
wider screens let either player inspect the others. Your role indicator tells
you which scale your movement controls affect.

| Action | Keyboard |
| --- | --- |
| Move / push | WASD or arrow keys |
| Exchange scales at a hatch | E |
| Undo the last shared move | Z or Backspace |
| Reset this puzzle | R |
| Show / hide hints | H |
| Continue after solving | Enter |

The diagonal touch pad shows where each direction goes: **W/Up moves
upper-right, D/Right lower-right, S/Down lower-left, and A/Left upper-left**.
The same direction compasses appear beside both boards on wider screens.
Hints use the grid directions: up is W, right is D, down is S and left is A.
After undo, reset or exchanging scales, release a held direction before
pressing it again.

The on-screen buttons provide the same actions. The puzzle shelf can revisit
or skip any of the twelve puzzles. Hints have three increasingly specific
tiers, with cards that wrap and resize to fit their text. Sound and music can
be toggled, and the Rooms menu's Settings page controls volume.

## The rules

- A large partner pushes one room box or workshop crate at a time. Boxes
  cannot be pulled and pushes never move a chain of objects.
- A room carries its small partner and every parcel inside it when it moves.
  A move outside never changes their interior positions.
- Rooms connect when their boxes are cardinal neighbours and their facing
  doors meet. The small partner and parcels can cross the connected doors.
- A parcel pushed through an unmatched door emerges beside that box in the
  workshop. People need a connected room to cross. The large partner can
  push a workshop parcel through a matching door into a room.
- A latched room can move only while its brass switch is occupied by the
  small partner or a parcel. Stepping off does not move or destroy the room.
- To exchange scales, the small partner stands on a hatch and the large
  partner stands beside their room box. Press E. You exchange control of the
  existing avatars; nothing else changes size or position.
- Solve a puzzle by filling every brass crate target, parking every room on
  its coloured numbered outline, and putting both partners on their leaf
  exits. A filled target may be emptied again while you work.

Undo belongs to both players and takes back the last successful move or scale
exchange, whoever made it. The last 128 actions are available. Reset always
restores the complete initial puzzle. A completed puzzle waits for you to
continue, so you can inspect it or undo its final move. Losing a partner stops
gameplay until a second participant is present again.

## The twelve workshops

| Workshop | What it explores |
| --- | --- |
| First delivery | Move the room outside and its parcel inside. |
| Good neighbours | Connect two rooms to give your partner a route. |
| Through the doorway | Send a parcel into the other room. |
| A helpful doorstop | A parcel keeps a room's movement switch pressed. |
| Out for delivery | Export a parcel and deliver it at the larger scale. |
| Return post | Import a parcel, then connect a room behind it to push inward. |
| The sorting line | Assemble a three-room delivery route. |
| Around the corner | Change a parcel's direction in the middle room. |
| Pass the key | One travelling parcel unlocks two rooms in sequence. |
| Passing place | Deliver first, then exchange the room boxes using a bay. |
| The way home | Return a latched ferry for your partner after delivery. |
| The whole workshop | Clear an incoming parcel off its target to dispatch another, then restore it and return home. |

The opening workshops introduce one relationship at a time. The final four
combine inside and outside planning. Their pace depends on your conversation
and discoveries; the prototype does not time or rush them.

## Solvability evidence

`test/solutions.h` contains a hand-built solution for every puzzle.
`test/campaign.c` sends each individual move through the production generated
two-player command handlers, checks that it succeeds, verifies completion,
undoes the final move, resets and replays the full solution. The role-exchange
check solves Return Post with the players controlling the opposite scales.
No alternate movement simulator supplies the evidence. These are valid
solution witnesses, rather than claims of shortest paths.

Every initial puzzle puts both avatars away from their required exits. Only
movement at the respective scale can change that avatar's position; a hatch
changes ownership alone. Thus both scales must act to complete every puzzle.
The match requires two real connected players and has no bot replacement.

## Building and testing

Enable `TIDE_PRIVATE_PROTOTYPES=ON` when configuring this checkout. Build
`inside_out`, `inside_out_tests`, and on web `inside_out_page`. The existing
`private_games` target includes the public page staging. Run the scoped
`inside_out_tests` check on native and web, plus the browser integration
checks registered for the prototype. Notes and validation evidence live in
`notes/inside-out.md`.

The browser fixture normally uses its own local relay. Set
`TIDE_INSIDE_OUT_RELAY` to an existing WebSocket relay URL to probe that service
with the same two-player checks.

During agent runs use `TIDE_HIDDEN=1`, only binaries from this checkout, and
bounded `--capture`, `--frames` or `--script` options. Browser launches require
`--mute-audio` even when headless.
