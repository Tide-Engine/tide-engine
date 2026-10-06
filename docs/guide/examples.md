# Live examples

Many pages of these docs have a small game beside the code they explain. The source shown is the whole program, built for the web as it is, and it runs in the page. An example starts when you scroll to it and stops once it's well off screen, so only what you're looking at runs. **Restart** starts it over, and **Stop** ends it until you press **Run** again.

The page scrolls over an example: the wheel always does, and on a touchscreen so does a swipe that starts on one. Click or tap an example to play it. It then has the keyboard, and on a touchscreen your swipes too, until you click or tap the page somewhere else; its frame is outlined meanwhile.

Some examples put two players side by side. Each frame is a machine of its own, and the two meet in a [room](../multiplayer/matches.md#rooms), as two browsers anywhere would.

## Two machines

- [Two players, in one match](./first-game.md#play-together): the game from the first tutorial, hosted by one frame and joined by the other.
- [Open a room, and join it](../multiplayer/matches.md#rooms): a lobby to work yourself. Start a match, open it, join it by its code, close it, kick, leave.
- [Predicted, and confirmed by the server](../multiplayer/networking.md#prediction-and-rollback): what a machine predicts beside what the server has confirmed, with a slider for the second player's ping.
- [Whose input a system reads](../game/input.md#who-gets-which-input): `Owner`, traded between two players, and a body nobody owns.
- [A command, and a handler that says no](../game/commands.md#handling): an action sent into the match, and the handler that checks who sent it.

## The match

- [Spawn, Add, Remove and Destroy](../game/entities.md#changing-entities): entities and their components, changed by taps.
- [Component filters in motion](../game/systems.md#parameters-are-the-query): which entities a system runs for.
- [Events, and what a machine shows of them](../game/events.md#local-handlers): events sent to an entity and to the world, with the match's handlers and local ones.
- [Rooms that load and unload](../game/scenes.md#loading-and-unloading): scenes, and the entities that go with them.
- [A countdown, and a dash](../game/tasks.md): async handlers that wait.
- [An input, sampled and read](../game/input.md): the devices, the input and the system that reads it.
- [Heat that spreads](../language/grids.md#going-through-every-cell): a grid, and a parallel loop over its cells.

## Physics

- [Crates, balls and a floor](../physics/box2d.md#a-first-world): Box2D's bodies as components, with crates to kick and balls to drop.
- [A wall of crates, and balls to throw](../physics/box3d.md#a-first-world): the same with Box3D, through a 3D camera.
- [Siege](./siege.md): a whole game on Box2D, with a lobby and rooms for four: towers, cannons, a sensor that turns gravity over.
- [Sumo](./sumo.md): a whole game on Box3D, with a lobby and rooms for eight: heavy balls that ram each other out of a ring stacked with two thousand crates, while its floor falls away.

## What a machine shows

- [A view draws the world](../game/views.md): cameras, and screen and world coordinates.
- [Five ticks a second, drawn every frame](../game/views.md#smooth-at-any-tick-rate): what views see between ticks.
- [Smooth shapes](../game/views.md#smooth-shapes): round rects, ellipses, arcs, outlines and soft edges.
- [Anchors, sizes and weights](../game/views.md#measuring-text): text, where it's placed and how big it is.
- [A grid, drawn as a texture](../game/views.md#textures): a `Grid2<Color>` to paint on.
- [Primitives through a 3D camera](../game/views.md#primitives): cubes, a sphere, a cylinder and a plane.
- [A scene through a frag](../game/views.md#targets-and-post-processing): a render target, blitted to the window.
- [A frag and a vert](../game/shaders.md): code of the game's on the GPU.
- [Lighting, in a frag](../game/shaders.md#where-the-pixel-is): a vertex with a normal, lit pixel by pixel.
- [A list of motes, in one draw](../game/shaders.md#instances-from-a-list): thousands of instances from a list.
- [Local entities, and no match](../game/local-state.md#views-change-local-state): particles that are one machine's own.

## The GUI

- [Try the controls](../game/gui.md#guilayout-and-gui): the widgets, and what they change.
- [Layout and modal focus](../game/gui.md#modals): rows, columns and a modal.
- [A theme around a block](../game/gui.md#themes): the GUI's colors and corners.
- [Cards that are controls](../game/gui.md#widgets-of-your-own): widgets a view draws itself.

## The language

- [Curves from Math](../language/math.md#functions): a few of `Math`'s functions, plotted.
- [The same seed, the same numbers](../language/math.md#seeds): a `Random` and its seed.
- [A list, changed and sorted](../language/text-and-lists.md#sorting): a list's members, a button each.

[Falling sand](./sand.md), [Duel](./duel.md), [Siege](./siege.md) and [Sumo](./sumo.md) are whole games, to play here too.
