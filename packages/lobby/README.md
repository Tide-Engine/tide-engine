# Lobby

Lobbies and a quick match for a game, before it has either of its own: a client for Tide's lobby service, which the relay runs beside its rooms, and a menu over it (Play alone, Create a lobby, Browse lobbies, Quick match, Join by ID; the lobby's page, with who's in it, who's ready and Start the match; a Menu button in the match). It's Tide, with nothing a game couldn't write itself.

It's made of four slots, each a set of `virtual` functions with Tide's own service as their bodies, so a game that ships on another service replaces a slot with a package that overrides it and keeps the rest: who a player is, the lobbies, matchmaking, and how a lobby's match is reached.

The docs say how it's used: https://tide-engine.dev/multiplayer/lobbies

## Using it

A game lists it in its `tide.packages`:

```
lobby
```

`Main` stays the match's scene, as with the `rooms` package: a match of this machine's own runs behind the title, and **Play alone** closes the menu. A lobby is a group of players; its owner's **Start the match** opens that machine's match to the lobby, and every member's machine joins it. A game started with `--host` or `--join`, or a page opened with `?host` or `?join=`, has no menu in its way.

What a game reads is the local singleton `LobbyClient`, through `using Lobbies;`:

```csharp
using Lobbies;

input Controls
{
    bool jump;

    Sample(Devices devices, LobbyClient client)
    {
        jump = client.playing && devices.keyboard.space.down;
    }
}

view Hud(LobbyClient client)
{
    if (client.page != LobbyMenu.Playing) return;
    foreach (var member in client.lobby.members) ...
}
```

| Field | What it says |
|---|---|
| `playing` | The player's hands are on the game: no page is up, the match is here, and the press that closed the last page is let go |
| `page` | `Playing`, `Title`, `Browse`, `Join`, `Lobby` or `Searching` |
| `newGame` | True for the frame **Play alone** was pressed in: a game of this machine's own, from its start |
| `lobby` | The lobby this player is in (`id` is `""` with none): `name`, `owner`, `max`, `listed`, `open`, `data`, `match`, `you` and `members`, each with `id`, `name` and `data` |
| `owned` | The lobby is this player's |
| `lobbies` | What the last list said: `id`, `name`, `players`, `max` and `data` of each |
| `busy`, `listing`, `ticket` | What's on its way: a lobby being made, joined or looked for, a list, and the quick match's ticket |
| `message`, `failure` | What went wrong last, or what became of the lobby or its match: as text for a page, and as a `LobbyFailure` |
| `me` | Who this player is to the service, once it was first asked: `id` and `name` |
| `match`, `hosting`, `joining` | The lobby's match this machine is in, whether it opened it, and whether it's on its way in |

And what a game sets: `name`, this player's name (the identity's, or `Player`, until then); `query`, what the Browse page lists (`filter`, the lobby data to match, and `seats`); and `service`, where the service is when a program is told as it runs (a test's port).

## What a game asks

Each is a call with `mut LobbyClient` first, from a view or a page. The service is asked after the frame's views, and `LobbyClient` says what became of it.

| Call | What it does |
|---|---|
| `HostLobby(client)` | Makes the lobby `NewLobby` says: the Create button |
| `CreateLobby(client, options)` | Makes a lobby (`name`, `max`, `listed`, `data`) and joins it as its owner |
| `RefreshLobbies(client)` | Lists the lobbies `client.query` asks for, into `client.lobbies` |
| `JoinLobby(client, id)` | Joins the lobby with that ID |
| `FindMatch(client)` | Looks for the match `QuickRequest` says: the Quick match button |
| `QuickMatch(client, request)` | Looks for a match (`mode`, `attributes`, `seats`; `name` and `max` of the lobby made when none is found) |
| `CancelLobby(client)` | Stops waiting for a lobby that was asked for; one that's joined meanwhile is left |
| `LeaveLobby(client)` | Leaves the lobby, and its match with it |
| `SetName(client, name)` | This player's name, here and in its lobby |
| `SetReady(client, ready)` | The `ready` key of this member's data, which `IsReady(member)` and `AllReady(lobby)` read |
| `SetMemberData(client, key, value)`, `RemoveMemberData(client, key)` | A key of this member's own data |
| `SetLobbyName`, `SetLobbyMax`, `SetLobbyListed`, `SetLobbyOpen`, `SetLobbyData`, `RemoveLobbyData`, `UpdateLobby(client, change)` | The owner's: what the lobby is |
| `Kick(client, player)`, `TransferLobby(client, player)` | The owner's: sends a member out for good, and hands the lobby on |
| `StartMatch(client)`, `EndMatch(client)` | The owner's: opens this machine's match to the lobby, and ends it for everyone |
| `RejoinMatch(client)` | Into the lobby's match again, after this machine lost it |

The pages' own moves are calls too: `PlayAlone`, `ResumeMatch`, `ShowMenu`, `ShowTitle`, `ShowBrowser`, `ShowJoin`, `MoreLobbies`, `CopyLobbyId` and `QuitMatch` (out of a match the machine was told to join, from the title).

## Making it the game's own

Three ways, from the least work to the most:

- **The GUI's theme.** The pages are the GUI's own widgets, so `GUI.SetTheme` in a view of the game's is their look.
- **A page of your own.** Each page is a function to `override`: `Lobbies.TitlePage(session, client)`, `BrowsePage(client)`, `JoinPage(client)`, `SearchPage(client)` and `LobbyPage(session, client)`. Its buttons call the functions above, so the menu keeps its way through its pages. A page that needs more than those parameters is a view of the game's that draws when `client.page` is that page, with the package's page overridden by an empty body.
- **`tide eject lobby`** copies the package into the game, as `packages/lobby`, and points its line in `tide.packages` there.

```csharp
override void Lobbies.SearchPage(mut LobbyClient client)
{
    GUI.Label(Rect(40, 200, 300, 32), "Finding players...");
    if (GUI.Button(Rect(40, 240, 200, 48), "Stop")) ShowTitle(client);
}
```

Smaller things are functions to override too: `Title()`, `Tagline()` and `MenuButton(screen)`, as in `rooms`; `NewLobby(player)` and `QuickRequest(player)`, what the Create and Quick match buttons ask for; and `Explain(failure)`, a failure in words.

## The service

`ServiceUrl()` says where it is: Tide's relay, `https://relay.tide-engine.dev`. A game overrides it for a relay of its own. In Tide's repository, `node relay/main.mjs` runs one on port 8080, which is how the package is tried on one machine:

```csharp
override string Lobbies.ServiceUrl()
{
    return "http://127.0.0.1:8080";
}
```

`ServiceGame()` says which game's lobbies these are (its `title` setting): players of two games never meet.

A player is a secret the service makes the first time it's asked, which this machine keeps in `lobby-identity.json` among the game's saved files (`IdentityFile()`; `""` keeps none). Two programs of a game on one machine share that file. The second one to join a lobby the first is in is told so, and joins as a player of its run's own, so two windows on one machine are two players.

## Another service

Each slot is `virtual` functions, for a package to override. A build picks its packages in `tide.packages`:

```
lobby
#if STEAM
github.com/someone/tide-lobby-steam 9c1d7be0a4f25d6c8e3b17f04a92d5c6e8f0a1b2
#endif
```

| Slot | Functions | Tide's own |
|---|---|---|
| Identity | `IdentitySignIn(client, fresh)` | A secret the service made, kept on this machine |
| Lobby | `LobbyCreate`, `LobbyList`, `LobbyJoin`, `LobbyQuickJoin`, `LobbyLeave`, `LobbyUpdate`, `LobbySetMember`, `LobbyKick`, `LobbyTransfer`, `LobbyWatch` | Requests to the relay's lobbies. `LobbyWatch` is answered when the lobby changes, and asked again each time |
| Matchmaking | `TicketStart(client, request)`, `TicketCheck(client, ticket)`, `TicketCancel(client, ticket)` | A quick join, which finds a lobby with room or makes one in one request: its ticket is found at once |
| Transport | `MatchOpen()`, `MatchAddress(session, room)`, `MatchJoin(address)`, `MatchLeave()` | Tide's rooms: the lobby's `match` is the room's code |

The slots are replaced apart, with one tie: Tide's own lobbies know a player by its identity's `secret`, so a package that replaces the identity alone brings one the relay takes.

The identity, lobby and matchmaking functions are `async`, take `LobbyClient` to read, and give an answer (`LobbyAnswer`, `LobbyListing`, `MatchTicket`): what's done with it is the package's. An override has the parameters of what it replaces, so what another service keeps between calls is in its own C, or in what the functions are handed.

## Tests

- `tide_lobby_menu` (`test/game.tide`, `menu_test.c`): a game that fills the identity, lobby and matchmaking slots itself, with no service and no window. Where the pages leave a machine by how it started, the pages driven by the keyboard, a ticket that searches before it's found, a page of the game's own, what the session is asked as a lobby's match starts, ends and is lost, and what becomes of a lobby that's full, gone or unreachable.
- `tide_lobby_relay` and `tide_lobby_relay_web` (`test/relay.tide`, `relay_check.h`): four players against the relay's lobbies on this machine, natively and in a page, from a player's first request to its last.
- `tide_lobby_beside_rooms` (`test/both.tide`): a game that lists `rooms` and `lobby` both.
