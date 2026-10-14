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

Integration: merged `origin/dev` through `a2ae312f`, retaining the incoming
SWINDLE catalog/build entries and this prototype's entries. The initial
resolution script stopped on Windows' default text encoding; the restored
registrations were reapplied with explicit UTF-8. Both scoped debug game
builds/tests and the merged docs production build, eighteen analytics/admin
checks and production HTTP test pass. The PDF has a scoped binary attribute
so git cannot normalize its bytes or treat its streams as text diffs.

Final shared-keyboard check: clicking the oscilloscope had let GUI focus
navigation consume Coil's arrows. Gameplay now claims the keyboard and its
pointer-only HUD controls claim their rectangles without taking keyboard focus;
the room menus and modals retain ordinary GUI controls. A native scripted
shared-keyboard run completes chamber one, and the browser fixture repeats
the specific probe-click/Coil-arrow/commissioning sequence as a regression.
The game forwards Escape to the room page while it owns the keyboard; the
fixture checks Escape opens that menu and resumes. Repeat navigation uses a
distinct review query so a previous frame cannot satisfy a new page's readiness
predicate. The optimized shipping page also passes the shared-keyboard and
online walkthrough on WebGPU.

Final validation: all three registered web debug CTest targets pass (six C
cases plus WebGL and WebGPU playthroughs, including the new keyboard/menu
regressions). Native debug's six C cases and hidden solo/shared-keyboard
completion runs pass; native/web optimized C cases also pass. The final
optimized WebGPU page passes the entire browser fixture. Merged the later
DEADWEIGHT registration through `03cb14af`, retaining all eight catalog ids;
the production docs build and all nineteen analytics/admin/HTTP checks pass
again. No incoming engine/compiler dependency changed.

The 1.68 MB delivery archive is CRC-checked and every listed SHA-256 matches.
Its renamed Windows executable requires the identically named
`Copper-and-Coil.pack`; this was corrected during the packaged-launch check.
The actual packaged executable completes the scripted chamber and produces
nonzero audio with peak 5,962. The staged browser bytes equal the optimized
page; the PDF contains four readable, visually checked pages.

## Input-only solvability verification (2026-10-14)

The owner requested verification of every level through player input. Native
debug was rebuilt in this checkout, and two hidden standard-host scripts
completed the campaign from the title screen: solo with WASD/E/Q, and shared
keyboard with WASD/E plus arrows/Enter. Mouse input selects live probe rows
and clicks the completion button between chambers. Neither script writes
match state, sends commands directly, teleports robots or uses the Chambers
selector to skip progression. Each chamber's bridge and completion card were
captured; both final review lists show all six completion markers. The native
completion cards were visually checked together. Input scripts and captures
remain ignored build artifacts in `build/copper-coil-input-verification/`.

The working configurations are:

| Chamber | A | B | MIX | OUT | + C | Live row (A, B, C) |
|---|---|---|---|---|---|---|
| 1: It takes two | WIRE | WIRE | AND | WIRE | unused | 1, 1 |
| 2: A little resistance | WIRE | NOT | AND | WIRE | unused | 1, 0 |
| 3: One or the other | WIRE | WIRE | XOR | WIRE | unused | 1, 0 |
| 4: The quiet hours | WIRE | WIRE | OR | NOT | unused | 0, 0 |
| 5: On the same wavelength | WIRE | WIRE | XOR | NOT | unused | 0, 0 |
| 6: The last light | WIRE | WIRE | XOR | WIRE | AND | 1, 0, 1 |

The browser fixture previously completed only chamber one through platform
input; its six-chamber coverage was the separate C command-level fixture.
`test/browser.mjs` now continues through all six in order using keyboard/mouse
events and the real completion buttons, retaining bridge/completion captures
and the final marked chamber list on both backends. It checks the repeated
completion heading for chambers two through five and shared progression,
then keeps the existing undo, shared-keyboard/menu, two-player WebRTC and
narrow-touch checks. No gameplay, engine or language behavior changed.

Validation: native debug's six C cases pass, as do all three Copper & Coil
web debug CTest targets (the same six C cases, the expanded WebGL playthrough
in 342.54 seconds and WebGPU in 47.92 seconds). Both browser final review
captures were visually checked and show six completed chambers. Fresh builds
use this checkout's compiler/runtime and Node 24.19.0 for web tests; the
machine's Node 24.15.0 was replaced for this run by an ignored build-local
download, without changing the owner's installation. No gameplay fixes were
needed. Real phones, other operating systems and an all-six WAN playthrough
were not tested; no CI was dispatched.
