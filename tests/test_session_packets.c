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
    uint8_t data[TIDE_NET_MTU];
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
    .player_joined = packet_join, .player_left = packet_leave,
    .read_input = packet_read, .read_input_delta = packet_read_delta, .write_input = packet_write,
    .world_input = packet_get, .set_input = packet_set,
    .check_command = packet_check, .command_sequence = packet_sequence, .command = packet_command,
};

static tide_writer packet_begin(packet_peer *p, uint8_t type)
{
    tide_writer w = {p->data, sizeof p->data, 0, false, false};
    tide_write_u16(&w, 0x5449u);
    tide_write_u8(&w, 18u);
    tide_write_u8(&w, type);
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
    tide_write_u64(&w, 0);
    tide_write_u8(&w, flags);
    tide_write_bytes(&w, zero, sizeof zero);
    return w;
}

static packet_peer packet_setup(void)
{
    packet_peer p = {0};
    p.net = tide_loopback_create(1234);
    p.server = tide_server_create(&(tide_server_desc){.game = &packet_game, .wait_for_first = true,
        .transports = {tide_loopback_endpoint(p.net, 1)}}, 0);
    p.transport = tide_loopback_endpoint(p.net, 2);
    tide_writer w = packet_hello(&p, 123u, 0);
    packet_send(&p, &w);
    return p;
}

static void packet_finish(packet_peer *p)
{
    tide_server_destroy(p->server);
    tide_loopback_destroy(p->net);
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
    tide_write_u32(&w, 123u);
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
