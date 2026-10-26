#pragma once

// A match's ticks, recorded, kept and shown again: a debugger's (Unity's time
// travel, tide/embed.h). It keeps what went into every tick from where it
// started (the server's journal records: inputs, joins and leaves, commands)
// and the worlds of the latest ticks, so any of those shows at once and any
// tick since the start can be made again by replaying it (and the worlds a
// debugger made the match go back to, tide_timeline_cut). As bytes, a
// recording replays on another machine bit for bit, each tick's world checked
// against the hash the server's had, which tells where a build that changed
// first goes otherwise.
//
// Tick n here is the world after n ticks: the tick that made it is the
// record of tick n - 1 (tide_server_desc.journal).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tide/session.h"

typedef struct tide_timeline tide_timeline;

typedef struct tide_timeline_desc {
    const tide_game *game;
    uint32_t keep;         // Ticks whose worlds it keeps to show at once: 0 for 600
    const tide_jobs *jobs; // Threads to replay ticks on, or NULL: this one
} tide_timeline_desc;

tide_timeline *tide_timeline_create(const tide_timeline_desc *desc);
// Lets go of its worlds: before the code of the game that made them goes.
void tide_timeline_destroy(tide_timeline *t);

// Recording a match (tide_session_recorder): from `world` (copied), as it is
// after `tick` ticks, everything before it let go...
void tide_timeline_begin(tide_timeline *t, const void *world, uint32_t tick);
// ...and each tick after it: its journal record and the world after it. A
// record that isn't the server's next tick's is left out.
void tide_timeline_tick(tide_timeline *t, uint32_t tick, const uint8_t *record, uint32_t size, const void *world);
// The recorder that does both, for tide_session_record.
tide_session_recorder tide_timeline_recorder(tide_timeline *t);

// A build of the same data layout runs the game from now on (a hot reload):
// its worlds are that build's to show, free and replay.
void tide_timeline_set_game(tide_timeline *t, const tide_game *game);

bool tide_timeline_started(const tide_timeline *t);
uint32_t tide_timeline_first(const tide_timeline *t); // The tick it starts at
uint32_t tide_timeline_last(const tide_timeline *t);  // The last tick it has a record of
uint32_t tide_timeline_kept(const tide_timeline *t);  // The first of the ticks it keeps worlds of, up to the last made
uint32_t tide_timeline_made(const tide_timeline *t);  // The last tick whose world it has made: the last, recording

// The world after `tick` ticks, from first to last: one it keeps, or made
// again by replaying the ticks before it, each checked (tide_timeline_check_of).
// Its until the next call; NULL outside them, or past a tick this build can't
// replay.
const void *tide_timeline_world(tide_timeline *t, uint32_t tick);

// What replaying found: every tick to `checked` made the world the server's
// had, or the first that didn't, `differs` (its world's hash `got`, the
// server's `expected`), and the first this build couldn't replay at all,
// `unreadable`; 0 for none.
typedef struct tide_timeline_check {
    uint32_t checked;
    uint32_t differs;
    uint32_t unreadable;
    uint64_t expected, got;
} tide_timeline_check;

void tide_timeline_check_of(const tide_timeline *t, tide_timeline_check *out);

// The match went back to tick `tick` and goes on from `world` there
// (tide_session_rewind's: the world after `tick` ticks with the match's
// commands as they were): the ticks after it are let go, `world` is kept as
// tick `tick`'s, and the next record, whichever server tick it's of, is the
// next tick's. Where `world` isn't what replaying makes of that tick, the
// recording says so, so a replay jumps there too. False, changing nothing,
// for a tick outside it.
bool tide_timeline_cut(tide_timeline *t, uint32_t tick, const void *world);

// As bytes, to free(), with what the machine that recorded it says of itself:
// its matches' tick rate and its player (tide_player_id.id, 0 for none). NULL
// before it's started, or without the memory.
uint8_t *tide_timeline_save(tide_timeline *t, uint32_t tick_rate, uint32_t player, uint32_t *size);

typedef struct tide_timeline_about {
    uint32_t tick_rate;
    uint32_t player;
    bool other_build; // Recorded by a build whose simulation was another (tide_game.simulation): its ticks may go otherwise
} tide_timeline_about;

// A recording saved by tide_timeline_save, to replay with `desc`'s game, which
// must have the data layout it was recorded with. NULL, saying why into `why`,
// for bytes that aren't one or a game of another layout.
tide_timeline *tide_timeline_load(const tide_timeline_desc *desc, const uint8_t *data, size_t size, tide_timeline_about *about,
                                  char *why, size_t why_size);
