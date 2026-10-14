---
description: "The C interface for a Tide game's own match network: tide_network, the transports it makes, addresses, host migration and what a custom host calls."
---

# A game's own network

`tide/network.h` is how a game's C carries its matches over a network of its own, in the place of Tide's [rooms](../multiplayer/matches.md#rooms): a store's, a server fleet's, plain sockets. [Your own network and service](../multiplayer/own-network.md) walks through one; this page is the reference.

A game's C includes `tide/network.h` (and with it `tide/net.h`) on every target, and nothing else of the engine. Nothing of the engine is built for a particular network.

```c
#include "tide/network.h"

bool UseMyNetwork(void) // extern bool UseMyNetwork(); in the game's Tide
{
    tide_network_set(&(tide_network){.host = my_host, .join = my_join, .address = my_address});
    return true;
}
```

## Registering

| Function | Does |
|---|---|
| `tide_network_set(const tide_network *network)` | Registers a copy of `network` as the game's network from now on. `NULL` registers none: Tide's rooms again |
| `tide_network_get()` | What's registered, or `NULL` |

- **When.** Before the `Session.Open()` or `Session.Join()` it's for. The same frame will do, as a frame's session calls are served at its end.
- **A match stays on its network** until this machine leaves it, whatever is registered meanwhile. The next match opened or joined is on what's registered then.
- **`self`** is the network's own pointer, handed back to every function. What it points at stays good while the network is registered and while a match is on it.
- **Threads.** Every function of a network and of its transports is called on the thread that runs the game's frames, never during a tick's systems. A network with threads of its own hands their datagrams over itself.
- **Under `tide run`** a game's C starts over at each reload, so the game registers again. A reload while a match is on the game's own network starts the game over.
- **On the web** the functions are there as everywhere. A network that doesn't exist in a page registers nothing there.

With a network registered, `Session.Open()` opens the match on it alone, with no room and no UDP port, and `Session.Join(text)` hands it the text. `Session.Connect(address)` is still Tide's own UDP.

## The network's functions

```c
typedef struct tide_network {
    void *self;
    bool (*host)(void *self, tide_transport *out);
    bool (*join)(void *self, const char *address, tide_transport *out, tide_address *server);
    void (*address)(void *self, char *out, size_t size);
    bool (*failed)(void *self);  // Optional
    void (*update)(void *self);  // Optional
    // Host migration: all three, or none
    void (*place)(void *self, char *out, size_t size);
    void (*migrate)(void *self, const char *place);
    int (*migrated)(void *self, tide_transport *out, tide_address *server);
} tide_network;
```

| Function | Called | Contract |
|---|---|---|
| `host` | `Session.Open()` on a match this machine runs | Writes a transport the players who join arrive on into `out` and returns true. False: the match stays closed, and `out` is untouched. It keeps hosting until the transport's `close` |
| `join` | `Session.Join(text)` | `address` is the game's text, zero-terminated, at most 255 bytes, good for the call only, and anything a player could type. Writes a transport to that match into `out` and the host's address on it into `server`, and returns true. False for text that can't be an address: the join fails at once with `Failed`. True promises nothing about the host being there |
| `address` | Every frame a match is on the network | Writes the text others join this machine's match by into `out` (`size` is `TIDE_NETWORK_ADDRESS_BYTES`, 64): of the match it hosts, or of the one it joined. `""` while there's none to give. It's what `Session.room` says. The engine cuts it at 63 bytes and at the first byte that isn't printable ASCII |
| `failed` | Every frame a match is on the network | True when the match can't go on: the address joined is nobody's, or the network is gone. The match ends on this machine with `Failed`, and it isn't asked again for that match |
| `update` | Once a frame, before the session reads its transports, in a match or not | Where a library's callbacks are run. Called for as long as the network is registered, and for a match's network until the match is left |

A join whose host never answers, with `failed` saying nothing, ends after 15 seconds with `TimedOut`, as a room's does.

## Transports

`host`, `join` and `migrated` each hand over a `tide_transport` (`tide/net.h`), which a session then calls:

```c
typedef struct tide_transport {
    void *self;
    void (*send)(void *self, tide_address to, const void *data, uint32_t size);
    uint32_t (*receive)(void *self, tide_address *from, void *data, uint32_t capacity);
    void (*close)(void *self);
    void (*end)(void *self);       // Optional
    double (*clock)(void *self);   // Optional, with arrived
    double (*arrived)(void *self);
} tide_transport;
```

| Function | Contract |
|---|---|
| `send` | One datagram of up to `TIDE_NET_MTU` (1200) bytes to `to`, an address this transport gave. Returns at once. `data` is the caller's, gone after the call. A datagram that can't go is dropped |
| `receive` | Copies the next datagram that arrived into `data`, writes who sent it into `from`, and returns its size; 0 with none. Never waits. One longer than `capacity` is dropped. Called until it returns 0, every frame |
| `close` | The session is done with the transport and calls nothing of it again: free what `self` holds. Called exactly once for every transport handed over |
| `end` | On a host, just before `close`, when the match ended for everyone (`Session.End()`, or its last scene unloading) and not when its host only left. A network with host migration records it where `migrated` reads it |
| `clock`, `arrived` | The network's clock in seconds, and when the datagram `receive` last returned arrived by it. With them `Session.ping` is the network's round trip alone; without, it counts from when datagrams are read |

A transport carries datagrams and nothing more. They can be lost, doubled or arrive out of order, and sessions make up for all of it: nothing is resent, ordered or acknowledged by the network.

**What arrives is another machine's bytes.** A session checks every datagram as hostile, whatever carried it, and a network adds no trust: it hands over what came, with who it came from as it knows them.

## Addresses

A transport names machines with `tide_address` values of kind `TIDE_ADDRESS_NETWORK`, whose `host` (32 bits) and `port` (16 bits) are the network's to give a meaning: an IPv4 address and a port, an account number, an index into a table of the network's own peers. A session only compares them and hands them back to `send`. What it relies on:

- One machine has one address for as long as a transport lives, and two machines never share one: a player is the address its datagrams come from.
- `server`, from `join` and `migrated`, is the address the host's datagrams arrive from on that transport.
- A machine that joins the same host again should get the same `server`, and another host another: that's how it comes back as the same player.

The engine sets `server`'s kind itself. `tide_address_format` writes one as `network 3:0`.

## Host migration

Optional, for a game whose settings say `hostMigration = true`. With all three functions, a match whose host goes changes hands; with none, it ends, as a match joined with `Session.Connect` does.

One authority decides who hosts next, and it's the network's to have: a relay, a lobby's owner, a matchmaker. Nothing is decided by who was first or who waited longest.

| Function | Called | Contract |
|---|---|---|
| `place` | On the host, every frame | Writes where the match's players meet when it goes into `out` (`size` is `TIDE_NETWORK_PLACE_BYTES`, 33): up to 32 letters and digits, which the session sends every player. `""`, or anything else, is nowhere: no migration |
| `migrate` | On a player whose match lost its host, once | Go to `place`, the text the host's `place` gave, and find out who hosts now. The engine has checked it's letters and digits |
| `migrated` | Every frame after that, until it's known | Returns one of the values below |

| `migrated` returns | Means |
|---|---|
| `TIDE_NETWORK_MOVING` (0) | Not known yet: asked again next frame |
| `TIDE_NETWORK_HOSTS` (1) | This machine hosts the match now. `out` is a transport the players who come back arrive on |
| `TIDE_NETWORK_JOINS` (2) | Another machine does. `out` reaches it, at `server` |
| `TIDE_NETWORK_LOST` (-1) | The place can't be reached: the match is over for this machine (`TimedOut`) |
| `TIDE_NETWORK_ENDED` (-2) | The match's host ended it for everyone (its transport's `end`): nobody takes it over (`Ended`) |

Exactly one machine may be told it hosts. A new host has 30 seconds to be found, and then the match is over for this machine. While a match changes hands, local code sees `Session.state` as `Connecting` and no `Connected` or `Disconnected`, as with [rooms](../multiplayer/matches.md#host-migration).

## For hosts

The standard host does everything above. A C program with a loop of its own ([C hosts](./hosts.md)) keeps a zeroed `tide_network_match` for each session and calls:

| Function | When |
|---|---|
| `tide_network_match_host(&m, network, &transport)` | In `Session.Open()`'s place, with `tide_network_get()`'s network when there is one: the transport for `tide_session_open` |
| `tide_network_match_join(&m, network, text, &transport, &server)` | In `Session.Join()`'s place: the transport and server for `tide_session_join` |
| `tide_network_match_update(&m, tide_network_get())` | Once a frame, before `tide_session_update` |
| `tide_network_match_after(&m, session, now)` | Once a frame, after it: fails a match whose network says so, and does a host's and a player's part of host migration |
| `tide_network_match_address(&m, room)` | The text for `tide_local_set_session`'s `room`, into `char room[TIDE_NETWORK_ADDRESS_BYTES]` |
| `tide_network_match_leave(&m)` | When the match is left, ended or replaced by one on no network of the game's |

A game's library has its own copy of the engine under `tide run`, so the standard host asks the game for its network through `tide_host_game.network`, which `tide/run.h` fills with `tide_network_get`.
