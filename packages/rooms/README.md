# Rooms

The menu a game opens on, and the way back to it from a match, so a game is playable with friends before it has a menu of its own: a title page (Play, Host a room, Join a room), a page to type a friend's code into, the room's page (the code to copy, who's in it, Resume, Leave) and a Menu button in the match. It's a page of Tide, with nothing a game couldn't write itself.

The docs say how it's used: https://tide-engine.dev/multiplayer/matches#a-menu-that-comes-with-tide

## Using it

A game lists it in its `tide.packages`:

```
rooms
```

That's all a game needs. `Main` stays the match's scene: the match runs behind the title, **Play** closes the menu, **Host a room** opens the match to others and **Join a room** leaves it for someone else's. A game started with `--host` or `--join`, or a page opened with `?host` or `?join=`, has no menu in its way. The pages are the GUI's own widgets, in whatever theme the game sets, and the devices are theirs while a page is up.

What a game reads of it is the local singleton `Lobby`, through `using Rooms;`:

```csharp
using Rooms;

input Controls
{
    bool jump;

    Sample(Devices devices, Lobby lobby)
    {
        jump = lobby.playing && devices.keyboard.space.down;
    }
}

view Hud(Lobby lobby)
{
    if (lobby.page != Page.Playing) return;
    ...
}
```

- `playing`: the player's hands are on the game. No page is up, the match is here, and the press that closed the last page is let go.
- `page`: `Playing`, `Title`, `Join` or `Room`, for a HUD that hides under a page.

What a game may tell the room's page, each frame, from a view that takes `mut Lobby`:

- `seats`, `manned`, `mine`, `bots`: how many seats to list, which have a player (a bit each), this machine's player's, and whether bots have the rest.
- `offersNewGame` and `offersBots` put **New game** and **Bots: on/off** on the page. Pressed, they set `newGame` and `toggleBots` for one frame, for the game to send its commands from. **Play** on the title sets `newGame` too: a game of this machine's own, from its start.

Three functions are the game's to replace with an `override`: `Rooms.Title()` (the game's `title` setting, `Application.title`), `Rooms.Tagline()` (the line under it) and `Rooms.MenuButton(screen)` (where the Menu button goes; the bottom left corner).

## Outgrowing it

Copy `rooms.tide` out of the installation's `src/packages/rooms` into the game, drop the line from `tide.packages`, and change what you like.

## Tests

`tide_rooms_lobby` (`test/`) runs the package with a small game, with no window: where the pages leave a machine by how it started, what each page's buttons ask of the session, driven by the keyboard, and what the game reads of it.
