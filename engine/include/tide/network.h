#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tide/net.h"

// A game's own network: how its matches are opened to other machines and
// joined, in the place of Tide's rooms. A game's C (or a package's) fills a
// tide_network and registers it; from then on `Session.Open()` opens a match
// on it, `Session.Join(text)` joins one through it, and `Session.room` is the
// text it says others join by. Nothing of the engine changes for it, and
// nothing else of a match does: sessions (tide/session.h) send the same
// datagrams over whatever carries them.
//
// This header and tide/net.h are all a game's C includes for it, on every
// target. A network that only exists on some (one over a desktop library)
// registers nothing on the others, where the match stays in Tide's rooms.
//
// Threads: every function here, and every function of a tide_network and of
// the transports it makes, is called on the thread that runs the game's
// frames, never during a tick's systems. A network that has threads of its
// own hands their datagrams over itself.
//
// What arrives is other machines' bytes. Sessions take every datagram as
// hostile, whatever carried it, and a network adds no trust: it hands over
// what came, with who it came from as it knows them.

// The text a match is joined by, with its NUL: up to 63 bytes, each a
// printable ASCII character (0x20 to 0x7E). What `address` writes is cut
// there by the engine, at the first byte that's neither.
#define TIDE_NETWORK_ADDRESS_BYTES 64u
// Where a match's players meet again when its host goes, with its NUL: up to
// 32 letters and digits (the session sends it to every player, see
// tide_session_set_room). Anything else `place` writes counts as none.
#define TIDE_NETWORK_PLACE_BYTES (TIDE_ROOM_KEY_LENGTH + 1u)

// What a host gets back from `migrated`.
#define TIDE_NETWORK_MOVING 0   // Not known yet: ask again next frame
#define TIDE_NETWORK_HOSTS 1    // This machine hosts the match now: `out` takes the players who come back
#define TIDE_NETWORK_JOINS 2    // Another machine does: `out` reaches it, at `server`
#define TIDE_NETWORK_LOST (-1)  // The place can't be reached: the match is over for this machine
#define TIDE_NETWORK_ENDED (-2) // The match's host ended it for everyone (tide_transport.end): nobody takes it over

// Addresses. A transport names the machines it reaches with tide_address
// values of kind TIDE_ADDRESS_NETWORK, whose `host` and `port` are the
// network's own to give a meaning (an index into a table of its peers, say:
// 48 bits don't hold every network's names). Sessions only compare them and
// hand them back to `send`. What they rely on:
//   - One machine has one address for as long as a transport lives, and two
//     machines never share one: a player is the address its datagrams come from.
//   - `server`, from `join` and `migrated`, is the address the host's
//     datagrams arrive from on that transport. A machine that joins the same
//     host again should get the same `server`, and another host another:
//     that's how a session knows to come back as the same player.
//
// Transports (tide_transport, tide/net.h). `self` is the network's own, and
// the session calls:
//   - `send(self, to, data, size)`: one datagram of up to TIDE_NET_MTU bytes
//     to `to`, which is an address this transport gave. It returns at once;
//     `data` is the caller's and is gone after the call. A datagram that
//     can't go is dropped: sessions make up for loss, duplicates and
//     reordering, so nothing here is resent or ordered.
//   - `receive(self, &from, data, capacity)`: copies the next datagram that
//     arrived into `data` and returns its size, with who sent it in `from`,
//     or returns 0 with none. Never waits. One longer than `capacity` is
//     dropped. Called until it returns 0, every frame.
//   - `close(self)`: the session is done with the transport, and calls
//     nothing of it again: free what `self` holds. Always called, exactly
//     once, for every transport handed over, also one a session refused.
//   - `end(self)`, optional (NULL): on a host, just before `close`, when the
//     match ended for everyone (`Session.End()`, or its last scene
//     unloading), as opposed to its host leaving. A network with host
//     migration records it where `migrated` reads it, so that no player takes
//     the match over.
//   - `clock(self)` and `arrived(self)`, optional (both NULL): the network's
//     own clock in seconds, and when the datagram `receive` last returned
//     arrived by it. With them `Session.ping` is the network's round trip
//     alone; without, it counts from when datagrams are read, a frame's wait
//     included.
typedef struct tide_network {
    void *self; // Handed back to every function below: the network's own

    // `Session.Open()`: a transport the players who join arrive on, into
    // `out`. False when the match can't be opened: it stays closed, and `out`
    // is left alone. The network keeps hosting until the transport's `close`.
    bool (*host)(void *self, tide_transport *out);

    // `Session.Join(text)`: a transport to the match `address` names, into
    // `out`, and the host's address on it into `server`. `address` is what
    // the game passed, NUL-terminated and at most 255 bytes, good for the
    // call only: text a player may have typed, so anything at all. False for
    // text that can't be an address, which fails the join at once
    // (`Disconnected` with `Failed`). True doesn't promise the host is there:
    // a join that turns out to have nobody at its end says so through
    // `failed`, or just never answers, and the session gives up after 15
    // seconds without a first answer.
    bool (*join)(void *self, const char *address, tide_transport *out, tide_address *server);

    // The text others join this machine's match by, NUL-terminated, into
    // `out` (`size` is TIDE_NETWORK_ADDRESS_BYTES): of the match it hosts,
    // or of the one it joined. "" while there's none to give, as before the
    // network knows it. Asked every frame a match is on this network, and
    // `Session.room` is what it says.
    void (*address)(void *self, char *out, size_t size);

    // Whether the match this machine opened or joined on this network can't
    // go on: the address joined turned out to be nobody's, or the network
    // itself is gone. Asked every frame while a match is on it; true ends the
    // match on this machine with `Failed`, and it isn't asked again for that
    // match. Optional (NULL: never).
    bool (*failed)(void *self);

    // Once a frame, before the session reads its transports, in a match or
    // not, for as long as the network is registered: where a library's
    // callbacks are run. Optional.
    void (*update)(void *self);

    // Host migration, for a game whose settings say `hostMigration = true`.
    // All three, or none: without them a match ends when its host goes, as
    // one joined by address does. One authority decides who the new host is,
    // and it's the network's to have: a relay, a lobby's owner. Nothing here
    // is decided by who waited how long.
    //
    // On the host, every frame: where its players meet when it goes, into
    // `out` (`size` is TIDE_NETWORK_PLACE_BYTES), or "" for nowhere, which
    // is no migration. The session tells every player.
    void (*place)(void *self, char *out, size_t size);
    // On a player whose match lost its host, once: go to `place`, the text
    // that host's `place` gave (letters and digits, checked by the engine),
    // and find out who hosts now.
    void (*migrate)(void *self, const char *place);
    // ...and then every frame until it's known: one of the values above,
    // with a new transport in `out` for TIDE_NETWORK_HOSTS and
    // TIDE_NETWORK_JOINS, and the new host's address in `server` for
    // TIDE_NETWORK_JOINS. Exactly one machine may be told it hosts. A host
    // has 30 seconds to be found; then the match is over for this machine.
    int (*migrated)(void *self, tide_transport *out, tide_address *server);
} tide_network;

// Registers `network`, copied, as the game's network from now on, or with
// NULL, none: Tide's rooms again. What `self` points at stays the game's, and
// has to stay good while it's registered and while a match is on it.
//
// When: before the `Session.Open()` or `Session.Join()` it's for; the same
// frame will do, as a frame's session calls are served at its end. A match
// that's on a network stays on it until this machine leaves it, whatever is
// registered meanwhile: the next match opened or joined is on the new one.
// Under `tide run`, a game's C starts over at each reload, so it registers
// again; a reload while a match is on the game's own network starts the game
// over, as the transports' code is the build's that made them.
void tide_network_set(const tide_network *network);
// What's registered, or NULL.
const tide_network *tide_network_get(void);

// ---------------------------------------------------------------------------
// For hosts: programs with a loop of their own (tide/host.h is the standard
// one, and does all of this). A host keeps a tide_network_match for each
// session, zeroed, and asks the game for its network where it would open or
// join a room.

struct tide_session;

typedef struct tide_network_match {
    tide_network network; // The network the session's match is on, as it was registered then
    bool on;              // ...while it's on one
    double since;         // Host migration: when this machine went to the place, or 0
} tide_network_match;

// `Session.Open()` and `Session.Join(address)` on `network` (not NULL): the
// transport, and the server's address, for tide_session_open and
// tide_session_join. False if the network couldn't.
bool tide_network_match_host(tide_network_match *m, const tide_network *network, tide_transport *out);
bool tide_network_match_join(tide_network_match *m, const tide_network *network, const char *address,
                             tide_transport *out, tide_address *server);
// The match was left, ended or replaced by one that's on no network of the game's.
void tide_network_match_leave(tide_network_match *m);
// Once a frame, before the session's update: `registered` is the game's
// network now (tide_network_get), or NULL.
void tide_network_match_update(const tide_network_match *m, const tide_network *registered);
// ...and after it: fails the session's match if its network says so, tells a
// match this machine hosts where its players meet again, and moves one that
// lost its host (see tide_network.migrated). Nothing while the match is on no
// network of the game's.
void tide_network_match_after(tide_network_match *m, struct tide_session *session, double now);
// `Session.room`: what the match's network says it's joined by, cut to what
// an address is. "" while the match is on none.
void tide_network_match_address(const tide_network_match *m, char out[TIDE_NETWORK_ADDRESS_BYTES]);
