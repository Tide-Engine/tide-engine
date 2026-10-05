# Siege

Two towers of crates over a pit, four cannons around them, and [Box2D](../physics/box2d.md) under all of it. A toy more than a game, made with Tide to show its 2D physics in a match: every crate is in the same place on every machine, whatever their pings.

Below are two machines in one match. Point at something in a frame and hold the button, or your finger, to fire that machine's cannon.

::: tide-game siege Siege
:::

[Open it on a page of its own](/siege/), which is also where friends on other machines join you: see [Play with friends](#play-with-friends).

## Play

| Action | Mouse and keyboard | Touchscreen |
|---|---|---|
| Aim | Move the pointer | Touch where to aim |
| Fire | Hold the left button, or Space | Keep the finger down |
| A new game | **Reset**, or `R` | **Reset** |

Your cannon is the one with the ring, and the dots show where its next shot goes. The further away you point, the harder it fires.

- **A shot shoves everything near where it lands,** and paints the crates there your color. A crate that falls off the map scores for whoever painted it last: one point, or five for a tower's king, the piece with the round head.
- **The orb in the middle turns gravity over** for a few seconds when one of your shots goes through it. Everything falls up to the ceiling, and comes down again, mostly somewhere else. It then rests for a while: the arc around it says how long.
- **The bridge goes back and forth over the pit,** and stops the shots it's in the way of. The three bumpers float where they are until something sends them off.
- **A shot that leaves by one side of the map comes back in by the other,** once. A shot that missed the far tower may hit the near one from behind.
- **Cannons nobody has play by themselves:** a couple of shots now and then, at a tower. **Menu** has a button to stop them. They never turn gravity over.
- **A round is over once a dozen pieces are left on the map.** It's whoever's scored most in it, and the next one is built. Scores carry on until someone presses **Reset**.

The bottom right corner shows the frame rate, and your ping in someone else's match. Click or tap it, or press `F8`, for the rest of what the engine is doing: the tick, the entities, what goes over the network and the systems that take longest. That's the game's [`stats`](../tools/cli.md#stats) setting.

In this page, a swipe on a frame scrolls the page until you tap the game, and again once you tap the page somewhere else. The game [on a page of its own](/siege/) has every touch from the start.

## Two machines, and their ping

Each frame above is a machine of its own: the first hosts the match in a [room](../multiplayer/matches.md#rooms), and the second joins it, as two browsers anywhere would. The other two cannons are nobody's, and play by themselves.

Only the players' inputs go over the network: where each points, and whether they're firing. Every machine runs the same Box2D from them, and gets the same crates, bit for bit (see [Determinism](../multiplayer/determinism.md)).

The **Ping** slider is the round trip between the second machine and the first. Raise it, and fire in the second frame: its own cannon still answers at once, because the machine [predicts](../multiplayer/networking.md#prediction-and-rollback) the ticks the server hasn't confirmed yet, physics and all. What the other player does arrives late, so for a moment the two machines disagree about a crate or two. When the server's answer comes, the machine goes back to the tick it guessed wrong, runs the ticks since again, and the crates it had drawn in the wrong place glide to the right one. Nothing in the game's code does any of that.

## Play with friends

A match takes four players, one for each cannon, on the web and on desktop alike: they meet in a room, by its code.

1. **Host.** Open the game [on a page of its own](/siege/), press **Menu** and then **Invite friends**. The room's code shows in the menu and over the buttons, like `K7QF2M`. **Copy the code** puts it on the clipboard.
2. **Share** the code, or a link that has it: the game's address with `?join=` and the code, like `https://tide-engine.dev/siege/?join=K7QF2M`. Whoever opens the link is in your match at once.
3. **Join.** With only the code, a friend opens the game, presses **Menu**, types or pastes the code into the field (Ctrl+V, or Cmd+V) and presses **Join**.

From the repo, with [`tide`](./install.md) installed, the same match runs in a window:

```sh
cd examples/siege
tide run --host          # A match of your own, open: its code shows in the game
tide run --join K7QF2M   # A friend's
```

- **A player takes the first cannon nobody has,** and goes on from the score its seat had. A fifth is turned away.
- **Joining in the middle works.** The match's world is sent as it is, with Box2D's whole state in it: every crate where it is and as fast, the ones in the air too.
- **When the host leaves, the match goes on.** Another player's machine takes it over, with the crates where they were (the game's [`hostMigration`](../multiplayer/matches.md#host-migration) setting). The cannon the host had plays by itself from then.
- **Alone again.** A match that ended, or a code nobody has, puts you back in a match of your own.

## The code

The game is `examples/siege` in the [repo](https://github.com/BlenMiner/tide-engine/tree/dev/examples/siege): two files of Tide, and a `tide.packages` that says `box2d`.

- `game.tide` is the match, which every machine runs the same way. It's on this page, [whole](#the-match-whole).
- `play.tide` is what one machine shows of it: the camera, the pieces, the scores and the menu.

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

The rest is the engine's:

- **Whose cannon is whose** is the built-in `Owner` component: the `Fire` system takes the game's [input](../game/input.md), and gets the input of the player who owns the cannon it runs for. The pointer's place on the map is worked out by a view, which has the camera, and `Sample` puts it in the input.
- **Reset and the bots' switch are [commands](../game/commands.md),** sent by the buttons and handled in the match.
- **The scores are read [`verified`](../game/views.md#confirmed-state):** what the server has confirmed, never a crate this machine only predicted falling. They're on the cannons, so the view goes through them with a [query](../game/views.md#other-entities).
- **A crate flashes when a shot lands on it.** The match says so with an event, and a [local handler](../game/events.md#local-handlers) sets a [local field](../game/local-state.md#local-fields) of the crate, which the view lets fade. Each machine hears it once, however often it runs the tick again.
- **Views see every body between its last two ticks,** and gliding through a correction, because the package tags a `Body`'s `position` and `angle` for it (see [correction smoothing](../game/views.md#provisional-correction-smoothing)).

### What it costs

With all four cannons firing there are about a hundred bodies, and a tick takes around 0.04 ms on a desktop, with half as much again to keep and hash its world, and not quite twice that in a browser. A machine that predicts runs a few ticks again each frame at a high ping, which this leaves plenty of room for. Box2D's whole state is in the match, about 100 KB here, so a rollback, a player who joins and a new host all go on from it (see [Multiplayer](../physics/box2d.md#multiplayer)).

### The match, whole

<<< @/../examples/siege/game.tide
