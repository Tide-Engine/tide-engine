#pragma once

// A run of several players with no window (tide_headless_desc.players, which
// is tide/run.h's --players): each player is a process of its own, as on
// another machine, and the run's network is made up as its time and its
// devices are. Nothing of this machine's is in it: no port, no relay, no clock.
//
// The first player's process starts the others, as copies of itself, and is
// the one they all talk to. They go a frame at a time, together: once a frame
// drew, each says what it sent in it, and has what the others sent, which its
// next frame reads. So a datagram takes a frame each way, the same in every
// run, in the same order (by player, then as it was sent): a run of several
// repeats as a run of one does.
//
// On that network a player's match is in a room, as on the real one, whose
// code is the player's own: six of a letter, `AAAAAA` for the first,
// `BBBBBB` for the second, and on through TIDE_ROOM_CODE_LETTERS' letters and
// then its digits. A room nobody opened says so at once, as the relay does.
// An address reaches the first player whose match is open on its port: its
// host says nothing there.
//
// The transports and what a host asks of them are in tide/platform.h
// (tide_platform_players and what follows it).

#include <stdbool.h>
#include <stdint.h>

#include "tide/platform.h"

#define TIDE_PLATFORM_PLAYERS_MOST 32u // A room's code has that many characters to say whose it is

// What `desc` says of the run, before its script is read: how many players it
// has, and which this is (tide_platform_players and tide_platform_player from
// then on). Nothing starts yet.
void tide_platform_players_configure(const tide_headless_desc *desc);

// The first player starts the others; another one finds the first. Ends the
// program, having said why, when it can't.
void tide_platform_players_start(void);

// Once frame `frame` drew, unless it was the run's last: what this player sent
// in it goes to the others, and what they sent comes. TIDE_KEEP_RUNNING, or
// the run's exit code when it ends here: another player's process ended,
// which the first says.
int tide_platform_players_step(uint32_t frame);

// The run's end, where this player's would end with `code`: the first waits
// for the others, and its exit code is the run's, the first that isn't 0.
int tide_platform_players_finish(int code);

// Interactive runs: broadcast the first player's command, then acknowledge
// its completion on every player before reporting success to the controller.
bool tide_platform_players_control(char *line, size_t capacity);
bool tide_platform_players_control_done(bool *ok);

// A script's `leave`, `drop` and `rejoin` lines for this player, at the
// frame they're polled in (script.c): leave and rejoin wait for the host
// (tide_platform_players_action); a drop silences this player's ends at once,
// both ways, and a rejoin lets them speak again.
void tide_platform_players_leave(void);
void tide_platform_players_drop(void);
void tide_platform_players_rejoin(void);

// Where a file of this player's goes: `path` with the player's number before
// its extension (fight-2.png) in a run of more than one, and `path` otherwise.
// Memory of the caller's.
char *tide_platform_players_file(const char *path);
