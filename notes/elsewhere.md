# Elsewhere

Added on 2026-10-13. The owner requested an original visually striking,
non-Euclidean/fractal, cozy cooperative prototype. The Elsewhere conservatory
and three-garden concept was accepted, followed by a request for a proper
playable demo to show publishers. No engine, language, runtime or package
change was needed. No deployment or CI dispatch is requested.

## Match contract

`play.tide` declares a 60 Hz match for two, with host migration enabled.
`Garden` owns two seats/entities/player IDs, each garden's 24-second source
energy, accumulated growth, seed custody, planted bits, completion count and
the finale. A `Gardener` owns its chart (`realm`), feet, yaw/pitch,
carried seed and sitting state. Movement uses clamped input and
tagged positions/rotation; the whole entity snaps at a crossing. Look deltas
are not predicted. The walkable inset keeps visitors on their island and
clear of the main trunk/source pedestal; other decorative scenery has no
collision. There is no physics package or generated-C boundary addition.

`Door(realm,index)` is shared by simulation and rendering. A forward crossing
of a hub window translates into its garden; the garden's back window returns
to the matching hub window. A second, forward-only connection leads to the
next garden and makes a three-chart cycle. Its destination's back window
leads home, not back to the preceding garden. Three independent hub windows
and that directed cycle are the implemented impossible topology.

Crossings use the movement segment's intersection with the plane and the
same 1.42-unit half-width as the rendered opening. Starting or ending exactly
on the plane counts when moving through it; resting there does not. There is
no crossing cooldown: an immediate reverse crosses the return window normally.
At most one chart change happens per tick. Directed crossings and movement
direction prevent an idle visitor at the exit from bouncing between charts.

Sources are activated or sustained while using nearby. A seed grows by
holding use for 3.5 seconds near its tree while energy remains. Growth is
persistent. Release/repress takes the finished seed; custody prevents a second
visitor taking the same one. The matching home bed accepts it once. Three
plants start a nine-second flowering transition, with no forced game end.
Using the home bench sits; using again or walking stands. Sources last long
enough to do everything alone, while partners can sustain/tend or visit
different charts at once. All progress/custody is normal snapshot/hash/packed
match data, not local presentation or a service answer.

Joining claims an empty seat. Leaving returns an unplanted carried seed to
its grown tree before destroying the visitor. A later join reuses the seat
with an empty hand. `BeginAgain` accepts only a current participant, resets
shared progress and both visitors' gameplay fields, preserving their owners.

## Presentation

`shapes.tide` makes original curved/branched geometry once into local lists:
four islands, self-similar trees and distant terrariums, clouds, moss, ferns,
flower beds, brass window frames, a ribbed conservatory with hanging vines,
the Sunwell's solar rings, Stillwater's pond/lotuses/channels and the
orchard's rosewood pergolas and suspended chimes. A ceramic gardener in a
straw hat represents the companion. Its body is static; bespoke movement or
gesture animation is not built.

`render.tide` records destination cameras and the main scene with the same
chart translation used by traversal. Apertures sample the destination
projectively by screen position and have curved tops. Incoming mouths are
excluded from their own destination pass, and a destination-side clip plane
keeps nearer scenery out. Further windows are frosted: **one live crossing,
not infinite portal recursion**. Destination visitors, source effects and
seed growth are the actual match state seen through each camera.

Each pass uses a 1536-square shadow map with four comparison taps, procedural
materials and foliage motion. The main and destination pictures follow the
window at full display resolution, HDR with four samples; bloom at 0.25. Wind, water,
fireflies, bloom and sky motion are local visuals. CPU simulation reads none
of them. Materials retain forgivable GPU precision differences. Static
geometry lists are drawn directly from their holder, not copied as a whole
`Sculpture` into scratch each pass. That correction changed no match or
image contract. A 1280x720 release stats capture before/after showed CPU about
28.1 ms versus 0.1 ms and GPU about 0.74/0.68 ms; these are one machine's
headless samples, not an FPS guarantee for other GPUs or browsers. VRAM in
that capture was about 151 MiB. Visible-browser performance on slower
hardware still needs review.

`ui.tide` supplies the title, play/host/copy/join flow, sound slider, guide,
nearby-source/seed prompts, carrying hint, verified restoration count,
completion message, reset, bench and HUD-hide photographs. Local loopback's
Connected event does not dismiss the initial title. Explicit host/join starts
can enter automatically. Mouse-lock loss brings up the menu. Touch controls
and a right-side look drag exist; a physical phone has not been tested.
The game-specific shell has its title, description, noindex and a graphics
startup error view. It is selected before `tide_add_game` so the ordinary web
pack embedding remains intact, rather than overwriting the page without sound.

`sound.tide` layers an original 32-second bell/piano miniature, air and water,
with chimes at crossings/source tending and seed pickup, and a planting
chord. `art/make_audio.py` synthesizes all five WAVs with stdlib arithmetic;
no recording, model or texture was imported. Sound settings are local. The
103.3-second full-journey capture had peak 0.115, RMS 0.0137 and zero clipped
PCM16 samples. **Nobody has listened to the mix** in the agent's muted runs;
musical quality, balance and loop audibility need the owner's listening review.

## Validation

- `elsewhere_test` plays a complete solo journey with actual movement/use
  input through all hub windows, grows/carries/plants all seeds, waits for
  flowering and sits/stands. Only initial yaw is normalized by the harness.
  Final hash is `13569490290871266231` on native and WASI after removal of
  the crossing cooldown field (previously `1432443607299100957`).
- Its two-player scenario pins inert tending before a source wakes, parallel
  source/tending, exclusive custody, carrier departure recovery and seat reuse.
  Its third scenario covers the directed garden cycle, bounds, non-finite
  input repair, snapshot replay and exact packed-state round trip.
  Restart command checks reject a departed visitor and preserve the remaining
  visitor's ownership while clearing progress, carrying and sitting.
- `elsewhere_render_test` checks destination seed changes are visible from
  the hub, distinct chart pictures and the flowering finale. Native OpenGL,
  WebGL and WebGPU pass. These are image-change assertions, not a substitute
  for the screenshots' visual review or frame-time measurement.
- Local native debug: 2/2 game tests. Local web debug: 3/3 game tests,
  including both GPU backends, with Node 24.21.0. The game and release page
  build with warnings as errors. Full engine/compiler suites are not required
  for a game-only change and were not run.
- A standard-host script plays the title-to-ending journey in 6198 frames
  (103.3 seconds of simulated play) using real sampled movement/use, and its
  completion capture shows 3/3 and the ending message. Opening, all gardens,
  grown seeds and the flowering home were inspected. Two standard-host
  processes also joined and moved a companion into the host's picture.
  The reusable input is `prototypes/elsewhere/test/tour.script`; copy it into
  an ignored build folder first because captures are written beside the script.
- The collaborative browser's two WebGL pages hosted/joined through the
  production relay and reported the same room. Actual client movement
  crossed into Stillwater; its frame was inspected via a canvas read in an
  animation callback because the preview snapshot tool failed after the
  second tab was opened. This is connection/traversal smoke, not a full
  remote two-player completion or host-migration test. WebGPU rendering is
  verified by the fixture, not by that interactive smoke.
- Desktop and 390x780 title captures fit. Touch movement/look on a physical
  device, real audio listening, browser performance on slow hardware,
  real-Internet joint completion and migration remain untested. Other OSes
  and Android were not run, and no CI was dispatched.

## Catalog and review delivery

The server-only catalog, three public/retired-path gates and both prototype
test id lists include `elsewhere`. `elsewhere_page` stages under
`/prototypes/elsewhere/`; `private_games` collects that target or the native
game. The gameplay thumbnail is checked in beside the ignored generated HTML.
The public route is available only in a site build/deployment that stages it.
Adding it to source does **not** deploy it.

The local handoff is a served release page and a portable review bundle with
the self-contained HTML, a small local server, screenshots and play/pitch
instructions. The infinite fractal/scale-changing world, persistent saves,
longer authored content and distinct multi-step garden mechanics in the initial
pitch remain future directions. This demo is four finite charts, three shared
restorations and a complete relaxed ending.

Integration with `origin/dev` on 2026-10-13 retains all ten catalog ids,
including the concurrent Copper & Coil, HOT SWAP, DEADWEIGHT and SWINDLE
additions and NIGHT ROUTE's removal. Conflicts were in registration/build
and notes files; no shared engine/compiler dependency changed. Merged native
and web configurations/builds pass, with the game's 2/2 native and 3/3 web
tests passing again. Svelte check is clean, the production build passes and
all nineteen analytics/admin/production HTTP checks pass. The release,
staged and production-packaged Elsewhere HTML bytes agree. The portable
review ZIP is built locally; its HTTP server passes normal serving/header
and root-escape checks. No deployment or CI dispatch.

## Portal correction, 2026-10-13

The owner reported blurred window views, a triangular mixed-world image near
a window and walking past it without teleporting. The game had four faults:
its 1.35-unit traversal half-width excluded a strip of the 1.42-unit visible
opening; crossing tested the tick's endpoint rather than its intersection;
starting exactly on the plane could never cross; and the 0.6-second cooldown
allowed walking back through the return opening without changing charts.
The swept crossing above fixes these without an engine change.

The ordinary 0.3-unit camera near plane also clipped away the window surface
before traversal. Each frame now reduces its near distance as needed to keep
the near rectangle, including its corners, on the camera side of every mouth.
The calculation uses perpendicular plane distance, vertical FOV and aspect
ratio with a factor of two clearance. All destination and primary cameras use
that same projection; the existing infinite far side is preserved. Back-side
window compositing is rejected at the plane, rather than allowed through a
0.05-unit tolerance. Full-resolution destination targets also use four samples
to match the primary picture. This increases GPU storage and rendering work;
the older performance/VRAM samples above describe the earlier reduced targets.

Regression fixtures first failed against the old implementation: the visible
edge did not teleport, and the 0.1-unit close camera differed from its mapped
destination by 117,702 of 230,400 pixels. The corrected movement fixture covers
both visible edges of every hub opening, immediate reverse, exact-plane starts,
diagonal intersections inside/outside the opening, idle thresholds and two
owners crossing/reversing together. The render fixture compares the portal to
its mapped destination at distances 0.1, 0.02 and 0.0001, including yaw/pitch.
Fewer than 2% of pixels may differ substantially, allowing sampling and geometry
precision at the seam; native results were 1,689, 2,573 and 549 pixels.

Final native debug 2/2 and web debug 3/3 game tests pass, including OpenGL,
WebGL and WebGPU. Native/WASI journey hashes agree on the new value above.
Native release and web release page build cleanly. The standard-host full
journey completes with 3/3 and its ending capture was inspected again. The
staged local page and portable review bundle are refreshed. CI and deployment
remain unrequested; other operating systems and physical mobile GPU coverage
remain unavailable here.
