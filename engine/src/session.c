#include "tide/session.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/page.h"
#include "tide/sha256.h"
#include "tide/pack.h"
#include "tide/shard.h"

// Every datagram starts with a header: "PU", the protocol version and a type.
//
//   HELLO    client: the game's hash, a number for this attempt, its time,
//            its cookie from before, if any, whether it knows the server
//            kicked it last time, and whether it has a world to build the
//            match's from
//   WELCOME  server: the attempt's number, the player, the tick rate, its
//            tick, what's being sent: its tick, number, kind, size and number
//            of chunks, and the player's cookie
//   REFUSE   server: why
//   CHUNK    server: a piece of what's being sent, by its number
//   CLIENT   client: times, whether it needs the world (and has one to build
//            it from), the chunks it has of what's being sent, the ticks it
//            has, what its world lacks of the one whose hashes it was sent,
//            and its inputs from the first the server lacks
//   COMMAND  client: its connection attempt, command sequence and copied bytes
//   UPLOAD   client: a chunk of a command too big for a datagram: its
//            attempt, the upload's number, the command's sequence and size,
//            the chunk's index and bytes
//   DOWNLOAD server: a chunk of such a command, on its way to every player
//            before it goes in a tick: its number, size, the chunk's index
//            and bytes
//   SERVER   server: times, its tick, the newest input it has of the player's
//            (from its tick), how long their inputs waited for the ticks it
//            last ran, what it has of the player's upload (in a game with
//            commands), and pieces of the ticks the player lacks, each by how
//            many ticks back it is
//
// Times are milliseconds, kept to 16 bits: they only measure round trips and
// order a side's datagrams, which are never seconds apart on a connection that
// hasn't timed out. Numbers that are mostly small go as varints.
//   BYE      either: leaving; the server's says whether the match ended, or
//            goes on with another machine as its server
//   HANDOVER server, with host migration: its room's code and key, its own
//            player, whether it's open, the players in the match and their
//            cookies' digests, numbered so a client keeps the newest, and in
//            parts when a match has more players than one datagram holds
//
// Nothing is sent reliably as such: each side says what it has, and the other
// sends what's missing again until it does.
//
// A tick, as the server sends it (a "frame"): the hash of the world after it,
// which of the rest it has (FRAME_*), the players who joined or left before
// it, and the inputs that changed for it: a bit per slot (players, then the
// server: a set of them, as a varint of as many bits as it takes, so a match
// of a few players takes a byte) and each input in slot order, as what differs from the slot's input
// as the world keeps it before the tick (tide_game.write_input_delta). A
// CLIENT's inputs go the same way, each but the first from the one before it.
// A slot without one keeps its last input, on every machine, whether it didn't
// change or didn't arrive in time; a second set of bits says which ones were
// late, so a client knows whether its prediction held.
// Commands follow in the server's selected order, each with its authenticated
// sender, sequence and bytes, or, for one too big for a datagram, the number
// of the download every player was sent it as. They are never guessed by
// repeating an input.
//
// A command's channel is its type's (tide_game.check_command): the default,
// one of the game's own, or none (TIDE_COMMAND_UNORDERED). Each player's
// commands are in order within a channel, by a sequence of their own, and
// in no order against another channel's: the server holds one back only for
// an earlier command of its channel. An unordered command goes in whichever
// tick it arrives, once: the world keeps a window of the ids it applied.
//
// A big command (over TIDE_COMMAND_BYTES) goes up in chunks (UPLOAD), which
// the server acknowledges in its SERVER datagrams, as a CLIENT does a world's.
// Whole, it waits in the player's queue like any other. When its turn comes,
// the server first sends its bytes down to every player in the match
// (DOWNLOAD, acknowledged in CLIENTs), paced under the frames, and puts it in
// a tick once they all have them: the tick's frame names the download, so a
// tick is never held up by a transfer, and a player who joins later with a
// world from before that tick is sent the download with the ticks it lacks.
//
// The world goes as a delta (tide/delta.h), in chunks. A player with no world
// gets all of it. One with a world to build it from (its own, gone wrong, or
// the last one it had of a match that changed hands) first gets the hashes of
// the world's pages, says which of them its world lacks, and gets those.

#define MAGIC 0x5449u // "TI"
#define PROTOCOL 18u

enum { MSG_HELLO = 1, MSG_WELCOME, MSG_REFUSE, MSG_CHUNK, MSG_CLIENT, MSG_SERVER, MSG_BYE, MSG_HANDOVER, MSG_COMMAND,
       MSG_UPLOAD, MSG_DOWNLOAD, MSG_LOCAL_INTEREST, MSG_LOCAL_DATA };
// What a SERVER says of the player's upload: nothing, the chunks it has of
// one on its way, or that one is whole (and the number of its download)
enum { UPLOAD_NONE = 0, UPLOAD_PART = 1, UPLOAD_WHOLE = 2 };
enum { REFUSE_OTHER_GAME = 1, REFUSE_FULL = 2, REFUSE_CLOSED = 3 };
enum { EVENT_JOIN = 1, EVENT_LEAVE = 2 };
// KICKED: then the message's length and bytes. HANDOVER: the server left, and
// the match goes on with another machine as its server.
enum { BYE_LEFT = 0, BYE_ENDED = 1, BYE_KICKED = 2, BYE_HANDOVER = 3 };
enum {
    HELLO_KNEW_KICK = 1, // It heard it was kicked from this server: the server takes it again
    HELLO_HAS_WORLD = 2, // It has a world to build the match's from
    HELLO_CAN_START = 4, // ...or can start the match itself to have one (tide_game.pure_start)
};
enum {
    CLIENT_WANTS_WORLD = 1, // It has none, or its own went wrong, or it's taking the one coming
    CLIENT_HAS_WORLD = 2,   // ...and can build it from what it has
    CLIENT_LACKS = 4,       // What its world lacks of the one whose hashes it was sent follows
    CLIENT_CAN_START = 8,   // ...or from a match it starts itself
    CLIENT_ASSETS = 16,     // Its exact current asset-pack digest follows the flags.
    CLIENT_REPAIR = 32,     // An exact historical projected tick needs correction.
    CLIENT_REPAIR_STREAM = 64, // Continuation of a bounded coalesced repair stream.
};
// What a frame has after its hash, most often nothing: the players who joined
// or left, a bit per slot whose input changed, and one per slot whose was late.
enum { FRAME_EVENTS = 1, FRAME_INPUTS = 2, FRAME_LATE = 4, FRAME_COMMANDS = 8, FRAME_DESTROYED = 16,
       FRAME_REPAIR = 32, FRAME_LOCAL_EVENTS = 64, FRAME_SHARD = 128 };
// What's being sent: the world, as a delta, or the hashes of its pages, to
// learn which of them the player's world lacks. The hashes come after how the
// match started: one of START_*, Time.dt's bits, the match's seed in a game
// that has one (tide_game.seeded) and, for START_GIVEN, the game's tide_start.
enum { SEND_WORLD = 0, SEND_HASHES = 1, SEND_CLIENT_WORLD = 2, SEND_CLIENT_REPAIR = 3 };
#define CLIENT_REGION_BYTES 16384u
enum { START_UNKNOWN = 0, START_MAIN = 1, START_GIVEN = 2 };
#define LACKS_BYTES 512u // The most a CLIENT says about what its world lacks: past that, it lacks the rest

// The server's input is the slot after the players': how many the game takes
// (tide_game_players), which servers and clients keep as `players`.
#define PREDICTION_SECONDS 1.0 // How far a client runs ahead of the last tick it knows, at most
#define RESIM_BUDGET 0.004     // Seconds an update a client may spend running predicted ticks again, on the whole, unless its desc says
#define ROLLBACK_FRAMES 1.0    // Frames (the time between its updates) one rollback may take, and never less than the budget
#define LOAD_SECONDS 1.0       // Over how long what running ticks again costs an update is averaged
#define LEAD_SECONDS 0.5       // How long the most a client was ahead of what's confirmed counts for what a rollback would run again
#define ROOM_SHARE 0.9         // The share of what it may take that a rollback, and the whole, must stay within for the delay to come back
#define ROOM_SECONDS 0.5       // How long a predicting client has to have had that room before it takes a tick of input delay back
#define COST_STALL 4.0         // A tick that took more times its mean than this was the machine stalling: it counts as that many ticks
#define LEVEL_TICKS 2u         // Ticks a client may be behind what the server said it ran and still be level with it, for the lead to count
#define LEAD_SETTLED 2.0       // Ticks its lead may be from what it's been of late and count as settled, for a tick of delay more
#define ROLLBACK_SECONDS (1.0 / 60.0) // The least one rollback may take, however short the frames: a 60th of a second
#define QUIET_SECONDS 2.0      // With no rollback for this long, nothing says the delay is still needed: it comes back a tick at a time
#define COLD_SECONDS 1.0       // A rollback in a client's first second with a world isn't measured: everything is cold, and it's catching up
#define REPLAY_SAMPLES 5       // Rollbacks' first ticks whose median is what one costs
#define GUESS_SECONDS 0.15     // How long a client guesses that another player's input stays as it was, unless its desc says
#define SLACK_SECONDS 5.0      // How long a client that never predicts has to have had room to spare before it takes input delay back
#define SLACK_TICKS 3          // Ticks a frame has to come before its tick is due to count as room to spare
#define HISTORY_SECONDS 4.0    // How long the server keeps ticks to send again, and a client keeps ticks ahead
#define HISTORY_MOST 8u        // ...times this at most, for a player catching up with a world it was sent; past that, the world again
#define FIRST_RING 8u          // Snapshots a client starts with; more as it runs further ahead
#define PIECE 1000u                  // Bytes of a tick per piece; piece indices are varints
#define CHUNK 1000u                  // Bytes of a packed world per chunk
#define CHUNKS_PER_UPDATE 64u
// Chunks of a big command an update sends, up and down: 16 KB, under the
// inputs and frames, so about a megabyte a second at 60 updates a second
#define COMMAND_CHUNKS_PER_UPDATE 16u
#define PUBLISHED_MOST 64u // Big commands a server keeps, dispatched or on their way to the players
#define DOWNLOADS_MOST 64u // ...and a client keeps, whole or coming, for the ticks that name them: as many
#define PACKETS_PER_UPDATE 8u
#define EVENTS_PER_PLAYER 4u    // Joins and leaves kept before a tick, for each player the game takes
#define PLAYER_SET_BYTES ((TIDE_PLAYER_SET_BITS + 6u) / 7u) // Seven bits per wire byte
#define HANDOVER_PART 1000u     // Bytes of a handover record per datagram
#define SENT_TIMES 256u         // Times a client sent that it remembers, to measure its ping when one comes back
#define TIMEOUT 5.0     // Seconds of silence before giving up on the other side
#define HELLO_TIMEOUT 2.0 // ...or on one that said hello and never wrote again: it may not be where it said
#define ROOM_TIMEOUT 15.0 // ...or on a room's host before it first answers: WebRTC can take a while to connect,
                          // as can a game's own network (tide/network.h)
#define HELLO_EVERY 0.2 // Seconds between HELLOs until the server answers
#define WAITS 960.0     // The parts of a second a server says an input's wait in: a 16th of a 60th
#define WAIT_MOST 30000 // The longest wait it says, either way...
#define WAIT_UNKNOWN (-WAIT_MOST - 1) // ...and what it says while it has none of the player's inputs to go by
#define EARLY_FRAME (1.0 / 60.0) // Seconds a client keeps its inputs early at the least, however short its frames
#define EARLY_LOOK 2.0  // Seconds a client watches how early its inputs arrive before it sends them later
#define EARLY_KEEP 10.0 // ...and after one was nearly late: what made it so may come again
#define EARLY_START 1.0 // ...but in its first second in a match: a machine starting up stutters, and that doesn't come again
#define CLOCK_SLOW 0.1  // The share of the time that passes a clock loses, while it has some to lose
#define MAX_TICKS 8u    // Ticks a server runs in one update at most: after a stall it drops the time instead
#define CATCH_UP_TICKS 8u // Normal frame allowance for server ticks while a client is behind (catch_up_room)
#define PRESENT_SECONDS 20.0  // How long players of a world a server went on from have to come back
#define HANDOVER_EVERY 1.0    // Seconds between HANDOVERs, once it's been the same that long

// How far each side looks, in ticks at the match's tick rate.
typedef struct windows {
    uint32_t prediction; // Ticks a client runs ahead of the last one it knows at most
    uint32_t inputs;     // Inputs kept by tick: the prediction, and a little more
    uint32_t history;    // Ticks the server keeps to send again, and a client keeps ahead
} windows;

static windows windows_for(const uint32_t rate)
{
    const uint32_t prediction = (uint32_t)(PREDICTION_SECONDS * rate) > 4u ? (uint32_t)(PREDICTION_SECONDS * rate) : 4u;
    const uint32_t history = (uint32_t)(HISTORY_SECONDS * rate) > 64u ? (uint32_t)(HISTORY_SECONDS * rate) : 64u;
    return (windows){prediction, prediction + 8u, history};
}

// How many ticks a client goes on guessing another player's input by their
// last one (tide_client_desc.guess_seconds), at the match's tick rate.
static uint32_t guess_ticks(const double seconds, const uint32_t rate)
{
    const double ticks = (seconds > 0.0 ? seconds : GUESS_SECONDS) * (double)rate + 0.5;
    return ticks < 4294967295.0 ? (uint32_t)ticks : UINT32_MAX;
}

// Lets go of what a world has, before its memory goes. NULL does nothing.
static void free_world(const tide_game *g, void *world)
{
    if (world && g->free_world) g->free_world(world);
}

// What players share of a cookie for host migration: the first bytes of its
// SHA-256, which nobody can work the cookie back from. 0 for none.
static uint64_t cookie_digest(const uint64_t cookie)
{
    if (!cookie) return 0;
    uint8_t bytes[8];
    for (int i = 0; i < 8; i++) bytes[i] = (uint8_t)(cookie >> (8 * i));
    uint8_t hash[32];
    tide_sha256_of(bytes, sizeof bytes, hash);
    uint64_t digest = 0;
    for (int i = 0; i < 8; i++) digest |= (uint64_t)hash[i] << (8 * i);
    return digest ? digest : 1u;
}

static uint32_t millis(const double t)
{
    return t > 0.0 ? (uint32_t)(uint64_t)(t * 1000.0) : 0u;
}

// Milliseconds as they're sent, in 16 bits, and the time since one of them.
static uint16_t millis16(const double t)
{
    return (uint16_t)millis(t);
}

static uint32_t millis_since(const double now, const uint16_t then)
{
    return (uint16_t)(millis16(now) - then);
}

// A transport's clock, and when the datagram it last gave arrived by it (see
// tide_transport.clock): what a ping is measured by, so it's the network's
// round trip, with no wait for an update in it. A transport with no clock
// goes by the session's time, `now`: a datagram then arrives as it's read.
static double clock_of(const tide_transport *t, const double now)
{
    return t->clock && t->arrived ? t->clock(t->self) : now;
}

static double arrived_of(const tide_transport *t, const double now)
{
    return t->clock && t->arrived ? t->arrived(t->self) : now;
}

static uint64_t ticks_in(const double seconds, const uint32_t rate)
{
    const double n = seconds * (double)rate + 1e-7;
    return n > 0.0 ? (uint64_t)n : 0u;
}

static void header(tide_writer *w, const uint8_t type)
{
    tide_write_u16(w, MAGIC);
    tide_write_u8(w, PROTOCOL);
    tide_write_u8(w, type);
}

// The datagram's type, or 0 if it isn't ours.
static uint8_t read_header(tide_reader *r)
{
    if (tide_read_u16(r) != MAGIC || tide_read_u8(r) != PROTOCOL) return 0;
    const uint8_t type = tide_read_u8(r);
    return r->failed ? 0 : type;
}

// Bytes a server or client sent and received over networks. Loopback, a
// machine's own match, isn't one.
typedef struct traffic {
    uint64_t sent;
    uint64_t received;
} traffic;

static void send_packet(const tide_transport *t, const tide_address to, const tide_writer *w, traffic *counted)
{
    assert(!w->overflow); // Every message is sized to fit: one that doesn't would be lost without a word
    if (w->overflow || !t->send) return;
    t->send(t->self, to, w->data, w->size);
    if (to.kind != TIDE_ADDRESS_LOOPBACK) counted->sent += w->size;
}

// A signed number as a varint, its sign in the lowest bit: small ones of
// either sign take a byte.
static void write_signed(tide_writer *w, const int32_t v)
{
    tide_write_varint(w, (uint32_t)v << 1 ^ (v < 0 ? UINT32_MAX : 0u));
}

static int32_t read_signed(tide_reader *r)
{
    const uint32_t u = tide_read_varint(r);
    return (int32_t)(u >> 1 ^ (0u - (u & 1u)));
}

static uint32_t varint_size(uint32_t v)
{
    uint32_t n = 1;
    for (; v >= 0x80u; v >>= 7) n++;
    return n;
}

static void patch_u64(uint8_t *at, const uint64_t v)
{
    for (int i = 0; i < 8; i++) at[i] = (uint8_t)(v >> (8 * i));
}

// A world's hash, between ticks: what changed of it since its last one is
// hashed on the session's threads where the game can share that out, which
// frees this one of most of it. The same hash either way.
static uint64_t hash_of(const tide_game *g, const void *world, const tide_jobs *jobs)
{
    return jobs && g->hash_world_on ? g->hash_world_on(world, jobs) : g->hash_world(world);
}

static uint32_t client_event_causes(const tide_event_log *events, tide_entity *out, const uint32_t capacity)
{
    uint32_t count = 0;
    for (uint32_t at = 0; events && at < events->size;) {
        const tide_heard *heard = (const tide_heard *)(events->bytes + at);
        at += heard->size;
        if (heard->event == TIDE_HEARD_DESTROYED_CAUSE) {
            if (out && count < capacity) out[count] = heard->target;
            count++;
        }
    }
    return count;
}

// Canonical permitted bytes use the existing page/run compressor. The base
// is a verified logical tick, never the receiver's unverified prediction.
static uint8_t *pack_client_state(const tide_game *g, const void *world, const void *base,
                                  const void *had, const tide_event_log *events, const tide_player_id player,
                                  const uint32_t base_tick, uint32_t *size)
{
    uint32_t count = 0, previous_count = 0;
    uint8_t *bytes = g->client_state(world, player, &count);
    uint8_t *previous = base ? g->client_state(base, player, &previous_count) : NULL;
    if (!bytes || (base && !previous)) {
        free(bytes);
        free(previous);
        return NULL;
    }
    tide_delta_writer d;
    tide_delta_begin(&d, base != NULL, NULL, 0, tide_hash(bytes, count));
    tide_delta_number(&d, base ? base_tick : UINT32_MAX);
    tide_delta_number(&d, count);
    uint32_t died_count = g->tick_client_events ? client_event_causes(events, NULL, 0)
        : had && g->client_destroyed ? g->client_destroyed(world, had, player, NULL, 0) : 0;
    tide_entity *died = died_count ? malloc((size_t)died_count * sizeof *died) : NULL;
    if (died_count && !died) tide_out_of_memory();
    if (died_count) died_count = g->tick_client_events ? client_event_causes(events, died, died_count)
        : g->client_destroyed(world, had, player, died, died_count);
    tide_delta_number(&d, died_count);
    for (uint32_t i = 0; i < died_count; i++) {
        tide_delta_number(&d, died[i].index);
        tide_delta_number(&d, died[i].generation);
    }
    free(died);
    tide_delta_number(&d, events ? events->size : 0u);
    if (events) tide_write_bytes(&d.bytes, events->bytes, events->size);
    for (uint32_t at = 0; at < count;) {
        const uint32_t n = count - at < CLIENT_REGION_BYTES ? count - at : CLIENT_REGION_BYTES;
        const uint32_t bn = at < previous_count
            ? (previous_count - at < CLIENT_REGION_BYTES ? previous_count - at : CLIENT_REGION_BYTES) : 0u;
        const uint8_t *old = bn ? previous + at : NULL;
        tide_delta_region(&d, bytes + at, n, old, bn, n == bn && old && memcmp(bytes + at, old, n) == 0, 0);
        at += n;
    }
    tide_delta_close(&d);
    free(bytes);
    free(previous);
    return tide_delta_end(&d, size);
}

static bool unpack_client_state(const tide_game *g, void *world, const void *base,
                                const tide_player_id player, const uint32_t base_tick,
                                const uint8_t *data, const uint32_t size, tide_entity **destroyed,
                                uint32_t *destroyed_count, tide_event_log *events)
{
    tide_delta_reader d;
    uint64_t hash;
    if (!tide_delta_open(&d, data, size, base != NULL, &hash)) return false;
    const uint32_t from = tide_delta_get_number(&d);
    const uint32_t count = tide_delta_get_number(&d);
    if (d.bytes.failed || from != (base ? base_tick : UINT32_MAX)) return false;
    const uint32_t died_count = tide_delta_get_number(&d);
    if (d.bytes.failed || died_count > (d.bytes.size - d.bytes.at) / 2u) return false;
    tide_entity *died = died_count ? malloc((size_t)died_count * sizeof *died) : NULL;
    if (died_count && !died) return false;
    for (uint32_t i = 0; i < died_count; i++) {
        died[i].index = tide_delta_get_number(&d);
        died[i].generation = tide_delta_get_number(&d);
    }
    if (d.bytes.failed) { free(died); return false; }
    const uint32_t event_size = tide_delta_get_number(&d);
    const uint8_t *event_bytes = tide_read_bytes(&d.bytes, event_size);
    uint32_t event_build = 0, event_count = 0;
    if (!event_bytes || (event_size && (!g->check_client_events
        || !g->check_client_events(event_bytes, event_size, &event_build, &event_count)))) {
        free(died);
        return false;
    }
    uint32_t previous_count = 0;
    uint8_t *previous = base ? g->client_state(base, player, &previous_count) : NULL;
    uint8_t *bytes = calloc(count ? count : 1u, 1);
    bool ok = bytes && (!base || previous);
    for (uint32_t at = 0; ok && at < count;) {
        const uint32_t n = count - at < CLIENT_REGION_BYTES ? count - at : CLIENT_REGION_BYTES;
        const uint32_t bn = at < previous_count
            ? (previous_count - at < CLIENT_REGION_BYTES ? previous_count - at : CLIENT_REGION_BYTES) : 0u;
        ok = tide_delta_get(&d, bytes + at, n, bn ? previous + at : NULL, bn);
        at += n;
    }
    ok = ok && tide_delta_closed(&d) && d.bytes.at == size && tide_hash(bytes, count) == hash;
    if (ok) ok = g->load_client_state(world, bytes, count) && g->client_hash(world, player) == hash;
    if (ok) {
        *destroyed = died;
        *destroyed_count = died_count;
        if (event_size) {
            events->bytes = malloc(event_size);
            if (!events->bytes) tide_out_of_memory();
            memcpy(events->bytes, event_bytes, event_size);
            events->size = events->capacity = event_size;
            events->build = event_build;
            events->count = event_count;
        }
    }
    else free(died);
    free(previous);
    free(bytes);
    return ok;
}

// The most a frame can take: its hash, the events, which inputs changed and
// were late, and every slot's input, after its size.
static uint32_t frame_capacity(const tide_game *g)
{
    const uint32_t players = tide_game_players(g);
    const uint64_t size = 8u + 1u + 5u + 4ull * EVENTS_PER_PLAYER * players + 2u * ((players + 7u) / 7u)
        + (players + 1ull) * (5ull + g->max_input_bytes)
        + (g->command ? 5u + TIDE_COMMANDS_PER_TICK * (15u + TIDE_COMMAND_BYTES) : 0u)
        + (g->shards ? 5u + TIDE_SHARD_MESSAGES_PER_TICK * (10u + TIDE_COMMAND_BYTES) : 0u);
    return size <= UINT32_MAX / 2u ? (uint32_t)size : 0u;
}

// A shard message handed to the server for a coming tick (tide_server_inject):
// up to TIDE_COMMAND_BYTES goes in the frame, and a bigger one is published
// first, as a big command is, under `published`.
typedef struct injected {
    uint8_t *data;
    uint32_t size;
    uint32_t published;
} injected;

// Whose a published download is when it's a shard message: no player's
#define PUBLISHED_SHARD UINT32_MAX

// A command waiting: at the server, for its turn in a tick; at a client, to
// be sent, until a verified world has it. `sequence` is its channel's, or an
// unordered command's id. One bigger than a datagram goes in chunks:
// `published` is its download's number, once the server gave it one (which
// the client learns when its upload is whole), and `uploaded` whether the
// server has all of it.
typedef struct pending_command {
    uint32_t channel;
    uint32_t sequence;
    uint32_t size;
    uint8_t *data;
    double sent;
    uint32_t published;
    bool uploaded;
} pending_command;

// A player's, in the order they came: TIDE_COMMAND_PENDING in each channel
// at most, the unordered ones counted as a channel of their own.
typedef struct command_queue {
    pending_command *items;
    uint32_t count;
    uint32_t capacity;
} command_queue;

static void remove_command(command_queue *q, const uint32_t index)
{
    free(q->items[index].data);
    memmove(q->items + index, q->items + index + 1u, (size_t)(--q->count - index) * sizeof *q->items);
}

static void clear_commands(command_queue *q)
{
    while (q->count) remove_command(q, q->count - 1u);
}

static void free_commands(command_queue *q)
{
    clear_commands(q);
    free(q->items);
    *q = (command_queue){0};
}

static uint32_t channel_count(const command_queue *q, const uint32_t channel)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < q->count; i++) n += q->items[i].channel == channel ? 1u : 0u;
    return n;
}

static bool big_command(const uint32_t size)
{
    return size > TIDE_COMMAND_BYTES;
}

// Takes `data` (to free()) as the queue's, in its channel, which has room
// for it; else frees it.
static bool append_owned(command_queue *q, const uint32_t channel, const uint32_t sequence, uint8_t *data,
                         const uint32_t size)
{
    if (!data || !size || size > TIDE_COMMAND_LIMIT || channel_count(q, channel) >= TIDE_COMMAND_PENDING) {
        free(data);
        return false;
    }
    if (q->count == q->capacity) {
        const uint32_t capacity = q->capacity ? 2u * q->capacity : 8u;
        pending_command *items = realloc(q->items, (size_t)capacity * sizeof *items);
        if (!items) {
            free(data);
            return false;
        }
        q->items = items;
        q->capacity = capacity;
    }
    q->items[q->count++] = (pending_command){channel, sequence, size, data, 0, 0, false};
    return true;
}

static bool append_command(command_queue *q, const uint32_t channel, const uint32_t sequence, const uint8_t *data,
                           const uint32_t size)
{
    if (!data || !size || size > TIDE_COMMAND_LIMIT) return false;
    uint8_t *copy = malloc(size);
    if (!copy) return false;
    memcpy(copy, data, size);
    return append_owned(q, channel, sequence, copy, size);
}

// Whether a command of `sequence` can still come in `channel` of a world
// where `previous` is the last: unordered ones can at any time, and an
// ordered one's sequence is within a queue's worth after the last.
static bool sequence_pending(const uint32_t channel, const uint32_t sequence, const uint32_t previous)
{
    if (channel == TIDE_COMMAND_UNORDERED) return true;
    return sequence > previous && sequence - previous <= TIDE_COMMAND_PENDING;
}

static bool queued(const command_queue *q, const uint32_t channel, const uint32_t sequence)
{
    for (uint32_t i = 0; i < q->count; i++) {
        if (q->items[i].channel == channel && q->items[i].sequence == sequence) return true;
    }
    return false;
}

// Whether a world applied `sequence` in `channel` for `player`: an ordered
// one's sequence is behind the channel's, and an unordered one's id is in
// the window the world keeps.
static bool command_done(const tide_game *g, const void *world, const tide_player_id player, const uint32_t channel,
                         const uint32_t sequence)
{
    if (channel == TIDE_COMMAND_UNORDERED) return g->command_applied && g->command_applied(world, player, sequence);
    return sequence <= g->command_sequence(world, player, channel);
}

// Chunks of a big command: as a world's, CHUNK bytes each
static uint32_t chunks_of(const uint32_t size)
{
    return size / CHUNK + (size % CHUNK != 0u);
}

// A transfer's bitmap of chunks: which it has from the first (`ack`), and of
// the 64 after those, as the CLIENT and SERVER say them
static void chunks_had(const uint8_t *have, const uint32_t count, uint32_t *ack, uint64_t *mask)
{
    *ack = 0;
    *mask = 0;
    while (*ack < count && have[*ack]) (*ack)++;
    for (uint32_t i = 0; i < 64u && *ack + 1u + i < count; i++) {
        if (have[*ack + 1u + i]) *mask |= (uint64_t)1 << i;
    }
}

// A set of players (and the server's slot after them) as a varint of as many
// bits as it has: 7 a byte, lowest first, each byte saying whether more come.
// So a set of the first 7 takes a byte, as a 32-bit varint does.
static void write_players(tide_writer *w, const tide_player_set *set)
{
    uint32_t top = set->words ? (set->words - 1u) * 64u : 0u;
    if (set->words) {
        uint64_t last = set->bits[set->words - 1u];
        while (last >>= 1u) top++;
    }
    const uint32_t groups = top / 7u + 1u;
    for (uint32_t g = 0; g < groups; g++) {
        uint8_t byte = 0;
        for (uint32_t b = 0; b < 7u; b++) {
            if (tide_players_has(set, g * 7u + b)) byte |= (uint8_t)(1u << b);
        }
        tide_write_u8(w, g + 1u < groups ? (uint8_t)(byte | 0x80u) : byte);
    }
}

// Reading stops after as many bytes as a set takes, whatever the last says,
// so bad input can't run on; bits past the set fall off.
static tide_player_set read_players(tide_reader *r)
{
    tide_player_set set = {0};
    for (uint32_t g = 0; g < PLAYER_SET_BYTES; g++) {
        const uint8_t byte = tide_read_u8(r);
        for (uint32_t b = 0; b < 7u; b++) {
            if (byte >> b & 1u) tide_players_add(&set, g * 7u + b);
        }
        if (!(byte & 0x80u)) break;
    }
    return set;
}

// The most a handover record takes (see send_handover): its server's player,
// whether it's open, the room's code and key, who's in the match and a digest each.
static uint32_t handover_capacity(const uint32_t players)
{
    return 16u + TIDE_ROOM_CODE_LENGTH + TIDE_ROOM_KEY_LENGTH + (players + 7u) / 7u + 8u * players;
}

static bool players_beyond(const tide_player_set *a, const tide_player_set *b)
{
    uint64_t more = 0;
    for (uint32_t w = 0; w < a->words; w++) more |= a->bits[w] & ~b->bits[w];
    return more != 0;
}

static bool players_meet(const tide_player_set *a, const tide_player_set *b)
{
    uint64_t both = 0;
    for (uint32_t w = 0; w < a->words && w < b->words; w++) both |= a->bits[w] & b->bits[w];
    return both != 0;
}

// Room for two inputs and one packed, whole or as a delta (write_input_delta).
static size_t scratch_size(const tide_game *g)
{
    return 2u * (size_t)g->input_size + g->max_input_bytes + 1u;
}

// An input from outside, as every machine will read it: unpacked, packed again
// the one way it packs, and unpacked from that. Returns the packed size, or 0.
static uint32_t canonical_input(const tide_game *g, const uint8_t *data, const uint32_t size, void *input,
                                uint8_t *packed)
{
    if (!g->read_input(data, size, input)) return 0;
    const uint32_t n = g->write_input(input, packed, g->max_input_bytes);
    if (n == 0 || !g->read_input(packed, n, input)) return 0;
    return n;
}

// ---------------------------------------------------------------------------
// Server

typedef struct stored_frame {
    uint32_t tick;
    uint32_t size;
    uint8_t *data;
    void *world; // With visibility: the full authoritative world before this tick.
    tide_event_log events; // Recipient's authoritative lifecycle journal, also retained for chunked repair.
} stored_frame;

typedef struct sent_mark {
    uint32_t tick;
    double at;
    uint32_t next; // Continue a frame larger than the per-update packet allowance.
} sent_mark;

// A tick's whole world, packed to send: every player who joined in that tick
// is sent these bytes, and they go with the last of them.
typedef struct shared_pack {
    uint32_t tick;
    uint8_t *bytes;
    uint32_t size;
    uint32_t refs; // Players being sent it
} shared_pack;

typedef struct connection {
    uint8_t asset_pack[32];
    bool used;
    uint32_t transport;
    tide_address address;
    uint32_t nonce;
    bool local; // On the server's machine: its input is the server's too
    double last_heard;
    uint16_t their_time;
    double their_arrived; // When the datagram that said so arrived, by its transport's clock
    uint32_t rtt_ms;
    // It wrote back after its hello, so it is where it said: only then does it
    // join the match and get the world. A hello alone could come from anyone,
    // saying it's anywhere: answering it with the world would send megabytes
    // wherever it said, and put a player in the match who isn't there.
    bool greeted;
    bool new_player; // Its cookie was made for it: if it never writes back, the cookie goes too
    bool welcoming;  // It said hello and hasn't written back yet: welcomed every update until it does
    double local_until; // A local-data subscription's lease; zero without one
    double local_credit, local_time; // Bounded local-data relay work per sender

    // The world it's being sent
    bool sending;
    bool has_world; // It has a world to build it from: it's sent the hashes first
    bool can_start; // ...or can start the match itself to have one
    uint32_t snapshot_tick;
    uint32_t view_limit; // Latest authoritative tick advertised to this connection.
    uint32_t snapshot_number; // Each thing sent has the next number, from 1
    uint8_t snapshot_kind;    // SEND_WORLD or SEND_HASHES
    bool snapshot_stream;
    uint32_t snapshot_size;
    uint32_t chunk_count;
    uint8_t *snapshot;
    struct shared_pack *shared; // ...is this pack's, a tick's whole world, which other players who joined in that tick are sent too
    void *hashed; // While the hashes are sent: the world they're of, to send once it says what it lacks
    double *chunk_sent;
    uint32_t chunk_ack;  // It has every chunk before this one
    uint64_t chunk_mask; // ...and these after it

    // Ticks
    uint32_t frame_ack;  // It has every tick before this one
    uint64_t frame_mask; // ...and these after it: the next 64, a second of ticks at 60 a second
    sent_mark *frame_sent;
    stored_frame *client_frames; // Recipient-specific frames; NULL without projections.
    void *verified_world; // Last acknowledged full server endpoint, retained for recovery causes.
    uint32_t verified_tick;
    uint32_t repair_requested;
    uint32_t repair_burst;
    double repair_last;
    double repair_stream_until;
    // It was sent the world and needs every tick from it on, however long
    // that took: the server keeps them until it catches up (keep_needed)
    bool catching_up;

    // Its inputs, by tick
    uint32_t newest_input; // The last tick it has an input for, plus one
    // The last tick it sent an input for, plus one, one that came too late
    // among them: how late it is goes by this. None before its first, as a
    // player who's still being sent the world has no tick to sample one for.
    uint32_t newest_sent;
    bool sent_input;
    uint32_t *input_tick;
    uint16_t *input_size;
    double *input_arrived; // When each first arrived, by its transport's clock
    // The least its inputs waited for the ticks of the last update that ran
    // any (note_wait), in WAITS, and which update that was: nothing known
    // before its first input
    int32_t waited;
    bool waited_known;
    uint64_t waited_update;
    uint8_t *inputs; // `inputs` of max_input_bytes, then the last one set
    uint32_t last_size;
    command_queue commands;
    // A big command on its way up, one at a time: the upload's number (0:
    // none), the command's sequence and size, its chunks and the bytes so
    // far. `upload_done` is the last upload that came whole, and which
    // command it was: the player is told so, with the number of its download
    // once it's published, until its next upload.
    uint32_t upload_number;
    uint32_t upload_sequence;
    uint32_t upload_size;
    uint32_t upload_chunks;
    uint32_t upload_had;
    uint8_t *upload;
    uint8_t *upload_have;
    uint32_t upload_done;
    uint32_t upload_done_channel;
    uint32_t upload_done_sequence;
    // A big command on its way down, one at a time (published.number): what
    // the player said it has of it (as of a world's chunks), when each chunk
    // was sent, and the last it said it had whole.
    uint32_t download_number;
    uint32_t download_ack;
    uint64_t download_mask;
    double *download_sent;
    uint32_t download_done;
} connection;

// A big command the server took from a player, kept while it goes to every
// player and, once it's in a tick, for as long as the tick's frame is: a
// player sent a world from before it gets it with the ticks it lacks.
typedef struct published {
    uint32_t number;   // Its download's, from 1
    uint32_t player;   // Whose it is
    uint32_t channel;  // ...and its channel, and its sequence in it
    uint32_t sequence;
    uint32_t size;
    uint8_t *data;
    bool dispatched;   // In a tick already...
    uint32_t tick;     // ...this one
    tide_player_set acked; // The players who have all of it
} published;

// splitmix64
static uint64_t next_random(uint64_t *state)
{
    uint64_t z = (*state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// A player the server sent away, and why. A goodbye can be lost, so it tells
// them again whenever they're in touch: when they send from where they were,
// and when they join again with their cookie, from anywhere, as they do to
// take a match over. They're let in again once they come back knowing.
typedef struct kicked {
    bool used;
    uint64_t cookie;      // The player's, if it had one by then
    uint32_t transport;   // Where it was last
    tide_address address;
    uint32_t nonce;       // Its HELLOs, if it was still joining; another one is a new try
    char message[TIDE_MESSAGE_BYTES];
} kicked;

// A kick's message into `out`: as much as fits, cut where a character starts,
// and only while it's UTF-8 (it may come from another machine), stopping at a
// NUL or `size` bytes.
static void copy_message(char out[TIDE_MESSAGE_BYTES], const char *message, const size_t size)
{
    size_t n = 0;
    while (message && n < size && message[n]) {
        const unsigned char lead = (unsigned char)message[n];
        const size_t length = lead < 0x80u ? 1u : lead >= 0xC2u && lead < 0xE0u ? 2u : lead >= 0xE0u && lead < 0xF0u ? 3u
                            : lead >= 0xF0u && lead < 0xF5u ? 4u : 0u;
        bool whole = length > 0 && n + length <= size && n + length < TIDE_MESSAGE_BYTES;
        for (size_t i = 1; whole && i < length; i++) whole = ((unsigned char)message[n + i] & 0xC0u) == 0x80u;
        if (!whole) break;
        n += length;
    }
    if (n) memcpy(out, message, n);
    memset(out + n, 0, TIDE_MESSAGE_BYTES - n);
}

struct tide_server {
    tide_server_desc desc;
    const tide_game *game;
    void *world;
    uint32_t tick;
    bool started;
    bool ended; // The match ran out of scenes (tide_game.ended): no more ticks
    bool closed; // Takes no one new, but the players on this machine (tide_session_close)
    double clock_start;
    uint32_t clock_base;
    double now;
    uint64_t updates; // How many it has had
    double arrived; // When the datagram being read arrived, by its transport's clock (arrived_of)
    traffic traffic;
    uint32_t players;        // How many the game takes (tide_game_players): what the arrays by player have
    connection *connections; // Player n is connections[n]
    uint32_t (*events)[2];    // Joins and leaves before the next tick: EVENTS_PER_PLAYER for each player
    uint32_t event_count;
    uint32_t command_player; // Next sender considered, so a busy player cannot starve the others
    published *published;    // Big commands, by number (see `published`): PUBLISHED_MOST at most
    uint32_t published_count;
    uint32_t next_published;
    windows w;
    stored_frame *frames;
    // The whole world as last packed to send, which every player joining in
    // the same tick is sent, so a tick packs it once however many join. NULL
    // once nobody is sent it. A tick's pack is its players' (shared_pack): it
    // outlives its place here when another tick's takes it.
    struct shared_pack *pack;
    uint8_t *frame;  // The tick being made, then its inputs as they're made (frame_capacity each)
    uint8_t *packet; // TIDE_NET_MTU
    uint8_t *input;  // An input: input_size, then its packed bytes
    uint8_t *packed; // max_input_bytes
    uint8_t *server_last; // The server's input last set, packed
    uint32_t server_last_size;
    uint8_t *scratch; // scratch_size: inputs and their deltas
    // Each player's cookie, kept after they leave so they can come back, and
    // since when they're away (0: here, or never was)
    uint64_t *cookies;
    uint8_t (*identities)[32];
    uint64_t *digests; // ...and each one's digest: all a server it went on from passed on
    double *away_since;
    uint64_t random;
    // How its match started, for players to start it too (START_*), with
    // Time.dt, the seed it picked (in a game that has one) and, for
    // START_GIVEN, the game's tide_start
    uint8_t start_kind;
    float dt;
    uint32_t seed;
    uint8_t *start;
    tide_player_set present; // Players in the world it went on from, who haven't joined it yet (tide_server_desc.players)
    double present_until; // ...who leave then
    kicked *kicks; // Players sent away, told so every update for a while
    // Host migration: the room its players meet in again when it goes, and
    // what it last told them (see send_handover)
    char room_code[TIDE_ROOM_CODE_LENGTH + 1];
    char room_key[TIDE_ROOM_KEY_LENGTH + 1];
    uint64_t handover_hash;
    uint32_t handover_version;
    double handover_changed;
    double handover_sent;
    uint8_t *handover; // The record as last made, which goes in parts (handover_capacity)
    // Shard messages handed it for coming ticks (tide_server_inject), in order;
    // a big one has its download's number once published
    struct injected *injected;
    uint32_t injected_count, injected_room;
};

static double resend_after(const uint32_t rtt_ms)
{
    const double rtt = (double)rtt_ms / 1000.0;
    return rtt * 1.5 > 0.05 ? rtt * 1.5 : 0.05;
}

// A join or leave for the next tick, in order. A player who joins and leaves
// before the tick runs was never in the match: the two go together, so each
// player has at most two waiting (a leave, then a join) and none is ever lost.
static void add_event(tide_server *s, const uint8_t kind, const uint32_t player)
{
    if (kind == EVENT_LEAVE) {
        for (uint32_t i = s->event_count; i-- > 0;) {
            if (s->events[i][1] != player) continue;
            if (s->events[i][0] != EVENT_JOIN) break;
            memmove(s->events[i], s->events[i + 1u], (s->event_count - i - 1u) * sizeof s->events[0]);
            s->event_count--;
            return;
        }
    }
    const uint32_t most = EVENTS_PER_PLAYER * s->players;
    assert(s->event_count < most);
    if (s->event_count == most) return;
    s->events[s->event_count][0] = kind;
    s->events[s->event_count][1] = player;
    s->event_count++;
}

// `g`: the game whose world `hashed` is.
// Lets go of what `c` is being sent: its own bytes, or its share of a tick's
// pack, which goes once none of that tick's players is sent it. A pack is
// counted by the players it's sent to, whichever tick the server has moved on
// to: one tick's count never frees another's bytes.
static void drop_snapshot(tide_server *s, connection *c)
{
    if (c->shared) {
        if (--c->shared->refs == 0) {
            if (s->pack == c->shared) s->pack = NULL;
            free(c->shared->bytes);
            free(c->shared);
        }
    } else {
        free(c->snapshot);
    }
    free(c->chunk_sent);
    c->snapshot = NULL;
    c->shared = NULL;
    c->chunk_sent = NULL;
}

static void stop_sending(tide_server *s, connection *c)
{
    drop_snapshot(s, c);
    free_world(s->game, c->hashed);
    free(c->hashed);
    c->hashed = NULL;
    c->sending = false;
}

// Sends `bytes` (size `size`, to free()) in chunks, from the first; or with
// `shared`, that pack's bytes, which `c` is counted among the players of.
static void send_in_chunks(tide_server *s, connection *c, const uint8_t kind, uint8_t *bytes, const uint32_t size,
                           shared_pack *shared)
{
    drop_snapshot(s, c);
    c->snapshot = bytes;
    c->shared = shared;
    c->snapshot_number++;
    c->snapshot_kind = kind;
    c->snapshot_size = size;
    c->chunk_count = chunks_of(size);
    c->chunk_sent = calloc(c->chunk_count ? c->chunk_count : 1u, sizeof *c->chunk_sent);
    if (!c->chunk_sent) tide_out_of_memory();
    c->chunk_ack = 0;
    c->chunk_mask = 0;
}

static const void *server_world_at(const tide_server *s, const uint32_t tick)
{
    if (tick == s->tick) return s->world;
    const stored_frame *f = &s->frames[tick % s->w.history];
    return f->tick == tick ? f->world : NULL;
}

static void start_snapshot(tide_server *s, connection *c);

static void start_client_repair(tide_server *s, connection *c, const uint32_t tick)
{
    if (tick <= c->frame_ack || tick > s->tick || c->sending) return;
    const void *world = server_world_at(s, tick);
    const void *base = c->verified_world && c->verified_tick == c->frame_ack
        ? c->verified_world : server_world_at(s, c->frame_ack);
    if (!world || !base || tick - c->frame_ack > s->w.history) { start_snapshot(s, c); return; }
    const tide_player_id player = tide_player_from_index((int32_t)(c - s->connections));
    uint32_t size = 0;
    tide_event_log events = {0};
    for (uint32_t t = c->frame_ack; t < tick; t++) {
        const stored_frame *frame = c->client_frames ? &c->client_frames[t % s->w.history] : NULL;
        if (!frame || frame->tick != t || !frame->data) {
            tide_event_log_free(&events);
            start_snapshot(s, c);
            return;
        }
        for (uint32_t at = 0; at < frame->events.size;) {
            const tide_heard *heard = (const tide_heard *)(frame->events.bytes + at);
            at += heard->size;
            tide_event_log_copy(&events, &frame->events, heard, 0);
        }
    }
    uint8_t *bytes = pack_client_state(s->game, world, base, base, &events, player, c->frame_ack, &size);
    tide_event_log_free(&events);
    if (!bytes) return;
    stop_sending(s, c);
    c->snapshot_tick = tick;
    c->snapshot_stream = s->now < c->repair_stream_until;
    c->sending = true;
    send_in_chunks(s, c, SEND_CLIENT_REPAIR, bytes, size, NULL);
}

// The hashes of the world's pages, after how the match started (see SEND_HASHES).
static uint8_t *hashes_of(const tide_server *s, const void *world, uint32_t *size)
{
    const tide_game *g = s->game;
    uint32_t list_size;
    uint8_t *list = g->hash_pages(world, &list_size);
    const uint32_t start = s->start_kind == START_GIVEN ? g->start_size : 0u;
    tide_writer w = {.grows = true};
    tide_write_u8(&w, s->start_kind);
    uint32_t dt;
    memcpy(&dt, &s->dt, 4);
    tide_write_u32(&w, dt);
    if (g->seeded) tide_write_u32(&w, s->seed);
    tide_write_bytes(&w, s->start, start);
    tide_write_bytes(&w, list, list_size);
    free(list);
    if (w.overflow) tide_out_of_memory();
    *size = w.size;
    return w.data;
}

// The world as it is now, before the next tick, to send it: whole, or for a
// player with a world to build it from (or that can start the match itself,
// over a network), its pages' hashes first. A world that goes in one update
// anyway goes whole: the hashes would only cost it a round trip.
static void start_snapshot(tide_server *s, connection *c)
{
    const tide_game *g = s->game;
    const void *had = g->client_state && c->has_world
        ? (c->verified_world ? c->verified_world : server_world_at(s, c->frame_ack)) : NULL;
    const uint32_t had_tick = c->verified_world ? c->verified_tick : c->frame_ack;
    stop_sending(s, c);
    c->snapshot_tick = s->tick;
    c->snapshot_stream = false;
    c->sending = true;
    c->frame_ack = s->tick;
    c->frame_mask = 0;
    c->catching_up = true;
    memset(c->frame_sent, 0, s->w.history * sizeof *c->frame_sent);
    if (g->client_state) {
        uint32_t size = 0;
        const tide_player_id player = tide_player_from_index((int32_t)(c - s->connections));
        tide_event_log causes = {0};
        const uint32_t oldest = s->tick > s->w.history ? s->tick - s->w.history : 0u;
        for (uint32_t t = had && had_tick > oldest ? had_tick : oldest; had && t < s->tick; t++) {
            const stored_frame *frame = c->client_frames ? &c->client_frames[t % s->w.history] : NULL;
            if (!frame || frame->tick != t) continue;
            for (uint32_t at = 0; at < frame->events.size;) {
                const tide_heard *heard = (const tide_heard *)(frame->events.bytes + at);
                at += heard->size;
                if (heard->event == TIDE_HEARD_DESTROYED_CAUSE) tide_event_log_copy(&causes, &frame->events, heard, 0);
            }
        }
        uint8_t *bytes = pack_client_state(g, s->world, NULL, had, &causes, player, 0, &size);
        tide_event_log_free(&causes);
        if (!bytes) { c->sending = false; return; }
        send_in_chunks(s, c, SEND_CLIENT_WORLD, bytes, size, NULL);
        return;
    }
    const bool base = c->has_world || (c->can_start && s->start_kind != START_UNKNOWN && !c->local);
    const bool big = !g->pack_world || g->pack_world(s->world, NULL, 0) > CHUNK * CHUNKS_PER_UPDATE;
    c->hashed = base && big && g->hash_pages ? calloc(1, g->world_size) : NULL;
    if (c->hashed) g->copy_world(c->hashed, s->world);
    if (c->hashed) {
        uint32_t size;
        uint8_t *bytes = hashes_of(s, c->hashed, &size);
        send_in_chunks(s, c, SEND_HASHES, bytes, size, NULL);
        return;
    }
    uint8_t asset_pack[32];
    tide_pack_digest(asset_pack);
    if (memcmp(asset_pack, c->asset_pack, 32) == 0 && memcmp(asset_pack, (uint8_t[32]){0}, 32) != 0) {
        uint32_t size;
        uint8_t *bytes = g->pack_delta(s->world, NULL, NULL, 0, true, &size);
        send_in_chunks(s, c, SEND_WORLD, bytes, size, NULL);
        return;
    }
    // The whole world: packed once a tick, however many join in it. An
    // earlier tick's pack that players are still being sent stays theirs.
    if (!s->pack || s->pack->tick != s->tick) {
        shared_pack *made = calloc(1, sizeof *made);
        if (!made) tide_out_of_memory();
        made->tick = s->tick;
        made->bytes = g->pack_delta(s->world, NULL, NULL, 0, false, &made->size);
        s->pack = made;
    }
    s->pack->refs++;
    send_in_chunks(s, c, SEND_WORLD, s->pack->bytes, s->pack->size, s->pack);
}

// It said which pages of the hashed world its own lacks: those, as a delta.
static void send_lacking(tide_server *s, connection *c, const uint8_t *lacks, const uint32_t size)
{
    const tide_game *g = s->game;
    uint32_t delta_size;
    uint8_t asset_pack[32];
    tide_pack_digest(asset_pack);
    const bool assets = memcmp(asset_pack, c->asset_pack, 32) == 0 && memcmp(asset_pack, (uint8_t[32]){0}, 32) != 0;
    uint8_t *delta = g->pack_delta(c->hashed, NULL, lacks, size, assets, &delta_size);
    free_world(g, c->hashed);
    free(c->hashed);
    c->hashed = NULL;
    send_in_chunks(s, c, SEND_WORLD, delta, delta_size, NULL);
}

static void stop_upload(connection *c)
{
    free(c->upload);
    free(c->upload_have);
    c->upload = NULL;
    c->upload_have = NULL;
    c->upload_number = 0;
    c->upload_had = 0;
}

static void stop_download(connection *c)
{
    free(c->download_sent);
    c->download_sent = NULL;
    c->download_number = 0;
}

static void free_connection(tide_server *s, connection *c)
{
    stop_sending(s, c);
    stop_upload(c);
    stop_download(c);
    free(c->inputs);
    free(c->input_tick);
    free(c->input_size);
    free(c->input_arrived);
    free(c->frame_sent);
    for (uint32_t i = 0; c->client_frames && i < s->w.history; i++) {
        free(c->client_frames[i].data);
        tide_event_log_free(&c->client_frames[i].events);
    }
    free(c->client_frames);
    free_world(s->game, c->verified_world);
    free(c->verified_world);
    free_commands(&c->commands);
    memset(c, 0, sizeof *c);
}

static void free_published(tide_server *s)
{
    for (uint32_t i = 0; i < s->published_count; i++) free(s->published[i].data);
    s->published_count = 0;
}

static published *published_by_number(tide_server *s, const uint32_t number)
{
    for (uint32_t i = 0; i < s->published_count; i++) {
        if (s->published[i].number == number) return &s->published[i];
    }
    return NULL;
}

// Whether `c` needs the published command: one not in a tick yet, or in a
// tick from the world it was sent on, whose frame it's sent
static bool needs_published(const connection *c, const published *p)
{
    return !p->dispatched || p->tick >= c->snapshot_tick;
}

// Whether the published command is the player's own last upload, whose
// bytes it still has: told its number, it says it has the download
static bool own_upload(const connection *c, const uint32_t player, const published *p)
{
    return p->player == player && c->upload_done && p->channel == c->upload_done_channel
        && p->sequence == c->upload_done_sequence;
}

// The download's number of the player's last upload, once it's published
static uint32_t upload_done_published(const tide_server *s, const connection *c)
{
    const uint32_t player = (uint32_t)(c - s->connections);
    for (uint32_t i = 0; i < s->published_count; i++) {
        if (own_upload(c, player, &s->published[i])) return s->published[i].number;
    }
    return 0;
}

// A player that was in the match leaves it. One that only said hello never
// joined: its slot is as it was, and a cookie made for it stops working, as
// it was sent wherever the hello said.
static void drop_connection(tide_server *s, connection *c)
{
    const uint32_t player = (uint32_t)(c - s->connections);
    if (c->greeted) {
        add_event(s, EVENT_LEAVE, player);
        s->away_since[player] = s->now > 0.0 ? s->now : 1e-9;
    } else if (c->new_player) {
        s->cookies[player] = s->digests[player] = 0;
    }
    free_connection(s, c);
}

// Whether its players take the match over when it goes (see tide_session_take_over).
static bool hands_over(const tide_server *s)
{
    return !s->game->client_state && s->game->host_migration && s->room_key[0] && s->room_code[0];
}

static void send_bye(tide_server *s, const uint32_t transport, const tide_address to)
{
    uint8_t data[8];
    tide_writer w = {data, sizeof data, 0, false, false};
    header(&w, MSG_BYE);
    tide_write_u8(&w, s->ended ? BYE_ENDED : hands_over(s) ? BYE_HANDOVER : BYE_LEFT);
    send_packet(&s->desc.transports[transport], to, &w, &s->traffic);
}

static void send_kicked(tide_server *s, const kicked *k)
{
    uint8_t data[8u + TIDE_MESSAGE_BYTES];
    tide_writer w = {data, sizeof data, 0, false, false};
    header(&w, MSG_BYE);
    tide_write_u8(&w, BYE_KICKED);
    const uint32_t n = (uint32_t)strlen(k->message);
    tide_write_u8(&w, (uint8_t)n);
    tide_write_bytes(&w, k->message, n);
    send_packet(&s->desc.transports[k->transport], k->address, &w, &s->traffic);
}

static void refuse(tide_server *s, const uint32_t transport, const tide_address to, const uint8_t reason)
{
    uint8_t data[8];
    tide_writer w = {data, sizeof data, 0, false, false};
    header(&w, MSG_REFUSE);
    tide_write_u8(&w, reason);
    send_packet(&s->desc.transports[transport], to, &w, &s->traffic);
}

static connection *find_connection(tide_server *s, const uint32_t transport, const tide_address from)
{
    for (uint32_t i = 0; i < s->players; i++) {
        connection *c = &s->connections[i];
        if (c->used && c->transport == transport && tide_address_equal(c->address, from)) return c;
    }
    return NULL;
}

static void on_hello(tide_server *s, const uint32_t transport, const tide_address from, tide_reader *r)
{
    const uint64_t game = tide_read_u64(r);
    const uint8_t *simulation = tide_read_bytes(r, 32);
    const uint32_t nonce = tide_read_u32(r);
    const uint16_t their_time = tide_read_u16(r);
    const uint64_t cookie = tide_read_u64(r);
    const uint8_t flags = tide_read_u8(r);
    const uint8_t *asset_pack = tide_read_bytes(r, 32);
    if (r->failed) return;
    uint8_t identity[32] = {0};
    const uint8_t anonymous[32] = {0};
    const uint64_t digest = cookie_digest(cookie);
    if (s->desc.identity) {
        if (!s->desc.identity(s->desc.identity_user, transport, from, identity)) {
            refuse(s, transport, from, REFUSE_CLOSED);
            return;
        }
        for (uint32_t i = 0; cookie && i < s->players; i++) {
            if (s->digests[i] == digest && memcmp(identity, s->identities[i], 32) != 0) {
                refuse(s, transport, from, REFUSE_CLOSED);
                return;
            }
        }
    }
    for (uint32_t i = 0; i < s->players; i++) {
        kicked *k = &s->kicks[i];
        if (!k->used) continue;
        const bool here = k->transport == transport && tide_address_equal(k->address, from);
        const bool same = cookie && k->cookie == cookie;
        if (same && (flags & HELLO_KNEW_KICK)) {
            k->used = false; // Back, knowing: a kick isn't a ban
        } else if (same || (here && k->nonce == nonce)) {
            // It didn't hear: told again, wherever it is now
            k->transport = transport;
            k->address = from;
            k->nonce = nonce;
            send_kicked(s, k);
            return;
        } else if (here) {
            k->used = false; // That machine trying again, without a cookie: someone new
        }
    }
    connection *c = find_connection(s, transport, from);
    if (c && c->nonce == nonce) return; // It's being welcomed already
    if (memcmp(s->game->simulation, anonymous, 32) == 0
        ? game != s->game->hash : memcmp(simulation, s->game->simulation, 32) != 0) {
        if (c) drop_connection(s, c);
        refuse(s, transport, from, REFUSE_OTHER_GAME);
        return;
    }
    // Its cookie's digest: the player's, from this server or the one it went on from
    int32_t player = -1;
    for (uint32_t i = 0; cookie && i < s->players; i++) {
        if (s->digests[i] == digest) player = (int32_t)i;
    }
    if (s->desc.identity && memcmp(identity, anonymous, 32) != 0) {
        for (uint32_t i = 0; i < s->players; i++) {
            if (s->digests[i] && memcmp(identity, s->identities[i], 32) == 0) player = (int32_t)i;
        }
    }
    if (player >= 0 && !s->cookies[player]) s->cookies[player] = cookie;
    if (c && (int32_t)(c - s->connections) != player) drop_connection(s, c); // The same machine, starting over

    // The player is still here, on another connection: this one takes over,
    // as if nothing happened, from the world as it is now, once it writes back.
    if (player >= 0 && s->connections[player].used) {
        c = &s->connections[player];
        const bool was_greeted = c->greeted;
        stop_sending(s, c);
        c->transport = transport;
        c->address = from;
        c->nonce = nonce;
        c->last_heard = s->now;
        c->their_time = their_time;
        c->their_arrived = s->arrived;
        c->local = s->desc.local_first && transport == 0;
        for (uint32_t i = 0; i < s->w.inputs; i++) c->input_tick[i] = UINT32_MAX;
        c->newest_input = s->tick;
        c->sent_input = false;
        c->waited_known = false;
        c->has_world = (flags & HELLO_HAS_WORLD) != 0;
        c->can_start = (flags & HELLO_CAN_START) != 0;
        memcpy(c->asset_pack, asset_pack, 32);
        // One that had joined stays joined; one that hadn't still has to write back
        c->greeted = was_greeted;
        c->welcoming = true;
        return;
    }
    // A closed match takes only the players in it: those of the world it went on from too
    const bool in_world = player >= 0 && tide_players_has(&s->present, (uint32_t)player);
    if (s->closed && !(s->desc.local_first && transport == 0) && !in_world) {
        refuse(s, transport, from, REFUSE_CLOSED);
        return;
    }
    // A player coming back gets their slot; a new one a slot never used, or
    // failing that, the one away the longest, whose cookie stops working.
    for (uint32_t i = 0; player < 0 && i < s->players; i++) {
        if (!s->connections[i].used && s->digests[i] == 0) player = (int32_t)i;
    }
    if (player < 0) {
        for (uint32_t i = 0; i < s->players; i++) {
            if (s->connections[i].used || tide_players_has(&s->present, i)) continue; // In the match
            if (player < 0 || s->away_since[i] < s->away_since[player]) player = (int32_t)i;
        }
        if (player >= 0) { // Given away
            s->cookies[player] = s->digests[player] = 0;
            s->kicks[player].used = false;
        }
    }
    if (player < 0) {
        refuse(s, transport, from, REFUSE_FULL);
        return;
    }
    const bool new_player = s->cookies[player] == 0;
    if (new_player) {
        memcpy(s->identities[player], identity, 32);
        s->random ^= (uint64_t)from.host << 16 ^ from.port ^ (uint64_t)nonce << 32;
        do s->cookies[player] = next_random(&s->random);
        while (s->cookies[player] == 0);
        s->digests[player] = cookie_digest(s->cookies[player]);
    }
    c = &s->connections[player];
    const uint32_t bytes = s->game->max_input_bytes ? s->game->max_input_bytes : 1u;
    *c = (connection){.used = true, .transport = transport, .address = from, .nonce = nonce, .last_heard = s->now,
                      .their_time = their_time, .their_arrived = s->arrived, .new_player = new_player, .welcoming = true,
                      .has_world = (flags & HELLO_HAS_WORLD) != 0, .can_start = (flags & HELLO_CAN_START) != 0};
    memcpy(c->asset_pack, asset_pack, 32);
    c->local = s->desc.local_first && transport == 0;
    c->inputs = malloc((size_t)(s->w.inputs + 1u) * bytes);
    c->input_tick = malloc(s->w.inputs * sizeof *c->input_tick);
    c->input_size = calloc(s->w.inputs, sizeof *c->input_size);
    c->input_arrived = calloc(s->w.inputs, sizeof *c->input_arrived);
    c->frame_sent = calloc(s->w.history, sizeof *c->frame_sent);
    if (s->game->client_state) c->client_frames = calloc(s->w.history, sizeof *c->client_frames);
    if (!c->inputs || !c->input_tick || !c->input_size || !c->input_arrived || !c->frame_sent
        || (s->game->client_state && !c->client_frames)) {
        free_connection(s, c);
        if (new_player) s->cookies[player] = s->digests[player] = 0;
        refuse(s, transport, from, REFUSE_FULL);
        return;
    }
    for (uint32_t i = 0; i < s->w.inputs; i++) c->input_tick[i] = UINT32_MAX;
    c->newest_input = s->tick;
    c->sent_input = false;
    c->waited_known = false;
    // It's welcomed every update until it writes back, and joins the match then (greet)
}

// Its first word after its hello, so it is where it said: it joins the match,
// and the match starts with its first player.
static void greet(tide_server *s, connection *c)
{
    const uint32_t player = (uint32_t)(c - s->connections);
    c->greeted = true;
    c->new_player = false;
    s->away_since[player] = 0.0;
    if (!tide_players_has(&s->present, player)) add_event(s, EVENT_JOIN, player); // Else it's in the world already
    tide_players_remove(&s->present, player);
    if (!s->started) { // The first player: the match starts with them
        s->started = true;
        s->clock_start = s->now;
        s->clock_base = s->tick;
    }
}

static void on_client(tide_server *s, connection *c, tide_reader *r)
{
    const uint16_t their_time = tide_read_u16(r);
    const uint16_t echo = tide_read_u16(r);
    const uint8_t flags = tide_read_u8(r);
    const uint8_t *asset_pack = flags & CLIENT_ASSETS ? tide_read_bytes(r, 32) : NULL;
    // What it has of what's being sent (see send_client)
    const uint32_t chunk_number = tide_read_varint(r);
    const uint32_t have = chunk_number ? tide_read_varint(r) : 0u;
    const uint32_t chunk_ack = !chunk_number ? 0u : have == 0 ? UINT32_MAX : have - 1u;
    const uint64_t chunk_mask = chunk_number && have ? tide_read_u64(r) : 0u;
    const uint32_t frame_ack = tide_read_varint(r);
    const uint64_t frame_mask = tide_read_varint64(r);
    const uint32_t repair_tick = flags & CLIENT_REPAIR ? tide_read_varint(r) : 0u;
    uint32_t lacks_number = 0;
    uint32_t lacks_size = 0;
    const uint8_t *lacks = NULL;
    if (flags & CLIENT_LACKS) {
        lacks_number = tide_read_varint(r);
        lacks_size = tide_read_varint(r);
        lacks = tide_read_bytes(r, lacks_size);
    }
    // What it has of the big command on its way down (see send_client), in a
    // game with commands: its number (0: none), then 0 for all of it, or the
    // chunks it has from the first, plus one, and which it has after those
    uint32_t download_number = 0;
    uint32_t download_ack = 0;
    uint64_t download_mask = 0;
    if (s->game->command) {
        download_number = tide_read_varint(r);
        const uint32_t has = download_number ? tide_read_varint(r) : 0u;
        download_ack = !download_number ? 0u : has == 0 ? UINT32_MAX : has - 1u;
        download_mask = download_number && has ? tide_read_u64(r) : 0u;
    }
    const uint32_t first_input = frame_ack + tide_read_varint(r);
    const uint8_t input_count = tide_read_u8(r);
    if (r->failed) return;
    if (download_number) {
        published *p = published_by_number(s, download_number);
        if (p && download_ack == UINT32_MAX) { // It has all of it
            tide_players_add(&p->acked, (uint32_t)(c - s->connections));
            if (c->download_number == download_number) stop_download(c);
            if (download_number > c->download_done) c->download_done = download_number;
        } else if (p && c->download_number == download_number) {
            if (download_ack > c->download_ack) {
                c->download_ack = download_ack < chunks_of(p->size) ? download_ack : chunks_of(p->size);
            }
            if (download_ack == c->download_ack) c->download_mask = download_mask;
        }
    }
    // Whether it wants the world goes by the newest it sent: an older one can come late
    const bool newest = (int16_t)(uint16_t)(their_time - c->their_time) >= 0;
    const bool wants = (flags & CLIENT_WANTS_WORLD) != 0;
    c->last_heard = s->now;
    if (newest) {
        if (asset_pack) memcpy(c->asset_pack, asset_pack, 32);
        c->their_time = their_time;
        c->their_arrived = s->arrived;
    }
    if (echo) c->rtt_ms = millis_since(s->now, echo);
    c->welcoming = false;
    if (!c->greeted) greet(s, c);

    if (s->game->client_state && frame_ack <= s->tick
        && (frame_ack > c->snapshot_tick || (chunk_number == c->snapshot_number && chunk_ack == UINT32_MAX))
        && (!c->verified_world || frame_ack > c->verified_tick)) {
        const void *known = server_world_at(s, frame_ack);
        if (known) {
            if (!c->verified_world) c->verified_world = calloc(1, s->game->world_size);
            if (!c->verified_world) tide_out_of_memory();
            s->game->copy_world(c->verified_world, known);
            c->verified_tick = frame_ack;
            c->has_world = true;
        }
    }
    if (s->game->client_state && frame_ack > c->frame_ack && frame_ack <= s->tick) c->frame_ack = frame_ack;

    const bool was_sending = c->sending;
    if (c->sending) {
        if (chunk_number == c->snapshot_number) { // About what's being sent, not something older
            if (chunk_ack > c->chunk_ack) c->chunk_ack = chunk_ack < c->chunk_count ? chunk_ack : c->chunk_count;
            if (chunk_ack == c->chunk_ack) c->chunk_mask = chunk_mask;
        }
        if (c->snapshot_kind == SEND_HASHES && lacks && lacks_number == c->snapshot_number) {
            send_lacking(s, c, lacks, lacks_size);
        } else if ((c->snapshot_kind != SEND_HASHES && c->chunk_ack >= c->chunk_count) || (newest && !wants)) {
            stop_sending(s, c); // It has the world, or one that needs nothing: ticks from here
        }
    }
    if (s->game->client_state && repair_tick && newest) {
        if (repair_tick == c->frame_ack + 1u && repair_tick <= s->tick) {
            if (!(flags & CLIENT_REPAIR_STREAM) && repair_tick != c->repair_requested) {
                c->repair_burst = s->now - c->repair_last <= 1.0 ? c->repair_burst + 1u : 1u;
                c->repair_last = s->now;
                c->repair_requested = repair_tick;
                if (c->repair_burst >= 2u) c->repair_stream_until = s->now + 1.0;
            }
            start_client_repair(s, c, (flags & CLIENT_REPAIR_STREAM) || s->now < c->repair_stream_until ? s->tick : repair_tick);
        }
    } else if (!was_sending && newest && wants) {
        c->has_world = (flags & CLIENT_HAS_WORLD) != 0;
        c->can_start = (flags & CLIENT_CAN_START) != 0;
        start_snapshot(s, c); // Its world went wrong, or never came: sent again
    } else if (!was_sending) {
        if (frame_ack > c->frame_ack && frame_ack <= s->tick) c->frame_ack = frame_ack;
        if (frame_ack == c->frame_ack) c->frame_mask = frame_mask;
    }

    // Its inputs: after the first, each as what differs from the one before,
    // when the game packs them that way
    const tide_game *g = s->game;
    const uint32_t max = g->max_input_bytes;
    const bool deltas = g->read_input && g->read_input_delta;
    uint8_t *previous = s->scratch;
    uint8_t *current = s->scratch + g->input_size;
    uint8_t *whole = current + g->input_size;
    for (uint32_t i = 0; i < input_count; i++) {
        const uint32_t size = tide_read_varint(r);
        const uint8_t *bytes = tide_read_bytes(r, size);
        if (!bytes) return;
        uint32_t n = size;
        if (deltas) { // Each builds on the one before, late or not
            if (!(i == 0 ? g->read_input(bytes, size, current) : g->read_input_delta(bytes, size, previous, current))) return;
            n = g->write_input(current, whole, max);
            bytes = whole;
            uint8_t *swap = previous;
            previous = current;
            current = swap;
        }
        const uint32_t t = first_input + i;
        if (n > max || n == 0) continue;
        if (!c->sent_input || t + 1u > c->newest_sent) c->newest_sent = t + 1u; // Whenever it came: it's what the player has got to
        c->sent_input = true;
        if (t < s->tick || t - s->tick >= s->w.inputs) continue; // Late, or much too early
        const uint32_t slot = t % s->w.inputs;
        if (c->input_tick[slot] != t) c->input_arrived[slot] = s->arrived; // As it first came: it's sent until the server says it has it
        c->input_tick[slot] = t;
        c->input_size[slot] = (uint16_t)n;
        memcpy(c->inputs + (size_t)slot * max, bytes, n);
        if (t + 1u > c->newest_input) c->newest_input = t + 1u;
    }
}

// Separate from sampled inputs: a command that arrives after its original
// frame still needs a turn. The connection supplies its sender and attempt,
// and the world supplies the last dispatched sequence.
// Takes a command that came whole, in a datagram or as an upload, into the
// player's queue: once its bytes decode, its channel (the bytes' own) has
// room, and the world hasn't applied its sequence, nor the queue got it
// already. `data` is to free(): the queue's if it's taken.
static void take_command(tide_server *s, connection *c, const uint32_t sequence, uint8_t *data, const uint32_t size)
{
    const tide_game *g = s->game;
    const tide_player_id sender = tide_player_from_index((int32_t)(c - s->connections));
    uint32_t channel = 0;
    if (!g->check_command(data, size, &channel)
        || (channel == TIDE_COMMAND_UNORDERED ? !g->command_applied : channel >= tide_game_command_channels(g))) {
        free(data);
        return;
    }
    const uint32_t previous = channel == TIDE_COMMAND_UNORDERED ? 0u : g->command_sequence(s->world, sender, channel);
    if (!sequence_pending(channel, sequence, previous) || command_done(g, s->world, sender, channel, sequence)
        || queued(&c->commands, channel, sequence)) {
        free(data);
        return;
    }
    (void)append_owned(&c->commands, channel, sequence, data, size);
}

// Separate from sampled inputs: a command that arrives after its original
// frame still needs a turn. The connection supplies its sender and attempt,
// and the world supplies the last dispatched sequence.
static void on_command(tide_server *s, connection *c, tide_reader *r)
{
    const tide_game *g = s->game;
    const uint32_t nonce = tide_read_u32(r);
    const uint32_t sequence = tide_read_varint(r);
    const uint32_t size = tide_read_varint(r);
    if (r->failed || !c->greeted || nonce != c->nonce || !g->command || !g->command_sequence || !g->check_command
        || !size || size > TIDE_COMMAND_BYTES) return;
    const uint8_t *data = tide_read_bytes(r, size);
    if (!data || r->at != r->size) return;
    uint8_t *copy = malloc(size);
    if (!copy) return;
    memcpy(copy, data, size);
    take_command(s, c, sequence, copy, size);
}

// A chunk of a command too big for a datagram. The player sends one upload
// at a time: a new number replaces one on its way, and one that came whole
// is acknowledged, by its number, until the next.
static void on_upload(tide_server *s, connection *c, tide_reader *r)
{
    const tide_game *g = s->game;
    const uint32_t nonce = tide_read_u32(r);
    const uint32_t number = tide_read_varint(r);
    const uint32_t sequence = tide_read_varint(r);
    const uint32_t size = tide_read_varint(r);
    const uint32_t index = tide_read_varint(r);
    const uint16_t bytes = tide_read_u16(r);
    const uint8_t *data = tide_read_bytes(r, bytes);
    if (r->failed || !data || r->at != r->size || !c->greeted || nonce != c->nonce || !g->command
        || !g->command_sequence || !g->check_command || !number || !big_command(size) || size > TIDE_COMMAND_LIMIT
        || number == c->upload_done) return;
    const uint32_t chunks = chunks_of(size);
    if (index >= chunks || (uint32_t)index * CHUNK + bytes > size
        || bytes != (size - index * CHUNK < CHUNK ? size - index * CHUNK : CHUNK)) return;
    if (number != c->upload_number) {
        stop_upload(c);
        c->upload = malloc(size);
        c->upload_have = calloc(chunks, 1);
        if (!c->upload || !c->upload_have) {
            stop_upload(c);
            return;
        }
        c->upload_number = number;
        c->upload_sequence = sequence;
        c->upload_size = size;
        c->upload_chunks = chunks;
    }
    if (c->upload_have[index]) return;
    memcpy(c->upload + (size_t)index * CHUNK, data, bytes);
    c->upload_have[index] = 1;
    if (++c->upload_had < c->upload_chunks) return;
    // Whole: the queue's now, or dropped; either way it's done, so the
    // player stops sending it
    uint8_t *whole = c->upload;
    c->upload = NULL;
    c->upload_done = number;
    c->upload_done_sequence = c->upload_sequence;
    c->upload_done_channel = 0;
    if (!g->check_command(whole, c->upload_size, &c->upload_done_channel)) c->upload_done_channel = 0;
    take_command(s, c, c->upload_sequence, whole, c->upload_size);
    stop_upload(c);
}

static void on_local_data(tide_server *s, connection *c, tide_reader *r, const bool interest)
{
    const uint32_t player = (uint32_t)(c - s->connections);
    const uint64_t cookie = tide_read_u64(r);
    if (r->failed || !c->greeted || s->ended || cookie != s->cookies[player]) return;
    if (interest) {
        const uint8_t enabled = tide_read_u8(r);
        if (r->failed || r->at != r->size || enabled > 1u) return;
        c->local_until = enabled ? s->now + 1.5 : 0.0;
        return;
    }
    const uint32_t size = r->size - r->at;
    if (!size || size > TIDE_LOCAL_DATA_BYTES || c->local_until <= s->now) return;
    if (s->now > c->local_time) {
        c->local_credit += (s->now - c->local_time) * 60.0;
        if (c->local_credit > 4.0) c->local_credit = 4.0;
        c->local_time = s->now;
    }
    if (c->local_credit < 1.0) return;
    c->local_credit -= 1.0;
    for (uint32_t i = 0; i < s->players; i++) {
        const connection *to = &s->connections[i];
        if (i == player || !to->used || !to->greeted || to->local_until <= s->now) continue;
        uint8_t data[TIDE_NET_MTU];
        tide_writer w = {data, sizeof data, 0, false, false};
        header(&w, MSG_LOCAL_DATA);
        tide_write_u64(&w, s->cookies[i]);
        tide_write_varint(&w, player + 1u);
        tide_write_u64(&w, s->digests[player]);
        tide_write_bytes(&w, r->data + r->at, size);
        send_packet(&s->desc.transports[to->transport], to->address, &w, &s->traffic);
    }
}

static void server_receive(tide_server *s, const uint32_t transport)
{
    const tide_transport *t = &s->desc.transports[transport];
    if (!t->receive) return;
    uint8_t data[TIDE_NET_MTU];
    tide_address from;
    uint32_t size;
    while ((size = t->receive(t->self, &from, data, sizeof data)) > 0) {
        if (from.kind != TIDE_ADDRESS_LOOPBACK) s->traffic.received += size;
        s->arrived = arrived_of(t, s->now);
        tide_reader r = {data, size, 0, false};
        const uint8_t type = read_header(&r);
        if (type == MSG_HELLO) {
            if (s->ended) send_bye(s, transport, from); // Too late to join
            else on_hello(s, transport, from, &r);
            continue;
        }
        connection *c = find_connection(s, transport, from);
        if (!c && type == MSG_CLIENT) { // A player it kicked, who didn't hear: told again
            for (uint32_t i = 0; i < s->players; i++) {
                const kicked *k = &s->kicks[i];
                if (k->used && k->transport == transport && tide_address_equal(k->address, from)) send_kicked(s, k);
            }
        }
        if (!c) continue;
        if (type == MSG_CLIENT) on_client(s, c, &r);
        else if (type == MSG_COMMAND) on_command(s, c, &r);
        else if (type == MSG_UPLOAD) on_upload(s, c, &r);
        else if (type == MSG_LOCAL_INTEREST || type == MSG_LOCAL_DATA) on_local_data(s, c, &r, type == MSG_LOCAL_INTEREST);
        else if (type == MSG_BYE) drop_connection(s, c);
    }
}

// A slot's new input in a frame, after its size: as what differs from the
// slot's input as the world keeps it, which every machine has before that
// tick, when the game packs inputs that way, else whole (`packed`).
static void put_input(tide_server *s, tide_writer *w, const uint32_t slot, const void *input, const uint8_t *packed,
                      uint32_t size)
{
    const tide_game *g = s->game;
    if (g->write_input_delta && g->world_input) {
        uint8_t *delta = s->scratch + g->input_size;
        g->world_input(s->world, slot, s->scratch);
        size = g->write_input_delta(input, s->scratch, delta, g->max_input_bytes);
        packed = delta;
    }
    tide_write_varint(w, size);
    tide_write_bytes(w, packed, size);
}

// Twice the ticks kept, each where it goes in the bigger ring, with what each
// player was sent of it. False, changing nothing, without the memory.
static bool grow_history(tide_server *s)
{
    const uint32_t old = s->w.history;
    const uint32_t grown = old * 2u;
    stored_frame *frames = calloc(grown, sizeof *frames);
    sent_mark **marks = calloc(s->players, sizeof *marks);
    stored_frame **client_frames = s->game->client_state ? calloc(s->players, sizeof *client_frames) : NULL;
    bool ok = frames != NULL && marks != NULL && (!s->game->client_state || client_frames);
    for (uint32_t p = 0; ok && p < s->players; p++) {
        if (!s->connections[p].used) continue;
        marks[p] = calloc(grown, sizeof *marks[p]);
        ok = marks[p] != NULL;
        if (ok && client_frames) {
            client_frames[p] = calloc(grown, sizeof *client_frames[p]);
            ok = client_frames[p] != NULL;
        }
    }
    if (!ok) {
        free(frames);
        for (uint32_t p = 0; marks && p < s->players; p++) free(marks[p]);
        for (uint32_t p = 0; client_frames && p < s->players; p++) free(client_frames[p]);
        free(client_frames);
        free(marks);
        return false;
    }
    for (uint32_t i = 0; i < old; i++) {
        if (s->frames[i].data) frames[s->frames[i].tick % grown] = s->frames[i];
    }
    for (uint32_t p = 0; p < s->players; p++) {
        connection *c = &s->connections[p];
        if (!c->used) continue;
        for (uint32_t i = 0; i < old; i++) {
            if (c->frame_sent[i].at > 0.0) marks[p][c->frame_sent[i].tick % grown] = c->frame_sent[i];
            if (client_frames && c->client_frames && c->client_frames[i].data) {
                client_frames[p][c->client_frames[i].tick % grown] = c->client_frames[i];
            }
        }
        if (client_frames) {
            free(c->client_frames);
            c->client_frames = client_frames[p];
        }
        free(c->frame_sent);
        c->frame_sent = marks[p];
    }
    free(marks);
    free(client_frames);
    free(s->frames);
    s->frames = frames;
    s->w.history = grown;
    return true;
}

// The ticks kept are a ring, which the tick about to be stored would go round
// onto the oldest. While a player who was sent the world still needs that
// one, the ring grows instead: a world that took longer to send than the
// ticks kept would otherwise be sent again and again, never caught up with.
static void keep_needed(tide_server *s, const uint32_t tick)
{
    const stored_frame *oldest = &s->frames[tick % s->w.history];
    if (!oldest->data) return;
    for (uint32_t p = 0; p < s->players; p++) {
        const connection *c = &s->connections[p];
        if (c->used && c->catching_up && oldest->tick >= c->frame_ack) {
            // Up to a point: a player that can't catch up in that long would
            // have the server keep ticks for it without end
            const uint32_t most = windows_for(s->desc.tick_rate).history * HISTORY_MOST;
            if (s->w.history < most && grow_history(s)) return;
            // Without the room, players catching up get the world again instead
            for (uint32_t q = 0; q < s->players; q++) s->connections[q].catching_up = false;
            return;
        }
    }
}

// A big command's turn has come: it's sent to every player in the match
// before it goes in a tick, so it's published, under a number, and the
// pending command remembers which. Full, nothing is published: it waits.
static void publish_command(tide_server *s, const uint32_t player, pending_command *cmd)
{
    if (cmd->published || s->published_count >= PUBLISHED_MOST) return;
    uint8_t *copy = malloc(cmd->size);
    if (!copy) return;
    memcpy(copy, cmd->data, cmd->size);
    const uint32_t number = ++s->next_published;
    s->published[s->published_count++] = (published){number, player, cmd->channel, cmd->sequence, cmd->size, copy, false, 0, {0}};
    cmd->published = number;
}

// Whether every player in the match has the published command (its sender
// too, who keeps its bytes and says so once told its number): those who are
// being sent a world, or catching up with one, get it with the ticks they
// lack, and don't hold it up
static bool everyone_has(const tide_server *s, const published *p)
{
    for (uint32_t i = 0; i < s->players; i++) {
        const connection *c = &s->connections[i];
        if (!c->used || !c->greeted || c->sending || c->catching_up) continue;
        if (!tide_players_has(&p->acked, i)) return false;
    }
    return true;
}

// Published commands nobody can need any more: in a tick whose frame has
// gone (frames are kept for the history, and longer for a player catching
// up: keep_needed), or from a player who left before it was dispatched
static void forget_published(tide_server *s)
{
    for (uint32_t i = 0; i < s->published_count; i++) {
        published *p = &s->published[i];
        const bool stale = p->dispatched ? s->tick - p->tick > s->w.history * HISTORY_MOST
                         : p->player == PUBLISHED_SHARD ? false
                                                        : !s->connections[p->player].used || !s->connections[p->player].greeted;
        if (!stale) continue;
        for (uint32_t k = 0; k < s->players; k++) {
            if (s->connections[k].download_number == p->number) stop_download(&s->connections[k]);
        }
        free(p->data);
        s->published[i--] = s->published[--s->published_count];
    }
}

// Commands into this tick, from each player in turn, in the server's order:
// a player's next in a channel, or an unordered one. A big one goes once
// every player has its bytes (publish_command): the frame names its
// download rather than carrying it.
static uint32_t dispatch_commands(tide_server *s, tide_writer *w)
{
    const tide_game *g = s->game;
    if (!g->command || !g->command_sequence) return 0;
    uint32_t count = 0;
    uint32_t examined = 0;
    while (count < TIDE_COMMANDS_PER_TICK && examined < s->players) {
        const uint32_t player = s->command_player;
        s->command_player = (player + 1u) % s->players;
        examined++;
        connection *c = &s->connections[player];
        if (!c->used || !c->greeted) continue;
        const tide_player_id sender = tide_player_from_index((int32_t)player);
        for (uint32_t i = 0; i < c->commands.count; i++) {
            pending_command *cmd = &c->commands.items[i];
            if (command_done(g, s->world, sender, cmd->channel, cmd->sequence)) {
                remove_command(&c->commands, i--);
                continue;
            }
            if (cmd->channel != TIDE_COMMAND_UNORDERED) {
                const uint32_t previous = g->command_sequence(s->world, sender, cmd->channel);
                if (previous == UINT32_MAX || cmd->sequence != previous + 1u) continue;
            }
            const published *p = NULL;
            if (big_command(cmd->size) && !g->client_state) {
                publish_command(s, player, cmd);
                p = cmd->published ? published_by_number(s, cmd->published) : NULL;
                if (!p || !everyone_has(s, p)) continue;
            }
            if (!g->command(s->world, sender, cmd->channel, cmd->sequence, cmd->data, cmd->size)) {
                remove_command(&c->commands, i);
                break;
            }
            if (g->client_state) {
                // Command payloads can contain secrets. Their permitted results
                // arrive through reconciliation; no other player gets the bytes.
                remove_command(&c->commands, i);
                count++;
                examined = 0;
                break;
            }
            tide_write_varint(w, player);
            tide_write_varint(w, cmd->sequence);
            tide_write_varint(w, cmd->size);
            if (p) {
                published *mine = published_by_number(s, p->number);
                mine->dispatched = true;
                mine->tick = s->tick;
                tide_write_varint(w, p->number);
            } else {
                tide_write_bytes(w, cmd->data, cmd->size);
            }
            remove_command(&c->commands, i);
            count++;
            examined = 0;
            break;
        }
    }
    return g->client_state ? 0u : count;
}

// Shard messages into this tick, in the order the server was handed them,
// each applied to the world before the tick runs (tide_shard_game.receive). A
// big one goes once every player has its bytes, as a big command does, and
// nothing after it goes before it. Under client_state players are sent none:
// what they change reaches them as permitted state.
static uint32_t dispatch_injected(tide_server *s, tide_writer *w)
{
    const tide_game *g = s->game;
    if (!g->shards || !s->injected_count) return 0;
    uint32_t count = 0;
    while (count < s->injected_count && count < TIDE_SHARD_MESSAGES_PER_TICK) {
        injected *m = &s->injected[count];
        const published *p = NULL;
        if (big_command(m->size) && !g->client_state) {
            if (!m->published && s->published_count < PUBLISHED_MOST) {
                uint8_t *copy = malloc(m->size);
                if (!copy) break;
                memcpy(copy, m->data, m->size);
                const uint32_t number = ++s->next_published;
                s->published[s->published_count++] =
                    (published){number, PUBLISHED_SHARD, UINT32_MAX, 0u, m->size, copy, false, 0, {0}};
                m->published = number;
            }
            p = m->published ? published_by_number(s, m->published) : NULL;
            if (!p || !everyone_has(s, p)) break;
        }
        g->shards->receive(s->world, m->data, m->size);
        if (!g->client_state) {
            tide_write_varint(w, m->size);
            if (p) {
                published *mine = published_by_number(s, p->number);
                mine->dispatched = true;
                mine->tick = s->tick;
                tide_write_varint(w, p->number);
            } else {
                tide_write_bytes(w, m->data, m->size);
            }
        }
        free(m->data);
        count++;
    }
    if (count) {
        memmove(s->injected, s->injected + count, (size_t)(s->injected_count - count) * sizeof *s->injected);
        s->injected_count -= count;
    }
    return g->client_state ? 0u : count;
}

bool tide_server_inject(tide_server *s, const uint8_t *data, const uint32_t size)
{
    if (!s->game->shards || !size || size > TIDE_SHARD_MESSAGE_LIMIT) return false;
    if (s->injected_count == s->injected_room) {
        const uint32_t room = s->injected_room ? s->injected_room * 2u : 16u;
        injected *grown = realloc(s->injected, (size_t)room * sizeof *grown);
        if (!grown) return false;
        s->injected = grown;
        s->injected_room = room;
    }
    uint8_t *copy = malloc(size);
    if (!copy) return false;
    memcpy(copy, data, size);
    s->injected[s->injected_count++] = (injected){copy, size, 0u};
    return true;
}

uint32_t tide_server_injected(const tide_server *s)
{
    return s->injected_count;
}

// How long a player's input waited here before its tick ran: from when it
// arrived to now, by its transport's clock, in WAITS. That wait is how much
// later the other players see what this one did than they had to, so the
// player is told, and keeps it short (keep_early). With no input for the tick,
// it's under 0 by a tick for each one missing, the newest it sent being that
// far back, whether that one came in time or not: a player whose inputs all
// come two ticks late is two ticks late, however long that has gone on. Of the
// ticks one update runs, the least is what's told.
//
// A player who has sent no input yet is told nothing: the ticks that run while
// its world is on its way are ticks it couldn't have sampled an input for, and
// counting them as late by as long as the world took had a client that joined
// a big world set its clock that far ahead, for inputs that were never late.
static void note_wait(tide_server *s, connection *c, const uint32_t tick, const bool has)
{
    double wait;
    if (has) wait = clock_of(&s->desc.transports[c->transport], s->now) - c->input_arrived[tick % s->w.inputs];
    else if (!c->sent_input) return;
    else if (c->newest_sent <= tick) wait = -(double)(tick + 1u - c->newest_sent) / (double)s->desc.tick_rate;
    else return; // A newer one came: this one went missing, which sending earlier doesn't mend
    const double parts = wait * WAITS + 1e-4;
    int32_t waited = parts <= -(double)WAIT_MOST ? -WAIT_MOST : parts >= (double)WAIT_MOST ? WAIT_MOST : (int32_t)parts;
    if ((double)waited > parts) waited--; // Down, under 0 too: never more than it waited
    if (!c->waited_known || c->waited_update != s->updates || waited < c->waited) c->waited = waited;
    c->waited_known = true;
    c->waited_update = s->updates;
}

static void store_client_frame(tide_server *s, connection *c, const void *before, const uint32_t tick,
                                tide_event_log *events_log)
{
    const tide_game *g = s->game;
    const tide_player_id player = tide_player_from_index((int32_t)(c - s->connections));
    for (uint32_t at = 0; events_log && at < events_log->size;) {
        tide_heard *heard = (tide_heard *)(events_log->bytes + at);
        at += heard->size;
        heard->tick = tick;
    }
    tide_reader source = {s->frame, frame_capacity(g), 8, false};
    const uint8_t source_parts = tide_read_u8(&source);
    const uint32_t events_at = source.at;
    const uint32_t events = source_parts & FRAME_EVENTS ? tide_read_varint(&source) : 0u;
    for (uint32_t i = 0; i < events && !source.failed; i++) { tide_read_u8(&source); tide_read_varint(&source); }
    const uint32_t events_size = source.at - events_at;
    if (source_parts & FRAME_INPUTS) read_players(&source);
    tide_player_set late = source_parts & FRAME_LATE ? read_players(&source) : (tide_player_set){0};
    // A late-input hint is useful only for this machine's real samples.
    tide_player_set own_late = {0};
    const uint32_t recipient = (uint32_t)(c - s->connections);
    if (tide_players_has(&late, recipient)) tide_players_add(&own_late, recipient);
    if (c->local && tide_players_has(&late, s->players)) tide_players_add(&own_late, s->players);
    late = own_late;
    tide_writer inputs = {.grows = true};
    tide_player_set mask = {0};
    uint8_t *all_inputs = NULL;
    size_t all_size = 0;
    if (g->filter_client_inputs && g->world_input && g->write_input && g->input_size) {
        const size_t slots = (size_t)s->players + 1u;
        if ((size_t)g->input_size > SIZE_MAX / 2u / slots) tide_out_of_memory();
        all_size = slots * g->input_size;
        all_inputs = malloc(2u * all_size);
        if (!all_inputs) tide_out_of_memory();
        for (uint32_t slot = 0; slot <= s->players; slot++) {
            g->world_input(before, slot, all_inputs + (size_t)slot * g->input_size);
            g->world_input(s->world, slot, all_inputs + all_size + (size_t)slot * g->input_size);
        }
        g->filter_client_inputs(before, player, all_inputs);
        g->filter_client_inputs(s->world, player, all_inputs + all_size);
    }
    uint8_t *packed = s->scratch + (size_t)2u * g->input_size;
    for (uint32_t slot = 0; g->world_input && g->write_input && slot <= s->players; slot++) {
        uint8_t *previous = all_inputs ? all_inputs + (size_t)slot * g->input_size : s->scratch;
        uint8_t *current = all_inputs ? previous + all_size : s->scratch + g->input_size;
        if (!all_inputs) {
            g->world_input(before, slot, previous);
            g->world_input(s->world, slot, current);
            if (g->filter_client_input) {
                g->filter_client_input(before, player, slot, previous);
                g->filter_client_input(s->world, player, slot, current);
            }
        }
        if (memcmp(previous, current, g->input_size) == 0) continue;
        const uint32_t n = g->write_input_delta
            ? g->write_input_delta(current, previous, packed, g->max_input_bytes)
            : g->write_input(current, packed, g->max_input_bytes);
        if (!n) continue;
        tide_players_add(&mask, slot);
        tide_write_varint(&inputs, n);
        tide_write_bytes(&inputs, packed, n);
    }
    free(all_inputs);
    uint32_t destroyed_count = g->tick_client_events ? client_event_causes(events_log, NULL, 0)
        : g->client_destroyed ? g->client_destroyed(s->world, before, player, NULL, 0) : 0;
    tide_entity *destroyed = destroyed_count ? malloc((size_t)destroyed_count * sizeof *destroyed) : NULL;
    if (destroyed_count && !destroyed) tide_out_of_memory();
    if (destroyed_count) destroyed_count = g->tick_client_events ? client_event_causes(events_log, destroyed, destroyed_count)
        : g->client_destroyed(s->world, before, player, destroyed, destroyed_count);
    tide_writer out = {.grows = true};
    tide_write_u64(&out, g->client_hash(s->world, player));
    const uint8_t parts = (uint8_t)((events ? FRAME_EVENTS : 0u) | (tide_players_any(&mask) ? FRAME_INPUTS : 0u)
        | (tide_players_any(&late) ? FRAME_LATE : 0u) | (destroyed_count ? FRAME_DESTROYED : 0u)
        | (events_log && events_log->size ? FRAME_LOCAL_EVENTS : 0u));
    tide_write_u8(&out, parts);
    if (events) tide_write_bytes(&out, s->frame + events_at, events_size);
    if (parts & FRAME_INPUTS) write_players(&out, &mask);
    if (parts & FRAME_LATE) write_players(&out, &late);
    tide_write_bytes(&out, inputs.data, inputs.size);
    if (destroyed_count) {
        tide_write_varint(&out, destroyed_count);
        for (uint32_t i = 0; i < destroyed_count; i++) {
            tide_write_varint(&out, destroyed[i].index);
            tide_write_varint(&out, destroyed[i].generation);
        }
    }
    if (parts & FRAME_LOCAL_EVENTS) {
        tide_write_varint(&out, events_log->size);
        tide_write_bytes(&out, events_log->bytes, events_log->size);
    }
    free(destroyed);
    free(inputs.data);
    if (out.overflow) tide_out_of_memory();
    if (out.size > 256u * PIECE) {
        // An arbitrarily large destruction batch travels through the ordinary
        // chunked correction to bound ordinary projected frame reassembly.
        out.size = 9;
        out.data[8] = FRAME_REPAIR;
    }
    stored_frame *f = &c->client_frames[tick % s->w.history];
    free(f->data);
    tide_event_log_free(&f->events);
    *f = (stored_frame){.tick = tick, .size = out.size, .data = out.data};
    if (events_log) { f->events = *events_log; *events_log = (tide_event_log){0}; }
}

// Runs one tick: joins and leaves, then the inputs that arrived for it, in
// slot order, the queued commands, then the systems. Clients do the same.
static void server_tick(tide_server *s)
{
    const tide_game *g = s->game;
    const uint32_t tick = s->tick;
    void *before = g->client_state ? calloc(1, g->world_size) : NULL;
    if (g->client_state && !before) tide_out_of_memory();
    if (before) g->copy_world(before, s->world);
    tide_writer w = {s->frame, frame_capacity(g), 0, false, false};
    tide_write_u64(&w, 0); // The hash, once the tick has run
    const uint32_t parts_at = w.size;
    tide_write_u8(&w, 0); // Which of what follows it has (FRAME_*), once it's known
    if (s->event_count) tide_write_varint(&w, s->event_count);
    for (uint32_t i = 0; i < s->event_count; i++) {
        const tide_player_id player = tide_player_from_index(s->events[i][1]);
        if (s->events[i][0] == EVENT_JOIN) g->player_joined(s->world, player);
        else g->player_left(s->world, player);
        tide_write_u8(&w, s->events[i][0]);
        tide_write_varint(&w, s->events[i][1]);
    }
    const uint8_t events = s->event_count ? FRAME_EVENTS : 0u;
    s->event_count = 0;

    // Each input that changed. One that's the same as the slot's last is left
    // out: keeping the last input is the same as setting it again. They come
    // after the bits that say which, so they're made apart first.
    tide_writer inputs = {s->frame + frame_capacity(g), frame_capacity(g), 0, false, false};
    tide_player_set mask = {0};
    tide_player_set late = {0};
    const uint32_t server_slot = s->players; // The server's input, after the players'
    const uint32_t max = g->max_input_bytes;
    uint8_t *input = s->input;
    uint32_t server_size = 0; // The server's input: its own player's, on the server's machine
    for (uint32_t p = 0; g->set_input && p < s->players; p++) {
        connection *c = &s->connections[p];
        if (!c->used) continue;
        const uint32_t slot = tick % s->w.inputs;
        note_wait(s, c, tick, c->input_tick[slot] == tick);
        uint32_t size = c->input_tick[slot] != tick
                                ? 0u
                                : canonical_input(g, c->inputs + (size_t)slot * max, c->input_size[slot], input, s->packed);
        if (size == 0) {
            tide_players_add(&late, p);
            if (c->local) tide_players_add(&late, server_slot);
            continue;
        }
        if (g->view_time && g->set_view_time) {
            tide_view_time view = g->view_time(input);
            const double at = (double)view.tick - 1.0 + (double)view.alpha;
            const double age = ((double)tick - at) / (double)s->desc.tick_rate;
            if (!view.valid || view.tick < 0 || !(view.alpha >= 0.0f && view.alpha <= 1.0f)
                || at < (double)c->snapshot_tick || at > (double)c->view_limit || at > (double)tick
                || !(g->max_rewind > 0.0f) || age > (double)g->max_rewind) {
                g->set_view_time(input, (tide_view_time){0});
                size = g->write_input(input, s->packed, max);
            }
        }
        if (c->local) {
            server_size = size;
            memcpy(input + g->input_size, s->packed, size);
        }
        uint8_t *last = c->inputs + (size_t)s->w.inputs * max;
        if (size == c->last_size && memcmp(last, s->packed, size) == 0) continue;
        memcpy(last, s->packed, size);
        c->last_size = size;
        put_input(s, &inputs, p, input, s->packed, size);
        g->set_input(s->world, tide_player_from_index((int32_t)p), input);
        tide_players_add(&mask, p);
    }
    const uint8_t *server_packed = input + g->input_size;
    if (server_size && !(server_size == s->server_last_size && memcmp(s->server_last, server_packed, server_size) == 0)) {
        memcpy(s->server_last, server_packed, server_size);
        s->server_last_size = server_size;
        g->read_input(server_packed, server_size, input);
        put_input(s, &inputs, server_slot, input, server_packed, server_size);
        g->set_server_input(s->world, input);
        tide_players_add(&mask, server_slot);
    }
    const bool any_input = tide_players_any(&mask);
    const bool any_late = tide_players_any(&late);
    if (any_input) write_players(&w, &mask);
    if (any_late) write_players(&w, &late);
    tide_write_bytes(&w, inputs.data, inputs.size);
    // Build the command entries apart, so their count precedes them. The
    // temporary input bytes have already been copied into this frame.
    tide_writer commands = {s->frame + frame_capacity(g), frame_capacity(g), 0, false, false};
    const uint32_t command_count = dispatch_commands(s, &commands);
    if (command_count) {
        tide_write_varint(&w, command_count);
        tide_write_bytes(&w, commands.data, commands.size);
    }
    // Shard messages after the commands, applied in the same order by players
    tide_writer shard_messages = {s->frame + frame_capacity(g), frame_capacity(g), 0, false, false};
    const uint32_t shard_count = dispatch_injected(s, &shard_messages);
    if (shard_count) {
        tide_write_varint(&w, shard_count);
        tide_write_bytes(&w, shard_messages.data, shard_messages.size);
    }
    s->frame[parts_at] = (uint8_t)(events | (any_input ? FRAME_INPUTS : 0u) | (any_late ? FRAME_LATE : 0u)
                                   | (command_count ? FRAME_COMMANDS : 0u) | (shard_count ? FRAME_SHARD : 0u));

    tide_event_log *journals = g->client_state && g->tick_client_events
        ? calloc(s->players, sizeof *journals) : NULL;
    if (g->client_state && g->tick_client_events && !journals) tide_out_of_memory();
    if (journals) g->tick_client_events(s->world, s->desc.jobs, journals);
    else g->tick(s->world, s->desc.jobs);
    s->tick++;
    s->ended = g->ended && g->ended(s->world);
    patch_u64(s->frame, hash_of(g, s->world, s->desc.jobs));

    keep_needed(s, tick);
    stored_frame *f = &s->frames[tick % s->w.history];
    uint8_t *data = realloc(f->data, w.size);
    if (!data) {
        free_world(g, before);
        free(before);
        for (uint32_t p = 0; journals && p < s->players; p++) tide_event_log_free(&journals[p]);
        free(journals);
        return;
    }
    memcpy(data, s->frame, w.size);
    free_world(g, f->world);
    free(f->world);
    *f = (stored_frame){.tick = tick, .size = w.size, .data = data, .world = before};
    for (uint32_t p = 0; before && p < s->players; p++) {
        connection *c = &s->connections[p];
        if (c->used && c->greeted) store_client_frame(s, c, before, tick, journals ? &journals[p] : NULL);
    }
    for (uint32_t p = 0; journals && p < s->players; p++) tide_event_log_free(&journals[p]);
    free(journals);
}

// How long the server has held the time it echoes to a player: from when the
// datagram that said it arrived to now, as it's sent back, in milliseconds.
// The player takes it out of its ping (see measure), which is the network's
// round trip and none of the server's wait for an update or a tick.
static uint32_t held_ms(const tide_server *s, const connection *c)
{
    const double held = clock_of(&s->desc.transports[c->transport], s->now) - c->their_arrived;
    return held > 0.0 ? (held < 60.0 ? (uint32_t)(held * 1000.0 + 0.5) : 60000u) : 0u;
}

static void send_welcome(tide_server *s, connection *c)
{
    uint8_t data[64];
    tide_writer w = {data, sizeof data, 0, false, false};
    header(&w, MSG_WELCOME);
    tide_write_u32(&w, c->nonce);
    tide_write_varint(&w, (uint32_t)(c - s->connections));
    tide_write_u8(&w, (c->local ? 1u : 0u) | (c->snapshot_stream ? 2u : 0u));
    tide_write_u32(&w, s->desc.tick_rate);
    tide_write_u32(&w, s->tick);
    // What it's being sent, or nothing yet (number 0): the world comes once it's written back
    tide_write_u32(&w, c->snapshot_tick);
    tide_write_u32(&w, c->sending ? c->snapshot_number : 0u);
    tide_write_u8(&w, c->snapshot_kind);
    tide_write_u32(&w, c->sending ? c->snapshot_size : 0u);
    tide_write_u32(&w, c->sending ? c->chunk_count : 0u);
    tide_write_u16(&w, millis16(s->now));
    tide_write_u16(&w, c->their_time);
    tide_write_u16(&w, (uint16_t)held_ms(s, c));
    tide_write_u64(&w, s->cookies[c - s->connections]);
    send_packet(&s->desc.transports[c->transport], c->address, &w, &s->traffic);
    c->view_limit = s->tick;
}

static void send_chunks(tide_server *s, connection *c)
{
    const double again = resend_after(c->rtt_ms);
    uint32_t sent = 0;
    for (uint32_t i = c->chunk_ack; i < c->chunk_count && sent < CHUNKS_PER_UPDATE; i++) {
        if (i > c->chunk_ack && i - c->chunk_ack <= 64u && (c->chunk_mask >> (i - c->chunk_ack - 1u) & 1u)) continue;
        if (c->chunk_sent[i] > 0.0 && s->now - c->chunk_sent[i] < again) continue;
        const uint32_t from = i * CHUNK;
        const uint32_t size = c->snapshot_size - from < CHUNK ? c->snapshot_size - from : CHUNK;
        tide_writer w = {s->packet, TIDE_NET_MTU, 0, false, false};
        header(&w, MSG_CHUNK);
        tide_write_u32(&w, c->snapshot_number);
        tide_write_u32(&w, i);
        tide_write_u16(&w, (uint16_t)size);
        tide_write_bytes(&w, c->snapshot + from, size);
        send_packet(&s->desc.transports[c->transport], c->address, &w, &s->traffic);
        c->chunk_sent[i] = s->now > 0.0 ? s->now : 1e-9;
        sent++;
    }
}

static void start_server_packet(const tide_server *s, const connection *c, tide_writer *w, uint32_t *count_at)
{
    *w = (tide_writer){s->packet, TIDE_NET_MTU, 0, false, false};
    header(w, MSG_SERVER);
    tide_write_u16(w, millis16(s->now));
    tide_write_u16(w, c->their_time);
    tide_write_varint(w, held_ms(s, c));
    tide_write_varint(w, s->tick);
    write_signed(w, (int32_t)(c->newest_input - s->tick)); // The newest input it has
    write_signed(w, c->waited_known ? c->waited : WAIT_UNKNOWN); // ...and how long they waited for the ticks it last ran (note_wait)
    // What it has of the player's upload, in a game with commands: the
    // chunks of one on its way (as a CLIENT says a world's), or that the
    // last came whole, and its download's number once it's published
    if (s->game->command) {
        if (c->upload_number) {
            uint32_t ack;
            uint64_t mask;
            chunks_had(c->upload_have, c->upload_chunks, &ack, &mask);
            tide_write_u8(w, UPLOAD_PART);
            tide_write_varint(w, c->upload_number);
            tide_write_varint(w, ack);
            tide_write_u64(w, mask);
        } else if (c->upload_done) {
            tide_write_u8(w, UPLOAD_WHOLE);
            tide_write_varint(w, c->upload_done);
            tide_write_varint(w, upload_done_published(s, c));
        } else {
            tide_write_u8(w, UPLOAD_NONE);
        }
    }
    *count_at = w->size;
    tide_write_u8(w, 0);
}

// Chunks of the big command on its way down to `c`: the lowest published one
// it needs and hasn't said it has all of, paced (COMMAND_CHUNKS_PER_UPDATE),
// each again once it should have been acknowledged. Its own waits until it's
// been told its number, as it has the bytes already.
static void send_downloads(tide_server *s, connection *c)
{
    if (s->game->client_state) return;
    if (!c->download_number) {
        const published *next = NULL;
        for (uint32_t i = 0; i < s->published_count; i++) {
            const published *p = &s->published[i];
            const uint32_t player = (uint32_t)(c - s->connections);
            if (p->number <= c->download_done || !needs_published(c, p) || tide_players_has(&p->acked, player)) continue;
            if (own_upload(c, player, p)) continue; // It has the bytes: told the number, it says so
            if (!next || p->number < next->number) next = p;
        }
        if (!next) return;
        c->download_sent = calloc(chunks_of(next->size), sizeof *c->download_sent);
        if (!c->download_sent) return;
        c->download_number = next->number;
        c->download_ack = 0;
        c->download_mask = 0;
    }
    const published *p = published_by_number(s, c->download_number);
    if (!p) {
        stop_download(c);
        return;
    }
    const double again = resend_after(c->rtt_ms);
    const uint32_t chunks = chunks_of(p->size);
    uint32_t sent = 0;
    for (uint32_t i = c->download_ack; i < chunks && sent < COMMAND_CHUNKS_PER_UPDATE; i++) {
        if (i > c->download_ack && i - c->download_ack <= 64u && (c->download_mask >> (i - c->download_ack - 1u) & 1u)) continue;
        if (c->download_sent[i] > 0.0 && s->now - c->download_sent[i] < again) continue;
        const uint32_t from = i * CHUNK;
        const uint32_t size = p->size - from < CHUNK ? p->size - from : CHUNK;
        tide_writer w = {s->packet, TIDE_NET_MTU, 0, false, false};
        header(&w, MSG_DOWNLOAD);
        tide_write_varint(&w, p->number);
        tide_write_varint(&w, p->size);
        tide_write_varint(&w, i);
        tide_write_u16(&w, (uint16_t)size);
        tide_write_bytes(&w, p->data + from, size);
        send_packet(&s->desc.transports[c->transport], c->address, &w, &s->traffic);
        c->download_sent[i] = s->now > 0.0 ? s->now : 1e-9;
        sent++;
    }
}

// A piece's own bytes in a SERVER: how many ticks before the packet's its
// tick is, its frame's size, and which piece of it, when it has several.
static uint32_t piece_header(const tide_server *s, const uint32_t tick, const uint32_t size)
{
    return varint_size(s->tick - tick) + varint_size(size) + (size > PIECE ? varint_size((size - 1u) / PIECE) : 0u);
}

// The ticks it lacks, in pieces, as many packets as it takes (up to a limit).
static void send_frames(tide_server *s, connection *c)
{
    // Too far behind for the ticks kept: the whole world again. One catching
    // up with the world it was sent has them all kept for it (keep_needed).
    if (!c->catching_up && s->tick - c->frame_ack >= s->w.history) {
        start_snapshot(s, c);
        return;
    }
    c->view_limit = s->tick;
    // Caught up with the world it was sent: from here, a player that falls
    // behind gets the world again, rather than the server keeping every tick
    if (c->catching_up && s->tick - c->frame_ack <= windows_for(s->desc.tick_rate).history / 2u) c->catching_up = false;
    // While what it lacks fits in one datagram, each one carries all of it:
    // a lost one costs a frame, not a round trip. Beyond that, what was sent
    // goes again once it should have been acknowledged.
    uint32_t lacking = 0;
    for (uint32_t tick = c->frame_ack; tick < s->tick && lacking <= TIDE_NET_MTU; tick++) {
        const stored_frame *f = &(c->client_frames ? c->client_frames : s->frames)[tick % s->w.history];
        if (f->tick == tick) lacking += f->size + piece_header(s, tick, f->size) * ((f->size + PIECE - 1u) / PIECE);
    }
    const double again = lacking + 32u <= TIDE_NET_MTU ? 0.0 : resend_after(c->rtt_ms);
    const tide_transport *t = &s->desc.transports[c->transport];
    tide_writer w;
    uint32_t count_at;
    uint32_t pieces = 0;
    uint32_t packets = 0;
    start_server_packet(s, c, &w, &count_at);
    for (uint32_t tick = c->frame_ack; tick < s->tick && packets < PACKETS_PER_UPDATE; tick++) {
        if (tick > c->frame_ack && tick - c->frame_ack <= 64u && (c->frame_mask >> (tick - c->frame_ack - 1u) & 1u)) continue;
        sent_mark *mark = &c->frame_sent[tick % s->w.history];
        if (mark->tick == tick && mark->at > 0.0 && s->now - mark->at < again) continue;
        const stored_frame *f = &(c->client_frames ? c->client_frames : s->frames)[tick % s->w.history];
        if (f->tick != tick || !f->data) continue;
        const uint32_t count = (f->size + PIECE - 1u) / PIECE;
        uint32_t k = mark->tick == tick ? mark->next : 0u;
        for (; k < count && packets < PACKETS_PER_UPDATE; k++) {
            const uint32_t from = k * PIECE;
            const uint32_t size = f->size - from < PIECE ? f->size - from : PIECE;
            if (w.size + piece_header(s, tick, f->size) + size > TIDE_NET_MTU || pieces == 255u) {
                s->packet[count_at] = (uint8_t)pieces;
                send_packet(t, c->address, &w, &s->traffic);
                packets++;
                if (packets == PACKETS_PER_UPDATE) break;
                start_server_packet(s, c, &w, &count_at);
                pieces = 0;
            }
            tide_write_varint(&w, s->tick - tick);
            tide_write_varint(&w, f->size);
            if (count > 1) tide_write_varint(&w, k);
            tide_write_bytes(&w, f->data + from, size);
            pieces++;
        }
        *mark = (sent_mark){.tick = tick, .at = k == count ? (s->now > 0.0 ? s->now : 1e-9) : 0.0,
                            .next = k == count ? 0u : k};
    }
    s->packet[count_at] = (uint8_t)pieces;
    if (packets < PACKETS_PER_UPDATE) send_packet(t, c->address, &w, &s->traffic); // Always one, for the times and its inputs' wait
}

tide_server *tide_server_create(const tide_server_desc *desc, const double now)
{
    if (!desc || !desc->game || tide_game_players(desc->game) > TIDE_PLAYERS_MOST
        || !frame_capacity(desc->game) || (desc->game->client_state
        && (!desc->game->load_client_state || !desc->game->client_hash
            || (desc->game->tick_client_events && !desc->game->check_client_events)))) return NULL;
    tide_server *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->desc = *desc;
    s->game = desc->game;
    const tide_game *g = s->game;
    if (s->desc.tick_rate == 0) s->desc.tick_rate = g->tick_rate ? g->tick_rate : 60u;
    s->w = windows_for(s->desc.tick_rate);
    s->players = tide_game_players(g);
    s->connections = calloc(s->players, sizeof *s->connections);
    s->events = calloc((size_t)EVENTS_PER_PLAYER * s->players, sizeof *s->events);
    s->cookies = calloc(s->players, sizeof *s->cookies);
    s->identities = calloc(s->players, sizeof *s->identities);
    s->digests = calloc(s->players, sizeof *s->digests);
    s->away_since = calloc(s->players, sizeof *s->away_since);
    s->kicks = calloc(s->players, sizeof *s->kicks);
    s->handover = malloc(handover_capacity(s->players));
    s->frames = calloc(s->w.history, sizeof *s->frames);
    s->published = calloc(PUBLISHED_MOST, sizeof *s->published);
    s->world = calloc(1, g->world_size);
    s->frame = malloc(2u * (size_t)frame_capacity(g));
    s->packet = malloc(TIDE_NET_MTU);
    s->input = calloc(1, (size_t)g->input_size + g->max_input_bytes + 1u);
    s->packed = malloc((size_t)g->max_input_bytes + 1u);
    s->server_last = malloc((size_t)g->max_input_bytes + 1u);
    s->scratch = calloc(1, scratch_size(g));
    if (!s->connections || !s->events || !s->cookies || !s->identities || !s->digests || !s->away_since || !s->kicks || !s->handover
        || !s->frames || !s->published || !s->world || !s->frame || !s->packet || !s->input || !s->packed
        || !s->server_last || !s->scratch) {
        tide_server_destroy(s);
        return NULL;
    }
    const float dt = desc->dt > 0.0f ? desc->dt : 1.0f / (float)s->desc.tick_rate;
    // A match's seed, in a game that has one: from the system where the host
    // has it to give, so no two matches go the same, or else the one the host
    // picked; 31 bits, as Time.seed is
    s->seed = desc->seed & 0x7FFFFFFFu;
    if (!desc->world && g->seeded) {
        s->seed = desc->seed;
        if (desc->entropy) {
            const uint64_t entropy = desc->entropy();
            s->seed = (uint32_t)(entropy ^ (entropy >> 32));
        }
        s->seed &= 0x7FFFFFFFu;
    }
    if (desc->world) g->copy_world(s->world, desc->world);
    else g->start(s->world, dt, desc->start, s->seed);
    s->dt = dt;
    s->start_kind = desc->world ? START_UNKNOWN : desc->start ? START_GIVEN : START_MAIN;
    if (s->start_kind == START_GIVEN) {
        s->start = malloc(g->start_size ? g->start_size : 1u);
        if (s->start) memcpy(s->start, desc->start, g->start_size);
        else s->start_kind = START_UNKNOWN;
    }
    if (desc->world) s->present = desc->players;
    s->present_until = now + PRESENT_SECONDS;
    for (uint32_t i = 0; desc->world && i < s->players; i++) {
        if (tide_players_has(&desc->leaving, i)) { // Gone with the last server
            add_event(s, EVENT_LEAVE, i);
            s->away_since[i] = now > 0.0 ? now : 1e-9;
        }
        if (desc->digests) s->digests[i] = desc->digests[i];
        if (desc->identities) memcpy(s->identities[i], desc->identities[i], 32);
    }
    s->closed = desc->closed;
    s->now = now;
    s->started = !desc->wait_for_first;
    s->clock_start = now;
    s->random = tide_hash(&s, sizeof s) ^ tide_hash(&now, sizeof now);
    return s;
}

void tide_server_destroy(tide_server *s)
{
    if (!s) return;
    for (uint32_t i = 0; s->connections && i < s->players; i++) {
        connection *c = &s->connections[i];
        if (!c->used) continue;
        send_bye(s, c->transport, c->address);
        free_connection(s, c);
    }
    // A goodbye can be lost: a match that ended says so where its players
    // would meet to take it over, so that none does (host migration)
    for (uint32_t i = 0; i < 2; i++) {
        const tide_transport *t = &s->desc.transports[i];
        if (s->ended && t->end) t->end(t->self);
        if (t->close) t->close(t->self);
    }
    for (uint32_t i = 0; s->frames && i < s->w.history; i++) {
        free(s->frames[i].data);
        free_world(s->game, s->frames[i].world);
        free(s->frames[i].world);
    }
    free(s->frames);
    if (s->published) free_published(s);
    free(s->published);
    free_world(s->game, s->world);
    free(s->world);
    free(s->frame);
    free(s->packet);
    free(s->input);
    free(s->start);
    free(s->scratch);
    free(s->packed);
    free(s->server_last);
    free(s->connections);
    free(s->events);
    free(s->cookies);
    free(s->identities);
    free(s->digests);
    free(s->away_since);
    free(s->kicks);
    free(s->handover);
    for (uint32_t i = 0; i < s->injected_count; i++) free(s->injected[i].data);
    free(s->injected);
    free(s);
}

// Host migration: what a player needs to take the match over when the server
// goes, or to join the one who did, every update while it's new, and every
// HANDOVER_EVERY after. The record is the server's own player (plus one; 0 for
// none), whether the match is open, the room's code and key, who's in the
// match and each one's digest. It goes in parts of a datagram each, as a match
// of many players has more digests than one holds: each part says the
// record's version and size, and which part it is.
static void send_handover(tide_server *s)
{
    if (!hands_over(s)) return;
    tide_player_set in_match = s->present;
    uint32_t own = 0; // This machine's player, who leaves with it
    for (uint32_t i = 0; i < s->players; i++) {
        if (!s->connections[i].used) continue;
        tide_players_add(&in_match, i);
        if (s->connections[i].local) own = i + 1u;
    }
    const bool open = !s->closed && s->desc.transports[1].send;
    tide_writer w = {s->handover, handover_capacity(s->players), 0, false, false};
    tide_write_varint(&w, own);
    tide_write_u8(&w, open ? 1u : 0u);
    tide_write_bytes(&w, s->room_code, TIDE_ROOM_CODE_LENGTH);
    const uint32_t key = (uint32_t)strlen(s->room_key);
    tide_write_u8(&w, (uint8_t)key);
    tide_write_bytes(&w, s->room_key, key);
    write_players(&w, &in_match);
    for (uint32_t i = 0; i < s->players; i++) {
        if (tide_players_has(&in_match, i)) tide_write_u64(&w, s->digests[i]);
    }
    const uint64_t hash = tide_hash(s->handover, w.size);
    if (hash != s->handover_hash || !s->handover_version) {
        s->handover_hash = hash;
        s->handover_version++;
        s->handover_changed = s->now;
    }
    if (s->now - s->handover_changed > HANDOVER_EVERY && s->now - s->handover_sent < HANDOVER_EVERY) return;
    s->handover_sent = s->now;
    const uint32_t parts = (w.size + HANDOVER_PART - 1u) / HANDOVER_PART;
    for (uint32_t part = 0; part < parts; part++) {
        uint8_t data[32u + HANDOVER_PART];
        _Static_assert(sizeof data <= TIDE_NET_MTU, "a handover's part fits one datagram");
        tide_writer p = {data, sizeof data, 0, false, false};
        header(&p, MSG_HANDOVER);
        tide_write_u32(&p, s->handover_version);
        tide_write_varint(&p, w.size);
        tide_write_varint(&p, part);
        const uint32_t from = part * HANDOVER_PART;
        tide_write_bytes(&p, s->handover + from, w.size - from < HANDOVER_PART ? w.size - from : HANDOVER_PART);
        for (uint32_t i = 0; i < s->players; i++) {
            const connection *c = &s->connections[i];
            if (c->used && !c->local) send_packet(&s->desc.transports[c->transport], c->address, &p, &s->traffic);
        }
    }
}

void tide_server_update(tide_server *s, const double now)
{
    s->now = now;
    s->updates++;
    for (uint32_t i = 0; i < 2; i++) server_receive(s, i);
    for (uint32_t i = 0; i < s->players; i++) {
        connection *c = &s->connections[i];
        if (c->used && now - c->last_heard > (c->greeted ? TIMEOUT : HELLO_TIMEOUT)) drop_connection(s, c);
    }
    // Players of the world it went on from who didn't come back in time left
    if (now >= s->present_until && tide_players_any(&s->present)) {
        for (uint32_t i = 0; i < s->players; i++) {
            if (!tide_players_has(&s->present, i)) continue;
            add_event(s, EVENT_LEAVE, i);
            s->away_since[i] = now > 0.0 ? now : 1e-9;
        }
        s->present = (tide_player_set){0};
    }
    if (s->started && !s->ended) {
        const uint64_t due = (uint64_t)s->clock_base + ticks_in(now - s->clock_start, s->desc.tick_rate);
        for (uint32_t n = 0; s->tick < due && n < MAX_TICKS && !s->ended; n++) server_tick(s);
        if (s->tick < due && !s->ended) { // Stalled: drop the time instead of catching up
            s->clock_base = s->tick;
            s->clock_start = now;
        }
    }
    for (uint32_t i = 0; i < s->players; i++) {
        connection *c = &s->connections[i];
        if (!c->used) continue;
        if (s->ended) { // Every update, until they've all gone
            send_bye(s, c->transport, c->address);
        } else if (c->welcoming) { // Until it writes back
            send_welcome(s, c);
        } else if (c->sending) {
            send_welcome(s, c);
            send_chunks(s, c);
        } else {
            send_frames(s, c);
            if (s->published_count) send_downloads(s, c);
        }
    }
    if (s->published_count) forget_published(s);
    if (!s->ended) send_handover(s);
}

const void *tide_server_world(const tide_server *s)
{
    return s->world;
}

uint32_t tide_server_tick(const tide_server *s)
{
    return s->tick;
}

uint32_t tide_server_player_count(const tide_server *s)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < s->players; i++) n += s->connections[i].used ? 1u : 0u;
    return n;
}

void tide_server_recovery_state(const tide_server *s, tide_server_recovery *out)
{
    uint64_t *digests = out->digests;
    uint8_t (*identities)[32] = out->identities;
    *out = (tide_server_recovery){.digests = digests, .identities = identities};
    out->players = s->present;
    out->closed = s->closed;
    out->tick_rate = s->desc.tick_rate;
    out->seed = s->seed;
    out->started = s->started;
    out->dt = s->dt;
    for (uint32_t i = 0; i < s->players; i++) {
        if (s->connections[i].used && s->connections[i].greeted) tide_players_add(&out->players, i);
        out->digests[i] = s->digests[i];
        memcpy(out->identities[i], s->identities[i], 32);
    }
    for (uint32_t i = s->event_count; i-- > 0;) {
        const uint32_t player = s->events[i][1];
        if (s->events[i][0] == EVENT_JOIN) tide_players_remove(&out->players, player);
        else {
            tide_players_add(&out->players, player);
            tide_players_add(&out->leaving, player);
        }
    }
}

bool tide_server_player_identity(const tide_server *s, const tide_player_id player, uint8_t identity[32])
{
    if (!identity) return false;
    memset(identity, 0, 32);
    if (!s || !s->desc.identity || !player.id || player.id > s->players || !s->digests[player.id - 1u]) return false;
    memcpy(identity, s->identities[player.id - 1u], 32);
    const uint8_t zero[32] = {0};
    return memcmp(identity, zero, 32) != 0;
}

bool tide_server_kick(tide_server *s, const tide_player_id player, const char *message)
{
    const int32_t index = tide_player_index(player);
    if (index < 0 || (uint32_t)index >= s->players) return false;
    connection *c = &s->connections[index];
    if (!c->used || c->local) return false;
    kicked *k = &s->kicks[index];
    *k = (kicked){.used = true, .cookie = s->cookies[index], .transport = c->transport, .address = c->address,
                  .nonce = c->nonce};
    copy_message(k->message, message, TIDE_MESSAGE_BYTES - 1u);
    send_kicked(s, k);
    drop_connection(s, c);
    return true;
}

// ---------------------------------------------------------------------------
// Client

// Host migration: what the server last said a player needs to take the match
// over when it goes, or to join the one who did (see send_handover).
typedef struct handover {
    uint32_t version; // 0: none yet
    uint32_t own;     // The server's own player plus one, 0 for none
    bool open;
    char code[TIDE_ROOM_CODE_LENGTH + 1];
    char key[TIDE_ROOM_KEY_LENGTH + 1];
    tide_player_set in_match;
    uint64_t *digests; // Owned, sized to this game; 0 for players outside the match
} handover;

// A tick's frame as it comes, in pieces. The pieces it has are a bit each,
// after its bytes: a frame can take as many pieces as the game's inputs make.
typedef struct pending_frame {
    uint32_t tick;
    uint32_t size;
    uint32_t pieces;
    uint32_t had;   // Pieces it has so far
    uint8_t *data;  // `size` bytes, then a byte per piece: 1 once it came
    // The downloads its commands name (big ones, see DOWNLOAD), read once
    // its pieces are all there: it's complete once it has those too
    uint32_t names[TIDE_COMMANDS_PER_TICK + TIDE_SHARD_MESSAGES_PER_TICK];
    uint8_t name_count;
    bool read;
    tide_entity *destroyed;
    uint32_t destroyed_count;
    tide_event_log events;
    bool invalid;
} pending_frame;

static void drop_frame(pending_frame *f)
{
    free(f->data);
    free(f->destroyed);
    tide_event_log_free(&f->events);
    *f = (pending_frame){0};
}

// A big command on its way down from the server, or whole, kept until the
// tick that names it is run (DOWNLOADS_MOST of them at most)
typedef struct download {
    uint32_t number;
    uint32_t size;
    uint32_t chunks;
    uint32_t had;
    uint8_t *data;
    uint8_t *have;
} download;

static void free_download(download *d)
{
    free(d->data);
    free(d->have);
    *d = (download){0};
}

static bool download_whole(const download *d)
{
    return d->number && d->had == d->chunks;
}

// How many of the server's ticks a client runs at one moment (in a frame),
// so that catching up with seconds of a match is spread over its frames
// (catch_up_room).
typedef struct catch_up {
    uint32_t most;   // Ticks, at the most
    uint32_t least;  // ...and with a clock, this many whatever they cost...
    uint64_t time;   // ...then no more than this many nanoseconds' worth
} catch_up;

struct tide_client {
    tide_client_desc desc;
    tide_local_data_fn local_receive;
    void *local_user;
    bool local_subscribed;
    double local_sent;
    const tide_game *game;
    tide_session_state state;
    tide_disconnect_reason reason;
    char message[TIDE_MESSAGE_BYTES]; // A kick's
    uint32_t nonce;
    double created;
    double last_hello;
    double last_heard;
    double now;
    bool welcomed;
    bool asset_failed; // Retry self-contained state after an unavailable asset reference.
    int32_t player;
    bool server_input_mine; // On the server's machine: its input is the server's too
    uint32_t tick_rate;
    uint32_t server_tick; // The server's, as it last said while sending a world
    // ...and as it last said at all: the ticks before it are the ones it has
    // run. The update it said so in, of this machine's, and what the answer
    // that said it took to be read, if it echoed a time (as rtt_ms)
    uint32_t heard_tick;
    uint64_t heard_update;
    uint32_t heard_trip_ms;
    bool heard_timed;
    uint64_t updates; // Updates it has had
    uint16_t their_time;
    uint32_t rtt_ms;  // What an answer takes to be read: from the update that sent to the update that reads it
    uint32_t ping_ms; // ...and the network's part of that alone (measure)
    double arrived;   // When the datagram being read arrived, by the transport's clock (arrived_of)
    // The times it last sent, as it said them, and when by the transport's clock
    struct {
        uint16_t said;
        double at;
    } sent[SENT_TIMES];
    uint32_t sent_count;
    uint32_t input_ack; // The server has every input before this tick
    command_queue commands;
    bool commands_rebase; // Pending intent carried to another server's authoritative snapshot
    bool commands_need_world; // A changed layout needs the remote server's current sequence first
    // A big command on its way up, one at a time: the upload's number (its
    // count of them, from 1), which pending command (by channel and
    // sequence), its chunks as the server last said it has them, and when
    // each was sent
    uint32_t upload_number;
    uint32_t upload_count; // Uploads so far: the next's number
    uint32_t upload_channel;
    uint32_t upload_sequence;
    uint32_t upload_ack;
    uint64_t upload_mask;
    double *upload_sent;
    uint32_t upload_chunks;
    // Big commands on their way down, or whole, for the ticks that name them
    // (see `download`), and the last it had whole, which it says until the
    // next comes
    download downloads[DOWNLOADS_MOST];
    uint32_t download_done;

    // What's being received: the world, or the hashes of its pages
    bool receiving;
    uint32_t snapshot_tick;
    uint32_t snapshot_number;
    uint8_t snapshot_kind;
    uint32_t snapshot_size;
    uint32_t chunk_count;
    uint32_t chunks_had;
    uint8_t *snapshot;
    uint8_t *chunk_have;
    uint32_t got_number; // The last thing it had whole
    bool need_snapshot;
    uint32_t repair_tick; // Exact projected endpoint pending authoritative correction; 0 otherwise.
    bool repair_stream;
    bool snapshot_stream;
    uint32_t resyncs;
    uint64_t world_bytes;
    traffic traffic;
    uint64_t cookie;

    // The world it builds the one being received from: the one it was given
    // (tide_client_desc.base) until it has its own, then a snapshot of its
    // own. Once the hashes of the world at answered_tick came, `lacks` says
    // which of their pages it lacks.
    void *base;
    bool base_failed; // A world built from one came out wrong: the next comes whole
    bool answered;
    uint32_t answered_tick;
    uint32_t answered_number;
    uint32_t lacks_size;
    uint8_t lacks[LACKS_BYTES];
    // The world it was given (tide_client_desc.base) as it was, until its
    // first world comes, where its host listens: one this machine had, so what
    // the match's world has beyond it is all that's new here (hear_arrived).
    void *had;

    // Snapshots: the world at each tick from `verified`, the last the server
    // confirmed, to `ahead`, the last it predicted, at worlds[tick % ring], and
    // the one before them, which views blend from
    bool loaded;
    uint32_t first_tick; // The tick of the world that arrived last: nothing before it
    void **worlds;
    uint32_t ring;
    uint32_t verified;
    uint32_t ahead;
    double verified_view; // Confirmed presentation time, held when no newer confirmed state exists.
    tide_view_time presented_view;
    windows w;              // From the match's tick rate, once welcomed
    pending_frame *frames;

    // Its own inputs, packed, by tick. It samples them for `sampled` on, as
    // its clock says, however far it runs: `delay` ticks short of them
    // (tide_prediction), so an input is sampled that many ticks before the
    // tick it's for is run.
    uint32_t *input_tick;
    uint16_t *input_size;
    uint8_t *inputs;
    uint32_t sampled;       // The next tick to sample an input for: at least `ahead`
    // The first of the inputs it sampled in a row up to `sampled`: it sampled
    // none for the ticks before, which the server runs without one (skip_inputs)
    uint32_t sampled_from;
    // Behind what the server ran by more than an update's worth: its world
    // just came, as old as it took to come, or this machine stopped for a
    // while. It runs what the server sent, so many ticks an update, and
    // predicts nothing until it's level (fall_behind, play)
    bool catching_up;
    // When it last played, and how long after the time before that: an
    // update's length is the shorter of its last two. What it may run of the
    // server's ticks at that moment (catch_up_room), how many it has, in every
    // update at that moment, and when it ran the first of them, with a clock
    double played;
    double took_before;
    catch_up room;
    uint32_t ran_now;
    uint64_t began_now;
    uint32_t far;       // Updates in a row the server's word had its clock past what it can sample (clock_back)
    uint32_t delay;         // Ticks short of `sampled` it runs
    uint32_t delay_cap;     // The most delay it takes, so its inputs fit their ring
    bool predicts;          // It runs ticks the server hasn't confirmed (TIDE_PREDICTION_ROLLBACK)
    double tick_cost;       // Seconds one of its predicted ticks takes, a moving mean (tide_client_desc.now_ns)
    // What running ticks again costs, measured as it happens (note_tick_cost):
    // the first tick of a rollback, which has to find or make what C keeps
    // of the world again, as the last REPLAY_SAMPLES rollbacks had it (their
    // median is what one costs), while the ticks after it cost what a
    // predicted tick does; when the last rollback was; what this update's
    // rollbacks took; and what they take an update on the whole, a moving
    // mean over LOAD_SECONDS (adjust_delay). `rerunning` while predict_from
    // runs ticks again, and which of them it's at.
    double rerun_firsts[REPLAY_SAMPLES];
    uint32_t rerun_count;
    double rerun_at;
    double resim_now;
    double resim_load;
    bool rerunning;
    uint32_t rerun_tick;
    double delay_at;        // When the delay was last looked at, for that mean
    double lead_mean;       // How far ahead of what's confirmed it's been of late, while level with the server
    double rollback_now;    // What a rollback would cost now, as last reckoned, and the most one may take: for the status
    double most_now;
    uint32_t again;         // Predicted ticks it ran again so far
    uint32_t ran;           // Ticks it ran so far, all of them...
    uint32_t ran_ahead;     // ...and the predicted ones among them, the first time and again
    // Other players' inputs are guesses past `verified`: their last ones for
    // `guess` ticks, and from there on as if they'd let go
    // (tide_game.let_go_inputs). `let_go` is the first tick it ran that way,
    // when that changed an input: the ticks from it on are other worlds than
    // the same ticks with the last inputs kept. UINT32_MAX: none is.
    uint32_t guess;
    uint32_t let_go;
    uint32_t input_guessed; // First tick whose input a custom guess changed
    double slack_since;     // Since when it's had room to spare, to take delay back after a while
    double delay_hold_until; // No change of delay until then: the last one takes a round trip to show

    // The last pair actually drawn. Recording it is free; replay keeps old
    // endpoints only when it is about to replace one of them.
    bool presented;
    bool presented_previous;
    uint32_t presented_tick;
    float presented_alpha;
    void *correction_before;
    void *correction_previous;
    void *correction_after;
    void *correction_after_previous;
    bool correction_pending;
    bool correction_reset;

    // Its clock: the tick it should have reached is clock_base plus the ticks since clock_start
    double clock_start;
    int64_t clock_base;
    int32_t waited; // How long its inputs waited at the server before their ticks ran, in WAITS, as the server last said
    bool waited_known;
    uint32_t waited_tick; // ...and the server's tick as it said so: the ticks before it are the ones it's about
    // What the server says only counts once it's about inputs sent by the
    // clock as it is now: from the first tick sampled when its time came,
    // after a change (`early_settling` until that tick is known)
    bool early_settling;
    uint32_t early_tick;
    // ...and the least it said since `early_since`, while the client watches
    // (keep_early): one input that's nearly late is what being early is for
    bool early_watching;
    int32_t early_least;
    double early_since;
    uint32_t early_updates; // Updates it has had since then...
    double frame;           // ...and so how long its frames were, the last while it watched: 0 before the first
    bool early_raised; // Its clock went ahead for an input that was nearly late, and none has been since for long
    double early_from; // When it joined the match
    double clock_slow; // Seconds its clock still has to lose, a little of each update (update_client)
    uint32_t late; // Ticks the server ran without an input this machine sampled for them, which came too late
    double hold_until; // No clock changes until then: it's still losing what the last one found

    uint8_t *packet;
    uint8_t *input;   // input_size
    uint8_t *scratch; // scratch_size: inputs and their deltas

    uint32_t players; // How many the game takes: the server's input is the slot after theirs
    handover handover;
    bool handed_over; // The server left saying the match goes on elsewhere
    // A handover record arriving in parts: the version being put together, its
    // size, and its bytes, then a byte per part: 1 once it came
    uint32_t handover_version;
    uint32_t handover_size;
    uint8_t *handover_bytes;

    // What each tick it ran and hasn't had confirmed told its local handlers,
    // so a tick it runs again tells them nothing twice (tide/events.h)
    tide_hearing hearing;
};

static void go_offline(tide_client *c, const tide_disconnect_reason reason)
{
    if (c->state == TIDE_SESSION_OFFLINE) return;
    c->state = TIDE_SESSION_OFFLINE;
    c->reason = reason;
}

static void stop_receiving(tide_client *c)
{
    free(c->snapshot);
    free(c->chunk_have);
    c->snapshot = NULL;
    c->chunk_have = NULL;
    c->receiving = false;
}

static void *world_at(const tide_client *c, uint32_t tick);

static download *download_by_number(tide_client *c, const uint32_t number)
{
    for (uint32_t i = 0; number && i < DOWNLOADS_MOST; i++) {
        if (c->downloads[i].number == number) return &c->downloads[i];
    }
    return NULL;
}

// Room for a download: a free slot, or else the oldest whole one's
static download *download_slot(tide_client *c)
{
    download *oldest = NULL;
    for (uint32_t i = 0; i < DOWNLOADS_MOST; i++) {
        download *d = &c->downloads[i];
        if (!d->number) return d;
        if (download_whole(d) && (!oldest || d->number < oldest->number)) oldest = d;
    }
    if (oldest) free_download(oldest);
    return oldest;
}

static void free_downloads(tide_client *c)
{
    for (uint32_t i = 0; i < DOWNLOADS_MOST; i++) free_download(&c->downloads[i]);
    c->download_done = 0;
}

static void stop_upload_client(tide_client *c)
{
    free(c->upload_sent);
    c->upload_sent = NULL;
    c->upload_number = 0;
    c->upload_chunks = 0;
}

// A download that's whole already: the bytes of this machine's own big
// command, once the server said which number it published it under
static void note_download(tide_client *c, const uint32_t number, const uint8_t *data, const uint32_t size)
{
    if (!number || download_by_number(c, number)) return;
    download *d = download_slot(c);
    if (!d) return;
    uint8_t *copy = malloc(size);
    if (!copy) return;
    memcpy(copy, data, size);
    *d = (download){number, size, chunks_of(size), chunks_of(size), copy, NULL};
}


static void discard_view_correction(tide_client *c)
{
    free_world(c->game, c->correction_before);
    free_world(c->game, c->correction_previous);
    free_world(c->game, c->correction_after);
    free_world(c->game, c->correction_after_previous);
    free(c->correction_before);
    free(c->correction_previous);
    free(c->correction_after);
    free(c->correction_after_previous);
    c->correction_before = NULL;
    c->correction_previous = NULL;
    c->correction_after = NULL;
    c->correction_after_previous = NULL;
    c->correction_pending = false;
    c->correction_reset = false;
}

static void reset_view_correction(tide_client *c)
{
    discard_view_correction(c);
    c->presented = false;
    c->correction_pending = c->desc.correct_views != NULL;
    c->correction_reset = true;
}

static bool presented_retained(const tide_client *c)
{
    const uint32_t first = c->presented_previous ? c->presented_tick - 1u : c->presented_tick;
    return c->loaded && c->presented_tick <= c->ahead && first >= c->first_tick
        && (c->verified == 0u || first >= c->verified - 1u)
        && c->ahead - first < c->ring;
}

// Called only for a replay overwrite, never ordinary advancement. Keep both
// old endpoints once, even when an update replays the same tick repeatedly.
static void capture_view_correction(tide_client *c, const uint32_t tick)
{
    if (!c->desc.correct_views || !c->presented || c->correction_pending
        || (tick != c->presented_tick && (!c->presented_previous || tick != c->presented_tick - 1u))) return;
    c->correction_pending = true;
    if (!presented_retained(c)) {
        c->correction_reset = true;
        return;
    }
    const tide_game *g = c->game;
    c->correction_before = calloc(1, g->world_size);
    if (c->presented_previous) c->correction_previous = calloc(1, g->world_size);
    if (!c->correction_before || (c->presented_previous && !c->correction_previous)) {
        free(c->correction_before);
        free(c->correction_previous);
        c->correction_before = NULL;
        c->correction_previous = NULL;
        c->correction_reset = true;
        return;
    }
    g->copy_world(c->correction_before, world_at(c, c->presented_tick));
    if (c->presented_previous) g->copy_world(c->correction_previous, world_at(c, c->presented_tick - 1u));
}

static void finish_view_correction(tide_client *c)
{
    if (!c->correction_pending) return;
    const bool reset = c->correction_reset || !c->correction_before;
    const tide_view_correction correction = {
        .before = reset ? NULL : c->correction_before,
        .before_previous = reset ? NULL : c->correction_previous,
        .after = reset ? NULL : c->correction_after ? c->correction_after : c->correction_before,
        .after_previous = reset ? NULL : c->correction_after_previous
            ? c->correction_after_previous : c->correction_previous,
        .tick = c->presented_tick,
        .alpha = c->presented_alpha,
        .reset = reset,
    };
    if (c->desc.correct_views) c->desc.correct_views(c->desc.user, &correction);
    discard_view_correction(c);
}

// A catch-up update may confirm past the displayed pair and reuse its ring
// slots before the callback. Keep each reconstructed endpoint as it changes;
// an endpoint not rewritten is still the copied old endpoint.
static void finish_view_rewrite(tide_client *c, const uint32_t tick)
{
    if (!c->correction_pending || c->correction_reset || !c->correction_before) return;
    void **after = tick == c->presented_tick ? &c->correction_after
        : c->presented_previous && tick == c->presented_tick - 1u ? &c->correction_after_previous : NULL;
    if (!after) return;
    if (!*after) *after = calloc(1, c->game->world_size);
    if (!*after) {
        c->correction_reset = true;
        return;
    }
    c->game->copy_world(*after, world_at(c, tick));
}

static void drop_base(tide_client *c)
{
    free_world(c->game, c->base);
    free(c->base);
    c->base = NULL;
    c->answered = false;
}

static void drop_had(tide_client *c)
{
    free_world(c->game, c->had);
    free(c->had);
    c->had = NULL;
}

// Whether it can start the match itself, to have a world to build the
// server's from.
static bool can_start(const tide_client *c)
{
    return !c->game->client_state && c->game->pure_start && !c->loaded && !c->base && !c->base_failed;
}

// The world to build the one at `tick` from: the one it was given, until it
// has its own, then its own nearest that tick; with neither, the match as it
// started (`kind`, `dt`, `seed` and `given`, see SEND_HASHES). NULL for none,
// after one built from it came out wrong.
static const void *base_for(tide_client *c, const uint32_t tick, const uint8_t kind, const float dt, const uint32_t seed,
                            const void *given)
{
    const tide_game *g = c->game;
    if (c->base_failed) {
        drop_base(c);
        return NULL;
    }
    if (!c->loaded && (c->base || !can_start(c) || kind == START_UNKNOWN)) return c->base;
    if (!c->base) c->base = calloc(1, g->world_size);
    if (!c->base) return NULL;
    if (c->loaded) {
        const uint32_t t = tick < c->verified ? c->verified : tick > c->ahead ? c->ahead : tick;
        g->copy_world(c->base, world_at(c, t));
    } else if (kind == START_GIVEN && g->start_size) {
        // The start's bytes are wherever they came in a message, and the game
        // reads them as the value they are: from memory aligned for one
        void *start = malloc(g->start_size);
        if (!start) tide_out_of_memory();
        memcpy(start, given, g->start_size);
        g->start(c->base, dt, start, seed);
        free(start);
    } else {
        g->start(c->base, dt, NULL, seed);
    }
    return c->base;
}

// The hashes of the world at snapshot_tick came: which of its pages the world
// it builds it from lacks, which it tells the server until the world comes.
static void answer_hashes(tide_client *c)
{
    const tide_game *g = c->game;
    // How the match started, then the hashes
    tide_reader r = {c->snapshot, c->snapshot_size, 0, false};
    const uint8_t kind = tide_read_u8(&r);
    const uint32_t dt_bits = tide_read_u32(&r);
    const uint32_t seed = g->seeded ? tide_read_u32(&r) : 0u;
    const uint8_t *given = kind == START_GIVEN ? tide_read_bytes(&r, g->start_size) : NULL;
    float dt;
    memcpy(&dt, &dt_bits, 4);
    const void *base = r.failed || kind > START_GIVEN ? NULL : base_for(c, c->snapshot_tick, kind, dt, seed, given);
    c->lacks_size = base && g->need_pages
                      ? g->need_pages(base, c->snapshot + r.at, c->snapshot_size - r.at, c->lacks, LACKS_BYTES)
                      : 0u;
    c->answered = true;
    c->answered_tick = c->snapshot_tick;
    c->answered_number = c->snapshot_number;
    c->got_number = c->snapshot_number;
    stop_receiving(c);
}

// The time this update's datagrams say they were sent: as the server echoes
// it, and remembered with when that is by the transport's clock.
static uint16_t said_time(tide_client *c)
{
    const uint16_t said = millis16(c->now);
    const uint32_t at = c->sent_count++ % SENT_TIMES;
    c->sent[at].said = said;
    c->sent[at].at = clock_of(&c->desc.transport, c->now);
    return said;
}

// The server echoed a time this machine said, `held_ms` after it arrived
// there. From the update that said it to this one is what an answer takes to
// be read, which is what the client waits by. The ping is the network's part
// of it: from when it was sent to when the answer arrived, by the transport's
// clock, less what the server held it. So it counts no wait for an update, on
// either machine: two players at one desk have a ping of about nothing,
// whatever their frame rates.
static void measure(tide_client *c, const uint16_t echo, const uint32_t held_ms)
{
    if (!echo) return;
    c->rtt_ms = millis_since(c->now, echo);
    double trip = (double)c->rtt_ms / 1000.0;
    for (uint32_t i = 0; i < SENT_TIMES && i < c->sent_count; i++) { // The newest that said so
        const uint32_t at = (c->sent_count - 1u - i) % SENT_TIMES;
        if (c->sent[at].said != echo) continue;
        trip = c->arrived - c->sent[at].at;
        break;
    }
    const double ping = trip - (double)held_ms / 1000.0;
    c->ping_ms = ping > 0.0 ? (uint32_t)(ping * 1000.0 + 0.5) : 0u;
}

static void on_welcome(tide_client *c, tide_reader *r)
{
    const uint32_t nonce = tide_read_u32(r);
    const uint32_t player = tide_read_varint(r);
    const uint8_t flags = tide_read_u8(r);
    const uint32_t tick_rate = tide_read_u32(r);
    const uint32_t server_tick = tide_read_u32(r);
    const uint32_t tick = tide_read_u32(r);
    const uint32_t number = tide_read_u32(r);
    const uint8_t kind = tide_read_u8(r);
    const uint32_t size = tide_read_u32(r);
    const uint32_t chunks = tide_read_u32(r);
    const uint16_t their_time = tide_read_u16(r);
    const uint16_t echo = tide_read_u16(r);
    const uint16_t held = tide_read_u16(r);
    const uint64_t cookie = tide_read_u64(r);
    if (r->failed || nonce != c->nonce || player >= c->players || tick_rate == 0 || kind > SEND_CLIENT_REPAIR
        || (number && ((kind >= SEND_CLIENT_WORLD) != (c->game->client_state != NULL)))
        || chunks != chunks_of(size)) return;
    if (!c->frames) { // Its windows, now that it knows the tick rate
        c->w = windows_for(tick_rate);
        c->frames = calloc(c->w.history, sizeof *c->frames);
        c->input_tick = malloc(c->w.inputs * sizeof *c->input_tick);
        c->input_size = calloc(c->w.inputs, sizeof *c->input_size);
        c->inputs = malloc((size_t)c->w.inputs * (c->game->max_input_bytes ? c->game->max_input_bytes : 1u));
        if (!c->frames || !c->input_tick || !c->input_size || !c->inputs) {
            go_offline(c, TIDE_DISCONNECT_FAILED);
            return;
        }
        for (uint32_t i = 0; i < c->w.inputs; i++) c->input_tick[i] = UINT32_MAX;
        // A tick it remembers is one it ran and hasn't had confirmed: as many as it has snapshots at most
        if (c->desc.events) tide_hearing_init(&c->hearing, c->w.prediction + 2u, c->desc.events);
    }
    c->welcomed = true;
    c->cookie = cookie;
    c->player = player;
    c->server_input_mine = (flags & 1u) != 0;
    c->tick_rate = tick_rate;
    c->guess = guess_ticks(c->desc.guess_seconds, tick_rate);
    if (server_tick > c->server_tick) c->server_tick = server_tick;
    c->their_time = their_time;
    measure(c, echo, held);
    // A world it doesn't have yet: the first, a newer one after it went wrong,
    // or one past all it ran, when it fell behind further than the server
    // keeps ticks. Something it had whole, it had: what's after it is newer.
    const bool wanted = !c->loaded || (c->need_snapshot && tick >= c->verified) || tick > c->ahead
        || (kind == SEND_CLIENT_REPAIR && c->loaded && tick > c->verified);
    if (!wanted || number <= c->got_number || (c->receiving && c->snapshot_number == number)) return;
    stop_receiving(c);
    c->snapshot = malloc(size ? size : 1u);
    c->chunk_have = calloc(chunks ? chunks : 1u, 1);
    if (!c->snapshot || !c->chunk_have) {
        stop_receiving(c);
        return;
    }
    c->receiving = true;
    c->snapshot_tick = tick;
    c->snapshot_number = number;
    c->snapshot_kind = kind;
    c->snapshot_stream = (flags & 2u) != 0;
    c->snapshot_size = size;
    c->chunk_count = chunks;
    c->chunks_had = 0;
}

static void load_snapshot(tide_client *c);

static void on_chunk(tide_client *c, tide_reader *r)
{
    const uint32_t number = tide_read_u32(r);
    const uint32_t index = tide_read_u32(r);
    const uint16_t size = tide_read_u16(r);
    const uint8_t *bytes = tide_read_bytes(r, size);
    if (!bytes || !c->receiving || number != c->snapshot_number || index >= c->chunk_count || c->chunk_have[index]) return;
    const uint32_t from = index * CHUNK;
    const uint32_t remaining = c->snapshot_size - from;
    if (size != (remaining < CHUNK ? remaining : CHUNK)) return;
    memcpy(c->snapshot + from, bytes, size);
    c->chunk_have[index] = 1;
    c->world_bytes += size;
    if (++c->chunks_had < c->chunk_count) return;
    if (c->snapshot_kind == SEND_HASHES) answer_hashes(c);
    else load_snapshot(c);
}

static void on_server(tide_client *c, tide_reader *r)
{
    const uint16_t their_time = tide_read_u16(r);
    const uint16_t echo = tide_read_u16(r);
    const uint32_t held = tide_read_varint(r);
    const uint32_t server_tick = tide_read_varint(r);
    const int32_t early = read_signed(r); // Its newest input, from the server's tick
    const int32_t waited = read_signed(r); // How long its inputs waited for the ticks the server last ran
    // What the server has of this machine's upload (see start_server_packet)
    uint8_t upload_state = UPLOAD_NONE;
    uint32_t upload_number = 0;
    uint32_t upload_ack = 0;
    uint64_t upload_mask = 0;
    uint32_t upload_published = 0;
    if (c->game->command) {
        upload_state = tide_read_u8(r);
        if (upload_state == UPLOAD_PART) {
            upload_number = tide_read_varint(r);
            upload_ack = tide_read_varint(r);
            upload_mask = tide_read_u64(r);
        } else if (upload_state == UPLOAD_WHOLE) {
            upload_number = tide_read_varint(r);
            upload_published = tide_read_varint(r);
        } else if (upload_state != UPLOAD_NONE) {
            return;
        }
    }
    const uint8_t pieces = tide_read_u8(r);
    if (r->failed) return;
    c->their_time = their_time;
    measure(c, echo, held);
    if (server_tick >= c->heard_tick) { // The newest it said, as datagrams come in any order
        c->heard_tick = server_tick;
        c->heard_update = c->updates;
        c->heard_timed = echo != 0;
        if (echo) c->heard_trip_ms = millis_since(c->now, echo);
    }
    if (upload_number && upload_number == c->upload_number) {
        if (upload_state == UPLOAD_PART) {
            if (upload_ack > c->upload_ack) c->upload_ack = upload_ack < c->upload_chunks ? upload_ack : c->upload_chunks;
            if (upload_ack == c->upload_ack) c->upload_mask = upload_mask;
        } else { // Whole: its pending command waits for a verified world now, like any other
            for (uint32_t i = 0; i < c->commands.count; i++) {
                pending_command *cmd = &c->commands.items[i];
                if (cmd->channel != c->upload_channel || cmd->sequence != c->upload_sequence) continue;
                cmd->uploaded = true;
                cmd->published = upload_published;
                if (upload_published) note_download(c, upload_published, cmd->data, cmd->size);
            }
            if (upload_published) stop_upload_client(c);
        }
    }
    // The newest it said, as datagrams come in any order. An older one still
    // counts for the least, if it's about the clock as it is (keep_early).
    const bool known = waited != WAIT_UNKNOWN;
    const int32_t wait = waited < -WAIT_MOST ? -WAIT_MOST : waited > WAIT_MOST ? WAIT_MOST : waited;
    if (!c->waited_known || server_tick >= c->waited_tick) {
        c->waited = known ? wait : 0;
        c->waited_known = known;
        c->waited_tick = server_tick;
    }
    if (known && c->early_watching && server_tick > c->early_tick && wait < c->early_least) c->early_least = wait;
    const uint32_t input_ack = server_tick + (uint32_t)early;
    if (input_ack > c->input_ack) c->input_ack = input_ack;
    if (!c->loaded) return;
    for (uint32_t i = 0; i < pieces; i++) {
        // Ticks back from the server's, its frame's size, and which piece of it, when there are more
        const uint32_t back = tide_read_varint(r);
        const uint32_t total = tide_read_varint(r);
        const uint32_t count = (total + PIECE - 1u) / PIECE;
        // No frame is bigger than what the game's inputs can make (frame_capacity)
        const uint32_t most = c->game->client_state ? 256u * PIECE : frame_capacity(c->game);
        if (r->failed || total == 0 || total > most) return; // Where the next piece starts is lost
        const uint32_t index = count > 1 ? tide_read_varint(r) : 0u;
        if (index >= count) return;
        const uint32_t from = index * PIECE;
        const uint32_t size = total - from < PIECE ? total - from : PIECE;
        const uint8_t *bytes = tide_read_bytes(r, size);
        if (!bytes) return;
        const uint32_t tick = server_tick - back;
        if (back == 0 || back > server_tick || tick < c->verified || tick - c->verified >= c->w.history) continue;
        pending_frame *f = &c->frames[tick % c->w.history];
        if (f->tick != tick || !f->data) {
            drop_frame(f);
            *f = (pending_frame){.tick = tick, .size = total, .pieces = count, .data = calloc((size_t)total + count, 1)};
            if (!f->data) continue;
        }
        if (f->size != total || f->data[total + index]) continue;
        memcpy(f->data + from, bytes, size);
        f->data[total + index] = 1;
        f->had++;
    }
}

// A chunk of a big command on its way down (see DOWNLOAD): kept by its
// number until the tick that names it is run. The server sends one at a
// time, so one that doesn't fit takes the oldest whole one's place.
static void on_download(tide_client *c, tide_reader *r)
{
    const uint32_t number = tide_read_varint(r);
    const uint32_t size = tide_read_varint(r);
    const uint32_t index = tide_read_varint(r);
    const uint16_t bytes = tide_read_u16(r);
    const uint8_t *data = tide_read_bytes(r, bytes);
    if (r->failed || !data || !number || !c->loaded || !big_command(size) || size > (c->game->shards ? TIDE_SHARD_MESSAGE_LIMIT : TIDE_COMMAND_LIMIT)) return;
    const uint32_t chunks = chunks_of(size);
    if (index >= chunks || bytes != (size - index * CHUNK < CHUNK ? size - index * CHUNK : CHUNK)) return;
    download *d = download_by_number(c, number);
    if (!d) {
        if (number <= c->download_done) return; // Had, and run
        d = download_slot(c);
        if (!d) return;
        *d = (download){number, size, chunks, 0, malloc(size), calloc(chunks, 1)};
        if (!d->data || !d->have) {
            free_download(d);
            return;
        }
    }
    if (d->size != size || d->have[index]) return;
    memcpy(d->data + (size_t)index * CHUNK, data, bytes);
    d->have[index] = 1;
    d->had++;
    c->world_bytes += bytes;
}

// A part of a handover record (see send_handover). Parts of a newer record
// than the one being put together start it over; once every part came, the
// record is read, and kept if it's sound.
static void on_handover(tide_client *c, tide_reader *r)
{
    if (c->game->client_state) return;
    const uint32_t version = tide_read_u32(r);
    const uint32_t size = tide_read_varint(r);
    const uint32_t part = tide_read_varint(r);
    if (r->failed || version <= c->handover.version || version < c->handover_version) return;
    if (size == 0 || size > handover_capacity(c->players)) return;
    const uint32_t parts = (size + HANDOVER_PART - 1u) / HANDOVER_PART;
    if (part >= parts) return;
    const uint32_t from = part * HANDOVER_PART;
    const uint32_t part_size = size - from < HANDOVER_PART ? size - from : HANDOVER_PART;
    const uint8_t *bytes = tide_read_bytes(r, part_size);
    if (!bytes) return;
    if (version != c->handover_version || size != c->handover_size || !c->handover_bytes) {
        free(c->handover_bytes);
        c->handover_bytes = calloc((size_t)size + parts, 1);
        c->handover_version = c->handover_bytes ? version : 0u;
        c->handover_size = size;
        if (!c->handover_bytes) return;
    }
    memcpy(c->handover_bytes + from, bytes, part_size);
    c->handover_bytes[size + part] = 1;
    for (uint32_t p = 0; p < parts; p++) {
        if (!c->handover_bytes[size + p]) return;
    }
    // Whole: the record
    handover h = {.version = version};
    tide_reader record = {c->handover_bytes, size, 0, false};
    h.own = tide_read_varint(&record);
    h.open = tide_read_u8(&record) != 0;
    const uint8_t *code = tide_read_bytes(&record, TIDE_ROOM_CODE_LENGTH);
    const uint8_t key = tide_read_u8(&record);
    const uint8_t *key_bytes = key <= TIDE_ROOM_KEY_LENGTH ? tide_read_bytes(&record, key) : NULL;
    h.in_match = read_players(&record);
    h.digests = calloc(c->players, sizeof *h.digests);
    if (!h.digests) tide_out_of_memory();
    bool sound = h.own <= c->players;
    for (uint32_t i = 0; i < TIDE_PLAYER_SET_BITS; i++) {
        if (!tide_players_has(&h.in_match, i)) continue;
        if (i >= c->players) sound = false; // No player the game takes
        else h.digests[i] = tide_read_u64(&record);
    }
    char letters[TIDE_ROOM_CODE_LENGTH + TIDE_ROOM_KEY_LENGTH];
    if (sound && !record.failed && code && key_bytes) {
        memcpy(letters, code, TIDE_ROOM_CODE_LENGTH);
        memcpy(letters + TIDE_ROOM_CODE_LENGTH, key_bytes, key);
    } else {
        sound = false;
    }
    // Done with the parts, whatever they made: a record that isn't sound waits for the next version
    free(c->handover_bytes);
    c->handover_bytes = NULL;
    if (!sound) { free(h.digests); return; }
    code = (const uint8_t *)letters;
    key_bytes = (const uint8_t *)letters + TIDE_ROOM_CODE_LENGTH;
    // A room's code and key are letters and digits, as the platform layer makes them
    for (uint32_t i = 0; i < TIDE_ROOM_CODE_LENGTH; i++) {
        if (!strchr(TIDE_ROOM_CODE_LETTERS, code[i]) || !code[i]) { free(h.digests); return; }
        h.code[i] = (char)code[i];
    }
    for (uint32_t i = 0; i < key; i++) {
        const char k = (char)key_bytes[i];
        if (!((k >= '0' && k <= '9') || (k >= 'a' && k <= 'z') || (k >= 'A' && k <= 'Z'))) { free(h.digests); return; }
        h.key[i] = k;
    }
    free(c->handover.digests);
    c->handover = h;
}

static void client_receive(tide_client *c)
{
    uint8_t data[TIDE_NET_MTU];
    tide_address from;
    uint32_t size;
    const tide_transport *t = &c->desc.transport;
    while (t->receive && (size = t->receive(t->self, &from, data, sizeof data)) > 0) {
        if (from.kind != TIDE_ADDRESS_LOOPBACK) c->traffic.received += size;
        if (!tide_address_equal(from, c->desc.server)) continue;
        c->arrived = arrived_of(t, c->now);
        tide_reader r = {data, size, 0, false};
        const uint8_t type = read_header(&r);
        if (type != 0 && type != MSG_HELLO) c->last_heard = c->now;
        switch (type) {
        case MSG_WELCOME: on_welcome(c, &r); break;
        case MSG_CHUNK: on_chunk(c, &r); break;
        case MSG_DOWNLOAD: on_download(c, &r); break;
        case MSG_SERVER: on_server(c, &r); break;
        case MSG_HANDOVER: on_handover(c, &r); break;
        case MSG_LOCAL_DATA: {
            const uint64_t cookie = tide_read_u64(&r);
            const tide_player_id sender = {tide_read_varint(&r)};
            const uint64_t connection = tide_read_u64(&r);
            if (r.failed || cookie != c->cookie || !sender.id || sender.id > c->players
                || sender.id == c->player + 1u || !connection || r.size == r.at
                || r.size - r.at > TIDE_LOCAL_DATA_BYTES || !c->loaded
                || c->state != TIDE_SESSION_CONNECTED || !c->local_receive) break;
            c->local_receive(c->local_user, sender, connection, r.data + r.at, r.size - r.at);
            break;
        }
        case MSG_REFUSE: {
            const uint8_t why = tide_read_u8(&r);
            if (r.failed) break;
            if (why == REFUSE_OTHER_GAME) copy_message(c->message,
                "Different simulation; update the client to this game's release.", TIDE_MESSAGE_BYTES - 1u);
            go_offline(c, TIDE_DISCONNECT_REFUSED);
            break;
        }
        case MSG_BYE: {
            const uint8_t why = tide_read_u8(&r);
            if (why == BYE_KICKED && !r.failed) {
                const uint8_t n = tide_read_u8(&r);
                const uint8_t *bytes = tide_read_bytes(&r, n);
                if (c->state != TIDE_SESSION_OFFLINE) copy_message(c->message, r.failed ? NULL : (const char *)bytes, n);
                go_offline(c, TIDE_DISCONNECT_KICKED);
                break;
            }
            if (why == BYE_HANDOVER && !r.failed) c->handed_over = c->state != TIDE_SESSION_OFFLINE;
            go_offline(c, why == BYE_ENDED && !r.failed ? TIDE_DISCONNECT_ENDED : TIDE_DISCONNECT_SERVER_LEFT);
            break;
        }
        default: break;
        }
        if (c->state == TIDE_SESSION_OFFLINE) return;
    }
}

// The downloads a frame's commands name (see dispatch_commands): read once
// its pieces are all there, by the same steps apply_frame takes
static void read_frame_names(const tide_client *c, pending_frame *f)
{
    f->read = true;
    f->name_count = 0;
    tide_reader r = {f->data, f->size, 0, false};
    tide_read_u64(&r);
    const uint8_t parts = tide_read_u8(&r);
    const uint32_t events = parts & FRAME_EVENTS ? tide_read_varint(&r) : 0u;
    for (uint32_t i = 0; i < events && !r.failed; i++) { tide_read_u8(&r); tide_read_varint(&r); }
    const tide_player_set none = {0};
    const tide_player_set mask = parts & FRAME_INPUTS ? read_players(&r) : none;
    if (parts & FRAME_LATE) read_players(&r);
    for (uint32_t slot = 0; slot <= c->players; slot++) {
        if (tide_players_has(&mask, slot)) tide_read_bytes(&r, tide_read_varint(&r));
    }
    const uint32_t count = parts & FRAME_COMMANDS ? tide_read_varint(&r) : 0u;
    for (uint32_t i = 0; i < count && i < TIDE_COMMANDS_PER_TICK && !r.failed; i++) {
        tide_read_varint(&r);
        tide_read_varint(&r);
        const uint32_t size = tide_read_varint(&r);
        if (!big_command(size)) {
            tide_read_bytes(&r, size);
            continue;
        }
        f->names[f->name_count++] = tide_read_varint(&r);
    }
    const uint32_t shard_count = parts & FRAME_SHARD ? tide_read_varint(&r) : 0u;
    if (shard_count > TIDE_SHARD_MESSAGES_PER_TICK) {
        f->invalid = true;
        return;
    }
    for (uint32_t i = 0; i < shard_count && !r.failed; i++) {
        const uint32_t size = tide_read_varint(&r);
        if (!big_command(size)) {
            tide_read_bytes(&r, size);
            continue;
        }
        f->names[f->name_count++] = tide_read_varint(&r);
    }
    if (parts & FRAME_DESTROYED) {
        const uint32_t count_destroyed = tide_read_varint(&r);
        if (r.failed || count_destroyed > (r.size - r.at) / 2u) return;
        f->destroyed = count_destroyed ? malloc((size_t)count_destroyed * sizeof *f->destroyed) : NULL;
        if (count_destroyed && !f->destroyed) return;
        for (uint32_t i = 0; i < count_destroyed; i++) {
            f->destroyed[i].index = tide_read_varint(&r);
            f->destroyed[i].generation = tide_read_varint(&r);
        }
        if (!r.failed) f->destroyed_count = count_destroyed;
    }
    if (parts & FRAME_LOCAL_EVENTS) {
        const uint32_t event_size = tide_read_varint(&r);
        const uint8_t *bytes = tide_read_bytes(&r, event_size);
        uint32_t build = 0, count_events = 0;
        if (!bytes || !c->game->check_client_events
            || !c->game->check_client_events(bytes, event_size, &build, &count_events)) {
            f->invalid = true;
            return;
        }
        if (event_size) {
            f->events.bytes = malloc(event_size);
            if (!f->events.bytes) { f->invalid = true; return; }
            memcpy(f->events.bytes, bytes, event_size);
            f->events.size = f->events.capacity = event_size;
            f->events.build = build;
            f->events.count = count_events;
        }
    }
    if (r.failed || r.at != r.size) f->invalid = true;
}

// A frame is complete once every piece of it came, and every download its
// commands name is whole
static bool frame_complete(tide_client *c, pending_frame *f, const uint32_t tick)
{
    if (f->tick != tick || !f->data || f->had != f->pieces) return false;
    if (!f->read) read_frame_names(c, f);
    if (f->invalid) { drop_frame(f); return false; }
    for (uint32_t i = 0; i < f->name_count; i++) {
        const download *d = download_by_number(c, f->names[i]);
        if (!d || !download_whole(d)) return false;
    }
    return true;
}

static const uint8_t *own_input(const tide_client *c, const uint32_t tick, uint32_t *size)
{
    const uint32_t slot = tick % c->w.inputs;
    if (c->input_tick[slot] != tick) return NULL;
    *size = c->input_size[slot];
    return c->inputs + (size_t)slot * c->game->max_input_bytes;
}

// Whether a tick went as it was predicted: no one joined or left, no one
// else's input changed, and this machine's arrived in time, as it was sent (and
// the server's, when that's this machine's too). A tick it sampled no input
// for was predicted with the one before kept, which is what the server ran it
// with: that it says the tick had no input of this machine's is as predicted.
static bool as_predicted(const tide_client *c, const pending_frame *f)
{
    tide_reader r = {f->data, f->size, 0, false};
    tide_read_u64(&r);
    const uint8_t parts = tide_read_u8(&r);
    if (parts & (FRAME_EVENTS | FRAME_COMMANDS | FRAME_DESTROYED | FRAME_SHARD)) return false; // What a guess can't have
    const tide_player_set none = {0};
    const tide_player_set mask = parts & FRAME_INPUTS ? read_players(&r) : none;
    const tide_player_set late = parts & FRAME_LATE ? read_players(&r) : none;
    if (!c->game->set_input) return !tide_players_any(&mask);
    tide_player_set mine = none;
    tide_players_add(&mine, (uint32_t)c->player);
    if (c->server_input_mine) tide_players_add(&mine, c->players);
    if (players_beyond(&mask, &mine)) return false;
    uint32_t own_size = 0;
    const uint8_t *own = own_input(c, f->tick, &own_size);
    if (!own) return !tide_players_any(&mask);
    if (players_meet(&late, &mine)) return false; // The one it sampled came too late
    const tide_game *g = c->game;
    const bool deltas = g->read_input_delta && g->world_input;
    uint8_t *base = c->scratch;
    uint8_t *theirs = c->scratch + g->input_size;
    if (g->world_input && !g->read_input(own, own_size, c->input)) return false;
    for (uint32_t slot = 0; slot <= c->players; slot++) {
        if (!tide_players_has(&mask, slot)) {
            // No delta means the server kept its previous value, which may
            // differ from our sample after it rejected an old view timestamp.
            // Check that held input too before reusing a predicted world.
            if (g->world_input && tide_players_has(&mine, slot)) {
                g->world_input(world_at(c, f->tick), slot, theirs);
                if (memcmp(theirs, c->input, g->input_size) != 0) return false;
            }
            continue;
        }
        const uint32_t size = tide_read_varint(&r);
        const uint8_t *bytes = tide_read_bytes(&r, size);
        if (!bytes) return false;
        if (!deltas) {
            if (size != own_size || memcmp(bytes, own, size) != 0) return false;
            continue;
        }
        // As the server sent it: what differs from the slot's input before the tick
        g->world_input(world_at(c, f->tick), slot, base);
        if (!g->read_input_delta(bytes, size, base, theirs) || memcmp(theirs, c->input, g->input_size) != 0) return false;
    }
    return !r.failed;
}

static void *world_at(const tide_client *c, const uint32_t tick)
{
    return c->worlds[tick % c->ring];
}

// Whether the server ran a frame's tick without this machine's input, which
// came too late for it.
static bool own_late(const tide_client *c, const pending_frame *f)
{
    tide_reader r = {f->data, f->size, 0, false};
    tide_read_u64(&r);
    const uint8_t parts = tide_read_u8(&r);
    if (!(parts & FRAME_LATE)) return false;
    if (parts & FRAME_EVENTS) {
        const uint32_t events = tide_read_varint(&r);
        for (uint32_t i = 0; i < events && !r.failed; i++) { tide_read_u8(&r); tide_read_varint(&r); } // Which, and whose
    }
    if (parts & FRAME_INPUTS) read_players(&r);
    const tide_player_set late = read_players(&r);
    return !r.failed && tide_players_has(&late, (uint32_t)c->player);
}

// Room for one more snapshot. The ones in use keep their ticks; the others,
// and new ones, fill the rest.
static bool room_ahead(tide_client *c)
{
    // Preserve the confirmed presentation interval as well as the prediction window.
    const uint32_t shown = (uint32_t)c->verified_view;
    const uint32_t oldest = shown > c->first_tick ? shown - 1u : c->first_tick;
    if (c->ahead + 1u - oldest < c->ring) return true;
    const uint32_t most = c->w.prediction * 2u + 4u;
    if (c->ring >= most) return false;
    const uint32_t ring = c->ring * 2u > most ? most : c->ring * 2u;
    void **worlds = calloc(ring, sizeof *worlds);
    bool *moved = calloc(c->ring, sizeof *moved);
    if (!worlds || !moved) {
        free(worlds);
        free(moved);
        return false;
    }
    for (uint32_t t = oldest; t <= c->ahead; t++) {
        worlds[t % ring] = c->worlds[t % c->ring];
        moved[t % c->ring] = true;
    }
    uint32_t spare = 0;
    for (uint32_t i = 0; i < ring; i++) {
        if (worlds[i]) continue;
        while (spare < c->ring && moved[spare]) spare++;
        worlds[i] = spare < c->ring ? c->worlds[spare++] : calloc(1, c->game->world_size);
        if (!worlds[i]) { // Out of memory: keep what there was
            for (uint32_t k = 0; k < ring; k++) {
                bool old = false;
                for (uint32_t j = 0; j < c->ring && !old; j++) old = worlds[k] == c->worlds[j];
                if (!old) free(worlds[k]);
            }
            free(worlds);
            free(moved);
            return false;
        }
    }
    free(moved);
    free(c->worlds);
    c->worlds = worlds;
    c->ring = ring;
    return true;
}

// Runs `tick` on `world`, the world before it. With local handlers that hear
// the match, and a host that takes what they hear, the tick records what it
// sent, and what of it the tick hadn't told them yet goes out (tide/events.h).
static void run_tick(tide_client *c, void *world, const uint32_t tick)
{
    const tide_game *g = c->game;
    c->ran++;
    tide_event_log *events = g->tick_events ? tide_hearing_begin(&c->hearing, tick) : NULL;
    if (!events) {
        g->tick(world, c->desc.jobs);
        return;
    }
    g->tick_events(world, c->desc.jobs, events);
    tide_hearing_end(&c->hearing, tick);
}

// The hash a frame says the world has after its tick.
static uint64_t frame_hash(const pending_frame *f)
{
    tide_reader r = {f->data, f->size, 0, false};
    return tide_read_u64(&r);
}

// Runs a tick the server sent on `world`, the world before it. False if it's
// broken or the world came out different from the server's.
static bool apply_frame(tide_client *c, void *world, const pending_frame *f)
{
    const tide_game *g = c->game;
    tide_reader r = {f->data, f->size, 0, false};
    const uint64_t hash = tide_read_u64(&r);
    const uint8_t parts = tide_read_u8(&r);
    const uint32_t events = parts & FRAME_EVENTS ? tide_read_varint(&r) : 0u;
    if (events > EVENTS_PER_PLAYER * c->players) return false;
    for (uint32_t i = 0; i < events; i++) {
        const uint8_t kind = tide_read_u8(&r);
        const uint32_t player = tide_read_varint(&r);
        if (r.failed || player >= c->players) return false;
        if (kind == EVENT_JOIN) g->player_joined(world, tide_player_from_index(player));
        else g->player_left(world, tide_player_from_index(player));
    }
    const tide_player_set none = {0};
    const tide_player_set mask = parts & FRAME_INPUTS ? read_players(&r) : none;
    if (parts & FRAME_LATE) read_players(&r); // Late ones
    for (uint32_t slot = 0; slot <= c->players; slot++) {
        if (!tide_players_has(&mask, slot)) continue;
        const uint32_t size = tide_read_varint(&r);
        const uint8_t *bytes = tide_read_bytes(&r, size);
        if (!bytes || !g->read_input) return false;
        if (g->read_input_delta && g->world_input) { // What differs from the slot's input as the world keeps it
            g->world_input(world, slot, c->scratch);
            if (!g->read_input_delta(bytes, size, c->scratch, c->input)) return false;
        } else if (!g->read_input(bytes, size, c->input)) {
            return false;
        }
        if (slot == c->players) g->set_server_input(world, c->input);
        else g->set_input(world, tide_player_from_index((int32_t)slot), c->input);
    }
    const uint32_t command_count = parts & FRAME_COMMANDS ? tide_read_varint(&r) : 0u;
    if (command_count > TIDE_COMMANDS_PER_TICK || (command_count && (!g->command || !g->command_sequence))) return false;
    for (uint32_t i = 0; i < command_count; i++) {
        const uint32_t player = tide_read_varint(&r);
        const uint32_t sequence = tide_read_varint(&r);
        const uint32_t size = tide_read_varint(&r);
        if (r.failed || player >= c->players || !size || size > TIDE_COMMAND_LIMIT) return false;
        const uint8_t *data;
        if (big_command(size)) { // Its bytes came apart, as a download the frame names
            download *d = download_by_number(c, tide_read_varint(&r));
            if (!d || !download_whole(d) || d->size != size) return false;
            data = d->data;
            if (d->number > c->download_done) c->download_done = d->number;
        } else {
            data = tide_read_bytes(&r, size);
        }
        const tide_player_id sender = tide_player_from_index((int32_t)player);
        uint32_t channel = 0;
        if (!data || !g->check_command(data, size, &channel)) return false;
        if (channel != TIDE_COMMAND_UNORDERED) {
            const uint32_t previous = g->command_sequence(world, sender, channel);
            if (previous == UINT32_MAX || sequence != previous + 1u) return false;
        }
        if (!g->command(world, sender, channel, sequence, data, size)) return false;
    }
    // Shard messages, in the server's order, before the tick as the server applied them
    const uint32_t shard_count = parts & FRAME_SHARD ? tide_read_varint(&r) : 0u;
    if (shard_count > TIDE_SHARD_MESSAGES_PER_TICK || (shard_count && !g->shards)) return false;
    for (uint32_t i = 0; i < shard_count; i++) {
        const uint32_t size = tide_read_varint(&r);
        if (r.failed || !size || size > TIDE_SHARD_MESSAGE_LIMIT) return false;
        const uint8_t *data;
        if (big_command(size)) {
            download *d = download_by_number(c, tide_read_varint(&r));
            if (!d || !download_whole(d) || d->size != size) return false;
            data = d->data;
            if (d->number > c->download_done) c->download_done = d->number;
        } else {
            data = tide_read_bytes(&r, size);
        }
        if (!data) return false;
        g->shards->receive(world, data, size);
    }
    if (parts & FRAME_DESTROYED) {
        const uint32_t count_destroyed = tide_read_varint(&r);
        if (r.failed || count_destroyed > (r.size - r.at) / 2u) return false;
        for (uint32_t i = 0; i < count_destroyed; i++) {
            tide_read_varint(&r);
            tide_read_varint(&r);
        }
    }
    if (parts & FRAME_LOCAL_EVENTS) tide_read_bytes(&r, tide_read_varint(&r));
    if (r.failed || r.at != r.size) return false;
    run_tick(c, world, f->tick);
    return (g->client_hash ? g->client_hash(world, tide_player_from_index(c->player))
                           : hash_of(g, world, c->desc.jobs)) == hash;
}

// What a predicted tick cost, for the input delay to go by (adjust_delay): a
// moving mean of a tick's, and apart, of the first tick of a rollback, which
// finds or makes again what C keeps of the world at the tick it starts from
// (`rerun_first`). A tick that took many times its mean is the machine
// stalling, not the tick, and counts as COST_STALL ticks' worth: a tick that
// got dearer is still learned within a few. What a rollback's ticks cost is
// summed for the update (`resim_now`).
static void note_tick_cost(tide_client *c, double cost)
{
    if (c->rerunning && c->rerun_tick++ == 0u) {
        // Not in its first second with a world: everything is cold then, and it's catching up
        if (c->now - c->early_from >= COLD_SECONDS) c->rerun_firsts[c->rerun_count++ % REPLAY_SAMPLES] = cost;
        c->resim_now += cost;
        return;
    }
    if (c->tick_cost > 0.0 && cost > c->tick_cost * COST_STALL) cost = c->tick_cost * COST_STALL;
    c->tick_cost = c->tick_cost > 0.0 ? c->tick_cost * 0.9 + cost * 0.1 : cost;
    if (c->rerunning) c->resim_now += cost;
}

// What a rollback's first tick costs: the median of the last rollbacks', so
// one that the machine stalled in, or that found everything cold, says
// nothing by itself. 0 before any.
static double rerun_first_cost(const tide_client *c)
{
    const uint32_t count = c->rerun_count < REPLAY_SAMPLES ? c->rerun_count : REPLAY_SAMPLES;
    if (count == 0u) return 0.0;
    double sorted[REPLAY_SAMPLES];
    for (uint32_t i = 0; i < count; i++) { // Insertion sort: a handful
        const double v = c->rerun_firsts[i];
        uint32_t at = i;
        while (at > 0u && sorted[at - 1u] > v) {
            sorted[at] = sorted[at - 1u];
            at--;
        }
        sorted[at] = v;
    }
    return sorted[count / 2u];
}

// Runs predicted tick `tick`: the snapshot after it is the one before it, run
// with this machine's input. With a clock, it measures what the tick cost,
// for the input delay to go by (adjust_delay).
//
// Everyone else's input is a guess: their last one, which the world before
// has. A guess `guess` ticks past the last tick the server confirmed has gone
// on long enough: from there the others are taken to have let go
// (tide_game.let_go_inputs), so what a player was doing is only carried that
// far past what's known, however far ahead this machine runs. The ticks after
// start from this one's world, where they've let go already.
static void run_predicted(tide_client *c, const uint32_t tick)
{
    const tide_game *g = c->game;
    void *world = world_at(c, tick + 1u);
    g->copy_world(world, world_at(c, tick));
    if (g->predict_inputs) {
        tide_player_set mine = {0};
        tide_players_add(&mine, (uint32_t)c->player);
        if (c->server_input_mine) tide_players_add(&mine, c->players);
        if (g->predict_inputs(world, &mine) && c->input_guessed == UINT32_MAX) c->input_guessed = tick;
    }
    if (g->let_go_inputs && (c->let_go == UINT32_MAX || g->predict_inputs) && tick - c->verified >= c->guess) {
        tide_player_set mine = {0};
        tide_players_add(&mine, (uint32_t)c->player);
        if (c->server_input_mine) tide_players_add(&mine, c->players);
        if (g->let_go_inputs(world, &mine) && c->let_go == UINT32_MAX) c->let_go = tick;
    }
    uint32_t size = 0;
    const uint8_t *own = g->set_input ? own_input(c, tick, &size) : NULL;
    if (own && g->read_input(own, size, c->input)) {
        const tide_player_id me = tide_player_from_index(c->player);
        g->set_input(world, me, c->input);
        if (c->server_input_mine) g->set_server_input(world, c->input);
    }
    const uint64_t started = c->desc.now_ns ? c->desc.now_ns() : 0u;
    c->ran_ahead++;
    run_tick(c, world, tick);
    finish_view_rewrite(c, tick + 1u);
    if (c->desc.now_ns) note_tick_cost(c, (double)(c->desc.now_ns() - started) * 1e-9);
}

// The predicted ticks from `from` on, again: the ones before it stand.
static void predict_from(tide_client *c, const uint32_t from)
{
    if (c->input_guessed < c->verified || c->input_guessed >= from) c->input_guessed = UINT32_MAX;
    c->let_go = UINT32_MAX; // Where the others let go is found again, from the tick confirmed now
    // What the rollback costs is measured tick by tick for the input delay to
    // go by (note_tick_cost): its first tick apart from the rest
    c->rerunning = c->desc.now_ns != NULL;
    c->rerun_tick = 0u;
    if (from < c->ahead) c->rerun_at = c->now;
    for (uint32_t t = from; t < c->ahead; t++) {
        capture_view_correction(c, t + 1u);
        run_predicted(c, t);
    }
    c->rerunning = false;
    c->again += c->ahead - from;
}

// From the verified world, the predicted ticks again.
static void predict_again(tide_client *c)
{
    predict_from(c, c->verified);
}

// A tick it ran as if the others had let go is within its guess once enough
// ticks before it are confirmed: it runs again with their last inputs, and so
// do the ticks after it, which started from its world. So while a player goes
// on as they were, each tick confirmed moves where they let go a tick on, and
// the ticks past that run again.
static void keep_guessing(tide_client *c)
{
    if (c->input_guessed < c->verified || c->input_guessed >= c->ahead) c->input_guessed = UINT32_MAX;
    if (c->let_go == UINT32_MAX) return;
    if (c->let_go >= c->ahead || c->let_go < c->verified) c->let_go = UINT32_MAX; // No tick it has was run that way
    else if (c->let_go - c->verified < c->guess) predict_from(c, c->let_go);
}

// The tick its clock says it should have an input for by now.
static int64_t due_input(const tide_client *c)
{
    return c->clock_base + (int64_t)ticks_in(c->now - c->clock_start, c->tick_rate);
}

// ...and the tick it should have run by now: its input delay short of that.
static int64_t due_tick(const tide_client *c)
{
    return due_input(c) - (int64_t)c->delay;
}

// Its clock: ahead of the server by its lead, and by the round trip it takes
// to get there (`trip`, in ticks), from where the server was as it said its
// tick was `server`. What the server says of its inputs' wait is about
// another clock until a tick sampled by this one has run (early_settled).
static void set_clock(tide_client *c, const uint32_t server, const uint32_t trip)
{
    c->clock_start = c->now;
    c->clock_base = (int64_t)server + c->desc.lead + (c->desc.lead ? trip : 0u);
    c->clock_slow = 0.0;
    c->early_watching = false;
    c->early_raised = false;
    c->early_settling = true;
    c->hold_until = c->now;
}

// No input is sampled for the ticks before `tick`: the server runs them
// without one of this machine's, as this machine predicts them, and what's
// sent starts after them (send_client).
static void skip_inputs(tide_client *c, const uint32_t tick)
{
    if (c->sampled >= tick) return;
    c->sampled = tick;
    c->sampled_from = tick;
}

// The ticks it may sample inputs past: the last one confirmed, or while it
// catches up, the last the server said it ran, which it will have confirmed
// by the time it predicts.
static uint32_t sampled_past(const tide_client *c)
{
    return c->catching_up && c->heard_tick > c->verified ? c->heard_tick : c->verified;
}

// Catching up can take several frames, or pause again before it finishes.
// Keep new samples at the first tick that can still arrive in time now,
// without changing inputs already sampled or restarting the catch-up.
static void skip_old_inputs(tide_client *c)
{
    int64_t soonest = due_input(c) - (int64_t)c->desc.lead;
    if (soonest < (int64_t)c->heard_tick) soonest = c->heard_tick;
    const int64_t most = (int64_t)sampled_past(c) + c->w.prediction - 1;
    if (soonest > most) soonest = most;
    skip_inputs(c, (uint32_t)soonest);
}

// It's behind what the server ran by more than it runs in an update: a world that just came, as old as it took to come, or
// this machine stopped for a while (a breakpoint, a sleeping tab), or heard nothing for longer than it predicts. It catches up
// with what's confirmed before it predicts again (play): a tick predicted from a world that old is a guess the server's ticks
// since then put right one by one, each running every tick after it again.
//
// What it had predicted goes. No input is sampled for a tick that can't be in time: its clock keeps its inputs `lead` ticks
// early, so the first that can is that far before the clock's, and never one the server said it ran. From there its inputs go
// by its clock, so the server has them in time while it catches up; what the server says of their wait meanwhile doesn't
// count (keep_early).
static void fall_behind(tide_client *c)
{
    if (c->ahead > c->verified) {
        reset_view_correction(c); // What views drew isn't there to correct from
        c->ahead = c->verified;
    }
    c->let_go = c->input_guessed = UINT32_MAX;
    c->catching_up = true;
    skip_old_inputs(c);
    c->early_watching = false;
    c->early_settling = true;
}

// A moment's allowance of the server's ticks: CATCH_UP_TICKS at the most, and twice what passes between its updates where
// that's more, so a machine whose frames are long still gains on the server. With a clock (tide_client_desc.now_ns), a game
// whose ticks are dear takes half the time between its updates at the most, so its frames stay about as long: but always as
// many ticks as came due since its last update, and one, or it would never be level, up to CATCH_UP_TICKS mandatory ticks.
// Past those the time budget applies: a machine that can't run the match's ticks in the time they take falls behind, which
// the server answers with the world again, rather than its frames growing longer each time.
//
// It's a moment's allowance, not a call's: a session updates its client twice
// in a frame, at the same time (tide_session_update), and the second has
// what the first left. An update's length is the shorter of the last two
// moments': one long frame isn't a slow machine, and a frame that catching up
// made long doesn't ask for more of the same.
static void catch_up_room(tide_client *c)
{
    if (c->now == c->played) return; // The same moment again
    const double took = c->now > c->played ? c->now - c->played : 0.0; // A host's time that went back is no time
    const double steady = took < c->took_before ? took : c->took_before;
    c->took_before = took;
    c->played = c->now;
    const double twice = 2.0 * steady * (double)c->tick_rate + 0.5;
    const double due = took * (double)c->tick_rate + 1.0;
    c->room = (catch_up){
        .most = twice > (double)CATCH_UP_TICKS ? (twice < 65536.0 ? (uint32_t)twice : 65536u) : CATCH_UP_TICKS,
        .least = due < (double)CATCH_UP_TICKS ? (uint32_t)due : CATCH_UP_TICKS,
        .time = (uint64_t)(steady * 0.5e9),
    };
    c->ran_now = 0;
    c->began_now = 0;
}

// What `world` has that `had` doesn't goes out to the handlers `as` says, as
// shown in `tick`.
static void hear_beyond(tide_client *c, const void *world, const void *had, const uint32_t tick, const uint8_t as)
{
    tide_event_log fresh = {0};
    c->game->hear_arrived(world, had, &fresh);
    for (uint32_t at = 0; at < fresh.size;) {
        tide_heard *heard = (tide_heard *)(fresh.bytes + at);
        at += heard->size;
        heard->tick = tick; // The session's count, as its ticks' events have
        if (heard->wants & as) tide_event_log_copy(c->desc.events, &fresh, heard, as);
    }
    tide_event_log_free(&fresh);
}

// `world` came for `tick`, and isn't among its worlds yet. The entities in it
// that this machine didn't have become available now: its local handlers
// hear Shown as the world has them (tide_game.hear_arrived).
// Which it had: before its first world, those of the world it was made with
// (tide_client_desc.base), if any. After, for the handlers that hear ticks as
// this machine runs them, those of its own world at that tick, as it has heard
// the ticks up to it; and for the handlers that hear ticks once they're
// confirmed, those of its last confirmed world, as the ticks since never were.
static void hear_arrived(tide_client *c, const void *world, const uint32_t tick)
{
    const tide_event_log *out = c->desc.events;
    if (!c->game->hear_arrived || !out || !out->taken) return;
    const void *ran = c->had, *sure = c->had;
    if (c->loaded) { // Its worlds are those from `verified` to `ahead`
        ran = world_at(c, tick < c->verified ? c->verified : tick > c->ahead ? c->ahead : tick);
        sure = world_at(c, c->verified);
    }
    if (ran == sure) {
        hear_beyond(c, world, ran, tick, TIDE_HEARD_PREDICTED | TIDE_HEARD_VERIFIED);
        return;
    }
    hear_beyond(c, world, ran, tick, TIDE_HEARD_PREDICTED);
    hear_beyond(c, world, sure, tick, TIDE_HEARD_VERIFIED);
}

static void hear_visibility_tick(tide_client *c, const void *world, const void *had,
                                 const pending_frame *frame, const uint32_t tick)
{
    if (!c->game->hear_visibility || !c->desc.events || !c->desc.events->taken) return;
    tide_event_log fresh = {0};
    c->game->hear_visibility(world, had, frame ? frame->destroyed : NULL,
                             frame ? frame->destroyed_count : 0u, &fresh);
    const tide_heard_tick *run = c->hearing.ticks ? &c->hearing.ticks[tick % c->hearing.ring] : NULL;
    for (uint32_t at = 0; at < fresh.size;) {
        tide_heard *heard = (tide_heard *)(fresh.bytes + at);
        at += heard->size;
        uint64_t predicted_handlers = heard->handlers;
        uint64_t verified_handlers = heard->handlers;
        // A genuine spawn in the locally run tick may already have emitted
        // Shown. Preserve each listener's once-only delivery through replay.
        if (run && run->used && run->tick == tick) {
            for (uint32_t i = 0; i < run->count; i++) {
                if (run->marks[i].event == heard->event && tide_entity_equal(run->marks[i].target, heard->target)) {
                    predicted_handlers &= ~run->marks[i].handlers;
                }
            }
            for (uint32_t prev = 0; prev < run->run.size;) {
                const tide_heard *earlier = (const tide_heard *)(run->run.bytes + prev);
                prev += earlier->size;
                if (earlier->event == heard->event && tide_entity_equal(earlier->target, heard->target)) {
                    verified_handlers &= ~earlier->handlers;
                }
            }
        }
        heard->tick = tick;
        const uint64_t handlers = heard->handlers;
        if ((heard->wants & TIDE_HEARD_PREDICTED) && (predicted_handlers || heard->event == TIDE_HEARD_ROWS)) {
            heard->handlers = predicted_handlers;
            tide_event_log_copy(c->desc.events, &fresh, heard, TIDE_HEARD_PREDICTED);
        }
        if ((heard->wants & TIDE_HEARD_VERIFIED) && (verified_handlers || heard->event == TIDE_HEARD_ROWS)) {
            heard->handlers = verified_handlers;
            tide_event_log_copy(c->desc.events, &fresh, heard, TIDE_HEARD_VERIFIED);
        }
        heard->handlers = handlers;
    }
    tide_event_log_free(&fresh);
}

static void suppress_guessed_lifecycle(tide_client *c, const uint32_t tick)
{
    tide_heard_tick *run = c->hearing.ticks ? &c->hearing.ticks[tick % c->hearing.ring] : NULL;
    if (run && run->used && run->tick == tick) {
        // Verified lifecycle handlers receive the server's actual event state,
        // never the component copies from an incomplete client's guessed tick.
        uint32_t kept = 0, count = 0;
        for (uint32_t at = 0; at < run->run.size;) {
            tide_heard *heard = (tide_heard *)(run->run.bytes + at);
            at += heard->size;
            // A guessed destruction cannot suppress verified Hidden when the
            // authoritative event-time policy instead concealed the entity.
            if (heard->event == TIDE_HEARD_DESTROYED_CAUSE) continue;
            if (heard->wants & TIDE_HEARD_LIFECYCLE) heard->handlers = 0;
            const uint32_t size = heard->size;
            if (kept != at - size) memmove(run->run.bytes + kept, heard, size);
            kept += size;
            count++;
        }
        run->run.size = kept;
        run->run.count = count;
        if (!kept) run->run.first_tick = 0;
    }
}

static void hear_authoritative_events(tide_client *c, tide_event_log *events, const uint32_t tick)
{
    if (!c->desc.events || !c->desc.events->taken) return;
    if (tick != UINT32_MAX) suppress_guessed_lifecycle(c, tick);
    for (uint32_t at = 0; at < events->size;) {
        tide_heard *heard = (tide_heard *)(events->bytes + at);
        at += heard->size;
        const uint32_t event_tick = tick == UINT32_MAX ? heard->tick : tick;
        if (heard->event == TIDE_HEARD_DESTROYED_CAUSE) {
            // Generated local delivery uses this private cause when comparing
            // the last presented and current frame endpoints. It has no
            // listener bits and must never reach a game event handler.
            heard->tick = event_tick;
            tide_event_log_copy(c->desc.events, events, heard, TIDE_HEARD_PREDICTED | TIDE_HEARD_VERIFIED);
            continue;
        }
        tide_heard_tick *run = c->hearing.ticks ? &c->hearing.ticks[event_tick % c->hearing.ring] : NULL;
        const uint64_t handlers = heard->handlers;
        uint64_t predicted = handlers;
        if (run && run->used && run->tick == event_tick && heard->event != TIDE_HEARD_ROWS) {
            for (uint32_t i = 0; i < run->count; i++) {
                const tide_heard_mark *mark = &run->marks[i];
                const bool same = mark->born && heard->born ? mark->born == heard->born
                    : tide_entity_equal(mark->target, heard->target);
                if (mark->event == heard->event && same) predicted &= ~mark->handlers;
            }
        }
        heard->tick = event_tick;
        if ((heard->wants & TIDE_HEARD_PREDICTED) && (predicted || heard->event == TIDE_HEARD_ROWS)) {
            heard->handlers = predicted;
            tide_event_log_copy(c->desc.events, events, heard, TIDE_HEARD_PREDICTED);
        }
        if ((heard->wants & TIDE_HEARD_VERIFIED) && (handlers || heard->event == TIDE_HEARD_ROWS)) {
            heard->handlers = handlers;
            tide_event_log_copy(c->desc.events, events, heard, TIDE_HEARD_VERIFIED);
        }
        heard->handlers = handlers;
    }
}

static void load_snapshot(tide_client *c)
{
    const tide_game *g = c->game;
    const uint32_t tick = c->snapshot_tick;
    const bool repair = c->snapshot_kind == SEND_CLIENT_REPAIR;
    // Built from the world it answered these pages' hashes with, if it did
    const void *base = c->answered && c->answered_tick == tick ? c->base : NULL;
    void *world = calloc(1, g->world_size);
    tide_entity *destroyed = NULL;
    uint32_t destroyed_count = 0;
    tide_event_log journal = {0};
    bool ok = world && (g->client_state
        ? unpack_client_state(g, world, repair && c->loaded ? world_at(c, c->verified) : NULL,
            tide_player_from_index(c->player), c->verified, c->snapshot, c->snapshot_size, &destroyed, &destroyed_count, &journal)
        : g->unpack_delta(world, base, c->snapshot, c->snapshot_size));
    for (uint32_t at = 0; ok && repair && at < journal.size;) {
        const tide_heard *heard = (const tide_heard *)(journal.bytes + at);
        at += heard->size;
        if (heard->tick < c->verified || heard->tick >= tick) ok = false;
    }
    c->got_number = c->snapshot_number;
    stop_receiving(c);
    c->answered = false;
    if (!ok) { // Asked for again, whole if it was built from a world of its own
        c->asset_failed = true;
        free_world(g, world);
        free(world);
        free(destroyed);
        tide_event_log_free(&journal);
        if (base) c->base_failed = true;
        c->need_snapshot = true;
        c->repair_tick = 0;
        c->repair_stream = false;
        return;
    }
    if (repair) {
        const uint32_t from = c->verified;
        pending_frame *frame = &c->frames[(tick - 1u) % c->w.history];
        const bool has_frame = frame_complete(c, frame, tick - 1u);
        if (!c->loaded || tick <= from || tick > c->server_tick
            || (has_frame && g->client_hash(world, tide_player_from_index(c->player)) != frame_hash(frame))) {
            free_world(g, world);
            free(world);
            free(destroyed);
            tide_event_log_free(&journal);
            c->repair_tick = 0;
            c->repair_stream = false;
            c->need_snapshot = true;
            return;
        }
        capture_view_correction(c, from + 1u);
        for (uint32_t t = from; t < tick; t++) suppress_guessed_lifecycle(c, t);
        hear_authoritative_events(c, &journal, UINT32_MAX);
        tide_event_log_free(&journal);
        const pending_frame transition = {.destroyed = destroyed, .destroyed_count = destroyed_count};
        hear_visibility_tick(c, world, world_at(c, from), &transition, tick - 1u);
        free(destroyed);
        g->copy_world(world_at(c, tick), world);
        free_world(g, world);
        free(world);
        finish_view_rewrite(c, tick);
        if (tick == from + 1u) tide_hearing_confirm(&c->hearing, from);
        else {
            // Intermediate snapshots were replaced by a complete endpoint.
            // Only the server journal can establish events in that interval.
            tide_hearing_skip_to(&c->hearing, tick);
            c->first_tick = tick;
            c->verified_view = (double)tick;
            c->presented_view = (tide_view_time){0};
        }
        for (uint32_t i = 0; i < c->w.history; i++) {
            if (c->frames[i].data && c->frames[i].tick < tick) drop_frame(&c->frames[i]);
        }
        c->verified = tick;
        c->repair_stream = c->snapshot_stream;
        c->repair_tick = c->repair_stream ? tick + 1u : 0u;
        c->need_snapshot = false;
        if (c->ahead < tick) { c->ahead = tick; skip_inputs(c, tick); }
        if (c->catching_up) c->verified_view = (double)tick;
        if (c->ahead > tick) predict_again(c);
        return;
    }
    drop_base(c);
    c->base_failed = false;
    // The world is as old as it took to come: what views drew, and what it
    // had predicted, have nothing to do with it. It starts from this world,
    // behind, and catches up with the ticks since before it predicts (fall_behind).
    reset_view_correction(c);
    c->let_go = c->input_guessed = UINT32_MAX;
    if (g->client_state && g->hear_visibility) {
        const pending_frame transition = {.destroyed = destroyed, .destroyed_count = destroyed_count};
        hear_visibility_tick(c, world, c->loaded ? world_at(c, c->verified) : c->had, &transition, tick);
    } else {
        hear_arrived(c, world, tick); // Against its old worlds, before this one takes their place
    }
    free(destroyed);
    hear_authoritative_events(c, &journal, UINT32_MAX);
    tide_event_log_free(&journal);
    drop_had(c);
    void **slot = &c->worlds[tick % c->ring];
    free_world(g, *slot);
    free(*slot);
    *slot = world;
    const bool first = !c->loaded;
    // The ticks before it were never confirmed one by one: what they sent stays unsaid
    tide_hearing_skip_to(&c->hearing, tick);
    c->loaded = true;
    c->first_tick = tick;
    c->need_snapshot = false;
    c->repair_tick = 0;
    c->repair_stream = false;
    c->commands_need_world = false;
    c->verified = c->ahead = tick;
    c->verified_view = (double)tick;
    c->presented_view = (tide_view_time){0};
    // The server ran the ticks up to where it was as it sent the world
    if (c->server_tick > c->heard_tick) c->heard_tick = c->server_tick;
    if (tick > c->heard_tick) c->heard_tick = tick;
    for (uint32_t i = 0; i < c->w.history; i++) {
        if (c->frames[i].tick < tick) drop_frame(&c->frames[i]);
    }
    if (first) {
        c->state = TIDE_SESSION_CONNECTED;
        c->sampled = tick;
        c->sampled_from = tick;
        // The moment the world came in has an update's worth to run, as any
        c->played = c->now;
        c->took_before = 0.0;
        c->room = (catch_up){.most = CATCH_UP_TICKS, .least = 1u, .time = 0u};
        c->ran_now = 0;
        c->began_now = 0;
        // Ahead of the server by its lead, and the round trip it takes to get
        // there: from where the server was as it sent the world, which may
        // have taken a while (and a round trip more for its pages' hashes)
        const uint32_t trip = (uint32_t)ticks_in((double)c->rtt_ms / 1000.0, c->tick_rate);
        set_clock(c, c->server_tick > tick ? c->server_tick : tick, trip);
        c->early_from = c->now;
        c->frame = 0.0;
        // Its input delay (tide_prediction): none to start with, when it
        // predicts. When it doesn't, enough that a tick's frame has come by the
        // time it runs the tick: the lead and round trip its inputs go ahead
        // by, and half a round trip more for the frame to come back, and a
        // little for the frame to be late by. Not more than its inputs' ring
        // holds (windows.inputs) less what the clock runs ahead meanwhile.
        c->predicts = g->prediction != TIDE_PREDICTION_DELAY;
        c->delay_cap = c->w.prediction > 8u ? c->w.prediction - 8u : 0u;
        const uint32_t delay = c->desc.lead ? c->desc.lead + trip + (trip + 1u) / 2u + 2u : 0u;
        c->delay = c->predicts ? 0u : delay < c->delay_cap ? delay : c->delay_cap;
        c->tick_cost = 0.0;
        c->rerun_count = 0u;
        c->rerun_at = 0.0;
        c->rerunning = false;
        c->resim_now = 0.0;
        c->resim_load = 0.0;
        c->delay_at = 0.0;
        c->lead_mean = 0.0;
        c->rollback_now = 0.0;
        c->most_now = 0.0;
        c->slack_since = c->now;
        c->delay_hold_until = c->now + (double)c->rtt_ms / 1000.0 + 3.0 / c->tick_rate;
    } else {
        c->resyncs++;
    }
    fall_behind(c);
}

// Samples this machine's input for the next tick to have one, which goes out
// to the server, and in when that tick runs.
static void sample_input(tide_client *c)
{
    const tide_game *g = c->game;
    const uint32_t tick = c->sampled;
    if (g->set_input && c->desc.sample) {
        c->desc.sample(c->desc.user, tick, c->input);
        if (g->set_view_time) g->set_view_time(c->input, c->presented_view);
        const uint32_t slot = tick % c->w.inputs;
        uint8_t *packed = c->inputs + (size_t)slot * g->max_input_bytes;
        const uint32_t size = g->write_input(c->input, packed, g->max_input_bytes);
        c->input_tick[slot] = size ? tick : UINT32_MAX;
        c->input_size[slot] = (uint16_t)size;
    }
    c->sampled++;
}

// Runs one tick ahead, with the input sampled for it.
static void predict(tide_client *c)
{
    run_predicted(c, c->ahead);
    c->ahead++;
}

// Keeps its inputs as early as they have to be, and no earlier: time an input waits at the server before its tick runs is
// time later that the other players see what this one did, and time further this machine runs ahead, guessing. The server says
// how long this machine's inputs waited for the ticks it last ran (note_wait); the least counts, as being early is for the
// input that's nearly late.
//
// The least is kept at the client's lead, in ticks: none on the server's machine, whose inputs go straight in. Over a network
// (lead_frame) one of this machine's frames, or a 60th of a second where its frames are shorter, so a late frame or a lost
// datagram still leaves the input in time.
static int32_t early_wanted(const tide_client *c)
{
    const int32_t ticks = (int32_t)((double)c->desc.lead * WAITS / (double)c->tick_rate + 0.5);
    if (!c->desc.lead_frame) return ticks;
    const int32_t frame = (int32_t)((c->frame > EARLY_FRAME ? c->frame : EARLY_FRAME) * WAITS + 0.5);
    return frame < ticks ? frame : ticks;
}

// The client watches a while at a time. Under what's wanted, an input was nearly late or late: its clock goes ahead at once by
// what was missing. Over it for the whole while by as much again (a tick at most), its clock loses the difference, a little of
// each update, so no tick stands still. The while is long after an input was nearly late, as what made it so may come again,
// and is not applied in a match's first second (a starting machine stutters). A client that sends no inputs has nothing to
// keep early.
//
// What the server says is about the ticks it ran, so after a clock change and on joining nothing it says counts until a tick
// whose input was sampled by the clock as it is now has run (early_settled). By the tick, never by an assumed round trip: on
// a network that's late or loses datagrams, what was said before the change is still heard well after it.
static void keep_early(tide_client *c)
{
    if (!c->game->set_input || !c->desc.sample || c->now < c->hold_until || c->early_settling) return;
    if (!c->waited_known || c->waited_tick <= c->early_tick) return;
    if (!c->early_watching) {
        c->early_watching = true;
        c->early_least = c->waited;
        c->early_since = c->now;
        c->early_updates = 0;
    }
    c->early_updates++;
    if (c->early_least < early_wanted(c)) {
        c->clock_start -= (double)(early_wanted(c) - c->early_least) / WAITS;
        c->clock_slow = 0.0;
        c->early_raised = c->now - c->early_from >= EARLY_START;
        c->early_settling = true;
        c->early_watching = false;
    } else if (c->now - c->early_since >= (c->early_raised ? EARLY_KEEP : EARLY_LOOK)) {
        if (c->early_updates > 1u) c->frame = (c->now - c->early_since) / (double)(c->early_updates - 1u);
        const int32_t wanted = early_wanted(c);
        const int32_t tick = (int32_t)(WAITS / (double)c->tick_rate + 0.5);
        const int32_t room = wanted > tick ? tick : wanted > 0 ? wanted : 1; // What it may be over by: it's never just so
        if (c->early_least >= wanted + room) {
            const double lose = (double)(c->early_least - wanted) / WAITS;
            c->clock_slow = lose;
            c->hold_until = c->now + lose / CLOCK_SLOW; // Until it has lost it
            c->early_settling = true;
        }
        c->early_raised = false;
        c->early_watching = false; // From here again: what was the least may be no more
    }
}

// After a change to its clock, the first tick whose input is sampled when its
// time comes by the clock as it is: the next one, once it has sampled up to
// where the clock is and isn't losing time any more. The inputs it sampled to
// get there went out together, the first of them later than their ticks ask.
static void early_settled(tide_client *c, const int64_t due)
{
    if (!c->early_settling || c->now < c->hold_until || (int64_t)c->sampled < due) return;
    c->early_tick = c->sampled;
    c->early_settling = false;
}

// A clock far ahead comes back at once. Its inputs go no further than a second past what's confirmed (windows.prediction), so
// with its clock past that the server only ever says they wait about that long, and losing it a little of each update
// (keep_early) takes rounds of ten seconds and more, with the others seeing this player a second late. A clock gets there when
// the server's machine stopped for a while: its ticks dropped the time, and this clock didn't.
//
// It's the server's word that says so, never frames that didn't come: in an update that heard from the server, with every tick
// it said it ran run here too, the clock is still past what can be sampled, and a clock set now as a join sets one would be
// within that. A datagram that was long on its way took that long to answer, which puts the clock a join would set as much
// further ahead: only a server whose ticks fell behind the time reads as a clock too far. In two such updates in a row.
//
// The clock is set as a join sets it. What was predicted past it goes, to be run again as the clock gets there, with the
// inputs already sampled for those ticks, which the server has: nothing more is sampled until then.
static void clock_back(tide_client *c)
{
    if (c->heard_update != c->updates || !c->heard_timed) return; // Nothing new from the server in this update
    const uint32_t trip = (uint32_t)ticks_in((double)c->heard_trip_ms / 1000.0, c->tick_rate);
    const int64_t joined = (int64_t)c->heard_tick + c->desc.lead + (c->desc.lead ? trip : 0u);
    const int64_t most = (int64_t)c->verified + c->w.prediction;
    const bool far = c->verified >= c->heard_tick && due_input(c) > most && joined <= most;
    c->far = far ? c->far + 1u : 0u;
    if (c->far < 2u) return;
    c->far = 0;
    set_clock(c, c->heard_tick, trip);
    const int64_t run_to = due_tick(c);
    if ((int64_t)c->ahead > run_to) {
        reset_view_correction(c); // Views drew a tick that's gone
        c->ahead = run_to > (int64_t)c->verified ? (uint32_t)run_to : c->verified;
    }
}

static void play(tide_client *c)
{
    catch_up_room(c);
    // The server has run ticks past the last this machine sampled an input
    // for, by more than it runs in an update: it's behind
    if (!c->catching_up && !c->need_snapshot && !c->receiving && c->heard_tick > c->sampled
        && c->heard_tick - c->sampled > c->room.most) {
        fall_behind(c);
    }
    if (c->catching_up) skip_old_inputs(c);

    // Ticks the server confirmed. One that went as predicted is already in its
    // snapshot, which only needs its hash checked. One that didn't runs on the
    // snapshot before it, and the predicted ones after it are stale; nor did
    // one it ran as if the others had let go (`let_go`), which they hadn't if
    // their inputs stayed. One past what it ran, whose tick its clock hasn't
    // reached, waits for the clock, so ticks run at the match's pace however
    // early their frames come. No more are run than a moment may
    // (catch_up_room): the rest wait for the frames to come, so that seconds
    // of ticks, after a world that took as long to arrive, are no one frame's
    // work.
    bool stale = false;
    bool waits = false; // The next tick's frame is there, and its time hasn't come
    bool out_of_room = false; // A complete, due confirmed tick is left for another frame
    const int64_t due = due_tick(c);
    while (!c->need_snapshot && !c->receiving && !c->repair_tick) {
        const uint32_t tick = c->verified;
        pending_frame *f = &c->frames[tick % c->w.history];
        if (!frame_complete(c, f, tick)) break;
        if (tick >= c->ahead && (int64_t)tick >= due) {
            waits = true;
            break;
        }
        if (tick >= c->ahead && !room_ahead(c)) {
            out_of_room = true;
            break;
        }
        if (c->game->client_state && (f->data[8] & FRAME_REPAIR)) {
            c->repair_tick = tick + 1u;
            break;
        }
        const bool predicted = tick < c->ahead && !stale && tick < c->let_go && tick < c->input_guessed && as_predicted(c, f);
        if (!predicted && c->ran_now >= c->room.most) {
            out_of_room = true;
            break;
        }
        if (!predicted && c->desc.now_ns) {
            if (c->ran_now == 0) c->began_now = c->desc.now_ns();
            else if (c->ran_now >= c->room.least && c->desc.now_ns() - c->began_now >= c->room.time) {
                out_of_room = true;
                break;
            }
        }
        // An input it sampled came too late: not a tick it sampled none for,
        // as before it had a world, or while it was behind
        uint32_t own_size = 0;
        if (own_late(c, f) && own_input(c, tick, &own_size)) c->late++;
        bool ok;
        if (predicted) {
            ok = (c->game->client_hash
                ? c->game->client_hash(world_at(c, tick + 1u), tide_player_from_index(c->player))
                : hash_of(c->game, world_at(c, tick + 1u), c->desc.jobs)) == frame_hash(f);
        } else {
            if (tick < c->ahead) capture_view_correction(c, tick + 1u);
            c->game->copy_world(world_at(c, tick + 1u), world_at(c, tick));
            ok = apply_frame(c, world_at(c, tick + 1u), f);
            finish_view_rewrite(c, tick + 1u);
            stale |= tick + 1u < c->ahead;
            c->ran_now++;
        }
        if (!ok) { // Diverged: ask for the world again, and keep predicting meanwhile
            if (c->game->client_state) c->repair_tick = tick + 1u;
            else { drop_frame(f); c->need_snapshot = true; }
            break;
        }
        if (c->game->tick_client_events) hear_authoritative_events(c, &f->events, tick);
        hear_visibility_tick(c, world_at(c, tick + 1u), world_at(c, tick), f, tick);
        drop_frame(f);
        tide_hearing_confirm(&c->hearing, tick); // What it sent is certain now
        c->verified++;
        // Catch-up consumes old backlog faster than real time. Present its newest
        // confirmed state so retaining a display interval cannot stall catch-up.
        if (c->catching_up) c->verified_view = (double)c->verified;
        if (c->ahead < c->verified) c->ahead = c->verified;
        skip_inputs(c, c->ahead); // Behind the server: no input was sampled for those ticks
    }
    // Input delay can leave the world behind inputs already sampled. A
    // shorter pause then misses few inputs but leaves more confirmed work
    // than this frame may run: finish that work before making predictions.
    if (!c->catching_up && out_of_room && c->heard_tick > c->sampled) fall_behind(c);
    // Level with the server once it has run every tick the server said it
    // ran, or the next one's time hasn't come: from there it predicts
    if (c->catching_up && !c->need_snapshot && !c->receiving && (c->verified >= c->heard_tick || waits)) {
        c->catching_up = false;
    }
    if (!c->catching_up) {
        clock_back(c);
        if (stale && c->ahead > c->verified) predict_again(c);
        keep_guessing(c);
        keep_early(c);
    }

    // Inputs up to where the clock is, as far as their ring goes; then, if it
    // predicts and isn't behind, the ticks up to its delay short of them, no
    // further than its clock is
    const int64_t due_in = due_input(c);
    const uint32_t past = sampled_past(c);
    // Even a short pause, without catch-up, can miss a tick the server ran.
    // Sampling one now cannot make it arrive in time. Inputs already stored
    // remain available for confirmation and genuine late-input accounting.
    skip_inputs(c, c->heard_tick);
    for (uint32_t n = 0; (int64_t)c->sampled < due_in && (c->sampled < past || c->sampled - past < c->w.prediction) && n < 16u; n++) {
        sample_input(c);
    }
    early_settled(c, due_in);
    if (c->catching_up) return;
    const uint32_t reached = (int64_t)c->sampled < due_in ? c->sampled : due_in > 0 ? (uint32_t)due_in : 0u;
    const uint32_t short_of = reached > c->delay ? reached - c->delay : 0u;
    const uint32_t target = short_of > c->ahead ? short_of : c->ahead;
    for (uint32_t n = 0; c->predicts && c->ahead < target && n < 16u && room_ahead(c); n++) predict(c);
}

// Its input delay (tide_prediction): how many ticks short of the ones it has inputs for it runs.
//
// Predicting, it's the least at which running ticks again fits what a frame can afford, by what that costs in practice, so a
// client whose guesses are right pays nothing for them, and none at all on the server's machine. What one rollback would cost
// now is what the last ones' first ticks cost (rerun_first_cost, or `tick_cost` before any) plus a tick's cost for each of
// the rest, over how far ahead it's been of late and a tick (`lead_mean`, while level with the server); it may take up to a
// frame, never less than the budget nor a 60th of a second (ROLLBACK_FRAMES, ROLLBACK_SECONDS). What rollbacks cost on the
// whole, an update, may take up to the budget (`resim_load`; an update counts for two budgets at most, so one rollback a
// stall made long isn't a load). It takes a tick more when either is over, and one back once both would still hold with a
// tick less, with a share to spare (ROOM_SHARE), for ROOM_SECONDS. With no rollback for QUIET_SECONDS it takes none more and
// takes them back a tick at a time, as what it reckons by may be stale.
//
// Not predicting, it takes a tick more when the clock reached a tick whose frame hasn't come, and a tick less once frames
// have come well before their ticks for a while. Each change takes a round trip to show, so there's one at a time.
static void adjust_delay(tide_client *c)
{
    if (!c->loaded) return;
    const double budget = c->desc.resim_budget > 0.0 ? c->desc.resim_budget : RESIM_BUDGET;
    const double frame = (double)c->room.time * 2e-9 * ROLLBACK_FRAMES; // room.time is half the time between updates
    double most = frame > budget ? frame : budget;                      // What one rollback may take
    if (most < ROLLBACK_SECONDS) most = ROLLBACK_SECONDS;
    double rollback = 0.0; // What one would cost now
    double ticks = 1.0;    // ...running this many ticks again
    bool settled = false;  // Its lead is what it's been of late: not a stall's, nor a change's on its way
    if (c->predicts && c->desc.now_ns) {
        // What running ticks again has cost an update, on the whole, over the
        // updates of late: an update that a stall made long is one update
        double since = c->delay_at > 0.0 && c->now > c->delay_at ? c->now - c->delay_at : 0.0;
        if (since > 0.1) since = 0.1;
        c->delay_at = c->now;
        const double spent = c->resim_now < budget * 2.0 ? c->resim_now : budget * 2.0;
        c->resim_load += (spent - c->resim_load) * (since / (since + LOAD_SECONDS));
        c->resim_now = 0.0;
        // How far ahead it's been of late, while level with the server: a
        // server that fell silent for a while, or whose ticks it's still
        // confirming after one did, leaves it further ahead than a rollback
        // is ever about
        const bool level = c->heard_tick <= c->verified + LEVEL_TICKS && c->now - c->last_heard <= 4.0 / c->tick_rate;
        const double lead = (double)(c->ahead - c->verified);
        if (level) c->lead_mean += (lead - c->lead_mean) * (since / (since + LEAD_SECONDS));
        const double apart = lead - c->lead_mean;
        settled = level && apart <= LEAD_SETTLED && apart >= -LEAD_SETTLED;
        const double first = rerun_first_cost(c);
        ticks = c->lead_mean + 1.0;
        rollback = (first > 0.0 ? first : c->tick_cost) + (ticks - 1.0) * c->tick_cost;
        c->rollback_now = rollback;
        c->most_now = most;
    }
    if (c->now < c->delay_hold_until) return;
    int change = 0;
    if (c->predicts) {
        if (!c->desc.now_ns || c->desc.lead == 0u) return; // No clock: the same run on every machine. No lead: nothing to run again
        const bool quiet = c->rerun_at == 0.0 || c->now - c->rerun_at > QUIET_SECONDS;
        // A tick more only for a lead that's settled: a client whose clock jumped
        // ahead while its machine stalled runs far ahead for a moment, and that
        // moment is no reason for a delay that would take seconds to come back
        if (!quiet && settled && (rollback > most || c->resim_load > budget)) change = 1;
        else {
            // With a tick of delay less it's a tick further ahead: a tick more to run again, each time.
            // Where that leaves half of what it may take to spare, the delay is clearly more than it
            // needs, and comes back a tick at a time as fast as changes can show
            const double more = c->resim_load * (ticks + 1.0) / ticks;
            const bool room = quiet || (rollback + c->tick_cost <= most * ROOM_SHARE && more <= budget * ROOM_SHARE);
            const bool spare = quiet || (rollback + c->tick_cost <= most * 0.5 && more <= budget * 0.5);
            if (!room) c->slack_since = c->now;
            else if (spare || c->now - c->slack_since > ROOM_SECONDS) change = -1;
        }
    } else {
        const int64_t due = due_tick(c);
        uint32_t has = 0; // Ticks from `verified` whose frames have come
        while (has < 32u && frame_complete(c, &c->frames[(c->verified + has) % c->w.history], c->verified + has)) has++;
        const bool late = (int64_t)c->verified < due && has == 0;
        const bool early = (int64_t)(c->verified + has) > due + SLACK_TICKS;
        if (late) change = 1;
        else if (early && c->now - c->slack_since > SLACK_SECONDS) change = -1;
        if (!early) c->slack_since = c->now;
    }
    if (change > 0 && c->delay < c->delay_cap) c->delay++;
    else if (change < 0 && c->delay > 0) c->delay--;
    else return;
    if (c->predicts) {
        // What rollbacks have cost is about the lead it had: with a tick of
        // delay more each runs a tick less again, and with one less a tick
        // more, until what they cost from here says otherwise; and the lead
        // itself changes by a tick at once
        c->resim_load *= change > 0 ? (ticks - 1.0) / ticks : (ticks + 1.0) / ticks;
        c->lead_mean += change > 0 ? -1.0 : 1.0;
        if (c->lead_mean < 0.0) c->lead_mean = 0.0;
    }
    c->slack_since = c->now;
    c->delay_hold_until = c->now + (double)c->rtt_ms / 1000.0 + 3.0 / c->tick_rate;
}

static void send_hello(tide_client *c)
{
    uint8_t data[96];
    tide_writer w = {data, sizeof data, 0, false, false};
    header(&w, MSG_HELLO);
    tide_write_u64(&w, c->game->hash);
    tide_write_bytes(&w, c->game->simulation, 32);
    tide_write_u32(&w, c->nonce);
    tide_write_u16(&w, said_time(c));
    tide_write_u64(&w, c->desc.cookie);
    tide_write_u8(&w, (c->desc.knew_kick ? HELLO_KNEW_KICK : 0u) | (c->base ? HELLO_HAS_WORLD : 0u)
                          | (can_start(c) ? HELLO_CAN_START : 0u));
    uint8_t asset_pack[32] = {0};
    if (!c->asset_failed) tide_pack_digest(asset_pack);
    tide_write_bytes(&w, asset_pack, 32);
    send_packet(&c->desc.transport, c->desc.server, &w, &c->traffic);
    c->last_hello = c->now;
}

static void send_client(tide_client *c)
{
    const tide_game *g = c->game;
    tide_writer w = {c->packet, TIDE_NET_MTU, 0, false, false};
    header(&w, MSG_CLIENT);
    tide_write_u16(&w, said_time(c));
    tide_write_u16(&w, c->their_time);
    // Whether it needs the world, or is taking the one coming, and has one to build it from
    const bool wants = c->need_snapshot || !c->loaded || c->receiving || c->repair_tick;
    const bool has = !c->base_failed && (c->loaded || c->base);
    const bool lacks = c->answered && !c->receiving;
    tide_write_u8(&w, (wants ? CLIENT_WANTS_WORLD : 0u) | (wants && has ? CLIENT_HAS_WORLD : 0u) | (lacks ? CLIENT_LACKS : 0u)
                          | (wants && can_start(c) ? CLIENT_CAN_START : 0u) | (wants || lacks ? CLIENT_ASSETS : 0u)
                          | (c->repair_tick ? CLIENT_REPAIR : 0u) | (c->repair_stream ? CLIENT_REPAIR_STREAM : 0u));
    if (wants || lacks) {
        uint8_t asset_pack[32] = {0};
        if (!c->asset_failed) tide_pack_digest(asset_pack);
        tide_write_bytes(&w, asset_pack, 32);
    }
    // The chunks it has of what's being received, or that it has all of the last thing
    uint32_t chunk_number = 0;
    uint32_t chunk_ack = 0;
    uint64_t chunk_mask = 0;
    if (c->receiving) {
        chunk_number = c->snapshot_number;
        while (chunk_ack < c->chunk_count && c->chunk_have[chunk_ack]) chunk_ack++;
        for (uint32_t i = 0; i < 64u && chunk_ack + 1u + i < c->chunk_count; i++) {
            if (c->chunk_have[chunk_ack + 1u + i]) chunk_mask |= (uint64_t)1 << i;
        }
    } else if (c->got_number) {
        chunk_number = c->got_number;
        chunk_ack = UINT32_MAX;
    }
    // Its number (0: nothing yet), then 0 for all of it, or the chunks it has
    // from the first, plus one, and which it has after those
    tide_write_varint(&w, chunk_number);
    if (chunk_number) tide_write_varint(&w, chunk_ack == UINT32_MAX ? 0u : chunk_ack + 1u);
    if (chunk_number && chunk_ack != UINT32_MAX) tide_write_u64(&w, chunk_mask);
    uint64_t frame_mask = 0;
    for (uint32_t i = 0; i < 64u && i < c->w.history; i++) {
        const uint32_t tick = c->verified + 1u + i;
        if (frame_complete(c, &c->frames[tick % c->w.history], tick)) frame_mask |= 1ull << i;
    }
    tide_write_varint(&w, c->verified);
    tide_write_varint64(&w, frame_mask);
    if (c->repair_tick) tide_write_varint(&w, c->repair_tick);
    if (lacks) { // Until the world comes
        tide_write_varint(&w, c->answered_number);
        tide_write_varint(&w, c->lacks_size);
        tide_write_bytes(&w, c->lacks, c->lacks_size);
    }
    // The big command on its way down, in a game with commands: the chunks it
    // has of the lowest one coming, or that it has all of the last, as for a
    // world's chunks above
    if (g->command) {
        const download *coming = NULL;
        uint32_t whole = c->download_done;
        for (uint32_t i = 0; i < DOWNLOADS_MOST; i++) {
            const download *d = &c->downloads[i];
            if (!d->number) continue;
            if (download_whole(d)) {
                if (d->number > whole) whole = d->number;
            } else if (!coming || d->number < coming->number) {
                coming = d;
            }
        }
        if (coming) {
            uint32_t ack;
            uint64_t mask;
            chunks_had(coming->have, coming->chunks, &ack, &mask);
            tide_write_varint(&w, coming->number);
            tide_write_varint(&w, ack + 1u);
            tide_write_u64(&w, mask);
        } else {
            tide_write_varint(&w, whole);
            if (whole) tide_write_varint(&w, 0);
        }
    }

    // Inputs from the first the server lacks, as many as fit: after the
    // first, each as what differs from the one before, when the game packs
    // them that way
    const bool deltas = g->read_input && g->write_input_delta;
    uint8_t *previous = c->scratch;
    uint8_t *current = c->scratch + g->input_size;
    uint8_t *delta = current + g->input_size;
    uint32_t first = c->input_ack > c->verified ? c->input_ack : c->verified;
    if (first < c->sampled_from) first = c->sampled_from; // It sampled none for the ticks before (skip_inputs)
    if (first > c->sampled) first = c->sampled;
    tide_write_varint(&w, first - c->verified); // From the ticks it has
    const uint32_t count_at = w.size;
    tide_write_u8(&w, 0);
    uint32_t count = 0;
    for (uint32_t t = first; t < c->sampled && count < 255u; t++) {
        uint32_t size = 0;
        const uint8_t *own = own_input(c, t, &size);
        if (!own) break;
        if (deltas) {
            if (!g->read_input(own, size, current)) break;
            if (count > 0) {
                size = g->write_input_delta(current, previous, delta, g->max_input_bytes);
                own = delta;
            }
            uint8_t *swap = previous;
            previous = current;
            current = swap;
        }
        if (size == 0 || w.size + varint_size(size) + size > TIDE_NET_MTU) break;
        tide_write_varint(&w, size);
        tide_write_bytes(&w, own, size);
        count++;
    }
    c->packet[count_at] = (uint8_t)count;
    send_packet(&c->desc.transport, c->desc.server, &w, &c->traffic);
}

// Confirmation comes from a verified world, rather than packet receipt, so
// intent survives a host disappearing before its tick has been verified.
// The last pending command's sequence in `channel`, or the verified world's
// if none is pending there: what the next takes its sequence after. For
// unordered commands, the highest id of either.
static uint32_t last_sequence(const tide_client *c, const uint32_t channel)
{
    const tide_player_id player = tide_player_from_index(c->player);
    uint32_t last = c->game->command_sequence(world_at(c, c->verified), player, channel);
    for (uint32_t i = 0; i < c->commands.count; i++) {
        const pending_command *cmd = &c->commands.items[i];
        if (cmd->channel == channel && cmd->sequence > last) last = cmd->sequence;
    }
    return last;
}

static void confirm_commands(tide_client *c)
{
    const tide_game *g = c->game;
    if (!c->loaded || !g->command_sequence) return;
    const void *world = world_at(c, c->verified);
    const tide_player_id player = tide_player_from_index(c->player);
    for (uint32_t i = 0; i < c->commands.count; i++) {
        const pending_command *cmd = &c->commands.items[i];
        if (!command_done(g, world, player, cmd->channel, cmd->sequence)) continue;
        if (c->upload_number && cmd->channel == c->upload_channel && cmd->sequence == c->upload_sequence) stop_upload_client(c);
        remove_command(&c->commands, i--);
    }
    if (c->commands_rebase) {
        // The next host can have verified less than this sender. Retained
        // intent starts contiguously in that selected authoritative world,
        // channel by channel, and a big one goes up to the new host again.
        stop_upload_client(c);
        for (uint32_t i = 0; i < c->commands.count; i++) {
            pending_command *cmd = &c->commands.items[i];
            uint32_t previous = g->command_sequence(world, player, cmd->channel);
            for (uint32_t k = 0; k < i; k++) {
                const pending_command *before = &c->commands.items[k];
                if (before->channel == cmd->channel && before->sequence > previous) previous = before->sequence;
            }
            if (previous == UINT32_MAX) {
                remove_command(&c->commands, i--);
                continue;
            }
            cmd->sequence = previous + 1u;
            cmd->sent = 0;
            cmd->uploaded = false;
            cmd->published = 0;
        }
        c->commands_rebase = false;
    }
}

bool tide_client_send_command(tide_client *c, const uint8_t *data, const uint32_t size)
{
    if (!c || c->state != TIDE_SESSION_CONNECTED || !c->loaded || c->commands_need_world
        || !data || !size || size > TIDE_COMMAND_LIMIT
        || !c->game->check_command || !c->game->command_sequence || !c->game->command) return false;
    confirm_commands(c);
    uint32_t channel = 0;
    if (!c->game->check_command(data, size, &channel)) return false;
    if (channel == TIDE_COMMAND_UNORDERED ? !c->game->command_applied : channel >= tide_game_command_channels(c->game)) return false;
    if (channel_count(&c->commands, channel) >= TIDE_COMMAND_PENDING) return false;
    const uint32_t previous = last_sequence(c, channel);
    return previous != UINT32_MAX && append_command(&c->commands, channel, previous + 1u, data, size);
}

// Chunks of the big command on its way up: the first pending one the server
// doesn't have whole, paced (COMMAND_CHUNKS_PER_UPDATE), each again once it
// should have been acknowledged (on_server). The next starts once this one's
// download has a number, which the server says until the next upload.
static void send_upload(tide_client *c)
{
    if (!c->upload_number) {
        for (uint32_t i = 0; i < c->commands.count; i++) {
            const pending_command *cmd = &c->commands.items[i];
            if (!big_command(cmd->size) || cmd->uploaded) continue;
            c->upload_sent = calloc(chunks_of(cmd->size), sizeof *c->upload_sent);
            if (!c->upload_sent) return;
            c->upload_number = ++c->upload_count;
            c->upload_channel = cmd->channel;
            c->upload_sequence = cmd->sequence;
            c->upload_chunks = chunks_of(cmd->size);
            c->upload_ack = 0;
            c->upload_mask = 0;
            break;
        }
        if (!c->upload_number) return;
    }
    const pending_command *cmd = NULL;
    for (uint32_t i = 0; i < c->commands.count; i++) {
        const pending_command *each = &c->commands.items[i];
        if (each->channel == c->upload_channel && each->sequence == c->upload_sequence) cmd = each;
    }
    if (!cmd) {
        stop_upload_client(c);
        return;
    }
    if (cmd->uploaded) return; // Whole at the server: waiting to be told its download's number
    const double again = resend_after(c->rtt_ms);
    uint32_t sent = 0;
    for (uint32_t i = c->upload_ack; i < c->upload_chunks && sent < COMMAND_CHUNKS_PER_UPDATE; i++) {
        if (i > c->upload_ack && i - c->upload_ack <= 64u && (c->upload_mask >> (i - c->upload_ack - 1u) & 1u)) continue;
        if (c->upload_sent[i] > 0.0 && c->now - c->upload_sent[i] < again) continue;
        const uint32_t from = i * CHUNK;
        const uint32_t size = cmd->size - from < CHUNK ? cmd->size - from : CHUNK;
        tide_writer w = {c->packet, TIDE_NET_MTU, 0, false, false};
        header(&w, MSG_UPLOAD);
        tide_write_u32(&w, c->nonce);
        tide_write_varint(&w, c->upload_number);
        tide_write_varint(&w, cmd->sequence);
        tide_write_varint(&w, cmd->size);
        tide_write_varint(&w, i);
        tide_write_u16(&w, (uint16_t)size);
        tide_write_bytes(&w, cmd->data + from, size);
        send_packet(&c->desc.transport, c->desc.server, &w, &c->traffic);
        c->upload_sent[i] = c->now > 0 ? c->now : 1e-9;
        sent++;
    }
}

static void send_commands(tide_client *c)
{
    if (c->state != TIDE_SESSION_CONNECTED || !c->loaded || c->commands_need_world) return;
    confirm_commands(c);
    uint32_t sent = 0;
    const double again = resend_after(c->rtt_ms);
    for (uint32_t i = 0; i < c->commands.count && sent < PACKETS_PER_UPDATE; i++) {
        pending_command *cmd = &c->commands.items[i];
        if (big_command(cmd->size) || (cmd->sent > 0 && c->now - cmd->sent < again)) continue;
        tide_writer w = {c->packet, TIDE_NET_MTU, 0, false, false};
        header(&w, MSG_COMMAND);
        tide_write_u32(&w, c->nonce);
        tide_write_varint(&w, cmd->sequence);
        tide_write_varint(&w, cmd->size);
        tide_write_bytes(&w, cmd->data, cmd->size);
        send_packet(&c->desc.transport, c->desc.server, &w, &c->traffic);
        cmd->sent = c->now > 0 ? c->now : 1e-9;
        sent++;
    }
    send_upload(c);
}

tide_client *tide_client_create(const tide_client_desc *desc, const double now)
{
    if (!desc || !desc->game || tide_game_players(desc->game) > TIDE_PLAYERS_MOST
        || !frame_capacity(desc->game) || (desc->game->client_state
        && (!desc->game->load_client_state || !desc->game->client_hash
            || (desc->game->tick_client_events && !desc->game->check_client_events)))) return NULL;
    tide_client *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->desc = *desc;
    c->game = desc->game;
    const tide_game *g = c->game;
    c->players = tide_game_players(g);
    c->state = TIDE_SESSION_CONNECTING;
    c->created = now;
    c->now = now;
    c->last_heard = now;
    c->last_hello = -1.0;
    c->nonce = (uint32_t)(tide_hash(&c, sizeof c) ^ tide_hash(&now, sizeof now)) | 1u;
    c->ring = FIRST_RING;
    c->let_go = c->input_guessed = UINT32_MAX;
    c->worlds = calloc(c->ring, sizeof *c->worlds);
    for (uint32_t i = 0; c->worlds && i < c->ring; i++) c->worlds[i] = calloc(1, g->world_size);
    c->input = calloc(1, (size_t)g->input_size + 1u);
    c->scratch = calloc(1, scratch_size(g));
    c->packet = malloc(TIDE_NET_MTU);
    bool worlds = c->worlds != NULL;
    for (uint32_t i = 0; worlds && i < c->ring; i++) worlds = c->worlds[i] != NULL;
    if (!worlds || !c->input || !c->scratch || !c->packet) {
        tide_client_destroy(c);
        return NULL;
    }
    if (desc->base) { // Kept as it is now: the server's hashes say what it lacks
        c->base = calloc(1, g->world_size);
        if (c->base) g->copy_world(c->base, desc->base);
        // ...and once more for what its local handlers hear of the world that comes: the one above
        // goes when a world built from it comes out wrong. Without it, every entity is heard.
        if (desc->events && g->hear_arrived) c->had = calloc(1, g->world_size);
        if (c->had) g->copy_world(c->had, desc->base);
    }
    return c;
}

void tide_client_destroy(tide_client *c)
{
    if (!c) return;
    if (c->state != TIDE_SESSION_OFFLINE && c->desc.transport.send) {
        uint8_t data[8];
        tide_writer w = {data, sizeof data, 0, false, false};
        header(&w, MSG_BYE);
        send_packet(&c->desc.transport, c->desc.server, &w, &c->traffic);
    }
    if (c->desc.transport.close) c->desc.transport.close(c->desc.transport.self);
    stop_receiving(c);
    discard_view_correction(c);
    tide_hearing_free(&c->hearing);
    drop_base(c);
    drop_had(c);
    free(c->handover_bytes);
    free(c->handover.digests);
    free_commands(&c->commands);
    stop_upload_client(c);
    free_downloads(c);
    for (uint32_t i = 0; c->frames && i < c->w.history; i++) drop_frame(&c->frames[i]);
    free(c->frames);
    free(c->input_tick);
    free(c->input_size);
    for (uint32_t i = 0; c->worlds && i < c->ring; i++) {
        free_world(c->game, c->worlds[i]);
        free(c->worlds[i]);
    }
    free(c->worlds);
    free(c->inputs);
    free(c->input);
    free(c->scratch);
    free(c->packet);
    free(c);
}

static void update_client(tide_client *c, const double now)
{
    if (c->loaded && now > c->now) {
        c->verified_view += (now - c->now) * (double)c->tick_rate;
        if (c->verified_view > (double)c->verified) c->verified_view = (double)c->verified;
    }
    // A clock with time to lose (keep_early) loses a share of what passed
    if (c->clock_slow > 0.0 && now > c->now) {
        const double most = (now - c->now) * CLOCK_SLOW;
        const double lose = c->clock_slow < most ? c->clock_slow : most;
        c->clock_start += lose;
        c->clock_slow -= lose;
    }
    c->now = now;
    c->updates++;
    if (c->state == TIDE_SESSION_OFFLINE) return;
    client_receive(c);
    if (c->state == TIDE_SESSION_OFFLINE) return;
    const bool slow_start = c->desc.server.kind == TIDE_ADDRESS_ROOM || c->desc.server.kind == TIDE_ADDRESS_NETWORK;
    const double patience = !c->welcomed && slow_start ? ROOM_TIMEOUT : TIMEOUT;
    if (now - c->last_heard > patience) {
        go_offline(c, TIDE_DISCONNECT_TIMED_OUT);
        return;
    }
    if (!c->welcomed) {
        if (now - c->last_hello >= HELLO_EVERY) send_hello(c);
        return;
    }
    if (c->loaded) play(c);
    send_client(c);
    send_commands(c);
    const bool listening = c->local_receive != NULL && c->state == TIDE_SESSION_CONNECTED;
    if (c->loaded && (listening != c->local_subscribed || (listening && now - c->local_sent >= 0.5))) {
        uint8_t data[16];
        tide_writer w = {data, sizeof data, 0, false, false};
        header(&w, MSG_LOCAL_INTEREST);
        tide_write_u64(&w, c->cookie);
        tide_write_u8(&w, listening ? 1u : 0u);
        send_packet(&c->desc.transport, c->desc.server, &w, &c->traffic);
        c->local_subscribed = listening;
        c->local_sent = now;
    }
    adjust_delay(c);
}

void tide_client_update(tide_client *c, const double now)
{
    update_client(c, now);
    finish_view_correction(c);
}

tide_client_status tide_client_status_of(const tide_client *c)
{
    tide_client_status status = {
        .state = c->state,
        .reason = c->reason,
        .player = c->welcomed ? tide_player_from_index(c->player) : (tide_player_id){0},
        .ping_ms = c->ping_ms,
        .answer_ms = c->rtt_ms,
        .verified_tick = c->verified,
        .predicted_tick = c->ahead,
        .server_tick = c->heard_tick,
        .delay_ticks = c->delay,
        .rollback_ms = (float)(c->rollback_now * 1000.0),
        .rollback_most_ms = (float)(c->most_now * 1000.0),
        .resim_ms = (float)(c->resim_load * 1000.0),
        .early_ms = c->waited_known ? (float)((double)c->waited * 1000.0 / WAITS) : 0.0f,
        .late_ticks = c->late,
        .again_ticks = c->again,
        .run_ticks = c->ran,
        .ahead_ticks = c->ran_ahead,
        .resyncs = c->resyncs,
        .world_bytes = c->world_bytes,
        .cookie = c->cookie,
    };
    memcpy(status.message, c->message, sizeof status.message);
    return status;
}

const void *tide_client_world(const tide_client *c)
{
    return c->loaded ? world_at(c, c->ahead) : NULL;
}

const void *tide_client_verified_world(const tide_client *c)
{
    return c->loaded ? world_at(c, c->verified) : NULL;
}

tide_view_worlds tide_client_view(const tide_client *c)
{
    if (!c->loaded) return (tide_view_worlds){0};
    // How many ticks into the match this moment is, by the client's clock,
    // less its input delay: the latest tick it ran is at most one of them
    // behind.
    const double at = (double)c->clock_base + (c->now - c->clock_start) * (double)c->tick_rate - (double)c->delay;
    double alpha = at - (double)c->ahead;
    if (alpha < 0.0) alpha = 0.0;
    if (alpha > 1.0) alpha = 1.0;
    const uint32_t before = (uint32_t)c->verified_view;
    const double fraction = c->verified_view - (double)before;
    const uint32_t shown = fraction > 0.0 && before < c->verified ? before + 1u : before;
    return (tide_view_worlds){.current = world_at(c, c->ahead),
        .previous = c->ahead > c->first_tick ? world_at(c, c->ahead - 1u) : NULL,
        .alpha = (float)alpha, .verified = world_at(c, shown),
        .verified_previous = shown > c->first_tick ? world_at(c, shown - 1u) : NULL,
        .verified_alpha = fraction > 0.0 ? (float)fraction : 1.0f, .verified_tick = shown};
}

void tide_client_mark_presented(tide_client *c)
{
    if (!c || !c->loaded) return;
    const tide_view_worlds view = tide_client_view(c);
    c->presented_view = view.verified_tick <= INT32_MAX
        ? (tide_view_time){(int32_t)view.verified_tick, view.verified_alpha, true} : (tide_view_time){0};
    if (!c->desc.correct_views) return;
    c->presented = true;
    c->presented_previous = view.previous != NULL;
    c->presented_tick = c->ahead;
    c->presented_alpha = view.alpha;
}

// ---------------------------------------------------------------------------
// Session

#define SESSION_EVENTS 8u

struct tide_session {
    tide_session_desc desc;
    tide_local_data_fn local_receive;
    void *local_user;
    uint64_t local_generation;
    tide_loopback *loopback;
    tide_server *server;
    tide_client *client;
    tide_session_state last_state;
    bool open; // Other machines can join its server (tide_session_open)
    tide_address joined;  // The server it last joined, and the cookie that makes it the same player there
    uint64_t cookie;
    bool kicked;          // ...which kicked it, as it heard, after it last played there
    tide_session_event events[SESSION_EVENTS];
    uint32_t event_count;
    double last_now; // The host's time at the last update
    double paused;   // Host time that didn't pass for this machine's own match (see tide_session_update)

    // Host migration: the client that lost its server, offline, whose last
    // world views see until the match is back. `migrating` while no other
    // machine took it over yet, or this machine joined the one that did.
    tide_client *stale;
    bool migrating;

    traffic traffic; // Of the servers and clients it had: theirs is added as they go

    // What its matches sent, for the host to hand the game's local handlers
    // (tide_session_events): its clients' own, one after another
    tide_event_log heard;
};

// The session's own time starts over with each match.
static void start_clock(tide_session *s, const double now)
{
    s->local_generation++;
    if (!s->local_generation) s->local_generation++;
    s->last_now = now;
    s->paused = 0.0;
}

static void push_event(tide_session *s, const tide_session_event e)
{
    if (s->event_count < SESSION_EVENTS) s->events[s->event_count++] = e;
}

static void keep_traffic(tide_session *s, const traffic t)
{
    s->traffic.sent += t.sent;
    s->traffic.received += t.received;
}

static void drop_stale(tide_session *s)
{
    if (s->stale) keep_traffic(s, s->stale->traffic);
    tide_client_destroy(s->stale);
    s->stale = NULL;
    s->migrating = false;
}

// Ends whatever it's in, without a word to local code.
static void tear_down(tide_session *s)
{
    if (s->desc.correct_views && (s->client || s->stale || s->server)) {
        s->desc.correct_views(s->desc.user, &(tide_view_correction){.reset = true});
    }
    if (s->client) keep_traffic(s, s->client->traffic);
    if (s->server) keep_traffic(s, s->server->traffic);
    tide_client_destroy(s->client);
    tide_server_destroy(s->server);
    tide_loopback_destroy(s->loopback);
    drop_stale(s);
    s->client = NULL;
    s->server = NULL;
    s->loopback = NULL;
    s->last_state = TIDE_SESSION_OFFLINE;
    s->open = false;
}



tide_session *tide_session_create(const tide_session_desc *desc)
{
    tide_session *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->desc = *desc; // A tick rate of 0 is the game's when a match starts, as builds can change
    return s;
}

void tide_session_destroy(tide_session *s)
{
    if (!s) return;
    tear_down(s);
    tide_event_log_free(&s->heard);
    free(s);
}

void tide_session_fail(tide_session *s, const tide_disconnect_reason reason)
{
    tide_session_fail_message(s, reason, NULL);
}

void tide_session_fail_message(tide_session *s, const tide_disconnect_reason reason, const char *message)
{
    tear_down(s);
    tide_session_event event = {.kind = TIDE_SESSION_DISCONNECTED_EVENT, .reason = reason};
    copy_message(event.message, message, TIDE_MESSAGE_BYTES - 1u);
    push_event(s, event);
}

void tide_session_leave(tide_session *s)
{
    if (!s->client && !s->server && !s->migrating) return;
    tear_down(s);
    push_event(s, (tide_session_event){.kind = TIDE_SESSION_DISCONNECTED_EVENT, .reason = TIDE_DISCONNECT_LEFT});
}

// A server with this machine's player on it, over loopback, and no one else
// until it's opened.
static void start_server(tide_session *s, const void *start, const void *world, const tide_player_set players,
                         const double now)
{
    tide_session_leave(s);
    start_clock(s, now);
    s->loopback = tide_loopback_create(1);
    if (!s->loopback) {
        tide_session_fail(s, TIDE_DISCONNECT_FAILED);
        return;
    }
    tide_loopback_set_time(s->loopback, now);
    const tide_server_desc server = {
        .game = s->desc.game,
        .tick_rate = s->desc.tick_rate,
        .start = start,
        .transports = {tide_loopback_endpoint(s->loopback, 1)},
        .local_first = true,
        .wait_for_first = true,
        .world = world,
        .players = players,
        .jobs = s->desc.jobs,
        .entropy = s->desc.entropy,
        .seed = s->desc.seed,
    };
    s->server = tide_server_create(&server, now);
    const tide_client_desc client = {
        .game = s->desc.game,
        .transport = tide_loopback_endpoint(s->loopback, 2),
        .server = tide_loopback_address(1),
        .sample = s->desc.sample,
        .user = s->desc.user,
        .lead = 0, // Its inputs go straight in: the server ticks right after it
        .jobs = s->desc.jobs,
        .base = world, // What its server goes on from, if anything: nothing to send
        .now_ns = s->desc.now_ns,
        .resim_budget = s->desc.resim_budget,
        .guess_seconds = s->desc.guess_seconds,
        .correct_views = s->desc.correct_views,
        .events = &s->heard,
    };
    s->client = s->server ? tide_client_create(&client, now) : NULL;
    if (!s->client) {
        tear_down(s);
        tide_session_fail(s, TIDE_DISCONNECT_FAILED);
        return;
    }
    s->last_state = TIDE_SESSION_CONNECTING;
}

void tide_session_start(tide_session *s, const void *start, const double now)
{
    start_server(s, start, NULL, (tide_player_set){0}, now);
}

void tide_session_start_from(tide_session *s, const void *world, const tide_player_set players, const double now)
{
    start_server(s, NULL, world, players, now);
}

bool tide_session_open(tide_session *s, const tide_transport network)
{
    tide_transport *mine = s->server ? &s->server->desc.transports[1] : NULL;
    const bool spare = network.send && (!mine || mine->send); // No match to take it, or it has one already
    if (spare && network.close) network.close(network.self);
    if (!mine || (!mine->send && !network.send)) return false;
    if (!mine->send) *mine = network;
    s->server->closed = false;
    s->open = true;
    return true;
}

void tide_session_close(tide_session *s)
{
    if (s->server) s->server->closed = true;
    s->open = false;
}

void tide_session_kick(tide_session *s, const tide_player_id player, const char *message)
{
    if (s->server) tide_server_kick(s->server, player, message);
}

void tide_session_kick_all(tide_session *s, const char *message)
{
    for (int32_t i = 0; s->server && i < (int32_t)s->server->players; i++) {
        tide_server_kick(s->server, tide_player_from_index(i), message);
    }
}

void tide_session_join(tide_session *s, const tide_transport network, const tide_address server, const double now)
{
    // Joining the match's next server, it's the same player, whatever the address
    const bool migrating = s->migrating;
    if (migrating) s->migrating = false; // Views see the last world until this one arrives
    else tide_session_leave(s);
    start_clock(s, now);
    if (!migrating && !tide_address_equal(server, s->joined)) {
        s->cookie = 0;
        s->kicked = false;
    }
    s->joined = server;
    const tide_client_desc client = {
        .game = s->desc.game,
        .transport = network,
        .server = server,
        .sample = s->desc.sample,
        .user = s->desc.user,
        .lead = 1,
        .lead_frame = true,
        .cookie = s->cookie,
        .knew_kick = s->kicked && !migrating,
        .jobs = s->desc.jobs,
        .base = migrating && s->stale ? tide_client_verified_world(s->stale) : NULL, // Most of it, most likely
        .now_ns = s->desc.now_ns,
        .resim_budget = s->desc.resim_budget,
        .guess_seconds = s->desc.guess_seconds,
        .correct_views = s->desc.correct_views,
        .events = &s->heard,
    };
    s->client = tide_client_create(&client, now);
    if (!s->client) {
        if (network.close) network.close(network.self);
        tide_session_fail(s, TIDE_DISCONNECT_FAILED);
        return;
    }
    if (migrating && s->stale) {
        s->client->commands = s->stale->commands;
        s->stale->commands = (command_queue){0};
        s->client->commands_rebase = true;
    }
    s->last_state = TIDE_SESSION_CONNECTING;
}

// Whether `c`, offline now, lost a server whose match another machine can take
// over: the server said where to meet, and it went, or stopped answering.
static bool can_migrate(const tide_client *c)
{
    return !c->game->client_state && c->loaded && c->handover.version && c->handover.key[0]
        && (c->reason == TIDE_DISCONNECT_TIMED_OUT || (c->reason == TIDE_DISCONNECT_SERVER_LEFT && c->handed_over));
}

// The match waits to change hands: this machine's client keeps its last world
// for views, and its room goes, to be found again (see tide_session_migrating).
static void begin_migration(tide_session *s)
{
    tide_client *c = s->client;
    if (c->desc.transport.close) c->desc.transport.close(c->desc.transport.self);
    c->desc.transport = (tide_transport){0};
    drop_stale(s);
    s->stale = c;
    s->client = NULL;
    s->migrating = true;
}

void tide_session_update(tide_session *s, const double now)
{
    if (!s->client) return;
    s->client->local_receive = s->local_receive;
    s->client->local_user = s->local_user;
    // This machine's server stops whenever the machine does (a breakpoint, a
    // browser that froze the page), and its player with it. Beyond the ticks
    // the server can run in one update, which it would drop anyway, that time
    // didn't pass for the match: neither side went quiet, and there's nothing
    // to catch up. A client of another machine keeps to the real time.
    const double most = s->server ? (double)MAX_TICKS / (double)s->server->desc.tick_rate : 0.0;
    if (s->server && now - s->last_now > most) s->paused += now - s->last_now - most;
    s->last_now = now;
    const double t = now - s->paused;

    if (s->loopback) tide_loopback_set_time(s->loopback, t);
    update_client(s->client, t);
    if (s->server) tide_server_update(s->server, t);
    update_client(s->client, t); // What the server just sent: this machine's ticks, at once

    const tide_client_status status = tide_client_status_of(s->client);
    if (!s->server && status.cookie) s->cookie = status.cookie;
    if (status.state == TIDE_SESSION_CONNECTED) s->kicked = false; // Played there since
    if (status.state == TIDE_SESSION_OFFLINE && status.reason == TIDE_DISCONNECT_KICKED && !s->server) s->kicked = true;
    if (status.state == TIDE_SESSION_CONNECTED && s->last_state != TIDE_SESSION_CONNECTED) {
        // Back in a match that changed hands, it was never out of it
        if (!s->stale) {
            push_event(s, (tide_session_event){.kind = TIDE_SESSION_CONNECTED_EVENT, .reason = TIDE_DISCONNECT_LEFT});
        } else if (s->desc.correct_views) {
            // Keep visual errors while the stale world is drawn. Adoption
            // restarts the network tick timeline: equal numeric ticks cannot
            // prove matching match timestamps, so the new history resets.
            s->desc.correct_views(s->desc.user, &(tide_view_correction){.reset = true});
            discard_view_correction(s->client); // Coalesce its initial snapshot reset with adoption.
        }
        drop_stale(s);
    }
    if (status.state == TIDE_SESSION_OFFLINE && !s->server && !s->stale && can_migrate(s->client)) {
        finish_view_correction(s->client);
        begin_migration(s);
        return;
    }
    if (status.state == TIDE_SESSION_OFFLINE) {
        tide_session_event gone = {.kind = TIDE_SESSION_DISCONNECTED_EVENT, .reason = status.reason};
        memcpy(gone.message, status.message, sizeof gone.message);
        tear_down(s);
        push_event(s, gone);
        return;
    }
    s->last_state = status.state;
    finish_view_correction(s->client);
}

// Seconds after `now` until a clock that started at `start`, at `rate`, counts
// its next tick (ticks_in), aimed at the tick itself rather than ticks_in's
// hair before it, so an update then always finds it due.
static double until_next(const double start, const uint32_t rate, const double now)
{
    return start + (double)(ticks_in(now - start, rate) + 1u) / (double)rate - now;
}

bool tide_session_send_command(tide_session *s, const uint8_t *data, const uint32_t size)
{
    return s && tide_client_send_command(s->client, data, size);
}

void tide_session_listen_data(tide_session *s, tide_local_data_fn receive, void *user)
{
    if (!s) return;
    s->local_receive = receive;
    s->local_user = user;
    if (s->client) {
        s->client->local_receive = receive;
        s->client->local_user = user;
    }
}

uint64_t tide_session_data_generation(const tide_session *s)
{
    return s ? s->local_generation : 0u;
}

bool tide_session_send_data(tide_session *s, const uint8_t *data, const uint32_t size)
{
    tide_client *c = s ? s->client : NULL;
    if (!c || !c->local_receive || !c->loaded || c->state != TIDE_SESSION_CONNECTED
        || !data || !size || size > TIDE_LOCAL_DATA_BYTES) return false;
    uint8_t packet[TIDE_NET_MTU];
    tide_writer w = {packet, sizeof packet, 0, false, false};
    header(&w, MSG_LOCAL_DATA);
    tide_write_u64(&w, c->cookie);
    tide_write_bytes(&w, data, size);
    send_packet(&c->desc.transport, c->desc.server, &w, &c->traffic);
    return !w.overflow;
}

double tide_session_until_tick(const tide_session *s)
{
    const double now = s->last_now - s->paused; // The match's time at the last update
    const tide_server *server = s->server;
    const tide_client *c = s->client;
    // This machine's player ticks right after its server: the server's clock
    if (server && server->started && !server->ended) return until_next(server->clock_start, server->desc.tick_rate, now);
    if (!server && c && c->loaded) return until_next(c->clock_start, c->tick_rate, now);
    uint32_t rate = server ? server->desc.tick_rate : c && c->tick_rate ? c->tick_rate : s->desc.tick_rate;
    if (!rate) rate = s->desc.game->tick_rate ? s->desc.game->tick_rate : 60u;
    return 1.0 / (double)rate;
}

const void *tide_session_world(const tide_session *s)
{
    const void *world = s->client ? tide_client_world(s->client) : NULL;
    return world || !s->stale ? world : tide_client_world(s->stale); // The last one, while the match changes hands
}

tide_view_worlds tide_session_view(const tide_session *s)
{
    if (s->client && s->client->loaded) return tide_client_view(s->client);
    if (s->stale) return (tide_view_worlds){.current = tide_client_world(s->stale), .alpha = 1.0f,
        .verified = tide_client_verified_world(s->stale), .verified_alpha = 1.0f, .verified_tick = s->stale->verified};
    return (tide_view_worlds){0};
}

tide_event_log *tide_session_events(tide_session *s)
{
    s->heard.taken = true; // From now on its clients' ticks record what they send
    return &s->heard;
}

void tide_session_mark_presented(tide_session *s)
{
    if (s->client && s->client->loaded) tide_client_mark_presented(s->client);
    else if (s->stale && s->desc.correct_views) {
        s->stale->presented = true;
        s->stale->presented_tick = s->stale->ahead;
        s->stale->presented_previous = false;
        s->stale->presented_alpha = 1.0f;
    }
}

const void *tide_session_server_world(const tide_session *s)
{
    return s->server ? tide_server_world(s->server) : NULL;
}

tide_session_status tide_session_status_of(const tide_session *s)
{
    tide_session_status status = {0};
    if (s->client) status.client = tide_client_status_of(s->client);
    if (s->stale && status.client.state != TIDE_SESSION_CONNECTED) { // Changing hands: the same player, connecting
        status.client.state = TIDE_SESSION_CONNECTING;
        status.client.player = tide_player_from_index(s->stale->player);
    }
    status.server = s->server != NULL;
    status.open = s->open;
    status.skipped = s->paused;
    traffic t = s->traffic;
    const traffic *live[] = {s->server ? &s->server->traffic : NULL, s->client ? &s->client->traffic : NULL,
                             s->stale ? &s->stale->traffic : NULL};
    for (int i = 0; i < 3; i++) {
        if (!live[i]) continue;
        t.sent += live[i]->sent;
        t.received += live[i]->received;
    }
    status.sent_bytes = t.sent;
    status.received_bytes = t.received;
    return status;
}

bool tide_session_next_event(tide_session *s, tide_session_event *event)
{
    if (s->event_count == 0) return false;
    *event = s->events[0];
    memmove(s->events, s->events + 1, (s->event_count - 1u) * sizeof s->events[0]);
    s->event_count--;
    return true;
}

void tide_session_end(tide_session *s)
{
    if (!s->server) return;
    // Its goodbyes say the match ended, and so does its transports' `end`
    s->server->ended = true;
    tear_down(s);
    push_event(s, (tide_session_event){.kind = TIDE_SESSION_DISCONNECTED_EVENT, .reason = TIDE_DISCONNECT_ENDED});
}

void tide_session_set_room(tide_session *s, const char *code, const char *key)
{
    if (!s->server) return;
    snprintf(s->server->room_code, sizeof s->server->room_code, "%s", code ? code : "");
    snprintf(s->server->room_key, sizeof s->server->room_key, "%s", key ? key : "");
}

bool tide_session_migrating(const tide_session *s, char code[TIDE_ROOM_CODE_LENGTH + 1], char key[TIDE_ROOM_KEY_LENGTH + 1])
{
    if (!s->migrating) return false;
    memcpy(code, s->stale->handover.code, TIDE_ROOM_CODE_LENGTH + 1);
    memcpy(key, s->stale->handover.key, TIDE_ROOM_KEY_LENGTH + 1);
    return true;
}

void tide_session_take_over(tide_session *s, const tide_transport network, const double now)
{
    if (!s->migrating || s->desc.game->client_state || (s->stale && s->stale->game->client_state)) {
        if (network.close) network.close(network.self);
        return;
    }
    // The players in the match come back to it, but the last server's own
    const tide_client *old = s->stale;
    const handover *h = &old->handover;
    tide_player_set players = h->in_match;
    tide_player_set leaving = {0};
    if (h->own && tide_players_has(&players, h->own - 1u)) {
        tide_players_remove(&players, h->own - 1u);
        tide_players_add(&leaving, h->own - 1u);
    }
    s->migrating = false; // Views see the last world until the server's arrives
    start_clock(s, now);
    s->loopback = tide_loopback_create(1);
    if (!s->loopback) {
        if (network.close) network.close(network.self);
        tide_session_fail(s, TIDE_DISCONNECT_FAILED);
        return;
    }
    tide_loopback_set_time(s->loopback, now);
    const tide_server_desc server = {
        .game = s->desc.game,
        .tick_rate = old->tick_rate, // The match's, whatever this machine's build says
        .transports = {tide_loopback_endpoint(s->loopback, 1), network},
        .local_first = true,
        .wait_for_first = true,
        .world = world_at(old, old->verified),
        .players = players,
        .leaving = leaving,
        .digests = h->digests,
        .closed = !h->open,
        .jobs = s->desc.jobs,
    };
    s->server = tide_server_create(&server, now); // Closes `network` if it can't be made
    if (s->server) tide_session_set_room(s, h->code, h->key);
    const tide_client_desc client = {
        .game = s->desc.game,
        .transport = tide_loopback_endpoint(s->loopback, 2),
        .server = tide_loopback_address(1),
        .sample = s->desc.sample,
        .user = s->desc.user,
        .lead = 0,
        .cookie = s->cookie, // This machine's player, as it was
        .jobs = s->desc.jobs,
        .base = server.world, // What its server goes on from: nothing to send
        .guess_seconds = s->desc.guess_seconds,
        .correct_views = s->desc.correct_views,
        .events = &s->heard,
    };
    s->client = s->server ? tide_client_create(&client, now) : NULL;
    if (!s->client) {
        tear_down(s);
        tide_session_fail(s, TIDE_DISCONNECT_FAILED);
        return;
    }
    s->client->commands = s->stale->commands;
    s->stale->commands = (command_queue){0};
    s->client->commands_rebase = true;
    s->open = h->open;
    s->last_state = TIDE_SESSION_CONNECTING;
}

void tide_session_set_game(tide_session *s, const tide_game *game)
{
    s->desc.game = game;
    if (s->stale) {
        s->stale->desc.game = game;
        s->stale->game = game;
    }
    if (s->server) {
        s->server->desc.game = game;
        s->server->game = game;
    }
    if (s->client) {
        s->client->desc.game = game;
        s->client->game = game;
    }
    // What the last build recorded is laid out its own way: the ticks it ran
    // and this one confirms as they were tell the handlers nothing
    if (s->client) tide_hearing_rebuilt(&s->client->hearing);
    tide_event_log_clear(&s->heard);
}

void tide_session_set_view_corrector(tide_session *s, const tide_view_correct_fn correct_views)
{
    s->desc.correct_views = correct_views;
    tide_client *clients[] = {s->client, s->stale};
    for (uint32_t i = 0; i < 2u; i++) {
        tide_client *c = clients[i];
        if (!c) continue;
        discard_view_correction(c);
        c->presented = false;
        c->desc.correct_views = correct_views;
    }
}

// What a new layout needs, made before anything changes, so running out of
// memory or a world that can't be carried over leaves the session as it was.
typedef struct server_migration {
    void *world;
    uint8_t *frame;
    uint8_t *input;
    uint8_t *packed;
    uint8_t *server_last;
    uint8_t *scratch;
    uint8_t **inputs; // Each player's: as many as the game takes
    uint32_t players;
} server_migration;

typedef struct client_migration {
    void **worlds;
    uint8_t *input;
    uint8_t *scratch;
    uint8_t *inputs;
} client_migration;

static void discard_server_migration(const tide_game *g, server_migration *m)
{
    free_world(g, m->world);
    free(m->world);
    free(m->frame);
    free(m->input);
    free(m->packed);
    free(m->server_last);
    free(m->scratch);
    for (uint32_t i = 0; m->inputs && i < m->players; i++) free(m->inputs[i]);
    free(m->inputs);
}

static void discard_client_migration(const tide_client *c, const tide_game *g, client_migration *m)
{
    for (uint32_t i = 0; m->worlds && i < c->ring; i++) {
        free_world(g, m->worlds[i]);
        free(m->worlds[i]);
    }
    free(m->worlds);
    free(m->input);
    free(m->scratch);
    free(m->inputs);
}

static bool prepare_server(const tide_server *s, const tide_game *g, const tide_migrate_fn migrate, void *user,
                           server_migration *m)
{
    // A build that takes another number of players keeps its inputs, and the
    // server's after them, elsewhere in its world: the match starts over
    if (tide_game_players(g) != s->players || !frame_capacity(g)) return false;
    const uint32_t bytes = g->max_input_bytes ? g->max_input_bytes : 1u;
    m->players = s->players;
    m->inputs = calloc(s->players, sizeof *m->inputs);
    m->world = calloc(1, g->world_size);
    m->frame = malloc(2u * (size_t)frame_capacity(g));
    m->input = calloc(1, (size_t)g->input_size + g->max_input_bytes + 1u);
    m->packed = malloc((size_t)g->max_input_bytes + 1u);
    m->server_last = malloc((size_t)g->max_input_bytes + 1u);
    m->scratch = calloc(1, scratch_size(g));
    bool ok = m->inputs && m->world && m->frame && m->input && m->packed && m->server_last && m->scratch;
    for (uint32_t i = 0; ok && i < s->players; i++) {
        if (!s->connections[i].used) continue;
        m->inputs[i] = malloc((size_t)(s->w.inputs + 1u) * bytes);
        ok = m->inputs[i] != NULL;
    }
    return ok && migrate(user, s->world, m->world);
}

// Inputs sent for ticks to come were packed for the old layout: the players'
// last ones stand for them until new ones arrive.
static void commit_server(tide_server *s, const tide_game *g, server_migration *m)
{
    for (uint32_t i = 0; i < s->w.history; i++) {
        free_world(s->game, s->frames[i].world);
        free(s->frames[i].world);
        s->frames[i].world = NULL;
    }
    bool *sending = calloc(s->players, sizeof *sending); // The old build's transfers start again
    if (!sending) tide_out_of_memory();
    for (uint32_t i = 0; i < s->players; i++) {
        sending[i] = s->connections[i].used && s->connections[i].sending;
        if (sending[i]) stop_sending(s, &s->connections[i]);
        clear_commands(&s->connections[i].commands); // Wire bytes describe the old layout
        stop_upload(&s->connections[i]);
        stop_download(&s->connections[i]);
        s->connections[i].upload_done = 0;
        s->connections[i].download_done = 0;
        connection *c = &s->connections[i];
        for (uint32_t k = 0; c->client_frames && k < s->w.history; k++) {
            free(c->client_frames[k].data);
            tide_event_log_free(&c->client_frames[k].events);
        }
        free(c->client_frames);
        c->client_frames = c->used && g->client_state ? calloc(s->w.history, sizeof *c->client_frames) : NULL;
        if (c->used && g->client_state && !c->client_frames) tide_out_of_memory();
        free_world(s->game, c->verified_world);
        free(c->verified_world);
        c->verified_world = NULL;
        c->repair_requested = c->repair_burst = 0;
        c->repair_stream_until = 0.0;
    }
    free_published(s);
    free_world(s->game, s->world);
    free(s->world);
    free(s->frame);
    free(s->input);
    free(s->packed);
    free(s->server_last);
    free(s->scratch);
    free(s->start); // The old build's, which starting the new one wouldn't make
    s->start = NULL;
    s->start_kind = START_UNKNOWN;
    s->world = m->world;
    s->frame = m->frame;
    s->input = m->input;
    s->packed = m->packed;
    s->server_last = m->server_last;
    s->scratch = m->scratch;
    s->server_last_size = 0;
    s->game = g;
    s->desc.game = g;
    for (uint32_t i = 0; i < s->players; i++) {
        connection *c = &s->connections[i];
        if (!c->used) continue;
        free(c->inputs);
        c->inputs = m->inputs[i];
        m->inputs[i] = NULL;
        for (uint32_t k = 0; k < s->w.inputs; k++) c->input_tick[k] = UINT32_MAX;
        c->last_size = 0;
        c->newest_input = s->tick;
        c->sent_input = false;
        c->waited_known = false;
        if (sending[i]) start_snapshot(s, c);
    }
    free(sending);
}

static bool prepare_client(const tide_client *c, const tide_game *g, const tide_migrate_fn migrate, void *user,
                           client_migration *m)
{
    if (tide_game_players(g) != c->players || !frame_capacity(g)) return false; // Incompatible input layout
    const uint32_t bytes = g->max_input_bytes ? g->max_input_bytes : 1u;
    m->worlds = calloc(c->ring, sizeof *m->worlds);
    bool ok = m->worlds != NULL;
    for (uint32_t i = 0; ok && i < c->ring; i++) {
        m->worlds[i] = calloc(1, g->world_size);
        ok = m->worlds[i] != NULL;
    }
    m->input = calloc(1, (size_t)g->input_size + 1u);
    m->scratch = calloc(1, scratch_size(g));
    ok = ok && m->input && m->scratch;
    if (ok && c->frames) {
        m->inputs = malloc((size_t)c->w.inputs * bytes);
        ok = m->inputs != NULL;
    }
    return ok && (!c->loaded || !migrate || migrate(user, world_at(c, c->verified), m->worlds[c->verified % c->ring]));
}

// It goes on from the verified world: the ticks it predicted, and its inputs
// for them, were the old build's.
static void commit_client(tide_client *c, const tide_game *g, client_migration *m)
{
    reset_view_correction(c);
    finish_view_correction(c);
    const bool changed = c->game->hash != g->hash;
    clear_commands(&c->commands); // Another layout has no translation of pending wire values
    stop_upload_client(c);
    free_downloads(c);
    for (uint32_t i = 0; i < c->ring; i++) {
        free_world(c->game, c->worlds[i]);
        free(c->worlds[i]);
    }
    free(c->worlds);
    c->worlds = m->worlds;
    free(c->input);
    c->input = m->input;
    free(c->scratch);
    c->scratch = m->scratch;
    if (c->frames) {
        free(c->inputs);
        c->inputs = m->inputs;
        for (uint32_t i = 0; i < c->w.inputs; i++) c->input_tick[i] = UINT32_MAX;
        c->sampled_from = c->sampled; // None of them is left
    }
    stop_receiving(c); // A world it was receiving is the old build's: the server sends it again
    drop_base(c);      // ...and so is the one it would have built it from
    drop_had(c);       // ...and the one it had: every entity of its first world is heard
    tide_hearing_rebuilt(&c->hearing); // ...and what its ticks sent, as its handlers heard it
    c->game = g;
    c->desc.game = g;
    c->repair_tick = 0;
    c->repair_stream = false;
    if (g->client_state && c->loaded) c->need_snapshot = true;
    if (changed && c->loaded && g->command && g->command_sequence) {
        // The server can already have processed an old command that this
        // client's verified world lacks. Read its watermark before assigning
        // another sequence, rather than accidentally reusing that request ID.
        c->commands_need_world = true;
        c->need_snapshot = true;
    }
    if (c->loaded) {
        c->ahead = c->verified;
        c->let_go = c->input_guessed = UINT32_MAX;
        c->first_tick = c->verified; // Views don't blend from the old build's world
        c->verified_view = (double)c->verified;
        c->presented_view = (tide_view_time){0};
    }
}

// This machine's own player takes its server's world as it is now, which is
// what it would come to.
static void take_server_world(tide_client *c, const tide_server *s)
{
    if (c->game->client_state) {
        uint32_t size;
        uint8_t *bytes = c->game->client_state(s->world, tide_player_from_index(c->player), &size);
        const bool ok = bytes && c->game->load_client_state(world_at(c, s->tick), bytes, size);
        free(bytes);
        if (!ok) { c->need_snapshot = true; return; }
    } else {
        c->game->copy_world(world_at(c, s->tick), s->world);
    }
    c->verified = s->tick;
    c->ahead = s->tick;
    c->let_go = c->input_guessed = UINT32_MAX;
    c->first_tick = s->tick;
    c->verified_view = (double)s->tick;
    c->presented_view = (tide_view_time){0};
    c->commands_need_world = false;
    c->need_snapshot = false;
    c->repair_tick = 0;
    c->repair_stream = false;
}

bool tide_session_migrate(tide_session *s, const tide_game *game, const tide_migrate_fn migrate, void *user)
{
    const bool own = s->server && s->client; // This machine's player, on its own server
    server_migration sm = {0};
    client_migration cm = {0};
    if (s->server && !prepare_server(s->server, game, migrate, user, &sm)) {
        discard_server_migration(game, &sm);
        return false;
    }
    if (s->client && !prepare_client(s->client, game, own ? NULL : migrate, user, &cm)) {
        discard_server_migration(game, &sm);
        discard_client_migration(s->client, game, &cm);
        return false;
    }
    if (s->server) commit_server(s->server, game, &sm);
    if (s->client) commit_client(s->client, game, &cm);
    if (own && s->client->loaded) take_server_world(s->client, s->server);
    if (s->stale && s->desc.game->hash != game->hash) clear_commands(&s->stale->commands);
    s->desc.game = game;
    return true;
}
