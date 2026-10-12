# Steam

Steam for the `lobby` package's four slots: the Steam user as the player's identity, Steam's lobbies behind every lobby function and the quick match, and a lobby's match on Steam's own network, joined by its host's Steam ID. The game's code doesn't change: the menu, `LobbyClient` and the calls are the lobby package's.

It's Tide and C, built on what any game or package can use, with nothing of the engine made for Steam: the lobby package's `virtual` functions, C functions over Steamworks' flat API (`bridge.c`), and a network of the game's own (`tide/network.h`).

**It hasn't been run against Steam.** Its tests run it against a stand-in for Steam's library that the tests bring. A first run with the real SDK will find things.

The docs say how it's used: https://tide-engine.dev/packages/steam

## Using it

A game lists it for the builds that ship on Steam, beside the lobby package:

```
lobby
#if STEAM
steam
#endif
```

`tide build --release --define STEAM` builds with it. Without the symbol the build has Tide's own lobby service and rooms, and none of this package: not its code, its C or Steam's library. It's desktop only: a web or Android build with the symbol stops with an error that says so.

## What the game brings

The Steamworks SDK isn't in tide and can't be passed on. From the SDK's `redistributable_bin`, the game's folder has the library of each platform it ships on (tide links every library in a game's folder that's for the platform it builds):

| Platform | Files |
|---|---|
| Windows | `win64/steam_api64.dll` and `win64/steam_api64.lib` |
| Linux | `linux64/libsteam_api.so` |
| macOS | `osx/libsteam_api.dylib` |

No SDK header is needed: `steam_flat.h` declares the functions and structs the package calls, written from SDK 1.60. A newer SDK may give an interface's accessor another version number (`SteamAPI_SteamUser_v023`): those names are all in that file. While the game is developed, `steam_appid.txt` with its App ID is beside the program.

## What's where

- `steam.tide`: the overrides of the four slots, `Steam.Start()`, `Steam.InviteFriends()`, and the view that follows a friend's invite.
- `bridge.c`: Steam, through its flat C API with callbacks run by hand, and the match's network.
- `steam_flat.h`: the part of the flat API it calls.
- `test/` (this repository only): a game that lists the package, and `fake_steam.c`, a Steam for several users in one program, in the SDK's library's place.

## Where Steam differs from Tide's own service

- A member's name is its Steam name; `SetName` changes nothing in a lobby.
- A quick match lists, then joins or creates, so two players asking at the same moment can each make a lobby.
- A kick is a list of Steam IDs in the lobby's data that the listed player's own program obeys. Steam has no kick.
- An unlisted lobby is a private one.
- The lobby's data has the package's own keys in it, which start with `tide_`, and `name`.
- Who owns a lobby is Steam's to say, and with `hostMigration` on, the lobby's owner is who hosts its match next.

## The first run with Steam

Not done yet. Two machines, two accounts, the Steam client running on each, `steam_appid.txt` and the SDK's library in place, `tide run --define STEAM`: sign in (a 17-digit `client.me.id`), create a lobby on one and find it on the other, ready, start the match, play, then leave the lobby on the host with `hostMigration` on and see the other go on as host.
