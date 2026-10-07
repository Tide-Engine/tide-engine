---
description: "Explore a deterministic 2D multiplayer physics game in Tide with Box2D, falling bricks, player input, a room lobby and drawing."
---

# Towers walkthrough

This walkthrough explains how a Box2D match steers falling bricks and turns them into loose parts of a tower. Follow its [physics](#the-physics), [input](#the-input), [lobby](#the-lobby) and [presentation](#what-a-machine-shows), with code excerpts and the match and lobby sources.

Four plinths side by side, bricks that come down one at a time, and a race to build the tallest tower, with [Box2D](../physics/box2d.md) under all of it. A game made with Tide to show its 2D physics in a match: a brick that has landed is a body like any other, so towers lean, slide and come down, the same on every machine, whatever their pings. Play it alone against three towers that build themselves, or with friends in a room. The idea is Tricky Towers', the party game of stacking falling bricks.

<iframe class="demo-frame roomy" src="/towers/?embedded" title="Towers" allow="fullscreen"></iframe>

[Open it on a page of its own](/towers/), which is the one to send to friends: see [Play with friends](#play-with-friends).

## Play

The game opens on its lobby, with all four towers building by themselves behind it. **Play** starts a game of your own against three of them, **Host a room** opens one for friends, and **Join a room** takes a friend's code.

| Action | Keyboard | Mouse or finger |
|---|---|---|
| Move the brick | Left and right, or `A` and `D` | Drag sideways |
| Turn it | Up, or `W` | Tap |
| Drop it | Hold down, `S` or Space | Swipe down and hold, or press and hold still |
| Use what you earned | `E` | The button at the bottom |
| The menu | **Menu**, or Escape | **Menu** |

Your tower is the one on the plinth that says **YOU**, your brick is the one with the white edge, and the lanes under it run down to its outline, where it will land. The row along the top says how high every tower stands, and the bar down the left side shows the race: a mark for each tower, yours the big one, and the finish line at the top.

- **A brick falls where you steer it,** half a cell at a time, and turns by quarters, wherever there's room for it. Where it touches down it's a real brick: it has weight, it leans and it slides. Then the next one comes. **NEXT** shows which.
- **The first tower to stand over the finish line for three seconds takes the round.** The line is 40 meters up. Everything then comes down at once, and the next round starts from nothing. Rounds won are the dots under a plinth's name.
- **A brick that falls off your tower costs you time:** your next brick waits longer, up to three seconds when a lot of them go at once. What fell lies where it landed for ten seconds, and is cleared.
- **Every brick of a tower is loose,** from the plinth up: nothing holds a tower together but how it was built. One that leans low down leans all the way up, and can go from anywhere.
- **A tower earns something at 10, 20 and 30 meters,** which the dotted lines across your plinth mark. A **gust** shoves the top of the tallest of the other towers away from yours. **Cement** sets every brick of your tower solid: they turn gray, and won't move again this round, whatever lands on them or shakes them. What you build on top of them is loose again. A **quake** shakes every plinth but yours. You hold one at a time, so use it before the next line.
- **A round that goes on long ends anyway:** after a minute and a half the finish line starts coming down, a meter every four seconds, until a tower is over it.
- **Towers nobody steers build by themselves,** each a little more or less careful than the next, and use what they earn. The menu has a button to stop them.

The bottom right corner shows the frame rate, and your ping in someone else's room. Click or tap it, or press `F8`, for the rest of what the engine is doing: the tick, the entities, what goes over the network and the systems that take longest. That's the game's [`stats`](../tools/cli.md#stats) setting.

In this page, click the game to give it the keyboard. On a touchscreen, a swipe on it scrolls the page until you tap the game, and again once you tap the page somewhere else. The game [on a page of its own](/towers/) has every touch from the start.

## Play with friends

A room takes four players, one for each plinth, on the web and on desktop alike. They meet by the room's code, and nobody waits for anybody: whoever arrives is in the race, on the next free plinth.

1. **Host.** On the lobby, press **Host a room**. The room's code shows at once, like `K7QF2M`, with the four seats under it and who has each. **Copy** puts the code on the clipboard, and **Play** takes you to your tower.
2. **Share** the code, or a link that has it: the game's address with `?join=` and the code, like `https://tide-engine.dev/towers/?join=K7QF2M`. Whoever opens the link is in your room, with no lobby in the way.
3. **Join.** With only the code, a friend opens the game, presses **Join a room**, types or pastes the code (Ctrl+V, or Cmd+V) and presses Enter, or **Join**. A code nobody has, a room that's full and a match that ended each say so, on the page the code is typed on, to try again from.

In a game, **Menu** shows the room again: its code to copy, who's in it, and **Leave the room**, which takes you back to the lobby and a game of your own. A game you started alone has **Invite friends** there instead, which opens a room around the game as it is. While your menu is up, your tower builds by itself.

From the repo, with [`tide`](./install.md) installed, the same game runs in a window, and joins the same rooms:

```sh
cd examples/towers
tide run                 # The lobby
tide run --host          # Straight into a room of your own: its code shows over the Menu button
tide run --join K7QF2M   # Straight into a friend's
```

- **A player takes the first plinth nobody has,** with the tower that built itself there, and its wins. A fifth is turned away, and told the room is full.
- **Joining in the middle of a round works.** The match's world is sent as it is, with Box2D's whole state in it: every brick where it is and as fast, the ones in the air too. A plinth that's well under the other players' towers rises until it's level with the lowest of them, so whoever joins late is in the race.
- **When the host leaves, the room goes on.** Another player's machine takes the match over, with the towers as they were (the game's [`hostMigration`](../multiplayer/matches.md#host-migration) setting). The tower the host had builds by itself from then.
- **A match that ends puts you back on the lobby,** with a line that says what happened, and four towers building behind it.

Only the players' inputs go over the network: where each wants their brick, how turned, and whether faster. Every machine runs the same Box2D from them, and gets the same towers, bit for bit (see [Determinism](../multiplayer/determinism.md)). Your own brick answers at once however far away the host is, because your machine [predicts](../multiplayer/networking.md#prediction-and-rollback) the ticks the host hasn't confirmed yet, physics and all, and puts right what it guessed wrong about the others.

## The code

Towers has five files of Tide, and a `tide.packages` that says `box2d` and `rooms`.

- `game.tide` is the match, which every machine runs the same way. It's on this page, [whole](#the-match-whole).
- `bots.tide` is how a tower nobody steers gets built: a plan for each brick, from how high the tower stands in each column.
- `play.tide` is what one machine shows of it: the hand on the brick, the camera and the pieces.
- `effects.tide` has the dust, the pieces and the words that float up, and `hud.tide` the race, the next brick and the words over the world.
- The lobby and the menu are the `rooms` package's, [the menu that comes with tide](../multiplayer/matches.md#a-menu-that-comes-with-tide): which match this machine is in is its to say.

### The physics

Everything that's solid is a body of Box2D's, made, moved and read through components, as the [physics page](../physics/box2d.md) says. A brick is one entity and one body from the moment it's dealt to the end of the round. What changes is what kind of body it is, and which shapes it has.

**A falling brick is a kinematic body with no shapes.** It goes where its `velocity` takes it, which is down, and where its `position` and `angle` are written, which is how a player steers it: half a cell across, a quarter turn. With no shape it touches nothing, and nothing touches it. What's in its way, it asks:

```csharp
float Free(Physics physics, Kind kind, float2 from, int quarter, float2 way, float2 slack)
{
    mut var free = 1.0;
    var angle = Angle(quarter);
    if (Cast(physics, Slim(First(kind), quarter, slack), from, angle, from + way, out var hit)) free = hit.fraction;
    var second = Second(kind);
    if (second.size.x > 0 && Cast(physics, Slim(second, quarter, slack), from, angle, from + way, out var other))
    {
        free = Math.Min(free, other.fraction);
    }
    return free;
}
```

[`Cast`](../physics/box2d.md#asking-the-world) moves a box along a way through the world, and says how much of the way it went before it touched something. The seven bricks are four cells each, which is one box for the bar and the square and two for the rest, so each box is moved, and the brick is free to go as far as the first of them. `Slim` makes a box a little smaller than it is: a shape that's cast counts as touching what it's flush with, and a brick has to slide down beside a wall it only just clears.

**The system that steers it asks three things each tick.** Is there room for the brick turned the way its player wants, which is the package's `Overlap`? Is it free to step half a cell across? And is it free to fall as far as this tick takes it?

```csharp
if (falling.quarter != falling.wantQuarter && Room(physics, piece.kind, body.position, falling.wantQuarter))
{
    falling.quarter = falling.wantQuarter;
    body.angle = Angle(falling.quarter);
}
if (falling.column != falling.wantColumn && falling.age % 2 == 0)
{
    var step = falling.wantColumn > falling.column ? 1 : -1;
    if (Free(physics, piece.kind, body.position, falling.quarter, float2(step * 0.5, 0), float2(SLACK)) >= 1)
    {
        falling.column += step;
        body.position.x = falling.home + falling.column * 0.5;
    }
}
var speed = fast ? FAST : FALL;
var fall = speed * time.dt;
var free = Free(physics, piece.kind, body.position, falling.quarter, float2(0, -fall), float2(SLACK, 0));
if (free >= 1)
{
    if (body.velocity.y != -speed) body.velocity = float2(0, -speed);
    return;
}
```

A turn or a step there's no room for doesn't happen, so a brick is never inside a wall and nothing is undone. The system is [`Before` the package's stage](../physics/box2d.md#before-the-step): the world it asks is the world as the last tick's step left it, which is where `body.position` says the brick is, and the step that moves the brick is this tick's. `controls` is the input of the player who owns the brick.

**Where it isn't free to fall, it lands: the same body is a dynamic one, with its shapes on it.**

```csharp
body.position.y -= fall * free;
body.velocity = float2(0, 0);
body.type = BodyType.Dynamic;
var first = First(piece.kind);
var second = Second(piece.kind);
this.Add(Box { size = first.size, offset = first.offset, friction = GRIP }, Brick { seat = falling.seat, born = time.tick });
if (second.size.x > 0) Spawn(Box { size = second.size, offset = second.offset, friction = GRIP }, Part { of = this });
this.Remove(Falling, Owner);
```

It's put where it touches, at rest, however fast it fell: it was never in the tower, so nothing pushes it out, and a landing doesn't hit what's under it. Writing `type` is [all it takes](../physics/box2d.md#changing-what-a-body-is) to change what a body is. Its first box is a `Box` on its own entity. An entity has one `Box`, so a brick of two gets the second as a [`Part`](../physics/box2d.md#more-shapes-on-a-body): an entity of its own that names the brick, whose box is on the brick's body. From the next step the brick is Box2D's: it has weight, it rests on what's under it, and it goes over if that's not enough.

**Where it will land is the same question, asked by a view.** The outline under your brick is where `Free` says it stops, looking 60 meters down. A [view asks the world](../physics/box2d.md#what-they-see) like the match does, of the world it draws:

```csharp
var fall = SIGHT * Free(physics, piece.kind, from, falling.quarter, float2(0, -SIGHT), float2(SLACK, 0));
```

**The plinths are kinematic bodies,** which go where their `velocity` takes them, whatever stands on them. A quake is a plinth going there and back four times. A plinth rises under a player who joined late, and when a round is over the plinths sink, the winner's last, and everything on them comes down.

**A tower is loose bricks all the way down.** With the world as Box2D comes, forty meters of them is a spring: a contact gives a little, and every landing ran down the whole tower and back. So the game tunes the world, which is [three fields of `Physics`](../physics/box2d.md#the-world):

```csharp
physics.substeps = SUBSTEPS;          // 8, where it comes with 4
physics.contactStiffness = STIFFNESS; // 60, where it comes with 30
physics.contactDamping = DAMPING;     // 40, where it comes with 10
```

Contacts are twice as stiff, which takes twice the substeps to count, and come back out four times as gently. A tower of loose bricks then stands at forty meters, and what's wrong with it low down still shows at the top.

**Cement is a body's type again.** Every brick at rest in the tower that uses it becomes a kinematic body, and goes where its plinth does from then on: the system that looks at every brick before the step gives it the plinth's velocity, so a quake carries it with what's built on it. When the round is over each is a dynamic body again, from the bottom of its tower up, and all of it comes down.

```csharp
mut var solid = body;
solid.type = BodyType.Kinematic;
world.Set<Body>(this, solid);
brick.set = true;
```

**A gust is an impulse.** That same system gives the loose bricks near the top of the tower a gust is against an `impulse` each tick it blows, more the nearer the top. It looks at every brick and writes a few, so it takes `Body` to read and puts the ones it changed back [through the World](../game/systems.md#other-entities-the-world):

```csharp
if (Of(towers.gusts, lot) <= 0 || Math.Abs(body.position.x - Home(lot)) > PLINTH) return;
var depth = Of(towers.tops, lot) - Peak(body.position, body.angle, piece.kind);
if (depth > WIND_DEPTH) return;
mut var pushed = body;
pushed.impulse.x += Of(towers.winds, lot) * WIND * (1 - Math.Max(depth, 0) / WIND_DEPTH);
world.Set<Body>(this, pushed);
```

**How hard things meet is the package's to say.** [`Hit`](../physics/box2d.md#hits) is sent to both bodies that meet faster than the world's `hitSpeed`, which the game puts at 2.5 meters a second, with where and how fast. Nothing in the match handles it. Each machine makes pieces fly of it, more the harder it was: a brick that goes over the side and meets the ground, a tower coming down.

```csharp
local event(Hit hit) Crash(Brick brick, mut Fx fx)
{
    var hard = Math.Saturate((hit.speed - CRASH) / 12);
    Chunks(fx, hit.point, 1 + int(hard * 3), 1.5 + hard * 4, SeatColor(brick.seat));
    if (brick.seat == fx.seat && !brick.down) Shake(fx, 0.03 + hard * 0.1);
}
```

**How high a tower stands is read from its bricks:** a system that runs for each plinth goes through every brick with a [query](../game/systems.md#another-set-of-entities), and takes the highest corner of the ones at rest over it. A brick that's under the plinths' tops, or well to one side of every plinth, is off its tower: it's marked, whoever put it there waits longer, and a few seconds later its entity is destroyed.

### The input

A player's [input](../game/input.md) isn't which keys they hold. It's where they want their brick, how turned, and whether faster:

```csharp
input Controls
{
    [Absolute] bool hands;      // No lobby or menu is up on this machine
    [Absolute] int serial;
    [Absolute, Clamp(-REACH, REACH)] int column;
    [Absolute, Clamp(0, 3)] int quarter;
    bool drop;
    ...
}
```

A view works those out a frame at a time, from the keys, the mouse and fingers, into a [local singleton](../game/local-state.md), and the input's `Sample` reads it. Saying where, not what, has three things going for it:

- **A tap is never lost.** A turn is "a quarter further than it was", which the next tick's input still says, where a button that was down for less than a tick might not.
- **A guess stays put.** A machine that runs ahead of the others guesses their inputs, and after 0.15 seconds takes them to have let go. These fields are marked [`[Absolute]`](../game/input.md#what-other-machines-guess), so the guess is that the other players' bricks stay on their way to where they were going.
- **One brick's steering isn't the next one's.** `serial` is the falling brick's number. The match only steers a brick by an input that names it, so the brick that's dealt as the last one lands doesn't start off to where the last one was sent.

### The lobby

The lobby isn't the game's: it's the `rooms` package, [the menu that comes with tide](../multiplayer/matches.md#a-menu-that-comes-with-tide), which the game lists in its `tide.packages`. `Main` is the match's scene, so a match of this machine's own starts with the program, closed, and the menu is drawn over it: **Play** closes it, **Host a room** opens that match to others, and **Join a room** leaves it for someone else's. A machine that was told which match to be in, with `--host` or `--join` on its command line or `?host` or `?join=` in its page's address, has no menu in its way.

What the game adds is in `hud.tide`. While a page is up, the player's hands are off their brick: the input reads the menu's `Lobby.playing` and says so (`hands`), and the match builds their tower for them, as it does a tower nobody has. And a view tells the room's page about the match, who has each seat and whether bots play the rest, and sends the game's commands when the page's **New game** and **Bots** buttons say so:

```csharp
view Roster(Towers towers, Session session, mut Lobby lobby)
{
    lobby.seats = SEATS;
    lobby.manned = 0;
    lobby.mine = -1;
    foreach (var entry in towers.seats)
    {
        lobby.manned |= 1 << entry.value;
        if (entry.key == session.player) lobby.mine = entry.value;
    }
    lobby.bots = towers.bots;
    lobby.offersNewGame = true;
    lobby.offersBots = true;
    if (lobby.newGame) Send(Rebuild { });
    if (lobby.toggleBots) Send(Bots { });
}
```

### What a machine shows

- **Whose tower is whose** is the built-in `Owner` component, on a plinth and on the bricks it's dealt: the system that steers a falling brick takes the game's input, and gets the input of the player who owns the brick it runs for. A view picks this machine's player's with [`with ownership`](../game/views.md#your-own-entities).
- **Using what a tower earned, a new game and the bots' switch are [commands](../game/commands.md),** sent by buttons and handled in the match.
- **The row of heights is read [`verified`](../game/views.md#confirmed-state):** what the server has confirmed. They're on the plinths, so the view goes through them with a [query](../game/views.md#other-entities).
- **The dust, the pieces and the camera's shake come from events.** The match says what happens in it: a brick landed, one fell off, a tower earned something or used it, a round ended. The package says when two bodies meet hard. Nothing in the match handles those, so they [cost it nothing](../game/events.md#events-only-local-code-handles), and each machine's [local handlers](../game/events.md#local-handlers) make an effect of them, once, however often it runs the tick again. A round's winner waits for the server, with a [`verified` handler](../game/events.md#verified-handlers).
- **A falling brick slides across and swings round,** where the match steps it half a cell and turns it a quarter at once: where it's drawn is two [local fields](../game/local-state.md#local-fields) of the match's own component, which its view eases. A brick that just landed is lit the same way. Its outline where it will land is asked of the physics world, each frame.
- **The camera follows your tower's top,** and shows more of the tower the taller it stands. While a round is over it shows the whole race, to watch everything come down.
- **Views see every body between its last two ticks,** and gliding through a correction, because the package tags a `Body`'s `position` and `angle` for it (see [correction smoothing](../game/views.md#provisional-correction-smoothing)).

### What it costs

Late in a round there are about 150 bodies: 120 bricks on the towers, loose but for the towers that used their Cement, and the rest falling or on the ground. On a desktop a tick takes around 0.07 ms while the towers build, with 0.03 more to keep and hash its world, 0.01 ms with everything at rest, and 0.1 ms while every tower comes down at once. In a browser those are 0.11, 0.04, 0.01 and 0.14. Box2D solves each tick in eight smaller steps here, twice what it comes with, which is what loose towers this tall take.

A machine that predicts runs ticks again when it guessed another player wrong, as many as it's ahead of the host by: ten of them, with Box2D's world made again from the match first and each tick's world kept and hashed, take 0.9 ms on a desktop and 1.2 ms in a browser, and 1.2 and 1.6 while everything comes down. That's a tenth of a frame at the worst, so there's room for a good deal more physics than the game asks for: what sets its size is how many bricks a player can place in a round, and how much of four towers fits in a small window.

Box2D's whole state is in the match, about 250 KB here, so a rollback, a player who joins and a new host all go on from it (see [Multiplayer](../physics/box2d.md#multiplayer)). A player who joins 75 seconds into a round is sent about 150 KB.

### The match, whole

<<< @/../examples/towers/game.tide
