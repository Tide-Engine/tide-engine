#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/shard.h"

// A sharded world's coordinator (docs/spec.md, Shards; notes/sharding.md):
// from each shard's agent's reports (tide/shard_agent.h), it decides what each
// shard is told (its instructions) and what its host does (actions: start a
// shard, stop one, keep a checkpoint). It carries what shards send each other
// to where it's for, keeps where every entity is, gives out entity indices,
// sends each region's ghosts to its neighbours, follows the players, splits a
// region whose shard holds too much and merges quiet neighbours, and makes
// checkpoints. It touches no world: what it knows of the shards is what they
// report, so it runs beside them (tide/shards.h) or on another machine.
//
// Each update: the hosts update their shards, hand it each shard's report
// (tide_shard_coordinator_report), then it steps, after which they do its
// actions and hand each shard its instructions. A change of the world's shape
// (a split, a merge) waits for a checkpoint of it to be kept before any shard
// hears of it, so the shards' journals never depend on a shape no checkpoint has.

typedef struct tide_shard_coordinator tide_shard_coordinator;

typedef struct tide_shard_coordinator_desc {
    uint32_t players;  // How many the game takes (tide_game_players)
    double range;      // The game's reach (tide_shard_game.range)
    uint32_t dims;     // ...and how many axes its positions have
    // The box the regions divide, which grow past it to take what goes
    // outside, and regions along each axis to start with (0 counts as 1)
    tide_shard_box world;
    uint32_t split[3];
    // A region whose shard owns more entities than this is split in two, by
    // where they are, down to twice the range across, and two neighbouring
    // regions owning a quarter of it together merge (0: neither)
    uint32_t most_entities;
    // ...and so is one whose shard is steadily busier than this, the share of
    // its time its updates take (tide_shard_agent_busy), and two neighbouring
    // regions merge only if they're a quarter of it together too (0: neither)
    float busiest;
    // Checkpoints: now and then (`checkpoint_seconds`, 0: 60) and after each
    // change of shape. False: none, and a change of shape is told at once.
    bool checkpoints;
    double checkpoint_seconds;
} tide_shard_coordinator_desc;

typedef enum tide_shard_action_kind {
    // Start shard `shard`: from `world` (its packed world, a checkpoint's) and
    // its journal's records from tick `tick` on; or, with no world, a world
    // started anew from `config` (its CONFIG message) at tick `tick`, the
    // root's in the game's start scene. Then its agent, with `ticks_before`
    // the tick it goes on from, and fresh when it went on from a checkpoint.
    TIDE_SHARD_ACTION_START,
    // Stop shard `shard`: its region merged into another's, and it's empty.
    TIDE_SHARD_ACTION_STOP,
    // Keep this checkpoint (`data`, `size`) for good, then say so
    // (tide_shard_coordinator_kept): the journals' records before it aren't needed.
    TIDE_SHARD_ACTION_CHECKPOINT,
} tide_shard_action_kind;

typedef struct tide_shard_action {
    tide_shard_action_kind kind;
    uint32_t shard;
    uint32_t tick;
    const uint8_t *config;
    uint32_t config_size;
    const uint8_t *world;
    uint32_t world_size;
    const uint8_t *data;
    uint32_t size;
} tide_shard_action;

// A new world: its first actions start the root and the regions.
tide_shard_coordinator *tide_shard_coordinator_create(const tide_shard_coordinator_desc *desc, double now);
// A world from a checkpoint (TIDE_SHARD_ACTION_CHECKPOINT's bytes): its first
// actions start each shard from its world. NULL for bytes that aren't one.
tide_shard_coordinator *tide_shard_coordinator_load(const tide_shard_coordinator_desc *desc, const uint8_t *data,
                                                    uint32_t size, double now);
void tide_shard_coordinator_destroy(tide_shard_coordinator *c);

// A shard's report: false for bytes that aren't one, or a shard it doesn't run.
bool tide_shard_coordinator_report(tide_shard_coordinator *c, const uint8_t *report, uint32_t size);
// Decides, from the reports since the last step.
void tide_shard_coordinator_step(tide_shard_coordinator *c, double now);
// Its actions since the last step, in order, for the host: valid until the
// next step. How many there are.
uint32_t tide_shard_coordinator_actions(const tide_shard_coordinator *c, const tide_shard_action **out);
// A shard's instructions, for its agent (tide_shard_agent_apply): bytes to
// free(), or NULL when there are none.
uint8_t *tide_shard_coordinator_instructions(tide_shard_coordinator *c, uint32_t shard, uint32_t *size);
// The last checkpoint is kept for good.
void tide_shard_coordinator_kept(tide_shard_coordinator *c);
// A shard started again, elsewhere, from the last checkpoint and its journal
// (its host failed): what it was handed and hadn't applied went with it, so
// every shard sends again what wasn't acknowledged (receivers drop what they
// had by number), it's told the regions, its ranges and the players again, its
// neighbours send it their ghosts whole, and its first report says what it
// owns and holds.
void tide_shard_coordinator_restarted(tide_shard_coordinator *c, uint32_t shard);
// Whether it waits for shards' checkpoints (their next reports have them).
bool tide_shard_coordinator_checkpointing(const tide_shard_coordinator *c);
// The game's build changed and its worlds were laid out anew: checkpoints of
// the old layout are no use to it, so one is made as soon as it can be.
void tide_shard_coordinator_rebuilt(tide_shard_coordinator *c);

// One shard's world in a checkpoint (TIDE_SHARD_ACTION_CHECKPOINT's bytes),
// and the tick its journal goes on from: false for none, or bytes that aren't one.
bool tide_shard_checkpoint_world(const uint8_t *data, uint32_t size, uint32_t shard, const uint8_t **world,
                                 uint32_t *world_size, uint32_t *tick);
// A checkpoint with each shard's world made anew by `world` (from its packed
// bytes, as bytes to free(), or NULL to fail): a hot reload's, carried over
// to another layout. NULL for bytes that aren't a checkpoint, or a world that fails.
typedef uint8_t *(*tide_shard_rewrite_fn)(void *user, uint32_t shard, const uint8_t *world, uint32_t size, uint32_t *out_size);
uint8_t *tide_shard_checkpoint_rewrite(const uint8_t *data, uint32_t size, tide_shard_rewrite_fn world, void *user,
                                       uint32_t *out_size);

// A checkpoint now, its own part and each running shard's world as `world`
// gives it (packed, with the tick its journal goes on from): for a host with
// every shard at hand (tide_shards_save). Bytes to free().
typedef uint8_t *(*tide_shard_world_fn)(void *user, uint32_t shard, uint32_t *size, uint32_t *tick);
uint8_t *tide_shard_coordinator_save(const tide_shard_coordinator *c, tide_shard_world_fn world, void *user, uint32_t *size);

// A player joined the world, or left it: the root sends PlayerJoined or PlayerLeft.
void tide_shard_coordinator_player(tide_shard_coordinator *c, tide_player_id player, bool joined);
// Shards are numbered from 0, the root; one that ended is no shard.
uint32_t tide_shard_coordinator_count(const tide_shard_coordinator *c);
bool tide_shard_coordinator_running(const tide_shard_coordinator *c, uint32_t shard);
bool tide_shard_coordinator_region(const tide_shard_coordinator *c, uint32_t shard, tide_shard_box *out); // False for the root
// A shard's status, as it last reported it.
void tide_shard_coordinator_status(const tide_shard_coordinator *c, uint32_t shard, tide_shard_status *out);
// Which shard owns an entity, as far as it knows: -1 for none.
int32_t tide_shard_coordinator_owner(const tide_shard_coordinator *c, tide_entity e);
// The shard whose region holds a position, or the root's (0) outside them all.
uint32_t tide_shard_coordinator_at(const tide_shard_coordinator *c, const double p[3]);
// The match a shard's host knows it by, for players' routes (0 in one process).
void tide_shard_coordinator_set_match(tide_shard_coordinator *c, uint32_t shard, uint64_t match);
