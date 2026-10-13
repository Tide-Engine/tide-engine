# NIGHT ROUTE

The owner requested a delivery game in a small town at night, brainstormed
first and then approved the proposed overhead neighbourhood and four parcel
rules on 2026-10-13. This slice tests cargo composition and delivery order;
it changes no engine, compiler, language or runtime contract.

## Implemented slice

`prototypes/night-route/game.tide` owns the deterministic match. One player,
60 Hz, a three-minute shift, 12 junctions and 20 road segments: a connected
grid plus three diagonal alleys. There are four numbered delivery addresses,
one southwest depot, a lit high street, a dark central cut-through and an
intermittently lit riverside road. Movement is continuous, constrained to
the street capsules. Walls count as standing, even with a movement key held.

Four offered parcels, at most three carried:

| Parcel | Violation | Strain per second |
| --- | --- | ---: |
| Mothbox | Outside the actual lamplight footprints | 18 |
| Restless | Actual travel speed below 0.6 m/s | 24 |
| Northbound | Facing outside the north cone, y < 0.85 | 15 |
| Echo | On a revisited street or reversing along the same street | 18 |

Every obeyed rule removes 8 strain/s, clamped to 0..100. At 100 the parcel
spoils once, is removed from carried cargo, and costs $10. There is no chase,
combat, hidden hazard or abrupt instant failure. Shift fixes facing north;
sideways/backwards pace becomes 2.4 m/s instead of 4.2. Forward northward
travel retains the normal pace. Normal movement sets facing automatically.

Echo records route history for the entire trip, including travel before its
delivery. Intersections are neutral; leaving them onto an old segment and
same-segment U-turns violate its rule. History clears on opening the depot's
next manifest. Light/collision/Echo and their map display share the same
graph. Alley lights are absent; ordinary lamp radii are exactly 2.65 m and
the depot has its own light. The painted halos show the gameplay footprint.

Handing over within 1.5 m of an address removes the rule immediately, before
that tick's strain calculation. Held E can make quick handoffs, and cannot
pay twice. Base pay is $26..50. A completely delivered bundle pays an extra
35% for two or 70% for three, rounded down independently per parcel; base pay
is immediate and the bonus is held until the final handoff. One loss forfeits
the whole bonus, preventing profitable depot loading/abandonment without
actually delivering the combination. Returning with live cargo abandons it
under the same loss rule; returning empty has no penalty.

The clock starts on first departure. Later depot planning and the Rules
guide consume shift time. Dawn spoils remaining cargo once and opens the
debrief: earnings, delivered/spoiled counts, trips, bonus pay, distance and
violation seconds by behaviour. Retry reconstructs the same seed's jobs;
Next Night advances it. Destinations permute deterministically by seed and
trip, while payouts vary independently. Initial job arrangements repeat
every 20 seeds; this is bounded prototype variety, not procedural town
generation. There are fourteen legal cargo subsets. Balance and whether
humans find these replayable remain playtest questions.

`show.tide` draws the sleeping quarter, roofs/windows, wet streets, river,
light pools, numbered stops, parcel marks, courier facing and purple route
history. Cards show per-parcel rules, destination, payout, strain and status.
Keyboard, gamepad and compact touch controls are provided. No external assets
or audio are needed. State/progress is not saved across sessions.

The GUI claims the keyboard to prevent arrows/Enter navigating HUD focus.
The owning view forwards WASD/arrows, Shift and E through local `TouchPad`
into Sample, along with the owning view's gamepad controls. The handoff button
holds its request for 0.12 seconds so fast rendering cannot lose it between
ticks. Reading keyboard directly in Sample under that claim consumed
all movement in the first scripted playthrough. Tests now exercise the host's
actual GUI -> local pad -> Sample -> match boundary. This workaround changes
only the game and introduces one frame of local control forwarding.

Depart/Shift/ReturnToDepot commands validate the connection's player, manifest
serial, payload bounds, phase and depot proximity. Shift reset preserves
player/command state, increments the serial and resets all shift counters.
Simulation, cargo, route history and results participate in snapshots/hashes
and packed world bytes. Generated code uses existing runtime functions only.

## Build and staging

Opt-in through `TIDE_PRIVATE_PROTOTYPES`. `night_route` builds with STATS;
`night_route_page` stages the self-contained page at
`docs/static/prototypes/night-route/index.html`, collected by `private_games`.
The authenticated catalog and public-root/asset gates include `night-route`.
The checked-in 960x540 thumbnail comes from an actual native playthrough.
The C host stays behind `test/tide.packages`; it is not CLI game code.

## Validation

The simulation and interface suite covers manifest bounds/stale commands,
clock lifetime, all four rules and recovery, collisions, U-turn/reentry,
delivery releasing constraints, full-bundle payouts and bonus forfeiture,
depot restock/history/abandonment, dawn/retry, all sixteen parcel/address
pairings, a real three-rule delivery route, snapshot/pack/replay, desktop
HUD/keyboard actions and the touch pad/north toggle/handoff.

Windows debug game/test builds pass without warnings. All fourteen cases pass
natively and under WASI (Node 24.16.0 downloaded inside this checkout).
Native scripted captures verify a two-parcel loop: Mothbox is delivered at
the chapel, Echo at the diner, neither spoils, and $94 includes the earned
bundle bonus. Captures at 1280x800, 960x540 and 390x844 were inspected; the
short desktop layout was corrected to keep departure visible and the Rules
button leaves the bottom-right STATS corner clear.

The T3 collaborative browser rendered the page on WebGL and WebGPU with
`Tide.checkGL` enabled, and keyboard departure, northward travel and handoff
ran on both without console/runtime/GPU errors. The browser was held hidden
by the preview host, with a non-configurable `document.hidden` override;
ordinary hidden pages do not render. An untracked fixture therefore replaced
only the runtime's hidden-page checks and used the existing frame-control
helper to advance the same compiled game and capture its drawing. WebGL's
capture shows the chapel handoff, $38 and the released Mothbox rule. T3's
ordinary snapshot operation failed; native full-resolution captures provided
layout QA. The final user-facing page contains none of these test overrides.

Docs Svelte check has zero errors/warnings; the production build and all
nineteen analytics/admin/production HTTP checks pass, including the new
catalog id and outside-root/discovery gates. No engine/compiler/platform
source changed, so validation ran the prototype targets/suite rather than
the complete engine suites. No online/co-op play, real phone, other operating system,
Android emulator, performance comparison or human replayability study is
claimed. CI and deployment are not part of this request.
