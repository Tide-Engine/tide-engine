# Newton arms

Two articulated arms use the experimental `newton` source package. Geometry
is visual only: there are no colliders, contacts, robot importers or trainer.
The installed CLI builds the same CPU kernels for native and web games;
Python and Warp are not needed to build or run this example.

From the repository root, with an installed `tide`:

```sh
tide build examples/robotics/arm
tide build examples/robotics/arm --web
```

Rooms initially shows its title menu. Escape closes it and enables controls;
Escape during play opens it again. A/D choose negative/positive shoulder
targets, S/W choose negative/positive elbow targets, and the gamepad's left
stick adds to those inputs. Each axis is clamped to `[-1, 1]`, then multiplied
by 1.2 radians. These are desired angles, not angle increments. Each player
controls their own arm.

For a bounded native capture, save this as `arm-capture.txt` in the repository
root:

```text
3 key escape
10 key d down
10 key w down
45 capture arm-active.png
100 key d up
100 key w up
```

Then run:

```sh
tide run examples/robotics/arm --script arm-capture.txt --frames 120 --size 960 540 --capture arm-final.png
```

The script uses 60 frames per simulated second; physics runs at 120 ticks per
second. Agent runs set `TIDE_HIDDEN=1` in their shell. The capture path above
uses the hidden standard host and opens no audio device.

Each arm keeps up to 128 executed transitions in match state. Before physics,
the controller records shoulder/elbow angles and angular velocities and
converts input into desired angles. Shoulder control requests
`30 * angleError - 5 * angularVelocity`; elbow control requests
`20 * angleError - 3 * angularVelocity`. Actuator limits clamp those controls
to 30 Nm and 20 Nm respectively.

After a successful step, a transition stores the simulation tick, pre-step
joint state, desired angles, clamped actuator controls, post-step joint state,
and reward: negative squared distance from the simulated endpoint to its
target. Failed steps add no transition. The recorded effort excludes passive
damping and limit forces; it is not a torque sensor. Recording continues
behind the Rooms menu. This bounded ring is not an exported demonstration
dataset and does not record when a human observed the state. Exporting
demonstrations would need verified ticks and an explicit association between
the observation used to choose an action and that action.
