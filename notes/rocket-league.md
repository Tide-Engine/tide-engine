# Afterburn car soccer

The owner requested a Rocket League prototype on 2026-10-09, then rejected the
first version's controls, camera and physics on 2026-10-10. The follow-up
`prototypes/rocket-league/RESEARCH.md` records the fidelity audit, primary
measurements, shipped input presets and pinned reference implementation. The
first tests checked self-consistency, not similarity to the reference game.
`README.md` covers playing/building; `DESIGN.md` records the visual target.

## Match contract

`game.tide` owns the deterministic simulation at 120 Hz. Units are metres;
100 Rocket League Unreal units equal one metre. Tide is Y-up, Z-forward.
There are two one-car teams, host migration and deterministic bots for absent
or not-yet-started players. Owned Participant entities bind player IDs to
seats. Human joining/leaving replaces/restores the bot without resetting the
match. `Restart` accepts only an actual participant. Rooms' New game action
sends the same command.

`MatchState` contains the cars, controls, ball, 34 pad timers, scores, clock,
phase and participant mapping. `CarState` includes its forward/up basis,
local pitch/yaw/roll angular rates, four-wheel contact state, jump/flip timers,
boost minimum-burn timer, powerslide fraction, supersonic state, bump/contact
cooldowns and demolition respawn. Local camera, meshes, sounds and particles
never enter its hash. No engine, compiler or package API changes were needed.

The arena is 81.92 by 102.4 metres with a 20.48 ceiling. Goals are 17.8551 wide,
6.42775 high and 8.8 deep. Horizontal corners use an analytic 11.52 radius;
lower/upper fillets use 2.56. These reproduce a stadium shape, not the exact
commercial arena mesh. Strict goal-mouth bounds and analytic post/crossbar
contacts require the whole ball to cross within the goal opening. Actual
kickoff positions and all 34 pad
coordinates come from measured field data. Pickup tests use horizontal circles
and COM height (cylindrical regions), not distance to a ground point.

The effective Octane box follows RocketSim commit
`c2baacb8f4b441dd8505e63c2aeb5a1679b60b02`: 1.20507 long, 0.866994 wide,
0.386591 high, with its centre 0.138757 forward and 0.20755 up from the car
origin. The resting origin is about 0.1701 above the floor. Earlier RLUtilities
box dimensions differ; do not mix them with the current effective box.
The procedural shell and wheels were repositioned to match this smaller car.

## Handling and contacts

Flat-ground throttle uses measured acceleration knots (16 m/s² at zero,
1.6 at 14 m/s, zero at 14.1). Brake is 35 m/s², coast is a constant 5.25,
and the resulting steering is checked against Samuel Mish's recorded-game
curvature fits. Four steered tire contacts generate lateral and
longitudinal impulses through the chassis inertia. Contact-point velocities,
effective mass, slip curves, handbrake rise/fall and friction scaling follow
the pinned reference; removing the local-up lever arm matches its roll rule.
There is no prescribed yaw rate or aggregate sideways-velocity damping.
A stationary car cannot steer in place. Ground boost forces full throttle and adds 9.916667 m/s²;
air boost adds 10.583333, independent of the separate 0.666667 forward air
throttle (half in reverse). Top speed is 23. Boost consumes 100/3 per second,
with a minimum 0.1-second burn; large pads fill/return in ten seconds, small
pads give 12/return in four. Supersonic begins at 22 and clears below 21.
The first second of a supersonic
episode permits 21–22; after that it clears immediately below 22. Status is
updated after contacts and final speed caps, matching the reference callback order.

Four suspension rays use reference wheel geometry, spring stiffness and
compression/relaxation damping. Force and torque act through car inertia;
there is no snap of the car's up vector to a nearby surface. The offset box
resolves chassis contacts separately from the wheels, allowing wall/ceiling
transitions and physical landings. Single chassis/contact iterations and
analytic surfaces do not reproduce Bullet's full wheel/manifold solver.

Each wheel chooses the nearest world or ball-sphere ray hit. As in RocketSim,
wheel springs/tire impulses act only on the car even with dynamic ball support;
normal damping reads chassis velocity, bilateral tire friction also reads the
ball's point velocity/inertia, and only static world contact adds adhesion.
Three contacts restore the jump budget. One/two contacts quarter engine force,
retain air throttle, and block ordinary aerial torque while allowing flip torque.

Jump impulse is 2.916667 m/s, hold acceleration 14.583333 m/s² for 0.025–0.2 s,
gravity 6.5, and the first three ticks include 3.25 opposite-up sticky force.
The second-jump window is 1.25 s after the first hold ends. Falling off a
surface without jumping preserves the unused dodge. Directional flips use
speed-dependent impulse, separate flip torque, vertical damping and pitch
cancellation; a roof recovery has its own self-right hop. Aerial torque and
axis-dependent damping follow Mish's measurements, with a shared 5.5 rad/s
angular limit. W pitches down, S up, D yaws right and E rolls right.

The ball uses radius 0.9125, mass 30 versus car mass 180, drag 0.03,
restitution 0.6, speed cap 60 and spin cap 6. The measured 91.25 collision
radius differs from the commonly listed 92.75 API radius/rest-height margin;
our analytic floor has no imported collision-mesh margin. Ball/surface
friction opposes slip including angular surface velocity, transferring spin
into bounce direction. Car/ball contact solves physical impulse with inertia
and friction, then applies the separate measured Psyonix gameplay impulse.
The reference mechanisms replace the original guessed speed multiplier/lift.
Extra shot direction uses the cached pre-correction car-to-ball offset. The
published car/ball model explicitly excludes wheel hits and pinches; separate
wheel-ray behavior follows the compiled reference, without claiming all pinch
trajectories match the commercial game.

Car/car contact uses 15-axis oriented-box separation, contact-point velocity,
angular inertia, 0.09 friction and 0.1 restitution. Bumper hits add the
RocketSim ground/air velocity and upward curves, with a 0.25 s cooldown.
Demolition requires a closing bumper contact while supersonic against the
other team; a fast backward/side hit is insufficient. Respawn takes three
seconds. A single contact witness approximates persistent box manifolds.
RocketSim adaptations retain the MIT notice in `THIRD_PARTY_NOTICES.md`;
no GPL RLUtilities implementation was translated.

Kickoffs count down for three seconds; goals celebrate for 3.4. Regulation is
five active minutes with the clock stopped during both. At zero an airborne
ball stays live until a goal or first floor contact. `ballGrounded` records
contact before bounce response. A tie enters sudden-death overtime.

## Input and camera contract

Input analogue values are clamped, quantized and repaired if non-finite.
Classic KBM defaults to LMB boost, RMB jump, Shift powerslide/free air roll,
Space ball camera and MMB rear view. Modern uses Shift boost, Space/LMB jump,
RMB powerslide/free air roll, MMB ball camera and Ctrl rear view. Both use
WASD and Q/E. These separate shipped presets were verified against the local
first-party `TAInput.ini`; the old hybrid mapping was incorrect. Gamepad uses
triggers, left stick, South jump/East boost/West slide/North ball camera,
LT free air roll and right-stick swivel/click rear. Directional air roll is
unbound on the standard gamepad preset. Touch provides analogous controls.

Keyboard aerial safety suppresses pitch torque when W is carried through
takeoff, until W release, S or roll. Raw pitch is preserved for a held-W
forward dodge, and gamepad pitch bypasses keyboard suppression. Local
preferences sample their dodge deadzone into deterministic inputs. F2 opens
the controls/camera panel, including Classic/Modern selection, camera geometry,
stiffness/swivel/transition, deadzones, safety and shake. Preferences are local
and currently last only for the process. Menus do not pause the match; an
already-started human gets neutral input while in menus/settings, not a bot
commandeering the car. Before starting or after departure, the seat is a bot.

The initial wide preset is 110 FOV, 270 distance, 100 height, −3 angle and
0.5 stiffness. It is a familiar wide configuration, not claimed as Rocket
League's stock preset. The FOV is a horizontal value at a 16:9 reference,
converted to about 77.55 vertical for Tide; the vertical framing is retained
at other aspects (Hor+). The reference aspect is an implementation inference
from direct widescreen measurements, not a published Psyonix projection formula.

Ball camera orbits and aims at the actual 3D ball including overhead aerials.
Car camera follows horizontal heading without inheriting aerial roll. Wrapped
yaw and separate pitch smoothing rebuild a world-horizon orientation each
frame. Camera position is anchored to the interpolated car with relative orbit
lag, so the car cannot outrun an absolute-eye spring. A local surface probe
clips the eye to rounded walls/goals. Right stick swivels; Modern mouse offers
free look (Alt also enables it in Classic). Transition resets free look.
Wall compression and overhead-ball cases formerly lost the car in portrait.
Automatic camera now restores an adequate collision-safe eye gap and minimally
reframes the measured car bounds when they do not fit. At portrait's 40.7-degree
horizontal view the full car and ball sometimes cannot both fit; the ball's
direction marker handles that case. Manual free look/rear view stay unconstrained.
Proprietary camera springs/occlusion are not published; these remain reproductions.

## Presentation

`geometry.tide` builds original car bodywork, ball panels, pitch/goals, curved
banks, seating/crowd, trusses/lights and skyline. There are no imported game
assets. Upper fillets remain visually approximated by vertical hex panels
meeting the flat roof. `scene.tide` owns procedural turf/stadium materials,
2048 shadows, four-sample MSAA, bloom, HUD, exhaust and impact/goal effects.
Targets resize in place. Every target-using view guards the first unconnected
frame to avoid submitting target zero before Prepare has run.
`devices.touchscreen.connected` selects touch UI; array capacity is not proof
of a connected touch device. Mouse buttons never emulate touch actions.

Goal/impact/kickoff one-shots observe verified state. A score reset does not
trigger a celebration. Six original mono 48 kHz WAVs come from
`sfx/make_sounds.py`; engine/boost loops remain local. Earlier audio capture
was non-silent (peak 6,404); the agent has not listened to the sounds.

## Build and catalog

`TIDE_PRIVATE_PROTOTYPES=ON` enables `rocket_league`, `rocket_league_tests` and
`rocket_league_render`; `private_games` includes it. `rocket_league_page`
stages the self-contained HTML at `docs/static/prototypes/rocket-league/index.html`.
The admin catalog lists Afterburn for 1–2 players at that private prototype
root, outside Samples, search and sitemap. Its checked-in thumbnail is actual
native gameplay. The existing manual docs workflow stages it; no deployment
or workflow dispatch was performed for this follow-up.

The nested `test/tide.packages` keeps C test hosts out of CLI playable sources.
Windows browser/WASI tests use supported bundled Node, not Node 24.15 (see
`notes/testing.md`); all browsers pass `--mute-audio`. Native runs use only
this checkout's binaries with `TIDE_HIDDEN=1` and bounded script/capture flags.

## Validation and limits

Follow-up validation passed on 2026-10-10: all 33 simulation tests in native
debug/release and WebAssembly; native debug/release render checks; and browser
play/render checks on both WebGL and WebGPU. Debug, release and WebAssembly
agree on the scripted simulation hash `0xe6230c01396f6826`.

The independent pinned RocketSim/Bullet build supplies two motion references
without commercial assets (fixtures are described in `RESEARCH.md`). For the
airborne nose hit, four-tick ball velocity differs by 0.311804 UU/s (0.0214%);
contact happens one tick early and position leads by about 5.39 UU, so this
does not establish identical scheduling. For powerslides starting at 10/15 m/s,
velocity-vector error is 0.044/0.074 m/s after 0.5 s and 0.079/0.146 after 1 s;
heading errors stay below one degree. Current-tick tire coefficients and a
single contact pass still differ from Bullet's cached solver scheduling.

Measured turn radii at 5/10/15/23 m/s are 2.454/4.139/6.955/11.481 m versus
the recorded-game fit's 2.513/4.255/7.246/11.364 (within 4.1%). Tap/full-hold
jump apexes are 0.9297/2.4124 m at 0.4833/0.8833 s. The bot completes a
five-minute match, 0–1. Lossy clients reach verified ticks 2995/2994 with
25,290/25,670 replayed ticks, no resyncs and exact verified-state agreement.

The numerical fixtures independently compare measured acceleration/coasting,
braking, curvature at four speeds, jump timing/height, boost timing, aerial
axis signs/damping, dodge budgets and ball flight/spin. Jump apex expectations
are 120 Hz integration predictions from published forces, not direct recorded
game apex measurements. Tests also cover controls, 3D camera projection,
reference geometry, menu input, integration, snapshots, replay, lossy clients
and native/WebGL/WebGPU rendering. Determinism alone does not establish fidelity.

Camera regressions cover both teams and desktop/portrait framing, all eight
car-box corners through reversal transitions, mirrored walls, rounded corners,
ceiling and an exactly overhead ball. Real browser play exercises both KBM
profiles, boost/jump/dodge, camera toggles/settings and touch drive/boost.
Fresh native and browser captures were inspected; the catalog thumbnail is
from the final native build. Site build/check and the 31 related
admin/HTTP/analytics/SEO/access regressions pass with the staged game.

This remains a single-stadium duel with reduced tire/contact and camera models.
Exact arena mesh contacts, competitive handling exploits, team playlists,
ranked matchmaking, garage/cosmetics and replay playback are not built. Physical
gamepad, real phone performance/touch feel, separate machines through the public
relay and CI-only platforms remain untested. Human competitive playtesting is
still required to assess the feel beyond the measured motions.
