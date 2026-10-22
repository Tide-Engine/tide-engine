# Anchored MicroDuck

A two-player joint-control fixture for the optional `newton` package. It uses
Pollen Robotics' published MicroDuck walking model: 15 rigid bodies and 14
hinge joints, with the trunk anchored to the world. Players can move the legs,
neck and head through bounded joint targets. It does not balance or walk;
there are no contacts or simulated floor.

Build and run from the repository root with an installation containing Newton:

```sh
tide run examples/robotics/microduck
tide build examples/robotics/microduck --web
```

Escape closes the Rooms menu. Q/E select the previous/next joint; A/D move its
target. R resets that player's duck to its authored standing pose and starts a
new recording. Both players control their own duck through the same input and
Newton step. The on-screen readout shows the selected joint and its state. The
gamepad D-pad also selects joints; the left stick moves the target and South
resets.

The checked-in model is generated offline from the pinned XML descriptions in
[`upstream/`](upstream/). Conversion preserves mass, centre of mass and full
inertia tensors by rotating each body into its principal axes and transforming
its joint frames consistently. Simulation builds need no Python, MuJoCo or
asset download. [`UPSTREAM.md`](UPSTREAM.md) records provenance, licensing and
regeneration instructions.

The visual duck is drawn from original primitive shapes. Pollen's STL meshes
have a separate noncommercial license and are not included. The imported XML
and the derived model use the upstream Apache-2.0 license.

Each duck keeps a bounded ring of 64 transitions in ordinary match state:

- Simulation tick.
- Observation: 14 joint angles, followed by 14 joint velocities.
- Action: 14 desired joint angles.
- Effort: 14 clamped actuator controls.
- Next observation in the same order, and a joint-target tracking reward.

Records describe executed simulation states and follow snapshots and rollback.
An exporter should consume verified ticks in order; this fixture does not export
a dataset or record the exact rendered observation a human used to choose an
action. Its bounded PD controller is illustrative and does not implement
Pollen's BAM servo model or run their pretrained walking policies.

For a quiet, bounded desktop capture, save a script such as:

```text
3 key escape
12 key e
20 key d down
65 capture duck-moving.png
85 key d up
```

Then run with `TIDE_HIDDEN=1` in the agent's shell:

```sh
tide run examples/robotics/microduck --script duck.txt --frames 100 --size 1280 720 --capture duck-final.png
```

Repository CMake targets are `newton_microduck` and `tide_newton_microduck`.
The latter tests the actual generated game, recording, reset, replay and
multiplayer behavior, plus converted dynamics against stock Newton using the
original full inertia tensors.
