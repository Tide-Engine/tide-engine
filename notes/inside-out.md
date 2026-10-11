# Inside Out

The owner requested a public online co-op puzzle prototype for two friends
in separate browsers on 2026-10-11. They chose a warm miniature workshop.
The first published build assigned one player to the outer workshop and
one to its room interiors, with twelve introductory puzzles. A follow-up
briefly changed its presentation to isometric 2.5D.

After playing that version, the owner requested polished 2D, harder puzzles
based on planning and interdependent moves, a shared world without fixed
roles, free dimension traversal for both makers, and recursion. They chose
walking into doorways to enter and holding Shift while moving to push rooms.
The current revision implements that request. Its particular layouts and
puzzle rules are game design choices within the prototype request, not new
engine or language requirements.

## Shared world and recursive rooms

`game.tide` contains a deterministic two-seat match. Both makers begin in
the outer workshop and keep their amber/teal identities. Each Traveler has
its dimension, grid position and a bounded entrance trail. No big/small seat
or role-exchange command remains. Both can inhabit the same room and block
one another at the same coordinate, regardless of the entrance used.

The workshop is 7 by 7 and room interiors are 5 by 5. Limits are four physical
room shells, eight parcels, four fixed portals and eight entrance layers.
Room identity is canonical: its interior exists once in match state. A shell
has a parent dimension and cell. Moving it carries its occupants, parcels
and contained rooms without changing their interior coordinates. Shells can
be pushed into other rooms, but placement rejects physical ancestor cycles.
Fixed doorway references may form cycles or point into their own room.

Walking into a shell through an enabled facing doorway enters its opposite
edge cell. Shift changes this to a shell push; parcels are always pushed
normally. Each action moves one object, with no pulling or push chains.
Pushing a parcel or shell into another doorway imports it into that room.
Pushing outward through an interior edge door exports an object beside the
containing room's physical shell. Walls, makers and occupied destination
cells block the action. Destination resolution follows at most one gateway,
so recursive graphs cannot hang the simulation.

A fixed infinity doorway refers to an existing interior rather than cloning
it. Entering a self-reference reaches a different doorway in the same shared
room. Parcels, switches and makers remain the same objects on every visit.
The maker records each shell reference or fixed entrance anchor in its trail.
Walking outward through an edge door pops that trail and returns beside the
entrance, using a shell's latest location. Further entry at layer eight is
blocked without changing state; undo remains available.

If a shell changes parent while occupied, exiting it enters the new parent.
A now-mismatching return prefix is rebuilt from the parent's finite physical
ancestry, so the maker can leave that parent too. Matching recursive prefixes
are retained. Popped entries are reset to their defaults for canonical bytes.
Every movement operates on a trial Puzzle and commits only after all guards
pass: failed door, collision, depth or cycle checks leave it byte-identical.

A latched room moves only while a maker or parcel occupies a Switch tile in
that room. Switches have no timing requirement. Completion requires every
shell on its numbered dock in the specified parent dimension, every parcel
target occupied, every active parcel on a target, and both makers on distinct
leaf exits. Filled targets can be emptied while arranging a solution.

There are exactly two real participants, no bot or solo substitute. Either
absent participant disables gameplay commands. Existing Rooms, Session,
typed sender/sequence validation and host migration are used without new
engine/runtime APIs. Stale board serials cannot carry commands across undo,
reset, selection or participant changes. Shared history retains 128 actions.
Reset loads the exact authored puzzle. Completion waits for explicit Next.
All twelve workshops can be selected from the shelf for prototype evaluation.

## Presentation and controls

`paint.tide` draws one shared top-down workshop scene: the physical workshop,
live nested miniatures and enlarged room interiors appear on both screens
in the same layout. Connection paths and parent labels identify where each
room currently lives. Both makers remain visible across dimensions, with
location/layer indicators and permanent identity colours. Recursive miniature
drawing has a finite depth; it does not duplicate or limit simulation content.
The isometric projection helper has been removed.

The visual treatment uses paper, coloured room frames, wood parcels, distinct
brass targets, leaf exits, doorway marks and latch feedback. Interpolation,
shadows, particles, blocked-action and doorway feedback are local state only.
Reduced motion bypasses movement interpolation and decorative motion.

`ui.tide` provides WASD/arrows, Shift to push a room, Z/Backspace shared undo,
R reset, H layered hints and Enter to continue. Directions match the screen.
The Push rooms toggle provides the modifier for touch buttons. After a serial
change, held directions must be released before sending a fresh action.
Hints are optional and wrap/grow to fit their text. Default level descriptions
state objectives without exposing the decisive solving sequence.

Original audio in `sound.tide` and `sfx/` is reproducible with
`sfx/make_sounds.py`. Entrance, movement, pushes, failed actions, undo, reset
and completion have soft cues. Sound/music/reduced-motion options are local;
Rooms settings control volume. No presentation or audio enters the match hash.

The existing opt-in `TIDE_PRIVATE_PROTOTYPES` builds this game;
`inside_out_page` and `private_games` stage the page beside its gameplay
thumbnail. The catalog is authenticated while the playable page remains
public at <https://tide-engine.dev/prototypes/inside-out/>. Site serving and
catalog boundaries remain in `notes/private-prototypes.md` and
`notes/docs-site.md`.

## Solvability and testing contract

The replacement campaign keeps one short introduction, then develops blocked
pushing stances, parking order, latch handoffs, nested delivery addresses and
recursive access. Its witnessed successful move counts are:

| Puzzle | Moves |
| --- | ---: |
| First delivery | 8 |
| Hold that thought | 21 |
| Reserved space | 32 |
| Passing place | 68 |
| A door to itself | 28 |
| Return post | 36 |
| Packed together | 38 |
| The round trip | 21 |
| Change of address | 42 |
| Loan counter | 29 |
| Shared address | 28 |
| Borrowed space | 30 |

Reserved space requires undocking an initially parked child to clear a parcel
route. Return post temporarily empties a filled target to recover the stance
needed to dock its child. Change of address exports before nesting changes
the parcel's physical destination. Loan counter uses a nested child's north
export to reach a target whose ordinary pushing stances are blocked, then
temporarily un-fills another target before exporting and redocking the child.
Shared address reaches an inaccessible north stance through A-to-B-to-A;
the partner's completed delivery closes its old return route. Borrowed space
posts a parcel into the newly nested child to recover the child's docking
stance, returns the parcel through the partner, and uses a third room's
recursive doorway to deliver another parcel. Outer walls keep the recursive
access rooms immovable, preventing the physical north-entrance bypass.

Campaign evidence pins recursive entries in puzzles 5, 8, 11 and 12 (depths
2, 3, 3 and 3), shell parent changes in 7, 9, 10 and 12, and temporary target
unfills in 6, 9, 10 and 12. Twenty-one witnessed pushes require the partner's
sole switch hold: removing that holder and replaying the same production
command must block without changing bytes or history. These probes establish
the dependencies in the witnesses; they do not exhaust all possible solutions
or prove that every layout is impossible with one maker.

`levels.tide` owns all layouts, objectives and three-tier hints.
`test/solutions.h` stores witnesses that are absent from the public game.
`test/campaign.c` replays them through actual generated Move command handlers
in a world with two joined players. Every individual action must succeed,
both seats must act, and production completion must become true. Final-action
undo and Reset/replay check exact state restoration. Witness length proves
existence, not shortest solutions or human difficulty.

Simulation fixtures exercise free entry/exit for both actors, nested shells,
self-reference and shared interior state, bounded trails and failed actions,
latch cooperation, authorization, malformed/stale commands, completion,
shared history, snapshots, packing and deterministic replay.

`test/browser.mjs` runs the real game in two separate muted Chrome/Edge
windows through a relay and unordered, unreliable WebRTC channels. It uses
the repository's existing bounded DevTools-pipe/process cleanup and serialized
frame helpers. Checkpoints require fresh verified ticks and packet exchange;
runtime, browser and graphics errors fail. Rendered label crops verify shared
actions without wasm inspection or a game-only debug export. The fixture
controls match time; physical devices, NAT reachability and real-time latency
remain separate coverage. `TIDE_INSIDE_OUT_RELAY` optionally probes production
signalling, retaining a local relay by default.

Native two-process scripts and browser captures are kept under ignored
`build/inside-out-captures/`. Runs use `TIDE_HIDDEN=1`, bounded frames/scripts
and muted browsers. They never open the owner's audio devices.

## Earlier published build (superseded)

The original isometric twelve-puzzle campaign passed fourteen native and
WASI fixtures plus WebGL/WebGPU co-op checks. Its witnesses took 8 to 59 moves.
The owner found those puzzles too easy; those lengths were never a human
difficulty measurement. That campaign and fixed-role presentation are replaced.

The initial public Fly deployment succeeded on 2026-10-11, machine version252,
image `deployment-01M4NW4CRPNFH1X3P4MRYWDY2B`. Anonymous page/thumbnail requests
returned 200 and the page hash was
`57a970dbf68a347b5502dca6eec59db4249c88b691a0881ee5e077ac2d39f21a`.
Two browsers exercised those downloaded bytes with the production relay in
17.1 seconds. Later independent JSON/text and session changes passed scoped
integration checks. This evidence describes the earlier build only.

## Shared-world revision validation, 2026-10-11

All eighteen game fixtures pass natively and under WASI: sixteen simulation
cases plus the full twelve-puzzle production-command campaign and its
identity-swapped replay. Native debug, web debug and web release game builds
pass without warnings. The final browser fixtures pass on WebGL (32.74 s)
and WebGPU (17.41 s), covering real two-browser admission, Shift pushing,
free entry by both makers into one interior, synchronized undo/reset and
held-input recovery, the first witnessed completion/progression and phone
touch movement. No runtime or graphics errors were reported.

Native captures at 1280 by 800, 900 by 800 and 390 by 844 were inspected.
The final capstone capture shows both makers, a shell physically nested in
Mint and the Mint/Lilac reference cycle. The refreshed catalog thumbnail
comes from that actual gameplay capture. A separate self-door capture shows
the maker entering the same canonical room twice. These are desktop/browser
viewport checks; physical phones, cross-machine NAT and human difficulty
remain unmeasured.

The site check has zero errors/warnings; production build, eighteen
analytics/admin tests and five production HTTP/SEO/access tests pass. The
staged page, release page and production site copy have identical SHA-256
`fcd07e6e5347c708a75713e1e6ba8257b37082cade06c8181f1503e289865d7a`.
The new thumbnail SHA-256 is
`0db7607f0c8c8c579514a7c9d735fd46dcd8d989cb81df8557ff518fae959920`.
