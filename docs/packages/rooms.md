---
description: "Add the bundled Rooms package to a Tide game for Play, Host and Join menus, room codes, volume settings and customizable pages."
---

# Rooms

`rooms` is an optional package bundled with Tide, in the namespace `Rooms`. It provides a ready-made menu over the engine's [sessions and room codes](../multiplayer/matches.md). The engine can host and join matches without this package; Rooms supplies the pages and buttons players use to do it.

## Using Rooms

A game needs all of the above before two people can play it, and a menu to reach them from. The `rooms` package is that menu, so a game has one from its first day and writes its own when it's ready. List it in the game's `tide.packages`:

```
rooms
```

The game then opens on a title page, with its name over **Play**, **Host a room** and **Join a room**; a page to type a friend's code into, which Enter joins from; and the room's page, with the code to copy, who's in it, **Resume** and **Leave the room**. In the match, **Menu** in the bottom left corner, or Escape, brings the room's page back. It's drawn over a live match: `Main` stays the match's, so the game runs behind the title, and **Play** closes the menu. A game started with `--host` or `--join`, or a page opened with `?host` or `?join=`, has no menu in its way. A match that ends, or a room that can't be joined, puts the machine back on the title with a line that says why.

**Settings** on the title or room's page opens a **Volume** slider, from mute to full volume, for this machine's audio. Changes apply at once; **Back** or Escape returns to the page that opened it. The local `Lobby.volume` goes from `0` to `1` and starts at `1`. It stays through starting, joining and leaving matches while the game is open, and resets to full volume when the game restarts. The settings page has `Lobby.page == Page.Settings`, so the same HUD and input checks below cover it.

The pages are the GUI's own widgets, in whatever [theme](../game/gui.md#themes) the game sets, and the devices are theirs while a page is up. What the game reads of it is one local singleton, `Lobby`, through `using Rooms;`:

```csharp
using Rooms;

input Controls
{
    bool jump;

    Sample(Devices devices, Lobby lobby)
    {
        // Nothing is sent while a page is up, or until the press that closed it is let go
        jump = lobby.playing && devices.keyboard.space.down;
    }
}

view Hud(Lobby lobby)
{
    if (lobby.page != Page.Playing) return;
    ...
}
```

A game can tell the room's page about its match, each frame, from a view that writes `Lobby`: `seats`, `manned` (a bit per seat a player has), `mine` and `bots` list who has each seat, and `offersNewGame` and `offersBots` put **New game** and **Bots: on/off** on the page, which set `newGame` and `toggleBots` for the frame they're pressed in, for the game to send its commands from. **Play** on the title sets `newGame` too. The title is the game's `title` setting (`Application.title`), and three functions are the game's to [override](../language/overrides.md): `Rooms.Title()`, `Rooms.Tagline()` (the line under it) and `Rooms.MenuButton(screen)` (where the button goes).

## Pages of your own

Each page is a function too, which a game overrides to draw that page its own way while the others stay the package's: `Rooms.TitlePage(session, lobby)`, `Rooms.JoinPage(session, lobby)`, `Rooms.RoomPage(session, room, lobby)` (`room` is the room's code, `session.room`, which a function is handed) and `Rooms.SettingsPage(lobby)`. What a page's buttons do is a call each, so a page of the game's keeps the menu's way through its pages:

```csharp
using Rooms;

override void Rooms.TitlePage(Session session, mut Lobby lobby)
{
    if (GUI.Button(Rect(40, 200, 200, 48), "Play")) Play(lobby);
    if (GUI.Button(Rect(40, 256, 200, 48), "Play with friends")) Host(lobby);
    if (GUI.Button(Rect(40, 312, 200, 48), "Enter a code")) ShowJoin(lobby);
    if (GUI.Button(Rect(40, 368, 200, 48), "Settings")) ShowSettings(lobby);
}
```

| Call | What it does |
|---|---|
| `Play(lobby)` | Into the match, as a new game: the game sees `newGame` |
| `Resume(lobby)` | Into the match as it is |
| `Host(lobby)` | Opens the match to others, and shows its room |
| `Invite()` | Opens the match to others from the room's page |
| `ShowJoin(lobby)` | To the page a code is typed into |
| `Join(lobby, code)` | Asks to join the room with that code |
| `LeaveJoin(session, lobby)` | From the Join page to the title, out of a match that was being joined |
| `ShowSettings(lobby)`, `LeaveSettings(lobby)` | To the settings, and back to the page they came from |
| `CopyCode(lobby, room)` | Copies the room's code |
| `Leave(session, lobby)` | Out of the room, to the title |

An override has the parameters of what it replaces, so a page that needs more (the game's own state, or `Devices`) is a view of the game's: override the package's page with an empty body, and draw yours from a view that returns unless `lobby.page` is that page. The same calls work there.

## Outgrowing it

`tide eject rooms` copies the package into the game, as `packages/rooms`, and points the game's `tide.packages` line at the copy: it's the game's own from there, to change as you like, and `tide upgrade` no longer updates it. It's a page of Tide, written with nothing a game can't use.

A game whose players gather before a match, see who's ready, browse what's open or press one button to find a match lists the `lobby` package instead: see [Lobbies and matchmaking](./lobby.md).
