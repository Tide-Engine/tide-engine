# HOT SWAP

The owner requested a solo/cooperative possession heist on 2026-10-13 and
approved the overhead view, playful industrial caper and equal solo/duo
support. `prototypes/hot-swap/` implements one connected warehouse targeting
a 10–15 minute discovery run. That duration and whether cooperation is fun
need human playtesting; knowing the route makes a run substantially shorter.
These are game rules, not new language or engine requirements. No engine,
compiler, platform, runtime or package source was changed.

## The match

`game.tide` keeps a fixed-size Heist singleton: fifteen machines, five movable
pallets, two guards, twelve expiring foam patches, the physical storm case,
two seats, three latched shutters and objective/security state. Two owned
Pilot entities sample continuous input into per-seat intentions; TickHeist
resolves seats in numerical order. This entire state belongs to the match,
including cargo, possession, guard navigation, captures, revisions and the
completion summary. Camera framing, UI, sound and spark trails are local.

Typed Jump, BeginHeist and Restart commands validate the sender and heist
revision. Jump additionally checks indices, occupancy, power and reach. Mobile
hops need a clear path within 7.5 metres; fixed cameras/controllers sharing a
cable circuit permit distant hops. A machine has at most one creature, including
late joins when the other player already borrowed the second starting cleaner.
Leaving releases occupancy and its owned input entity. There is no free-moving
spark or possession-energy meter. All vacated machines stay where parked, with
cargo retained, and mobile bodies obstruct movement.

The 58-by-36-metre warehouse has three connected routes rather than separate
levels. A cleaner can use the narrow upper ducts; a forklift carries cargo and
moves pallets; fixed hosts scout and operate the doors. Either player can copy
the shipping manifest: a mobile body reads the terminal with E, or Records eye
faces right and holds E for three seconds. Dropping a pallet on the amber power
plate, or using a cleaner at the secure service bypass, supplies vault power.
Manifest plus power releases the case permanently.

Taking the case starts a security search and closes both main shutters. Bring
it to the green receiving/getaway area by cleaner duct, reopened loading
shutter or reversed shipping conveyor. Controllers toggle their own shutter
with E and the shared conveyor with Space. The conveyor transports a dropped
case west across the otherwise closed gates. Escape classification records the
first receiving-fence crossing, rather than changing as the case moves inside
the bay. Completion freezes the mission and reports time, swaps, captures and
route; another heist resets objectives and cargo while keeping both players.

Solo shutter escape is specifically verified: leave the case on its forklift,
jump through the vault camera to a cleaner, cross the upper duct to the main
controller, reopen the secure shutter and return. Later park the carrier clear
of pallets, use the receiving circuit to open its shutter and resume carrying.
Together, one player can prepare those doors or distract while the other hauls.
Neither version requires holding a switch or an AI partner.

## Machines and security

- Cleaners tow the case more slowly and spray eighteen-second foam patches.
  Guards entering foam slip/stun. The cleaner can use both upper ducts.
- Forklifts lift/drop pallets or the case with E. Space sounds a guard-attracting
  horn and raises nearby cleaners for fifteen seconds. A raised cleaner can
  cross the secure fence's service catwalk at y=13..15. Expiring elevation while
  crossing resolves onto the nearer bank so it cannot strand a body in a wall.
- Cameras aim with movement input, scan the manifest and flash visible nearby
  guards, stunning them and directing their investigation away from the carrier.
- Controllers latch shutters and reverse the belt. Their cable lines distinguish
  long hops from ordinary nearby possession.

Guards patrol, investigate noise, build suspicion through a visible cone and
chase. Sight checks racks, shutters and pallets. A bounded 2,088-cell grid
search finds their next step around changed routes; collision preserves the
warehouse bounds. Getting caught reboots the body for eight seconds, ejects
the creature to the nearest free powered machine and gives five seconds of
recovery protection. Cargo drops where it was; unlocked objectives and changed
routes persist. There is no hard timer, lives counter or mission-loss reset.

## Presentation and input

`ui.tide` draws an overhead warehouse with floor markings, rack shadows, cable
routes, machine silhouettes, guard cones, foam, cargo, possession rings and
spark trails. It uses the existing 2D Draw API, with a triangle mesh for each
occluded guard cone. The view follows the current body; M fits the complete
warehouse and permits clicking reachable machines on the map. The HUD includes
body-specific actions, the next objective/hint, suspicion, elapsed time, swaps,
a minimap, friend identity and the room code. Title/host/join and room/volume
menus use Rooms; H supplies controls, V mutes and Escape opens the room menu.
Desktop keyboard/mouse is the primary playtest target; there is no touch pad.
Gamepad inputs are wired but no physical gamepad was tried.

The HUD claims keyboard navigation so Tab/Space/arrows cannot activate its
buttons. Such a claim also hides keys from Sample, so the HUD explicitly copies
held movement/E/Space into local Desk, which Sample reads. This adds a frame of
keyboard sampling latency and keeps the network's normal bool-edge handling.
Actual browser movement tests guard this boundary. New game requests must be
read in RoomRequests after Rooms.Pages, in the same frame they are raised;
Rooms.Begin clears them next frame. The menu restart was checked visually and
is in the browser fixture.

`sound.tide` plays seven synthetic event cues and a quiet factory loop, all
made from arithmetic by `sfx/make_sounds.py` with Python's standard library.
No borrowed recording or external asset is used. The optional page query
`?silent` keeps its AudioContext suspended for unattended previews; normal
play uses the actual context and the V key/room volume setting. Sounds were
captured and checked as nonzero, but never heard during these muted agent runs.

## Build, catalog and tests

`hot_swap`, `hot_swap_tests` and web `hot_swap_page` are opt-in through
TIDE_PRIVATE_PROTOTYPES. The standalone target includes STATS. Its self-contained
page stages under `docs/static/prototypes/hot-swap/`, collected by private_games;
the checked-in 960-by-540 thumbnail is an actual two-process gameplay capture.
The server-only catalog and its three public-root/outside-root gates register
hot-swap; admin and HTTP tests include its anonymous page/assets, encoded roots,
stale copies and discovery exclusion. Source, test and CLI package discovery
are isolated by `test/tide.packages`. README documents controls and routes.

Fourteen native/WASI tests use the real command decoder and sampled input:
unauthorized/stale/out-of-range/occupied/offline possession, cable versus sight,
late join/leave/rejoin, latched shutters, foam expiry, camera aiming/scanning and
flash, pallet versus bypass power, capture recovery, an entire solo heist from
the authored starting bay through ducts, partner-assisted shutters, conveyor
escape, solo return-to-cargo through both shutters, the catwalk/expiry recovery,
restart and stale revisions, and snapshot/packing/replay equivalence. Threat
and geometry tests are separate: route tests stun the guards but keep the
actual warehouse geometry. Three independently restored worlds agree on every
one of 600 two-player input ticks. Exact replay hash 12f0f9acbfa1453c is pinned
on native and WASI, debug and web release.

`hot_swap_play_webgl` and `hot_swap_play_webgpu` run the actual page in muted
Chrome, using Inside Out's existing frame/process/DevTools helpers and a local
relay. They exercise solo movement, keyboard possession, help/map, room restart,
two players joining through real unordered/unreliable WebRTC, each player's
movement visible to the other, and possession without taking the partner's
body. They read public netcode reports and rendered labels/minimap, never game
state through a test export. Tide.checkGL and browser/runtime errors fail them.

## Validation, 2026-10-13

On this Windows machine with Node 24.19.0 from the portable Codex runtime:

- Native debug game and tests build without warnings; fourteen cases pass.
- Web debug and web release compile without warnings; fourteen cases pass in
  WASI with the same pinned replay hash. Both browser backends pass the solo/
  two-player fixture. The local handoff stages the optimized web release page.
- Windowless native pictures at 1280x800 and 960x540 were inspected: title,
  movement, possession, help, map, room menu and restart, and both players of
  a synchronized --players run. Browser captures show both player identities,
  help/map and cooperative play on WebGL and WebGPU.
- A three-second native audio capture contains 144,000 stereo frames, peak
  4,219 in PCM16 and 271,404 nonzero channel samples, without an output device.
- Docs check has zero errors/warnings; production build and eighteen analytics/
  admin checks pass. The production prototype HTTP check and three SEO checks
  pass. Release, staged and packaged page bytes agree; the packaged thumbnail
  is the actual checked-in gameplay image.

Not run: full engine/compiler suites (game-only scope), other operating systems,
Android, sanitizers, real phones/gamepads, two physical PCs/Internet latency,
real-relay host migration, or a human fun/duration playtest. No CI or deployment
was dispatched. The public website gains the page when its normal manually
requested Docs workflow next deploys; the handoff is a running localhost page.
