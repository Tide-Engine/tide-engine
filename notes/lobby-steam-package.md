# The Steam package

What's built, what code relies on, the tests, what was verified and what wasn't, and which decisions are the owner's. `AGENTS.md` has the rules every session needs and the index of these files. The hook it's built on is in `notes/own-network.md`; the package it fills the slots of is in `notes/lobby-package.md`.

## What it is

`packages/steam/` (built 2026-10-11), namespace `Steam`, a game's line `steam`: Tide (`steam.tide`) and C (`bridge.c`, `steam_flat.h`) that override the lobby package's four slots for Steam. No engine code is Steam's.

**The owner's decisions** (2026-10-11): the integration is anything "as long as any user could technically also do it themselves in your game without having to change the tide engine"; engine changes only at a wall; a build picks it with `#if STEAM` in `tide.packages` and `--define STEAM`; the match goes over Steam's own networking, not Tide's relay; host migration is supported where Steam makes sense of it, with lobby ownership as the authority.

**Everything else is the agent's pick, Provisional**, listed under Picks.

**The folder is `packages/steam`, not `packages/lobby-steam`** as it was first asked for: a package that comes with tide is named by a line of lower-case letters and digits (`packages_is_builtin`), which a hyphen isn't.

## It has not been run against Steam

The SDK isn't in the repository and can't be (its license). Everything below was verified against `test/fake_steam.c`, a stand-in written here from the SDK's documented behavior. Not verified, and the first live run's to find:

- The accessors' version suffixes for the SDK a game uses (`SteamAPI_SteamUser_v023`, `SteamAPI_SteamFriends_v017`, `SteamAPI_SteamMatchmaking_v009`, `SteamAPI_SteamNetworkingMessages_SteamAPI_v002`), written from memory of SDK 1.60's `steam_api_flat.h`, and `SteamAPI_InitFlat` (1.58 and later).
- Struct layouts: `steam_flat.h` has the SDK's layouts as understood (packing 8 on Windows, 4 on Linux and macOS). `LobbyCreated_t` is the one whose field offset differs by platform, and `steam_lobby_created` reads it by platform. `GetAPICallResult` is passed the size `SteamAPICallCompleted_t` says, never a `sizeof` of ours.
- Linking: a Windows game builds for MinGW, and whether lld links Valve's `steam_api64.lib` import library as it is.
- Steam's timing: how long ownership takes to pass when an owner's program dies; what `GetLobbyOwner` says in between; whether `LobbyDataUpdate_t` comes for an owner change (the package asks `GetLobbyOwner` each frame of a migration, so it needs no callback for it).
- `ISteamNetworkingMessages` on a real network: session setup time against the session's 15 seconds for a first answer, relays, `k_nSteamNetworkingSend_AutoRestartBrokenSession`.
- `k_ELobbyTypePrivate` for an unlisted lobby: whether `JoinLobby` by ID takes a player into one without an invite.

**The first live test** (also in the package's README and `docs/multiplayer/steam.md`): Steam client running and signed in on two machines with two accounts; `steam_appid.txt` (480 while trying) beside the program; the SDK's library in the game's folder; `tide run --define STEAM` on both; sign in, create and browse, join, ready, start, play, then leave the lobby on the host with `hostMigration` on.

## What code relies on

- **Plain C over the flat API, callbacks by hand.** `SteamAPI_ManualDispatch_Init`, then each frame `RunFrame` and `GetNextCallback` until none (`steam_update`, the network's `update`, so the engine runs it once a frame from the first sign-in on, in a match or not). No C++ and no SDK header.
- **One program is one Steam user.** `bridge.c`'s state is one `steam` struct. A test build defines `TIDE_STEAM_USERS` (4) and switches with `TideSteamUse`; nothing else is the tests'.
- **A call Steam answers later is a numbered request** (`requests`, 16): `TideSteamCreate`, `Join`, `List` and `Quick` give a number, `TideSteamDone` says when `SteamAPICallCompleted_t` came for it, `TideSteamTake` gives the answer and frees it. The Tide overrides wait a frame at a time. A quick match is a small machine in one request: list, then join the fullest lobby that hasn't this player on its kick list, or create; a join that fails (full meanwhile) creates.
- **A lobby crosses to Tide as JSON** in the relay's shape, read by `Json.Read<LobbyInfo>`. Text another player set (names, lobby data, member data) is bounded (keys under 64 bytes, values cut at 256, 32 keys, 256 members, a 64 KiB document) and made valid UTF-8 (`clean`: a `?` for each byte of text that isn't) before it's escaped. A document that doesn't fit is a failure, never a cut one.
- **What Steam has no field for is lobby data** under the package's keys: `name`, `tide_match`, `tide_listed`, `tide_open`, `tide_kicked`, `tide_ended`, and a member's `tide_keys` (Steam lists no keys of member data, so a member says its own). A game's keys can't start with `tide_` or be `name`, and those keys aren't in what the game reads.
- **`tide_match` is read as a Steam ID or nothing**: text there that isn't digits names no match.
- **The version a member sees** counts Steam's `LobbyDataUpdate_t` and `LobbyChatUpdate_t` for its lobby, and its own changes, which it sees at once.
- **A kick** appends the Steam ID to `tide_kicked`; the listed player's program leaves when it reads so (`obey_kicks`, at each update and right after it enters) and says `Kicked`. A full list forgets its oldest.
- **The network** (`tide/network.h`): a transport sends with `SendMessageToUser` (unreliable, no Nagle, auto-restart) on channel 0 and reads with `ReceiveMessagesOnChannel`, one message at a time, releasing each. An address is the account, the low half of the Steam ID; every player's high half is taken to be this player's (an individual's, of the same universe), and a message from any other kind of ID is dropped. A joined match hears its host alone. The text a match is joined by is its host's Steam ID in decimal.
- **Sessions are accepted** (`SteamNetworkingMessagesSessionRequest_t`) by a host from anyone, as its session decides who's let into the match, and by any other machine from the host of the match it joined and from members of its lobby, one of whom may host next. A request that comes before a machine hosts isn't asked again by Steam, which is why lobby members are taken early. A host closes the sessions of everyone it sent to with its transport.
- **`failed`** is true when Steam said a session failed before anything came from the host joined.
- **Host migration.** `place` is the lobby's ID, on a host that owns its lobby; `migrated` asks `GetLobbyOwner`: the host that went means not yet (`MOVING`), this player hosts, anyone else is joined. `tide_ended` naming the host that went means ended. `end` writes it, and the next `host` by an owner clears it. Not a member any more, or another lobby than the one named, is lost.
- **Steam starts at the first sign-in** (`IdentitySignIn` or `LobbySignIn`), or at `Steam.Start()`, and `MatchOpen` and `MatchJoin` register the network again before each `Session.Open` and `Session.Join`.
- **`#error` on wasm and Android**, at the top of `bridge.c`, with what to write.

## The fake

`test/fake_steam.c` defines every function `steam_flat.h` declares, for four users in one program (`FakeSteamUser`), and is linked in the SDK's library's place by the test only. It has: users with Steam IDs and names (one with quotes and a backslash in it); lobbies with type, joinable, limit, data, member data and an owner, which passes to the member there longest when the owner leaves; lists by string filters and free slots, kept per user; call results two of the asking user's frames later, through `SteamAPICallCompleted_t` and `GetAPICallResult` (which refuses another size or kind, as Steam does); `LobbyDataUpdate_t` and `LobbyChatUpdate_t` to every member; messages between users, held until the receiver accepted the sender's session, with `SteamNetworkingMessagesSessionRequest_t` for the first; `LobbyCreated_t` and `LobbyEnter_t` in the platform's size. `FakeSteamDeafen` cuts a user off (callbacks and messages), `FakeSteamForge` writes any bytes into a lobby's data as another program might, `FakeSteamData` reads a key.

It has no overlay, friends, relays, rate limits, or anything Valve's servers check.

## Tests

- `tide_steam_lobby` (`test/game.tide`, `test/steam_test.c`, with `packages/test/network_players.h`: each player a local state and a real session, served as the standard host serves one), native desktop only. One scenario, in made-up time: a quick match's ticket searching and ending in a lobby the first to ask made, with the request's mode as its data; two more finding it; the Steam ID and name as identity; a name with quotes arriving whole; member data, ready and the owner's rename reaching the others through callbacks; forged lobby data (bytes that aren't UTF-8, a `tide_match` that's no Steam ID) made harmless; the owner's match opened on Steam messages and named in the lobby by its Steam ID, the members joining; a fourth listing by mode, joining by ID and being in the match; a kick obeyed and kept; inputs both ways and identical worlds at the same tick, with no resync; **host migration**: the host leaves the lobby, ownership passes, the new owner's machine takes the match over, the third joins it at the new owner's Steam ID, neither sees `Connected` or `Disconnected`, the lobby names the match by its new host; **an ended match**: a player cut off from the goodbye goes to the lobby and is told `Ended`; Steam started once a user.
- `tide_steam_web_refused` (web trees): `bridge.c` compiled for wasm says what the package is for.
- A game that lists `lobby` alone builds for the web as before (`tide_lobby_menu`, `tide_lobby_own` and `tide_lobby_relay_web` in the web suite): the `#if` line leaves the package out, which `tide_cli_packages` checks for `#if` lines in general.
- Not run: Linux and macOS (CI's), and anything with the real SDK.
- Validation on Windows (2026-10-11), with the hook (`notes/own-network.md`): 2,234 of 2,234 native and 2,202 of 2,202 web debug tests, 37 of 37 relay tests, and the docs production build.

## Picks (Provisional, awaiting the owner)

- The package's name and folder (`steam`), its namespace (`Steam`), `Steam.Start()` and `Steam.InviteFriends()`.
- The flat API and SDK 1.60 as what `steam_flat.h` is written from, and declaring it in the package.
- A lobby as JSON between C and Tide.
- The `tide_` keys, `name` as the lobby's name, and member keys listed in `tide_keys`.
- A kick as a list the client obeys; that it also keeps the player out; a full list forgetting its oldest.
- `listed` false as `k_ELobbyTypePrivate`.
- Names being Steam's, with `SetName` doing nothing.
- A made lobby's data being the search's filter, and the quick match picking the fullest.
- Sessions accepted by a host from anyone, and otherwise from lobby members and the joined host only.
- The address as the account, with other kinds of Steam ID dropped.
- Migration: the lobby's ID as the place; waiting while the owner is the host that went; only an owner's match having a place; `tide_ended`.
- An invite followed by leaving the current lobby and joining.

## Not built

- A run against Steam.
- Launch parameters (`+connect_lobby`), rich presence, friends, lobby chat, Steam's own ping or timestamps (`Session.ping` counts a frame's wait there), relay settings, dedicated game servers, authentication tickets.
- An identity Tide's own service verifies with Steam.
- A package for another store: it would be written the same way.
