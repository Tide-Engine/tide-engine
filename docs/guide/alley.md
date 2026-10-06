# Alley

Bowling with far too many pins, a wall of crates behind them, and [Box3D](../physics/box3d.md) under all of it. A small game made with Tide to show its 3D physics in a match: every pin is in the same place on every machine, whatever their pings, for up to eight players throwing at once. Play it alone, or with friends in a room.

<iframe class="demo-frame roomy" src="/alley/?embedded" title="Alley" allow="fullscreen"></iframe>

[Open it on a page of its own](/alley/), which is the one to send to friends: see [Play with friends](#play-with-friends).

## Play

The game opens on its lobby, with the lane set up behind it. **Play** starts a game of your own, **Host a room** opens one for friends, and **Join a room** takes a friend's code.

| Action | Mouse and keyboard | Touchscreen |
|---|---|---|
| Aim | Move the pointer | Touch where to aim |
| Throw | Hold the left button, or Space | Keep the finger down |
| Spin | The **Spin** slider, or `A` / `D` | The **Spin** slider |
| Low gravity, and back | **Float** and **Heavy**, or `G` | The same buttons |
| Clear the lane | **Sweep**, or `S` | **Sweep** |
| The menu | **Menu**, or Escape | **Menu** |
| A new game | `R`, or **New game** in the menu | **New game** in the menu |

The dots on the lane are the way your next ball will go, bent by the spin you've set, and the ring is where you point. Your score is the one marked at the top.

- **A ball paints what it hits your color,** and what a painted pin knocks into takes the color on, unless someone's ball got to it first.
- **What goes off the lane's far end scores,** through the gate of light, for whoever painted it last: one point, or five for a kingpin, the three with a gold head that stand on the wall. What it was worth floats up from the gate. What falls off the sides scores for nobody.
- **Spin makes a ball curve.** Set it before you throw: the ball leaves spinning around the way it goes, and the lane's grip does the rest.
- **Float lets gravity go.** Everything hops, and comes down slowly: a ball then sends pins a long way. **Heavy** puts it back.
- **Sweep sends the bar down the lane,** from behind you, with whatever's on the lane in front of it. It needs a few seconds before it goes again, which its button shows.
- **A round is over once next to nothing is left.** It's whoever's scored most in it, and the lane is set up again. Scores carry on until someone starts a new game, and whoever leads wears the crown.

The bottom right corner shows the frame rate, and your ping in someone else's room. Click or tap it, or press `F8`, for the rest of what the engine is doing: the tick, the entities, what goes over the network and the systems that take longest. That's the game's [`stats`](../tools/cli.md#stats) setting.

In this page, click the game to give it the keyboard. On a touchscreen, a swipe on it scrolls the page until you tap the game, and again once you tap the page somewhere else. The game [on a page of its own](/alley/) has every touch from the start.

## Play with friends

A room takes eight players, on the web and on desktop alike. They meet by the room's code, and nobody waits for anybody: whoever arrives is in the game, at the next free place on the line.

1. **Host.** On the lobby, press **Host a room**. The room's code shows at once, like `K7QF2M`, with the eight seats under it and who has each. **Copy** puts the code on the clipboard, and **Play** takes you to the lane.
2. **Share** the code, or a link that has it: the game's address with `?join=` and the code, like `https://tide-engine.dev/alley/?join=K7QF2M`. Whoever opens the link is in your room, with no lobby in the way.
3. **Join.** With only the code, a friend opens the game, presses **Join a room**, types or pastes the code (Ctrl+V, or Cmd+V) and presses **Join**. A code nobody has, a room that's full and a match that ended each say so, on the page the code is typed on, to try again from.

In a game, **Menu** shows the room again: its code to copy, who's in it, and **Leave**, which takes you back to the lobby and a game of your own. A game you started alone has **Invite friends** there instead, which opens a room around the game as it is.

From the repo, with [`tide`](./install.md) installed, the same game runs in a window, and joins the same rooms:

```sh
cd examples/alley
tide run                 # The lobby
tide run --host          # Straight into a room of your own: its code shows by the Menu button
tide run --join K7QF2M   # Straight into a friend's
```

- **Each player has a place on the line and a color of their own,** and a score beside the others' at the top. A ninth is turned away, and told the room is full.
- **Joining in the middle works.** The match's world is sent as it is, with Box3D's whole state in it: every pin where it is and as fast, the ones in the air too.
- **When the host leaves, the room goes on.** Another player's machine takes the match over, with the pins where they were (the game's [`hostMigration`](../multiplayer/matches.md#host-migration) setting).
- **A match that ends puts you back on the lobby,** with a line that says what happened.

Only the players' inputs go over the network: where each points, the spin they've set, and whether they're throwing. Every machine runs the same Box3D from them, and gets the same pins, bit for bit (see [Determinism](../multiplayer/determinism.md)). Your own ball leaves at once however far away the host is, because your machine [predicts](../multiplayer/networking.md#prediction-and-rollback) the ticks the host hasn't confirmed yet, physics and all, and puts right what it guessed wrong about the others.

## The code

The game is `examples/alley` in the [repo](https://github.com/BlenMiner/tide-engine/tree/dev/examples/alley): six files of Tide, and a `tide.packages` that says `box3d`.

- `game.tide` is the match, which every machine runs the same way. It's on this page, [whole](#the-match-whole).
- `play.tide` is what one machine shows of it: the camera, and the lane and what's on it.
- `light.tide` has the meshes and the frags that light them, `effects.tide` the sparks and the numbers that float up, and `hud.tide` the scores, the words over the lane and the controls.
- `lobby.tide` is the lobby and the menu: which match this machine is in.

### The physics

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
event(Touched touched) Paint(mut Target target, Body body, mut Lane lane, World world)
{
    if (world.Has<Goal>(touched.other))
    {
        if (target.counted) return;
        target.counted = true;
        lane.through += 1;
        if (lane.bowlers.TryGet(target.last, out var bowler)) bowler.Send(Scored { worth = target.worth });
        Send(Through { at = body.position, seat = lane.bowlers.Has(target.last) ? target.last : -1, worth = target.worth });
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

`Scored` goes to the bowler, whose handler counts it. `Through` and `Struck` are for whoever shows the match: nothing in it handles them (see [What a machine shows](#what-a-machine-shows)). The goal itself counts what's in it, with `Touched` as a body comes in and `Parted` as it leaves.

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

### The lobby

The lobby isn't a scene, and nothing of it is in the match. `Main` is the match's scene, so a match of this machine's own starts with the program, closed, and the lobby is a [local singleton](../game/local-state.md) and a view drawn over it. **Play** closes the lobby, as the match is there already, **Host a room** opens that match to others with `Session.Open()`, and **Join a room** leaves it for someone else's with `Session.Join(code)`.

A machine that was told which match to be in has no lobby in its way. With `--host` or `--join` on its command line, or `?host` or `?join=` in its page's address, the session is open or joining by the first frame, and the lobby starts closed:

```csharp
if (!lobby.started)
{
    lobby.started = true;
    lobby.joining = !session.server && session.state == SessionState.Connecting;
    lobby.page = lobby.joining ? Page.Join : session.server && session.open ? Page.None : Page.Title;
}
```

While a page is up, the player's hands are off the ball: the [input](../game/input.md)'s `Sample` reads a local singleton the lobby writes, and sends nothing to throw with. A match that ends, or a room that can't be joined, comes back as the built-in `Disconnected` event, whose handler starts a match of this machine's own again and leaves a line for the lobby to show. [Siege](./siege.md#the-lobby) has the same lobby, with its file whole.

Its buttons, and the game's own **Spin** slider, are [controls](../game/gui.md#widgets-of-your-own): the game draws them, and the GUI handles their input, so the pointer, a finger, the keyboard and a gamepad all work them. A control says while the pointer holds it, wherever the pointer goes meanwhile, which is all a slider needs:

```csharp
var slider = GUI.Control(rect);
if (slider.pressed)
{
    var along = (Screen.Flip(devices.pointer.position).x - start) / Math.Max(span, 1) * 2 - 1;
    hand.spin = Math.Abs(along) < 0.08 ? 0 : Math.Clamp(along, -1, 1);
    fx.spun = true;
}
```

### What a machine shows

- **A player is an entity with an `Owner`,** spawned as they join: the `Throw` system takes the game's [input](../game/input.md), and gets the input of the player who owns the bowler it runs for. Where the pointer is on the lane is worked out by a view, which has the camera: a ray through its pixel, down to the height a ball rolls at. `Sample` puts it in the input.
- **There's no lighting built in,** so everything is a mesh whose corners have a normal, made once in a local singleton, and drawn through a [frag](../game/shaders.md) that lights each pixel from it: a lamp, a cooler light from the other side, a shine where the lamp reflects, and the dark of the room for what's far. A pin's frag reads its entity's component for its color, and a ball's gives it a white band, so that its spin shows.
- **Shadows are discs on the lane.** Each view adds one to a list for the thing it draws, where the lamp's light would leave it, and the marks that show a ball's way go in the same list. One [`Draw3D.Instanced`](../game/shaders.md#instances-from-a-list) draws them all, through a frag that fades each at its edge and leaves out what's past the lane's sides.
- **The scores are read [`verified`](../game/views.md#confirmed-state):** what the server has confirmed, never a pin this machine only predicted falling. They're on the bowlers, so the view goes through them with a [query](../game/views.md#other-entities).
- **The sparks, the goal's light and the camera's shake come from events.** The match says what happens in it: a ball hit a target, something went through the goal, gravity let go, the bar set off, a round ended. Nothing in the match handles those, so they [cost it nothing](../game/events.md#events-only-local-code-handles), and each machine's [local handlers](../game/events.md#local-handlers) make an effect of them, once, however often it runs the tick again. The numbers that float up and a round's winner wait for the server, with [`verified` handlers](../game/events.md#verified-handlers).
- **A target flashes when a ball hits it,** which is a [local field](../game/local-state.md#local-fields) of the target: the handler sets it, the frag reads it and the view lets it fade.
- **Views see every body between its last two ticks,** and gliding through a correction, because the package tags a `Body`'s `position` and `rotation` for it (see [correction smoothing](../game/views.md#provisional-correction-smoothing)).

### What it costs

With three players throwing there are about a hundred bodies, most of them in a pile, and a tick takes around 0.2 ms on a desktop, with 0.06 more to keep and hash its world, and not quite twice that in a browser, where Box3D has no SIMD. A machine that predicts runs a few ticks again each frame at a high ping, which this leaves room for: past what a frame can spare, the engine [delays the player's input](../multiplayer/matches.md#input-delay) a tick at a time instead. A frame's views, shadows and sparks included, take about 0.04 ms, less than a tick does. Box3D's whole state is in the match, a few hundred kilobytes here, so a rollback, a player who joins and a new host all go on from it (see [Multiplayer](../physics/box3d.md#multiplayer)).

### The match, whole

<<< @/../examples/alley/game.tide
