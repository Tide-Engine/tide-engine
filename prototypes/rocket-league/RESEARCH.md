# Rocket League fidelity audit

Researched on 2026-10-10 after the owner rejected the first prototype's
controls, camera and physics. The first version's tests established that its
own rules were repeatable. They did not establish Rocket League fidelity.
The beginner articles in `DESIGN.md` were insufficient implementation evidence.

## Controls: verify a preset, do not invent a hybrid

The first-party `TAInput.ini` installed in the standard Rocket League Documents
configuration directory on this machine (dated 2026-09-23) contains the actual
Standard, Legacy and Modern preset definitions. It was read without modification.
This establishes shipped mappings, not which preset the owner actively uses.

| Action | Classic keyboard/mouse | Modern keyboard/mouse |
|---|---|---|
| Drive / steer | WASD | WASD |
| Jump | Right mouse | Space; left mouse alternative |
| Boost | Left mouse | Left Shift |
| Powerslide / free air roll | Left Shift | Right mouse |
| Ball camera | Space | Middle mouse |
| Rear view | Middle mouse | Left Ctrl |
| Directional air roll | Q / E | Q / E |

The first version combined Space jump, Shift/left-mouse boost, Ctrl slide,
C camera and shoulder directional roll. That was neither preset.

Psyonix's [September 2020 patch notes](https://www.rocketleague.com/news/rocket-league-patch-notes-september-update/)
document the gamepad Default/Legacy distinction: Default free air roll is LT/L2,
where Legacy shares it with X/Square. They also specify keyboard aerial safety:
W held through takeoff does not immediately pitch down until the relevant
release, opposite pitch or roll input. Safety suppresses aerial pitch torque;
it must preserve raw W input for a forward dodge on the second press of jump.
[Season 24 patch notes](https://www.rocketleague.com/news/rocket-league-s24-patch-notes-v276)
document the 2026 Modern controls and Free Look introduction. The exact mappings
above come from the installed preset definitions rather than inferring them
from the announcement.

## Camera: geometry and conventions

Rocket League camera distances use centimetres, while this prototype uses
metres. Tide's perspective-camera FOV is vertical. A horizontal reference FOV
is converted using a 16:9 reference:

`verticalFov = 2 * atan(tan(horizontalFov / 2) / (16 / 9))`.

At a setting of 110 this is approximately 77.55 degrees vertically. Other
aspects preserve that vertical framing: ultrawide gains horizontal view,
portrait crops horizontally. [WSGF's direct aspect-ratio measurements](https://www.wsgf.org/dr/rocket-league/en)
report Rocket League's Hor+ behavior. Using 16:9 as the numerical reference
is an implementation inference, not a published Psyonix projection formula.

The first version passed 90–96 degrees directly as vertical FOV, producing
roughly 121–126 degrees horizontally at 16:9. Its chase offset was 3.8 metres
back and 1.7 metres up, and its ball tracking was mostly yaw with restricted
elevation. Keeping the car visible did not make that camera correct.

The [March 2020 official patch](https://www.rocketleague.com/news/patch-notes-v1-74-march-update/)
sets the Default distance to 270 and stiffness to 0.5 and distinguishes Legacy.
A 110-degree horizontal, 270-distance, 100-height, −3-degree camera is used as
a familiar wide-view configuration, not claimed to be the commercial game's
stock preset. Camera settings and viewport projection must remain separate.

The proprietary ball-camera smoothing, occlusion and wall transitions are not
published. Our full 3D ball tracking, orbit transitions, stable world horizon
and free-look implementation are reproductions of behavior, with explicit
geometric tests; they are not a source-identical camera algorithm.
Collision-safe automatic reframing keeps the car visible near walls and under
overhead balls. At narrow portrait aspects, when both cannot fit, the existing
offscreen ball-direction marker takes over; manual free look stays unconstrained.

## Ground handling: measured curves

Samuel Mish's [ground-control experiments](https://www.smish.dev/rocket_league/ground_control/)
record steering curvature and acceleration from the actual game. They establish
a piecewise speed-dependent turning radius, speed-dependent throttle taper,
35 m/s² braking and 5.25 m/s² coasting. Boost forces forward throttle and adds
9.91667 m/s² ground acceleration. A car cannot rotate in place merely because
steer is held.

The first version used a nonzero stationary yaw rate, an unrelated reverse
curve and speed-proportional coast drag. Its handbrake was a multiplier on yaw
plus a single sideways damping coefficient. Those choices were not measured.

The reference fit uses curvature knots at 0, 5, 10, 15, 17.5 and 23 m/s;
curvatures are 0.69, 0.398, 0.235, 0.138, 0.11 and 0.088 inverse metres,
using the author's [RLUtilities implementation](https://github.com/samuelpmish/RLUtilities/blob/b9d1cdbd3965a24c6ecdc6d662ac7d6af2407ed7/src/mechanics/drive.cc).
The resulting angular rate is signed forward speed times curvature times
steering. This is a measured benchmark, not the final prescribed yaw law.
The first researched replacement still imposed that yaw while damping lateral
COM velocity. An independent RocketSim flat-plane comparison exposed overly
aggressive powerslides and excessive speed loss. The final implementation
instead applies forces at four steered tire contacts, using point velocities,
effective mass/inertia, slip and handbrake friction curves. This recovers the
coupling between rotation and drift rather than multiplying a target turn rate.

## Jump, dodge and aerial control

[RLBot's jump measurements](https://wiki.rlbot.org/v5/botmaking/jumping-physics/)
describe 6.5 m/s² gravity, an approximately 2.92 m/s jump impulse, a 14.6 m/s²
hold contribution for at most 0.2 seconds and at least three 120 Hz ticks,
and three ticks of post-jump adhesion at 3.25 m/s². A tapped jump must still
receive the minimum hold contribution.

[RocketSim's car update](https://github.com/ZealanL/RocketSim/blob/c2baacb8f4b441dd8505e63c2aeb5a1679b60b02/src/Sim/Car/Car.cpp)
and [constants](https://github.com/ZealanL/RocketSim/blob/c2baacb8f4b441dd8505e63c2aeb5a1679b60b02/src/RLConst.h)
provide independently implemented timing and force details: the second-jump
window follows the end of the initial hold, driving off a surface preserves an
unused dodge, the flip has its own torque and vertical-velocity behavior,
opposing pitch can cancel a flip, and a boost tap has a minimum burn interval.
These are reverse-engineered reference implementations, not Psyonix source.

Mish's [aerial-control measurements](https://www.smish.dev/rocket_league/aerial_control/)
fit independent pitch/yaw/roll torque and damping, with a shared 5.5 rad/s
angular-speed cap. Pitch and yaw damping change while their corresponding
control is active. The first version instead chased arbitrary target rates
with one spring, and its pitch sign was inverted: W raised the nose.

## Contacts, scale and the ball

[RLBot's measured field values](https://wiki.rlbot.org/v5/botmaking/useful-game-values/)
give 81.92 × 102.4 metres, a 20.48-metre ceiling, goal and kickoff coordinates,
34 pad positions, and cylindrical pickup regions tested against the car's
centre of mass. The first version guessed several small-pad positions and put
corner large pads too far sideways. It also used spherical pickup distance.

[RocketSim's current Octane configuration](https://github.com/ZealanL/RocketSim/blob/c2baacb8f4b441dd8505e63c2aeb5a1679b60b02/src/Sim/Car/CarConfig/CarConfig.cpp)
uses an effective oriented 1.20507 × 0.866994 × 0.386591-metre collision box,
offset 0.138757 forward and 0.20755 up from the car origin. Earlier RLUtilities
dimensions are 1.180074 × 0.841994 × 0.361591; this implementation follows the
current RocketSim configuration rather than mixing those two references.
The origin rests near 0.17 metres.
The first version's centred 1.48 × 1 × 0.46 box and 0.38 origin gave different
contacts even before considering its impulse response.

Mish's [ball/surface experiments](https://www.smish.dev/rocket_league/ball_simulation_1/)
show restitution near 0.6 and friction opposing contact slip, which includes
spin. A spinning ball can gain lateral motion on a bounce. The first version
used restitution 0.64, sideways drag and cosmetic spin with no spin-to-velocity
coupling. Its ball collision radius was also slightly oversized.

Mish, Nevercast and tarehart's [car–ball collision experiments](https://www.smish.dev/rocket_league/ball_simulation_3/)
identify two contributions: the physical contact impulse (including angular
surface velocity, inertia and friction), then a gameplay impulse dependent on
relative speed and direction from car to ball. The first version used a guessed
normal speed multiplier and an arbitrary bumper lift. It omitted the mechanisms
that distinguish controlled dribbles, flicks and strong corner hits.

Car–car contacts use oriented-box separating axes, physical contact impulses
with angular inertia, 0.09 friction and 0.1 restitution. The separate bumper
velocity curves, 0.25-second bump cooldown and supersonic demolition condition
follow [RocketSim's arena contact callback](https://github.com/ZealanL/RocketSim/blob/c2baacb8f4b441dd8505e63c2aeb5a1679b60b02/src/Sim/Arena/Arena.cpp).
This prototype uses a single contact witness rather than Bullet's persistent
multi-point manifolds, so box stacking and glancing contact remain approximate.

The numerical fit data and mathematical models were implemented independently.
RocketSim code adaptations retain its MIT notice in `THIRD_PARTY_NOTICES.md`.
No GPL RLUtilities implementation was copied or translated; its published
curvature knots are measured reference data.

## Verification standard

The pinned RocketSim implementation was compiled independently with Bullet,
without commercial arena assets. A `THE_VOID` airborne nose-hit fixture uses
an identity Octane at `(0,0,800)` UU, velocity `(1000,0,0)`, no boost/controls;
the ball starts at `(190,0,820.755)`, velocity `(0,0,-100)` and zero spin.
After four 120 Hz ticks the reference ball velocity is
`(1459.242310,0,-28.956762)` UU/s. The regression permits 1 UU/s vector error.
This is a reverse-engineered simulation reference, not measured Psyonix output.
The prototype contacts one tick earlier and its ball is about 5.39 UU ahead;
the velocity benchmark does not claim identical collision scheduling.

For powerslides, the same reference adds a static flat plane with the original
arena friction/restitution. With 0.02 throttle and full steering, initial speeds
of 1000/1500 UU/s produce 0.5-second yaw of 1.099752/1.215760 radians and
one-second speed of 611.136/906.201 UU/s. Tests compare both heading and retained
speed, because a car can turn to the right angle while drifting incorrectly.

Reference checks measure motion in game units after real simulation ticks:
acceleration/coasting/braking, turn curvature at several speeds, jump height
and timing, released-input angular damping, rotation signs, dodge budgets and
ball slip response. Input tests exercise both shipped binding schemes and
gamepad deadzones. Camera tests check projection, 3D ball tracking, independent
car orientation and user overrides, rather than requiring every scene to put
the car inside one arbitrary screen band.

Deterministic hashes, snapshots, lossy-network agreement and browser rendering
remain required integration checks. They cannot establish an indistinguishable
feel. Final measured results and remaining contact/camera approximations are
recorded in `notes/rocket-league.md` after validation.
