# Afterburn

A Rocket League inspired car-soccer prototype for Tide. The requested target
is the familiar sport and its feel: blue versus orange, a large bouncing ball,
rocket-powered cars, a walled stadium and an uninterrupted chase camera.
The working title, vehicle, stadium, materials and sound are original.

## Play target

One car per side keeps every touch, save and recovery meaningful in a small
prototype. Solo play fills the other seat with a deterministic bot. A second
player takes that seat through Tide's usual host/join room flow. Five-minute
matches, kickoffs after goals and sudden-death overtime provide a complete
round rather than a physics sandbox.

The movement target includes acceleration, braking/reverse, speed-dependent
steering, powerslides, limited boost with field pickups, variable-height jumps,
a second jump or directional dodge, aerial pitch/yaw/roll and wall driving.
Car and ball state belongs to the match, including collisions and scores;
cameras, geometry, lighting, particles and sounds remain local presentation.
There are no engine or compiler additions.

## Reference and interpretation

Epic's [Rocket League crash course](https://www.epicgames.com/help/c-182/c-7261959845275/a13334808?lang=en-US)
describes held-jump height, aerial boost, air-roll recovery, directional dodges,
stronger dodge shots, softer wheel contacts and quicker handbrake turns.
Those are the behavior targets for the driving model. Psyonix's
[beginner tips](https://www.rocketleague.com/news/five-tips-for-taking-your-game-from--potato--to--pro-?lang=en)
motivate ball camera, forward-dodge momentum, kickoff positioning and wall
shots. Both were consulted on 2026-10-09.

These sources describe player behavior, not a published physics implementation.
The prototype's equations, dimensions, collision response, bot and tuning are
implementation choices. It does not reproduce proprietary physics or use
extracted game assets. Exact competitive handling and the original game's
advanced contact exploits are outside the verified scope.

## Presentation target

A striped green pitch with white field markings, blue/orange recessed goals,
curved perimeter ramps and a transparent cage establishes the sport at a glance.
Sporty cars have shaped bodywork, glass, wheels, lights and exhaust; the ball
has contrasting panels. Seating, floodlights and the city beyond the stadium
give the match a sense of scale. Perspective, smooth camera tracking, lighting,
shadows, antialiasing and restrained glow aim for the clean stadium look rather
than the retro treatment of the horror prototypes.

## Prototype boundaries

This is a playable duel, with a single stadium and vehicle. Team playlists,
ranked matchmaking, garage/customization, licensed cosmetics, match replays
and the commercial game's exact suspension/contact model are not built.
Native and web behavior, deterministic snapshots and both web renderers are
checked locally; physical gamepad/phone feel and human competitive tuning need
playtesting.
