---
description: "Understand Tide's server-authoritative rollback netcode, client prediction, input delivery, corrections and host migration."
---

# Networking

Tide uses rollback netcode with full server authority. The server is the source of truth, and clients predict ahead of it, rolling back when it disagrees. Since the simulation is [deterministic](./determinism.md), what goes over the network is mostly inputs.

For how a game starts, hosts and joins matches, see [Matches](./matches.md). This page is about what happens underneath.

## One path for everything

Single-player is a match too: the machine runs a server itself, and connects to it through a loopback transport. There's no separate offline path, so what works alone works online.

A minimized desktop window or a browser tab in the background keeps running without drawing, so the match goes on for the other players. Browsers slow a hidden page's timers and stop its animation frames, so a hidden page's frames come from a worker's timers instead. Meanwhile it runs a frame when its match is due a tick, at the game's `tickRate`, and none in between.

A machine that stops simulating can't keep hosting: a program paused in a debugger, a browser that freezes its page, or a phone whose app is suspended can stop answering the other players. With [host migration](./matches.md#host-migration), another player's machine takes over a room's match. Without it, the other players eventually time out; the suspended host's own match resumes from where it stopped if its program comes back. Putting a phone's game in the background does not keep its match running there indefinitely.

## What the server sends

The server ticks the one true world. Every tick, it sends each player:

- who joined or left,
- the inputs that changed,
- the commands scheduled for that tick, with their sender and sequence (a big one by the number of the transfer every player was sent it as),
- and a hash of the world after the tick.

An input that didn't change, or didn't arrive, keeps its last value, on every machine. One that changed goes as what changed: a bit for each value that didn't. Players send theirs the same way, each tick's from the tick before. Its ints and enums take as little as they need: a number from -64 to 63 takes a byte, one up to about 8,000 either way two, and so on, and one that changed goes as by how much, so a big number that moves a little takes a byte too. A float that changed goes as the bits that changed, which for a value that moved a little, or a round one, is far fewer than its 32. Nothing is rounded: every value arrives exactly as it was sent, bit for bit.

## Prediction and rollback

Clients start with no input delay in the default rollback mode. A client runs ahead of the server by about half the round trip, plus a small margin, so its inputs arrive in time for the tick they're meant for. The margin is as small as the connection allows: the server says how long each player's inputs waited before their ticks ran, and a client sends them only as early as the network's worst moments need, so that the one that waits least still waits one of the player's frames (a 60th of a second where its frames are shorter, and never more than a tick). All the time an input waits at the server is time later that the other players see it. A frame, because inputs are read and sent a frame at a time: one that came late still gets its input there in time. With no delay, its own input applies at once, and only other players' inputs are guessed, by repeating their last one: for 0.15 seconds past the verified tick, and from there on as if they'd let go, so a wrong guess moves things by what a player does in that long at most, however far ahead a slow connection has the client run (see [What other machines guess](../game/input.md#what-other-machines-guess)). In a game without an input there's nothing to get there in time, so clients keep the small lead they joined with.

A client keeps a snapshot of every tick from the last one the server confirmed, the **verified tick**, to the one it's predicting. When the server's tick arrives:

- If it went as the client guessed, the client only checks its hash against its snapshot.
- If an input was different, the client goes back to the snapshot before that tick, and runs it and the ticks after it again with the right inputs.
- If a hash differs anyway, the client gets the world again from the server: only the pages its own world lacks (see [Sending worlds](#sending-worlds)).

Below are two machines in one match: the first runs the server, and the second joined its room. Move the pointer over a frame to steer that machine's player. A disc is where the machine drawing it predicts the body is, and the ring of its color is where the server has confirmed it, which a view reads [`verified`](#predicted-and-confirmed).

Both machines are at one desk here, so their packets arrive at once. The slider is player 2's ping, the network's round trip between its machine and the server, there and back: its page holds back what it sends and what it receives, half the trip each, as a slow connection would. It's the number player 2's frame shows. Beside the slider is the least it can be, with nothing held back, which at one desk is next to nothing.

A ping is the network's time alone. A packet that arrives still waits for its machine's next frame to be read, on the way there and on the way back, so an answer takes up to two frames longer than the ping says, on any connection.

- **Player 2's own disc still answers at once**, however late its packets are. Its ring trails behind: that's the server's answer, a round trip old.
- **Player 1 sees player 2 late**, by the way there, half the round trip, and guesses in between: steer player 2 in a circle, and its disc in player 1's frame cuts the corners, walking to where it last heard player 2 point.
- **Player 1's own disc and ring stay together.** Its machine runs the server, so nothing of its own is ever late.

A guess that was wrong is put right the moment the server's answer arrives, which would move a disc all at once. The bodies' `position` is marked `[Position]`, so a disc glides to where the correction put it (see [correction smoothing](#provisional-correction-smoothing)); a ring is the confirmed world as it is.

::: tide-match prediction lag=200 Predicted, and confirmed by the server

<<< @/examples/prediction.tide

:::

A client predicts at most a second ahead of the verified tick; beyond that, it waits for the server. The server keeps four seconds of ticks to send again, and a player further behind gets the whole world. A player that was sent the world has every tick since kept for it until it catches up, however long a big world took to arrive, up to 32 seconds; one that can't catch up in that long gets the world again.

When a client gets a world, or returns after its machine stopped while the match went on, it first catches up with the ticks the server has already run. It predicts nothing from the old world while doing so, and samples no input for ticks that can't arrive in time. A short pause skips old inputs too, even when it does not need catch-up. If the pause leaves new inputs behind the server and more confirmed ticks due now than the frame can run, the client catches up before making new predictions. Future inputs still go to the server while the client catches up. Catch-up work is spread over frames: normally no more than eight authoritative ticks a frame, with room for more when frames are consistently longer. A client with a clock for measuring its work also gives that work a budget of half a frame's time, checked between ticks, while running enough ticks to make progress, up to eight mandatory ticks. A machine too slow for the match falls behind and gets the world again; its frames don't keep asking for more work because the last one took longer.

If the server's machine stopped instead, its match clock drops that time while the other machines' clocks went on. A client brings its clock back once two answers prove it is past the prediction window: it has confirmed every tick the server says it ran, and setting its clock from that tick and the answer's round trip would fit within the window. A slow or reordered answer alone doesn't prove that, because its round trip includes that wait. Predictions past the new clock are discarded, and inputs already sent stay with the server; new inputs wait until the clock has reached those ticks again.

Running ticks again is the cost of prediction: another player's input that wasn't what the client guessed means running every tick from there to the one it's predicting again, which on a busy match is most ticks. A client further ahead than its guesses go also runs the ticks past them again each time a tick is confirmed, while another player is doing something: where they let go has moved a tick on. A client measures what its rollbacks cost, and keeps its input delay at the least where they fit: a single rollback may take up to about a frame, the time between its updates, and never less than its budget (4 ms, unless the host says otherwise) nor than a 60th of a second, and running ticks again may take up to that budget an update on average. Past either it delays its input a tick at a time: it still samples inputs as far ahead as before, so they arrive in time, but runs ticks that many behind, predicting less. It takes the delay back a tick at a time once there's been room to spare for half a second, and once no guess has been wrong for a couple of seconds, so a burst of expensive ticks, like a level being built, costs seconds of delay rather than minutes; a client whose guesses are right pays no delay for ticks it never runs again, and the player on the server's own machine, whose inputs go straight in, never has any. A game can ask for delay in place of prediction altogether, with `prediction = Prediction.Delay` in its settings (see [Input delay](./matches.md#input-delay)): then its clients never predict, and run each tick once as the server confirms it, as many ticks behind as that takes. Either way, a tick runs when the client's clock reaches it, however early its frame came, so views blend between ticks at the match's pace.

Snapshots are cheap, which is what makes this work. The world is plain data, with no pointers, and everything past what's in use is zero, so a snapshot is a copy of the bytes in use, and a hash covers only those. Their cost follows what's in the world, not how big it could be.

## Sending worlds

A world goes over the network page by page, as a delta: each page is either the same as the one the receiver has in its place, or its bytes, with runs of zeros packed small. Where its rows or cells are like the ones before them (entities lined up, a grid of sand), a page goes as how each one differs from the one before, which is mostly zeros too. Every byte arrives exactly as it was. A player with a world like the match's, such as its own after a hash differed, or the last one it had of a match that changed hands, first gets a hash of each page, says which ones its world lacks, and gets only those. A player joining with nothing does the same with the match as it started, which it starts itself, when starting a match calls no C (see [Calling C](../c/functions.md#what-c-is-trusted-with)): what never changed since, like a level the match made as it started, never goes over the network. A world small enough to go in one update goes whole, as do worlds of games whose start calls C, and of matches that went on from another's world. A world that doesn't come out as the server's hash says is asked for again, whole.

## Views and prediction

Views draw the predicted world, blended between its last two ticks (see [Views](../game/views.md#smooth-at-any-tick-rate)). A tick that's run again after a rollback changes the simulation immediately; tagged fields can smooth how the correction appears.

### Predicted and confirmed

Local code tells what's predicted from what the server has confirmed with one word, `verified`. The match can't say it: it's the same on every machine, and has no "confirmed".

- **Before a view's parameter or filter**, it reads the world as of the verified tick: `view HealthBar(Health health, verified Health confirmed)` gets the health this machine predicts, and the one the server confirmed. See [Confirmed state](../game/views-and-the-match.md#confirmed-state).
- **Before a local handler's event**, it hears the event once its tick is verified: `local event(verified Died died) ShowDeath(...)` runs once, late by as far as this machine runs ahead, and is never wrong. See [`verified` handlers](../game/events.md#verified-handlers).

Without it, a view draws the world as this machine predicts it, and a [local handler](../game/events.md#local-handlers) hears each tick as this machine first ran it.

A local handler hears an event once, though the tick that sent it may run again. A client remembers what each tick it hasn't had confirmed delivered: an event the tick sends again stays silent, one only the corrected tick has is heard then, late, and one the correction removed isn't taken back (see [Once, under rollback](../game/events.md#once-under-rollback)). What's kept for that is beside the world and never in it, so hashes, snapshots and what's sent are the same with local handlers or without.

### Provisional correction smoothing

**Provisional.** Match fields marked `[Position]` or `[Rotation]` retain local presentation error across a prediction correction and remove it over time. `[Position]` uses additive error for floats and floating-point vectors. `[Rotation]` uses the shortest wrapped angular error for a float in radians, or relative rotational error for a quaternion; the tag also selects shortest-path tick interpolation.

Subsequent corrections accumulate with the error still present, which decays exponentially with frame time, with a provisional half-life of 75 ms. Ordinary movement follows the corrected simulation without being low-passed. Untagged fields keep ordinary tick interpolation. `entity.Snap()` and `singleton.Snap()` remain immediate and clear the accumulated error. A replacement with no comparable presentation history clears it too, including adoption of a new host's network timeline.

Only presentation keeps this error: match state and collisions correct immediately, and snapshots, hashes and network state are unchanged. Annotated built-in fields can be nested in ordinary structs; correction smoothing through a custom `Interpolate` type is deferred. See [Views](../game/views.md#provisional-correction-smoothing) for the provisional syntax and contract.

## Joining

A player who joins gets the world (see [Sending worlds](#sending-worlds)) and a cookie. Joining the same server again with the cookie gets them their `PlayerID` back, and with it everything the game kept for them.

## Host migration

With the game's `hostMigration` setting, the server tells every player what they need for its match to go on without it: its room's code, the room's key (which lets only them take the room over), which players are in the match, and the SHA-256 of each one's cookie. When the server leaves or stops answering, its players go to the room again. The relay pings the room's host: if it's gone, the first player there hosts the room, and the relay introduces the others to it. That machine runs the server from the last tick it verified, with the players already in its world, and the others join it with their cookies, which it checks against the hashes. Their own last worlds are nearly the new server's, so they only get the pages theirs lack. A match that ends says so at the relay too, which keeps its room as ended for five minutes: a player who missed the goodbye and comes back to take the match over is told it ended. See [Matches](./matches.md#host-migration).

## Reliability

Everything is sent again until it's acknowledged. Nothing waits on a reliable stream, so one lost packet never holds up the ones after it.

[Commands](../game/commands.md) have a pending queue separate from tick inputs. The server schedules each received command into its next ticks and records its order. A command stays pending until a verified world confirms its sequence; duplicates are ignored. Per-player sequence state is part of the world and follows snapshots and host migration: a sequence for each [channel](../game/commands.md#ordering-channels) a game's commands are in, and a window of the unordered ids applied. Command handlers use ordinary guard clauses to check the sender's permissions and the payload against match state.

A command too big for a datagram (one with a `List<byte>`: a file's bytes, up to 4 MiB) goes up in chunks of 1000 bytes, 16 an update, acknowledged and resent as a world's chunks are, under the inputs. Once the server has it whole, and its turn in its channel has come, the server sends it down to every player in the match the same way, and puts it in a tick only once they all have it: the tick's frame names the transfer rather than carrying the bytes, so no tick waits for one, on the server or on any player. A player being sent the world, or catching up with one, gets the transfer with the ticks it lacks, and one who joins with a world from a later tick needs nothing of it.

## Transports

Matches run over WebRTC data channels, which neither order nor resend, like UDP, on the web and on desktop alike: desktop games speak WebRTC with an implementation of Tide's own. That's what lets a browser host matches as well as join them, and web and desktop players meet in the same rooms. A desktop host also takes players on a UDP port, Tide's own thin layer, which `--connect` and `Session.Connect` reach by address; the web has no UDP, so a browser can't join by address. The protocol above the transport is the same.

Players find each other in rooms, by a code (see [Matches](./matches.md#rooms)), whether they're in a browser or a desktop game. A relay of ours introduces them, passing along what WebRTC needs to connect them, and their packets then go straight between them. Players whose networks can't connect directly go through a TURN server, which carries their packets. Everything to and from the relay is encrypted (`wss://`), and WebRTC encrypts the match packets. Direct UDP connections by address are unencrypted. On desktop, the relay's encryption is the system's own TLS: Windows' and macOS's, and on Linux OpenSSL's `libssl`, which nearly every distribution has. A Linux game without it can't reach the relay, so it can't use rooms; it can still `--connect` by address.

## Who sees what

[Private scenes](../game/scenes.md#private-scenes) record which players belong to them, but selective replication is not built yet: every client receives and simulates the whole match. A private scene therefore cannot keep cards, random seeds or other secrets from another player's machine. Hiding them in a view only changes what the screen draws.

## Coming later

- Selective state visibility and clients simulating only the parts of the world they receive.
- Dedicated servers with no window and no player of their own, and a public match browser. Room codes and a [ready-made lobby](./matches.md#a-menu-that-comes-with-tide) are available now.

## Collider rewind

Draw remote players with `verified Body body` to interpolate confirmed positions without extrapolating guessed input around corners. The player's own movement can still use the predicted view. Verified event handlers receive exact event state.

The physics packages offer a read-only query context for hit validation. Enable bounded history with `settings { maxRewind = 0.25; }` and opt bodies in with `Body { rewind = true }`. `physics.Rewind(controls.viewTime, out var query)` prepares one scene for all the queries in that shot; `query.Raycast(...)`, `Cast`, `Overlap` and `Inside` have the current physics query forms. Non-opted bodies use current geometry, so walls still block a historical player. Opt moving doors in when they should also rewind.

With `maxRewind` enabled, every input has an engine-supplied, read-only `ViewTime viewTime`. Its `tick` is the upper confirmed endpoint, `alpha` is the interpolation fraction, and `valid` says whether a frame has been presented. The timestamp describes the last frame actually presented, not a future predicted tick. The server treats its wire value as an untrusted claim: it rejects malformed fractions, future ticks, times before that connection's snapshot, times beyond what it advertised and times outside the configured window. `Rewind` also checks retained physics history and returns false when the requested interval is unavailable. It never silently substitutes current geometry or clamps the time.

The game still validates the shooter, origin, aim, weapon, ammunition and firing cadence from authoritative simulation state. A timestamp does not make a client-supplied hit trustworthy, or prove what a modified client really displayed. See [2D physics](../physics/box2d.md#rewinding-queries) and [3D physics](../physics/box3d.md#rewinding-queries).
