# Inside Out

The owner requested a public online co-op puzzle prototype for two friends
in separate browsers on 2026-10-11. They chose a warm miniature workshop.
The first published build assigned one player to the outer workshop and
one to its room interiors, with twelve introductory puzzles. A follow-up
briefly changed its presentation to isometric 2.5D.

After playing that version, the owner requested polished 2D, harder puzzles
based on planning and interdependent moves, a shared world without fixed
roles, free dimension traversal for both makers, and recursion. They chose
walking into doorways to enter and holding Shift while moving to push rooms
in that revision.
They subsequently rejected the simultaneous room overview: each player should
see their current dimension, with the containing world visible beyond it,
as in Patrick's Parabox. They also requested improved graphics and a doorway
sound that feels like entering a box instead of collecting a coin.
They then requested a continuous spatial zoom with visible outside makers,
automatic room pushing with doorway entry when blocked, and an unmistakably
unpitched entrance sound. The current prototype implements that follow-up.
Its particular layouts and puzzle rules are game design choices within the
prototype request, not new
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

Walking into a physical shell first tries to push it. If that push cannot
happen, the same direction tries its enabled facing doorway and enters at
the opposite edge cell. Walls, occupied cells and a closed latch can stop
the push. A failed push uses a separate candidate, so entry starts from
unchanged state. No modifier or force-entry command remains. Fixed infinity
doorways are entered directly; parcels are always pushed normally. Each
action moves one object, with no pulling or push chains.
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

`paint.tide` draws one top-down scene from the local maker's dimension.
The workshop is the initial view. Room interiors and their miniatures use
identical drawing geometry: an inner tile is 0.16 of its containing tile.
Entry starts at the miniature's actual location and interpolates scale and
translation over 0.55 seconds; exit starts from the inverse transform.
There is no scene fade. The camera widens and shifts when needed to keep a
maker in the immediate containing world visible. Up to three real ancestor
contexts surround the current room, in the same scene. That outside
uses the maker's latest entrance: shell entries resolve their current parent
and location, and fixed references use their recorded parent and anchor.
A self-reference can therefore put the same canonical room around itself;
two makers in the same room can have different surrounding contexts if they
used different entrances. Room shells retain live nested miniature interiors.
Each context draws at most 48 room instances and three descendant levels.
Below 0.65 pixels per inner tile, preview details collapse to coloured room
or infinity markers. Drawing never duplicates simulation content. The
simultaneous enlarged room cards and connection-path overview are removed.

The visual treatment uses coloured room frames, richer wood surfaces and
inset shadows, distinct brass targets, leaf exits, doorway marks and latch
feedback. Camera transitions, interpolation, shadows, particles and failed
action feedback are local state only. Reduced motion bypasses transitions,
movement interpolation and decorative motion.

`ui.tide` provides WASD/arrows to walk and push, Z/Backspace shared undo,
R reset, H layered hints and Enter to continue. Directions match the screen.
Touch uses the same directional buttons. After a serial
change, held directions must be released before sending a fresh action.
Hints are optional and wrap/grow to fit their text. Default level descriptions
state objectives without exposing the decisive solving sequence.

Original audio in `sound.tide` and `sfx/` is reproducible with
`sfx/make_sounds.py`. Entrance, movement, pushes, failed actions, undo, reset
and completion have soft cues. Sound/music/reduced-motion options are local;
Rooms settings control volume. No presentation or audio enters the match hash.

The doorway cue uses `enter.wav`, now a 300 ms seeded-noise cardboard friction
and rounded air puff. Its previous low sine bodies have been removed: there
are no oscillators, resonant ringing, note sweeps or musical tails. Entry,
exit and recursive depth changes select kind 2 before the push cue, including
travel that pushes a parcel from the doorway. The clip is mono 24 kHz PCM16,
7,200 frames, with silent endpoints and no clipped samples. Its peak is
-12.906 dBFS before the 0.38 playback gain. The other
seven WAV files, including music, remain byte-identical after regeneration;
the obsolete `swap.wav` has been removed.

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
| Reserved space | 28 |
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
command must leave the physical shells unchanged. The attempted push can
legitimately fall back to doorway entry, including pushing a parcel at that
door. Those effects are separately guarded by the core fixtures. These probes establish
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
frame helpers, now beside it in `test/`. They moved unmodified from The Last
Light when that prototype was removed on 2026-10-11; their original unit tests
left with that game and their history remains in git. Checkpoints require
fresh verified ticks and packet exchange;
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

Both final two-browser fixtures pass, WebGL in 84.87 s and WebGPU in 54.97 s,
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

The site check reports zero errors/warnings; the production build, eighteen
analytics/admin tests and five production HTTP/SEO/access tests pass. Release,
staged and production-site HTML match SHA-256
`3d3d048342d10a72476529eedbcfa75f4f5ca49291851e54c507fdf994c68547`.
The packaged asset pack contains the exact new doorway WAV bytes.
Thumbnail SHA-256 is
`2e8c973ae731d9f677d89c779b54b1cee90388bfa87bbc3678121e784940b19b`.
