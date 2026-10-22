# MicroDuck stepping lab

A two-player **assisted physical stepping** prototype for the optional `newton`
package, using Pollen Robotics' published MicroDuck model: 15 rigid bodies and
14 hinge joints. Each trunk is a free dynamic root. Authored foot-plane forces
support the robot while an illustrative joint controller alternates its feet.
The ducks start in separate lanes.

**This is a stepping demo.** Opposite step patterns have both drifted backward
in probes; controlled forward/backward walking and self-balancing are not
demonstrated. Upright, heading and lane assistance starts enabled and is labelled
in the HUD. There is no learned walking policy, BAM servo model or full-body
collision handling.

Build and run from the repository root with an installation containing Newton:

```sh
tide run examples/robotics/microduck --release
tide build examples/robotics/microduck --release --web
```

Use `--release` for play: the unoptimized CLI Newton build is too slow for
this full articulation. It uses the same simulation and contact code.

Escape closes the Rooms menu. Each player controls their own duck:

| Control | Action |
| --- | --- |
| Hold W/S, Up/Down, or left stick Y | Opposite joint step patterns, after a one-second settling period; not velocity tracking |
| Q/E, Left/Right, or D-pad Left/Right | Previous/next leg, neck or head joint |
| A/D or left stick X | Trim the selected joint's target |
| R or gamepad South | Reset the full dynamics, controller and recording ring |
| B or gamepad North | Toggle upright, heading and lane assistance |

Reset restores the authored STAND pose with coherent body placement, a trunk
height of 0.12 m, zero velocities/forces, cleared trims/step phase and assistance
enabled. Joint selection and `floorEnabled` persist. Controls still apply on
the reset tick, and its successful step starts the new ring. The readout shows
actual joint state, clamped control and foot loads.

The match runs at 200 Hz with one 5 ms Newton step per tick. Joint PD gains are
4/0.03 with a 0.96 Nm motor-control clamp; authored passive damping 0.053 and
armature 0.0018 remain separate. These are illustrative controls.

Each foot has four authored contact points spanning 64 × 30 mm around its XML
foot site. A flat plane at world Y=0 applies a compliant normal response of
1000 N/m and 4 N·s/m, clamped to 12 N per point, plus regularized friction
(coefficient 0.8, gain 20). These ordinary Tide match forces and torques use
the existing Newton body interface; the package still has no general collision
backend. Only the feet contact the plane, so fallen bodies can pass through it.

Assistance applies upright and world +X heading torques and a lateral Z force
toward the duck's lane. Its forward X and vertical Y force components are
exactly zero. `Duck.assisted` and `Duck.floorEnabled` are ordinary match fields;
setting `floorEnabled = false` removes foot-plane forces for an ablation.

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
  applied foot-normal force, smoothed step command, sine/cosine of step phase,
  and assistance flag (0/1).
- Action: 14 desired joint angles.
- Effort: 14 clamped actuator controls.
- Actual `assistanceForce` and `assistanceTorque` applied during the transition.
- Reward: negative summed squared post-step joint-target error, an illustrative
  tracking score rather than a walking reward.

The pre-step observation is captured after any reset but before this tick's
controls, assistance toggle, phase update and contact calculation. Its foot
loads therefore describe the preceding step (zero after reset). The next
observation follows successful physics; its foot loads are the normal forces
applied during that step. Failed steps append nothing.

Records follow snapshots and rollback. An exporter should consume verified ticks
in order; this fixture has no exporter, trainer or record of the exact rendered
observation a human used to choose an action.

For a quiet, bounded desktop capture, save a script such as:

```text
3 key escape
80 key w down
200 capture duck-stepping.png
220 key w up
```

Then run with `TIDE_HIDDEN=1` in the agent's shell:

```sh
tide run examples/robotics/microduck --release --script duck.txt --frames 260 --size 1280 720 --capture duck-final.png
```

Repository CMake targets are `newton_microduck` and `tide_newton_microduck`.
All 14 generated-game tests and fresh-process joins pass on native/WASI debug
and release, including sustained foot lift/touchdown, contacts, reset, recording,
replay and multiplayer. Native-to-WASI continuation passes in both directions.
Installed builds and desktop/WebGL/WebGPU play checks pass too. Physical
fidelity and unassisted walking remain unvalidated.
