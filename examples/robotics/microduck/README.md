# MicroDuck physics sandbox

A two-player physics sandbox for the optional `newton` package, using Pollen
Robotics' published MicroDuck model: 15 rigid bodies and 14 hinge joints per
duck, each with a free dynamic trunk. Newton package colliders support contacts
with the floor, the other duck and four loose balls. Hold the left mouse button
to drag a marked duck part or ball; either player can grab either duck.

The illustrative joint controller still offers opposite step patterns, not
reliable forward/backward travel. Controlled walking and self-balancing are not
demonstrated. Upright, heading and lane assistance starts enabled and is labelled
in the HUD. There is no learned walking policy or BAM servo model.

Build and run from the repository root with an installation containing Newton:

```sh
tide run examples/robotics/microduck --release
tide build examples/robotics/microduck --release --web
```

Use `--release` for play: the unoptimized CLI Newton build is too slow for
this full articulation. It uses the same simulation and contact code.

Escape closes the Rooms menu. Joint controls apply to your own duck:

| Control | Action |
| --- | --- |
| Hold left mouse and drag | Pull a marked part of either duck or one of the shared balls; release to let go |
| Hold W/S, Up/Down, or left stick Y | Opposite joint step patterns, after a one-second settling period; not velocity tracking |
| Q/E, Left/Right, or D-pad Left/Right | Previous/next leg, neck or head joint |
| A/D or left stick X | Trim the selected joint's target |
| R or gamepad South | Reset your duck and all four shared balls; leave the other duck unchanged |
| B or gamepad North | Toggle upright, heading and lane assistance |

Reset restores the authored STAND pose with coherent body placement, a trunk
height of 0.12 m, zero velocities/forces, cleared trims/step phase and assistance
enabled. It clears your recording ring and restores the balls to their home
poses. Body revisions invalidate either player's grabs on those reset bodies;
release and press again to grab them. Joint selection persists. Controls still
apply on the reset tick, and its successful step starts the new ring. The
readout shows actual joint state, clamped control, sole contact loads and the
accepted grab wrench.

The match runs at 200 Hz with four 1.25 ms Newton substeps per tick. Joint PD
gains are 4/0.03 with a 0.96 Nm motor-control clamp; authored passive damping
0.053 and armature 0.0018 remain separate. These are illustrative controls.

The physical floor is a static infinite `Plane` at world Y=0; its drawn surface
is only 4 × 3 m. Each duck has 61 original compound `Sphere` proxies covering
all 15 bodies, with six marked sole spheres per foot. Matching exclusion groups
disable collisions within one duck while retaining contacts with the other
duck, balls and floor. All game colliders use `stiffness = 5600`, `damping = 3`,
`frictionDamping = 1` and `friction = 0.8`. The actual upstream Newton contact
kernel supplies compliant contact forces and regularized friction, rather than
a hard nonpenetration or static-friction constraint. Shapes are approximate
estimates in the original visual/source frames, not STL or CAD geometry.

Assistance applies upright and world +X heading torques and a lateral Z force
of up to 4 N toward the duck's lane. Its forward X and vertical Y force
components are exactly zero. `Duck.assisted` is an ordinary match field
controlled by B.

Mouse picking uses the displayed body poses and `Draw.ScreenRayOrigin` /
`Draw.ScreenRayDirection` in a view. The initial hit defines a body-local
sphere anchor and a fixed drag plane; local pointer state never drives the
solver directly. Bounded absolute match input carries the collider, anchor,
target, body revision and frame serial. Match code validates the full entity
generation, enabled sphere, surface anchor and revision before applying a pull.
Point velocity includes COM linear velocity plus angular velocity crossed with
the COM-to-anchor arm; torque is that arm crossed with the applied force.
Each controller's wrench is capped at 12 N and 0.6 Nm, scaling the force when
the torque cap is reached to keep it acting at the same point. Grabs add to
assistance and other players' forces. A serial heartbeat expires after 0.25 s
of elapsed match ticks without a new pointer frame. Release, reset or invalid
selection stops that pull; game-owned external wrenches are replaced every
tick, including after a failed physics step.

The checked-in model is generated offline from the pinned XML descriptions in
[`upstream/`](upstream/). Conversion preserves mass, centre of mass and full
inertia tensors by rotating each body into its principal axes and transforming
its joint frames consistently. Simulation builds need no Python, MuJoCo or
asset download. [`UPSTREAM.md`](UPSTREAM.md) records provenance, licensing and
regeneration instructions.

The robot casings, dark camera/servos and broad feet are original procedural
geometry informed by [Pollen's official photos](https://pollen-robotics.com/microduck/press-kit/)
and the Apache-2.0 XML frames and metadata. Dimensions estimated from photos
are approximations, not exact CAD. No STL geometry or photo textures are used.
The imported XML and converted model retain their Apache-2.0 provenance; see
`UPSTREAM.md` for the separate upstream mesh license and appearance references.
The visual jaw moves with the head; this 14-joint model has no mouth actuator.
Geometry is cached locally and follows the body transforms; its vertex lighting
is baked from the first pose.

Each duck keeps a bounded ring of 64 transitions in ordinary match state:

- Simulation tick.
- Observation and next observation: 44 values in order—14 joint angles,
  14 joint velocities, trunk height, world COM linear velocity XYZ, world angular
  velocity XYZ, gravity direction in the original trunk frame XYZ, left/right
  sole world-Y contact force, smoothed step command, sine/cosine of step phase,
  and assistance flag (0/1).
- Action: 14 desired joint angles.
- Effort: 14 clamped actuator controls.
- Actual `assistanceForce` and `assistanceTorque` applied during the transition.
- This controller's grab request, requested collider/body-local anchor/world
  target, accepted body and actual `grabForce` / `grabTorque`. An unaccepted
  request has no accepted body or applied grab wrench.
- Reward: negative summed squared post-step joint-target error, an illustrative
  tracking score rather than a walking reward.

The pre-step observation is captured after any reset but before this tick's
controls, assistance toggle, phase update and physics step. Its foot
loads therefore describe the preceding step (zero after reset). The next
observation follows successful physics. Each foot value sums the mean world-Y
`Collider.contactForce` across its six marked sole spheres, including any
vertical friction contribution or contact with another object. It is not a
floor-only normal force or a real sensor reading. Failed steps append nothing.

Records follow snapshots and rollback. An exporter should consume verified ticks
in order; this fixture has no exporter, trainer or record of the exact rendered
observation a human used to choose an action. Grab records describe only that
duck controller; another player's grab can affect either duck or shared balls,
so a duck's ring is not a complete world dataset.

For a quiet, bounded desktop capture, save a script such as:

```text
3 key escape
80 key w down
200 capture duck-sandbox.png
220 key w up
```

Then run with `TIDE_HIDDEN=1` in the agent's shell:

```sh
tide run examples/robotics/microduck --release --script duck.txt --frames 260 --size 1280 720 --capture duck-final.png
```

Repository CMake targets are `newton_microduck` and `tide_newton_microduck`.
All 33 backend runtime tests pass on native and WASI debug and release,
including nine independent stock Newton contact-reference samples checking
both state and forces. The contact history hash is
`ddf1584fd2502ff1`; the existing noncontact history pin remains
`92249536c6268981`. Independent integration review found no actionable issues.

With the final 5600/3/1/0.8 contact settings and four substeps, both signed
patterns pass the unchanged >5 mm sole-envelope clearance and contact-dwell
checks. Each foot completes five lift/touchdown cycles in the ten-second probe.
Peak left/right clearances, measured from the actual sole spheres, are:

| Pattern | Left | Right |
| --- | ---: | ---: |
| +1 | 19.101 mm | 19.505 mm |
| -1 | 11.245 mm | 11.207 mm |

An unassisted topple initialized at a 0.35 m trunk height reaches a minimum
sphere bottom of -15.692 mm, within the unchanged 20 mm transient penetration
budget. The settled minimum is -0.581 mm and the final trunk COM speed is
0.000097 m/s. These checks exercise full-body contact and assisted foot motion;
they do not demonstrate controlled travel or validate real-robot fidelity.

Game replay checks pass. The MicroDuck world hash `f87f0902ec4db694`
and arm hash `db6cb5835d7c9b26` agree across native/WASI debug and release.
All 24 MicroDuck fixture cases pass in those four configurations, including
the final two-client rollback scenario. Both clients complete thousands of
replay ticks with zero resyncs. Active-grab fresh-process joins also pass,
with all four join files byte-identical. The full native debug workflow passes
2,554/2,554 tests and web debug passes 2,416/2,416, including WebGPU drawing.
Installed CLI native/web release builds and a bounded native capture pass.
Muted WebGL and WebGPU checks at the final stiffness confirm ball
grab/lift/release, head grab/lift/drop with assistance off, full-body floor
rest, reset of the owned duck and shared balls, and resumed recording. There
are no page exceptions or WebGL/WebGPU validation errors; only the known
autoplay warning remains. All 40 upstream hashes and 46 staged source/header
files match. Other-platform coverage, independent physical fidelity and
controlled unassisted walking remain unvalidated.
