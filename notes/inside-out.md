# Inside Out

The owner requested a public online co-op puzzle prototype for two friends
in separate browsers on 2026-10-11. The initial workshop, fixed player roles,
isometric experiment, switch locks and fixed recursive entrances are
superseded. The current direction is actual 3D with a fixed 2.5D view, harder planning and
interdependent moves, physically nested starting rooms, varied sizes and
offset doors, and movable boxes including self-nesting. The owner chose
spatial constraints instead of locks, and an unsettling institutional /
PSX-inspired atmosphere without threats or jumpscares. These are game design
choices within the prototype request, not engine or language requirements.

## Shared world and recursive boxes

`game.tide` contains one deterministic two-seat match. Both makers start
inside the same small box, itself inside an ordinary containing room box
that can be left. The root is called Outer room. Makers retain amber/teal
identities. Their coordinates use one canonical interior: they block one
another even when they arrived through different copies.

Root and room dimensions are authored independently, bounded to 1..7 on
each axis, with 49-cell storage and actual-width row stride. Each room has
a four-side door mask and per-side offsets; -1 selects the midpoint.
Invalid offsets and destinations block atomically. RoomLayout array elements
require explicit literals to obtain defaults: a zeroed LevelLayout does
not apply field defaults inside its room array.

Limits: four canonical interiors / primary boxes, four movable reference
boxes, eight parcels and eight explicit entrance layers. IDs 0..3 are primary;
IDs 4..7 are references. `LevelLayout.references` stores destinations;
`Puzzle.references` stores live parents and positions. References use their
canonical room's doors and offsets. Small stencils distinguish primary 1
from copy 1′ (and 2..4), with identical materials and opening geometry.

Every instance tries to move first. Walls, boxes, parcels and the partner's
body can be backstops. A failed push tries the facing doorway using unchanged
state. There are no switch locks, modifiers, pulling or push chains. A moving
box carries its interior and occupants without changing interior coordinates.
Objects pushed into another doorway enter its canonical interior. Objects
pushed out emerge beside that room's primary box; makers return through the
instance they entered. This distinction is stated in the guide and README.
Resolution crosses at most one boundary and one gateway per action;
occupied door cells block unbounded gateway chains.

Self-placement and two-room cycles are allowed without cloning interiors.
`Entry.box` resolves the live entered instance, so a moved occupied box
changes its maker's return location. A mismatching prefix rebuilds from
primary ancestry, stops at the first repeated room, and retains finite
unique-room ancestry instead of inventing an outer anchor. Matching visits
retain their explicit trails. Depth-eight entry blocks unchanged; undo
remains available. Popped slots reset to defaults for canonical bytes.

Movement commits a trial Puzzle after all guards pass. Completion requires
designated primary boxes on numbered docks in the specified parent, every
parcel target occupied, every active parcel on a target and both makers on
distinct player goals. Targets may be emptied during planning. Shared history
retains 128 actions. Reset restores boxes, references and trails exactly;
completion waits for Next. Exactly two real participants are required.
Existing Rooms, Session, sender/sequence validation and host migration remain
without new runtime APIs. Missing seats disable commands; stale serials
cannot cross undo/reset/selection/participant changes. All twelve puzzles
can be selected from the public shelf.

## Presentation, controls and audio

Miniatures and focused rooms share one transform: inner tile scale is
1 / max(width,height). The longest box edge fills its containing cell.
Frames sit inside the rectangle with real doorway gaps. Offset door lips
preserve their tangent rather than projecting radially toward the midpoint.

Camera and maker passage share a 0.55-second clock: approach 0..35%,
doorway shrink 35..70%, interior step 70..100%, reversed on exit. The camera
holds the miniature during approach, then zooms continuously. Aperture
clipping and solid-wall occlusion prevent silhouettes showing through walls.
Same-room recursion and adjacent handoffs use these transforms. Interrupted
travel begins from the rendered pose; input stays responsive. Reduced motion,
level changes and zero-move reset discard stale passages. There is no fade.

Each settled room fits max(width,height)+6 tiles in a fixed centered square,
revealing actual containing-world context. Outer room retains its 0.96
viewport-side fit. Partner presence, position and room never change camera
targets; makers may leave the viewport. Surroundings resolve the live entered
instance, including moved copies and self-containing boxes. Drawing is bounded
to three ancestor contexts, three descendant levels and 48 instances per
context; subpixel previews become markers. No match objects are duplicated.

`geometry.tide` builds normalled, depth-tested cube instances for floors,
low cutaway walls, crates, stencils and maintenance makers. The orthographic
camera has a fixed 65-degree pitch and no yaw; ground coordinates preserve
the existing screen axes and camera framing. Each occurrence has a shared
ground plane; component elevations change world Y only, keeping heads, lids
and trim physically stacked. Interior floors rise by 0.018 containing tiles.
Finite contexts and descendant limits remain unchanged. Geometry, lighting
and render resources are machine-local; layouts, rules and audio are unchanged.

`render.tide` uses a common scene/depth for every visible occurrence: Rgba8
material plus Rgba16f normal/occurrence-scale MRT, a fixed-focus 1024-square
directional shadow map with four comparison taps, and half-resolution SSAO.
Eight normal-oriented hemisphere samples reconstruct world positions from
hardware depth and inverse camera projection. Metric radii/biases follow
the originating occurrence's tile scale. Two separable bilateral passes
respect normal and depth discontinuities; AO affects ambient light only.
No temporal jitter or geometry-dependent camera fitting is used. Alpha and
doorway ground-column discard rules are shared by all mesh passes. The
composite explicitly clips the board because Blit does not inherit Draw.Clip.
Workbench prepares geometry and SceneLighting owns view-only shader draws.

Materials use square edges, worn tiles, subdued colours, cast shadows,
screen-space contact shading and fluorescent accents. Maintenance makers retain AMBER/TEAL colours.
Targets show box icons, player goals show player icons and docks show numbered
outlines. Panels/buttons share a dark institutional palette. There are no
enemies, threat events, jumpscares or timers.

WASD/arrows walk/push, Z/Backspace undo, R reset, H hints and Enter continues.
Touch uses the same directional buttons. Held directions require release
after serial changes. Hints grow/wrap to fit. Ambience, sound and reduced
motion are local options; Rooms volume settings remain. Presentation and
audio never enter the match hash.

`sfx/make_sounds.py` reproduces original assets. The melody is replaced by
`ambience.wav`: a seamless 12-second mono 24 kHz PCM16 ventilation/fluorescent
hum, filtered seeded noise and subtle 50/100 Hz harmonics. Peak -28.754 dBFS,
RMS -36.725 dBFS before 0.27 gain; no clipping and a 14-PCM-unit boundary delta.
There are no stingers or sudden threat cues. SHA256:
`274097dcafdf4c1355d435444dc2a3ae8bc59a6cb77684b320d4448e2498c715`.
The obsolete `workshop.wav` is removed. Seven short cues remain byte-identical.
`enter.wav` remains the 300 ms seeded-noise friction and air puff, without
oscillators, coin ringing or note sweeps: 7,200 mono 24 kHz PCM16 frames,
silent endpoints, no clipping, -12.906 dBFS peak before 0.38 gain. SHA256:
`657d7c91dc2cd45538453e7956af5ea608df74f5dd90f7d1c835dfee9fe4f40a`.
Entry/exit/depth changes select kind 2 before push cues, including doorway
travel that pushes a parcel.

## Solvability and verification contract

The authored replacement campaign has these production-command witnesses:

| Puzzle | Moves |
| --- | ---: |
| A room outside | 10 |
| No handle inside | 12 |
| The other opening | 20 |
| A place to stand | 31 |
| Passing place | 41 |
| The same corner | 21 |
| Carried together | 41 |
| Before it moves | 40 |
| Two ways home | 34 |
| Return address | 51 |
| Folded landing | 26 |
| Crossed return | 28 |

Return address requires a copied box's parent to change into its own
canonical interior and then extraction. Folded landing forms and extracts
a primary self-cycle. Crossed return forms and extracts a primary two-room
cycle. Carried together pins two partner-body backstops with counterfactual
production commands: removing that partner makes the box push instead of
admitting the approaching maker.

No handle inside exhausts all 167 reachable production-command states for
either initial maker acting alone with the other body removed, ignoring
player goals. Neither can both dock the home and deliver its parcel. This
establishes a mechanical co-op dependency for that layout; it does not prove
solo impossibility or human difficulty for the entire campaign.

`levels.tide` owns twelve nested-start layouts and three-tier hints. A short
intro teaches leaving two containing boxes; later puzzles develop backstops,
occupied transport, narrow storage, offset exits and movable recursion.
Witnesses in `test/solutions.h` are absent from the public game. Campaign
checks invoke actual generated Move handlers with two joined players: every
action succeeds, both seats act, production completion is true, and undo /
reset-replay restore exact state. Witnesses prove solvability, not shortest
paths or measured human difficulty. Final campaign pins/results follow below.

PuzzleCue carries the accepted direction and source Traveler for travel.
An outward step emits kind 2 even when cycle rebasing leaves room/depth/box
identity unchanged. A paint-owned all-seat listener handles this exit only
when snapshots missed it, validates the live box/endpoint against late states,
starts at the captured border pose and clears pending cues on undo/reset.
Ordinary crossings consume cues without replay. This is event/local data,
not new match state. The test-only `test/cues.tide` primitive/source listener
enables a typed-command cue fixture and is absent from the public game.

Core fixtures cover bounds, offset traversal, canonical collision, occupied
instances, cycles/extraction, atomic blockage, history, snapshots, packing and
deterministic replay. `test/browser.mjs` runs two muted Chrome/Edge windows
through real unordered/unreliable WebRTC using existing bounded process/pipe
and serialized-frame helpers beside it. Checkpoints require fresh verified
ticks and packets; browser/runtime/graphics errors fail. Rendered labels and
colour crops observe production play without wasm inspection or debug exports.
Crops on one page run sequentially to avoid Chrome's temporary viewport race.
Local relay is default; TIDE_INSIDE_OUT_RELAY probes production signalling.
Real devices, NAT and wall-clock network latency remain separate coverage.

Bounded scripts, audio and browser captures live in ignored
`build/inside-out-captures/`; TIDE_HIDDEN=1 and muted browsers prevent owner
audio devices opening. `inside_out_page` / `private_games` stage the opted-in
prototype beside its thumbnail. Public play remains
<https://tide-engine.dev/prototypes/inside-out/>; catalog authentication and
serving/deployment contracts remain in `notes/private-prototypes.md` and
`notes/docs-site.md`.


## 3D revision validation (2026-10-12)

Native and WASI retain all 33 fixtures, including twelve production-command
solutions, exchanged jobs, exact undo/reset/replay and the exhaustive second
puzzle co-op check. No match, layout, hint or audio file changed. Native,
web debug and web release builds are warning-free. Both two-browser fixtures
pass: WebGL 125.98 s and WebGPU 89.89 s. They retain actual online admission,
three production completions, phone touch, continuous doorway growth/shrink,
nearby/offscreen partner framing, carried interiors, physical self-nesting,
undo and unchanged-room/depth/entrance cycle exits.

Illumination multiplies material RGB uniformly. Camera/interior assertions
therefore compare exact brightness-normalized material masks, with mandatory
feature and boundary presence, rather than treating a moving shadow as a
camera movement. Quantitative zoom growth, maker visibility, narrowing and
travel direction remain checked. Undo and settled-cycle whole-scene RGB
comparisons remain exact. Old uniform wall crops are replaced by real edges.

Bounded 1280x800, 1920x1080 and 390x844 native captures were inspected, including
offset entrances and the unchanged-identity primary self exit. Components
share each occurrence's ground plane so lids, heads and trim stack vertically.
CPU cropping includes a conservative caster guard based on its top above
the minimum containing floor; artificial cut faces cannot shadow the view
under the fixed light and camera. The refreshed 960x540 gameplay thumbnail
is 128,622 bytes, SHA-256
`61bcfef843b34a0e24ffdbd7eb5560a5193d4cd272419f461d3028ee7c0f511c`.

Controlled full/no-AO/no-shadow native captures prove both effects: removing
AO changes 30,461 initial scene pixels (maximum channel delta 55); removing
cast shadows changes 37,026 (maximum 92). An empty 5,280-pixel floor patch
changes zero pixels with AO removed, and both comparisons change zero pixels
outside the scene. Temporary comparison edits were restored byte-for-byte.
The final caster-guard build's initial and outside images are RGB-identical
to these measured baselines. Evidence lives in ignored `3d-lighting-*` and
`volume-*` captures under `build/inside-out-captures/`.

The production site build, zero-error/warning check, eighteen analytics/admin,
227 grammar and five HTTP/SEO/access tests pass. All 64 release/staged/package
playable pages and four thumbnails agree. Inside Out is 1,385,947 bytes,
SHA-256 `2b71dad7d5b74c912fc55801122d44eba762f5dfbf414cbf10ef05bae4947483`.
Its pack contains each unchanged current WAV exactly once; removed music,
coin and tonal-doorway assets remain absent. No CI was dispatched. Physical
phone/GPU coverage, audio listening and external NAT conditions remain separate.

## Nested-box revision validation (2026-10-11; 2D presentation superseded)

Final native desktop, 900px and phone captures include the primary self-cycle
exit whose canonical room, depth and entered box remain unchanged. Approach,
middle and settled poses show continuous passage; doorway jambs use square
rectangles. The refreshed 960x540 gameplay thumbnail SHA-256 is
`082d090d130c07b526c6921cea56490a61fc4d23e49dfb58f0e49187d2d9ed78`.
A device-free 15-second production mixer capture verifies the new ambience:
source correlation 0.9999975, gain 0.2699908, peak -40.125 dBFS, identical stereo
channels, no clipping and exact repeated samples across the 12-second loop.
Hardware listening is not claimed.

The native build is warning-free and all 33 fixtures pass: 30 core cases,
all twelve command witnesses with undo/reset/exact replay, all twelve with
exchanged jobs, and the second puzzle's exhaustive mechanical co-op proof.
The WASI aggregate passes as well. Two-browser WebGL and WebGPU checks pass
in 117.9 and 57.4 seconds, respectively, with three production solutions,
phone touch, stationary-partner camera crops, occupied-box context, actual
primary self-nesting/undo/extraction and an unchanged-room/depth/box cycle
exit. The final square-jamb presentation and integrated dependency checks
are recorded below after staging.

Bounded native desktop, 900-pixel and phone captures review nested starts,
offset miniature doorways, self-recursive entrances and the exact primary
self-exit. The final same-room passage starts at its real east border,
crosses continuously and restores aliases without a finite-end pop.

A 15-second device-free production ambience capture is stereo, unclipped,
and repeats its twelve-second period byte-identically. Source correlation is
0.9999975, measured gain 0.2699908 and mixed peak -40.125 dBFS. The seven
retained short WAVs remain unchanged; public pack validation follows staging.
Real phones, hardware audio listening and network/NAT conditions beyond
this machine remain unmeasured.

Integration through `origin/dev` `825c6542` is clean. Shared compiler,
network/host dependencies and current Underfoot were rebuilt. Native and WASI
retain 33/33 passing fixtures without warnings; final square-jamb two-browser
checks pass, WebGL in 113.42 s and WebGPU in 49.89 s. All 64 release pages were
rebuilt/restaged (46 docs examples, 14 gallery games, four prototypes), and
their staged/production-package bytes agree. All four thumbnails agree too.
Inside Out HTML is 1,356,103 bytes with SHA-256
`f305bd41a4733ba85f119337e4bbaf9ee9ddbd96dff31ee71d572d18a82b500e`.
The decoded 784,288-byte pack contains each of the eight current WAVs once,
including the exact ambience/doorway assets above. Removed workshop music,
the original coin cue and the previous tonal doorway cue are absent.
Site check has zero errors/warnings; 227 grammar, 18 analytics/admin and five
production HTTP/SEO/access checks pass. The production build and complete
WebGL example/gallery rendering smoke pass. No CI was dispatched.

Source `7a44440f` deployed to the existing Fly site as release 263, image
`deployment-01M4PF2HHN71TH2ZJ2C4S8M4XF`, with passing machine health checks.
All four anonymous prototype pages and thumbnails return 200 and match the
tested package. Inside Out retains noindex, COOP same-origin and COEP
require-corp; its page and thumbnail have the exact hashes above. The full
two-player WebGPU fixture on downloaded public bytes through the production
relay passes in 47.7 seconds. The collaborative preview displays the updated
title screen without console errors. No CI was dispatched.

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

Integration with `origin/dev` through `f4fd43fe` preserved the newly reduced
prototype catalog and the relocated browser helpers. Only documentation
conflicted; the fixture import paths merged cleanly. Native eighteen-case
validation, WASI and both real browser checks pass again (WebGL 26.32 s,
WebGPU 16.74 s), and all 37 relay/lobby Node cases pass. The site check/build
and 23 HTTP/admin/analytics/SEO/access tests pass with the new catalog. All
three current prototype pages were staged; ignored HTML left by the removed
prototypes was cleared before packaging. The Inside Out page hash is unchanged.

The existing Fly site was updated directly from source `0530c23f` on
2026-10-11, release 255, image `deployment-01M4NZJJYP3NF97YMDSPHKSDDF`.
Its machine reached healthy state. Anonymous game/thumbnail requests return
200 with the exact page and thumbnail hashes above; the page retains noindex,
COOP same-origin and COEP require-corp. Two separate muted browsers exercised
the downloaded published bytes with `wss://relay.tide-engine.dev` on WebGPU
in 16.5 seconds, including join, shared controls, undo/reset, completion and
phone touch. This uses the fixture's controlled frame clock and does not
claim physical NAT or human latency coverage. No CI workflow was dispatched.

## Focused-view revision validation, 2026-10-11

The per-maker camera and material/audio changes leave the campaign and match
rules unchanged. All eighteen simulation/campaign cases pass natively and
under WASI. Native debug, web debug and web release game targets build without
warnings. Inspected native captures at 1280 by 800, 900 by 800 and 390 by 844
show one focused scene, the physical containing world, nested capstone rooms
and the changed containing Mint on self-entry. The new catalog thumbnail comes
from `focus-capstone-desktop-1.png`.

Both real two-browser fixtures pass: WebGL 125.63 s and WebGPU 46.96 s,
with no runtime, browser or graphics errors. They compare independent
Workshop/Mint views, the canonical interior reached through opposite doors,
actual outside geometry before/after an occupied-shell push, and physical
versus self-recursive containing worlds. Entry/exit, partner undo/reset,
held-input recovery, first-puzzle completion/progression, real shelf
selection and phone touch all pass. An initial fixture failure compared a
720 px baseline header with an 800 px return: height-dependent backdrop grain
changed its pixels. Restoring both viewports before the unchanged strict
comparison fixed the test; the intended puzzle selection was already correct.

A bounded, device-free 330-frame native two-player run captured the actual
entry and exit mix with music disabled through the normal guide. Each 5.5 s
48 kHz stereo capture contains two isolated threshold cues. Correlation with
the upsampled source is 0.99999955, measured gain is 0.379992, global peak is
-24.675 dBFS, and neither mix clips. No audio device was opened; this verifies
the asset and mixer path rather than a human listening judgment. The source
`enter.wav` SHA-256 is
`6dd808ceb4bad8a85c56ca231312e1c6608b93e8c8382d3dffc2b9677c115dda`.

The site check has zero errors/warnings; its production build, eighteen
analytics/admin tests and five production HTTP/SEO/access tests pass. Release,
staged and production-site HTML are identical with SHA-256
`593221e6e4a00a449cbfdefb6508383f07cf35cbf7217b3e37315bd096138f14`.
The refreshed thumbnail SHA-256 is
`adb64454b74621e664244b23931bfee1bcc01c1c21290ab6a18525745cceca3d`.

Source `f8f61e9e` was merged/pushed to `dev` after local validation; `origin/dev`
had no newer changes. The existing `tide-website` Fly app was updated directly,
release 256, image `deployment-01M4P1D8MP14MTP7X08VBF1594`. Deployment checks
passed and the machine reached healthy started state. Anonymous published
game/thumbnail requests return 200 with the exact hashes above, retaining
COOP same-origin, COEP require-corp and noindex. The collaborative preview
points at the public URL. No CI workflow was dispatched.

Two separate muted browsers exercised those downloaded published bytes on
WebGPU with `wss://relay.tide-engine.dev` in 44.9 s: admission, independent
dimension cameras, canonical interior parity, moved-shell surroundings,
self-recursive entry/exit, shared undo/reset and held-input recovery,
completion/progression, shelf selection and phone touch all pass with no
runtime/browser/graphics errors. This uses the controlled frame clock;
physical phones, cross-machine NAT and human latency remain unmeasured.

## Spatial zoom and automatic-entry revision validation, 2026-10-11

Walking into a physical room now pushes first and tries its facing doorway
only when that push fails. Shift and the touch push toggle are removed.
Entry with a parcel at its doorway remains atomic; the travel sound takes
priority over the cargo push. A full entrance trail prevents further entry
but still allows a room push.

All twenty-one game fixtures pass natively and under WASI: nineteen simulation
cases and both twelve-puzzle production-command campaign replays, including
exchanged maker identities. The campaign repairs add physical backstops and
adjust latch handoffs for automatic entry. Its twenty-one partner-only latch
push probes retain the physical-shell dependency while allowing valid fallback
entry or one inward doorway parcel push. Undo, reset and deterministic replay
remain exact. Native debug, web debug and web release targets build without
warnings. These witnesses establish solvability, not shortest solutions or
measured human difficulty.

The initial spatial two-browser fixtures pass, WebGL in 84.87 s and WebGPU in 54.97 s,
with no runtime, browser or graphics errors. New pixel-area assertions require
the miniature to grow progressively during entry and shrink during exit;
the settled/initial area differs by more than twofold. Existing strict
canonical-interior, occupied-shell parent-context and recursive-camera
comparisons pass. Admission, shared undo/reset, held-input recovery,
completion/progression, shelf selection and phone touch remain covered.
An initial canonical-interior comparison caught the camera's asymptotic
settlement: tiny residual affine differences changed edge pixels. The camera
now reaches its exact target after the finite transition, and idle cell
interpolation also settles exactly. The strict comparison was retained.

Inspected bounded native captures at 1280 by 800, 900 by 800 and 390 by 844
include early/middle/late entry, exit, self-reference and nested capstone
views. Mint's wood area grows 2,651 to 8,625 to 25,936 to 33,970 pixels on
entry; exit decreases 39,775 to 21,532 to 5,878 to 2,648. The focused maker,
actual containing world and outside partner are visible in the same scene.
The catalog thumbnail comes from `spatial-capstone-desktop-2.png`, showing
Peach inside Mint with Amber outside.

The new doorway effect uses only seeded filtered noise, with no tonal body,
sweep, echo or ringing tail. The 300 ms source has RMS -25.452 dBFS and SHA-256
`657d7c91dc2cd45538453e7956af5ea608df74f5dd90f7d1c835dfee9fe4f40a`.
A bounded, device-free two-player capture with music disabled through the
guide confirms both entry and exit use this asset: correlation 0.99999992,
measured gain 0.379988 and cue peak -21.310 dBFS. Both 7.0 s, 48 kHz stereo
mixes remain unclipped. The previous published HTML contained the previous
threshold asset's exact bytes; its remaining pickup similarity plausibly
came from that asset's low sine body. These checks verify asset routing and
mixer output, not a human listening judgment.

The initial spatial site check reports zero errors/warnings; the production build, eighteen
analytics/admin tests and five production HTTP/SEO/access tests pass. Release,
staged and production-site HTML match SHA-256
`3d3d048342d10a72476529eedbcfa75f4f5ca49291851e54c507fdf994c68547`.
The packaged asset pack contains the exact new doorway WAV bytes.
Thumbnail SHA-256 is
`2e8c973ae731d9f677d89c779b54b1cee90388bfa87bbc3678121e784940b19b`.

The maker-passage follow-up builds without warnings natively and for web
debug/release. All twenty-one native fixtures remain green; the final WASI
fixtures and two-browser checks pass again, WebGL in 81.12 s and WebGPU in
54.75 s. The new stationary outside-view assertion tracks Amber's actual
body pixels: its footprint shrinks progressively by more than threefold and
its centroid moves toward the doorway across early/middle/late/settled frames.
This independently verifies the maker's passage rather than camera zoom.
Existing camera, canonical-interior, occupied-shell, recursive, shared-command,
progression, shelf and phone-viewport checks remain green.

Bounded native captures also cover physical entry/exit from both views,
recursive alias handoff with a stationary partner, fast inner steps,
immediate reversal/Undo and a rebased exit whose trail depth stays equal.
Desktop and phone-viewport passage captures were inspected. The current
settled capstone capture is byte-identical to the existing thumbnail.
A separate read-only renderer audit found no further critical blockers.

Integration with `origin/dev` through `1e969c61` was clean. Incoming compiler
changes triggered game and public-page rebuilds; the twenty-one native tests
passed again. All 64 packaged pages match their release artifacts: 46 docs
examples, fourteen public gallery games and four prototypes, retaining
Underfoot, Inside Out, CRACKED and Two Forks in the authenticated catalog.
All 225 grammar checks and the full WebGL examples smoke pass, including
the 46 docs examples, two embedded games and fourteen gallery games. The
site check has zero errors/warnings, eighteen analytics/admin tests pass,
and the final production build plus five HTTP/SEO/access checks pass.
Final release, staged and packaged Inside Out HTML has SHA-256
`c112d75234015c1ff4ea5f6fc27e00e78dc82e27d8e1b30439a94021f0d8ad33`.
Decoding the packaged asset pack confirms the exact new doorway WAV and
the absence of the previous doorway WAV bytes. No CI was dispatched.

The final merge through `origin/dev` `7c4e0835` brought unrelated CRACKED
and Underfoot graphics/game changes without Inside Out overlap. Their
web-release pages were rebuilt/restaged; all 64 playable pages and all four
prototype thumbnails match the final packaged bytes. The production docs
build and five HTTP/SEO/access checks pass again. No Inside Out game test
restart was needed for those unrelated incoming changes.

Source `9d25977b` was pushed to `dev` and deployed directly to the existing
`tide-website` Fly app, release 258, image
`deployment-01M4P4XTA0MGC534C21G0PTWBC`. Deployment and machine health checks
passed. Anonymous Inside Out HTML and thumbnail requests return 200 with
the exact final hashes above, retaining noindex, COOP same-origin and COEP
require-corp. The published asset pack contains the exact new doorway WAV
and none of the previous doorway WAV bytes. The collaborative preview
navigates to the public URL; snapshot requests fail inside the preview client.

Two separate muted browsers exercised the downloaded published bytes on
WebGPU with `wss://relay.tide-engine.dev` in 63.4 s. Progressive maker motion
from the stationary outside view, spatial entry/exit zoom, canonical interior
parity, occupied-shell context, self-recursion, admission, shared undo/reset,
held-input recovery, completion/progression, shelf selection and phone touch
all pass without runtime, browser or graphics errors. This uses a controlled
frame clock; physical phones, cross-machine NAT and human latency remain
unmeasured. No CI workflow was dispatched.

## Local dimension framing validation, 2026-10-11

The owner chose the whole current room with a one-tile outside rim. The
partner-fit helper, presence mask and camera transition trigger are removed.
Inner rooms use the fixed square scene viewport and seven-cell span used in
this revision; the outer workshop retains its scale. Simulation, puzzle layouts,
doorway passage and audio are unchanged. The README and authenticated catalog
description now describe the local framing, and the thumbnail comes from
`rim-capstone-desktop-1.png`, with Peach nested in Mint and a recursive door.

Native debug and web debug/release builds pass without warnings. All
twenty-one simulation/campaign fixtures pass natively and under WASI.
The final real two-browser checks pass, WebGL in 109.39 s and WebGPU in
66.05 s. The new regression accepts all ten partner moves and compares four
nonblank scenery patches exactly while the partner walks far away, approaches,
enters and exits. Amber's rendered pixels are absent in the outside positions
and present after entering the local room. Existing progressive camera zoom,
maker passage, canonical parity, occupied-shell context, recursion, shared
commands, progression, shelf and phone-touch assertions still pass.

The first browser attempt exposed a fixture error: four simultaneous clipped
screenshots on one page raced Chrome's temporary capture viewport. The saved
page was translated by exactly one crop origin. Capturing those crops
sequentially fixed the failure without changing crop geometry or assertions.
Desktop and 390 by 844 native captures show readable complete rooms, bounded
rims and visible doorway motion, including self-reference. A read-only camera
audit found no critical blocker. Physical-phone/NAT coverage remains separate.

Site check has zero errors/warnings; the production build, eighteen
analytics/admin tests and five HTTP/SEO/access tests pass. Release, staged
and packaged HTML match SHA-256
`67de50e12c7e96b3d0e1128b3959a8800b88199156e0ef18056fbf19bb2bbeb9`.
The staged and packaged thumbnail SHA-256 is
`48f24a67cf3d60c14bfbb7559d280ae4604a456029997532f43124fb0aae1707`.
The inline pack retains the exact current doorway WAV. No CI was dispatched.

Integration through `origin/dev` `e377d0a9` was clean. Incoming compiler,
Fetch/platform/web runtime and docs changes triggered dependency rebuilds.
The rebuilt game and twenty-one fixtures pass without warnings natively and
under WASI; both final browser checks pass again, WebGL in 106.74 s and
WebGPU in 63.92 s. All 64 public game/example pages were rebuilt and match
release, static and production-package bytes; all four prototype thumbnails
also match. Current Underfoot changes are retained. Docs check is clean,
all 226 grammar checks pass, and the production build plus five HTTP/SEO/
access checks pass again. The integrated Inside Out HTML SHA-256 is
`761bae9375434b22596e6bd05711a6d077f01cb9a034dfa31340d8f0bf507943`;
the thumbnail hash is unchanged from this revision. The inline pack still
contains the exact current doorway WAV. Source `ebbd0649` was pushed to `dev`.

That source deployed directly to the existing `tide-website` Fly app as
release 259, image `deployment-01M4P66FRKJFQ5BR7SKC3E045M`. Machine and
deployment health checks pass. Anonymous page/thumbnail requests return 200
with the exact integrated HTML and current thumbnail hashes, retaining
noindex, COOP same-origin and COEP require-corp. Decoding the public pack
confirms the current doorway WAV. The collaborative preview was navigated
to the updated public URL.

The downloaded published page passes the complete two-browser WebGPU
fixture with `wss://relay.tide-engine.dev` in 65.2 s, including partner
off-screen movement with exact stationary local framing, smooth maker/camera
doorway travel, occupied-shell context, recursion, shared commands, progression
and phone touch. No runtime/browser/graphics errors or CI dispatch occurred.
Physical phones, cross-machine NAT and human latency remain unmeasured.

## Nearby outside maker and goal icons, 2026-10-11

The owner asked to see part of a maker standing beside the current room box,
and for goal tiles to show the occupant they require. Inner framing now uses
an eleven-cell span, leaving three interior-grid tiles of actual containing
world around the five-cell room. The fixed viewport and camera remain
independent of partner movement. Parcel targets show a box icon, maker goals
show a player icon, and switches show both because either can hold a latch
open. Existing occupied colours remain. Guide, lesson and hint text follows
the icons. No simulation rules, puzzle layouts, witnesses or audio changed.

Native debug and web debug/release builds are warning-free. All twenty-one
fixtures pass natively and under WASI, including both twelve-puzzle campaign
replays. The two-browser checks pass, WebGL in 86.90 s and WebGPU in 63.06 s.
The partner regression observes a clipped nearby maker at the fixed scene
edge, zero maker pixels farther away, and a visible maker after entry. Four
nonblank scenery patches remain byte-identical through all ten accepted
partner moves. Existing progressive maker passage, camera zoom, canonical
parity, occupied-shell context, recursion, commands, progression, shelf and
phone-touch checks remain green. A final stale capstone hint was corrected;
native/web targets and all twenty-one fixtures passed again after that text
change.

Bounded native captures at 1280 by 800 and 390 by 844 show the nearby maker
partly clipped at the bottom edge, readable complete rooms and literal goal
icons. Three scenery regions are byte-identical through the settled near,
far, return, inside and outside states; the full focused scenes at near,
return and outside are identical. The refreshed thumbnail comes directly
from `wide-capstone-desktop-1.png`. Captures and pixel evidence are under
ignored `build/inside-out-captures/`.

Site check reports zero errors/warnings; the production build, eighteen
analytics/admin tests and five HTTP/SEO/access tests pass. All 64 public
game/example pages and all four prototype thumbnails match release, staged
and packaged bytes. Inside Out HTML SHA-256 is
`56defc3b4b91e9aa97082f679382707ef9adb76b779178dc3b858886d8c82f50`;
thumbnail SHA-256 is
`a41a9c7066f5810fa8dcf89ea87c9b6974716e83d47fb771c3dbeec0ec9344cc`.
Decoding the inline pack confirms the exact current doorway WAV and absence
of the previous WAV bytes. No CI was dispatched.

The clean merge through `origin/dev` `27ddca74` brought shared compiler,
asset-list/session/delta runtime changes and current Underfoot updates.
Affected native and web debug/release targets were rebuilt without warnings;
all twenty-one native/WASI fixtures pass again. Final two-browser checks pass,
WebGL in 93.56 s and WebGPU in 63.20 s. All 64 public playable pages were
rebuilt/restaged and match their packaged copies; all four thumbnails match,
including the incoming Underfoot image. Docs check remains clean, all 227
grammar checks pass, and the final production build and five HTTP/SEO/access
checks pass. Integrated Inside Out HTML SHA-256 is
`c10808dbe0d78fcf976aca136860da2bc6dbe82eacb4295a58495d9f551d0d5f`;
the current thumbnail and doorway WAV hashes above are unchanged. No CI was
dispatched.

Source `0fa243a2` was pushed to `dev` and deployed directly to the existing
Fly site as release 260, image `deployment-01M4P75RDBACPVV6P25S6HRV16`.
Deployment/machine health checks pass. Anonymous Inside Out page and thumbnail
requests return 200 with the exact integrated hashes, retaining noindex,
COOP same-origin and COEP require-corp. The public inline pack contains the
exact current doorway WAV and none of the previous WAV bytes. The collaborative
preview navigates to the updated URL; snapshot inspection still fails inside
the preview client.

The downloaded published bytes pass the complete two-player WebGPU fixture
with `wss://relay.tide-engine.dev` in 62.4 s. Nearby partner clipping and distant
off-screen movement preserve exact local framing; smooth maker/camera travel,
canonical interior parity, moved-shell context, recursion, admission, shared
commands, progression, shelf and phone touch pass without runtime/browser/
graphics errors. The probe uses a controlled frame clock. Physical phones,
cross-machine NAT and human latency remain unmeasured. No CI was dispatched.
