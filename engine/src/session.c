#include "tide/session.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/page.h"
#include "tide/sha256.h"

// See tide/session.h.
//
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
//   COMMAND client: its connection attempt, command sequence and copied bytes
//   SERVER   server: times, its tick, the newest input it has of the player's
//            (from its tick: how early they arrive), and pieces of the ticks
//            the player lacks, each by how many ticks back it is
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
// sender, sequence and bytes. They are never guessed by repeating an input.
//
// The world goes as a delta (tide/delta.h), in chunks. A player with no world
// gets all of it. One with a world to build it from (its own, gone wrong, or
// the last one it had of a match that changed hands) first gets the hashes of
// the world's pages, says which of them its world lacks, and gets those.

#define MAGIC 0x5449u // "TI"
#define PROTOCOL 9u

enum { MSG_HELLO = 1, MSG_WELCOME, MSG_REFUSE, MSG_CHUNK, MSG_CLIENT, MSG_SERVER, MSG_BYE, MSG_HANDOVER, MSG_COMMAND };
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
};
// What a frame has after its hash, most often nothing: the players who joined
// or left, a bit per slot whose input changed, and one per slot whose was late.
enum { FRAME_EVENTS = 1, FRAME_INPUTS = 2, FRAME_LATE = 4, FRAME_COMMANDS = 8 };
// What's being sent: the world, as a delta, or the hashes of its pages, to
// learn which of them the player's world lacks. The hashes come after how the
// match started: one of START_*, Time.dt's bits, the match's seed in a game
// that has one (tide_game.seeded) and, for START_GIVEN, the game's tide_start.
enum { SEND_WORLD = 0, SEND_HASHES = 1 };
enum { START_UNKNOWN = 0, START_MAIN = 1, START_GIVEN = 2 };
#define LACKS_BYTES 512u // The most a CLIENT says about what its world lacks: past that, it lacks the rest

// The server's input is the slot after the players': how many the game takes
// (tide_game_players), which servers and clients keep as `players`.
#define PREDICTION_SECONDS 1.0 // How far a client runs ahead of the last tick it knows, at most
#define RESIM_BUDGET 0.004     // Seconds a frame a client may spend running predicted ticks again, unless its desc says
#define SLACK_SECONDS 5.0      // How long a client has to have had room to spare before it takes input delay back
#define SLACK_TICKS 3          // Ticks a frame has to come before its tick is due to count as room to spare
#define HISTORY_SECONDS 4.0    // How long the server keeps ticks to send again, and a client keeps ticks ahead
#define HISTORY_MOST 8u        // ...times this at most, for a player catching up with a world it was sent; past that, the world again
#define FIRST_RING 8u          // Snapshots a client starts with; more as it runs further ahead
#define PIECE 1000u                  // Bytes of a tick per piece: a tick takes as many as its inputs make, up to 256
#define CHUNK 1000u                  // Bytes of a packed world per chunk
#define CHUNKS_PER_UPDATE 64u
#define PACKETS_PER_UPDATE 8u
#define EVENTS_PER_PLAYER 4u    // Joins and leaves kept before a tick, for each player the game takes
#define PLAYER_SET_BYTES 46u    // The most a set of players takes, as a varint: 7 of its 320 bits a byte
#define HANDOVER_PART 1000u     // Bytes of a handover record per datagram
#define SENT_TIMES 256u         // Times a client sent that it remembers, to measure its ping when one comes back
#define TIMEOUT 5.0     // Seconds of silence before giving up on the other side
#define HELLO_TIMEOUT 2.0 // ...or on one that said hello and never wrote again: it may not be where it said
#define ROOM_TIMEOUT 15.0 // ...or on a room's host before it first answers: WebRTC can take a while to connect
#define HELLO_EVERY 0.2 // Seconds between HELLOs until the server answers
#define MAX_TICKS 8u    // Ticks a server runs in one update at most: after a stall it drops the time instead
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

// Whole ticks from `seconds` at `rate`.
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

// The most a frame can take: its hash, the events, which inputs changed and
// were late, and every slot's input, after its size.
static uint32_t frame_capacity(const tide_game *g)
{
    const uint32_t players = tide_game_players(g);
    return 8u + 1u + 5u + 2u * EVENTS_PER_PLAYER * players + 2u * PLAYER_SET_BYTES + (players + 1u) * (5u + g->max_input_bytes)
           + (g->command ? 5u + TIDE_COMMANDS_PER_TICK * (15u + TIDE_COMMAND_BYTES) : 0u);
}

typedef struct pending_command {
    uint32_t sequence;
    uint32_t size;
    uint8_t *data;
    double sent;
} pending_command;

typedef struct command_queue {
    pending_command items[TIDE_COMMAND_PENDING];
    uint32_t count;
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

static bool append_command(command_queue *q, const uint32_t sequence, const uint8_t *data, const uint32_t size)
{
    if (q->count >= TIDE_COMMAND_PENDING || !size || size > TIDE_COMMAND_BYTES) return false;
    uint8_t *copy = malloc(size);
    if (!copy) return false;
    memcpy(copy, data, size);
    q->items[q->count++] = (pending_command){sequence, size, copy, 0};
    return true;
}

// A set of players (and the server's slot after them) as a varint of as many
// bits as it has: 7 a byte, lowest first, each byte saying whether more come.
// So a set of the first 7 takes a byte, as a 32-bit varint does.
static void write_players(tide_writer *w, const tide_player_set *set)
{
    uint32_t top = 0;
    for (uint32_t i = 0; i < TIDE_PLAYER_SET_BITS; i++) {
        if (tide_players_has(set, i)) top = i;
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
    tide_player_set set = {{0}};
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
    return 16u + TIDE_ROOM_CODE_LENGTH + TIDE_ROOM_KEY_LENGTH + PLAYER_SET_BYTES + 8u * players;
}

// Whether `a` has a player `b` hasn't.
static bool players_beyond(const tide_player_set *a, const tide_player_set *b)
{
    uint64_t more = 0;
    for (uint32_t w = 0; w < TIDE_PLAYER_SET_BITS / 64u; w++) more |= a->bits[w] & ~b->bits[w];
    return more != 0;
}

// Whether the sets share a player.
static bool players_meet(const tide_player_set *a, const tide_player_set *b)
{
    uint64_t both = 0;
    for (uint32_t w = 0; w < TIDE_PLAYER_SET_BITS / 64u; w++) both |= a->bits[w] & b->bits[w];
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
} stored_frame;

typedef struct sent_mark {
    uint32_t tick;
    double at;
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

    // The world it's being sent
    bool sending;
    bool has_world; // It has a world to build it from: it's sent the hashes first
    bool can_start; // ...or can start the match itself to have one
    uint32_t snapshot_tick;
    uint32_t snapshot_number; // Each thing sent has the next number, from 1
    uint8_t snapshot_kind;    // SEND_WORLD or SEND_HASHES
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
    sent_mark *frame_sent; // history
    // It was sent the world and needs every tick from it on, however long
    // that took: the server keeps them until it catches up (keep_needed)
    bool catching_up;

    // Its inputs, by tick
    uint32_t newest_input; // The last tick it sent an input for, plus one
    uint32_t *input_tick; // inputs
    uint16_t *input_size;
    uint8_t *inputs; // `inputs` of max_input_bytes, then the last one set
    uint32_t last_size;
    command_queue commands;
} connection;

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
    double arrived; // When the datagram being read arrived, by its transport's clock (arrived_of)
    traffic traffic;
    uint32_t players;        // How many the game takes (tide_game_players): what the arrays by player have
    connection *connections; // Player n is connections[n]
    uint8_t (*events)[2];    // Joins and leaves before the next tick: EVENTS_PER_PLAYER for each player
    uint32_t event_count;
    uint32_t command_player; // Next sender considered, so a busy player cannot starve the others
    windows w;
    stored_frame *frames; // history
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
    s->events[s->event_count][1] = (uint8_t)player;
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
    c->chunk_count = (size + CHUNK - 1u) / CHUNK;
    c->chunk_sent = calloc(c->chunk_count ? c->chunk_count : 1u, sizeof *c->chunk_sent);
    if (!c->chunk_sent) tide_out_of_memory();
    c->chunk_ack = 0;
    c->chunk_mask = 0;
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
    stop_sending(s, c);
    c->snapshot_tick = s->tick;
    c->sending = true;
    c->frame_ack = s->tick;
    c->frame_mask = 0;
    c->catching_up = true;
    memset(c->frame_sent, 0, s->w.history * sizeof *c->frame_sent);
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
    // The whole world: packed once a tick, however many join in it. An
    // earlier tick's pack that players are still being sent stays theirs.
    if (!s->pack || s->pack->tick != s->tick) {
        shared_pack *made = calloc(1, sizeof *made);
        if (!made) tide_out_of_memory();
        made->tick = s->tick;
        made->bytes = g->pack_delta(s->world, NULL, NULL, 0, &made->size);
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
    uint8_t *delta = g->pack_delta(c->hashed, NULL, lacks, size, &delta_size);
    free_world(g, c->hashed);
    free(c->hashed);
    c->hashed = NULL;
    send_in_chunks(s, c, SEND_WORLD, delta, delta_size, NULL);
}

static void free_connection(tide_server *s, connection *c)
{
    stop_sending(s, c);
    free(c->inputs);
    free(c->input_tick);
    free(c->input_size);
    free(c->frame_sent);
    clear_commands(&c->commands);
    memset(c, 0, sizeof *c);
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
    return s->game->host_migration && s->room_key[0] && s->room_code[0];
}

static void send_bye(tide_server *s, const uint32_t transport, const tide_address to)
{
    uint8_t data[8];
    tide_writer w = {data, sizeof data, 0, false, false};
    header(&w, MSG_BYE);
    tide_write_u8(&w, s->ended ? BYE_ENDED : hands_over(s) ? BYE_HANDOVER : BYE_LEFT);
    send_packet(&s->desc.transports[transport], to, &w, &s->traffic);
}

// Tells a kicked player they were, and why.
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
    const uint32_t nonce = tide_read_u32(r);
    const uint16_t their_time = tide_read_u16(r);
    const uint64_t cookie = tide_read_u64(r);
    const uint8_t flags = tide_read_u8(r);
    if (r->failed) return;
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
    if (game != s->game->hash) {
        if (c) drop_connection(s, c);
        refuse(s, transport, from, REFUSE_OTHER_GAME);
        return;
    }
    // Its cookie's digest: the player's, from this server or the one it went on from
    int32_t player = -1;
    const uint64_t digest = cookie_digest(cookie);
    for (uint32_t i = 0; cookie && i < s->players; i++) {
        if (s->digests[i] == digest) player = (int32_t)i;
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
        c->has_world = (flags & HELLO_HAS_WORLD) != 0;
        c->can_start = (flags & HELLO_CAN_START) != 0;
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
    c->local = s->desc.local_first && transport == 0;
    c->inputs = malloc((size_t)(s->w.inputs + 1u) * bytes);
    c->input_tick = malloc(s->w.inputs * sizeof *c->input_tick);
    c->input_size = calloc(s->w.inputs, sizeof *c->input_size);
    c->frame_sent = calloc(s->w.history, sizeof *c->frame_sent);
    if (!c->inputs || !c->input_tick || !c->input_size || !c->frame_sent) {
        free_connection(s, c);
        if (new_player) s->cookies[player] = s->digests[player] = 0;
        refuse(s, transport, from, REFUSE_FULL);
        return;
    }
    for (uint32_t i = 0; i < s->w.inputs; i++) c->input_tick[i] = UINT32_MAX;
    c->newest_input = s->tick;
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
    // What it has of what's being sent (see send_client)
    const uint32_t chunk_number = tide_read_varint(r);
    const uint32_t have = chunk_number ? tide_read_varint(r) : 0u;
    const uint32_t chunk_ack = !chunk_number ? 0u : have == 0 ? UINT32_MAX : have - 1u;
    const uint64_t chunk_mask = chunk_number && have ? tide_read_u64(r) : 0u;
    const uint32_t frame_ack = tide_read_varint(r);
    const uint64_t frame_mask = tide_read_varint64(r);
    uint32_t lacks_number = 0;
    uint32_t lacks_size = 0;
    const uint8_t *lacks = NULL;
    if (flags & CLIENT_LACKS) {
        lacks_number = tide_read_varint(r);
        lacks_size = tide_read_varint(r);
        lacks = tide_read_bytes(r, lacks_size);
    }
    const uint32_t first_input = frame_ack + tide_read_varint(r);
    const uint8_t input_count = tide_read_u8(r);
    if (r->failed) return;
    // Whether it wants the world goes by the newest it sent: an older one can come late
    const bool newest = (int16_t)(uint16_t)(their_time - c->their_time) >= 0;
    const bool wants = (flags & CLIENT_WANTS_WORLD) != 0;
    c->last_heard = s->now;
    if (newest) {
        c->their_time = their_time;
        c->their_arrived = s->arrived;
    }
    if (echo) c->rtt_ms = millis_since(s->now, echo);
    c->welcoming = false;
    if (!c->greeted) greet(s, c);

    if (c->sending) {
        if (chunk_number == c->snapshot_number) { // About what's being sent, not something older
            if (chunk_ack > c->chunk_ack) c->chunk_ack = chunk_ack < c->chunk_count ? chunk_ack : c->chunk_count;
            if (chunk_ack == c->chunk_ack) c->chunk_mask = chunk_mask;
        }
        if (c->snapshot_kind == SEND_HASHES && lacks && lacks_number == c->snapshot_number) {
            send_lacking(s, c, lacks, lacks_size);
        } else if ((c->snapshot_kind == SEND_WORLD && c->chunk_ack >= c->chunk_count) || (newest && !wants)) {
            stop_sending(s, c); // It has the world, or one that needs nothing: ticks from here
        }
    } else if (newest && wants) {
        c->has_world = (flags & CLIENT_HAS_WORLD) != 0;
        c->can_start = (flags & CLIENT_CAN_START) != 0;
        start_snapshot(s, c); // Its world went wrong, or never came: sent again
    } else {
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
        if (t < s->tick || t - s->tick >= s->w.inputs || n > max || n == 0) continue; // Late, or much too early
        const uint32_t slot = t % s->w.inputs;
        c->input_tick[slot] = t;
        c->input_size[slot] = (uint16_t)n;
        memcpy(c->inputs + (size_t)slot * max, bytes, n);
        if (t + 1u > c->newest_input) c->newest_input = t + 1u;
    }
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
    const tide_player_id sender = tide_player_from_index((int32_t)(c - s->connections));
    const uint32_t previous = g->command_sequence(s->world, sender);
    if (sequence <= previous || sequence - previous > TIDE_COMMAND_PENDING) return;
    for (uint32_t i = 0; i < c->commands.count; i++) {
        if (c->commands.items[i].sequence == sequence) return;
    }
    if (c->commands.count < TIDE_COMMAND_PENDING && g->check_command(data, size)) {
        (void)append_command(&c->commands, sequence, data, size);
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
    bool ok = frames != NULL && marks != NULL;
    for (uint32_t p = 0; ok && p < s->players; p++) {
        if (!s->connections[p].used) continue;
        marks[p] = calloc(grown, sizeof *marks[p]);
        ok = marks[p] != NULL;
    }
    if (!ok) {
        free(frames);
        for (uint32_t p = 0; marks && p < s->players; p++) free(marks[p]);
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
        }
        free(c->frame_sent);
        c->frame_sent = marks[p];
    }
    free(marks);
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
        const uint32_t previous = g->command_sequence(s->world, sender);
        for (uint32_t i = 0; i < c->commands.count; i++) {
            const pending_command *cmd = &c->commands.items[i];
            if (cmd->sequence <= previous) {
                remove_command(&c->commands, i--);
                continue;
            }
            if (previous == UINT32_MAX || cmd->sequence != previous + 1u) continue;
            if (!g->command(s->world, sender, cmd->sequence, cmd->data, cmd->size)) {
                remove_command(&c->commands, i);
                break;
            }
            tide_write_varint(w, player);
            tide_write_varint(w, cmd->sequence);
            tide_write_varint(w, cmd->size);
            tide_write_bytes(w, cmd->data, cmd->size);
            remove_command(&c->commands, i);
            count++;
            examined = 0;
            break;
        }
    }
    return count;
}

// Runs one tick: joins and leaves, then the inputs that arrived for it, in
// slot order, the queued commands, then the systems. Clients do the same.
static void server_tick(tide_server *s)
{
    const tide_game *g = s->game;
    const uint32_t tick = s->tick;
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
        tide_write_u8(&w, s->events[i][1]);
    }
    const uint8_t events = s->event_count ? FRAME_EVENTS : 0u;
    s->event_count = 0;

    // Each input that changed. One that's the same as the slot's last is left
    // out: keeping the last input is the same as setting it again. They come
    // after the bits that say which, so they're made apart first.
    tide_writer inputs = {s->frame + frame_capacity(g), frame_capacity(g), 0, false, false};
    tide_player_set mask = {{0}};
    tide_player_set late = {{0}};
    const uint32_t server_slot = s->players; // The server's input, after the players'
    const uint32_t max = g->max_input_bytes;
    uint8_t *input = s->input;
    uint32_t server_size = 0; // The server's input: its own player's, on the server's machine
    for (uint32_t p = 0; g->set_input && p < s->players; p++) {
        connection *c = &s->connections[p];
        if (!c->used) continue;
        const uint32_t slot = tick % s->w.inputs;
        const uint32_t size = c->input_tick[slot] != tick
                                ? 0u
                                : canonical_input(g, c->inputs + (size_t)slot * max, c->input_size[slot], input, s->packed);
        if (size == 0) {
            tide_players_add(&late, p);
            if (c->local) tide_players_add(&late, server_slot);
            continue;
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
    s->frame[parts_at] = (uint8_t)(events | (any_input ? FRAME_INPUTS : 0u) | (any_late ? FRAME_LATE : 0u)
                                   | (command_count ? FRAME_COMMANDS : 0u));

    g->tick(s->world, s->desc.jobs);
    s->tick++;
    s->ended = g->ended && g->ended(s->world);
    patch_u64(s->frame, g->hash_world(s->world));

    keep_needed(s, tick);
    stored_frame *f = &s->frames[tick % s->w.history];
    uint8_t *data = realloc(f->data, w.size);
    if (!data) return;
    memcpy(data, s->frame, w.size);
    *f = (stored_frame){tick, w.size, data};
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

static void send_welcome(tide_server *s, const connection *c)
{
    uint8_t data[64];
    tide_writer w = {data, sizeof data, 0, false, false};
    header(&w, MSG_WELCOME);
    tide_write_u32(&w, c->nonce);
    tide_write_u8(&w, (uint8_t)(c - s->connections));
    tide_write_u8(&w, c->local ? 1u : 0u); // Its input is the server's too
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
    tide_write_u32(w, s->tick);
    write_signed(w, (int32_t)(c->newest_input - s->tick)); // The newest input it has, and so how early they arrive
    *count_at = w->size;
    tide_write_u8(w, 0);
}

// A piece's own bytes in a SERVER: how many ticks before the packet's its
// tick is, its frame's size, and which piece of it, when it has several.
static uint32_t piece_header(const tide_server *s, const uint32_t tick, const uint32_t size)
{
    return varint_size(s->tick - tick) + varint_size(size) + (size > PIECE ? 1u : 0u);
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
    // Caught up with the world it was sent: from here, a player that falls
    // behind gets the world again, rather than the server keeping every tick
    if (c->catching_up && s->tick - c->frame_ack <= windows_for(s->desc.tick_rate).history / 2u) c->catching_up = false;
    // While what it lacks fits in one datagram, each one carries all of it:
    // a lost one costs a frame, not a round trip. Beyond that, what was sent
    // goes again once it should have been acknowledged.
    uint32_t lacking = 0;
    for (uint32_t tick = c->frame_ack; tick < s->tick && lacking <= TIDE_NET_MTU; tick++) {
        const stored_frame *f = &s->frames[tick % s->w.history];
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
        const stored_frame *f = &s->frames[tick % s->w.history];
        if (f->tick != tick || !f->data) continue;
        const uint32_t count = (f->size + PIECE - 1u) / PIECE;
        for (uint32_t k = 0; k < count && packets < PACKETS_PER_UPDATE; k++) {
            const uint32_t from = k * PIECE;
            const uint32_t size = f->size - from < PIECE ? f->size - from : PIECE;
            if (w.size + piece_header(s, tick, f->size) + size > TIDE_NET_MTU || pieces == 255u) {
                s->packet[count_at] = (uint8_t)pieces;
                send_packet(t, c->address, &w, &s->traffic);
                packets++;
                start_server_packet(s, c, &w, &count_at);
                pieces = 0;
            }
            tide_write_varint(&w, s->tick - tick);
            tide_write_varint(&w, f->size);
            if (count > 1) tide_write_u8(&w, (uint8_t)k);
            tide_write_bytes(&w, f->data + from, size);
            pieces++;
        }
        *mark = (sent_mark){tick, s->now > 0.0 ? s->now : 1e-9};
    }
    s->packet[count_at] = (uint8_t)pieces;
    if (packets < PACKETS_PER_UPDATE) send_packet(t, c->address, &w, &s->traffic); // Always one, for the times and margin
}

tide_server *tide_server_create(const tide_server_desc *desc, const double now)
{
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
    s->digests = calloc(s->players, sizeof *s->digests);
    s->away_since = calloc(s->players, sizeof *s->away_since);
    s->kicks = calloc(s->players, sizeof *s->kicks);
    s->handover = malloc(handover_capacity(s->players));
    s->frames = calloc(s->w.history, sizeof *s->frames);
    s->world = calloc(1, g->world_size);
    s->frame = malloc(2u * (size_t)frame_capacity(g));
    s->packet = malloc(TIDE_NET_MTU);
    s->input = calloc(1, (size_t)g->input_size + g->max_input_bytes + 1u);
    s->packed = malloc((size_t)g->max_input_bytes + 1u);
    s->server_last = malloc((size_t)g->max_input_bytes + 1u);
    s->scratch = calloc(1, scratch_size(g));
    if (!s->connections || !s->events || !s->cookies || !s->digests || !s->away_since || !s->kicks || !s->handover
        || !s->frames || !s->world || !s->frame || !s->packet || !s->input || !s->packed || !s->server_last || !s->scratch) {
        tide_server_destroy(s);
        return NULL;
    }
    const float dt = desc->dt > 0.0f ? desc->dt : 1.0f / (float)s->desc.tick_rate;
    // A match's seed, in a game that has one: from the system where the host
    // has it to give, so no two matches go the same, or else the one the host
    // picked; 31 bits, as Time.seed is
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
    for (uint32_t i = 0; s->frames && i < s->w.history; i++) free(s->frames[i].data);
    free(s->frames);
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
    free(s->digests);
    free(s->away_since);
    free(s->kicks);
    free(s->handover);
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
        tide_write_u8(&p, (uint8_t)part);
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
        s->present = (tide_player_set){{0}};
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
        }
    }
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

bool tide_server_kick(tide_server *s, const tide_player_id player, const char *message)
{
    const int32_t index = tide_player_index(player);
    if (index < 0) return false;
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
    uint64_t digests[TIDE_PLAYERS_MOST]; // By player; 0 for those not in the match
} handover;

// A tick's frame as it comes, in pieces. The pieces it has are a bit each,
// after its bytes: a frame can take as many pieces as the game's inputs make.
typedef struct pending_frame {
    uint32_t tick;
    uint32_t size;
    uint32_t pieces;
    uint32_t had;   // Pieces it has so far
    uint8_t *data;  // `size` bytes, then a byte per piece: 1 once it came
} pending_frame;

static void drop_frame(pending_frame *f)
{
    free(f->data);
    *f = (pending_frame){0};
}

struct tide_client {
    tide_client_desc desc;
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
    int32_t player;
    bool server_input_mine; // On the server's machine: its input is the server's too
    uint32_t tick_rate;
    uint32_t server_tick; // The server's, as it last said while sending a world
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

    // Snapshots: the world at each tick from `verified`, the last the server
    // confirmed, to `ahead`, the last it predicted, at worlds[tick % ring], and
    // the one before them, which views blend from
    bool loaded;
    uint32_t first_tick; // The tick of the world that arrived last: nothing before it
    void **worlds;
    uint32_t ring;
    uint32_t verified;
    uint32_t ahead;
    windows w;              // From the match's tick rate, once welcomed
    pending_frame *frames;  // history

    // Its own inputs, packed, by tick. It samples them for `sampled` on, as
    // its clock says, however far it runs: `delay` ticks short of them
    // (tide_prediction), so an input is sampled that many ticks before the
    // tick it's for is run.
    uint32_t *input_tick;   // inputs
    uint16_t *input_size;
    uint8_t *inputs;
    uint32_t sampled;       // The next tick to sample an input for: at least `ahead`
    uint32_t delay;         // Ticks short of `sampled` it runs
    uint32_t delay_cap;     // The most delay it takes, so its inputs fit their ring
    bool predicts;          // It runs ticks the server hasn't confirmed (TIDE_PREDICTION_ROLLBACK)
    double tick_cost;       // Seconds one of its ticks takes, as last measured (tide_client_desc.now_ns)
    uint32_t again;         // Predicted ticks it ran again so far
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
    int32_t margin; // How early its inputs reach the server, in ticks, as the server last said
    bool margin_known;
    double hold_until; // No clock changes until then: the last one takes a round trip to show

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

// Whether it can start the match itself, to have a world to build the
// server's from.
static bool can_start(const tide_client *c)
{
    return c->game->pure_start && !c->loaded && !c->base && !c->base_failed;
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
    const uint8_t player = tide_read_u8(r);
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
    if (r->failed || nonce != c->nonce || player >= c->players || tick_rate == 0 || kind > SEND_HASHES) return;
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
    if (server_tick > c->server_tick) c->server_tick = server_tick;
    c->their_time = their_time;
    measure(c, echo, held);
    // A world it doesn't have yet: the first, a newer one after it went wrong,
    // or one past all it ran, when it fell behind further than the server
    // keeps ticks. Something it had whole, it had: what's after it is newer.
    const bool wanted = !c->loaded || (c->need_snapshot && tick >= c->verified) || tick > c->ahead;
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
    if (from + size > c->snapshot_size) return;
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
    const uint32_t server_tick = tide_read_u32(r);
    const int32_t early = read_signed(r); // Its newest input, from the server's tick
    const uint8_t pieces = tide_read_u8(r);
    if (r->failed) return;
    c->their_time = their_time;
    measure(c, echo, held);
    c->margin = early < -30000 ? -30000 : early > 30000 ? 30000 : early;
    c->margin_known = true;
    const uint32_t input_ack = server_tick + (uint32_t)early;
    if (input_ack > c->input_ack) c->input_ack = input_ack;
    if (!c->loaded) return;
    for (uint32_t i = 0; i < pieces; i++) {
        // Ticks back from the server's, its frame's size, and which piece of it, when there are more
        const uint32_t back = tide_read_varint(r);
        const uint32_t total = tide_read_varint(r);
        const uint32_t count = (total + PIECE - 1u) / PIECE;
        // No frame is bigger than what the game's inputs can make (frame_capacity)
        if (r->failed || total == 0 || total > frame_capacity(c->game)) return; // Where the next piece starts is lost
        const uint32_t index = count > 1 ? tide_read_u8(r) : 0u;
        if (index >= count) return;
        const uint32_t from = index * PIECE;
        const uint32_t size = total - from < PIECE ? total - from : PIECE;
        const uint8_t *bytes = tide_read_bytes(r, size);
        if (!bytes) return;
        const uint32_t tick = server_tick - back;
        if (back == 0 || back > server_tick || tick < c->verified || tick - c->verified >= c->w.history) continue;
        pending_frame *f = &c->frames[tick % c->w.history];
        if (f->tick != tick || !f->data) {
            free(f->data);
            *f = (pending_frame){tick, total, count, 0, calloc((size_t)total + count, 1)};
            if (!f->data) continue;
        }
        if (f->size != total || f->data[total + index]) continue;
        memcpy(f->data + from, bytes, size);
        f->data[total + index] = 1;
        f->had++;
    }
}

// A part of a handover record (see send_handover). Parts of a newer record
// than the one being put together start it over; once every part came, the
// record is read, and kept if it's sound.
static void on_handover(tide_client *c, tide_reader *r)
{
    const uint32_t version = tide_read_u32(r);
    const uint32_t size = tide_read_varint(r);
    const uint8_t part = tide_read_u8(r);
    if (r->failed || version <= c->handover.version || version < c->handover_version) return;
    if (size == 0 || size > handover_capacity(TIDE_PLAYERS_MOST)) return;
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
    if (!sound) return;
    code = (const uint8_t *)letters;
    key_bytes = (const uint8_t *)letters + TIDE_ROOM_CODE_LENGTH;
    // A room's code and key are letters and digits, as the platform layer makes them
    for (uint32_t i = 0; i < TIDE_ROOM_CODE_LENGTH; i++) {
        if (!strchr(TIDE_ROOM_CODE_LETTERS, code[i]) || !code[i]) return;
        h.code[i] = (char)code[i];
    }
    for (uint32_t i = 0; i < key; i++) {
        const char k = (char)key_bytes[i];
        if (!((k >= '0' && k <= '9') || (k >= 'a' && k <= 'z') || (k >= 'A' && k <= 'Z'))) return;
        h.key[i] = k;
    }
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
        case MSG_SERVER: on_server(c, &r); break;
        case MSG_HANDOVER: on_handover(c, &r); break;
        case MSG_REFUSE: go_offline(c, TIDE_DISCONNECT_REFUSED); break;
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

static bool frame_complete(const pending_frame *f, const uint32_t tick)
{
    return f->tick == tick && f->data && f->had == f->pieces;
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
// the server's, when that's this machine's too).
static bool as_predicted(const tide_client *c, const pending_frame *f)
{
    tide_reader r = {f->data, f->size, 0, false};
    tide_read_u64(&r);
    const uint8_t parts = tide_read_u8(&r);
    if (parts & (FRAME_EVENTS | FRAME_COMMANDS)) return false;
    const tide_player_set none = {{0}};
    const tide_player_set mask = parts & FRAME_INPUTS ? read_players(&r) : none;
    const tide_player_set late = parts & FRAME_LATE ? read_players(&r) : none;
    if (!c->game->set_input) return !tide_players_any(&mask);
    tide_player_set mine = none;
    tide_players_add(&mine, (uint32_t)c->player);
    if (c->server_input_mine) tide_players_add(&mine, c->players);
    if (players_beyond(&mask, &mine) || players_meet(&late, &mine)) return false;
    uint32_t own_size = 0;
    const uint8_t *own = own_input(c, f->tick, &own_size);
    if (!own) return !tide_players_any(&mask);
    const tide_game *g = c->game;
    const bool deltas = g->read_input_delta && g->world_input;
    uint8_t *base = c->scratch;
    uint8_t *theirs = c->scratch + g->input_size;
    if (deltas && !g->read_input(own, own_size, c->input)) return false;
    for (uint32_t slot = 0; slot <= c->players; slot++) {
        if (!tide_players_has(&mask, slot)) continue;
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

// Room for one more snapshot. The ones in use keep their ticks; the others,
// and new ones, fill the rest.
static bool room_ahead(tide_client *c)
{
    // From the one before `verified` to the one after `ahead`
    if (c->ahead + 2u - c->verified < c->ring) return true;
    const uint32_t most = c->w.prediction + 2u;
    if (c->ring >= most) return false;
    const uint32_t ring = c->ring * 2u > most ? most : c->ring * 2u;
    void **worlds = calloc(ring, sizeof *worlds);
    bool *moved = calloc(c->ring, sizeof *moved);
    if (!worlds || !moved) {
        free(worlds);
        free(moved);
        return false;
    }
    for (uint32_t t = c->verified > c->first_tick ? c->verified - 1u : c->verified; t <= c->ahead; t++) {
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
        const uint8_t player = tide_read_u8(&r);
        if (r.failed || player >= c->players) return false;
        if (kind == EVENT_JOIN) g->player_joined(world, tide_player_from_index(player));
        else g->player_left(world, tide_player_from_index(player));
    }
    const tide_player_set none = {{0}};
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
        if (r.failed || player >= c->players || !size || size > TIDE_COMMAND_BYTES) return false;
        const uint8_t *data = tide_read_bytes(&r, size);
        const tide_player_id sender = tide_player_from_index((int32_t)player);
        const uint32_t previous = g->command_sequence(world, sender);
        if (!data || previous == UINT32_MAX || sequence != previous + 1u
            || !g->command(world, sender, sequence, data, size)) return false;
    }
    if (r.failed || r.at != r.size) return false;
    run_tick(c, world, f->tick);
    return g->hash_world(world) == hash;
}

// Runs predicted tick `tick`: the snapshot after it is the one before it, run
// with this machine's input. With a clock, it measures what the tick cost,
// for the input delay to go by (adjust_delay).
static void run_predicted(tide_client *c, const uint32_t tick)
{
    const tide_game *g = c->game;
    void *world = world_at(c, tick + 1u);
    g->copy_world(world, world_at(c, tick));
    uint32_t size = 0;
    const uint8_t *own = g->set_input ? own_input(c, tick, &size) : NULL;
    if (own && g->read_input(own, size, c->input)) {
        const tide_player_id me = tide_player_from_index(c->player);
        g->set_input(world, me, c->input);
        if (c->server_input_mine) g->set_server_input(world, c->input);
    }
    const uint64_t started = c->desc.now_ns ? c->desc.now_ns() : 0u;
    run_tick(c, world, tick);
    finish_view_rewrite(c, tick + 1u);
    if (c->desc.now_ns) {
        const double cost = (double)(c->desc.now_ns() - started) * 1e-9;
        c->tick_cost = c->tick_cost > 0.0 ? c->tick_cost * 0.9 + cost * 0.1 : cost;
    }
}

// From the verified world, the predicted ticks again.
static void predict_again(tide_client *c)
{
    for (uint32_t t = c->verified; t < c->ahead; t++) {
        capture_view_correction(c, t + 1u);
        run_predicted(c, t);
    }
    c->again += c->ahead - c->verified;
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

static void load_snapshot(tide_client *c)
{
    const tide_game *g = c->game;
    const uint32_t tick = c->snapshot_tick;
    // Built from the world it answered these pages' hashes with, if it did
    const void *base = c->answered && c->answered_tick == tick ? c->base : NULL;
    void *world = calloc(1, g->world_size);
    const bool ok = world && g->unpack_delta(world, base, c->snapshot, c->snapshot_size);
    c->got_number = c->snapshot_number;
    stop_receiving(c);
    c->answered = false;
    if (!ok) { // Asked for again, whole if it was built from a world of its own
        free(world);
        if (base) c->base_failed = true;
        c->need_snapshot = true;
        return;
    }
    drop_base(c);
    c->base_failed = false;
    if (!c->loaded) reset_view_correction(c);
    else if (c->presented && tick > c->presented_tick - (c->presented_previous ? 1u : 0u)) {
        reset_view_correction(c); // The replacement has no history at the displayed timestamp.
    } else capture_view_correction(c, tick);
    // Ahead of the world that came, it keeps its predicted ticks, which it runs again
    if (!c->loaded || c->ahead < tick) c->verified = c->ahead = tick;
    if (c->sampled < c->ahead) c->sampled = c->ahead;
    void **slot = &c->worlds[tick % c->ring];
    free_world(g, *slot);
    free(*slot);
    *slot = world;
    finish_view_rewrite(c, tick);
    const bool first = !c->loaded;
    // The ticks before it were never confirmed one by one: what they sent stays unsaid
    tide_hearing_skip_to(&c->hearing, tick);
    c->loaded = true;
    c->first_tick = tick;
    c->need_snapshot = false;
    c->commands_need_world = false;
    c->verified = tick;
    for (uint32_t i = 0; i < c->w.history; i++) {
        if (c->frames[i].tick < tick) drop_frame(&c->frames[i]);
    }
    if (first) {
        c->state = TIDE_SESSION_CONNECTED;
        c->ahead = tick;
        c->sampled = tick;
        // Ahead of the server by its lead, and the round trip it takes to get
        // there: from where the server was as it sent the world, which may
        // have taken a while (and a round trip more for its pages' hashes)
        const uint32_t trip = (uint32_t)ticks_in((double)c->rtt_ms / 1000.0, c->tick_rate);
        const uint32_t server = c->server_tick > tick ? c->server_tick : tick;
        c->clock_start = c->now;
        c->clock_base = (int64_t)server + c->desc.lead + (c->desc.lead ? trip : 0u);
        c->hold_until = c->now + (double)c->rtt_ms / 1000.0 + 3.0 / c->tick_rate;
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
        c->slack_since = c->now;
        c->delay_hold_until = c->hold_until;
    } else {
        c->resyncs++;
        if (c->ahead > tick) predict_again(c);
    }
}

// Samples this machine's input for the next tick to have one, which goes out
// to the server, and in when that tick runs.
static void sample_input(tide_client *c)
{
    const tide_game *g = c->game;
    const uint32_t tick = c->sampled;
    if (g->set_input && c->desc.sample) {
        c->desc.sample(c->desc.user, tick, c->input);
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

static void play(tide_client *c)
{
    // Ticks the server confirmed. One that went as predicted is already in its
    // snapshot, which only needs its hash checked. One that didn't runs on the
    // snapshot before it, and the predicted ones after it are stale. One past
    // what it ran, whose tick its clock hasn't reached, waits for the clock, so
    // ticks run at the match's pace however early their frames come.
    bool stale = false;
    const int64_t due = due_tick(c);
    while (!c->need_snapshot && !c->receiving) {
        const uint32_t tick = c->verified;
        pending_frame *f = &c->frames[tick % c->w.history];
        if (!frame_complete(f, tick)) break;
        if (tick >= c->ahead && (int64_t)tick >= due) break;
        bool ok;
        if (tick < c->ahead && !stale && as_predicted(c, f)) {
            ok = c->game->hash_world(world_at(c, tick + 1u)) == frame_hash(f);
        } else {
            if (tick < c->ahead) capture_view_correction(c, tick + 1u);
            c->game->copy_world(world_at(c, tick + 1u), world_at(c, tick));
            ok = apply_frame(c, world_at(c, tick + 1u), f);
            finish_view_rewrite(c, tick + 1u);
            stale |= tick + 1u < c->ahead;
        }
        drop_frame(f);
        if (!ok) { // Diverged: ask for the world again, and keep predicting meanwhile
            c->need_snapshot = true;
            break;
        }
        tide_hearing_confirm(&c->hearing, tick); // What it sent is certain now
        c->verified++;
        if (c->ahead < c->verified) c->ahead = c->verified;
        if (c->sampled < c->ahead) c->sampled = c->ahead; // Behind the server: no input was sampled for those ticks
    }
    if (stale && c->ahead > c->verified) predict_again(c);

    // Keep its inputs the right number of ticks early: early enough to arrive
    // in time, and no earlier, since further ahead is more to predict. A client
    // that sends none (a game without an input) has no margin to keep: its
    // clock stays as the world's arrival set it.
    const int32_t low = (int32_t)c->desc.lead;
    const int32_t high = low + 2;
    const bool sends_inputs = c->game->set_input && c->desc.sample;
    if (sends_inputs && c->margin_known && c->now >= c->hold_until && (c->margin < low || c->margin > high + 2)) {
        c->clock_base += c->margin < low ? low - c->margin : high - c->margin;
        c->hold_until = c->now + (double)c->rtt_ms / 1000.0 + 3.0 / c->tick_rate;
    }
    // Inputs up to where the clock is, as far as their ring goes; then, if it
    // predicts, the ticks up to its delay short of them
    const int64_t due_in = due_input(c);
    for (uint32_t n = 0; (int64_t)c->sampled < due_in && c->sampled - c->verified < c->w.prediction && n < 16u; n++) {
        sample_input(c);
    }
    const uint32_t short_of = c->sampled > c->delay ? c->sampled - c->delay : 0u;
    const uint32_t target = short_of > c->ahead ? short_of : c->ahead;
    for (uint32_t n = 0; c->predicts && c->ahead < target && n < 16u && room_ahead(c); n++) predict(c);
}

// Its input delay (tide_prediction): how many ticks short of the ones it has
// inputs for it runs. Predicting, it takes a tick more when running the
// predicted ticks again would cost more than its budget a frame, and a tick
// less once there's been room to spare for a while. Not predicting, it takes
// a tick more when the clock reached a tick whose frame hasn't come, and a
// tick less once frames have come well before their ticks for a while. Each
// change takes a round trip to show, so there's one at a time.
static void adjust_delay(tide_client *c)
{
    if (!c->loaded || c->now < c->delay_hold_until) return;
    int change = 0;
    if (c->predicts) {
        if (!c->desc.now_ns) return; // No clock: the same run on every machine
        const double budget = c->desc.resim_budget > 0.0 ? c->desc.resim_budget : RESIM_BUDGET;
        const double cost = (double)(c->ahead - c->verified) * c->tick_cost;
        if (cost > budget) change = 1;
        else if (cost < budget / 3.0 && c->now - c->slack_since > SLACK_SECONDS) change = -1;
        if (cost >= budget / 3.0) c->slack_since = c->now;
    } else {
        const int64_t due = due_tick(c);
        uint32_t has = 0; // Ticks from `verified` whose frames have come
        while (has < 32u && frame_complete(&c->frames[(c->verified + has) % c->w.history], c->verified + has)) has++;
        const bool late = (int64_t)c->verified < due && has == 0;
        const bool early = (int64_t)(c->verified + has) > due + SLACK_TICKS;
        if (late) change = 1;
        else if (early && c->now - c->slack_since > SLACK_SECONDS) change = -1;
        if (!early) c->slack_since = c->now;
    }
    if (change > 0 && c->delay < c->delay_cap) c->delay++;
    else if (change < 0 && c->delay > 0) c->delay--;
    else return;
    c->slack_since = c->now;
    c->delay_hold_until = c->now + (double)c->rtt_ms / 1000.0 + 3.0 / c->tick_rate;
}

static void send_hello(tide_client *c)
{
    uint8_t data[32];
    tide_writer w = {data, sizeof data, 0, false, false};
    header(&w, MSG_HELLO);
    tide_write_u64(&w, c->game->hash);
    tide_write_u32(&w, c->nonce);
    tide_write_u16(&w, said_time(c));
    tide_write_u64(&w, c->desc.cookie);
    tide_write_u8(&w, (c->desc.knew_kick ? HELLO_KNEW_KICK : 0u) | (c->base ? HELLO_HAS_WORLD : 0u)
                          | (can_start(c) ? HELLO_CAN_START : 0u));
    send_packet(&c->desc.transport, c->desc.server, &w, &c->traffic);
    c->last_hello = c->now;
}

static void send_client(tide_client *c)
{
    tide_writer w = {c->packet, TIDE_NET_MTU, 0, false, false};
    header(&w, MSG_CLIENT);
    tide_write_u16(&w, said_time(c));
    tide_write_u16(&w, c->their_time);
    // Whether it needs the world, or is taking the one coming, and has one to build it from
    const bool wants = c->need_snapshot || !c->loaded || c->receiving;
    const bool has = !c->base_failed && (c->loaded || c->base);
    const bool lacks = c->answered && !c->receiving;
    tide_write_u8(&w, (wants ? CLIENT_WANTS_WORLD : 0u) | (wants && has ? CLIENT_HAS_WORLD : 0u) | (lacks ? CLIENT_LACKS : 0u)
                          | (wants && can_start(c) ? CLIENT_CAN_START : 0u));
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
        if (frame_complete(&c->frames[tick % c->w.history], tick)) frame_mask |= 1ull << i;
    }
    tide_write_varint(&w, c->verified);
    tide_write_varint64(&w, frame_mask);
    if (lacks) { // Until the world comes
        tide_write_varint(&w, c->answered_number);
        tide_write_varint(&w, c->lacks_size);
        tide_write_bytes(&w, c->lacks, c->lacks_size);
    }

    // Inputs from the first the server lacks, as many as fit: after the
    // first, each as what differs from the one before, when the game packs
    // them that way
    const tide_game *g = c->game;
    const bool deltas = g->read_input && g->write_input_delta;
    uint8_t *previous = c->scratch;
    uint8_t *current = c->scratch + g->input_size;
    uint8_t *delta = current + g->input_size;
    uint32_t first = c->input_ack > c->verified ? c->input_ack : c->verified;
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
static void confirm_commands(tide_client *c)
{
    if (!c->loaded || !c->game->command_sequence) return;
    const uint32_t previous = c->game->command_sequence(world_at(c, c->verified), tide_player_from_index(c->player));
    for (uint32_t i = 0; i < c->commands.count; i++) {
        if (c->commands.items[i].sequence <= previous) remove_command(&c->commands, i--);
    }
    if (c->commands_rebase) {
        // The next host can have verified less than this sender. Retained
        // intent starts contiguously in that selected authoritative world.
        while (c->commands.count > UINT32_MAX - previous) remove_command(&c->commands, c->commands.count - 1u);
        for (uint32_t i = 0; i < c->commands.count; i++) {
            c->commands.items[i].sequence = previous + i + 1u;
            c->commands.items[i].sent = 0;
        }
        c->commands_rebase = false;
    }
}

bool tide_client_send_command(tide_client *c, const uint8_t *data, const uint32_t size)
{
    if (!c || c->state != TIDE_SESSION_CONNECTED || !c->loaded || c->commands_need_world
        || !data || !size || size > TIDE_COMMAND_BYTES
        || !c->game->check_command || !c->game->command_sequence || !c->game->command) return false;
    confirm_commands(c);
    if (c->commands.count >= TIDE_COMMAND_PENDING || !c->game->check_command(data, size)) return false;
    const uint32_t previous = c->commands.count ? c->commands.items[c->commands.count - 1u].sequence
                                               : c->game->command_sequence(world_at(c, c->verified),
                                                                            tide_player_from_index(c->player));
    return previous != UINT32_MAX && append_command(&c->commands, previous + 1u, data, size);
}

static void send_commands(tide_client *c)
{
    if (c->state != TIDE_SESSION_CONNECTED || !c->loaded || c->commands_need_world) return;
    confirm_commands(c);
    uint32_t sent = 0;
    const double again = resend_after(c->rtt_ms);
    for (uint32_t i = 0; i < c->commands.count && sent < PACKETS_PER_UPDATE; i++) {
        pending_command *cmd = &c->commands.items[i];
        if (cmd->sent > 0 && c->now - cmd->sent < again) continue;
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
}

tide_client *tide_client_create(const tide_client_desc *desc, const double now)
{
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
    free(c->handover_bytes);
    clear_commands(&c->commands);
    for (uint32_t i = 0; c->frames && i < c->w.history; i++) free(c->frames[i].data);
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
    c->now = now;
    if (c->state == TIDE_SESSION_OFFLINE) return;
    client_receive(c);
    if (c->state == TIDE_SESSION_OFFLINE) return;
    const double patience = !c->welcomed && c->desc.server.kind == TIDE_ADDRESS_ROOM ? ROOM_TIMEOUT : TIMEOUT;
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
        .delay_ticks = c->delay,
        .again_ticks = c->again,
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
    return (tide_view_worlds){world_at(c, c->ahead), c->ahead > c->first_tick ? world_at(c, c->ahead - 1u) : NULL,
                              (float)alpha, world_at(c, c->verified)};
}

void tide_client_mark_presented(tide_client *c)
{
    if (!c || !c->desc.correct_views || !c->loaded) return;
    const tide_view_worlds view = tide_client_view(c);
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
    tear_down(s);
    push_event(s, (tide_session_event){.kind = TIDE_SESSION_DISCONNECTED_EVENT, .reason = reason});
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
    start_server(s, start, NULL, (tide_player_set){{0}}, now);
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
        .lead = 2,
        .cookie = s->cookie,
        .knew_kick = s->kicked && !migrating,
        .jobs = s->desc.jobs,
        .base = migrating && s->stale ? tide_client_verified_world(s->stale) : NULL, // Most of it, most likely
        .now_ns = s->desc.now_ns,
        .resim_budget = s->desc.resim_budget,
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
    return c->loaded && c->handover.version && c->handover.key[0]
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
    if (s->stale) return (tide_view_worlds){tide_client_world(s->stale), NULL, 1.0f, tide_client_verified_world(s->stale)};
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
    if (!s->migrating) {
        if (network.close) network.close(network.self);
        return;
    }
    // The players in the match come back to it, but the last server's own
    const tide_client *old = s->stale;
    const handover *h = &old->handover;
    tide_player_set players = h->in_match;
    tide_player_set leaving = {{0}};
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
    if (tide_game_players(g) != s->players) return false;
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
    bool sending[TIDE_PLAYERS_MOST]; // What it was sending was the old build's: it starts again
    for (uint32_t i = 0; i < s->players; i++) {
        sending[i] = s->connections[i].used && s->connections[i].sending;
        if (sending[i]) stop_sending(s, &s->connections[i]);
        clear_commands(&s->connections[i].commands); // Wire bytes describe the old layout
    }
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
        if (sending[i]) start_snapshot(s, c);
    }
}

static bool prepare_client(const tide_client *c, const tide_game *g, const tide_migrate_fn migrate, void *user,
                           client_migration *m)
{
    if (tide_game_players(g) != c->players) return false; // Another number of players: the match starts over
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
    }
    stop_receiving(c); // A world it was receiving is the old build's: the server sends it again
    drop_base(c);      // ...and so is the one it would have built it from
    tide_hearing_rebuilt(&c->hearing); // ...and what its ticks sent, as its handlers heard it
    c->game = g;
    c->desc.game = g;
    if (changed && c->loaded && g->command && g->command_sequence) {
        // The server can already have processed an old command that this
        // client's verified world lacks. Read its watermark before assigning
        // another sequence, rather than accidentally reusing that request ID.
        c->commands_need_world = true;
        c->need_snapshot = true;
    }
    if (c->loaded) {
        c->ahead = c->verified;
        c->first_tick = c->verified; // Views don't blend from the old build's world
    }
}

// This machine's own player takes its server's world as it is now, which is
// what it would come to.
static void take_server_world(tide_client *c, const tide_server *s)
{
    c->game->copy_world(world_at(c, s->tick), s->world);
    c->verified = s->tick;
    c->ahead = s->tick;
    c->first_tick = s->tick;
    c->commands_need_world = false;
    c->need_snapshot = false;
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
