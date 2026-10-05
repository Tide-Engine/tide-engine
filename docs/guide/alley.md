# Alley

Bowling with far too many pins, a wall of crates behind them, and [Box3D](../physics/box3d.md) under all of it. A toy more than a game, made with Tide to show its 3D physics in a match: every pin is in the same place on every machine, whatever their pings, for up to eight players throwing at once.

Below are two machines in one match. Point down the lane in a frame and hold the button, or your finger, to throw that machine's balls.

::: tide-game alley Alley
:::

[Open it on a page of its own](/alley/), which is also where friends on other machines join you: see [Play with friends](#play-with-friends).

## Play

| Action | Mouse and keyboard | Touchscreen |
|---|---|---|
| Aim | Move the pointer | Touch where to aim |
| Throw | Hold the left button, or Space | Keep the finger down |
| Spin | The **Spin** slider, or `A` / `D` | The **Spin** slider |
| Low gravity, and back | **Float** and **Heavy**, or `G` | The same buttons |
| Clear the lane | **Sweep**, or `S` | **Sweep** |
| A new game | **Menu**, then **Reset the game**, or `R` | The same |

Your place on the line is the one with the ball waiting, and the line on the lane is the way it will go.

- **A ball paints what it hits your color,** and what a painted pin knocks into takes the color on, unless someone's ball got to it first.
- **What goes off the lane's far end scores,** for whoever painted it last: one point, or five for a kingpin, the three with a round head that stand on the wall. What falls off the sides scores for nobody.
- **Spin makes a ball curve.** Set it before you throw: the ball leaves spinning around the way it goes, and the lane's grip does the rest.
- **Float lets gravity go.** Everything hops, and comes down slowly: a ball then sends pins a long way. **Heavy** puts it back.
- **Sweep sends the bar down the lane,** from behind you, with whatever's on the lane in front of it. It needs a few seconds before it goes again.
- **A round is over once next to nothing is left.** The lane is set up again, and scores carry on until someone resets the game.

The bottom right corner shows the frame rate, and your ping in someone else's match. Click or tap it, or press `F8`, for the rest of what the engine is doing: the tick, the entities, what goes over the network and the systems that take longest. That's the game's [`stats`](../tools/cli.md#stats) setting.

In this page, a swipe on a frame scrolls the page until you tap the game, and again once you tap the page somewhere else. The game [on a page of its own](/alley/) has every touch from the start.

## Two machines, and their ping

Each frame above is a machine of its own: the first hosts the match in a [room](../multiplayer/matches.md#rooms), and the second joins it, as two browsers anywhere would.

Only the players' inputs go over the network: where each points, the spin they've set, and whether they're throwing. Every machine runs the same Box3D from them, and gets the same pins, bit for bit (see [Determinism](../multiplayer/determinism.md)).

The **Ping** slider is the round trip between the second machine and the first. Raise it, and throw in the second frame: its own ball still leaves at once, because the machine [predicts](../multiplayer/networking.md#prediction-and-rollback) the ticks the server hasn't confirmed yet, physics and all. The other player's balls arrive late, so for a moment the two machines disagree about a pin or two. When the server's answer comes, the machine goes back to the tick it guessed wrong, runs the ticks since again, and the pins it had drawn in the wrong place glide to the right one. Nothing in the game's code does any of that.

## Play with friends

A match takes eight players, on the web and on desktop alike: they meet in a room, by its code.

1. **Host.** Open the game [on a page of its own](/alley/), press **Menu** and then **Invite friends**. The room's code shows in the menu and under the scores, like `K7QF2M`. **Copy the code** puts it on the clipboard.
2. **Share** the code, or a link that has it: the game's address with `?join=` and the code, like `https://tide-engine.dev/alley/?join=K7QF2M`. Whoever opens the link is in your match at once.
3. **Join.** With only the code, a friend opens the game, presses **Menu**, types or pastes the code into the field (Ctrl+V, or Cmd+V) and presses **Join**.

From the repo, with [`tide`](./install.md) installed, the same match runs in a window:

```sh
cd examples/alley
tide run --host          # A match of your own, open: its code shows in the game
tide run --join K7QF2M   # A friend's
```

- **Each player has a place on the line and a color of their own,** and a score beside the others' at the top. A ninth is turned away.
- **Joining in the middle works.** The match's world is sent as it is, with Box3D's whole state in it: every pin where it is and as fast, the ones in the air too.
- **When the host leaves, the match goes on.** Another player's machine takes it over, with the pins where they were (the game's [`hostMigration`](../multiplayer/matches.md#host-migration) setting).
- **Alone again.** A match that ended, or a code nobody has, puts you back in a match of your own.

## The code

The game is `examples/alley` in the [repo](https://github.com/BlenMiner/tide-engine/tree/dev/examples/alley): two files of Tide, and a `tide.packages` that says `box3d`.

- `game.tide` is the match, which every machine runs the same way. It's on this page, [whole](#the-match-whole).
- `play.tide` is what one machine shows of it: the camera, the meshes and their light, the scores and the menu.

Nothing in the game moves a pin. It spawns bodies, pushes them and reads where they are, all through components, as the [physics page](../physics/box3d.md) says:

**Three kinds of bodies, and every shape.** The lane is [static](../physics/box3d.md#bodies), the bar is kinematic and goes where its `velocity` takes it, and everything else is dynamic. A pin is a `Capsule` on a small `Box` for a foot, so that it stands, and a kingpin has a `Sphere` for a head too: [three shapes on one body](../physics/box3d.md#shapes), each at its `offset`.

```csharp
Spawn(Body { position = float3((i - 1) * 1.64, 2 * CRATE + PIN_HEIGHT / 2, WALL), angularDamping = 0.3 },
      Capsule { radius = PIN_RADIUS, height = PIN_HEIGHT, density = 330 },
      Box { size = float3(0.3, 0.06, 0.3), offset = float3(0, 0.03 - PIN_HEIGHT / 2, 0), density = 600 },
      Sphere { radius = 0.21, offset = float3(0, PIN_HEIGHT / 2 + 0.1, 0), density = 200 },
      Target { worth = 5 }, Loose { mass = PIN_MASS + 8 });
```

**A ball is spawned with two impulses.** One down the lane, and an angular one: the roll it would have by then, and the spin its player set, around the way it goes. Box3D gives it both at its first step. It's heavy and fast, so it's a `bullet`, and its `gravityScale` keeps it on the lane when gravity is low:

```csharp
var roll = Math.Cross(float3(0, 1, 0), way) * (BALL_SPEED / BALL);
var hook = way * (-controls.spin * HOOK);
Spawn(Body { position = Stand(bowler.seat) + way * 0.7, impulse = way * (BALL_SPEED * BALL_MASS),
             angularImpulse = (roll + hook) * BALL_INERTIA, gravityScale = 1.5, angularDamping = 0.05,
             bullet = true },
      Sphere { radius = BALL, density = BALL_DENSITY, friction = 0.9, bounce = 0.1 }, Ball { seat = bowler.seat });
```

**Who painted what is a `Touched` event.** The package sends it to a body's entity when it starts [touching](../physics/box3d.md#touching) another. The goal is a [sensor](../physics/box3d.md#sensors), a box past the lane's end that things fall through, so the same event says a target went into it:

```csharp
event(Touched touched) Paint(mut Target target, mut Lane lane, World world)
{
    if (world.Has<Goal>(touched.other))
    {
        if (target.counted) return;
        target.counted = true;
        lane.through += 1;
        if (lane.bowlers.TryGet(target.last, out var bowler)) bowler.Send(Scored { worth = target.worth });
        return;
    }
    if (world.Has<Ball>(touched.other))
    {
        target.last = world.Get<Ball>(touched.other).seat;
        this.Send(Struck { });
    }
    else if (target.last < 0 && world.Has<Target>(touched.other)) target.last = world.Get<Target>(touched.other).last;
}
```

The goal itself counts what's in it, with `Touched` as a body comes in and `Parted` as it leaves, and lights up meanwhile.

**Gravity is the world's.** The **Float** button is a [command](../game/commands.md), and a system writes what it asked into the package's `Physics`, which wakes every body that had come to rest. The tick after, everything loose gets an impulse up, from a system that's [`Before` the package's stage](../physics/box3d.md#before-the-step), so the step that gives it is that tick's:

```csharp
[Before(PhysicsStages.Step)]
system Hop(mut Body body, Loose loose, Lane lane)
{
    if (!lane.hop) return;
    body.impulse += float3(0, 2.6 * loose.mass, 0);
    body.angularImpulse += float3(0.02, 0, 0.03) * loose.mass;
}
```

**The bar is back where it waits at once,** by having its position written, which is how code [moves a body](../physics/box3d.md#moving-a-body) all at once. `Snap` has views draw it there too, rather than slide it back up the lane. And what falls far enough is destroyed, which takes its body with it.

The rest is the engine's:

- **A player is an entity with an `Owner`,** spawned as they join: the `Throw` system takes the game's [input](../game/input.md), and gets the input of the player who owns the bowler it runs for. Where the pointer is on the lane is worked out by a view, which has the camera: a ray through its pixel, down to the height a ball rolls at. `Sample` puts it in the input.
- **There's no lighting built in,** so everything is a mesh whose corners have a normal, made once in a local singleton, and drawn through a [frag](../game/shaders.md) that lights each pixel from it. A target's frag reads its entity's component for its color, and a ball's gives it a white band, so that its spin shows.
- **The scores are read [`verified`](../game/views.md#confirmed-state):** what the server has confirmed, never a pin this machine only predicted falling. They're on the bowlers, so the view goes through them with a [query](../game/views.md#other-entities).
- **A target flashes when a ball hits it.** The match says so with an event, and a [local handler](../game/events.md#local-handlers) sets a [local field](../game/local-state.md#local-fields) of the target, which its frag reads and its view lets fade.
- **Views see every body between its last two ticks,** and gliding through a correction, because the package tags a `Body`'s `position` and `rotation` for it (see [correction smoothing](../game/views.md#provisional-correction-smoothing)).

### What it costs

With three players throwing there are about a hundred bodies, most of them in a pile, and a tick takes between 0.1 and 0.2 ms on a desktop, with 0.05 more to keep and hash its world, and not quite twice that in a browser, where Box3D has no SIMD. A machine that predicts runs a few ticks again each frame at a high ping, which this leaves room for: past what a frame can spare, the engine [delays the player's input](../multiplayer/matches.md#input-delay) a tick at a time instead. Box3D's whole state is in the match, a few hundred kilobytes here, so a rollback, a player who joins and a new host all go on from it (see [Multiplayer](../physics/box3d.md#multiplayer)).

### The match, whole

<<< @/../examples/alley/game.tide
