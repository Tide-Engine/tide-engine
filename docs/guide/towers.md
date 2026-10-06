# Towers

Four plinths side by side, bricks that come down one at a time, and a race to build the tallest tower, with [Box2D](../physics/box2d.md) under all of it. A game made with Tide to show its 2D physics in a match: a brick that has landed is a body like any other, so towers lean, slide and come down, the same on every machine, whatever their pings. Play it alone against three towers that build themselves, or with friends in a room.

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

Your tower is the one on the plinth that says **YOU**, your brick is the one with the white edge, and the lanes under it show where it will come down. The row along the top says how high every tower stands, and the bar down the left side shows the race: a mark for each tower, yours the big one, and the finish line at the top.

- **A brick falls where you steer it,** half a cell at a time, and turns by quarters. The moment it touches anything it eases down the last of its way and is a real brick: it has weight, it leans and it slides. Then the next one comes. **NEXT** shows which.
- **The first tower to stand over the finish line for three seconds takes the round.** The line is 40 meters up. Everything then comes down at once, and the next round starts from nothing. Rounds won are the dots under a plinth's name.
- **A brick that falls off your tower costs you time:** your next brick waits longer, up to three seconds when a lot of them go at once. Bricks lie on the ground for a few seconds, and are cleared.
- **The lower part of a tower sets solid.** A brick that's at rest ten meters under its tower's top turns gray: its mortar has set, and it won't move again this round. So a tower only ever leans and comes down from its top part, which is where you're building.
- **A tower earns something at 10, 20 and 30 meters,** which the dotted lines across your plinth mark. A **gust** shoves the top of the tallest of the other towers away from yours. **Cement** sets every brick of your tower solid at once, the loose top too. A **quake** shakes every plinth but yours. You hold one at a time, so use it before the next line.
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

The game is `examples/towers` in the [repo](https://github.com/BlenMiner/tide-engine/tree/dev/examples/towers): six files of Tide, and a `tide.packages` that says `box2d`.

- `game.tide` is the match, which every machine runs the same way. It's on this page, [whole](#the-match-whole).
- `bots.tide` is how a tower nobody steers gets built: a plan for each brick, from how high the tower stands in each column.
- `play.tide` is what one machine shows of it: the hand on the brick, the camera and the pieces.
- `effects.tide` has the dust, the pieces and the words that float up, and `hud.tide` the race, the next brick and the words over the world.
- `lobby.tide` is the lobby and the menu: which match this machine is in. It's on this page too, [whole](#the-lobby-whole).

### The physics

Everything that's solid is a body of Box2D's, made, moved and read through components, as the [physics page](../physics/box2d.md) says. A brick is two bodies in its time, and a third if it's set solid.

**A falling brick is a kinematic body of sensors.** It goes where its `velocity` takes it, which is down, and where its `position` and `angle` are written, which is how a player steers it: half a cell across, a quarter turn. Its shapes are [sensors](../physics/box2d.md#sensors), so it stops nothing and pushes nothing, and hears `Touched` for each shape that comes into it, a plinth's and a brick's alike. The system that steers it is [`Before` the package's stage](../physics/box2d.md#before-the-step), so the step that moves it is this tick's, and it reads the input of the player who owns the brick:

```csharp
if (falling.column != falling.wantColumn && falling.age % 2 == 0)
{
    falling.column += falling.wantColumn > falling.column ? 1 : -1;
    body.position.x = falling.home + falling.column * 0.5;
}
if (falling.eased > 0) falling.eased -= 1;
var speed = falling.eased > 0 ? -EASE : fast ? -FAST : -FALL;
if (body.velocity.y != speed) body.velocity = float2(0, speed);
```

**The first thing it touches stops it short.** A sensor says what's in it once a step, where things are after the step, so a brick that falls 30 centimeters a tick is that far into the tower when it hears. It's put back where it was a tick before, as it was, and comes down from there at a slow, steady speed: however fast it fell, it lands softly, and a move or a turn that ran it into a wall is undone.

```csharp
event(Touched) Touch(mut Falling falling, mut Body body, Time time)
{
    falling.touching += 1;
    if (falling.eased > 0)
    {
        if (time.tick != falling.since) falling.landed = true;
        return;
    }
    falling.eased = EASED;
    falling.since = time.tick;
    falling.speed = -body.velocity.y;
    falling.column = falling.freeColumn;
    falling.quarter = falling.freeQuarter;
    body.position = falling.free;
    body.angle = Angle(falling.freeQuarter);
    body.velocity = float2(0, -EASE);
}
```

**What it touches then is where it lands: it becomes a dynamic body.** A body's `type` is read when it's made, so the falling brick's entity is destroyed, which takes its body with it, and a brick is spawned where the falling one last was free, moving down as slowly. From its first step it's Box2D's: it has weight, it rests on what's under it, and it goes over if that's not enough.

```csharp
Spawn(Body { position = falling.free, angle = Angle(falling.freeQuarter), velocity = float2(0, -EASE), angularDamping = STEADY },
      Box { size = part.size, offset = part.offset, friction = GRIP }, Piece { kind = piece.kind },
      Brick { seat = falling.seat, born = time.tick });
```

**A brick is one body with two boxes.** The seven bricks are four cells each, which is one box for the bar and the square, and two for the rest. The package gives an entity one `Box`, so the second is put on the body by a system of the game's, in the package's stage, with the function the package puts the first on with:

```csharp
[Stage(PhysicsStages.Step, 4)]
system Fit(Body body, Box box, Piece piece, mut Physics physics, without Fitted)
{
    var second = Second(piece.kind);
    if (second.size.x > 0)
    {
        var size = box.sensor ? second.size - float2(SLACK) : second.size;
        physics.shapes += AttachBox(body.id, Box { size = size, offset = second.offset, friction = box.friction, sensor = box.sensor });
    }
    this.Add(Fitted);
}
```

::: warning A stopgap
`AttachBox` is the package's own, and a game's system in the package's stage is what its page says not to write. It's here because a brick of two boxes has no other way to be one body yet: a body takes one `Box` and one `Circle`. It works because it's the same call, at the same point of the tick, that the package makes for the first box, so the physics state the match keeps has it. When the package takes several shapes of a kind, this system goes.
:::

**The plinths are kinematic bodies,** which go where their `velocity` takes them, whatever stands on them. A quake is a plinth going there and back four times. A plinth rises under a player who joined late, and when a round is over the plinths sink, the winner's last, and everything on them comes down.

**The lower part of a tower is set solid.** Forty meters of loose bricks is a spring: every landing would run down the whole tower and back. So a brick at rest ten meters under its tower's top becomes a kinematic body in the dynamic one's place, and goes where its plinth does from then on: the system that looks at every brick before the step gives it the plinth's velocity. What can lean, slide and fall is the top of each tower, which is where the bricks are landing. When the round is over, every one of them is a dynamic body again, from the bottom of its tower up, and all of it comes down.

**A gust is an impulse.** That same system gives the loose bricks near the top of the tower a gust is against an `impulse` each tick it blows, more the nearer the top. It looks at every brick and writes a few, so it takes `Body` to read and puts the ones it changed back [through the World](../game/systems.md#other-entities-the-world):

```csharp
if (Of(towers.gusts, lot) <= 0 || Math.Abs(body.position.x - Home(lot)) > PLINTH) return;
var depth = Of(towers.tops, lot) - Peak(body.position, body.angle, piece.kind);
if (depth > WIND_DEPTH) return;
mut var pushed = body;
pushed.impulse.x += Of(towers.winds, lot) * WIND * (1 - Math.Max(depth, 0) / WIND_DEPTH);
world.Set<Body>(this, pushed);
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

While a page is up, the player's hands are off their brick: the input says so (`hands`), and the match builds their tower for them, as it does a tower nobody has. A match that ends, or a room that can't be joined, comes back as the built-in `Disconnected` event, whose handler starts a match of this machine's own again and leaves a line for the lobby to show:

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

Its buttons are [controls](../game/gui.md#widgets-of-your-own): the game draws them, and the GUI handles their input, so the pointer, a finger, the keyboard and a gamepad all work them. The field for a room's code is the GUI's own, in a [theme](../game/gui.md#themes) that matches, and it [says when Enter ends the typing](../game/gui.md#when-a-field-is-entered), which joins as the button does.

### What a machine shows

- **Whose tower is whose** is the built-in `Owner` component, on a plinth and on the bricks it's dealt: the system that steers a falling brick takes the game's input, and gets the input of the player who owns the brick it runs for. A view picks this machine's player's with [`with ownership`](../game/views.md#your-own-entities).
- **Using what a tower earned, a new game and the bots' switch are [commands](../game/commands.md),** sent by buttons and handled in the match.
- **The row of heights is read [`verified`](../game/views.md#confirmed-state):** what the server has confirmed. They're on the plinths, so the view goes through them with a [query](../game/views.md#other-entities).
- **The dust, the pieces and the camera's shake come from events.** The match says what happens in it: a brick landed, one fell off, a tower earned something or used it, a round ended. Nothing in the match handles those, so they [cost it nothing](../game/events.md#events-only-local-code-handles), and each machine's [local handlers](../game/events.md#local-handlers) make an effect of them, once, however often it runs the tick again. A round's winner waits for the server, with a [`verified` handler](../game/events.md#verified-handlers).
- **A falling brick slides across and swings round,** where the match steps it half a cell and turns it a quarter at once: where it's drawn is two [local fields](../game/local-state.md#local-fields) of the match's own component, which its view eases. A brick that just landed is lit the same way.
- **The camera follows your tower's top,** and shows more of the tower the taller it stands. While a round is over it shows the whole race, to watch everything come down.
- **Views see every body between its last two ticks,** and gliding through a correction, because the package tags a `Body`'s `position` and `angle` for it (see [correction smoothing](../game/views.md#provisional-correction-smoothing)).

### What it costs

Late in a round there are about 180 bodies, most of them set solid. On a desktop a tick takes around 0.06 ms while the towers build, with half as much again to keep and hash its world, 0.01 ms with everything at rest, and 0.13 ms while every tower comes down at once. In a browser it's about twice that. A machine that predicts runs a few ticks again each frame at a high ping, ten of them at the worst, which this leaves plenty of room for. Box2D's whole state is in the match, between 230 and 370 KB here, so a rollback, a player who joins and a new host all go on from it (see [Multiplayer](../physics/box2d.md#multiplayer)): a player who joins in the middle of a round is sent about 45 KB.

### The match, whole

<<< @/../examples/towers/game.tide

### The lobby, whole

<<< @/../examples/towers/lobby.tide
