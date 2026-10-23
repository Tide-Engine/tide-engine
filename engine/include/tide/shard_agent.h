#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/session.h"
#include "tide/shard.h"

// A shard's agent: beside the shard's server, wherever that runs, it tells
// the world's coordinator (tide/shard_coordinator.h) what the coordinator
// needs of the shard after each of the server's updates (a report), and does
// what the coordinator tells it (instructions). Both are bytes, so the two
// can be in one process (tide/shards.h) or on different machines (Tide's
// hosting service).
//
// What it reports: the shard's status and the players connected to it, where
// each player's entities are, the messages its world sent since the last
// report, and its ghosts for its neighbours: what changed of what it owns near
// each one's region since it last sent them, worked out here, as only this
// shard has its world. What it does: hands the server messages from other
// shards and the coordinator, admits players, and when asked, says where its
// entities are and makes a checkpoint of its world.

typedef struct tide_shard_agent tide_shard_agent;

// The agent of `shard`, whose server is `server` (the agent doesn't own it).
// `ticks_before`: the shard's ticks before this server's first, when it went
// on from a checkpoint, so the ticks it reports go on from there. `fresh`:
// it went on from a checkpoint, and its first report says what it owns and
// holds, for the coordinator to know where everything is.
tide_shard_agent *tide_shard_agent_create(const tide_game *game, tide_server *server, uint32_t shard,
                                          uint32_t ticks_before, bool fresh);
void tide_shard_agent_destroy(tide_shard_agent *a);
// Its report, once the server has updated (and the shard's journal has kept
// what its ticks were): bytes to free(), for tide_shard_coordinator_report.
uint8_t *tide_shard_agent_report(tide_shard_agent *a, uint32_t *size);
// The coordinator's instructions (tide_shard_coordinator_instructions):
// false, doing nothing, for bytes that aren't any.
bool tide_shard_agent_apply(tide_shard_agent *a, const uint8_t *data, uint32_t size);
// Whether it has a checkpoint of its world to report.
bool tide_shard_agent_checkpointing(const tide_shard_agent *a);
// The shard's ticks so far, as its journal numbers them.
uint32_t tide_shard_agent_tick(const tide_shard_agent *a);

// ---------------------------------------------------------------------------
// The bytes between them, little-endian. Both start with their magic.
//
// Instructions: u32 TIDE_SHARD_INSTRUCTIONS, u32 batch (from 1, each a
// shard's own), u8 flags (TIDE_SHARD_DO_*), u32 count and each message to
// hand the server (u32 size, bytes), u32 count and each player to admit (u32
// PlayerID, u64 cookie); with DO_TARGETS, u32 count and each shard its ghosts
// go to (u32 shard, u8 whether a region, 48 bytes its box); with DO_SAMPLE,
// u32 every how many entities' positions to sample; with DO_HELD, u32 count
// and each ghost of this shard's another holds (u32 holder, u64 entity key).
//
// Report: u32 TIDE_SHARD_REPORT, u32 shard, u32 the last batch applied, u32
// the shard's tick (as its journal numbers them), u32 messages handed the
// server and not yet applied, its status (i32 id, u32 role, owned, ghosts,
// waiting, outbox, lent, u64 indices, ranges_end), u32 count and each player
// connected (u32 PlayerID, u64 cookie), u32 count and each player's presence
// (u32 PlayerID, u32 count, 48 bytes box), u32 count and each message its
// world sent since the last report (as an outbox record: u32 route, target,
// entity index, entity generation, number, size, then the message), u32 count
// and each ghost message (u32 the shard it's for, u32 size, the message);
// then u8 and, if 1, the box its entities are in and u32 count and each
// sampled position (3 doubles); u8 and, if 1, u32 count and each entity it
// owns (u64 key); u8 and, if 1, u32 count and each ghost it holds (u64 key,
// u32 its shard); u8 and, if 1, its checkpoint: u32 the tick its journal goes
// on from, u32 size and its packed world.

#define TIDE_SHARD_INSTRUCTIONS 0x4E495354u // "TSIN"
#define TIDE_SHARD_REPORT 0x50525354u       // "TSRP"

enum {
    TIDE_SHARD_DO_FROM_START = 1,  // Report every message its world has, not only those since the last report
    TIDE_SHARD_DO_CHECKPOINT = 2,  // Make a checkpoint of its world, for the next report
    TIDE_SHARD_DO_TARGETS = 4,     // Where its ghosts go now
    TIDE_SHARD_DO_SAMPLE = 8,      // Report where its entities are
    TIDE_SHARD_DO_HELD = 16,       // Ghosts of its entities other shards hold
};
