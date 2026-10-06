# Siege

Two towers of crates over a pit, four cannons around them, and [Box2D](../physics/box2d.md) under all of it. A small game made with Tide to show its 2D physics in a match: every crate is in the same place on every machine, whatever their pings. Play it alone against three bots, or with friends in a room.

<iframe class="demo-frame roomy" src="/siege/?embedded" title="Siege" allow="fullscreen"></iframe>

[Open it on a page of its own](/siege/), which is the one to send to friends: see [Play with friends](#play-with-friends).

## Play

The game opens on its lobby, with the bots shelling the towers behind it. **Play** starts a game of your own against them, **Host a room** opens one for friends, and **Join a room** takes a friend's code.

| Action | Mouse and keyboard | Touchscreen |
|---|---|---|
| Aim | Move the pointer | Touch where to aim |
| Fire | Hold the left button, or Space | Keep the finger down |
| The menu | **Menu**, or Escape | **Menu** |
| A new game | `R`, or **New game** in the menu | **New game** in the menu |

Your cannon is the one with the ring and **YOU** on it, and your score is the one marked at the top. The dots show where your next shot goes: the further away you point, the harder it fires.

- **A shot shoves everything near where it lands,** and paints the crates there your color. A crate that falls off the map scores for whoever painted it last, and what it was worth floats up from where it went: one point, or five for a tower's king, the piece with the crown.
- **The orb in the middle turns gravity over** for a few seconds when one of your shots goes through it. Everything falls up to the ceiling, and comes down again, mostly somewhere else. It then rests for a while: the arc around it says how long.
- **The bridge goes back and forth over the pit,** and stops the shots it's in the way of. The three bumpers float where they are until something sends them off.
- **A shot that leaves by one side of the map comes back in by the other,** once. A shot that missed the far tower may hit the near one from behind.
- **Cannons nobody has play by themselves:** a couple of shots now and then, at a tower. The menu has a button to stop them. They never turn gravity over.
- **A round is over once a dozen pieces are left on the map.** It's whoever's scored most in it, and the next one is built. Scores carry on until someone starts a new game, and whoever leads wears the crown.

The bottom right corner shows the frame rate, and your ping in someone else's room. Click or tap it, or press `F8`, for the rest of what the engine is doing: the tick, the entities, what goes over the network and the systems that take longest. That's the game's [`stats`](../tools/cli.md#stats) setting.

In this page, click the game to give it the keyboard. On a touchscreen, a swipe on it scrolls the page until you tap the game, and again once you tap the page somewhere else. The game [on a page of its own](/siege/) has every touch from the start.

## Play with friends

A room takes four players, one for each cannon, on the web and on desktop alike. They meet by the room's code, and nobody waits for anybody: whoever arrives is in the game, at the next free cannon.

1. **Host.** On the lobby, press **Host a room**. The room's code shows at once, like `K7QF2M`, with the four seats under it and who has each. **Copy** puts the code on the clipboard, and **Play** takes you to your cannon.
2. **Share** the code, or a link that has it: the game's address with `?join=` and the code, like `https://tide-engine.dev/siege/?join=K7QF2M`. Whoever opens the link is in your room, with no lobby in the way.
3. **Join.** With only the code, a friend opens the game, presses **Join a room**, types or pastes the code (Ctrl+V, or Cmd+V) and presses **Join**. A code nobody has, a room that's full and a match that ended each say so, on the page the code is typed on, to try again from.

In a game, **Menu** shows the room again: its code to copy, who's in it, and **Leave the room**, which takes you back to the lobby and a game of your own. A game you started alone has **Invite friends** there instead, which opens a room around the game as it is.

From the repo, with [`tide`](./install.md) installed, the same game runs in a window, and joins the same rooms:

```sh
cd examples/siege
tide run                 # The lobby
tide run --host          # Straight into a room of your own: its code shows by the Menu button
tide run --join K7QF2M   # Straight into a friend's
```

- **A player takes the first cannon nobody has,** and goes on from the score its seat had. A fifth is turned away, and told the room is full.
- **Joining in the middle works.** The match's world is sent as it is, with Box2D's whole state in it: every crate where it is and as fast, the ones in the air too.
- **When the host leaves, the room goes on.** Another player's machine takes the match over, with the crates where they were (the game's [`hostMigration`](../multiplayer/matches.md#host-migration) setting). The cannon the host had plays by itself from then.
- **A match that ends puts you back on the lobby,** with a line that says what happened, and the bots at the towers behind it.

Only the players' inputs go over the network: where each points, and whether they're firing. Every machine runs the same Box2D from them, and gets the same crates, bit for bit (see [Determinism](../multiplayer/determinism.md)). Your own cannon answers at once however far away the host is, because your machine [predicts](../multiplayer/networking.md#prediction-and-rollback) the ticks the host hasn't confirmed yet, physics and all, and puts right what it guessed wrong about the others.

## The code

The game is `examples/siege` in the [repo](https://github.com/BlenMiner/tide-engine/tree/dev/examples/siege): five files of Tide, and a `tide.packages` that says `box2d`.

- `game.tide` is the match, which every machine runs the same way. It's on this page, [whole](#the-match-whole).
- `play.tide` is what one machine shows of it: the camera and the pieces.
- `effects.tide` has the sparks, the debris and the numbers that float up, and `hud.tide` the scores and the words over the map.
- `lobby.tide` is the lobby and the menu: which match this machine is in. It's on this page too, [whole](#the-lobby-whole).

### The physics

Nothing in the game moves a crate. It spawns bodies, pushes them and reads where they are, all through components, as the [physics page](../physics/box2d.md) says:

**Three kinds of bodies.** The islands and the ceiling are [static](../physics/box2d.md#bodies), the bridge is kinematic and goes where its `velocity` takes it, and everything else is dynamic. A cannon's body and a king's each have [two shapes](../physics/box2d.md#shapes), a `Box` and a `Circle` at an `offset`. The bumpers float with `gravityScale = 0`, and their damping stops them soon after a shot sent them off.

**A shot is spawned with an impulse.** Box2D gives it to the body at its first step, with a `turn` for some spin. It's small and fast, so it's a `bullet`: checked against the crates all along its way.

```csharp
Spawn(Body { position = pivot + cannon.aim * MUZZLE, impulse = cannon.aim * (cannon.power * BALL_MASS),
             turn = cannon.seat % 2 == 0 ? -0.4 : 0.4, angularDamping = 0.4, bullet = true },
      Circle { radius = BALL, density = BALL_DENSITY, bounce = 0.25 }, Ball { seat = cannon.seat, manned = manned });
```

**Where it lands is a `Touched` event.** The package sends it to a body's entity when it starts [touching](../physics/box2d.md#touching) another. A shot's first one leaves a `Blast` there, and tells the crate it hit:

```csharp
event(Touched touched) Land(mut Ball ball, Body body, World world)
{
    if (ball.landed || world.Has<Orb>(touched.other)) return;
    ball.landed = true;
    Spawn(Blast { at = body.position, seat = ball.seat });
    if (world.Has<Crate>(touched.other)) touched.other.Send(Struck { seat = ball.seat });
}
```

**The blast shoves what's near, before the step.** A system that writes an `impulse` into bodies, for each `Blast` in its first tick. It's [`Before` the package's stage](../physics/box2d.md#before-the-step), so the step that gives the impulses is this tick's:

```csharp
[Before(PhysicsStages.Step)]
system Shove(mut Body body, Loose loose, query (Blast blast) blasts)
{
    foreach (var entry in blasts)
    {
        if (entry.blast.age != 0) continue;
        var away = body.position - entry.blast.at;
        var distance = Math.Length(away);
        if (distance > REACH) continue;
        var push = (1 - distance / REACH) * SHOVE * loose.mass;
        body.impulse += (distance > 0.05 ? away / distance : float2(0, 1)) * push;
        body.turn += (away.x > 0 ? -0.12 : 0.12) * push;
    }
}
```

**The orb is a sensor.** Shots pass through it, and it hears `Touched` for what comes in and `Parted` for what leaves (see [Sensors](../physics/box2d.md#sensors)). A player's shot starts the countdown, and a system writes the world's gravity, which wakes every body that had come to rest:

```csharp
[After(PhysicsStages.Step)]
system Turn(mut Siege siege, mut Physics physics)
{
    if (siege.flip > 0) siege.flip -= 1;
    var up = siege.flip > 0;
    if (up != (physics.gravity.y > 0)) physics.gravity = float2(0, up ? 7 : -10);
}
```

**A shot wraps around the map by having its position written,** which is how code [moves a body](../physics/box2d.md#moving-a-body) all at once. `Snap` has views draw it there too, rather than slide it across the map. And a crate that's off the map is destroyed, which takes its body with it.

### The lobby

The lobby isn't a scene, and nothing of it is in the match. `Main` is the match's scene, so a match of this machine's own starts with the program, closed, and the lobby is a [local singleton](../game/local-state.md) and a view drawn over it. Its three ways in are three [session calls](../multiplayer/matches.md#starting-a-match-from-code), or none:

- **Play** closes the lobby. The match is there already.
- **Host a room** opens that match to others, with `Session.Open()`, and `Session.room` is the code to show.
- **Join a room** leaves it for someone else's, with `Session.Join(code)`.

A machine that was told which match to be in has no lobby in its way. With `--host` or `--join` on its command line, or `?host` or `?join=` in its page's address, the session is open or joining by the first frame, and the lobby starts closed:

```csharp
if (!lobby.started)
{
    lobby.started = true;
    lobby.joining = !session.server && session.state == SessionState.Connecting;
    lobby.page = lobby.joining ? Page.Join : session.server && session.open ? Page.None : Page.Title;
}
```

While a page is up, the player's hands are off the cannon: the [input](../game/input.md)'s `Sample` reads a local singleton the lobby writes, and sends nothing to fire with. A match that ends, or a room that can't be joined, comes back as the built-in `Disconnected` event, whose handler starts a match of this machine's own again and leaves a line for the lobby to show:

```csharp
local event(Disconnected gone) BackToLobby(mut Lobby lobby)
{
    if (gone.reason == DisconnectReason.Left) return;
    lobby.message = gone.reason == DisconnectReason.Failed ? (lobby.joining ? "No room has that code." : "Couldn't reach the room.")
                  : gone.reason == DisconnectReason.Refused ? "That room is full, or closed."
                  : gone.reason == DisconnectReason.TimedOut ? (lobby.joining ? "That room didn't answer." : "Lost the connection to the room.")
                  : gone.reason == DisconnectReason.Kicked ? (gone.message != "" ? gone.message : "The host sent you out of the room.")
                  : "The match ended.";
    lobby.started = true;
    lobby.page = lobby.joining ? Page.Join : Page.Title;
    lobby.joining = false;
    Session.Start(Main);
}
```

Its buttons are [controls](../game/gui.md#widgets-of-your-own): the game draws them, and the GUI handles their input, so the pointer, a finger, the keyboard and a gamepad all work them. The field for a room's code is the GUI's own, in a [theme](../game/gui.md#themes) that matches.

### What a machine shows

- **Whose cannon is whose** is the built-in `Owner` component: the `Fire` system takes the game's [input](../game/input.md), and gets the input of the player who owns the cannon it runs for. The pointer's place on the map is worked out by a view, which has the camera, and `Sample` puts it in the input.
- **A new game and the bots' switch are [commands](../game/commands.md),** sent by the menu's buttons and handled in the match.
- **The scores are read [`verified`](../game/views.md#confirmed-state):** what the server has confirmed, never a crate this machine only predicted falling. They're on the cannons, so the view goes through them with a [query](../game/views.md#other-entities).
- **The sparks, the debris and the camera's shake come from events.** The match says what happens in it: a shot landed, a crate left the map, gravity turned over, a round ended. Nothing in the match handles most of those, so they [cost it nothing](../game/events.md#events-only-local-code-handles), and each machine's [local handlers](../game/events.md#local-handlers) make an effect of them, once, however often it runs the tick again. The numbers that float up and a round's winner wait for the server, with [`verified` handlers](../game/events.md#verified-handlers).
- **A crate flashes when a shot lands on it,** and a shot leaves a tail: both are [local fields](../game/local-state.md#local-fields) of the match's own components, which a handler or a view writes, and which are no part of the match.
- **Views see every body between its last two ticks,** and gliding through a correction, because the package tags a `Body`'s `position` and `angle` for it (see [correction smoothing](../game/views.md#provisional-correction-smoothing)).

### What it costs

With all four cannons firing there are about a hundred bodies, and a tick takes around 0.04 ms on a desktop, with half as much again to keep and hash its world, and not quite twice that in a browser. A machine that predicts runs a few ticks again each frame at a high ping, which this leaves plenty of room for. A frame's views, effects and all, take about 0.02 ms, less than a tick does. Box2D's whole state is in the match, about 100 KB here, so a rollback, a player who joins and a new host all go on from it (see [Multiplayer](../physics/box2d.md#multiplayer)).

### The match, whole

<<< @/../examples/siege/game.tide

### The lobby, whole

<<< @/../examples/siege/lobby.tide
