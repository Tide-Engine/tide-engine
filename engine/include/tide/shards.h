#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/session.h"
#include "tide/shard.h"

// The engine's orchestrator: every shard of a sharded match's world in one
// process (docs/spec.md, Shards; notes/sharding.md). Each shard is a server
// (tide/session.h) its players connect to; the orchestrator steps them
// together, which keeps their ticks in step, carries each shard's messages to
// the shards they're for, sends each shard copies of its neighbours'
// entities near its border (ghosts), splits a region whose shard holds too
// much, and saves every shard at one tick as a checkpoint.
//
// Tide's hosting service runs the same shards across machines; this one is
// for development, tests and worlds one machine holds.

typedef struct tide_shards tide_shards;

typedef struct tide_shards_desc {
    const tide_game *game;   // A sharded game's (tide_game.shards)
    uint32_t tick_rate;      // 0: the game's, or else 60
    float dt;                // 0: 1 / tick_rate
    const void *start;       // The scene the match starts in (tide_start), NULL: Main
    uint32_t seed;           // Time.seed, every shard's
    // Where things are: the box the regions divide, which grow past it to
    // take what goes outside. `split` regions along each axis to start with
    // (0 counts as 1).
    tide_shard_box world;
    uint32_t split[3];
    // A region whose shard owns more entities than this is split in two, by
    // where they are, down to twice the game's range across, and two
    // neighbouring regions whose shards own a quarter of it together merge
    // back into one (0: neither).
    uint32_t most_entities;
    // Each shard's server's transports (tide_server_desc.transports), made as
    // the shard starts: `shard` is its id. NULL: none, as in tests that only
    // tick the shards.
    void (*transports)(void *user, uint32_t shard, tide_transport out[2]);
    void *user;
    const tide_jobs *jobs;
    // A host of its own runs each shard's server as one of its matches
    // (tide/hosting.h): `host_start` makes it from `desc` and says the match
    // the host knows it by, the host updates it, and `host_stop` ends it. NULL: the
    // orchestrator makes, updates and ends them itself.
    tide_server *(*host_start)(void *user, uint32_t shard, const tide_server_desc *desc, uint64_t *match);
    void (*host_stop)(void *user, uint32_t shard, uint64_t match);
    // The world's journal, so it comes back after a crash where it was
    // (tide_shards_recover). Each shard's tick, as its record
    // (tide_server_desc.journal); then, once an update, `flush`: every record
    // so far is to be kept for good by the time it returns, as nothing a shard
    // sent goes on to another before its ticks are kept. Now and then
    // (`checkpoint_seconds`, 0: 60), and whenever the regions or the entity
    // ranges change, a checkpoint (tide_shards_save's bytes), after which the
    // records before it aren't needed. NULL: none of it.
    void (*journal)(void *user, uint32_t shard, uint32_t tick, const uint8_t *record, uint32_t size);
    void (*flush)(void *user);
    void (*checkpoint)(void *user, const uint8_t *data, uint32_t size);
    double checkpoint_seconds;
} tide_shards_desc;

// A dedicated server's matches as the shards (tide_shards_desc.host_start
// and .host_stop, with the tide_hosting as `user`): kept running with no one in them,
// and the root the host's front match, which players asking for match 0 join.
tide_server *tide_shards_hosting_start(void *hosting, uint32_t shard, const tide_server_desc *desc, uint64_t *match);
void tide_shards_hosting_stop(void *hosting, uint32_t shard, uint64_t match);

tide_shards *tide_shards_create(const tide_shards_desc *desc, double now);
void tide_shards_destroy(tide_shards *s);
// Runs every shard's due ticks, then carries what they sent and their ghosts
// to the shards they're for, which apply them at their next ticks.
void tide_shards_update(tide_shards *s, double now);

// Shards are numbered from 0, the root; one that was merged away is NULL.
uint32_t tide_shards_count(const tide_shards *s);
tide_server *tide_shards_server(const tide_shards *s, uint32_t shard);
const void *tide_shards_world(const tide_shards *s, uint32_t shard);
bool tide_shards_region(const tide_shards *s, uint32_t shard, tide_shard_box *out); // False for the root
void tide_shards_status(const tide_shards *s, uint32_t shard, tide_shard_status *out);
// Which shard owns an entity now, as the orchestrator knows it: the shard it
// last went to, or the one that gave its handle. -1 for none.
int32_t tide_shards_owner(const tide_shards *s, tide_entity e);
// A player joined the world, or left it: the root sends PlayerJoined or PlayerLeft.
void tide_shards_player(tide_shards *s, tide_player_id player, bool joined);
// The shard whose region holds a position, or the root's (0) outside them all.
uint32_t tide_shards_at(const tide_shards *s, const double p[3]);

// A checkpoint: every shard's world and what the orchestrator keeps, at one
// tick, between updates; bytes to free(). Loading one makes the orchestrator
// it was, its shards' servers new (players connect again).
uint8_t *tide_shards_save(const tide_shards *s, uint32_t *size);
tide_shards *tide_shards_load(const tide_shards_desc *desc, const uint8_t *data, uint32_t size, double now);
// A checkpoint, then each shard's journal records kept since it, run again:
// the world as it was at the last records kept. `records` gives shard
// `shard`'s records in turn (the `index`th from 0), false past the last;
// records from before the checkpoint are passed over. NULL for a world whose
// records don't make the ticks they say.
typedef bool (*tide_shards_records)(void *user, uint32_t shard, uint32_t index, const uint8_t **record, uint32_t *size);
tide_shards *tide_shards_recover(const tide_shards_desc *desc, const uint8_t *data, uint32_t size,
                                 tide_shards_records records, void *records_user, double now);
