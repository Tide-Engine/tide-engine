# A game's own network

What's built, what code relies on, the tests, the gaps, and which decisions are the owner's. `AGENTS.md` has the rules every session needs and the index of these files. Tide's own way into a match, which this replaces for a game that says so, is in `notes/webrtc-and-rooms.md`; sessions are in `notes/netcode.md`; the lobby package, whose slots a game fills over it, is in `notes/lobby-package.md`.

## What it is

`engine/include/tide/network.h` and `engine/src/network.c` (built 2026-10-11): a public way for a game's own C, or a package's, to carry the game's matches, in the place of Tide's rooms, with no engine code for any particular network.

**The owner's decisions** (2026-10-11): of the Steam integration, "i dont really care how the steam integration is as long as any user could technically also do it themselves in your game without having to change the tide engine"; "and yes we should support host migration if steam makes sense"; "i guess we can touch the engine, but we should if we hit a wall only". The owner was shown a sketch of the provider (host, join, address, failed, update, optional migration) before it was built.

**The shapes are the agent's picks, Provisional** (listed under Picks), in `docs/spec.md`, Sessions.

## The surface

- **For a game's C:** `tide_network` (`self`, `host`, `join`, `address`, and optional `failed`, `update`, and `place`, `migrate`, `migrated` together), `tide_network_set`, `tide_network_get`, `TIDE_NETWORK_ADDRESS_BYTES` (64), `TIDE_NETWORK_PLACE_BYTES` (33), the five `TIDE_NETWORK_*` answers of `migrated`, and `TIDE_ADDRESS_NETWORK` in `tide/net.h`. The header's comments are the contract: whose memory, which thread, when each is called, what a transport's functions do, what an address is.
- **For hosts:** `tide_network_match` (the network a session's match is on, copied when the match was opened or joined) and `tide_network_match_host`, `_join`, `_leave`, `_update`, `_after` and `_address`.
- **`tide_host_game.network`** (`tide/host.h`): the game's `tide_network_get`, which `tide/run.h` fills.

## What code relies on

- **The registration lives in the engine's copy that the game's C links,** which under `tide run` is the game library's own (`notes/hot-reload.md`: a library has its own copy of the engine). So a host never reads it directly: it asks the game through `tide_host_game.network`. A table with no `network` (a host's own) has Tide's rooms.
- **A match stays on the network it started on.** `tide_network_match_host` and `_join` copy the `tide_network` into the session's `tide_network_match` and set `on`; address, failure, callbacks and migration go by that copy until `tide_network_match_leave`, or until `_after` finds the session offline. Registering another network, or none, changes the next match only.
- **The standard host** (`tide/host.h`): `tide_run_request` serves `TIDE_REQUEST_OPEN` and `TIDE_REQUEST_JOIN` from the registered network before the `--players` network and the rooms, and forgets the match's network at `START`, `JOIN`, `CONNECT`, `LEAVE` and a server's `END`; `tide_run_migrate` hands a match that's on one to `tide_network_match_after` and returns; `tide_run_frame` calls `tide_network_match_update` before the session's update and takes `Session.room` from `tide_network_match_address`.
- **With a network registered, `Session.Open` opens no room and no UDP port:** the match is on the game's network alone. `Session.Connect` is as it was.
- **`--players` runs go through it too** once the game registers one.
- **What the engine bounds:** `Session.room` is 63 bytes of printable ASCII, cut at the first byte that isn't (`tide_network_match_address`), into a `tide_room` that's 64 bytes now (it was 8), so `tide_local` is 56 bytes bigger; a place is 1 to 32 letters and digits or it's none (`place_of`), and a place that comes from the server is checked by the session as a room's key always was; a transport without `send` or `receive` is closed and counts as a failure; `server`'s kind is set by the engine.
- **A place travels as the room's key.** A session's handover record has a room's code (6 of the room letters) and its key (32 letters and digits). A game's network has one text, which goes as the key under a fixed code that's no relay's concern (`PLACE_CODE`, `222222`). The wire format, `tide_session_set_room` and `tide_session_migrating` are unchanged.
- **Migration is the rooms' steps with the network's answers:** on a server, `place` each frame into `tide_session_set_room`; on a session that's migrating, `migrate` once, then `migrated` each frame: `HOSTS` is `tide_session_take_over`, `JOINS` is `tide_session_join`, `ENDED` fails with `Ended`, anything else but `MOVING`, or 30 seconds, with `TimedOut` (the same 30 seconds rooms have). A session that's migrating on a network with no `migrate` fails with `ServerLeft`.
- **`failed`** is asked once a frame while the match is on the network and not migrating; true fails the session with `Failed` and ends the asking.
- **`update`** runs for the registered network every frame, and for a match's network when it's another.
- **A host joined on `TIDE_ADDRESS_NETWORK` gets `ROOM_TIMEOUT` (15 s) to first answer,** as a room's host does (`tide_client_update`).
- **Hot reload:** `swap` in `platform/src/reload.c` starts the game over when `tide_run_network.on`, before the old library unloads: the session's transports are functions of the library that made them. `tide_run_end` closes them while that library is still loaded.
- **Hostile input:** nothing new is trusted. Datagrams reach the session as from any transport. The only text of another machine's that the engine hands a network is the place, which the session checked.

## Engine changes, and the wall each removed

The hook itself is the public surface the requirement asks for. Beside it:

- `TIDE_ADDRESS_NETWORK` (`tide/net.h`, formatted by `tide_address_format`): a network's addresses had no kind that isn't an IPv4 address or a room's player, which hosts route by.
- `ROOM_TIMEOUT` for it (`engine/src/session.c`): a network that sets up a connection first (Steam's) needs more than the 5 seconds a UDP server gets.
- `tide_room` from 8 to 64 bytes (`compiler/src/codegen.c`) and `tide/host.h`'s `room` buffer: an address is longer than a room's code.
- The literal check at `Session.Join` (`compiler/src/check.c`): text that isn't a room's code is no longer provably wrong.
- `tide_host_game.network`, and the start-over in `reload.c`: the library's own engine copy, and its transports' code.

Nothing in `platform/src/rooms.c`, the relay or the session's wire format changed.

## `Session.Join`'s literal

Before, a string literal that wasn't a room's code was an error. Now: `Session.Join("")` is an error ("Session.Join takes what the match is joined by, and \"\" is nothing"); a literal that reads as a machine's address (four numbers with dots and maybe a port, or `localhost` with maybe one: `address_like`) is a warning with a note that points to `Session.Connect` and says text a network takes goes in a constant or a variable; any other text says nothing. Fixtures: `compiler/tests/errors/session_join_nothing.tide`, `compiler/tests/warnings/session_join_address.tide`; `session_join_not_a_code.tide` is gone. The language server has no text of its own for this.

## Tests

- `tide_network_udp` (`examples/network/`: `game.tide`, `udp.c`, `test/drive.tide`, `test/udp_test.c`), native: the docs' walkthrough as it's written, two players in one program over the system's UDP on 127.0.0.1, each run as a host runs one (`packages/test/network_players.h`): nothing registered until the game's own code asks; `Session.Open` from Tide, `Session.room` an address; `Session.Join(text)` with it; inputs both ways; identical verified worlds at the same tick with no resync; traffic counted as a network's; `Session.End` heard as `Ended`; text that's no address failing at once. In real time, a millisecond a frame, with steps that wait on state, never on a time. On the web the same target checks that the game builds, registers nothing and says so.
- `tide_network_hosted` (`examples/network/test/hosted.tide`, `hosted.c`), native, labelled `gpu` as every run with no window: the same game as its own program under the standard host, `--frames 20`. It hosts at its first frame, and its C checks at the tenth that `tide/host.h` opened the match on the network the game registered and that `Session.room` is that network's address. One player: the host's own lines around the `tide_network_match_*` calls, which the tests with several players in one program can't run.
- `tide_lobby_own` (`packages/lobby/test/own.tide`, `own.c`, `own_test.c`), native and web: all four of the lobby package's slots overridden over the game's own C, an in-memory service and a network whose datagrams go between queues in the program, with host migration by the lobby's owner. Four players: quick match into one lobby; member data seen; the owner's match opened on the game's network and named by the lobby; a fourth through a list and a join by ID; a kick; inputs and identical worlds; the host leaving the lobby, the next owner taking the match over and the third joining it there, with no `Connected` or `Disconnected`, and the lobby naming the match by its new host; a match its host ended told to a player who missed the goodbye (`migrated` answering `ENDED`); `failed`; `update` called. In made-up time, so it repeats exactly.
- `packages/test/network_players.h` is those tests' host: per player a `tide_local`, a `tide_session` and a `tide_network_match`, served with the same `tide_network_match_*` calls as `tide/host.h`. It doesn't run `tide/host.h` itself, whose statics are one player's a program.
- **Not tested:** a join or a host migration through `tide/host.h` itself (the standard host is one player a program, and two programs on the system's UDP in made-up time would rest on how fast datagrams arrive); the start-over at a reload; a transport with `clock` and `arrived`.

## Picks (Provisional, awaiting the owner)

- Every name and the struct's shape; `self` handed back; `address` and `place` as text the network writes into a buffer.
- One network registered for the program, copied; a match keeping its own.
- No room and no UDP port beside a registered network on `Session.Open`; `--players` going through it.
- 63 bytes of printable ASCII for an address; 32 letters and digits for a place, carried as the room's key.
- `TIDE_ADDRESS_NETWORK`, and 15 seconds for a first answer on it.
- `failed` as a question asked each frame; `update` running outside a match.
- A reload starting the game over while a match is on the game's network.
- The warning for an address-like literal at `Session.Join`, and nothing for other text.

## Not built

- A match kept across a reload on a game's own network.
- A UDP port or a room beside the game's network.
- Several networks at once, or one per match chosen by the address.
- A ping without the frame's wait for a transport with no clock of its own.
- Anything for visibility or hosted matches on a game's network (neither exists yet for any).
