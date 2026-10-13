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
the finale. A `Gardener` owns its chart (`realm`), feet, yaw/pitch, doorway
cooldown, carried seed and sitting state. Movement uses clamped input and
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
materials and foliage motion. The main picture is HDR with four samples;
destination pictures follow the window at 0.65, bloom at 0.25. Wind, water,
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
  Final hash is `1432443607299100957` on native and WASI.
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
