---
description: "Explore a deterministic 3D multiplayer physics game in Tide with Box3D, impulses, a collapsing arena, instanced drawing and shadow maps."
---

# Sumo walkthrough

This walkthrough explains how a Box3D match drives balls with impulses, replaces floor bodies when tiles fall and keeps a large arena deterministic. It also covers [the lobby](#the-lobby), [instanced rendering](#what-a-machine-shows), [the shadow map](#the-shadows), and [the cost of simulation and rollback](#what-it-costs).

You are a heavy ball, in a ring in the sky that's stacked with two thousand crates, and so are up to seven others. Plough through what's in your way, ram the others over the edge, and stay on yourself while the floor falls away under everyone. A game made with Tide to push its 3D physics in a match: every crate is in the same place on every machine, whatever their pings, with [Box3D](../physics/box3d.md) under all of it. Play it alone against bots, or with friends in a room.

<iframe class="demo-frame roomy" src="/sumo/?embedded" title="Sumo" allow="fullscreen"></iframe>

[Open it on a page of its own](/sumo/), which is the one to send to friends: see [Play with friends](#play-with-friends).

## Play

The game opens on its lobby, with bots at each other in the ring behind it. **Play** starts a game of your own, **Host a room** opens one for friends, and **Join a room** takes a friend's code.

| Action | Mouse and keyboard | Touchscreen | Gamepad |
|---|---|---|---|
| Roll | `W` `A` `S` `D` or the arrows, or drag anywhere | Drag anywhere | The left stick |
| Charge | Hold Space, or the right button, and let go | Hold **Charge** with another finger, and let go | Hold the south button, and let go |
| The menu | **Menu**, or Escape | **Menu** | |

Your ball is the one marked **YOU**, with a white ring around it that shows through whatever stands in front. A mark at the screen's edge is a ball that's out of view, on the side it's on.

- **You drive your ball, and it weighs 640 kilograms.** It gathers speed, and takes a moment to turn. A crate weighs 23, so a stack of them is no wall: go through it.
- **A charge is how you throw someone out.** Hold it to wind up, which the ring under your ball shows, and let go for a burst the way you're rolling. Two balls that meet are thrown apart, the faster they closed the harder, and the slower one loses its grip for a moment and slides. The next charge takes two and a half seconds, which the ring around the button counts.
- **Over the edge and you're out.** Whoever touched you last, not too long ago, has the point, and you're dropped back in two seconds later. A ball that rolls off by itself scores for nobody.
- **The floor falls away from the rim in,** three times in a round. The tiles that are about to go turn rose for five seconds first, and then drop one after another with everything that stands on them. The last half minute is fought on what's left in the middle.
- **A white pickup makes you heavy:** a ball thirteen times your weight for eight seconds, which nothing stops. **A teal one is a shock:** everything within nine meters is thrown outward, balls too, and whoever it throws out is yours.
- **A rose crate is a keg.** Hit it hard and it blows up, and so does the next keg its blast reaches.
- **What's knocked about breaks up** after a few seconds, so the ring is clearer the longer a round goes on.
- **A round is two minutes,** and it's whoever threw out the most. Then the ring is raised again, in another arrangement, and scores carry on until someone starts a new game. Whoever leads wears the crown.

The bottom right corner shows the frame rate, and your ping in someone else's room. Click or tap it, or press `F8`, for the rest of what the engine is doing: the tick, the entities, the draw calls and the systems that take longest. That's the game's [`stats`](../tools/cli.md#stats) setting.

In this page, click the game to give it the keyboard. On a touchscreen, a swipe on it scrolls the page until you tap the game, and again once you tap the page somewhere else. The game [on a page of its own](/sumo/) has every touch from the start.

## Play with friends

A room takes eight players, on the web and on desktop alike. They meet by the room's code, and nobody waits for anybody: whoever arrives has a ball in the ring, in the round that's on.

1. **Host.** On the lobby, press **Host a room**. The room's code shows at once, like `K7QF2M`, with the eight seats under it and who has each. **Copy** puts the code on the clipboard, and **Play** takes you to the ring.
2. **Share** the code, or a link that has it: the game's address with `?join=` and the code, like `https://tide-engine.dev/sumo/?join=K7QF2M`. Whoever opens the link is in your room, with no lobby in the way.
3. **Join.** With only the code, a friend opens the game, presses **Join a room**, types or pastes the code (Ctrl+V, or Cmd+V) and presses Enter, or **Join**. A code nobody has, a room that's full and a match that ended each say so, on the page the code is typed on, to try again from.

In a game, **Menu** shows the room again: its code to copy, who's in it, and **Leave**, which takes you back to the lobby and a game of your own. A game you started alone has **Invite friends** there instead, which opens a room around the game as it is. While your menu is up, your ball looks after itself, as a bot's does.

From the repo, with [`tide`](./install.md) installed, the same game runs in a window, and joins the same rooms:

```sh
cd examples/sumo
tide run                 # The lobby
tide run --host          # Straight into a room of your own: its code shows by the Menu button
tide run --join K7QF2M   # Straight into a friend's
```

- **Each player has a seat and a color of their own,** and a score beside the others' at the top. A ninth is turned away, and told the room is full.
- **Bots have the seats nobody has,** until there are four balls in the ring. A player who joins takes a bot's ball as it is, and a bot takes over from one who leaves. **Bots** in the menu turns them off.
- **Joining in the middle works.** The match's world is sent as it is, with Box3D's whole state in it: every crate where it is and as fast, the ones in the air too. That's 2.4 megabytes a few seconds into a round, which took two seconds in the game's test, over a network that's late and loses packets.
- **When the host leaves, the room goes on.** Another player's machine takes the match over, with the ring as it was (the game's [`hostMigration`](../multiplayer/matches.md#host-migration) setting).
- **A match that ends puts you back on the lobby,** with a line that says what happened.

Only the players' inputs go over the network: which way each holds the stick, and whether they hold the charge. Every machine runs the same Box3D from them, and gets the same ring, bit for bit (see [Determinism](../multiplayer/determinism.md)). Your own ball answers at once however far away the host is, because your machine [predicts](../multiplayer/networking.md#prediction-and-rollback) the ticks the host hasn't confirmed yet, physics and all, and puts right what it guessed wrong about the others.

## The code

Sumo has seven files of Tide, and a `tide.packages` that says `box3d`.

- `game.tide` is the match, which every machine runs the same way, and `arena.tide` what a round's ring is built of. Both are on this page, [whole](#the-match-whole).
- `play.tide` is what one machine shows of it: the camera, and the ring and what's on it.
- `light.tide` has the meshes, the frags that light them and what the shadows are made of, `effects.tide` the sparks and the words that float up, and `hud.tide` the scores, the clock and the controls.
- The lobby and the menu are the `rooms` package's, [the menu that comes with tide](../multiplayer/matches.md#a-menu-that-comes-with-tide): which match this machine is in is its to say.

### The physics

Nothing in the game moves a body. It spawns them, pushes them and reads where they are, all through components, as the [physics page](../physics/box3d.md) says.

**A ball is driven by pushes.** Its player's stick is an impulse every tick, which is a force, less what the floor takes of its speed, so it gathers speed, coasts and turns as something heavy does. The system that gives it is [`Before` the package's stage](../physics/box3d.md#before-the-step), so the step that uses the push is the tick's own, and a ball answers in the tick its player pressed:

```csharp
[Before(PhysicsStages.Step)]
system Drive(Controls controls, mut Body body, mut Ball ball, Brain brain, Ring ring, Time time)
{
    ...
    var grip = GRIP * (ball.winding ? 0.4 : 1) * (dazed ? 0.15 : 1);
    var rolling = float2(body.velocity.x, body.velocity.z);
    var push = steer * grip - rolling * (hard > 0.05 && !dazed ? DRAG : COAST);
    body.impulse += float3(push.x, 0, push.y) * (ball.mass * time.dt);
}
```

A charge is one more impulse, given when the button is let go. The [input](../game/input.md)'s `charge.up` says so, and it's only ever true for a button a player let go of: a machine that's [guessing](../game/input.md#what-other-machines-guess) what another player does never guesses a burst.

**Two balls that meet are thrown apart.** The package sends `Touched` to a body's entity when it starts [touching](../physics/box3d.md#touching) another. Each ball's handler gives its own ball an impulse away from the other, on top of what Box3D made of the hit, and remembers who it was, for when the ball goes over the edge:

```csharp
event(Touched touched) Bump(mut Ball ball, mut Body body, World world, Time time)
{
    ...
    ball.last = other.seat;
    ball.touched = time.tick;
    var their = world.Get<Body>(touched.other).position;
    var apart = Math.Normalize(float3(body.position.x - their.x, 0, body.position.z - their.z));
    var closing = Math.Max(Math.Dot(other.before - ball.before, apart), 0);
    body.impulse += apart * ((BUMP + BUMP_MORE * closing) * ball.mass * other.mass / (ball.mass + other.mass));
    ...
}
```

**A tile that falls is another body.** The floor is 113 [static](../physics/box3d.md#bodies) bodies, which never move, and a body's type is read once, when it's made. So a tile that's due to fall is destroyed, and a loose one is spawned in its place, a little smaller so that it clears its neighbors. What stood on the tile wakes, and goes down with it:

```csharp
this.Destroy();
var size = float3(TILE - 0.16, 1, TILE - 0.16);
Spawn(Body { position = float3(tile.at.x, -0.5, tile.at.y), velocity = float3(0, -1, 0), angularVelocity = lean },
      Box { size = size, density = 400, friction = 0.8 }, Crate { kind = Kind.Slab }, Loose { mass = size.x * size.z * 400 });
```

A heavy ball is the same: a shape is read once too, so the ball that took the pickup is destroyed and a bigger, denser one is spawned where it was, as fast, with what the first one knew. Eight seconds later it's the other way round.

**A blast is an impulse for everything near.** A system goes through every loose body with a [query](../game/systems.md#another-set-of-entities), only in a tick that has a blast to give, and writes an impulse into the ones in reach through the [`World`](../game/systems.md#other-entities-the-world). An impulse written into a body that's asleep wakes it:

```csharp
foreach (var thing in things)
{
    ...
    mut var thrown = thing.body;
    thrown.impulse += way * (blast.push * (0.35 + 0.65 * near) * thing.loose.mass);
    world.Set<Body>(thing.entity, thrown);
    if (world.Has<Keg>(thing.entity)) thing.entity.Send(Kindled { seat = blast.seat });
    else if (world.Has<Ball>(thing.entity)) thing.entity.Send(Rocked { seat = blast.seat });
}
```

**Two sensors.** The pit under the ring is a [sensor](../physics/box3d.md#sensors) the size of the sky: what falls into it is destroyed, which takes its body with it. A pickup is one too, floating over the floor: the first ball that comes into it has it.

### Two thousand crates

A ring is raised in one tick: `arena.tide` plans it, one of three arrangements, an eighth of the ring mirrored seven times so that every seat starts with the same ring around it, as high as the round's own [dice](../language/math.md#random-numbers) say. The dice are seeded from the match's seed and the round, so every machine builds the same ring. A function can't spawn, so the plan is a list of where each crate goes, and a system spawns what's on it.

- **Everything stacked is asleep until something hits it.** A body at rest is [asleep](../physics/box3d.md#bodies-at-rest), and a world where everything sleeps isn't stepped at all. A ring with nobody moving costs a tick 0.03 milliseconds, with 2,400 bodies standing in it.
- **A ring is put to sleep as soon as it's stacked.** It's stacked where it rests, and left to itself Box3D stepped all of it for a second before it let it sleep. So each crate is spawned with a tag, and a system writes `asleep = true` into the ones that have it, a tick later, and takes the tag off. The one step in between is for Box3D to find what rests on what: a stack then sleeps as one, and wakes as one when a ball gets to it. Spawned asleep, each crate would sleep by itself, and a stack that's hit would wake a crate a tick, which costs more in the ticks where a lot wakes at once.
- **A column stands a hand's width from the next.** Box3D keeps a contact for every two bodies whose boxes are within 14 centimeters of each other, touching or not, and every contact is in its state, which the match holds, hashes and sends to a player who joins. A crate in a column has one contact, with the crate under it. Packed closer than that, each crate had seven, and the state of the same ring was three times the size.
- **What's knocked about breaks up.** A crate that's hit hard enough has a few seconds left. Without that, the rubble of every tower lay in a heap in the middle of a floor that's getting smaller, most of it awake, and a tick took 2 milliseconds by the end of a round, where it takes 0.1.
- **Nothing is sent while nothing can be driven.** The ticks a ring is raised in are the ones a machine can least afford to run again. So the input sends nothing while a round is counted in: an input that stays as it was is one every other machine guesses right, and a tick that was guessed right isn't run again.
- **How much is stacked is the match's, from its start.** The match's [scene](../game/scenes.md) has one value, how many tiles out from the middle its rings are stacked. That's every tile, unless whoever [starts the match](../multiplayer/matches.md) says less, as in `Session.Start(Main { stacked = 2 })`, and then every player who joins gets that ring, as it's in the match. The game always starts one with all of it. Its test starts some with less: its match of three players is four machines, each keeping a ring, and it has the middle's pile and the cabins around it, a tenth of the crates.

### The bots

Every ball has a `Brain`: where it would go by itself, and whether it would charge. A system works it out each tick, with a query over the other balls: it picks someone to go for, goes round to their inside, where a hit sends them out, winds up and lets go. It turns back from the rim by how far the rim is and how fast it's going out to it. `Drive` uses the brain where nobody drives: for a seat no player has, and for a player whose lobby or menu is up, which the input says with one field, `away`.

That field is marked [`[Absolute]`](../game/input.md#what-other-machines-guess): it says how things are, not what the player does, so what another machine guesses of it stays as it was.

### The lobby

The lobby isn't the game's: it's the `rooms` package, [the menu that comes with tide](../multiplayer/matches.md#a-menu-that-comes-with-tide), which the game lists in its `tide.packages`. `Main` is the match's scene, so a match of this machine's own starts with the program, closed, and the menu is drawn over it: **Play** closes it, **Host a room** opens that match to others, and **Join a room** leaves it for someone else's. A machine that was told which match to be in, with `--host` or `--join` on its command line or `?host` or `?join=` in its page's address, has no menu in its way.

What the game adds is in `hud.tide`: the input reads the menu's `Lobby.playing`, and says the player is away while a page is up, so their ball plays by itself; and a view tells the room's page who has each seat and whether bots play the rest, and sends `Rebuild` and `Bots` when the page's **New game** and **Bots** buttons say so. [Towers](./towers.md#the-lobby) does the same.

### What a machine shows

- **The crates are one draw.** A view runs for every crate and adds a box to a list, where the body is between its last two ticks. One [`Draw.Instanced`](../game/shaders.md#instances-from-a-list) draws the list, through a [frag](../game/shaders.md) that paints a frame around each face and lights it. A frame of a full ring is under thirty draw calls.
- **The floor is a texture of 15 cells by 15.** A view writes each tile into a local grid, there or about to go, and the frags [sample it](../game/shaders.md#textures): the floor's paint has a line of light along an edge with no tile past it, and the rings under the balls stop where the floor does. Those rings are drawn inside [`Draw.Depth(Depth.Read)`](../game/3d.md#depth): what stands in front hides them, and they hide nothing.
- **There's no lighting built in,** so everything is a mesh whose corners have a normal, made once in a local singleton, and lit by one function that every frag calls. A ball's frag gives it a white band and a dark cap, so that its roll shows.
- **The camera shows the whole ring when a ball is still big enough to find at that size,** and as much of it as that takes otherwise, around your ball and towards the middle, where the others are. It always looks from the same side, so up on the screen is the same way in the ring for every player, and for the stick.
- **The scores are read [`verified`](../game/views-and-the-match.md#confirmed-state):** what the server has confirmed, never a fall this machine only predicted. They're on the seats, so the view goes through them with a [query](../game/views-and-the-match.md#other-entities).
- **The sparks, the blasts' rings and the camera's shake come from events.** The match says what happens in it: two balls met, a ball went through crates, a charge was let go, a keg went off, a tile dropped, a ball fell. Nothing in the match handles those, so they [cost it nothing](../game/events.md#events-only-local-code-handles), and each machine's [local handlers](../game/events.md#local-handlers) make an effect of them, once, however often it runs the tick again. The point that floats up from where a ball fell and a round's winner wait for the server, with [`verified` handlers](../game/events.md#verified-handlers).
- **Views see every body between its last two ticks,** and gliding through a correction, because the package tags a `Body`'s `position` and `rotation` for it (see [correction smoothing](../game/views.md#provisional-correction-smoothing)).

### The shadows

The engine has no shadows either. It has the pieces of a [shadow map](../game/shaders.md#a-shadow-map): a [render target that holds depth alone](../game/render-targets.md#what-a-target-holds), and a frag that [reads a target's depth](../game/shaders.md#a-target-s-depth). So a tower's shadow falls across the tiles behind it, and across the crates, the balls and the towers that stand there.

**What casts is drawn once more, through a camera where the lamp is.** The lists the window's frame is drawn from go into a target of depth alone first, with no frag, as no color goes anywhere there: the tiles, the crates and the pickups that are boxes in one draw, and the balls in another. What's left in the target is how far the lamp sees along each of its pixels:

```csharp
[Stage(Layers.Lamp)]
view Cast(Fx fx, Models models, mut Shadow shadow)
{
    if (!shadow.made)
    {
        shadow.made = true;
        shadow.map = RenderTarget(LAMP_PIXELS, LAMP_PIXELS, 1, Format.Depth);
        shadow.depth = shadow.map.depth;
    }
    var turned = quaternion.LookRotation(-Lamp(), float3(0, 1, 0));
    var lamp = float4x4.TRS(Math.Mul(turned, float3(0, LAMP_RAISED, -LAMP_AWAY)), turned, float3(1));
    var sees = float4x4.Ortho(LAMP_SPAN, LAMP_SPAN, 0, LAMP_FAR);

    shadow.map.Clear(Color.white);
    shadow.map.SetCamera(lamp, sees);
    shadow.toLamp = shadow.map.WorldToClip();
    shadow.map.Instanced(models.cube, models.cubeFaces, fx.tiles);
    shadow.map.Instanced(models.cube, models.cubeFaces, fx.solids);
    ...
}
```

That view comes before anything is drawn to the window, the window's own clear too: everything drawn there asks the lamp's image, so the image is whole first.

**Every frag that lights a pixel asks the lamp's image.** It works out where its pixel is in that image, and compares how far the pixel is from the lamp with what's stored there. Only a frag reads a target's depth, so each of the four that light something asks for itself, four times around its pixel, which blends a shadow's edge, and hands the one function that does the lighting how much of the lamp's light arrives:

```csharp
frag CrateLook(Corner corner, out Color color, Solid solid, Instance instance, Room room, Shadow shadow)
{
    ...
    float3 normal = Math.Normalize(Math.Rotate(instance.transform, corner.normal));
    float3 seen = FromLamp(this.world, normal, shadow.toLamp);
    float sun = (shadow.depth.Compare(seen.xy + float2(-LAMP_SOFT, -LAMP_SOFT), seen.z) + shadow.depth.Compare(seen.xy + float2(LAMP_SOFT, -LAMP_SOFT), seen.z)
               + shadow.depth.Compare(seen.xy + float2(-LAMP_SOFT, LAMP_SOFT), seen.z) + shadow.depth.Compare(seen.xy + float2(LAMP_SOFT, LAMP_SOFT), seen.z)) * 0.25;
    color = Lit(paint, normal, this.world, room, 0.25, sun);
}
```

In shadow, the lamp's light and its shine are gone, with a little of the sky's blue in their place, and the sky's own light and the cool one from the far side stay. A face that looks away from the lamp had none of the lamp's light to lose, so it's the same in a shadow and out of one.

- **The lamp's image is 1,024 pixels a side, of the whole ring.** It sees a box 40 meters across with no perspective, as a sun's light has none, so a pixel of it is 4 centimeters of the ring. In this page's frame the camera shows 30 meters across 375 pixels, 8 centimeters each, so a shadow's edge is finer than the screen's pixels there, and about as fine as them in a window of 1080 lines that shows the whole ring. Half as many pixels a side blur a crate's shadow. Twice as many are four times the drawing, for an edge that's only sharper where a screen's pixel is smaller than the image's.
- **It never moves.** The ring gets smaller through a round, and an image that followed it would be finer by the end. But the lamp's pixels would then slide over the ring as it shrank, and the edge of every shadow would crawl. Fixed to the ring as a round starts, the shadow of what stands still is still.
- **A pixel looks from a little off its surface.** What the lamp stored for a surface is how far it is at the middle of one of the image's pixels, which on a surface that slants away from the lamp is a way off, nearer or further. Compared as it is, such a surface is in its own shadow, in stripes. So `FromLamp` moves the pixel along its normal first, by 0.4 of one of the image's pixels and up to 1.8 more as the surface slants, which is as far as the four looks reach: with none of it everything lit is striped, and with half of it a tumbling crate still shimmers. It's off the surface, not nearer the lamp, so the floor beside a crate is still behind the crate's side, and a shadow starts where its crate does.
- **What's outside the image is lit.** Past its edges, a depth reads the pixels at the edge, so a crate thrown across one would smear its shadow over everything beyond. `FromLamp` gives such a pixel a depth of 0, nearer than anything stored.
- **What's flat on the floor isn't lit,** so it's the same in a shadow: the rings under the balls, and the line of light along the floor's edge, are their own light.

### What it costs

Measured on a release build, on one thread, with the world kept and hashed after every tick as a client's is, and in a browser's WebAssembly under Node, where Box3D solves four contacts at a time and a desktop eight:

| A tick, in milliseconds | Desktop | Browser |
|---|---|---|
| A fresh ring at rest | 0.02 | 0.03 |
| Four bots at a full ring | 0.4 to 0.6 | 0.5 to 1.0 |
| The worst: a shock beside the middle's pyramid | 0.9 to 1.1 | 1.2 to 1.5 |
| Keeping and hashing the world, after each | 0.06 to 0.1 | 0.08 to 0.14 |
| ...where there's one thread to hash it on | 0.12 to 0.2 | 0.14 to 0.23 |
| Raising a ring: the tick that makes its 2,460 bodies | 9 | 27 |
| ...the one after, which puts what's stacked to sleep | 3 | 6 |
| ...and each tick of the half second in which the balls come to rest | 0.09 | 0.5 |

- **Box3D's state is 3.8 megabytes** as a round starts, a kilobyte and a half a body, and all of it is in the match. Most ticks write only the part of it that changed: two in three while four bots bring a full ring down, and nine in ten after.
- **Keeping that state costs about as much as the physics.** With four bots at a full ring, Box3D's own step is 0.3 ms of a tick. Writing what changed of its state into the match is 0.2 more, and hashing the world after is 0.12 for one thread: about half of the state's 16 kilobyte blocks have a byte that changed, since what's moving is scattered through it. So the engine calls a few more threads in to hash, where the machine has them and the page lets it use them, which takes it to 0.06. The game's own systems, and the package's that read bodies in and out, are 0.03 together.
- **A player who joins downloads it:** 2.4 megabytes as the match packs it, with the rest of the world.
- **A client that guessed wrong runs ticks again,** from the last one the server confirmed: Box3D's world is made again from the state in the match, and the ticks since run again. Five ticks take 5 ms on a desktop and 7 in a browser, and ten take 7 and 8.5. Where the ticks a client is ahead by would take more than 4 ms to run again, by what its ticks took of late, the engine [delays the player's input](../multiplayer/matches.md#input-delay) a tick at a time instead. With four bots at a full ring that's past about seven ticks ahead on a desktop, which is where a client is at a ping of about 90 milliseconds, and past four in a browser, which is still at most pings. Later in a round, with less in the ring, there's room for more.
- **A frame's views** take 0.2 ms with a full ring standing, for under thirty draw calls: 22 with four balls in the ring, three of them the lamp's image.
- **The shadows cost a GPU next to nothing.** A frame of a full ring is 0.1 ms of a desktop GPU's time in a window of 1080 lines, and 0.2 in a browser on WebGPU. In this page's frame it's 0.08 ms, of which the lamp's image is about 0.01, as it is whatever the window's size. Where a browser draws with no GPU at all, the shadows are a tenth of a frame's drawing.

That's about as much as a match takes, with the package as it is: a ring twice the size is twice the state to hold, to send and to read again for every wrong guess.

### The match, whole

<<< @/../examples/sumo/game.tide

And what a round's ring is built of:

<<< @/../examples/sumo/arena.tide
