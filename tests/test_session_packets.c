#include <stdlib.h>
#include <string.h>

#include "tide/session.h"
#include "tide_test.h"

// Raw hostile peers exercise the public server boundary, including the state
// it must leave untouched when only a packet's final input is malformed.
typedef struct packet_world {
    uint32_t ticks, joins;
    uint8_t input;
} packet_world;

typedef struct packet_peer {
    tide_loopback *net;
    tide_server *server;
    tide_transport transport;
    tide_transport server_transport;
    tide_client *client;
    uint8_t data[TIDE_NET_MTU];
    uint64_t cookie;
    uint32_t nonce;
    double now;
} packet_peer;

static void packet_start(void *world, float dt, const void *start, uint32_t seed)
{
    (void)dt; (void)start; (void)seed;
    memset(world, 0, sizeof(packet_world));
}

static void packet_tick(void *world, const tide_jobs *jobs)
{
    (void)jobs;
    ((packet_world *)world)->ticks++;
}

static void packet_copy(void *to, const void *from) { memcpy(to, from, sizeof(packet_world)); }
static uint64_t packet_hash(const void *world) { return tide_hash(world, sizeof(packet_world)); }

static uint32_t packet_pack(const void *world, uint8_t *out, uint32_t capacity)
{
    if (out && capacity >= sizeof(packet_world)) memcpy(out, world, sizeof(packet_world));
    return sizeof(packet_world);
}

static bool packet_unpack(void *world, const uint8_t *data, uint32_t size)
{
    if (size != sizeof(packet_world)) return false;
    memcpy(world, data, size);
    return true;
}

static uint8_t *packet_pack_delta(const void *world, const void *base, const uint8_t *need, uint32_t need_size,
                                   bool assets, uint32_t *size)
{
    (void)base; (void)need; (void)need_size; (void)assets;
    *size = sizeof(packet_world);
    uint8_t *data = malloc(*size);
    if (data) memcpy(data, world, *size);
    return data;
}

static bool packet_unpack_delta(void *world, const void *base, const uint8_t *data, uint32_t size)
{
    (void)base;
    return packet_unpack(world, data, size);
}

static void packet_join(void *world, tide_player_id player)
{
    (void)player;
    ((packet_world *)world)->joins++;
}

static void packet_leave(void *world, tide_player_id player) { (void)world; (void)player; }

static bool packet_read(const uint8_t *data, uint32_t size, void *input)
{
    if (size != 1 || data[0] == 255u) return false;
    *(uint8_t *)input = data[0];
    return true;
}

static bool packet_read_delta(const uint8_t *data, uint32_t size, const void *previous, void *input)
{
    (void)previous;
    return packet_read(data, size, input);
}

static uint32_t packet_write(const void *input, uint8_t *data, uint32_t capacity)
{
    if (!capacity) return 0;
    data[0] = *(const uint8_t *)input;
    return 1;
}

static void packet_set(void *world, tide_player_id player, const void *input)
{
    if (player.id == 1) ((packet_world *)world)->input = *(const uint8_t *)input;
}

static void packet_get(const void *world, uint32_t slot, void *input)
{
    *(uint8_t *)input = slot == 0 ? ((const packet_world *)world)->input : 0;
}

static bool packet_check(const uint8_t *data, uint32_t size, uint32_t *channel)
{
    if (size != 2000u) return false;
    for (uint32_t i = 0; i < size; i++) if (data[i] != 0xCCu) return false;
    if (channel) *channel = 0;
    return true;
}

static uint32_t packet_sequence(const void *world, tide_player_id player, uint32_t channel)
{
    (void)world; (void)player; (void)channel;
    return 0;
}

static bool packet_command(void *world, tide_player_id player, uint32_t channel, uint32_t sequence,
                           const uint8_t *data, uint32_t size)
{
    (void)world; (void)player; (void)channel; (void)sequence;
    return packet_check(data, size, NULL);
}

static const tide_game packet_game = {
    .hash = 1234, .world_size = sizeof(packet_world), .max_players = 1,
    .input_size = 1, .max_input_bytes = 1,
    .start = packet_start, .tick = packet_tick, .copy_world = packet_copy, .hash_world = packet_hash,
    .pack_world = packet_pack, .unpack_world = packet_unpack,
    .pack_delta = packet_pack_delta, .unpack_delta = packet_unpack_delta,
    .player_joined = packet_join, .player_left = packet_leave,
    .read_input = packet_read, .read_input_delta = packet_read_delta, .write_input = packet_write,
    .world_input = packet_get, .set_input = packet_set,
    .check_command = packet_check, .command_sequence = packet_sequence, .command = packet_command,
};

static tide_writer packet_begin(packet_peer *p, uint8_t type)
{
    tide_writer w = {p->data, sizeof p->data, 0, false, false};
    tide_write_u16(&w, 0x5449u);
    tide_write_u8(&w, 26u); // PROTOCOL
    tide_write_u8(&w, type);
    if (type != 1u) {
        tide_write_u64(&w, p->cookie);
        tide_write_u32(&w, p->nonce);
    }
    return w;
}

static void packet_send(packet_peer *p, const tide_writer *w)
{
    TIDE_CHECK(!w->overflow);
    p->transport.send(p->transport.self, tide_loopback_address(1), w->data, w->size);
    tide_server_update(p->server, p->now);
}

static tide_writer packet_hello(packet_peer *p, uint32_t nonce, uint8_t flags)
{
    tide_writer w = packet_begin(p, 1); // HELLO
    tide_write_u64(&w, packet_game.hash);
    const uint8_t zero[32] = {0};
    tide_write_bytes(&w, zero, sizeof zero);
    tide_write_u32(&w, nonce);
    tide_write_u16(&w, 0);
    tide_write_u64(&w, p->cookie);
    tide_write_u8(&w, flags);
    tide_write_bytes(&w, zero, sizeof zero);
    return w;
}

static packet_peer packet_setup(void)
{
    packet_peer p = {.nonce = 123u};
    p.net = tide_loopback_create(1234);
    p.server = tide_server_create(&(tide_server_desc){.game = &packet_game, .wait_for_first = true,
        .transports = {tide_loopback_endpoint(p.net, 1)}}, 0);
    p.transport = tide_loopback_endpoint(p.net, 2);
    tide_writer w = packet_hello(&p, 123u, 0);
    packet_send(&p, &w);
    p.cookie = tide_server_cookie(p.server, (tide_player_id){1});
    return p;
}

static void packet_finish(packet_peer *p)
{
    if (p->client) tide_client_destroy(p->client);
    tide_server_destroy(p->server);
    tide_loopback_destroy(p->net);
}

static void packet_steps(packet_peer *p, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        p->now += 0.01;
        tide_loopback_set_time(p->net, p->now);
        tide_client_update(p->client, p->now);
        tide_server_update(p->server, p->now);
    }
}

static packet_peer packet_connected_client(void)
{
    packet_peer p = {0};
    p.net = tide_loopback_create(1234);
    p.server_transport = tide_loopback_endpoint(p.net, 1);
    p.server = tide_server_create(&(tide_server_desc){.game = &packet_game, .wait_for_first = true,
        .transports = {p.server_transport}}, 0);
    p.transport = tide_loopback_endpoint(p.net, 2);
    p.client = tide_client_create(&(tide_client_desc){.game = &packet_game,
        .transport = p.transport, .server = tide_loopback_address(1)}, 0);
    tide_client_update(p.client, 0);
    tide_address from;
    const uint32_t size = p.server_transport.receive(p.server_transport.self, &from, p.data, sizeof p.data);
    TIDE_CHECK(size > 48u && p.data[3] == 1u);
    tide_reader r = {p.data, size, 44u, false};
    p.nonce = tide_read_u32(&r);
    p.transport.send(p.transport.self, tide_loopback_address(1), p.data, size);
    packet_steps(&p, 50);
    p.cookie = tide_client_status_of(p.client).cookie;
    TIDE_CHECK(p.cookie && tide_client_status_of(p.client).state == TIDE_SESSION_CONNECTED);
    return p;
}

static tide_writer packet_client(packet_peer *p, uint32_t ack, uint32_t offset, uint8_t count)
{
    tide_writer w = packet_begin(p, 5); // CLIENT
    tide_write_u16(&w, 1);
    tide_write_u16(&w, 0);
    tide_write_u8(&w, 0); // No world requested
    tide_write_varint(&w, 0); // No chunks
    tide_write_varint(&w, ack);
    tide_write_varint64(&w, 0); // No frame mask
    tide_write_u8(&w, 0); // No SERVERs counted
    tide_write_varint(&w, 0); // No download
    tide_write_varint(&w, offset);
    tide_write_u8(&w, count);
    return w;
}

static void packet_input(tide_writer *w, uint8_t value)
{
    tide_write_varint(w, 1);
    tide_write_u8(w, value);
}

TIDE_TEST(session_packets_malformed_hello_cannot_replace_a_pending_connection)
{
    for (uint32_t kind = 0; kind < 2; kind++) {
        packet_peer p = packet_setup();
        const uint64_t cookie = tide_server_cookie(p.server, (tide_player_id){1});
        TIDE_REQUIRE(cookie);
        tide_writer w = packet_hello(&p, 124u, kind ? 0x80u : 0);
        if (!kind) tide_write_u8(&w, 0);
        packet_send(&p, &w);
        TIDE_CHECK(tide_server_cookie(p.server, (tide_player_id){1}) == cookie);
        w = packet_client(&p, 0, 0, 0);
        packet_send(&p, &w);
        TIDE_CHECK(tide_server_connected(p.server, (tide_player_id){1}));
        packet_finish(&p);
    }
}

TIDE_TEST(session_packets_validate_all_inputs_before_joining_or_applying)
{
    for (uint32_t kind = 0; kind < 6; kind++) {
        packet_peer p = packet_setup();
        tide_writer w = packet_client(&p, 0, 0, kind < 4 ? 2 : 0);
        if (kind < 4) packet_input(&w, 7);
        if (kind == 0) tide_write_varint(&w, 1); // Truncated second input
        if (kind == 1) packet_input(&w, 255); // Decoder rejects it
        if (kind == 2) tide_write_varint(&w, 0); // Empty second input
        if (kind == 3) { packet_input(&w, 8); tide_write_u8(&w, 0); } // Trailing bytes
        if (kind == 4) w = packet_client(&p, 1, 0, 0); // Acknowledges an unsent future tick
        if (kind == 5) w = packet_client(&p, 0, UINT32_MAX, 1); // Input tick would wrap
        packet_send(&p, &w);
        TIDE_CHECK(!tide_server_connected(p.server, (tide_player_id){1}));
        w = packet_client(&p, 0, 0, 0); // A valid packet can still greet it
        packet_send(&p, &w);
        TIDE_CHECK(tide_server_connected(p.server, (tide_player_id){1}));
        tide_server_update(p.server, 0.1);
        const packet_world *world = tide_server_world(p.server);
        TIDE_CHECK(world->joins == 1);
        TIDE_CHECK(world->input == 0); // First input of each rejected packet was not retained
        packet_finish(&p);
    }
}

TIDE_TEST(session_packets_keep_valid_whole_and_delta_inputs)
{
    packet_peer p = packet_setup();
    tide_writer w = packet_client(&p, 0, 0, 2);
    packet_input(&w, 7);
    packet_input(&w, 11);
    packet_send(&p, &w);
    tide_server_update(p.server, 0.1);
    const packet_world *world = tide_server_world(p.server);
    TIDE_CHECK(world->joins == 1 && world->ticks > 1);
    TIDE_CHECK(world->input == 11);
    packet_finish(&p);
}

TIDE_TEST(session_packets_far_future_inputs_cannot_suppress_late_input_feedback)
{
    packet_peer p = packet_setup();
    tide_writer w = packet_client(&p, 0, 100000, 1);
    packet_input(&w, 7); // Far outside the server's receive window
    packet_send(&p, &w);
    w = packet_client(&p, 0, 0, 1);
    packet_input(&w, 3);
    packet_send(&p, &w);
    tide_server_update(p.server, 0.1); // Tick zero arrived; all subsequent inputs are missing.
    tide_address from;
    uint32_t size;
    int32_t waited = INT32_MAX;
    while ((size = p.transport.receive(p.transport.self, &from, p.data, sizeof p.data)) > 0) {
        tide_reader r = {p.data, size, 0, false};
        (void)tide_read_u16(&r); (void)tide_read_u8(&r);
        if (tide_read_u8(&r) != 6) continue;
        (void)tide_read_u64(&r); (void)tide_read_u32(&r); // Cookie and connection attempt
        (void)tide_read_u16(&r); (void)tide_read_u16(&r);
        for (uint32_t i = 0; i < 3; i++) (void)tide_read_varint(&r); // Held, tick, lead
        waited = tide_unzigzag(tide_read_varint(&r));
        TIDE_CHECK(!r.failed);
    }
    TIDE_CHECK(waited < 0); // The future claim cannot leave feedback stuck at tick zero's positive wait.
    TIDE_CHECK(((const packet_world *)tide_server_world(p.server))->input == 3);
    packet_finish(&p);
}

static void packet_upload(packet_peer *p, uint32_t number, uint32_t sequence, uint32_t size, uint32_t index)
{
    tide_writer w = packet_begin(p, 10); // UPLOAD; CHUNK is 1000
    tide_write_varint(&w, number);
    tide_write_varint(&w, sequence);
    tide_write_varint(&w, size);
    tide_write_varint(&w, index);
    uint8_t bytes[1000];
    memset(bytes, 0xCC, sizeof bytes);
    const uint32_t n = size - index * 1000u < sizeof bytes ? size - index * 1000u : sizeof bytes;
    tide_write_u16(&w, (uint16_t)n);
    tide_write_bytes(&w, bytes, n);
    packet_send(p, &w);
}

static uint8_t packet_upload_state(packet_peer *p)
{
    uint8_t result = 0;
    tide_address from;
    uint32_t size;
    while ((size = p->transport.receive(p->transport.self, &from, p->data, sizeof p->data)) > 0) {
        tide_reader r = {p->data, size, 0, false};
        (void)tide_read_u16(&r);
        (void)tide_read_u8(&r);
        if (tide_read_u8(&r) != 6) continue; // SERVER
        (void)tide_read_u64(&r); (void)tide_read_u32(&r);
        (void)tide_read_u16(&r); (void)tide_read_u16(&r);
        for (uint32_t i = 0; i < 4; i++) (void)tide_read_varint(&r); // Held, tick, lead, wait
        result = tide_read_u8(&r);
        TIDE_CHECK(!r.failed);
    }
    return result;
}

TIDE_TEST(session_packets_upload_metadata_cannot_resize_or_resequence_existing_buffers)
{
    packet_peer p = packet_setup();
    tide_writer w = packet_client(&p, 0, 0, 0);
    packet_send(&p, &w);
    packet_upload(&p, 1, 1, 2000, 0);
    TIDE_CHECK(packet_upload_state(&p) == 1); // PART
    packet_upload(&p, 1, 1, TIDE_COMMAND_LIMIT, 1024); // Index far outside original allocations
    TIDE_CHECK(packet_upload_state(&p) == 1);
    packet_upload(&p, 1, 1, 1025, 1); // A shorter final chunk cannot complete it either
    TIDE_CHECK(packet_upload_state(&p) == 1);
    packet_upload(&p, 1, 2, 2000, 1); // Same size, forged sequence
    TIDE_CHECK(packet_upload_state(&p) == 1);
    packet_upload(&p, 1, 1, 2000, 1);
    TIDE_CHECK(packet_upload_state(&p) == 2); // WHOLE, original transfer still usable
    packet_upload(&p, 2, 2, 2000, 0); // A new number legitimately starts a new transfer
    TIDE_CHECK(packet_upload_state(&p) == 1);
    packet_upload(&p, 2, 2, 2000, 1);
    TIDE_CHECK(packet_upload_state(&p) == 2);
    packet_finish(&p);
}

TIDE_TEST(session_packets_cookie_proof_precedes_greeting_inputs_and_disconnect)
{
    packet_peer p = packet_setup();
    const uint64_t cookie = p.cookie;
    p.cookie ^= 1u;
    tide_writer w = packet_client(&p, 0, 0, 1);
    packet_input(&w, 7);
    packet_send(&p, &w);
    TIDE_CHECK(!tide_server_connected(p.server, (tide_player_id){1}));
    p.cookie = cookie;
    p.nonce++;
    w = packet_client(&p, 0, 0, 0);
    packet_send(&p, &w);
    TIDE_CHECK(!tide_server_connected(p.server, (tide_player_id){1}));
    p.nonce--;
    for (uint32_t cut = 4; cut < 16; cut++) {
        w = packet_client(&p, 0, 0, 0);
        w.size = cut;
        packet_send(&p, &w);
        TIDE_CHECK(!tide_server_connected(p.server, (tide_player_id){1}));
    }
    w = packet_client(&p, 0, 0, 1);
    packet_input(&w, 3);
    packet_send(&p, &w);
    TIDE_CHECK(tide_server_connected(p.server, (tide_player_id){1}));
    p.cookie ^= 1u;
    w = packet_client(&p, 0, 0, 1);
    packet_input(&w, 9);
    packet_send(&p, &w);
    w = packet_begin(&p, 7); // Forged BYE from the correct address
    packet_send(&p, &w);
    TIDE_CHECK(tide_server_connected(p.server, (tide_player_id){1}));
    tide_server_update(p.server, 0.1);
    TIDE_CHECK(((const packet_world *)tide_server_world(p.server))->input == 3);
    p.cookie = cookie;
    w = packet_begin(&p, 7);
    packet_send(&p, &w);
    TIDE_CHECK(!tide_server_connected(p.server, (tide_player_id){1}));
    packet_finish(&p);
}

TIDE_TEST(session_packets_hello_needs_existing_cookie_and_old_attempts_cannot_disconnect_reconnects)
{
    packet_peer p = packet_setup();
    tide_writer w = packet_client(&p, 0, 0, 0);
    packet_send(&p, &w);
    const uint64_t cookie = p.cookie;
    for (uint32_t mode = 0; mode < 2; mode++) {
        p.cookie = mode ? cookie ^ 1u : 0;
        w = packet_hello(&p, 124u, 0);
        packet_send(&p, &w);
        TIDE_CHECK(tide_server_connected(p.server, (tide_player_id){1}));
        TIDE_CHECK(tide_server_cookie(p.server, (tide_player_id){1}) == cookie);
    }
    p.cookie = cookie;
    w = packet_hello(&p, 124u, 0);
    packet_send(&p, &w); // Genuine reconnect with the same cookie
    w = packet_begin(&p, 7); // Cookie is right, attempt 123 is stale
    packet_send(&p, &w);
    TIDE_CHECK(tide_server_connected(p.server, (tide_player_id){1}));
    p.nonce = 124u;
    w = packet_client(&p, 0, 0, 0);
    packet_send(&p, &w);
    TIDE_CHECK(tide_server_connected(p.server, (tide_player_id){1}));
    w = packet_begin(&p, 7);
    packet_send(&p, &w);
    TIDE_CHECK(!tide_server_connected(p.server, (tide_player_id){1}));
    packet_finish(&p);
}

TIDE_TEST(session_packets_client_ignores_forged_server_frames_refusals_and_disconnects)
{
    packet_peer p = packet_connected_client();
    const uint64_t cookie = p.cookie;
    const uint32_t nonce = p.nonce;
    const uint32_t verified = tide_client_status_of(p.client).verified_tick;
    for (uint32_t mode = 0; mode < 2; mode++) {
        p.cookie = mode ? cookie : cookie ^ 1u;
        p.nonce = mode ? nonce ^ 1u : nonce;
        tide_writer w = packet_begin(&p, 6); // Valid SERVER syntax, forged credentials
        tide_write_u16(&w, 1); tide_write_u16(&w, 0);
        tide_write_varint(&w, 0); // Held milliseconds
        tide_write_varint(&w, 100000); // Forged server clock
        tide_write_varint(&w, 0); tide_write_varint(&w, tide_zigzag(-30001));
        tide_write_u8(&w, 0); tide_write_u8(&w, 0); // No upload or frame pieces
        p.server_transport.send(p.server_transport.self, tide_loopback_address(2), w.data, w.size);
        w = packet_begin(&p, 7);
        tide_write_u8(&w, 1); // ENDED
        p.server_transport.send(p.server_transport.self, tide_loopback_address(2), w.data, w.size);
        tide_client_update(p.client, p.now);
        TIDE_CHECK(tide_client_status_of(p.client).state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(tide_client_status_of(p.client).server_tick < 100000);
    }
    p.cookie = cookie;
    p.nonce = nonce;
    tide_writer w = packet_begin(&p, 3); // REFUSE has only an echoed nonce, not the connected prefix.
    w.size = 4;
    tide_write_u32(&w, nonce);
    tide_write_u8(&w, 1);
    p.server_transport.send(p.server_transport.self, tide_loopback_address(2), w.data, w.size);
    w = packet_begin(&p, 7); // Even authenticated malformed BYE is ignored.
    p.server_transport.send(p.server_transport.self, tide_loopback_address(2), w.data, w.size);
    tide_client_update(p.client, p.now);
    TIDE_CHECK(tide_client_status_of(p.client).state == TIDE_SESSION_CONNECTED);
    packet_steps(&p, 10);
    TIDE_CHECK(tide_client_status_of(p.client).verified_tick > verified);
    TIDE_CHECK(tide_client_status_of(p.client).resyncs == 0);
    w = packet_begin(&p, 7);
    tide_write_u8(&w, 1);
    p.server_transport.send(p.server_transport.self, tide_loopback_address(2), w.data, w.size);
    tide_client_update(p.client, p.now);
    TIDE_CHECK(tide_client_status_of(p.client).state == TIDE_SESSION_OFFLINE);
    TIDE_CHECK(tide_client_status_of(p.client).reason == TIDE_DISCONNECT_ENDED);
    packet_finish(&p);
}

TIDE_TEST(session_packets_forged_server_packets_cannot_keep_a_connection_alive)
{
    packet_peer p = packet_connected_client();
    p.cookie ^= 1u;
    for (uint32_t i = 0; i < 70; i++) {
        p.now += 0.1;
        tide_loopback_set_time(p.net, p.now);
        tide_writer w = packet_begin(&p, 7);
        tide_write_u8(&w, 0);
        p.server_transport.send(p.server_transport.self, tide_loopback_address(2), w.data, w.size);
        tide_client_update(p.client, p.now); // The genuine server has stopped updating.
    }
    TIDE_CHECK(tide_client_status_of(p.client).state == TIDE_SESSION_OFFLINE);
    TIDE_CHECK(tide_client_status_of(p.client).reason == TIDE_DISCONNECT_TIMED_OUT);
    packet_finish(&p);
}

static uint64_t packet_failed_entropy(void) { return 0; }

TIDE_TEST(session_packets_reconnect_can_verify_a_kick_before_receiving_a_welcome)
{
    packet_peer p = packet_connected_client();
    TIDE_REQUIRE(tide_server_kick(p.server, (tide_player_id){1}, "Bye"));
    tide_client_destroy(p.client); // Its transport closes before it hears the kick.
    p.transport = tide_loopback_endpoint(p.net, 2);
    p.client = tide_client_create(&(tide_client_desc){.game = &packet_game, .transport = p.transport,
        .server = tide_loopback_address(1), .cookie = p.cookie}, p.now);
    packet_steps(&p, 10);
    const tide_client_status status = tide_client_status_of(p.client);
    TIDE_CHECK(status.state == TIDE_SESSION_OFFLINE && status.reason == TIDE_DISCONNECT_KICKED);
    TIDE_CHECK(strcmp(status.message, "Bye") == 0);
    packet_finish(&p);
}

TIDE_TEST(session_packets_failed_entropy_does_not_issue_known_cookie_credentials)
{
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &packet_game,
        .entropy = packet_failed_entropy}, 0);
    TIDE_CHECK(server == NULL);
    if (server) tide_server_destroy(server);
}

// Ticks to a player: each in a few datagrams in a row, as many as the loss
// the player's CLIENTs measure takes; then only once a CLIENT shows it lost,
// or nothing was heard of it for as long as an acknowledgement takes
// (session.c, send_frames). A raw peer says what it likes of the ticks and the
// datagrams it has, and counts the times each tick comes.
#define FRAMES_COUNTED 16384u

typedef struct frames_seen {
    uint16_t counts[FRAMES_COUNTED]; // Times each tick came
    uint32_t base;       // The first tick the tests speak of
    uint32_t tick;       // The server's, as its last datagram said
    uint16_t said;       // ...and its time
    uint32_t datagrams;  // SERVERs the last read took...
    uint32_t pieces;     // ...and the ticks in them
    uint32_t got;        // SERVERs read so far
    uint32_t lose_every; // Of those, each one in this many it says it didn't read; 0: it says them all
    uint32_t late_every; // ...and each one in this many it counts a CLIENT late, as one that came out of order
    uint32_t late;       // Read, and not counted yet
    uint8_t claimed;     // ...so this many, wrapping, is what it says
    uint16_t their_time;
} frames_seen;

static void frames_read(packet_peer *p, frames_seen *seen)
{
    tide_address from;
    uint32_t size;
    seen->datagrams = 0;
    seen->pieces = 0;
    while ((size = p->transport.receive(p->transport.self, &from, p->data, sizeof p->data)) > 0) {
        tide_reader r = {p->data, size, 0, false};
        (void)tide_read_u16(&r);
        (void)tide_read_u8(&r);
        if (tide_read_u8(&r) != 6) continue; // SERVER
        (void)tide_read_u64(&r); (void)tide_read_u32(&r);
        seen->said = tide_read_u16(&r);
        (void)tide_read_u16(&r);
        (void)tide_read_varint(&r); // Held
        seen->tick = tide_read_varint(&r);
        (void)tide_read_varint(&r); (void)tide_read_varint(&r); // Lead, wait
        TIDE_CHECK(tide_read_u8(&r) == 0); // No upload
        const uint8_t pieces = tide_read_u8(&r);
        for (uint32_t i = 0; i < pieces; i++) {
            const uint32_t back = tide_read_varint(&r);
            const uint32_t total = tide_read_varint(&r);
            TIDE_REQUIRE(!r.failed && back > 0 && back <= seen->tick && total > 0 && total <= 1000u);
            TIDE_REQUIRE(tide_read_bytes(&r, total) != NULL);
            if (seen->tick - back < FRAMES_COUNTED) seen->counts[seen->tick - back]++;
            seen->pieces++;
        }
        TIDE_CHECK(!r.failed && r.at == r.size);
        seen->datagrams++;
        seen->got++;
        if (seen->late_every && seen->got % seen->late_every == 0) {
            seen->late++;
        } else if (!seen->lose_every || seen->got % seen->lose_every) {
            seen->claimed = (uint8_t)(seen->claimed + 1u + seen->late);
            seen->late = 0;
        }
    }
}

// A CLIENT that has every tick before `ack` and those of `mask` from it on,
// echoes `echo` and has read as many SERVERs as it claims. Queued: the server
// reads it at its next update.
static void frames_say(packet_peer *p, frames_seen *seen, uint16_t echo, uint32_t ack, uint64_t mask)
{
    tide_writer w = packet_begin(p, 5); // CLIENT
    tide_write_u16(&w, ++seen->their_time);
    tide_write_u16(&w, echo);
    tide_write_u8(&w, 0); // No world requested
    tide_write_varint(&w, 0); // No chunks
    tide_write_varint(&w, ack);
    tide_write_varint64(&w, mask);
    tide_write_u8(&w, seen->claimed);
    tide_write_varint(&w, 0); // No download
    tide_write_varint(&w, 0);
    tide_write_u8(&w, 0); // No inputs
    TIDE_CHECK(!w.overflow);
    p->transport.send(p->transport.self, tide_loopback_address(1), w.data, w.size);
}

static void frames_update(packet_peer *p, frames_seen *seen, uint32_t updates)
{
    for (uint32_t i = 0; i < updates; i++) {
        p->now += 1.0 / 60.0;
        tide_server_update(p->server, p->now);
        frames_read(p, seen);
    }
}

// Says the same with an old echo after it, so the server's round trip stays a second whatever was just echoed:
// nothing goes again for want of an acknowledgement for a second and a half
static void frames_slow(packet_peer *p, frames_seen *seen, uint32_t ack, uint64_t mask)
{
    frames_say(p, seen, (uint16_t)(seen->said - 1000u), ack, mask);
}

// Updates in which the peer reads every datagram at once and has every tick
static void frames_keep_up(packet_peer *p, frames_seen *seen, uint32_t updates)
{
    for (uint32_t i = 0; i < updates; i++) {
        frames_say(p, seen, seen->said, seen->tick, 0);
        frames_update(p, seen, 1);
        TIDE_CHECK(seen->datagrams == 1);
    }
}

// In the match, with the datagrams the loss is measured over once all read, so ticks go in two datagrams; then
// silent, with every tick before `base` acknowledged
static packet_peer frames_setup(frames_seen *seen)
{
    packet_peer p = packet_setup();
    memset(seen, 0, sizeof *seen);
    p.now = 1.0;
    frames_say(&p, seen, 0, 0, 0);
    frames_update(&p, seen, 1);
    TIDE_CHECK(tide_server_connected(p.server, (tide_player_id){1}));
    frames_keep_up(&p, seen, 520);
    seen->base = seen->tick;
    frames_slow(&p, seen, seen->base, 0);
    return p;
}

TIDE_TEST(session_frames_go_in_two_datagrams_then_wait_for_word_of_them)
{
    frames_seen *seen = malloc(sizeof *seen);
    packet_peer p = frames_setup(seen);
    for (uint32_t i = 0; i < 60; i++) {
        frames_update(&p, seen, 1);
        TIDE_CHECK(seen->datagrams == 1); // One an update, whatever it lacks
    }
    const uint32_t base = seen->base;
    TIDE_REQUIRE(seen->tick > base + 50u);
    for (uint32_t t = base; t + 2 < seen->tick; t++) TIDE_CHECK(seen->counts[t] == 2);
    // Nothing heard of the first for as long as an acknowledgement takes: once more, then quiet again
    frames_update(&p, seen, 24);
    TIDE_CHECK(seen->counts[base] == 2 && seen->counts[base + 1u] == 2);
    frames_update(&p, seen, 30);
    TIDE_CHECK(seen->counts[base] == 3 && seen->counts[base + 1u] == 3);
    for (uint32_t t = base + 40u; t < base + 50u; t++) TIDE_CHECK(seen->counts[t] == 2);
    packet_finish(&p);
    free(seen);
}

TIDE_TEST(session_frames_go_in_eight_datagrams_until_the_loss_is_measured)
{
    frames_seen *seen = malloc(sizeof *seen);
    packet_peer p = packet_setup();
    memset(seen, 0, sizeof *seen);
    p.now = 1.0;
    frames_say(&p, seen, 0, 0, 0);
    frames_update(&p, seen, 1);
    frames_say(&p, seen, (uint16_t)(seen->said - 1000u), 0, 0);
    frames_update(&p, seen, 40);
    for (uint32_t t = 0; t < seen->tick; t++) {
        if (seen->counts[t]) { seen->base = t; break; }
    }
    for (uint32_t t = seen->base; t + 9 < seen->tick; t++) TIDE_CHECK(seen->counts[t] == 8);
    packet_finish(&p);
    free(seen);
}

TIDE_TEST(session_frames_shown_lost_go_again_at_once_and_once)
{
    frames_seen *seen = malloc(sizeof *seen);
    packet_peer p = frames_setup(seen);
    frames_update(&p, seen, 20);
    const uint32_t base = seen->base;
    // It read a datagram of the last update and hasn't a tick that went in that update: nothing says that one
    // was lost (an update can send several datagrams, which come one by one)
    const uint32_t last = seen->tick - 2u; // The one whose second datagram the last update's was
    TIDE_REQUIRE(last > base + 8u && last - base < 60u && seen->counts[last] == 2);
    const uint64_t but_last = ((1ull << (seen->tick - base)) - 1ull) & ~(1ull << (last - base));
    frames_say(&p, seen, seen->said, base, but_last);
    frames_slow(&p, seen, base, but_last);
    frames_update(&p, seen, 1);
    TIDE_CHECK(seen->counts[last] == 2);
    const uint32_t made = seen->tick - base;
    TIDE_REQUIRE(made >= 12 && made < 60);
    // It read the last datagram and has every tick so far but the first and the sixth
    const uint64_t all = (1ull << made) - 1ull;
    const uint64_t lacks = all & ~(1ull << 0) & ~(1ull << 5);
    const uint16_t heard = seen->said;
    frames_say(&p, seen, heard, base, lacks);
    frames_slow(&p, seen, base, lacks);
    frames_update(&p, seen, 1);
    TIDE_CHECK(seen->counts[base] == 3 && seen->counts[base + 5u] == 3); // At once, with no wait for a time
    TIDE_CHECK(seen->counts[base + 1u] == 2 && seen->counts[base + 4u] == 2 && seen->counts[base + 6u] == 2);
    TIDE_CHECK(seen->datagrams == 1);
    // The same word again says nothing new: it's of before they went again
    frames_say(&p, seen, heard, base, lacks);
    frames_slow(&p, seen, base, lacks);
    frames_update(&p, seen, 10);
    TIDE_CHECK(seen->counts[base] == 3 && seen->counts[base + 5u] == 3);
    // Ticks were lost in both their datagrams, at once: the ticks after go in three
    for (uint32_t t = base + made + 2u; t + 5 < seen->tick; t++) TIDE_CHECK(seen->counts[t] == 3);
    // Word from after they went again, still without the sixth: once more. The first it now has, and hasn't run
    const uint32_t later = seen->tick;
    TIDE_REQUIRE(later - base < 60);
    const uint64_t but_sixth = ((1ull << (later - base)) - 1ull) & ~(1ull << 5);
    frames_say(&p, seen, seen->said, base, but_sixth);
    frames_slow(&p, seen, base, but_sixth);
    frames_update(&p, seen, 1);
    TIDE_CHECK(seen->counts[base] == 3 && seen->counts[base + 5u] == 4);
    // ...and a tick shown lost twice counts once: still three
    frames_update(&p, seen, 12);
    for (uint32_t t = later + 2u; t + 5 < seen->tick; t++) TIDE_CHECK(seen->counts[t] == 3);
    TIDE_CHECK(seen->counts[base] == 3 && seen->counts[base + 5u] == 4 && seen->counts[base + 1u] == 2);
    // A CLIENT from before those, come late, says what it had then: nothing goes for it
    seen->their_time -= 20u;
    frames_say(&p, seen, heard, base, 0);
    seen->their_time += 20u;
    const uint16_t second = seen->counts[base + 1u], third = seen->counts[base + 2u];
    frames_update(&p, seen, 3);
    TIDE_CHECK(seen->counts[base + 1u] == second && seen->counts[base + 2u] == third && seen->counts[base] == 3);
    packet_finish(&p);
    free(seen);
}

// How many datagrams in a row a new tick goes in now: the peer says nothing of the ticks from here for a while
static uint32_t frames_copies(packet_peer *p, frames_seen *seen)
{
    const uint32_t from = seen->tick;
    frames_slow(p, seen, from, 0);
    frames_update(p, seen, 14);
    const uint32_t copies = seen->counts[from + 1u];
    for (uint32_t t = from + 1u; t + 9u < seen->tick; t++) TIDE_CHECK(seen->counts[t] == copies);
    return copies;
}

TIDE_TEST(session_frames_copies_follow_the_loss_measured)
{
    frames_seen *seen = malloc(sizeof *seen);
    packet_peer p = frames_setup(seen);
    TIDE_CHECK(frames_copies(&p, seen) == 2);
    // One datagram in a hundred lost, for as many as the loss is measured over and a few: three in a row keep a
    // tick from being lost in all of them more than once in a hundred thousand (0.01 cubed)
    seen->lose_every = 100;
    frames_keep_up(&p, seen, 600);
    TIDE_CHECK(frames_copies(&p, seen) == 3);
    // One in eight: six
    seen->lose_every = 8;
    frames_keep_up(&p, seen, 600);
    TIDE_CHECK(frames_copies(&p, seen) == 6);
    // None: two again, once that many were all read
    seen->lose_every = 0;
    frames_keep_up(&p, seen, 1100);
    TIDE_CHECK(frames_copies(&p, seen) == 2);
    // One in five comes out of order, counted a CLIENT late: none is lost
    seen->late_every = 5;
    frames_keep_up(&p, seen, 1100);
    TIDE_CHECK(frames_copies(&p, seen) == 2);
    seen->late_every = 0;
    // A network gone bad shows within a second, not at the next measure: with one in four lost, sixteen were
    // by then, eight or more of them among the last counted apart, which four datagrams are for
    seen->lose_every = 4;
    frames_keep_up(&p, seen, 64);
    TIDE_CHECK(frames_copies(&p, seen) >= 4);
    // Every datagram lost: eight, never more
    seen->lose_every = 1;
    frames_keep_up(&p, seen, 600);
    TIDE_CHECK(frames_copies(&p, seen) == 8);
    seen->lose_every = 0;
    frames_keep_up(&p, seen, 1100);
    TIDE_CHECK(frames_copies(&p, seen) == 2);
    TIDE_CHECK(tide_server_connected(p.server, (tide_player_id){1}));
    packet_finish(&p);
    free(seen);
}

TIDE_TEST(session_frames_hostile_acknowledgements_cost_a_bounded_number_of_datagrams)
{
    frames_seen *seen = malloc(sizeof *seen);
    packet_peer p = frames_setup(seen);
    frames_update(&p, seen, 10);
    // An echo of a time the server never said shows nothing lost
    const uint32_t base = seen->base;
    const uint32_t before = seen->tick;
    frames_say(&p, seen, (uint16_t)(seen->said + 500u), base, 0);
    frames_slow(&p, seen, base, 0);
    frames_update(&p, seen, 12);
    for (uint32_t t = base; t + 2 < before; t++) TIDE_CHECK(seen->counts[t] == 2);
    // A tick that isn't made yet, and bits for ticks that aren't: refused, or the peer's own loss
    frames_say(&p, seen, seen->said, seen->tick + 1000u, UINT64_MAX);
    frames_say(&p, seen, seen->said, UINT32_MAX, UINT64_MAX);
    frames_update(&p, seen, 2);
    TIDE_CHECK(tide_server_connected(p.server, (tide_player_id){1}));
    // Every update it says it read the last datagram and has nothing, then something else, with whatever count
    // of datagrams read: each tick it lacks comes once an update at most, in no more datagrams than an update
    // may send
    uint32_t most = 0;
    for (uint32_t i = 0; i < 120; i++) {
        seen->claimed = (uint8_t)(i * 97u);
        frames_say(&p, seen, seen->said, base, 0);
        seen->claimed = (uint8_t)(i * 31u + 200u);
        frames_say(&p, seen, (uint16_t)(seen->said - (uint16_t)(i * 7919u)), base, i % 3u ? 0 : UINT64_MAX << (i % 64u));
        const uint16_t had = seen->counts[base + 3u];
        frames_update(&p, seen, 1);
        if (seen->datagrams > most) most = seen->datagrams;
        TIDE_CHECK(seen->counts[base + 3u] <= had + 1u);
    }
    TIDE_CHECK(most >= 1 && most <= 8);
    // Then that it never has the last twenty ticks, and never read a datagram, though it echoes each: ticks go
    // in eight datagrams, never more
    for (uint32_t i = 0; i < 80; i++) {
        seen->claimed = 7;
        frames_say(&p, seen, seen->said, seen->tick - 20u, 0);
        frames_update(&p, seen, 1);
        TIDE_CHECK(seen->datagrams >= 1 && seen->datagrams <= 8);
    }
    TIDE_CHECK(frames_copies(&p, seen) == 8);
    TIDE_CHECK(tide_server_connected(p.server, (tide_player_id){1}));
    packet_finish(&p);
    free(seen);
}

TIDE_TEST(session_frames_further_copies_take_a_quarter_of_a_datagram_at_most)
{
    frames_seen *seen = malloc(sizeof *seen);
    packet_peer p = frames_setup(seen);
    const uint32_t base = seen->base;
    // Two and a half seconds with no word of 150 ticks, from a peer three seconds away: each went twice
    frames_say(&p, seen, (uint16_t)(seen->said - 3000u), base, 0);
    frames_update(&p, seen, 150);
    const uint32_t made = seen->tick;
    TIDE_REQUIRE(made >= base + 140u);
    for (uint32_t t = base; t + 2 < made; t++) TIDE_CHECK(seen->counts[t] == 2);
    // The first was lost in both its datagrams, so ticks go in three: some ninety that the peer said nothing
    // of went in two. The newest sixty-four of them get one more, the newest first, a quarter of a datagram at a
    // time, in the one datagram an update sends; the ones before those are too old for a datagram's loss to
    // be what keeps them from the peer
    const uint64_t but_first = UINT64_MAX & ~1ull;
    frames_say(&p, seen, seen->said, base, but_first);
    frames_say(&p, seen, (uint16_t)(seen->said - 3000u), base, but_first);
    frames_update(&p, seen, 1);
    TIDE_CHECK(seen->counts[base] == 3);
    TIDE_CHECK(seen->datagrams == 1);
    frames_update(&p, seen, 1);
    TIDE_CHECK(seen->datagrams == 1);
    TIDE_CHECK(seen->pieces >= 15 && seen->pieces <= 30);
    TIDE_CHECK(seen->counts[made - 3u] == 3 && seen->counts[made - 12u] == 3); // The newest first
    TIDE_CHECK(seen->counts[made - 50u] == 2);
    for (uint32_t i = 0; i < 6; i++) {
        frames_update(&p, seen, 1);
        TIDE_CHECK(seen->datagrams == 1 && seen->pieces <= 30);
    }
    for (uint32_t t = made - 55u; t + 2 < made; t++) TIDE_CHECK(seen->counts[t] == 3);
    for (uint32_t t = base + 64u; t + 75u < made; t++) TIDE_CHECK(seen->counts[t] == 2); // Too old
    for (uint32_t t = base + 1u; t < base + 64u; t++) TIDE_CHECK(seen->counts[t] == 2); // It said it has those
    packet_finish(&p);
    free(seen);
}

// A real client 50 ms from its server, whose datagrams to it a test can lose
typedef struct frames_lossy {
    tide_transport inner;
    uint32_t lose; // SERVERs to drop from here
    uint8_t last[TIDE_NET_MTU]; // The last SERVER that went, to read its ticks
    uint32_t last_size;
} frames_lossy;

static void frames_lossy_send(void *self, tide_address to, const void *data, uint32_t size)
{
    frames_lossy *l = self;
    if (size > 3 && size <= sizeof l->last && ((const uint8_t *)data)[3] == 6) {
        if (l->lose) {
            l->lose--;
            return;
        }
        memcpy(l->last, data, size);
        l->last_size = size;
    }
    l->inner.send(l->inner.self, to, data, size);
}

static uint32_t frames_lossy_receive(void *self, tide_address *from, void *data, uint32_t capacity)
{
    frames_lossy *l = self;
    return l->inner.receive(l->inner.self, from, data, capacity);
}

// Ticks in the last SERVER that went
static uint32_t frames_lossy_pieces(const frames_lossy *l)
{
    tide_reader r = {l->last, l->last_size, 16u, false};
    (void)tide_read_u16(&r); (void)tide_read_u16(&r);
    for (uint32_t i = 0; i < 4; i++) (void)tide_read_varint(&r);
    (void)tide_read_u8(&r);
    return tide_read_u8(&r);
}

typedef struct frames_match {
    frames_lossy lossy;
    tide_loopback *net;
    tide_server *server;
    tide_client *client;
    double now;
} frames_match;

// How far behind the server's the client's verified world got, in ticks, over `updates`
static uint32_t frames_run(frames_match *m, uint32_t updates)
{
    uint32_t most = 0;
    for (uint32_t i = 0; i < updates; i++) {
        m->now += 1.0 / 60.0;
        tide_loopback_set_time(m->net, m->now);
        tide_client_update(m->client, m->now);
        tide_server_update(m->server, m->now);
        const uint32_t verified = tide_client_status_of(m->client).verified_tick;
        const uint32_t tick = tide_server_tick(m->server);
        if (tick > verified && tick - verified > most) most = tick - verified;
    }
    return most;
}

TIDE_TEST(session_frames_a_lost_datagram_stalls_nobody_and_two_cost_a_round_trip)
{
    frames_match *m = calloc(1, sizeof *m);
    m->net = tide_loopback_create(77);
    tide_loopback_set_conditions(m->net, (tide_net_conditions){.latency = 0.05});
    m->lossy.inner = tide_loopback_endpoint(m->net, 1);
    m->server = tide_server_create(&(tide_server_desc){.game = &packet_game, .wait_for_first = true,
        .transports = {{.self = &m->lossy, .send = frames_lossy_send, .receive = frames_lossy_receive}}}, 0);
    m->client = tide_client_create(&(tide_client_desc){.game = &packet_game,
        .transport = tide_loopback_endpoint(m->net, 2), .server = tide_loopback_address(1), .lead = 2}, 0);
    TIDE_REQUIRE(m->server && m->client);
    frames_run(m, 60);
    TIDE_REQUIRE(tide_client_status_of(m->client).state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(frames_lossy_pieces(&m->lossy) >= 5); // Every tick it hasn't acknowledged, until its datagrams are counted
    frames_run(m, 600);
    const uint32_t steady = frames_run(m, 120); // With nothing lost
    TIDE_CHECK(steady >= 3 && steady <= 8);
    TIDE_CHECK(frames_lossy_pieces(&m->lossy) == 2); // The tick just made and the one before

    // One datagram lost: the tick it had is in the next one too. And one lost of so few is a loss three
    // datagrams are for, until as many are counted with none lost
    m->lossy.lose = 1;
    TIDE_CHECK(frames_run(m, 40) <= steady + 1u);
    TIDE_CHECK(frames_lossy_pieces(&m->lossy) == 3);
    frames_run(m, 1100);
    TIDE_CHECK(frames_lossy_pieces(&m->lossy) == 2);

    // Two in a row: a tick was in neither of its datagrams. It comes when the client's word of the datagram
    // after them gets back, a round trip, not when the server gives up waiting for an acknowledgement (half as
    // long again), and the ticks after go in more datagrams
    m->lossy.lose = 2;
    const uint32_t stalled = frames_run(m, 40);
    TIDE_CHECK(stalled > steady + 1u);
    TIDE_CHECK(stalled <= steady + 9u);
    TIDE_CHECK(frames_lossy_pieces(&m->lossy) >= 3 && frames_lossy_pieces(&m->lossy) <= 5);
    TIDE_CHECK(frames_run(m, 40) <= steady);
    // ...so two in a row lost again stall nobody
    m->lossy.lose = 2;
    TIDE_CHECK(frames_run(m, 40) <= steady + 2u);
    TIDE_CHECK(tide_client_status_of(m->client).resyncs == 0);
    tide_client_destroy(m->client);
    tide_server_destroy(m->server);
    tide_loopback_destroy(m->net);
    free(m);
}

// A client's inputs: the newest, as far back as one could still be in time for its tick by the server's word
// of how long they wait (session.c, send_client). Its CLIENTs are read as they go.
typedef struct inputs_seen {
    tide_transport inner;
    uint32_t most;  // The most inputs a CLIENT carried since this was cleared...
    uint32_t last;  // ...and the last one's
    uint32_t count; // CLIENTs that went
} inputs_seen;

static void inputs_seen_send(void *self, tide_address to, const void *data, uint32_t size)
{
    inputs_seen *seen = self;
    tide_reader r = {data, size, 3u, false};
    if (size > 3 && tide_read_u8(&r) == 5) { // CLIENT
        (void)tide_read_u64(&r); (void)tide_read_u32(&r);
        (void)tide_read_u16(&r); (void)tide_read_u16(&r);
        const uint8_t flags = tide_read_u8(&r);
        if (flags == 0) { // Neither a world nor a repair asked for: the same fields every time
            const uint32_t chunks = tide_read_varint(&r);
            if (chunks) (void)tide_read_varint(&r); // It has all of the last thing it was sent
            (void)tide_read_varint(&r);   // The ticks it has
            (void)tide_read_varint64(&r);
            (void)tide_read_u8(&r);       // SERVERs read
            (void)tide_read_varint(&r);   // No download
            (void)tide_read_varint(&r);   // Its first input, from the ticks it has
            const uint8_t inputs = tide_read_u8(&r);
            if (!r.failed) {
                seen->last = inputs;
                if (inputs > seen->most) seen->most = inputs;
                seen->count++;
            }
        }
    }
    seen->inner.send(seen->inner.self, to, data, size);
}

static uint32_t inputs_seen_receive(void *self, tide_address *from, void *data, uint32_t capacity)
{
    inputs_seen *seen = self;
    return seen->inner.receive(seen->inner.self, from, data, capacity);
}

static void inputs_sample(void *user, const uint32_t tick, void *input)
{
    (void)user;
    *(uint8_t *)input = (uint8_t)(tick % 200u); // Another every tick, as a hand on a mouse
}

TIDE_TEST(session_inputs_go_only_while_they_can_be_in_time)
{
    frames_match *m = calloc(1, sizeof *m);
    inputs_seen *seen = calloc(1, sizeof *seen);
    m->net = tide_loopback_create(78);
    tide_loopback_set_conditions(m->net, (tide_net_conditions){.latency = 0.05});
    m->lossy.inner = tide_loopback_endpoint(m->net, 1);
    seen->inner = tide_loopback_endpoint(m->net, 2);
    m->server = tide_server_create(&(tide_server_desc){.game = &packet_game, .wait_for_first = true,
        .transports = {{.self = &m->lossy, .send = frames_lossy_send, .receive = frames_lossy_receive}}}, 0);
    m->client = tide_client_create(&(tide_client_desc){.game = &packet_game, .sample = inputs_sample,
        .transport = {.self = seen, .send = inputs_seen_send, .receive = inputs_seen_receive},
        .server = tide_loopback_address(1), .lead = 2}, 0);
    TIDE_REQUIRE(m->server && m->client);
    frames_run(m, 240);
    TIDE_REQUIRE(tide_client_status_of(m->client).state == TIDE_SESSION_CONNECTED);
    // Six ticks' round trip, and inputs two ticks early: nine or ten inputs were unacknowledged at any time,
    // and each CLIENT had them all. It has the newest six: two ticks' wait, the next, and three to spare
    seen->most = seen->count = 0;
    frames_run(m, 120);
    TIDE_CHECK(seen->count >= 100);
    const uint32_t waits = (uint32_t)(tide_client_status_of(m->client).early_ms * 0.06f + 0.5f); // Ticks its inputs wait
    TIDE_CHECK(waits >= 1 && waits <= 4);
    TIDE_CHECK(seen->most >= waits + 4u && seen->most <= waits + 5u); // Those, the one being sampled, and three
    const uint32_t steady = seen->most;
    const uint32_t late = tide_client_status_of(m->client).late_ticks;
    // The server's word stops coming for half a second (every SERVER lost): no more inputs for it, where each
    // CLIENT used to grow by one. And none of the thirty ticks' inputs was late for that
    m->lossy.lose = 30;
    seen->most = 0;
    frames_run(m, 30);
    TIDE_CHECK(seen->most <= steady + 1u);
    frames_run(m, 120);
    TIDE_CHECK(m->lossy.lose == 0);
    TIDE_CHECK(tide_client_status_of(m->client).late_ticks == late);
    TIDE_CHECK(((const packet_world *)tide_server_world(m->server))->input == (uint8_t)((tide_server_tick(m->server) - 1u) % 200u));
    TIDE_CHECK(tide_client_status_of(m->client).resyncs == 0);
    tide_client_destroy(m->client);
    tide_server_destroy(m->server);
    tide_loopback_destroy(m->net);
    free(seen);
    free(m);
}
