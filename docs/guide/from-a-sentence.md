# Games from one sentence

Each game on this page was written by an AI agent from the one sentence above it.

The agent was Claude, Anthropic's model. Tide is newer than what the model was trained on, so it had never seen the language. It was given the pages of this site as files, a compiler, and a way to run a game with no window and look at a picture of it. It was told to make a small multiplayer game that needs no lobby, and to keep going until it had played the game and it was what the sentence asked for.

They're here as a test of Tide more than of the agent:

- **The docs were what it learned Tide from.** Nine agents wrote about 900 lines each, in about 25 minutes. Of the 127 times they built their games, 6 failed, and four of the nine never had a build fail at all.
- **None of them wrote networking, and none ever had a second player.** An agent could only run its game alone. The second frame beside each game here is another machine, joined through a room as two browsers anywhere would be, and the first time anyone joined was when the game was put on this page. What makes that work is in [Networking](../multiplayer/networking.md): a game's code says what happens in a tick, and the engine keeps every machine's match the same.
- **What they stumbled on got fixed.** Each agent ended with a list of what had cost it time. Three engine bugs came out of those lists, and so did corrections to these pages.

The source under each game is the file as the agent left it. Nobody edited one to make it better. They'll change where the language does, and no further.

Each pair of frames is one match: the first hosts it and the second joins. Click a frame so it has the keyboard. Every game also takes a mouse or a finger.

## Pong

> pong but for two players online

::: tide-match prompt-pong Pong
<<< @/examples/prompt-pong.tide
:::

`W` and `S` or the arrows move your paddle, and so does the mouse or a finger. First to 7. Alone, you play the computer; a third player waits in line for the loser's seat.

## Snake

> multiplayer snake, everyone on the same board, last one alive wins

::: tide-match prompt-snake Snake
<<< @/examples/prompt-snake.tide
:::

Arrows or `WASD` to steer, or swipe. A round starts once two snakes are in, and the last one alive takes it.

## Tanks

> top down tanks that shoot bouncing bullets at each other

::: tide-match prompt-tanks Tanks
<<< @/examples/prompt-tanks.tide
:::

`WASD` or the arrows drive, the mouse aims, and a click or Space shoots. On a touchscreen, drag to drive and tap where to shoot. Bullets bounce three times, off you too. Bots fill the arena until players join.

## Bomberman

> bomberman with friends

::: tide-match prompt-bomberman Bomberman
<<< @/examples/prompt-bomberman.tide
:::

Arrows or `WASD` to walk, Space for a bomb; or drag to walk and tap for a bomb. Crates hide power-ups, ghosts are worth a point and a rival three. First to 10.

## Connect four

> connect four online

::: tide-match prompt-connect-four Connect four
<<< @/examples/prompt-connect-four.tide
:::

Click or tap a column, or pick one with the arrows and drop with Space. Alone, you play a bot; a friend who joins takes the other color.

## Asteroids

> asteroids but co-op with my friends

::: tide-match prompt-asteroids Asteroids
<<< @/examples/prompt-asteroids.tide
:::

Arrows or `WASD` to fly, Space to fire; or drag, which steers and fires. Everyone shares one score. A ship that's down drifts until a friend flies over it.

## Sumo

> sumo, we bump each other out of the ring

::: tide-match prompt-sumo Sumo
<<< @/examples/prompt-sumo.tide
:::

`WASD` or the arrows, or press and hold where to go. Space dashes, which hits harder and can carry you out. First to 5.

## Tower defense

> tower defense where me and a friend place towers together

::: tide-match prompt-tower-defense Tower defense
<<< @/examples/prompt-tower-defense.tide
:::

Click a tile to build and a tower to upgrade it; `1`, `2` and `3` pick the tower. Lives are shared, and so is the gold from kills.

## Marbles

> a 3D game where we roll marbles around and collect coins

::: tide-match prompt-marbles Marbles
<<< @/examples/prompt-marbles.tide
:::

`WASD` or the arrows to roll, or hold the mouse or a finger where to go. First to 20 coins. There are no walls, and a fall drops three of yours.

## How they were made

Every agent got the same instructions and a sentence. The instructions asked for one file with no packages, no asset files and no C; a match that players join and leave while it runs, with something for each of them to control and a game that still makes sense alone; keys, and a mouse or a finger; a layout that works in a small frame and a wide window; and a line on screen that says how to play.

For tools it had two commands: one that built the game and printed what the compiler said, and one that ran it as `tide run` does [without a window](../tools/cli.md#without-a-window), from a [script](../tools/cli.md#scripts) of input, and wrote pictures of it. It had no way to start a second player.

::: info Not a clean room
The agents ran inside a session in the engine's own repository, which put that repository's notes for contributors in front of them along with their instructions. They were told to take nothing about Tide from those notes and to read only this site's pages, and what they reported not knowing suggests they did. It's still not the same as an agent that never saw them.
:::
