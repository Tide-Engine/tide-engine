---
description: "Bring your own match transport and your own lobby service to a Tide game, from the game's own Tide and C files, with no change to the engine."
---

# Your own network and service

A match's packets go through Tide's [rooms](./matches.md#rooms) unless the game says otherwise, and its lobbies are Tide's [lobby service](./lobbies.md) unless it says otherwise. Both are the game's to replace, from its own files or a package it lists, with nothing of the engine changed: a store's network and lobbies, a server fleet's, a LAN's.

There are two parts, and a game takes either alone:

- **A network of its own** carries the match's datagrams. The game's C registers it, and `Session.Open()`, `Session.Join(text)` and `Session.room` go through it from then on.
- **A service of its own** answers the lobby package's questions: who the player is, which lobbies there are, who's matched with whom. The game overrides the package's [slots](./lobbies.md#another-service).

The [Steam package](./steam.md) is built from exactly these two, and nothing else.

## A network of your own

A network is a handful of C functions in a `tide_network` (`tide/network.h`), which a game's C registers with `tide_network_set`. The reference for each function, and for the transports it makes, is [A game's own network](../c/network.md). This walkthrough is the simplest one there is: plain UDP sockets, where the text a match is joined by is its host's address.

The whole of it is `examples/network/` in Tide's repository, two files, which Tide's own tests build and play a match through.

### The Tide side

Nothing changes but one call. The game declares the C function that registers its network, and calls it before the `Session.Open()` or `Session.Join()` it's for:

```csharp
// udp.c: registers the game's network. False where there's no UDP, as in a
// page, and the game's matches are in Tide's rooms there.
extern bool UseUdpNetwork();

void HostMatch()
{
    _ = UseUdpNetwork(); // Before the Open it's for: the same frame will do
    Session.Start(Arena);
    Session.Open();
}

void JoinMatch(string address)
{
    _ = UseUdpNetwork();
    Session.Join(address);
}
```

`Session.room` is then what the network says the match is joined by, here `127.0.0.1:50123`, where with rooms it's a code like `K7QF2M`. The menu shows it, and another player types it:

```csharp
view Lobby(Session session, mut Menu menu)
{
    GUILayout.Area(Rect(12, 12, Screen.width - 24, 160))
    {
        if (session.state == SessionState.Offline)
        {
            if (GUILayout.Button("Host")) HostMatch();
            GUILayout.TextField("Address", menu.address, out var entered);
            if (GUILayout.Button("Join") || entered) JoinMatch(menu.address);
        }
        else if (session.room != "")
        {
            GUILayout.Label($"Join at {session.room}");
        }
    }
}
```

Everything else is a match like any other: the same server, prediction and rollback, the same `Connected` and `Disconnected`, the same world on every machine.

### The C side

A transport is how a session sends and receives datagrams (`tide_transport`, in `tide/net.h`). This one's is a socket. An address is the network's own to give a meaning: here an IPv4 address and a port, which fit a `tide_address` as they are.

```c
#include "tide/network.h"

typedef struct udp_end {
    udp_socket socket;
} udp_end;

static void udp_send(void *self, const tide_address to, const void *data, const uint32_t size)
{
    const udp_end *end = self;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(to.port);
    a.sin_addr.s_addr = htonl(to.host);
    sendto(end->socket, (const char *)data, (int)size, 0, (const struct sockaddr *)&a, sizeof a);
}

// The next datagram that's there, never waiting for one.
static uint32_t udp_receive(void *self, tide_address *from, void *data, const uint32_t capacity)
{
    const udp_end *end = self;
    char whole[TIDE_NET_MTU + 1u];
    for (;;) {
        struct sockaddr_in a;
        udp_length length = sizeof a;
        const int n = (int)recvfrom(end->socket, whole, sizeof whole, 0, (struct sockaddr *)&a, &length);
        if (n < 0) return 0; // Nothing there (the example also steps over Windows' errors for a datagram that found nobody)
        if (n == 0 || (uint32_t)n > capacity || (uint32_t)n > TIDE_NET_MTU) continue; // Too long: dropped
        memcpy(data, whole, (size_t)n);
        *from = (tide_address){TIDE_ADDRESS_NETWORK, ntohl(a.sin_addr.s_addr), ntohs(a.sin_port)};
        return (uint32_t)n;
    }
}

static void udp_close(void *self)
{
    udp_end *end = self;
    close_socket(end->socket);
    free(end);
}
```

`send` returns at once, `receive` never waits, and neither resends or orders anything: sessions make up for datagrams that are lost, doubled or late. What arrives is another machine's bytes, which the session takes as hostile; the network hands them over as they came.

The network itself is three functions here. `host` is `Session.Open()`: a socket on a port, whose address is the text players join by. `join` is `Session.Join(text)`: a socket of this machine's own, and the host's address read from text a player may have typed. `address` is `Session.room`.

```c
static char udp_address[TIDE_NETWORK_ADDRESS_BYTES];

static bool udp_host(void *self, tide_transport *out)
{
    (void)self;
    uint16_t port = 0;
    if (!udp_open(0, out, &port)) return false; // A socket that never waits, as a transport
    snprintf(udp_address, sizeof udp_address, "127.0.0.1:%u", (unsigned)port);
    return true;
}

static bool udp_join(void *self, const char *address, tide_transport *out, tide_address *server)
{
    (void)self;
    tide_address host;
    uint16_t port = 0;
    if (!tide_address_parse(address, 0, &host) || !host.port) return false; // Text that's no address joins nothing
    if (!udp_open(0, out, &port)) return false;
    *server = (tide_address){TIDE_ADDRESS_NETWORK, host.host, host.port};
    /* ...and udp_address is the address joined */
    return true;
}

static void udp_room(void *self, char *out, const size_t size)
{
    (void)self;
    snprintf(out, size, "%s", udp_address);
}

bool UseUdpNetwork(void)
{
    tide_network_set(&(tide_network){.host = udp_host, .join = udp_join, .address = udp_room});
    return true;
}
```

That's a whole network. The example binds `127.0.0.1`, so it opens nothing beyond the machine it runs on; a game for a LAN binds every address (`INADDR_ANY`) and shows the machine's own.

### Where it doesn't exist

A game's C builds for every platform the game does, and a page has no UDP. The same file says so, and registers nothing there, so the game's matches are in Tide's rooms on the web:

```c
#if defined(__wasm__)
bool UseUdpNetwork(void)
{
    return false;
}
#else
/* ...the sockets */
#endif
```

A network that exists everywhere needs none of this. Tide code has no platform symbols, as every machine of a match runs the same code: the C function is what differs.

### What a network decides

- **What a match is joined by.** Text of up to 63 printable ASCII characters, which `Session.room` shows and `Session.Join` takes: an address, a Steam ID, a lobby's name. A network reads whatever a player typed, and refuses what isn't its own.
- **Whether a join can fail early.** `join` returning false fails at once (`Disconnected` with `Failed`). A network that finds out later that nobody is there says so through `failed`; one that can't tell lets the session give up after 15 seconds without a first answer.
- **Callbacks.** `update` runs once a frame, in a match or not, for a library that wants its callbacks run.
- **Host migration**, or none. Three more functions, for a game with `hostMigration = true`: where a match's players meet when its host goes, and who hosts next. Without them a match ends with its host, as one joined with `Session.Connect` does. The network has to have an authority for it, like a lobby's owner, never a race. See [host migration](../c/network.md#host-migration).

### When it's registered

Before the `Session.Open()` or `Session.Join()` it's for; the same frame is fine. A match stays on the network it was opened or joined on until this machine leaves it, whatever is registered meanwhile, and `tide_network_set(NULL)` gives the next match Tide's rooms again.

With a network registered, `Session.Open()` takes players on it alone: no room is opened, and no UDP port. `Session.Connect(address)` still joins a machine by its address over Tide's own UDP.

Under `tide run`, a game's C starts over at each [reload](../tools/cli.md), so the game registers again by calling its function again, as the example does at each `HostMatch`. A reload while a match is on the game's own network starts the game over, as Ctrl+R does: the match's transports are the code of the build that made them.

A run of several players with no window (`--players`) is on the game's network too once the game registers one, so a network that needs a real service isn't made up for such a run.

## A service of your own

The [lobby package](./lobbies.md) asks four things of a service, each a set of `virtual` functions: who the player is, lobbies, matchmaking, and how a lobby's match is reached. A game, or a package it lists, [overrides](../language/overrides.md) the ones it has its own answer for. Here every one of them is the game's own, over C that keeps its lobbies however it likes:

```csharp
using Lobbies;

// The game's own C
extern string OwnWho();
extern string OwnJoin(string id, string member);
extern string OwnLobby(string id);
extern int OwnVersion(string id);
extern bool UseOwnNetwork();

// Identity: who the game's own service says plays
override async LobbyIdentity IdentitySignIn(LobbyClient client, bool fresh)
{
    return LobbyIdentity { id = OwnWho() };
}

// Lobbies: every function of the slot, of which these are three
override async LobbyIdentity LobbySignIn(LobbyClient client, LobbyIdentity who)
{
    return who; // These lobbies know a player by its identity alone
}

override async LobbyAnswer LobbyJoin(LobbyClient client, string id)
{
    return Said(OwnJoin(id, client.name));
}

override async LobbyAnswer LobbyWatch(LobbyClient client, string id, int version)
{
    await Wait.Frames(1);
    while (OwnVersion(id) == version) await Wait.Frames(1); // Asked, a frame at a time: nothing is timed
    return Said(OwnLobby(id));
}

// Transport: a lobby's match is on the game's own network
override void MatchOpen()
{
    _ = UseOwnNetwork();
    Session.Open();
}

override void MatchJoin(string address)
{
    _ = UseOwnNetwork();
    Session.Join(address);
}
```

The package's menu, `LobbyClient` and every call a game makes (`QuickMatch`, `StartMatch`, `SetReady`) stay as they are, and go through these.

Two things make the C side short:

- **A lobby crosses as JSON.** C can't make a Tide struct with text and lists in it, so the game's C writes a lobby the way Tide's own service does, and the override reads it with [`Json.Read<LobbyInfo>`](../language/json.md). What other players wrote (names, data) goes through the same reader as any JSON from a network, so nothing C hands over is trusted to be text.

  ```csharp
  LobbyAnswer Said(string lobby)
  {
      if (lobby == "") return LobbyAnswer { failure = Why() }; // The game's own: why its C gave nothing
      var read = Json.Read<LobbyInfo>(lobby) ?? LobbyInfo { };
      if (read.id == "") return LobbyAnswer { failure = LobbyFailure.Failed };
      return LobbyAnswer { ok = true, lobby = read };
  }
  ```

- **A service that answers later is waited for a frame at a time.** The slot functions are [`async`](../game/tasks.md): the C starts a request and gives a number, and the override asks whether it's done each frame (`await Wait.Frames(1)`) before it reads the answer.

An override has the parameters of what it replaces, so what a service keeps between calls is in its C: one program is one player.

### A match that follows its lobby

With a lobby there's an authority for host migration: the lobby's owner. The game's network says the lobby is where a match's players meet (`place`), and a player whose match lost its host asks the lobby who owns it now (`migrated`): the owner takes the match over, and everyone else joins the owner. While the lobby still names the host that went, nothing is known, and the players wait. A match its host ended for everyone is marked so in the lobby (the transport's `end`), and nobody takes it over.

The lobby package follows along: when a lobby's match changes hands to its owner under another address, the owner's machine names the match again and the members' machines, already there, take the new name without joining twice.

Tide's tests play all of this with a game whose service and network are its own C (`packages/lobby/test/own.tide` and `own.c` in the repository): four players, from a quick match's lobby into a match on the game's network, which changes hands when its host leaves the lobby.

## Limits for now

- **One network at a time is registered**, for the whole program. A match keeps the one it started on.
- **A reload under `tide run` starts the game over** while a match is on the game's own network.
- **`Session.ping` on a network without a clock of its own** (`clock` and `arrived`) counts the wait for a frame.
- **Where a match's players meet again is up to 32 letters and digits**, which is what a session sends its players for it.
- **The engine's visibility and hosted-match plans don't know about a game's own network yet**: every player still gets the whole world, on any network.
