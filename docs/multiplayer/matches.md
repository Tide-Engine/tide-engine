---
description: "Start, host and join Tide matches, share room codes and manage sessions with one server-based path for single-player and multiplayer."
---

# Matches

Every match runs on a server, and this machine's player connects to it. When the server is on this machine, that's over a loopback transport, so single-player and multiplayer are the same game with the same code. The engine assumes nothing about what a game does with a match, like pausing: games build that from inputs and state.

## Starting a match from `tide run`

A game whose `Main` scene is the match's starts playing at once. `tide run` can open it to others, or join one instead:

```sh
tide run --host                 # others can join: in a room, and on UDP port 7777
tide run --host 8000            # on another port
tide run --connect 192.168.1.5  # join another machine's match, by its address
tide run --join K7QF2M          # join the room with this code
tide run --web --host           # on the web, in a room only
```

To check a match with several players without opening a window for each, `tide run --players 2 --script play.txt` plays them all at once with no window, and takes a picture from each machine (see [Several players](../tools/cli.md#several-players)).

## Starting a match from code

A game that starts in a menu has a local `Main`, and its local code decides which match this machine is in:

| Call | What it does |
|---|---|
| `Session.Start(scene)` | Starts a match on this machine, which runs its server |
| `Session.Start()` | The same, in `Main`: for code that can't name the game's scenes, like a package's |
| `Session.Open()` | Lets others join the match this machine runs: in a [room](#rooms), and on port 7777 too (not on the web) |
| `Session.Open(port)` | The same, on another port |
| `Session.Close()` | Lets no one else join; the players in the match stay |
| `Session.Kick(player, message)` | Sends a player out of the match this machine runs, telling them why (the message is optional) |
| `Session.KickAll(message)` | The same for every player on another machine |
| `Session.Join(code)` | Joins the match in the room with this code, like `"K7QF2M"`, or on [a network of the game's own](./own-network.md), by the text that network takes |
| `Session.Connect(address)` | Joins another machine's match by its address: `"192.168.1.5"`, `"192.168.1.5:7777"` or a name like `"localhost"` |
| `Session.Connect(address, port)` | The same, on another port than 7777 |
| `Session.Leave()` | Leaves the match. With [host migration](#host-migration), a match this machine runs goes on without it |
| `Session.End()` | Ends the match this machine runs, for everyone |

`Start` names the scene the match starts in, with its values as for `Scene.Load`: `Session.Start(Arena { size = 30 })`, or nothing for `Main` with its defaults, which has to be the match's. `Join` and `Connect` get whatever the server runs. Starting or joining a match leaves the one this machine is in first.

There's one kind of match. It starts closed, so single-player is just a match nobody else was let into. `Open` and `Close` change that at any time: a game can start alone and open its match to friends later, then close it once the party's full. Closing turns away anyone who isn't in the match, and opening it again uses the same room and port.

`Kick` and `KickAll` send players out, and they get the message with their `Disconnected` (see below). A kicked player always hears it: one whose network lost the goodbye is told the next time they're in touch with the server. It's a kick, not a ban: once they know, a kicked player can join again, as the same player, while the match is open. To end a party and play on alone, close the match and kick everyone:

```csharp
Session.Close();
Session.KickAll("Thanks for playing!");
```

```csharp
local scene Main { }

scene Arena
{
    int size = 20;
}

view Menu(Session session)
{
    if (session.state != SessionState.Offline) return;
    GUILayout.Area(Anchor.MiddleCenter)
    {
        if (GUILayout.Button("Play")) Session.Start(Arena);
        if (GUILayout.Button("Host"))
        {
            Session.Start(Arena { size = 40 });
            Session.Open();
        }
        if (GUILayout.Button("Join")) Session.Join("K7QF2M");
        if (GUILayout.Button("Connect")) Session.Connect("192.168.1.5");
    }
}
```

Session calls are statements, in views and local event handlers, and in the functions they call:

```csharp
void Host(mut Menu menu)
{
    Session.Open();
    menu.page = Page.Room;
}
```

Match code can't make them, as it runs the same on every machine: calling such a function from a system is an error at that call.

## A menu that comes with tide

The bundled [Rooms package](../packages/rooms.md) adds Play, Host and Join menus, invite links and room codes to share, and volume settings. List `rooms` in the game's `tide.packages` to use it. For pre-match groups, ready flags, lobby browsing and quick match, use the [Lobby package](../packages/lobby.md). Both menus are optional; sessions and room codes are built into the engine.

## Rooms

Players find each other's matches by a room's code, like `K7QF2M`: 6 letters and digits, without look-alikes like `0` and `O`. Opening a match opens a room, and `Session.room` is its code at once, for the game to show; other players join it with `Session.Join(code)`, from a browser or a desktop game alike.

```csharp
view RoomCode(Session session)
{
    if (session.room == "") return;
    GUILayout.Area(Anchor.UpperCenter)
    {
        GUILayout.Label($"Room {session.room}");
    }
}
```

Here are two machines, side by side. Each frame below is the game on its own, starting in its menu, in no match:

1. In one, press **Start a match**: it plays alone, with its match closed. Move the pointer over the frame to steer its player.
2. Press **Open**. Its room's code shows, and **Copy code** puts it on the clipboard.
3. In the other frame, type the code into the field, or click the field and paste, and press Enter or **Join**. Both players are now in one match, each frame showing it from its own machine.
4. Try **Close** and then **Leave** and **Join** again: a closed match turns the player away, and its menu says why. **Kick all** sends a message along, and **End** ends the match for both.

::: tide-players rooms Open a room, and join it

<<< @/examples/rooms.tide

:::

The relay, a server of ours, only introduces players to each other. Their packets then go straight between them, over WebRTC, which finds a way through their routers. When their networks can't connect directly, a TURN server carries the packets instead. The host's machine picks the code itself, so there's nothing to wait for. If another room had it first, the host picks another before anyone could read it out, and `Session.room` changes. It's `""` while the relay can't be reached, and the room opens again once it can: players already in keep playing, and only joining needs the relay.

### Inviting with a link

`Session.invite` is what a player sends a friend to join their room. On the web it's a link: the game's page, wherever it's served, with `?join=` and the room's code, like `https://example.com/game/?join=K7QF2M`. Opening it starts the game in that room, as `?join=` in a page's address always does. On desktop and Android there's no page to link to, so it's the room's code itself, which the friend types in. It's `""` whenever `Session.room` is, and on the web until the relay confirms the host's room is registered.

```csharp
view Invite(Session session)
{
    if (session.invite == "") return;
    GUILayout.Area(Anchor.UpperCenter)
    {
        // A link on the web, the code elsewhere
        var link = session.invite != session.room;
        if (GUILayout.Button(link ? "Copy the invite link" : "Copy the code")) Clipboard.Copy(session.invite);
    }
}
```

## Where this machine stands

`Session` is a built-in local singleton, taken as a parameter like any other, and read-only:

| Field | What it is |
|---|---|
| `state` | `SessionState.Offline`, `Connecting` or `Connected` |
| `player` | This machine's `PlayerID`, once connected |
| `ping` | The network's round trip to the server, in milliseconds: what the machines wait for their next frame to read a packet isn't in it |
| `server` | Whether this machine runs the server |
| `open` | Whether others can join it (only the server's machine knows) |
| `room` | The code of the room the match is in, or `""` if it's in none, as when it's closed. On [a network of the game's own](./own-network.md), what that network says the match is joined by |
| `invite` | What others open to join the match: on the web a link to the game's page with `?join=` and the room's code, elsewhere the code itself (see [Inviting with a link](#inviting-with-a-link)). `""` when `room` is, and while a web host waits for the relay to register its room |

A single-player match is a match too, so `state` is `Connected` there as well: this machine runs the server, and its player connects to it without going through the network. That's `server` true and `open` false. `$"{session}"` shows all of it at once.

The built-in local events `Connected` and `Disconnected` say when that changes. `Connected` is sent once the match's world has arrived and this machine plays in it. `Disconnected` has a `reason`:

| Reason | Why |
|---|---|
| `Left` | This machine called `Leave`, or started another match |
| `TimedOut` | The server stopped answering, or never did |
| `Refused` | The server runs another build of the game, has no room left, or its match is closed |
| `ServerLeft` | The server's machine left, which ended the match |
| `Failed` | It couldn't start: no network, a port in use, an address that isn't one, or a room nobody has |
| `Ended` | The match's last scene unloaded (see [Scenes](../game/scenes.md#loading-and-unloading)), or the machine running it called `Session.End()` |
| `Kicked` | The server's machine sent it away, saying why in `message` |

```csharp
local event(Disconnected gone) BackToMenu(mut Menu menu)
{
    menu.message = gone.reason == DisconnectReason.Kicked ? $"Kicked: {gone.message}" : $"Disconnected: {gone.reason}";
}
```

## Dedicated servers

`tide run --server --anonymous` starts a native server with no window, graphics
context, audio device, views or player of its own. One process can run many
independent matches of the same game. Matches own separate worlds, players,
inputs, commands and clocks; they share game code, immutable assets and a worker
pool. Matches currently update in order, with parallel systems within each match.
Game C must keep match-specific state in its world or an explicit context;
process-wide C globals are shared. A native C crash still affects the process.

For quick startup, clients request a match with
`Session.Connect("localhost:7777/0")`, or `Session.Join("K7QF2M/0")` through the
server's WebRTC room. Replace `0` with an existing match ID to join that match.
The command line accepts the same address through `--connect` or `--join`.
`Session.room` reports the endpoint with its assigned match ID once admitted.
A match ID identifies a match; it grants no permission by itself.
A [sharded game](./shards.md)'s server runs one world split across its matches instead, and match 0 joins it.

Anonymous creation and joining are explicit testing policies. Without
`--anonymous`, configure authentication and admission policy through the
[server entry point](../c/network.md#dedicated-hosting). Creation and joining
are authorized separately, so a backend can create reserved matches while
players only receive permission to join them. Verification can complete
asynchronously without blocking simulation. Accepted identities bind player
reconnections; credentials and account metadata stay outside replicated worlds.

The built-in client address form is anonymous. Authenticated clients and trusted
services use `tide/hosting.h` with a game's verifier and credentials. Credentials
require WebRTC/DTLS or a custom transport that provides equivalent protection;
the hosting layer refuses credentials over its plain UDP path. Tide supplies
the admission interface, not an account database or a particular identity provider.

The server starts matches in match `Main` by default. For a local menu `Main`,
provide a process entry point that selects the match scene and its typed startup
parameters. See [server options](../tools/cli.md#dedicated-servers).

Server builds can omit assets reached only from client code. Assets used by
simulation, including helper calls, defaults and dynamic match asset references,
remain available. Server and client retain the same match compatibility identity;
different pack bytes use the existing self-contained snapshot transfer path.

## Host migration

When the machine running a match leaves, the match ends for everyone, unless the game turns on host migration in its [settings](../language/basics.md#settings):

```csharp
settings
{
    hostMigration = true;
}
```

Then, when the machine running a room's match leaves or stops answering, another player's machine takes the match over, and the others join it again as the same players:

- The match goes on from the last tick the new host had verified. Players' predictions after it are rolled back, as when any guess is wrong.
- The last host's player leaves the match (`PlayerLeft`), and from then on entities without an owner read the new host's input.
- Local code sees no `Connected` or `Disconnected` while the match changes hands: `Session.state` is `Connecting` meanwhile, and views keep showing the last world they had. On the new host, `Session.server` becomes true.
- A player who doesn't come back to the new host within 20 seconds leaves the match.
- A host that leaves says so, and the match changes hands at once. One that stops answering is noticed after a few seconds.
- `Session.End()` ends the match for everyone instead: every player gets `Disconnected` with `Ended`, even one who missed the goodbye, as the relay keeps the room as ended for a while. So does a match whose last scene unloads.
- Only matches in a room change hands, as the room is where the players meet again, or on [a network of the game's own](./own-network.md) that says where its players meet. A closed match stays closed: its players come back, but no one new joins.

This migration path requires the full authoritative world. [Visibility-enabled](../game/visibility.md) clients receive partial worlds and cannot currently promote one into a host: Tide refuses that takeover until a trusted complete-state handover exists. Players can't pass for each other: they only ever get each other's cookies hashed (see [Coming back](#players-in-the-match)).

## Players in the match

In the match, players come and go through the built-in events `PlayerJoined` and `PlayerLeft`, with the player's `PlayerID`. The server's own player joins before the match's first tick.

```csharp
event(PlayerJoined joined) GiveBody()
{
    Spawn(Body, Owner { player = joined.player });
}
```

What each player controls is up to the `Owner` component (see [Input](../game/input.md)).

**Coming back:** joining a server gives this machine a cookie, and joining the same server again presents it, so the player gets their `PlayerID` back, and with it whatever the game kept for them. If the server still thought they were connected, the new connection takes over with no events at all; if they'd left, `PlayerJoined` comes again with the same `PlayerID`. The cookie lasts as long as the program, for now.

## Input delay

By default there is none: a player's own input applies at once. Clients run ahead of the server, by about half the round trip, so their input arrives in time. Only other players' inputs are ever guessed, and when a guess is wrong, the client rolls back and runs the ticks again. See [Networking](./networking.md#prediction-and-rollback) for how.

Running ticks again costs time: a wrong guess runs about as many ticks again as the client is ahead, and on a busy match that's most ticks. A client measures what its rollbacks cost and keeps its delay at the least where they fit: one rollback may take up to about a frame, and never less than a 60th of a second, and all of them together up to a budget an update (4 ms, unless the host says otherwise), on average. Past either it delays its input a tick at a time, and takes the delay back within a second or two of there being room again, or of there being no rollback at all. A client whose guesses are right pays no delay for ticks it never runs again, and the player on the server's own machine never has any. `tide run --stats` shows the delay when there is one.

A game can ask for delay in place of prediction altogether:

```csharp
settings
{
    prediction = Prediction.Delay;
}
```

Then clients never predict. Their inputs are delayed so that every player's reach the server before its tick, and ticks run once each, as the server confirms them: nothing is ever rolled back, at the cost of input delay of about a round trip and a half. A game whose ticks are expensive, or whose views can't show a rollback well, picks it; the server runs the match the same either way.

## How many players

A match takes 16 players unless the game says otherwise:

```csharp
settings
{
    maxPlayers = 64;
}
```

`maxPlayers` goes from 1 to 65,536, the same supported ceiling as a dedicated server's total connections. The settings are separate: one limits a match, the other limits connections across all of the server's matches. One player more is refused, as from a full match (`DisconnectReason.Refused`). The world keeps an input for every player it can take, so the number is part of the game's data: builds with different `maxPlayers` can't share a match, and changing it under `tide run` starts the match over.

Pick what the game is built for. In a full-world match every player receives every other's input and simulates the whole world, so traffic can grow with the square of its players. [Visibility](../game/visibility.md) restricts state, simulation and owner-input delivery, while the server still simulates the complete match.

## Limits for now

- Under `tide run --web`, a reload plays on alone: the room closes, and the other players drop out.
- A match can't start in a scene that holds text or lists yet.
