#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/events.h"
#include "tide/jobs.h"
#include "tide/net.h"
#include "tide/player.h"

// Sessions: how a machine takes part in a match.
//
// A server runs the match. It ticks the one true world with the inputs players
// send, and sends every player each tick: whose input changed and to what,
// players joining and leaving, and a hash of the world after it.
//
// A client predicts. It runs a few ticks ahead of the server, so its inputs
// reach the server in time, with its own input straight away and, for everyone
// else, their last one: for a while (tide_client_desc.guess_seconds), then as
// if they'd let go. It keeps a snapshot of each tick from the last one the
// server confirmed (the verified world) to the one it predicted. A tick that
// arrives as it was predicted only has its hash checked; one that went
// otherwise runs on the verified world, and the ticks after it run again. A
// hash that differs means the client went wrong somewhere, and it asks for the
// world again: the server sends the hashes of its world's pages, and then the
// pages the client's own world lacks (tide/delta.h).
//
// A player joining gets the server's world, whole, then plays on from there,
// and a cookie: coming back with it, the player gets their PlayerID back. If
// the server still has them connected, the new connection takes over with no
// events; if they'd left, PlayerJoined comes again, with the same PlayerID.
// Single-player and hosting are the same: a server and its own player on one
// machine, over a loopback transport (tide_session).
//
// Host migration (tide_game.host_migration): the server tells its players
// where to meet when it goes, its room and the room's key, and the digests of
// every player's cookie (SHA-256, so none can be worked back from them). When
// the server leaves or stops answering, its players go to the room: one takes
// the match over from the last tick it verified (tide_session_take_over), and
// the others join it again, as the same players, since it knows their
// cookies' digests, getting only what their last worlds lack of its world.
// The server's player leaves the match. Nothing else
// changes hands: what the server kept of its own, the others never had.
//
// A match whose last scene unloads, when Main is local, ends: the server stops
// ticking and tells every player, who go offline with TIDE_DISCONNECT_ENDED.

// How a game's clients keep up with the match (settings { prediction = ...; }).
// Tide's Prediction has the same values.
typedef enum tide_prediction {
    // Clients predict ahead, with no input delay: their own input applies at
    // once, and other players' are guessed. When running the predicted ticks
    // again would cost more than a frame's budget, a client delays its input a
    // tick at a time, predicting less, until it fits; it takes the delay back
    // when there's room.
    TIDE_PREDICTION_ROLLBACK,
    // Clients never predict: their inputs are delayed so every player's reach
    // the server before its tick, and clients run ticks as the server confirms
    // them, as many ticks behind as that takes. Nothing is ever run again, so
    // the simulation runs once on every machine, at the cost of input delay.
    TIDE_PREDICTION_DELAY,
} tide_prediction;

typedef struct tide_view_time {
    int32_t tick; // Upper endpoint; the presented time is tick - 1 + alpha.
    float alpha;
    bool valid;
} tide_view_time;

// What a session needs from a game. tidec generates it as tide_game_api in
// <game>.h; games without an input have no input functions.
typedef struct tide_game {
    float max_rewind; // Finite seconds of collider history; 0 disables it.
    tide_view_time (*view_time)(const void *input);
    void (*set_view_time)(void *input, tide_view_time time);
    uint64_t hash;            // Tells builds apart: servers only take players of the same game
    uint32_t world_size;      // sizeof(tide_world): a world starts as that many zeros
    uint32_t input_size;      // sizeof(tide_input), or 0
    uint32_t max_input_bytes; // The most write_input or write_input_delta writes
    uint32_t start_size;      // sizeof(tide_start)
    // Clears the world and starts the match: `start` is a tide_start, the scene
    // it starts in, or NULL for Main. `seed` is the match's (Time.seed, its
    // low 31 bits), which a game without one ignores (see `seeded`).
    void (*start)(void *world, float dt, const void *start, uint32_t seed);
    // Runs a tick, on `jobs`' threads (tide/jobs.h), or NULL for this one's.
    void (*tick)(void *world, const tide_jobs *jobs);
    // The same, recording into `events` what the tick sent that the game's
    // local handlers hear (tide/events.h): the events, each with what its
    // handlers read, beside the world and never in it. The world comes out
    // the same as from `tick`. NULL: the game has no local handlers of the
    // match's events, and nothing is recorded for it.
    void (*tick_events)(void *world, const tide_jobs *jobs, tide_event_log *events);
    // Records into `events` a `Spawned` for each entity `world` has that `had`
    // doesn't (NULL: every one), for the local handlers that hear the match's:
    // the entities a world starts with, or arrives with, were spawned in no
    // tick this machine ran, and are heard as `world` has them, in its
    // order. An entity is one `had` has by its handle. NULL: no local handler
    // hears the match's `Spawned`. A host that ticks a world itself calls it
    // once the world has started, and hands the log to the game's local state
    // as it does a tick's.
    void (*hear_arrived)(const void *world, const void *had, tide_event_log *events);
    // A snapshot: `to` becomes `from`, sharing its pages until one of them
    // changes. The hash covers what's in use, and only reads what changed since
    // it was last taken.
    void (*copy_world)(void *to, const void *from);
    uint64_t (*hash_world)(const void *world);
    // The same hash, with the pages that changed since the last one hashed on
    // `jobs`' threads first (tide/page.h), where that's megabytes: what a
    // session with threads calls after a tick, never during one. NULL in a
    // game built before there was one, and `hash_world` then.
    uint64_t (*hash_world_on)(const void *world, const tide_jobs *jobs);
    // Lets go of what a world has, before its memory goes; NULL for worlds of
    // plain data.
    void (*free_world)(void *world);
    // The world as bytes, to carry it over to another build: written into
    // `out` when they fit in `capacity` (NULL to ask), and how many there are.
    // Unpacking makes a world from them, false if they aren't one.
    uint32_t (*pack_world)(const void *world, uint8_t *out, uint32_t capacity);
    bool (*unpack_world)(void *world, const uint8_t *data, uint32_t size);
    // The world as a delta (tide/delta.h), to send it: what differs from
    // `base`, a world the receiver has too, or from the receiver's own world,
    // which lacks the pages `need` says, or neither: all of it. The bytes are
    // to free(). Unpacking makes `world` from them and the base they were made
    // from (NULL for none, never `world`): false if they don't make the world
    // they say.
    uint8_t *(*pack_delta)(const void *world, const void *base, const uint8_t *need, uint32_t need_size, bool assets,
                           uint32_t *size);
    bool (*unpack_delta)(void *world, const void *base, const uint8_t *data, uint32_t size);
    // The hashes of a world's pages (bytes to free()), and which of them
    // `base` lacks, into `out`: its size, as much as fits in `capacity`, past
    // which it lacks the rest. NULL: worlds always go whole.
    uint8_t *(*hash_pages)(const void *world, uint32_t *size);
    uint32_t (*need_pages)(const void *base, const uint8_t *hashes, uint32_t size, uint8_t *out, uint32_t capacity);
    void (*player_joined)(void *world, tide_player_id player);
    void (*player_left)(void *world, tide_player_id player);
    void (*set_input)(void *world, tide_player_id player, const void *input);
    void (*set_server_input)(void *world, const void *input);
    uint32_t (*write_input)(const void *input, uint8_t *out, uint32_t capacity); // Bytes written, 0 if it didn't fit
    bool (*read_input)(const uint8_t *data, uint32_t size, void *input);
    // The same as what differs from `previous`, field by field: an input that
    // changed little takes a few bits. NULL: inputs always go whole.
    uint32_t (*write_input_delta)(const void *input, const void *previous, uint8_t *out, uint32_t capacity);
    bool (*read_input_delta)(const uint8_t *data, uint32_t size, const void *previous, void *input);
    // A slot's input as the world keeps it: players' by index, then the
    // server's (the slot after the last player's: tide_game_players). What
    // ticks' inputs go as deltas from.
    void (*world_input)(const void *world, uint32_t slot, void *input);
    // What a client's guess comes to once it has gone on long enough
    // (tide_client_desc.guess_seconds): every slot's input but `keep`'s as if
    // its player had let go, each field back to its default but those the
    // game marks [Absolute], and with no button going up for it. True if that
    // changed any of them. Only ever on a predicted world: the server's
    // inputs, and a confirmed tick's, are what was sent. NULL: no default
    // let-go step; predict_inputs can still adjust guesses each tick.
    bool (*let_go_inputs)(void *world, const tide_player_set *keep);
    // Adjust guessed inputs before each predicted tick, excluding keep's
    // real samples. No invented button edges. True if any input changed,
    // so confirmation replays that tick instead of reusing its guess.
    // NULL: guesses keep their previous values until let_go_inputs.
    bool (*predict_inputs)(void *world, const tide_player_set *keep);
    // Commands are bounded, copied wire values. Checking only decodes them,
    // and says which channel the command's type is in: 0, the default, one
    // of the game's own (1 to command_channels - 1), or
    // TIDE_COMMAND_UNORDERED. Dispatch records one for this tick, with the
    // connection's sender, and advances its channel's sequence; `channel` has
    // to be the bytes' own. Sequences are world state, a player's per
    // channel: snapshots, hashes and migration keep them, even when the
    // game's handler chooses to do nothing. An unordered command has an id
    // of its own in place of a sequence, which the world keeps a window of
    // (TIDE_COMMAND_WINDOW, the last ids applied) to apply each exactly once:
    // command_sequence gives the highest applied, and command_applied
    // whether an id was, or is too old to tell. NULL where the game has no
    // unordered command.
    bool (*check_command)(const uint8_t *data, uint32_t size, uint32_t *channel);
    uint32_t (*command_sequence)(const void *world, tide_player_id player, uint32_t channel);
    bool (*command)(void *world, tide_player_id sender, uint32_t channel, uint32_t sequence, const uint8_t *data,
                    uint32_t size);
    bool (*command_applied)(const void *world, tide_player_id player, uint32_t id);
    // How many channels its commands are in, the default counted: 0 for 1
    uint32_t command_channels;
    // Whether the match is over: its last scene unloaded, and Main is local,
    // so it can't come back. The server ends it then. NULL: it never is.
    bool (*ended)(const void *world);
    // Starting a match calls no C, and its start holds no text or lists: a
    // machine joining a match can start it too, and be sent only what the
    // match's world has that the one it started lacks.
    bool pure_start;
    // The world has a seed, Time.seed: the game reads it, or has generators
    // (Random) the engine seeds from it. A server picks one for each match it
    // starts, from its desc's `entropy` or else its `seed`, and tells the
    // players who start the match themselves (pure_start). A game without one
    // starts the same whatever the seed, and nothing is picked or sent for it.
    bool seeded;
    // The game's settings (`settings { ... }`), 0 or NULL where it sets none
    uint32_t tick_rate;  // tickRate: what servers run its matches at, unless their desc says
    const char *title;   // title: the window's, unless the host says
    const char *app_id;  // appId: stable identity for this game's local saves
    bool host_migration; // hostMigration: when a room's server goes, another machine takes the match over
    uint8_t prediction;  // prediction: how its clients keep up (tide_prediction)
    bool stats;          // stats: the host shows its stats overlay, though nothing else asked it to
    bool debug_views;    // debugViews: the host shows its debug views over the game when asked (tide/host.h)
    // maxPlayers: how many players its matches take, up to TIDE_PLAYERS_MOST;
    // 0 for TIDE_PLAYERS_DEFAULT. The world keeps an input for each, and the
    // server's after them, so it's part of the game's layout: its hash differs.
    uint32_t max_players;
} tide_game;

// A command of up to TIDE_COMMAND_BYTES goes in one datagram, as commands
// always did. A bigger one, up to TIDE_COMMAND_LIMIT (a command with a
// List<byte> field, a file's bytes), goes as a numbered transfer in chunks,
// the way worlds go, paced under the input traffic, and lands in a later
// tick: the server sends its bytes to every player first, and puts it in a
// tick once they all have them, so no tick waits for a transfer.
#define TIDE_COMMAND_BYTES 1024u
#define TIDE_COMMAND_LIMIT (4u << 20)
#define TIDE_COMMAND_PENDING 32u // Pending per player, in each channel
#define TIDE_COMMANDS_PER_TICK 16u
#define TIDE_COMMAND_CHANNELS_MOST 64u // Channels a game's commands can be in, the default counted
#define TIDE_COMMAND_UNORDERED UINT32_MAX // The channel of a command with no order
#define TIDE_COMMAND_WINDOW 64u // Unordered ids a world remembers applying, per player

// How many players a game's matches take. The server's input is the slot after
// the last player's.
static inline uint32_t tide_game_players(const tide_game *g)
{
    return g->max_players ? g->max_players : TIDE_PLAYERS_DEFAULT;
}

// How many channels a game's commands are in: the default, and its own.
static inline uint32_t tide_game_command_channels(const tide_game *g)
{
    return g->command_channels ? g->command_channels : 1u;
}

typedef enum tide_session_state {
    TIDE_SESSION_OFFLINE,    // In no match
    TIDE_SESSION_CONNECTING, // Waiting for the server, then for its world
    TIDE_SESSION_CONNECTED,  // Playing
} tide_session_state;

// Why a machine left a match. Tide's DisconnectReason has the same values.
typedef enum tide_disconnect_reason {
    TIDE_DISCONNECT_LEFT,        // This machine left
    TIDE_DISCONNECT_TIMED_OUT,   // The server stopped answering, or never did
    TIDE_DISCONNECT_REFUSED,     // The server turned it away: another build of the game, no room left, or not open
    TIDE_DISCONNECT_SERVER_LEFT, // The server's machine left, which ended the match
    TIDE_DISCONNECT_FAILED,      // It couldn't start: no network, a port in use, an address that isn't one, a room nobody has
    TIDE_DISCONNECT_ENDED,       // The match ended: its last scene unloaded (see tide_game.ended)
    TIDE_DISCONNECT_KICKED,      // The server sent it away (tide_server_kick), with a message
} tide_disconnect_reason;

// A kick's message, with its NUL: up to 255 bytes of UTF-8, cut where a
// character starts.
#define TIDE_MESSAGE_BYTES 256u

// This machine's input for `tick`, into `input` (the game's input_size bytes).
// Clients call it once for each tick they run ahead, in order.
typedef void (*tide_sample_fn)(void *user, uint32_t tick, void *input);

// A presentation correction, at the tick and fraction last drawn. The worlds
// are valid only during the callback. A reset discards retained visual errors;
// its worlds can be NULL when there is no comparable presentation history.
typedef struct tide_view_correction {
    const void *before;
    const void *before_previous;
    const void *after;
    const void *after_previous;
    uint32_t tick;
    float alpha;
    bool reset;
} tide_view_correction;

typedef void (*tide_view_correct_fn)(void *user, const tide_view_correction *correction);

// ---------------------------------------------------------------------------
// Server

typedef struct tide_server tide_server;

typedef struct tide_server_desc {
    const tide_game *game;
    uint32_t tick_rate;          // Ticks per second; 0 for the game's (tide_game.tick_rate), or else 60
    float dt;                    // Time.dt; 1 / tick_rate when 0
    const void *start;           // The game's tide_start, or NULL: Main
    tide_transport transports[2]; // Where players connect from; the ones with no `send` are unused
    bool local_first;            // Players on transports[0] are on this machine: their input is the server's too
    bool wait_for_first;         // Tick once the first player has joined, before anything happens without them
    // Hot reloading and host migration: the match goes on from this world (the
    // game's tide_world) instead of starting from `start`, with `players` in it
    // already (a tide_player_set). A player joining into one of their slots gets
    // no PlayerJoined; those who haven't come back in 20 seconds get
    // PlayerLeft. `leaving` went with the last server: PlayerLeft at the first
    // tick.
    const void *world;
    tide_player_set players;
    tide_player_set leaving;
    // Host migration: the digests of the players' cookies (as many as the game
    // takes players, 0 for none), which they come back with. NULL: none.
    const uint64_t *digests;
    bool closed;                 // Takes no one new from the start, only players coming back
    const tide_jobs *jobs;       // Threads to run ticks on (tide/jobs.h), or NULL: this one
    // 64 bits nobody could guess, from the system, for the seed of the match
    // it starts (tide_game.seeded): no two matches go the same. NULL: the seed
    // is `seed`, and a run repeats exactly (tests and benchmarks). A match
    // that goes on from `world` keeps the seed it has.
    uint64_t (*entropy)(void);
    // With no entropy, the seed of the match it starts (Time.seed): its low 31
    // bits. 0 unless the host picked one.
    uint32_t seed;
} tide_server_desc;

tide_server *tide_server_create(const tide_server_desc *desc, double now);
// Tells every player the match is over, and closes the transports.
void tide_server_destroy(tide_server *s);
// Reads what arrived, runs the ticks that are due, and sends each player what's new.
void tide_server_update(tide_server *s, double now);
const void *tide_server_world(const tide_server *s);
uint32_t tide_server_tick(const tide_server *s); // Ticks run so far
uint32_t tide_server_player_count(const tide_server *s);
// Sends `player` away: they go offline with TIDE_DISCONNECT_KICKED and
// `message` (NULL for none), and PlayerLeft follows at the end of the next
// tick. A goodbye can be lost: one who didn't hear it is told again whenever
// they're in touch, still sending or joining again. It's a kick, not a ban:
// once they know, they can join again while the match takes players. False
// if they aren't in the match, or they're on its machine (local_first).
bool tide_server_kick(tide_server *s, tide_player_id player, const char *message);

// ---------------------------------------------------------------------------
// Client

typedef struct tide_client tide_client;

typedef struct tide_client_desc {
    const tide_game *game;
    tide_transport transport;
    tide_address server;
    tide_sample_fn sample; // NULL for a game without an input
    void *user;
    // Ticks its inputs wait at the server at the least, before their tick
    // runs: 0 on the server's machine, 1 or more over a network. The server
    // says how long they waited, and the client keeps the least of that at
    // this, so they're as early as the network's unevenness needs and no
    // earlier: all of that wait is time later that the others see this player,
    // and time further that it runs ahead of them.
    uint32_t lead;
    // ...or one of this machine's frames, where that's less than `lead` ticks,
    // and a 60th of a second where its frames are shorter: what a player over
    // a network wants in a match of fewer ticks a second than frames.
    bool lead_frame;
    uint64_t cookie;       // From an earlier connection to this server, to be the same player again; 0 for none
    bool knew_kick;        // ...which kicked it, as it heard: the server lets it in again (see tide_server_kick)
    const tide_jobs *jobs; // Threads to run ticks on (tide/jobs.h), or NULL: this one
    // A world like the match's, such as the last one it had of a match that
    // changed hands (the game's tide_world, copied): the server only sends what
    // it lacks. NULL: the whole world comes. It's a world this machine had, so
    // its entities aren't new to it: with `events`, only the entities the
    // match's world has beyond it are heard as spawned when that world comes.
    const void *base;
    // The real time, in nanoseconds, to measure what the game's ticks cost:
    // a client predicting (TIDE_PREDICTION_ROLLBACK) keeps running them again
    // within `resim_budget` seconds a frame by delaying its input. NULL: no
    // clock, so no delay but what the game's prediction setting asks, and a
    // run is the same however fast the machine (benchmarks and tests).
    uint64_t (*now_ns)(void);
    double resim_budget; // Seconds a frame may spend running predicted ticks again; 0 for 0.004
    // How long it guesses that another player's input stays as it was, past
    // the last tick the server confirmed: 0 for 0.15 seconds. Further ahead
    // than that, it predicts with the others having let go
    // (tide_game.let_go_inputs), so a guess that was wrong moves things by
    // what a player does in that long at most, however far ahead a slow
    // connection has it run. To the nearest tick; a second or more is as far
    // as a client predicts anyway, so then nothing is let go.
    double guess_seconds;
    tide_view_correct_fn correct_views; // Optional: coalesced replay corrections, using `user`
    // Where what the match sends goes for this machine's local handlers
    // (tide/events.h), once its host takes it from there (`taken`): each event
    // once, however often its tick ran again, and again, as verified, once the
    // server confirmed the tick. The host hands it to the game's local state
    // before each frame (tide_local_events). NULL: nobody listens. The
    // entities a world it's sent has that this machine didn't have are heard
    // as spawned when the world comes (tide_game.hear_arrived): all of its
    // first world's, but for `base`'s, and of one sent again, those its own
    // world at that tick lacked, and for the handlers that hear confirmed
    // ticks, those its last confirmed world lacked.
    tide_event_log *events;
} tide_client_desc;

typedef struct tide_client_status {
    tide_session_state state;
    tide_disconnect_reason reason; // Once it's offline again
    tide_player_id player;         // This machine's player, once connected
    // The network's round trip to the server: from when this machine sent to
    // when the answer arrived, less what the server held it. No wait for an
    // update is in it, on either machine (tide_transport.clock)...
    uint32_t ping_ms;
    // ...and what an answer takes to be read, those waits in it: from the
    // update that sent to the update that reads it. What the client waits by.
    uint32_t answer_ms;
    uint32_t verified_tick;        // Ticks the server has confirmed
    uint32_t predicted_tick;       // Ticks run, predicted ones included
    uint32_t server_tick;          // The server's tick as it last said: the ticks before it are the ones it has run
    uint32_t delay_ticks;          // Ticks this machine's input is delayed by (tide_prediction)
    // What the delay goes by, in milliseconds: what running its predicted
    // ticks again would cost now, were a guess wrong, the most one such
    // rollback may take a frame, and what rollbacks have cost an update of
    // late, on the whole. 0 where it doesn't predict, or has no clock.
    float rollback_ms;
    float rollback_most_ms;
    float resim_ms;
    float early_ms;                // How long this machine's inputs waited at the server before their ticks ran, as it last said: under 0, too late
    uint32_t late_ticks;           // Ticks the server ran without an input this machine sampled for them, which came too late, so far
    uint32_t again_ticks;          // Predicted ticks it ran again so far, after one went otherwise than it guessed
    uint32_t run_ticks;            // Ticks it ran so far, all of them: as the server sent them, predicted, and again
    uint32_t ahead_ticks;          // ...and the predicted ones among them, again_ticks included
    uint32_t resyncs;              // Times its world diverged and the server sent it again
    uint64_t world_bytes;          // Bytes the server sent of worlds so far: whole, as deltas, and the hashes of their pages
    uint64_t cookie;               // What makes this player this player again, on the next connection
    char message[TIDE_MESSAGE_BYTES]; // With TIDE_DISCONNECT_KICKED: the server's message
} tide_client_status;

// What views draw: the latest tick's world, the one before it, and how far
// this moment is between them (0 to 1), to blend them. Views draw a tick late
// that way, but smoothly at any tick rate. `previous` is NULL right after the
// world arrives. `verified` and `verified_previous` bracket a separate confirmed
// presentation clock, which holds at the latest confirmation without extrapolating.
// `verified_alpha` blends that interval. An entity
// in neither it nor `current` is gone for good, where one only `current` lost
// may be back once the server says how the tick went, so what this machine
// keeps for the match's entities (their local fields) goes by both.
typedef struct tide_view_worlds {
    const void *current; // NULL outside a match
    const void *previous;
    float alpha;
    const void *verified; // NULL outside a match
    const void *verified_previous;
    float verified_alpha;
    uint32_t verified_tick; // Upper endpoint of the confirmed presentation interval.
} tide_view_worlds;

tide_client *tide_client_create(const tide_client_desc *desc, double now);
// Tells the server it's leaving, and closes the transport.
void tide_client_destroy(tide_client *c);
void tide_client_update(tide_client *c, double now);
// Copies a command to resend until its sequence is in a verified world (or,
// unordered, its id was applied). Its channel is its type's, which the bytes
// say (tide_game.check_command). False outside a connected match, for invalid
// bytes, a full queue (TIDE_COMMAND_PENDING in its channel), or when its
// 32-bit sequence is exhausted. After a layout change a remote client waits
// for a fresh authoritative world. Commands never predict by repeating. One
// bigger than TIDE_COMMAND_BYTES goes in chunks (see TIDE_COMMAND_LIMIT).
bool tide_client_send_command(tide_client *c, const uint8_t *data, uint32_t size);
tide_client_status tide_client_status_of(const tide_client *c);
// What views read: the world as predicted, up to predicted_tick. NULL until connected.
const void *tide_client_world(const tide_client *c);
// The world up to verified_tick, which the server confirmed. NULL until connected.
const void *tide_client_verified_world(const tide_client *c);
// What views draw, as of the client's last update.
tide_view_worlds tide_client_view(const tide_client *c);
// After drawing tide_client_view, record its timestamp. This alone copies no
// worlds: old endpoints are kept only if a later replay replaces them.
void tide_client_mark_presented(tide_client *c);

// ---------------------------------------------------------------------------
// Session: what a host program uses. Start runs a server with this machine's
// player on it, which Open lets other machines join; Join is a client of
// another machine's server.

typedef struct tide_session tide_session;

typedef struct tide_session_desc {
    const tide_game *game;
    uint32_t tick_rate; // The matches it starts: 0 for the game's at the time (tide_game.tick_rate), or else 60
    tide_sample_fn sample;
    void *user;
    const tide_jobs *jobs;    // Threads to run ticks on (tide/jobs.h), or NULL: this one
    uint64_t (*now_ns)(void); // The real time, for its clients to keep within their budget (tide_client_desc), or NULL
    double resim_budget;      // Seconds a frame a client may spend running predicted ticks again; 0 for 0.004
    double guess_seconds;     // How long its clients guess that others' inputs stay as they were; 0 for 0.15 (tide_client_desc)
    uint64_t (*entropy)(void); // For the seeds of the matches it starts (tide_server_desc), or NULL: they're `seed`
    uint32_t seed;             // With no entropy, the seed of every match it starts: 0 unless the host picked one
    tide_view_correct_fn correct_views; // Optional: presentation corrections, using `user`
} tide_session_desc;

typedef enum tide_session_event_kind {
    TIDE_SESSION_CONNECTED_EVENT,
    TIDE_SESSION_DISCONNECTED_EVENT,
} tide_session_event_kind;

typedef struct tide_session_event {
    tide_session_event_kind kind;
    tide_disconnect_reason reason;    // Disconnected
    char message[TIDE_MESSAGE_BYTES]; // ...and a kick's message
} tide_session_event;

typedef struct tide_session_status {
    tide_client_status client;
    bool server; // This machine runs the server
    bool open;   // ...and other machines can join it (tide_session_open)
    // Seconds the match this machine runs skipped: updates further apart than
    // the ticks its server runs in one, where the rest never happens.
    double skipped;
    // Bytes this machine sent and received over networks since the session was
    // made, in its datagrams (what lower layers add isn't counted). Its own
    // match, over loopback, isn't a network.
    uint64_t sent_bytes;
    uint64_t received_bytes;
} tide_session_status;

tide_session *tide_session_create(const tide_session_desc *desc);
void tide_session_destroy(tide_session *s);
// A match on this machine, which runs its server; closed until it's opened.
// Leaves the one it's in first.
void tide_session_start(tide_session *s, const void *start, double now);
// The same, going on from `world` (the game's tide_world), for hot reloading
// where a new build is a new program, as on the web. `players` are in it
// already: this machine's player is the first of them, with no PlayerJoined.
void tide_session_start_from(tide_session *s, const void *world, tide_player_set players, double now);
// Lets other machines join the match this machine runs, through `network`
// (the session closes it as the match ends). A match opened before opens
// again on the network it had, so pass a zeroed transport first: false if it
// had none, and also if this machine runs no match (closing `network`).
bool tide_session_open(tide_session *s, tide_transport network);
// No one else joins the match from now on; the players in it stay.
void tide_session_close(tide_session *s);
// Sends a player on another machine out of the match this machine runs (see
// tide_server_kick). Nothing on a client, or for this machine's own player.
void tide_session_kick(tide_session *s, tide_player_id player, const char *message);
// The same for every player on another machine.
void tide_session_kick_all(tide_session *s, const char *message);
// Joining the server it joined last, it's the same player again, if the server
// still has room for them. While migrating, it's the match's next server.
void tide_session_join(tide_session *s, tide_transport network, tide_address server, double now);
// Ends the match this machine runs, for everyone: every player goes offline
// with TIDE_DISCONNECT_ENDED, this machine's too, and no other machine takes
// it over: the server's transports hear it ended (tide_transport.end), which
// a room tells its relay, for players who missed the goodbye. Nothing on a
// client.
void tide_session_end(tide_session *s);

// Host migration (tide_game.host_migration), on the machine that runs the
// match: the room its players can meet in again when it goes, by its code and
// key ("" for none), which the server tells them. Once a frame will do.
void tide_session_set_room(tide_session *s, const char *code, const char *key);
// Whether this machine's match lost its server and waits to change hands: the
// room's `code` and `key` are where to meet. Until this machine takes it over
// (tide_session_take_over) or joins the one that did (tide_session_join),
// local code sees it connecting and views see the last world it had. Hosts
// fail it (tide_session_fail) if the room can't be reached.
bool tide_session_migrating(const tide_session *s, char code[TIDE_ROOM_CODE_LENGTH + 1],
                            char key[TIDE_ROOM_KEY_LENGTH + 1]);
// This machine takes the match over: it runs the server from the last tick it
// verified, at the match's tick rate, taking players on `network` (the room).
// The others join it again as the same players; the last server's player
// leaves. Local code sees no Connected or Disconnected, only that this
// machine runs the server now.
void tide_session_take_over(tide_session *s, tide_transport network, double now);
void tide_session_leave(tide_session *s);
// A session that couldn't start, or whose network gave out: leaves the match,
// if it's in one, and reports Disconnected with the reason.
void tide_session_fail(tide_session *s, tide_disconnect_reason reason);
// Once per frame, with the host's time in seconds. When this machine runs the
// server, a long gap since the last update is a pause (a breakpoint, a
// browser that froze the page): the match goes on from where it stopped.
void tide_session_update(tide_session *s, double now);
bool tide_session_send_command(tide_session *s, const uint8_t *data, uint32_t size);
// Seconds from the last update until the match is due its next tick: when it
// next needs an update, for a host with nothing else to do meanwhile, like one
// whose window nobody sees (tide_platform_next_frame). Outside a match, or
// while one starts, a tick at the rate it runs or would start at.
double tide_session_until_tick(const tide_session *s);
// What views read, or NULL outside a match.
const void *tide_session_world(const tide_session *s);
// What views draw, blended between ticks (see tide_view_worlds).
tide_view_worlds tide_session_view(const tide_session *s);
// What the match sent since the host last took it, for the game's local
// handlers (tide/events.h): the host hands it to the game's local state before
// each frame (tide_local_events, tide_host_game.events), which empties it.
// It's the same log for every match the session is in, empty outside one. A
// session records nothing until this is first called, and nothing more for a
// host that stopped taking what's in it.
tide_event_log *tide_session_events(tide_session *s);
void tide_session_mark_presented(tide_session *s);
// The server's world, on the machine that runs it; NULL elsewhere.
const void *tide_session_server_world(const tide_session *s);
tide_session_status tide_session_status_of(const tide_session *s);
// The next thing that happened since the last call, oldest first.
bool tide_session_next_event(tide_session *s, tide_session_event *event);
// Runs another build of the same game from now on: new code with the same
// data layout (the same hash), for hot reloading. The match goes on.
void tide_session_set_game(tide_session *s, const tide_game *game);
// Hot reloading can add or remove correction attributes without changing the
// simulation layout. Refresh the optional presentation callback with its table.
void tide_session_set_view_corrector(tide_session *s, tide_view_correct_fn correct_views);

// Makes `to`, a zeroed world of the new build, from `from`, a world of the old.
typedef bool (*tide_migrate_fn)(void *user, const void *from, void *to);

// The same, for a build with another data layout: `migrate` carries the
// match's world over to it, on the machine that runs the server, and the last
// world the server confirmed on a client of another machine. This machine's
// own player takes its server's world. Players stay where they are. A client's
// ticks ahead of the server run again, and a world that came out different
// from the server's is sent again, as when a client goes wrong. False,
// changing nothing, if `migrate` fails or there isn't the memory.
bool tide_session_migrate(tide_session *s, const tide_game *game, tide_migrate_fn migrate, void *user);

// What local code asked for, with Session.Start, Join, Connect, Leave, Open,
// Close, Kick, KickAll and End, and Clipboard.Copy, which isn't the session's
// but goes to the host the same way.
typedef enum tide_session_request_kind {
    TIDE_REQUEST_NONE,
    TIDE_REQUEST_START,
    TIDE_REQUEST_JOIN,    // A room, by its code
    TIDE_REQUEST_CONNECT, // A machine, by its address
    TIDE_REQUEST_LEAVE,
    TIDE_REQUEST_OPEN,    // Other machines can join the match this machine runs
    TIDE_REQUEST_CLOSE,   // ...and no longer
    TIDE_REQUEST_KICK,    // A player, with a message
    TIDE_REQUEST_KICK_ALL,
    TIDE_REQUEST_END,     // The match this machine runs ends, for everyone
    TIDE_REQUEST_COPY,    // Text onto this machine's clipboard
} tide_session_request_kind;

#define TIDE_DEFAULT_PORT 7777u

typedef struct tide_session_request {
    uint32_t kind;
    uint32_t port;          // Open: the port to take players on. Connect: the server's, unless `text` has one
    tide_player_id player;  // Kick: whom
    char text[TIDE_MESSAGE_BYTES]; // Join: the room's code. Connect: the server's address, "host" or "host:port".
                                   // Kick and KickAll: the message. Copy: the text
} tide_session_request;
