# Inside Out

The owner requested a public two-player cooperative puzzle prototype on
2026-10-11, inspired by their favourite spatial and discovery puzzle games.
They accepted the Inside Out idea: one player moves around inside a room that
the other player can move as a box. The owner selected online play from
separate browsers and a warm miniature-workshop mood. The prototype's
particular puzzle rules, levels and presentation are agent design choices
within that request, not engine or language requirements.

## Game and campaign contract

`prototypes/inside-out/game.tide` contains the production movement rules and
two-seat match. `levels.tide` owns twelve hand-built layouts and their names,
lessons and three-tier hints. The workshop is a 7-by-7 grid and each room has
a 5-by-5 interior. Production limits are four rooms and eight crates; authored
levels use at most three rooms and two crates. Initial state, immutable
layouts, bounded shared undo history, completion flags and player seats are
deterministic match state. Room movement preserves interior coordinates.

Each facing door is at its room edge's midpoint. Cardinally adjacent boxes
with compatible door masks connect their interiors. Both the small avatar
and cargo can cross. Cargo leaving an unmatched door exports to the adjacent
empty workshop cell. The small avatar cannot enter the workshop this way.
A workshop crate pushed into a room through a compatible door imports at
that door's interior edge cell. A second room lets the small partner approach
the imported crate from behind and push it farther inward. No crate or room
push chains exist. Walls, boxes, crates and avatars block their corresponding
destination cells.

A latched room moves only while its inner Switch tile has the small avatar
or a crate on it. It has no timing requirement. Hatch exchanges which seat
owns the small and large avatars when the small avatar stands on the hatch
and the large avatar stands cardinally beside the occupied room box.
Ownership changes alone; crate types, geometry and positions do not.
Role exchange is an optional social tool, not an artificial completion gate.

Completion requires every authored room dock, every inner and outer crate
target and both avatars' Exit tiles. Targets can be emptied while solving.
Both avatars start away from Exit, so actions at both scales are necessary
for every level. Shared undo restores up to 128 successful movement/role
exchange actions. Reset restores the authored initial puzzle. Solved puzzles
wait for an explicit Next; undo remains available. Every level is directly
selectable from the shelf for prototype evaluation.

There are exactly two real participants and no single-player bot substitute.
Either absent seat disables gameplay actions. Existing Rooms, Session,
typed command sender/sequence validation and host migration are used without
new engine or runtime APIs. Stale board serials cannot carry an action across
reset, selection, undo, role exchange or player availability changes.

## Campaign progression

1. First delivery separates movement at the two scales: a room dock and an
   inner crate target must both be filled before the partners exit.
2. Good neighbours introduces connecting room doors.
3. Through the doorway introduces parcel transfer between rooms.
4. A helpful doorstop introduces a persistent crate-held movement switch.
5. Out for delivery exports a parcel and makes the large partner deliver it.
6. Return post imports a parcel at a doorway, then assembles the approach
   room needed for the small partner to push it inward.
7. The sorting line builds a three-room straight delivery route.
8. Around the corner turns a parcel south inside the relay room.
9. Pass the key uses the same parcel to unlock two latched rooms in sequence,
   then parks the parcel after both boxes reach their docks.
10. Passing place requires dispatch while two rooms are connected, followed
    by a Sokoban room-order exchange through a passing bay.
11. The way home uses a latched room as a ferry, first held by the parcel and
    later by the returning small partner.
12. The whole workshop extends the ferry problem with a second outgoing
    parcel: temporarily unfill the incoming parcel's target, export the
    outgoing parcel, restore the target, then return the small partner home.

Every hint starts with a relationship or planning question, then names a
useful arrangement, then provides the decisive sequence. No hint executes
actions or changes the puzzle. Puzzle witnesses are not embedded in the
public playable game.

## Presentation and controls

At the owner's follow-up request for 3D, the workshop and active room use an
isometric 2.5D presentation with the same fixed orientation at both scales.
`iso.tide` projects grid positions into screen space and builds shaded faces
with Draw.Mesh; `paint.tide` draws raised room boxes with live five-by-five
miniatures in their open tops, wooden parcel blocks and minifigure makers.
The larger interior uses high rear walls and low foreground cutaway walls.
Rendering elevation, shadows and depth ordering do not change the grid rules
or the simulation. Room tabs on wider screens permit inspection.
Filled brass circles identify crate targets, coloured numbered outlines
identify room docks, leaves identify avatar exits, curved arrows identify
hatches and small locks identify latched rooms. Live miniature switches show
their occupancy. User controls and room inspection belong to `ui.tide`.

WASD/arrows move, E swaps scales, Z/Backspace undoes, R resets, H toggles hints
and Enter continues after completion. The diagonal touch pad and wide-screen
board compasses match the fixed projection: W/Up is upper-right, D/Right is
lower-right, S/Down is lower-left and A/Left is upper-left. Serial changes
(undo, reset, selection, scale exchange and participant changes) clear the
local repeat state and require release of a held direction before a new move
can be sent. Screen buttons offer the same actions and a shelf for all
puzzles. Hint cards wrap to their available width and size their height from
the wrapped line count so the tier's full text and buttons stay separated.
Original audio in
`sound.tide` and `sfx/` is reproducible with `sfx/make_sounds.py`; sound/music
toggles are local and volume uses Rooms settings. No audio or presentation
state enters the match hash.

The prototype joins the admin catalog while its staged page is publicly
accessible at `/prototypes/inside-out/`. Invited players need no admin session.
The existing opt-in `TIDE_PRIVATE_PROTOTYPES` and `private_games` staging are
used; site integration is described in `notes/private-prototypes.md` and
`notes/docs-site.md`.

## Solvability and validation

`test/solutions.h` contains one manually derived successful witness for every
authored layout. Uppercase directions act as seat 0; lowercase directions
act as seat 1. `test/campaign.c` replays every move through the actual generated
Move command handler in a world with two joined players. It asserts every
individual move succeeds, both seats and both scales act, and production
completion becomes true. It then undoes the final action, restores the exact
initial puzzle with Reset and replays to the same completed puzzle bytes.
A second test exchanges roles on Return Post and replays with seat ownership
reversed. No mirror rule implementation or level solver supplies this proof;
the witnesses establish existence, without claiming minimum move counts.

The simulation fixtures separately cover authorization, malformed and stale
actions, room contents following a moved box, Switch occupancy, aligned
doorways and entry pushes, cargo changing scale, blocked destinations and
non-chain pushes, hatch exchange with undo, every completion condition,
bounded history, snapshots, packed state and deterministic replay.

On 2026-10-11 the scoped native and web debug simulation suites pass all
fourteen cases, including the campaign witnesses and exchanged-role replay.
The twelve witnesses take 8, 12, 10, 9, 16, 18, 21, 25, 28, 33, 37 and 59
successful production moves respectively. Every witness also passes full
Reset/replay and final-move undo. The move counts describe these witnesses,
not measured player duration or optimal solutions. Browser and presentation
validation is recorded below.

`test/browser.mjs` runs two visible Chrome/Edge players through a local relay
and actual unordered, unreliable WebRTC data channels. It uses The Last Light's
DevTools-pipe, bounded browser-process cleanup and serialized frame-control
helpers, unmodified: they're beside it in `test/` since 2026-10-11, when The
Last Light was deleted (`notes/private-prototypes.md`); their own unit tests
went with that game. Both rendered players advance
one controlled 30 Hz frame together and finish their GPU work before the next
round. Readiness requires fresh match packets and verified ticks; browser,
runtime and graphics errors fail the check. Static rendered move-count/title
crops check shared changes without a game-only debug export or wasm-memory
inspection. Desktop and phone screenshots remain available for visual review.

The final isometric fixture passes on WebGL and WebGPU in web debug and web
release with Node 24.21 and muted Chrome on this Windows machine (2026-10-11).
It verifies waiting for the second player, movement from both keyboard roles,
partner undo, shared reset, held-key suppression across partner reset/undo,
release/new-press recovery, the actual eight-move first-puzzle witness,
synchronized Next progression and a
real phone touch move received by the partner. Both canvases fill their phone
viewports; desktop completion and phone captures were visually inspected.
The CTest entries are `inside_out_play_webgl` and `inside_out_play_webgpu`,
with 180-second guards and serial scheduling. The fixture controls match time
to separate software rendering waits from session deadlines; real-time
transport behavior remains the engine's session/browser-room coverage.
After merging `origin/dev` at `ebeea92c`, the final debug CTests take 70.33
seconds on WebGL and 14.21 seconds on WebGPU, within their unchanged guards.
Native and WASI simulation checks again pass all fourteen cases after the
incoming confirmed-frame ABI/compiler changes. Every release docs/example,
sample and prototype page was rebuilt and staged; docs check/build, five
production HTTP/SEO checks and the complete WebGL example/gallery smoke pass.
Static captures reuse completed checkpoints;
gameplay key edges receive one delivered frame, while title/modal typing keeps
its two-frame settling. Major checkpoints require six fresh verified ticks;
individual witness moves use two and the completed witness uses six. The
fixture verifies the resulting completion and progression rather than spending
extra frames at every unobserved intermediate state.

Native two-process captures checked the isometric first and final workshops,
the puzzle shelf, guide and third-tier hint on 1280-by-800 desktop and
390-by-844 phone layouts. Cutaway walls leave destinations visible; the live
miniatures include switch occupancy. The direction pad and both desktop
compasses follow the fixed projection, and hints name the corresponding grid
keys. Hint cards grow to fit their wrapped text. The catalog thumbnail is a
capture of the actual first workshop. The game-specific HTML shell preserves
the embedded pack and runtime while supplying the game's title, warm loading
background and touch canvas behavior.
An additional two-process Return Post capture checks a scale exchange: each
player keeps their amber/teal identity colour, and their role label and active
interior ownership change to match the exchanged controls.

All eight original source WAVs decode as mono 24 kHz PCM. Their maximum sample
magnitudes range from 0.144 to 0.319 of full scale. A bounded native two-player
mixed-audio capture is non-silent and peaks at 0.063 without clipping. These
checks inspect PCM and mixer output without opening an audio device; subjective
listening is still part of playtesting.

No human pair has yet evaluated whether the late
puzzles are satisfying or whether discovery and discussion last long enough;
that is the purpose of this public prototype. Native platforms beyond this
Windows machine and physical phone hardware remain separate coverage.

## Public deployment (2026-10-11)

The feature was committed, merged with `origin/dev` and pushed directly to
`dev` using the owner's procedure. Incoming compiler, standard-host frame ABI
and docs changes merged cleanly. The affected native/web game targets and all
site game/example staging targets were rebuilt; the scoped game, browser and
site checks above passed again before publication.

The existing `tide-website` Fly app was deployed with
`fly deploy --remote-only --ha=false`. Its rolling update completed with a
healthy machine (version 252), using image
`deployment-01M4NW4CRPNFH1X3P4MRYWDY2B`. No GitHub workflow was dispatched.
Anonymous requests to `https://tide-engine.dev/prototypes/inside-out/` and its
thumbnail both return 200. The served HTML matches the staged/built page
byte-for-byte (SHA256
`57a970dbf68a347b5502dca6eec59db4249c88b691a0881ee5e077ac2d39f21a`),
and the thumbnail bytes match the checked-in capture. The page retains
`noindex`, same-origin opener isolation and require-corp embedder isolation.

The browser fixture accepts optional `TIDE_INSIDE_OUT_RELAY` for a service
probe, retaining its local relay by default. A final WebGPU run used the
anonymously downloaded deployed HTML and `wss://relay.tide-engine.dev`.
Two separate muted browser windows connected through the production relay
and passed keyboard actions from both roles, shared undo/reset, held-input
recovery, the first-puzzle solution, progression and phone touch in 17.1
seconds (220/210 completed frames). The fixture serves those exact downloaded
bytes locally and controls match time, as in its standard checks; this probes
production signalling and peer traffic without claiming physical-device/NAT
or real-time latency coverage. Captures are in the ignored
`build/inside-out-captures/production-relay/` directory.

While saving this evidence, `dev` advanced with the independent JSON/text
language feature. That merge was clean. The affected game targets were rebuilt
again, all fourteen native and WASI cases passed, and the updated web debug
WebGPU co-op fixture passed in 13.25 seconds. The published artifact remains
the identical bytes verified above; these later changes are repository
integration and test/documentation evidence.

The final sync also merged the independent owned-asset benchmark evidence and
the held-input prediction-reuse fix (`822edee7`). The latter touches the
session dependency: both game targets were rebuilt, all fourteen native and
WASI cases passed, and the two-browser web debug WebGPU fixture passed in
12.73 seconds. No prototype conflicts or rule changes were needed.
