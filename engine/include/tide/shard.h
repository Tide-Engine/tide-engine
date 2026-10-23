#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/entity.h"
#include "tide/list.h"
#include "tide/net.h"
#include "tide/page.h"
#include "tide/player.h"

// Shards: one match world split across servers by where things are
// (docs/spec.md, Shards; notes/sharding.md).
//
// Each shard is an ordinary match world, which the generated game makes a
// shard by its hidden shard state. Everything a shard hears from another, or
// from the orchestrator, is a message the server hands it for a tick
// (tide_server_inject): its clients get it in that tick's frame and apply it
// the same way, so they keep the server's world. What a shard has to say goes
// in its outbox, which is world state: the orchestrator reads it after each
// tick, delivers each message, and acknowledges it, and the shard lets it go.
//
// Messages that change who owns something are numbered by their sender, and
// a receiver applies each number once, in order: a shard restored from a
// checkpoint sends again what it hadn't heard acknowledged, and receivers drop
// what they already applied. Ghosts (copies of other shards' entities near the
// border) are state, the newest of which wins, so they're not numbered.

// Who a message is from when it's from the orchestrator, not a shard.
#define TIDE_SHARD_ORCHESTRATOR 0xFFFFFFFFu
// The root shard: the singletons, what has no position, and the code that runs
// for no entity.
#define TIDE_SHARD_ROOT 0u
// A world that's no shard: an ordinary match of a sharded game, which owns all
// it holds.
#define TIDE_SHARD_NONE (-1)

// Message kinds: a message is its kind (one byte), its sender (u32), its number
// (u32, 0 for messages that aren't numbered), then what the kind says.
typedef enum tide_shard_kind {
    // Orchestrator: u32 shard id, u8 role (tide_shard_role), the regions (u32
    // count, each u32 shard, its box's lo and hi as six doubles, and u64 the
    // match its host knows it by), then the entity index ranges this shard
    // allocates from (u32 count, each u32 first and u32 end).
    TIDE_SHARD_CONFIG = 1,
    // Orchestrator: the regions, as in CONFIG.
    TIDE_SHARD_MAP = 2,
    // Orchestrator: one more range of entity indices (u32 first, u32 end).
    TIDE_SHARD_RANGE = 3,
    // A shard's ghosts for this one, since the last it sent: u32 count of
    // entities, each as tide_shard_game.border packs it, then u32 count of
    // handles it no longer holds near here (u32 index, u32 generation each),
    // then u8 whether the singletons follow, and if so, them.
    TIDE_SHARD_GHOSTS = 4,
    // Numbered: an entity moving here, u8 flags (TIDE_SHARD_BORROWED), u32 the
    // shard it's borrowed from, then the entity as border packs it.
    TIDE_SHARD_ARRIVE = 5,
    // Numbered: a shard asks for an entity: u32 the shard asking, then the
    // entity (u32 index, u32 generation). The one that owns it lends it, and
    // one that doesn't hands the request on to the one that does.
    TIDE_SHARD_BORROW = 6,
    // Numbered: the entity a shard asked for is gone (u32 index, u32 generation).
    TIDE_SHARD_GONE = 7,
    // Numbered: an event for an entity this shard owns: u32 index, u32
    // generation, u32 the event's number, u32 its sender (a command's), then its value.
    TIDE_SHARD_EVENT = 8,
    // Numbered: a scene unloaded: its entities here go too (u32 index, u32 generation).
    TIDE_SHARD_UNLOAD = 9,
    // Orchestrator: everything this shard sent, numbered up to and including
    // this (u32), has been delivered.
    TIDE_SHARD_ACK = 10,
    // Orchestrator: a player joined (u8 1) or left (u8 0) the world: u32 PlayerID.
    TIDE_SHARD_PLAYER = 11,
    // Numbered: a singleton moving here (u32 its number, u32 the shard it's
    // borrowed from, then its value).
    TIDE_SHARD_SINGLETON = 12,
    // Numbered: a shard asks for a singleton (u32 the shard asking, u32 its number).
    TIDE_SHARD_BORROW_SINGLETON = 13,
    // Orchestrator: where a player plays: u32 PlayerID, u32 its home shard, u32
    // count, then the shards its machine follows, its home among them, each
    // u32 shard and u64 the match a host knows it by (0 in one process).
    // Count 0: the player's gone. Every shard keeps every player's, so a
    // player's machine reads its own from whichever shard's world it has.
    TIDE_SHARD_ROUTE = 14,
    // Orchestrator: this shard's region merged into another shard's (u32 its
    // id): everything it owns goes, by position, and what has no position to
    // that shard, after which it ends.
    TIDE_SHARD_RETIRE = 15,
    // Numbered: an entity of an index this shard gave was destroyed on the
    // shard it went to (u32 index, u32 generation): the index is free again.
    TIDE_SHARD_FREED = 16,
    // Numbered, to the root: a region's code wrote the root's fields of the
    // singletons holding a C library's state ([Native]): their bytes, as GHOSTS
    // carries the root's. The root takes them, and every shard has them then.
    TIDE_SHARD_SHARED = 17,
} tide_shard_kind;

// A shard a player's machine follows, and the match its host knows it by.
typedef struct tide_shard_link {
    uint32_t shard;
    uint64_t match;
} tide_shard_link;

// ARRIVE's flags
#define TIDE_SHARD_BORROWED 1u

typedef enum tide_shard_role {
    TIDE_SHARD_REGION = 0,
    TIDE_SHARD_ROOT_ROLE = 1,
} tide_shard_role;

// Where an outbox message goes (tide_shard_outgoing.route).
typedef enum tide_shard_route {
    TIDE_SHARD_TO_SHARD = 0,  // to `target`, a shard's id
    TIDE_SHARD_TO_OWNER = 1,  // to whichever shard owns `entity` now
    TIDE_SHARD_TO_ALL = 2,    // to every shard but the sender
    TIDE_SHARD_TO_ORCHESTRATOR = 3,
    TIDE_SHARD_TO_RANGE = 4,  // to the shard whose range of indices has `entity`'s
} tide_shard_route;

// A region: the box of positions a shard owns, from lo up to (not including)
// hi. A game partitioned in 2D ignores z.
typedef struct tide_shard_box {
    double lo[3];
    double hi[3];
} tide_shard_box;

// A region, the shard that owns it, and the match its host knows that shard by
// (0 in one process, and until the host says).
typedef struct tide_shard_region {
    uint32_t shard;
    tide_shard_box box;
    uint64_t match;
} tide_shard_region;

// A message in a shard's outbox, as the orchestrator reads it.
typedef struct tide_shard_outgoing {
    uint32_t route;    // tide_shard_route
    uint32_t target;   // TO_SHARD's shard
    tide_entity entity; // TO_OWNER's entity
    uint32_t number;   // The message's own number (its header's)
    const uint8_t *data; // The message, header first, in the outbox's bytes
    uint32_t size;
} tide_shard_outgoing;

// The next message of an outbox's bytes (tide_shard_game.outbox), from `r`:
// false at the end, or where the bytes aren't one.
bool tide_shard_next_outgoing(tide_reader *r, tide_shard_outgoing *out);

// What a shard holds for one player: how many of the entities it owns are the
// player's (Owner), and the box around their positions.
typedef struct tide_shard_presence {
    tide_player_id player;
    uint32_t count;
    tide_shard_box box;
} tide_shard_presence;

// One packed entity, for tide_shard_game.border.
typedef void (*tide_shard_each)(void *context, tide_entity e, const uint8_t *data, uint32_t size);

// The shard state's numbers, for tests and tools.
typedef struct tide_shard_status {
    int32_t id;          // TIDE_SHARD_NONE for a world that's no shard
    uint32_t role;       // tide_shard_role
    uint32_t owned;      // Entities it owns
    uint32_t ghosts;     // Copies of other shards' entities it holds
    uint32_t waiting;    // Handlers and commands waiting for a border
    uint32_t outbox;     // Messages not yet acknowledged
    uint32_t lent;       // Requests for what it owns, waiting until it can lend it
    uint64_t indices;    // Entity indices it has left to give its spawns
    uint64_t ranges_end; // The end of the highest range of indices it was given
} tide_shard_status;

// What a sharded game gives the runtime: tide_game.shards, generated for
// games with `settings { shards = true; }`, NULL for others.
typedef struct tide_shard_game {
    double range;  // The game's reach
    uint32_t dims; // 2 or 3: the [Partition] field's
    // Starts `world` as a shard: the root as tide_game.start would (`start`,
    // the scene, or NULL for Main), a region with nothing in it. `config` is a
    // CONFIG message; the world's tick starts at `tick`.
    void (*start)(void *world, float dt, const void *start, uint32_t seed, uint32_t tick, const uint8_t *config,
                  uint32_t config_size);
    // A message for the coming tick, applied to the world before it: the
    // server calls it as it runs the tick, and clients as they apply its frame.
    // False when the bytes aren't a message, which changes nothing.
    bool (*receive)(void *world, const uint8_t *data, uint32_t size);
    // The outbox: every message not yet acknowledged, oldest first, as bytes
    // to free() (NULL for none): each u32 route, u32 target, u32 entity index,
    // u32 entity generation, u32 number, u32 size, then the message itself.
    // tide_shard_next_outgoing reads them.
    uint8_t *(*outbox)(const void *world, uint32_t *size);
    // Each entity the world owns whose position is in `box`, packed as ARRIVE
    // and GHOSTS carry them. Singletons aren't entities: see singletons.
    void (*border)(const void *world, const tide_shard_box *box, tide_shard_each each, void *context);
    // The singletons this world owns, packed as GHOSTS carries them, into
    // bytes to free(); NULL when it owns none.
    uint8_t *(*singletons)(const void *world, uint32_t *size);
    // How many entities it owns, and the owned ones' positions' box.
    uint32_t (*load)(const void *world, tide_shard_box *box);
    // Up to `capacity` positions of owned entities, every `stride`th, for
    // splitting a region by where its entities are.
    uint32_t (*sample)(const void *world, uint32_t stride, double (*out)[3], uint32_t capacity);
    // For each player owning entities here, how many and where (up to
    // `capacity`), and how many there are.
    uint32_t (*presence)(const void *world, tide_shard_presence *out, uint32_t capacity);
    // Its numbers (below), for the orchestrator, tests and tools.
    void (*status)(const void *world, tide_shard_status *out);
    // Each ghost it holds that's the root of what came (a tree comes whole),
    // with the shard whose it is: what a restored orchestrator knows it sent.
    void (*ghosts)(const void *world, void (*each)(void *context, tide_entity e, int32_t source), void *context);
    // A player's route, as the last ROUTE for them said: their home shard,
    // and up to `capacity` of the shards their machine follows. How many it
    // follows, 0 for no route.
    uint32_t (*route)(const void *world, tide_player_id player, uint32_t *home, tide_shard_link *out, uint32_t capacity);
    // The regions as the last CONFIG or MAP said: up to `capacity` of them,
    // and how many there are. Every shard has them, so a player's machine
    // finds the shards owning an area it watches in whichever world it has.
    uint32_t (*regions)(const void *world, tide_shard_region *out, uint32_t capacity);
    // Each entity the shard owns (no ghost), for the orchestrator to know
    // where everything is when it starts from a checkpoint.
    void (*owned)(const void *world, void (*each)(void *context, tide_entity e), void *context);
} tide_shard_game;


// Changes across a border that no run of their tick could leave to wait (code
// that can't wait yet, see notes/sharding.md), dropped, in this process.
uint64_t tide_shard_dropped(void);
void tide_shard_note_dropped(uint32_t count);

// ---------------------------------------------------------------------------
// What generated code calls (engine/src/shard.c)

// Code that touched what its shard doesn't own: the code (its number in the
// game), the entity it ran for, which of that code's runs this tick it was,
// and what it touched.
typedef struct tide_shard_attempt {
    uint32_t code;
    uint32_t occurrence;
    tide_entity entity;
    tide_entity touched;
} tide_shard_attempt;

// Two entities a run's code says act on each other now (Shards.Together, +1)
// or no longer (Shards.Apart, -1): its shard keeps them on one shard while
// they do (tide_shard_state.touching).
typedef struct tide_shard_pair {
    tide_entity a, b;
    int32_t delta;
} tide_shard_pair;

// A tick's runs: what the last run noted, and what the next skips. Outside the
// world: a tick that ran again starts from the world as it was before it.
typedef struct tide_shard_tick {
    tide_shard_attempt *noted;
    uint32_t noted_count, noted_room;
    tide_shard_attempt *skip; // Sorted by code, entity and occurrence
    uint32_t skip_count, skip_room;
    uint32_t *occurrences;    // Each code's runs so far this run
    uint32_t codes;
    uint32_t runs;            // Runs so far
    bool unknown;             // Code that can't be skipped noted something: no run can avoid it
    tide_shard_pair *pairs;   // What the last run said acts on what, in no order: its shard sorts them
    uint32_t pair_count, pair_room;
    int lock;
} tide_shard_tick;

// Code that's running now, on this thread: what tide_shard_note records it as.
extern TIDE_THREAD_LOCAL uint32_t tide_shard_code;
extern TIDE_THREAD_LOCAL uint32_t tide_shard_occurrence;
extern TIDE_THREAD_LOCAL tide_entity tide_shard_this;
// Shards.delay: how many ticks the code running now waited on a border
extern TIDE_THREAD_LOCAL int32_t tide_shard_delay_now;

// The code running now touched `touched`, which its shard doesn't own: it's
// noted, under a lock, as systems run on several threads.
void tide_shard_note(tide_shard_tick *t, tide_entity touched);
// Shards.Together(a, b) (delta 1) and Shards.Apart(a, b) (-1), noted the same
// way. NULL: a world that's no shard, where nothing's noted.
void tide_shard_pair_note(tide_shard_tick *t, tide_entity a, tide_entity b, int32_t delta);
// Whether the next run skips the code running now (tide_shard_code and the rest).
bool tide_shard_skips(const tide_shard_tick *t, uint32_t code, tide_entity entity, uint32_t occurrence);
// What the last run noted, as what the next skips; false when it noted nothing.
bool tide_shard_tick_again(tide_shard_tick *t);
// A new run: nothing noted, every code's count back at 0.
void tide_shard_tick_begin(tide_shard_tick *t, uint32_t codes);
void tide_shard_tick_free(tide_shard_tick *t);
// The entities a skipped run of code touched: up to `capacity` into `out`, and how many.
uint32_t tide_shard_touched(const tide_shard_tick *t, uint32_t code, tide_entity entity, uint32_t occurrence,
                            tide_entity *out, uint32_t capacity);

// Shards.Owns: what the world being ticked or drawn says, which its tick or
// frame tells this thread (tide_shard_use); true with none.
typedef bool (*tide_shard_owner)(const void *world, tide_entity e);
typedef int64_t (*tide_shard_mover)(const void *world);
void tide_shard_use(const void *world, tide_shard_owner owns, tide_shard_mover moves);
bool tide_shard_owns_now(tide_entity e);
// Shards.moves: how many times an entity came to the world being ticked or
// drawn, or left it, as its shard has it; 0 with none.
int64_t tide_shard_moves_now(void);

// A byte list (List<byte>) as one run of bytes: appended to, read whole (to
// tide_shard_bytes_done after), and replaced.
void tide_shard_bytes_append(tide_list *l, const void *data, uint32_t size, uint32_t where);
const uint8_t *tide_shard_bytes_flat(tide_list l, uint32_t *size, bool *copied);
void tide_shard_bytes_done(tide_list l, const uint8_t *flat, bool copied);
void tide_shard_bytes_set(tide_list *l, const void *data, uint32_t size, uint32_t where);

// Writing message headers.
static inline void tide_shard_header(tide_writer *w, const tide_shard_kind kind, const uint32_t from, const uint32_t number)
{
    tide_write_u8(w, (uint8_t)kind);
    tide_write_u32(w, from);
    tide_write_u32(w, number);
}

// A u32 written over four bytes already written, as tide_write_u32 writes one.
static inline void tide_shard_patch_u32(uint8_t *at, const uint32_t value)
{
    for (uint32_t k = 0; k < 4u; k++) at[k] = (uint8_t)(value >> (8u * k));
}

// Whether `p` is in the box: lo <= p < hi on each of `dims` axes.
static inline bool tide_shard_box_holds(const tide_shard_box *b, const double p[3], const uint32_t dims)
{
    for (uint32_t i = 0; i < dims; i++) {
        if (!(p[i] >= b->lo[i] && p[i] < b->hi[i])) return false;
    }
    return true;
}

// Whether two boxes overlap once `a` is grown by `margin` on every side.
static inline bool tide_shard_box_near(const tide_shard_box *a, const tide_shard_box *b, const double margin,
                                       const uint32_t dims)
{
    for (uint32_t i = 0; i < dims; i++) {
        if (a->hi[i] + margin <= b->lo[i] || b->hi[i] <= a->lo[i] - margin) return false;
    }
    return true;
}
