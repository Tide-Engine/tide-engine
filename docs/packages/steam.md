---
description: "Ship a Tide game's lobbies and matches on Steam with the steam package: Steam identity, Steam lobbies, quick match and Steam's own networking, picked per build."
---

# Steam

The bundled `steam` package, in the namespace `Steam`, puts a game's [lobbies](./lobby.md) and matches on Steam for the builds that ship there. A game lists it under a condition, beside the lobby package it fills in:

```
lobby
#if STEAM
steam
#endif
```

`tide build --release --define STEAM` then builds with it, and every other build has Tide's own lobby service and rooms. The game's code is the same either way: the menu, `LobbyClient` and every call are the lobby package's.

::: warning Not run against Steam yet
The package is tested against a stand-in for Steam's library that Tide's tests bring, with several users in one program. It hasn't been run against the Steam client. Expect a first run with the real SDK to find things, and see [the first run with Steam](#the-first-run-with-steam).
:::

It's a package like any other, built on what any game can use: the lobby package's [`virtual` functions](./lobby.md#another-service), [C functions](../c/functions.md) over Steamworks' flat C API, and [a network of the game's own](../multiplayer/own-network.md). Nothing in the engine knows about Steam, so a package for another store is written the same way.

## What a game brings

Steamworks' SDK isn't part of `tide` and can't be: its license doesn't let it be passed on. From the SDK's `redistributable_bin` folder, put the library for each platform the game ships on anywhere in the game's folder, where `tide` links [every library it finds](../c/functions.md#where-the-c-goes) for the platform it builds:

| Platform | Files |
|---|---|
| Windows | `win64/steam_api64.dll` and `win64/steam_api64.lib` |
| Linux | `linux64/libsteam_api.so` |
| macOS | `osx/libsteam_api.dylib` |

The package declares the functions it calls itself, written from SDK 1.60 (`steam_flat.h` in the package), so no SDK header is needed, and no C++. A newer SDK may name an interface's accessor with another version number: those names are all in that one file.

While the game is developed, a `steam_appid.txt` with the game's App ID beside the program is what tells Steam which game it is, as with any Steam game.

Steam is desktop only, and so is the package. A web or Android build that lists it stops with an error that says so, which is what the `#if STEAM` line is for: those builds leave the symbol out.

## What each slot is on Steam

| Slot | On Steam |
|---|---|
| Identity | The Steam user: `client.me.id` is its Steam ID, and its name the one its friends see |
| Lobby | Steam's lobbies. A lobby's ID is Steam's, members are Steam users, and data is Steam's lobby data and member data |
| Matchmaking | A search of Steam's lobbies by the request's mode and attributes, which joins the fullest or makes one. The ticket is `Searching` while Steam answers |
| Transport | Steam's own networking (`ISteamNetworkingMessages`): a match is joined by its host's Steam ID, which is what `Session.room` says. No Tide relay or room is involved |

Steam starts the first time a player asks something of the lobby (the first sign-in). Signing in fails with `SignedOut` when the Steam client isn't running or doesn't know the game. `Steam.Start()` starts it sooner, for a game that wants invites from its first frame.

### Where Steam differs

- **Names are Steam's.** `SetName` changes nothing in a lobby: members see each other's Steam names.
- **A quick match isn't one question.** Steam lists, then the game joins or creates, so two players who ask at the same moment can each make a lobby. A made lobby's data is what was searched for.
- **A kick is the owner's list.** Steam has no kick. The owner writes the player's Steam ID into the lobby's data, and that player's own program leaves when it reads so, and leaves again if it comes back. A program that isn't the game's could ignore it: a host that wants a player out of the match uses [`Session.Kick`](../multiplayer/matches.md#players-in-the-match) as well.
- **An unlisted lobby is a private one**, joined by its ID or an invite.
- **The lobby's own bookkeeping is in its data**, under keys that start with `tide_`, and `name`. A game's own keys can't use those.
- **Data has limits**: keys of up to 63 bytes and values of up to 256 are shown, 32 keys of a lobby and of a member.
- **Who owns a lobby is Steam's to say.** When the owner leaves, Steam picks the next one.

### Invites

`Steam.InviteFriends()` opens Steam's own dialog to ask friends into the lobby this player is in. A player who accepts an invite, or picks **Join game** on a friend, while the game runs leaves the lobby they're in and joins that one, with nothing for the game to write.

## Host migration

With `settings { hostMigration = true; }`, a lobby's match changes hands when its host goes, and the lobby's owner is who hosts next. Steam is the one authority: it hands a lobby to another member when its owner leaves the lobby or goes offline.

- The host's machine tells its players the lobby's ID as the place to meet.
- A player whose match lost its host asks Steam who owns the lobby. While that's still the host that went, it waits. Once it's another member, that member's machine takes the match over, and every other player joins it at its Steam ID.
- The lobby then names the match by its new host, so a player who joins the lobby later goes to the right one.
- A match its host ended for everyone (`Session.End()`) is marked so in the lobby's data, and nobody takes it over: players who missed the goodbye go offline with `Ended`.

What follows from Steam's model:

- **The match follows the lobby's owner, not the first player to notice.** With Tide's rooms the first player back at the room hosts; here it's whoever Steam made the owner.
- **A host that leaves the match but stays in the lobby as its owner** takes nobody's place: Steam still names it. The lobby package has the members leave such a match when the lobby stops naming one, and a game without that sees them time out after 30 seconds.
- **A host that crashes is noticed when Steam drops it from the lobby**, which is Steam's timing, not Tide's.
- **A match with no lobby doesn't change hands**: there's no owner to follow. Only a match its lobby's owner hosts has a place to meet.

## The first run with Steam

Nothing below has been done yet. For a first run against the real thing:

1. The Steam client is running and signed in, on two machines with two accounts that both own the game (or use App ID 480, Valve's test app, while trying).
2. `steam_appid.txt` with the App ID is beside the program, and the SDK's library for the platform is in the game's folder.
3. `tide run --define STEAM --stats` on both. The first thing to see is the sign-in: the title page's buttons work, and `client.me.id` is a 17-digit Steam ID.
4. **Create a lobby** on one, **Browse lobbies** on the other: the lobby shows, with its name and player count. Join it, set ready on each, and see the other change.
5. **Start the match** on the owner: both are in it, `Session.room` is the owner's Steam ID on both, and input from each moves on the other.
6. Leave the lobby on the host with `hostMigration` on: the other machine goes on with the match as its host.

What the stand-in can't tell, and a first run will: whether the accessors' version numbers match the SDK used, whether a Windows game (built for MinGW) links Valve's import library as it is, how long Steam takes to hand a lobby on, and how Steam's relays behave on a real network.

## Limits for now

- **Not run against Steam.**
- **Launching the game from an invite** (Steam starts it with `+connect_lobby`) isn't read: invites are followed while the game runs.
- **`Session.ping` counts the wait for a frame** on Steam's network: the package doesn't read Steam's own timestamps.
- **No Steam Datagram Relay settings, no dedicated servers, no friends list, no rich presence, no lobby chat.** The package fills the four slots and nothing else.
- **One lobby at a time**, as the lobby package has it.
