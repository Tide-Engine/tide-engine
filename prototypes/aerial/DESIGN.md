# Aerial: what it aims at

The owner asked on 2026-10-10 for a second Rocket League prototype, better
than the first and as close to the real game as could be made, in how it
plays and how it looks. Afterburn, the first, is a duel between two points
that follow the ground, on tuning of its own. Aerial takes the other road:
the sport's published numbers, on a car that's a rigid body. Its name, its
bots, its look and everything the sources don't give are the agent's choices.

## What "close" means here

Rocket League's feel comes from a few things together, and each is built as
the game is known to do it, not as something that looks like it from a
distance:

- **A car is a box on four springs.** It has a mass, a moment about each
  axis, a spin and a quaternion. Each wheel casts a ray down its spring; the
  springs hold the body up, the tyres grip sideways by how much they slide,
  and nothing snaps the car to a surface. So ramps, landings, half-landed
  cars, wall rides and falling off a ceiling are what the wheels make of the
  arena.
- **The ball goes where the car's middle points.** A hit is the contact, then
  a second push along the line from the car's centre to the ball's, flattened
  and leaned along the nose, by how fast the two met. That's what makes a
  dodge a shot and a roof a dribble.
- **The air is torque.** Pitch, yaw and roll are angular accelerations with
  their own damping, a dodge is a shove with a spin that pitch can cancel,
  and boost pushes along the nose.
- **The pitch is the standard one**: its size, its 45 degree corners, its
  goals, its 34 pads where they are, the five kickoff places and the four
  places a wrecked car comes back.

## Where the numbers come from

Checked on 2026-10-10 against the RLBot wiki's
[Useful game values](https://wiki.rlbot.org/v4/botmaking/useful-game-values/)
and [Jumping physics](https://wiki.rlbot.org/v4/botmaking/jumping-physics/),
at a hundredth of their units (a metre is 100 of the game's):

- The arena's walls, ceiling, goals and corner planes; the ball's radius, top
  speed, restitution, drag, spin limit and mass.
- A car's top speeds (14.1 m/s on wheels, 23 with boost, 22 for supersonic),
  boost use and thrust on the ground and in the air, braking and coasting,
  its spin limit, its mass and its three angular accelerations in the air.
- The jump's kick, its push while held and for how long, the hold on the
  ground after it, and the second jump's window.
- All 34 pads, what they give, how long they take and how near a car has to
  be; the kickoff places and the respawn places.

The model wasn't tuned to these three, and comes out at them: a car at rest
sits 0.171 m up (the wiki: 0.1701); flat out on full lock it settles at
12.0 m/s on a circle of 5.0 m (the wiki: 12.34 and 5.3), and at 23 m/s on one
of 11.5 m (11.4).

Held to RocketSim, the open reimplementation of the game's physics, by two
fixtures another session built it for and ran (its numbers and how they were
made are in `prototypes/rocket-league/RESEARCH.md`; its license is in
`THIRD_PARTY_NOTICES.md`):

- **A shot in the air.** A car flying level at 10 m/s meets a ball falling at
  1 m/s. Four ticks on, the ball leaves 1.6 cm/s from RocketSim's 14.59 m/s,
  and touches on the same tick.
- **A powerslide.** From 10 and 15 m/s with a touch of throttle, full lock and
  the handbrake held, where the car is, its velocity, its heading and its yaw
  rate after half a second and a whole one are within 0.08 m, 0.15 m/s,
  0.02 rad and 0.04 rad/s.

Neither was reached by tuning to it: the hitbox became RocketSim's (1.205 by
0.867 by 0.387 m, the box with its collision margin) and the hit's contact is
settled with its friction over a few rounds, and the numbers followed.

From memory of RocketSim's constants, and **not checked against a source in
this work** beyond those two fixtures: where the wheels are, the springs'
stiffness and damping, the steering angle by speed, the tyres' grip by slide,
what a powerslide does to it, the dodge's shove and spin and their timings,
the hit's second push and its curve, the bump and its curves, righting an
overturned car. The ball's bounce is the model usually credited to Chip, with
its 0.285 grip.

The controls are the game's own two keyboard and mouse layouts, Classic and
Modern, as the same research read them from the game's files on this machine,
with its keyboard aerial safety. The camera is the game's widest: 110 degrees
across a 16:9 screen (77.55 up and down, kept on every screen), 2.7 m back,
1 m up, tilted down 3 degrees.

## Where it isn't the game

- **The arena's curves are guessed.** The ramps are quarter pipes of 2.56 m
  top and bottom, and the diagonal walls round into the straight ones over
  4 m. The game's own meshes aren't used, and its goals are rounder inside.
- **Under the wheels, the box is a bump stop.** Where a wheel is on the
  ground and the box touches the same surface, the contact has no friction
  and no bounce. Without it a ramp taken head on at full speed cost over half
  the car's speed; with it, about a third. How much the game costs there
  wasn't measured.
- **Car against car is corners in boxes**, not two boxes against each other,
  and two cars arriving at the ball together settle it over three rounds in
  an order that turns each tick.
- **No replays, no spectating, no camera settings, no quick chat, no
  customising.** One stadium, one car, one ball.
- **The bots are simple.** They read where the ball will be, line up a shot,
  dodge into it, jump for it, take turns going and hold back, and fetch
  boost. They don't fly, dribble or pass on purpose.

## How it looks

The stadium by day: a mown pitch with its lines, glass walls in a honeycomb
that the stands and the sky show through, ramps and goals lit in each side's
color, a crowd that gets to its feet, floodlights and screens. The car is a
short buggy with big wheels, arches, a glazed cabin and a wing, in its side's
paint under a clear coat. The HUD is the sport's: the score either side of
the clock, a boost dial, the countdown, a goal's banner with who and how
fast, names over cars, a ring on the grass under the ball.
