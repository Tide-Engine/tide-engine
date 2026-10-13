# Copper & Coil

Owner request (2026-10-13): a cooperative redstone/logic-gate puzzle prototype,
a ready demo and material suitable for a publisher. The working title,
robots, visual direction and six-chamber sequence are this implementation's
design choices. No language/runtime changes or full-game production scope
are proposed as approved requirements.

## Game and contracts

`prototypes/copper-coil/` is an opt-in Tide game with the Rooms package and
STATS. Native target `copper_coil`, test target `copper_coil_tests`, web staging
target `copper_coil_page`; `private_games` collects it. The catalog id is
`copper-coil`, served at `/prototypes/copper-coil/`. Its server-only card,
thumbnail, public-root/outside-root patterns and admin/HTTP test lists agree.

Two robots work separated banks of one isometric workshop. Copper owns the
A modifier and main mixer; Coil owns B, the output modifier and the final
safety mixer. Gray consoles are fixed for a chamber. WIRE/NOT modifiers and
AND/OR/XOR mixers form a static acyclic signal graph. The final mixer adds C
in chamber six. The six required output masks, for rows numbered A + 2B + 4C,
are 8, 2, 6, 1, 9 and 96 (AND, A AND NOT B, XOR, NOR, equality, XOR AND C).

All rows must match, the current probe output must be ON, and both robots
must occupy their own SYNC pads for 36 ticks at 30 Hz. The bridge then latches
on. Consoles stop changing while powered. Both robots must reach their distinct
exit tiles to complete the chamber. The probe panel changes live signals;
in-world levers toggle A/B and, in the final chamber, C. It does not edit gates.

Match state is fixed-array Circuit data, 128 undo entries, chamber/completion
bits and seat assignments in Station. Movement, configuration, progress,
charging and history participate in snapshots, hashes and packing. All player
requests are typed commands with a chamber serial. Handlers reject invalid
indices/actions, stale serials and unknown senders. Two connected players
control only their own robot; one player may control both. Seat associations
permit rejoining until another player takes the vacant seat.

Solo controls are WASD/arrows + E/Space, Q to switch. Shared keyboard is
Copper WASD + E and Coil arrows + Enter. Tab remains GUI focus navigation:
the first browser playthrough exposed its conflict with room-menu focus, so
robot switching moved to Q. Online users each use the ordinary movement keys.
Narrow screens offer touch controls and a compact test board. Z undoes, R
restarts, and all six chambers are accessible for review. Undo can reverse
commissioning and gives a fresh charge interval rather than relatching instantly.
Completion credit stays in the match after undo; there is no cross-session save.

Rendering is game-authored 2D mesh geometry, with shaded isometric faces,
robots, animated signal dots and original arithmetic-only sound cues. It uses
the existing Draw, GUI, Audio and Rooms APIs. Visual easing, hints, selection
and effects are local state. Reduced motion is in Guide; master volume is the
package's Settings. Block faces remain consecutive mesh draws to permit
batching, rather than interleaving an edge-shape draw on every floor face.

## Publisher material and review

`docs/static/prototypes/copper-coil/` contains actual title/gameplay/completion
images, the 960x540 catalog thumbnail and a four-page publisher PDF. Its source
is `test/make_pitch.py`, using ReportLab and Segoe fonts on Windows. The README
has controls, online setup, build instructions and an eight-minute meeting route.
The pitch labels distinct room layouts, movable modules/routing, save/resume,
gamepads/platform invites and a funded vertical slice as proposed work. It
claims no external user study, sales forecast, funding budget or release date.

The self-contained optimized browser page and optimized Windows executable
with its pack are packaged with the PDF, screenshots and README in a local
delivery ZIP. Compiled game pages and the delivery archive remain build outputs.
`test/serve.mjs` serves `docs/static` on loopback port 4179 with cross-origin
isolation headers; the handoff address is
`http://localhost:4179/prototypes/copper-coil/`.

## Validation

Six C cases pass in native debug/release and web debug/release. They exercise
all six chambers through real player movement/console commands (BFS only finds
walking routes; it never changes the match), wrong circuits, both-pad/live
power requirements, interrupted charging, authority and invalid/stale commands,
fixed consoles, shared undo, partner departure, snapshots and packed restore.

The two-browser fixture uses the existing Inside Out DevTools/frame helpers,
a local relay and actual unordered/unreliable WebRTC channels. WebGL and
WebGPU pass solo circuit editing/undo, commissioning/crossing/completion,
two-player signal edits and shared progression, plus narrow touch rendering.
It enables Tide.checkGL and fails on runtime/shader/GPU/browser errors. Chrome
is muted. The first cached Chrome could not create a tab; the installed Chrome
works. Static comparison crops stay inside the completion card, excluding
animated scenery and robot-selection outlines. Captures were visually reviewed.

Native hidden scripted play completes chamber one and produces original cues;
the captured 10.333-second stereo WAV contains 435,104 nonzero samples and a
peak of 5,962/32,767. Sound was inspected numerically, not listened to. PDF pages
were rendered through Poppler and visually checked; its build-feature panel
was tightened after the first render exposed overflow. The docs production
build, Svelte check (zero errors/warnings), 18 analytics/admin checks and the
production prototype HTTP test pass with Node 24.19.0.

No engine/compiler/platform changes, deployment or CI dispatch. Real phones,
gamepads, WAN/TURN/host migration and other operating systems are untested.
This is a six-circuit prototype in a shared layout; free wiring, different
physical chambers and persistence are future production work.
