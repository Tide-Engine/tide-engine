# DEADWEIGHT

The owner requested a two-worker cooperative salvage prototype on 2026-10-13
and approved the elevated 3D pumping-station proposal and its three jobs before
implementation. One hall, satisfying physical coordination, recoverable mistakes
and reliable controls are the scope. Game design choices below are that approved
prototype direction and its implementation; no language/runtime design changed.

## Match and physics

`prototypes/deadweight/game.tide` owns the site, two workers, six winches, cargo,
the counterweight mechanism and completion. Box3D supplies rigid bodies and
collisions through its existing package API. No engine, compiler or package
source is changed. Cargo's position/rotation and all cable lengths, anchors,
attachments, workers, seat ownership, completion and recovery are match state.

- Conveyor: a nine-metre, 2.4-tonne section starts slightly skewed at a narrow
  doorway. Its actual body spans the four-metre trench and supports walking.
  Side walks offer recovery and alternative routes; making a bridge is a
  recorded achievement, not a separate invisible switch required for delivery.
- Turbine: a 2.2-tonne drum is behind a vertically guided shutter. The 1.8-tonne
  ballast and shutter are both dynamic bodies, connected by a tension-only
  pulley constraint. Lowering the ballast raises the shutter. Delivery of the
  drum precedes recovery of the ballast to its rack; both are required to finish.
  A guided shutter replaces the proposed hinged service ramp in this first
  build, retaining the physical counterweight cooperation.
- Vessel: a six-metre, 3.2-tonne vessel starts upright. A roof hoist and floor
  capstan pull its top sockets. It must tilt under the four-metre lintel. The
  hoist supports its nose while the floor line pulls it over the sled's ramp;
  pulling without that support can jam it against the floor and beam.

Tethers run before `PhysicsStages.Step`, after worker controls and the pulley.
Each applies a capped spring/damping impulse at its socket, including the cross
product angular impulse. Effective mass includes rotated box inertia; later
cables see earlier accumulated impulses. Slack applies no force. Reel/payout
changes rest length at 1.8 m/s, clamped to 0.5–45 m. Releasing controls holds
length; it does not remove inertia from cargo moving towards an anchor. The
solver uses 60 Hz ticks, eight Box3D substeps and bounded forces. It needs no
new runtime function or exposed joint API.

Cables are straight and ignore collision/wrapping. Cargo has box envelopes,
including faceted cylindrical visuals. The pulley uses a fixed cable-length
sum and vertical guide rails, not a new Box3D joint. No cable breaking,
destruction, currency, upgrades, extra sites or time pressure is implemented.

Workers are upright dynamic capsules with camera-aligned movement, cargo point
velocity support, and automatic steps onto low walkable cargo decks. Impacts
above 2.5 m/s cause a short tumble. Falls recover to the last grounded ordinary
floor position. Lost main cargo returns home, with attached cable lengths reset
to that pose. Recoveries use Snap; mistakes do not cost the job or completion.

Full oriented-box bounds must be inside the sled, its bottom near the deck,
and linear/angular speeds low for 1.1 seconds before delivery. The completion
bit persists across job resets. Job changes rebuild tagged site entities while
retaining worker identities and roster. Commands accept only either current
crew member and job indices 0–2.

## Controls, ownership and presentation

The Rooms package provides Play/Host/Join, menu, settings and host migration.
One player owns both workers, selected with Tab/a Swap button, or controls both
with the shared-keyboard option. A second player owns B. The remaining player
owns both after a departure. Ownership is assigned by a pre-control system
from the roster: first join can happen in the tick that queues worker spawns,
when a direct World.Set on those queued entities would fail.

Controls are documented in `prototypes/deadweight/README.md`. B uses Right
Shift for interaction, keeping Enter available for GUI activation. Keyboard
and gamepad are supported; this build has no touch movement UI. Workers carry
winch radios, so they can walk while controlling their assigned line. Switching
consoles leaves the old line attached and braked; a partner's claimed console
is excluded. Unhook/attach uses the same nearest, free, reachable socket search
as the highlight, with a bounded vertical reach. There is no hidden retargeting.

`paint.tide` builds the procedural industrial kit, faceted loads, workers and
segmented slack cables. An orthographic elevated camera keeps a fixed basis,
with scroll zoom. A 1024-square depth map supplies shadows on both GPU backends;
geometry is batched into cube/round instance lists. `ui.tide` draws briefs,
per-worker cable length/tension, job selection and completion. `sound.tide`
uses local looping winch motors, deduplicated local impact handlers and a
completion cue. The three WAV files are original arithmetic synthesis from
`sfx/make_sounds.py`; room volume controls them. No audio enters match state.

## Build, staging and verification

The folder is opt-in under `TIDE_PRIVATE_PROTOTYPES`. `deadweight` uses STATS;
`deadweight_page` stages self-contained `/prototypes/deadweight/index.html`,
collected by `private_games`. The server-only admin card, three public/id gates
and both prototype HTTP test id lists include `deadweight`. The checked-in
960 × 540 thumbnail is a capture of the actual conveyor bridge. Test hosts
stay under `test/tide.packages`, outside CLI game discovery.

Simulation tests solve all three jobs using sampled reel/payout controls,
without moving cargo into the goal. Their final world hashes are pinned and
must agree on native/WASI: `4ed5916f3c6a6094`, `a1fa0ec602478a72`,
`2d739a1276e8e402`. Jobs two and three start through the actual selection
command, including teardown of the previous site's physics bodies. Other checks
cover actual walking across the bridge,
unhook/nearest-socket reattachment, braking length, separate player inputs,
seat handoff, command guards/rebuilds, worker/cargo recovery, repaired hostile
input, snapshot replay, canonical pack/unpack and continuation from restored
physics state. Placement of workers/cargo occurs only in isolated interaction
and recovery fixtures; the three job solutions use ordinary controls.

Network tests run a real server and two clients at 60/30 updates per second
over deterministic loopback, clean and with 50 ms each way, 10 ms jitter and
5% loss. Both profiles haul the conveyor, agree on 63 recent verified ticks
and have no resyncs. Render tests load the cue pack, render geometry/shadows,
check a changed image after hauling, and inspect all three job configurations.
Native OpenGL, browser WebGL and WebGPU run the same fixture.

Local validation: all three native and four web debug CTests pass, including
the pinned job solutions, both network profiles, OpenGL, WebGL and WebGPU.
Node 24.16.0 is configured in both build trees. The docs production build and
Svelte check pass (zero errors/warnings), as do all nineteen analytics/admin
and production prototype HTTP checks. Native standard-host scripts complete
each job through the actual GUI and keyboard controls with zero crew mishaps;
conveyor bridge, all three secured loads and the thumbnail were visually
inspected. Moving offcuts out of the mandatory ramp approach, widening the
loading sled and separating the low lintel from the landing ramp removed a
fussy simultaneous floor/ceiling wedge without removing the required tilt.

Integration retained the incoming SWINDLE registration, unrelated FOLDLINE
updates and NIGHT ROUTE removal. Conflicts in the shared catalog/id lists,
prototype CMake list and navigation notes were resolved by retaining both
live additions and the removal. DEADWEIGHT targets were rebuilt and all three
native/four web checks pass again with unchanged pins; the merged production
docs build, zero-warning Svelte check and nineteen HTTP/admin checks pass.

Real Internet room play, a physical gamepad, phones, other operating systems and sanitizers
are outside the coverage of this Windows prototype task. No CI or deployment
was requested.
