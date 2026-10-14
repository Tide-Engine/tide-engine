---
description: "Add lobbies and a quick match to a Tide game with the lobby package: a menu, ready flags, lobby data, and slots for another service such as Steam."
---

# Lobby: lobbies and matchmaking

A [room](../multiplayer/matches.md#rooms) is a match players reach by a code. A lobby comes before the match: a group of players who see each other, say they're ready, and go into a match together. The bundled `lobby` package is a lobby service's client and a menu over it, in the namespace `Lobbies`, so a game has both before it writes either. List it in the game's `tide.packages`:

```
lobby
```

The game then opens on a title page, with its name over **Play alone**, **Create a lobby**, **Browse lobbies**, **Quick match** and **Join by ID**. A lobby's page shows its ID to share, its members and who's ready, and for its owner **Start the match**, **Kick** beside each member and a switch that closes the lobby to others. In the match, **Menu** in the bottom left corner, or Escape, brings the lobby's page back.

It's drawn over a live match, as [the rooms menu](./rooms.md) is: `Main` stays the match's scene, so the game runs behind the title, and **Play alone** closes the menu. A game started with `--host` or `--join`, or a page opened with `?host` or `?join=`, has no menu in its way. `rooms` is the smaller of the two: a code and nothing else. Both are packages that come with `tide`, and a game lists the one it wants.

::: info Tide's own service
The lobbies are Tide's relay's, which also introduces the players of a room. It's for building and testing a game: nothing is installed, there are no accounts, and two windows on one machine are two players. A game that ships on a store's service [replaces the parts it needs](#another-service) and keeps the rest.
:::

## What a lobby is

A lobby is a group of members with a little data, and a match that may be attached:

- **Its members**, in the order they joined, each with an ID, a name and data of its own: text by key, like `ready`, a team or a character. A member changes its own.
- **Its owner**, a member, who changes the lobby: its name, how many members it takes, whether lists show it (`listed`), whether others can join (`open`), and its data. When the owner leaves, the member who has been there longest owns it. A lobby nobody is in is gone.
- **Its data**, text by key, which lists filter by: a mode, a map, a region.
- **Its match**, how to reach it, or nothing while there's none. A lobby with no match yet is what other services call a party.

A member sees every change as it happens, and a member whose program stops asking is taken out of the lobby by the service after a while.

## What the game reads

One local singleton, `LobbyClient`, through `using Lobbies;`:

```csharp
using Lobbies;

input Controls
{
    bool jump;

    Sample(Devices devices, LobbyClient client)
    {
        // Nothing is sent while a page is up, or until the press that closed it is let go
        jump = client.playing && devices.keyboard.space.down;
    }
}

view Hud(LobbyClient client)
{
    if (client.page != LobbyMenu.Playing) return;
    GUILayout.Area(Anchor.UpperLeft)
    {
        foreach (var member in client.lobby.members) GUILayout.Label(member.name);
    }
}
```

| Field | What it says |
|---|---|
| `playing` | The player's hands are on the game: no page is up, the match is here, and the press that closed the last page is let go |
| `page` | `LobbyMenu.Playing`, `Title`, `Browse`, `Join`, `Lobby` or `Searching`, for a HUD that hides under a page |
| `newGame` | True for the frame **Play alone** was pressed in, for the game to start over |
| `lobby` | The lobby this player is in; `lobby.id` is `""` with none. `name`, `owner`, `max`, `listed`, `open`, `data`, `match`, `you` (this player's ID in it) and `members` |
| `owned` | The lobby is this player's |
| `lobbies` | What the last list said: each lobby's `id`, `name`, `players`, `max` and `data` |
| `busy` | `LobbyBusy.Idle`, `Creating`, `Joining` or `Searching` |
| `message`, `failure` | What went wrong last, or what became of the lobby or its match: text for a page, and a `LobbyFailure` (`Full`, `Closed`, `Kicked`, `Unreachable` and so on) |
| `me` | Who this player is to the service, once it was first asked: `id`, `name` |

`LobbyClient` is local state: nothing of a lobby is in the match. What the match needs of it, like which team a player picked, goes in with a [command](../game/commands.md) once the match is on.

The game sets `client.name`, this player's name, from a view. Until it does, the name is the one the identity gives, or `Player`.

## What the game asks

Each is a call with `mut LobbyClient` first, from a view or a page of the game's. The service is asked at once, and `LobbyClient` says what became of it some frames later: `busy` while it waits, then the lobby, or `message` and `failure`.

```csharp
view Lobby(mut LobbyClient client)
{
    if (client.lobby.id == "") return;
    var ready = IsReady(Mine(client));
    GUILayout.Area(Anchor.UpperRight)
    {
        if (GUILayout.Button(ready ? "Not ready" : "Ready")) SetReady(client, !ready);
        if (client.owned)
        {
            GUI.Disabled(!AllReady(client.lobby))
            {
                if (GUILayout.Button("Start")) StartMatch(client);
            }
        }
    }
}
```

| Call | What it does |
|---|---|
| `HostLobby(client)` | Makes the lobby `NewLobby` says, and joins it as its owner: the Create button |
| `CreateLobby(client, options)` | The same, for a `LobbyOptions` of your own: `name`, `max`, `listed`, `data` |
| `RefreshLobbies(client)` | Lists the lobbies `client.query` asks for (`filter`, lobby data to match, and `seats` free) into `client.lobbies` |
| `JoinLobby(client, id)` | Joins the lobby with that ID |
| `FindMatch(client)` | Looks for the match `QuickRequest` says: the Quick match button |
| `QuickMatch(client, request)` | The same, for a `MatchRequest` of your own |
| `CancelLobby(client)` | Stops waiting for a lobby that was asked for. One that's joined meanwhile is left |
| `LeaveLobby(client)` | Leaves the lobby, and its match with it |
| `SetName(client, name)` | This player's name, here and in its lobby |
| `SetReady(client, ready)` | The `ready` key of this member's data. `IsReady(member)` and `AllReady(lobby)` read it |
| `SetMemberData(client, key, value)`, `RemoveMemberData(client, key)` | A key of this member's own data |
| `SetLobbyName`, `SetLobbyMax`, `SetLobbyListed`, `SetLobbyOpen`, `SetLobbyData`, `RemoveLobbyData` | The owner's: one thing about the lobby. `UpdateLobby(client, change)` changes several at once |
| `Kick(client, player)` | The owner's: sends a member out, who can't come back to that lobby |
| `TransferLobby(client, player)` | The owner's: hands the lobby to another member |
| `StartMatch(client)`, `EndMatch(client)` | The owner's: opens this machine's match to the lobby, and ends it for everyone |
| `RejoinMatch(client)` | Into the lobby's match again, after this machine lost it |

What only the owner does is refused for anyone else, with `failure` set to `LobbyFailure.NotOwner`.

## The lobby's match

**Start the match** opens the owner's machine's match to others, as [`Session.Open`](../multiplayer/matches.md#starting-a-match-from-code) does. An owner who's playing in someone else's match, having started with `--join`, leaves it for a match of its own first. Once that match has a room, the lobby says the room's code as its `match`, and every member's machine joins that room, with no code for anyone to type. A member who joins the lobby later joins the match too. Who gets in, and what a late player finds, is the game's: `Session.Close()` in the match keeps latecomers out.

**End the match** puts the owner in a match of its own again, and the lobby says it has none: every member's machine leaves for a match of its own, with the lobby's page up. A match that ends some other way, or can't be joined, puts a member on the lobby's page with a line that says why, and **Rejoin the match** tries again. Leaving the lobby leaves its match.

## A quick match

**Quick match** asks for a lobby rather than making or picking one: it joins a listed lobby with room, the fullest first, or makes one when there's none, and the player lands on that lobby's page. Two players who press it at the same moment end up in one lobby, as the service does both in one request.

What it looks for is `QuickRequest(player)`, which a game overrides: a `mode`, `attributes` that have to match, and `seats`, how many it needs free.

```csharp
override MatchRequest Lobbies.QuickRequest(string player)
{
    mut var request = MatchRequest { mode = "duel", max = 2, name = $"{player}'s duel" };
    request.attributes["region"] = "eu";
    return request;
}
```

The search is a ticket, `client.ticket`: `Searching` until it's `Found`, with the lobby it ends in. Tide's own service finds or makes the lobby at once. A service with a queue keeps a ticket searching for as long as it takes, and the menu's page waits with it.

## Making the menu yours

Three ways, from the least work to the most.

**The theme.** The pages are the GUI's own widgets, in whatever [theme](../game/gui.md#themes) the game sets: `GUI.SetTheme` in a view of the game's is their look.

**A page of your own.** Each page is a function a game [overrides](../language/overrides.md) to draw it its own way, while the others stay the package's: `Lobbies.TitlePage(session, client)`, `BrowsePage(client)`, `JoinPage(client)`, `SearchPage(client)` and `LobbyPage(session, client)`. Its buttons call the functions above, so the menu keeps its way through its pages:

```csharp
using Lobbies;

override void Lobbies.SearchPage(mut LobbyClient client)
{
    GUI.Label(Rect(40, 200, 300, 32), "Finding players...");
    if (GUI.Button(Rect(40, 240, 200, 48), "Stop")) ShowTitle(client);
}
```

The moves between pages are calls as well: `PlayAlone(client)`, `ResumeMatch(client)`, `ShowMenu(client)`, `ShowTitle(client)`, `ShowBrowser(client)`, `ShowJoin(client)`, `MoreLobbies(client)`, `CopyLobbyId(client)` and `QuitMatch(client)`, which leaves a match the machine was told to join. An override has the parameters of what it replaces, so a page that needs more (the game's own state, or `Devices`) is a view of the game's: override the package's page with an empty body, and draw yours from a view that returns unless `client.page` is that page.

Smaller things are functions to override too: `Title()`, `Tagline()` and `MenuButton(screen)`; `NewLobby(player)`, the lobby the Create button makes; and `Explain(failure)`, a failure in words.

**`tide eject lobby`** copies the package into the game, as `packages/lobby`, and points the game's `tide.packages` line at the copy: it's the game's own from there.

## The service

`Lobbies.ServiceUrl()` says where the lobby service is: Tide's relay, unless the game overrides it for a server of its own that answers the same requests.

```csharp
override string Lobbies.ServiceUrl()
{
    return "https://lobbies.example.com";
}
```

`Lobbies.ServiceGame()` says which game's lobbies these are, the game's `title` unless it says otherwise: players of two games never see each other's.

A request the service hasn't answered in `client.timeout` seconds (10) counts as one that couldn't reach it, so a service that takes requests and never answers shows as `LobbyFailure.Unreachable`, as one that's down does. The request a member watches its lobby with is held by Tide's service until the lobby changes, for about 20 seconds at the most, so it has a limit of its own, `client.watchTimeout` (30). Both are fields a game sets, and 0 is no limit.

A player is a secret the service makes the first time the game asks it anything. The machine keeps it in `lobby-identity.json` among the game's [saved files](../game/files.md), so a player has the same ID the next time the game runs. Two programs of one game on a machine share that file: the second to join a lobby the first is in joins as a player of its run's own. `Lobbies.IdentityFile()` names the file, and `""` keeps none.

## Another service

The package is four slots, each a set of `virtual` functions whose bodies are Tide's own service. A package for another service overrides the slots it has an answer for, and the game keeps the menu, `LobbyClient` and the calls above as they are.

| Slot | Functions | Tide's own |
|---|---|---|
| Identity | `IdentitySignIn` | A secret the service made, kept on this machine |
| Lobby | `LobbySignIn`, `LobbyCreate`, `LobbyList`, `LobbyJoin`, `LobbyQuickJoin`, `LobbyLeave`, `LobbyUpdate`, `LobbySetMember`, `LobbyKick`, `LobbyTransfer`, `LobbyWatch` | The relay's lobbies, over HTTP |
| Matchmaking | `TicketStart`, `TicketCheck`, `TicketCancel` | A quick join |
| Transport | `MatchOpen`, `MatchAddress`, `MatchJoin`, `MatchLeave` | Tide's rooms |

The slots are replaced apart: a store's lobbies with Tide's rooms as the way into the match, a store's identity with Tide's lobbies, or Tide's lobbies with another way in. Tide's own lobbies know a player by a secret their service made. With Tide's identity that's the one it keeps; with another, `LobbySignIn` asks the service for a player of this run's, so an identity package needs nothing of them. A package that brings lobbies of its own overrides `LobbySignIn` with what its service needs, often `return who;`.

A build picks its packages with [`#if` lines](../packages/using-and-writing.md#packages-for-some-builds-only) in `tide.packages`, so a shipping build has the store's service and every other build has Tide's:

```
lobby
#if STEAM
steam
#endif
```

The identity, lobby and matchmaking functions are [`async`](../game/tasks.md): each asks its service, waits, and gives an answer for the package to act on.

```csharp
// The store's own calls, in the package's C
extern int StoreJoin(string id);
extern bool StoreDone(int request);
extern bool StoreJoined(int request);

override async LobbyAnswer Lobbies.LobbyJoin(LobbyClient client, string id)
{
    var request = StoreJoin(id);
    while (!StoreDone(request)) await Wait.Frames(1);
    if (!StoreJoined(request)) return LobbyAnswer { failure = LobbyFailure.Full };
    return LobbyAnswer { ok = true, lobby = StoreLobby(request) };
}
```

`StoreLobby` there is the package's own function, which makes a `LobbyInfo` of what the store says. An override has the parameters of what it replaces, so what a service keeps between calls is in its own C, or in what the functions are handed.

The transport slot goes with [a network of the game's own](../multiplayer/own-network.md): `MatchOpen` and `MatchJoin` register it and then call `Session.Open()` and `Session.Join(address)`, and what the lobby says of its match is what that network's `Session.room` is. That page builds a whole service and network of a game's own, and [`steam`](./steam.md), which comes with `tide`, is one for Steam.

Where a match is joined by its host (a Steam ID) and changes hands with [host migration](../multiplayer/matches.md#host-migration), the lobby follows: its owner's machine, once it runs the match, names it again, and the members' machines, which went there with the match, take the new name.

## Limits for now

- **Tide's own service is for building and testing.** A player is whoever holds its secret, with no account behind it, and a name is what the player says. Lobbies are kept in the relay's memory: a relay that restarts has none.
- **One lobby at a time.** A party that stays together from one match's lobby to the next isn't built: `seats` in a `MatchRequest` asks for room for a group, and the group's members each join the lobby that's found.
- **The quick match is find-or-create.** There's no queue, and no rule for who's matched with whom beyond the request's mode and attributes.
- **A kick is from the lobby.** The kicked player's machine leaves the lobby's match itself. A host that wants a player out of the match as well uses [`Session.Kick`](../multiplayer/matches.md#players-in-the-match).
- **The Browse page shows six lobbies at a time**, of the 50 at most a list has, with **More** for the next six.
- **In a browser, each open lobby holds one request to the service**, and a browser allows a site six at once to one address. More than six tabs of a game in one browser wait on each other.
