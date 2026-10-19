#include <stdlib.h>
#include <string.h>

#include "tide/session.h"
#include "tide_test.h"

// A small plain-data game isolates session delivery from generated code.
// The order includes the execution tick, so replay at another tick differs.
#define COMMAND_PLAYERS 4u
typedef struct command_world {
    uint32_t ticks;
    uint32_t joined;
    uint32_t left;
    uint32_t sequence[COMMAND_PLAYERS];
    uint32_t count[COMMAND_PLAYERS];
    uint32_t total[COMMAND_PLAYERS];
    uint64_t order;
} command_world;

static void command_start(void *world, float dt, const void *start, uint32_t seed)
{
    (void)dt;
    (void)start;
    (void)seed;
    memset(world, 0, sizeof(command_world));
}

static void command_tick(void *world, const tide_jobs *jobs)
{
    (void)jobs;
    ((command_world *)world)->ticks++;
}

static void command_copy(void *to, const void *from)
{
    memcpy(to, from, sizeof(command_world));
}

static bool command_migrate(void *user, const void *from, void *to);

static uint64_t command_hash(const void *world)
{
    const uint8_t *data = world;
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < sizeof(command_world); i++) hash = (hash ^ data[i]) * 1099511628211ull;
    return hash;
}

static uint32_t command_pack(const void *world, uint8_t *out, uint32_t capacity)
{
    if (out && capacity >= sizeof(command_world)) memcpy(out, world, sizeof(command_world));
    return (uint32_t)sizeof(command_world);
}

static bool command_unpack(void *world, const uint8_t *data, uint32_t size)
{
    if (size != sizeof(command_world)) return false;
    memcpy(world, data, size);
    return true;
}

static uint8_t *command_pack_delta(const void *world, const void *base, const uint8_t *need, uint32_t need_size,
                                  bool assets, uint32_t *size)
{
    (void)base;
    (void)need;
    (void)need_size;
    (void)assets;
    *size = (uint32_t)sizeof(command_world);
    uint8_t *data = malloc(*size);
    if (data) memcpy(data, world, *size);
    return data;
}

static bool command_unpack_delta(void *world, const void *base, const uint8_t *data, uint32_t size)
{
    (void)base;
    return command_unpack(world, data, size);
}

static void command_joined(void *world, tide_player_id player)
{
    (void)player;
    ((command_world *)world)->joined++;
}

static void command_left(void *world, tide_player_id player)
{
    (void)player;
    ((command_world *)world)->left++;
}

// Every command of this game is in the default channel
static bool command_check(const uint8_t *data, uint32_t size, uint32_t *channel)
{
    if (!data || size < 2u || size > TIDE_COMMAND_BYTES || data[0] != 0xA5u) return false;
    for (uint32_t i = 2; i < size; i++) {
        if (data[i] != 0xCCu) return false;
    }
    if (channel) *channel = 0;
    return true;
}

static uint32_t command_sequence(const void *world, tide_player_id player, uint32_t channel)
{
    const int32_t index = tide_player_index(player);
    return channel == 0 && index >= 0 && (uint32_t)index < COMMAND_PLAYERS ? ((const command_world *)world)->sequence[index] : 0u;
}

static bool command_receive(void *world, tide_player_id sender, uint32_t channel, uint32_t sequence, const uint8_t *data,
                            uint32_t size)
{
    command_world *w = world;
    const int32_t player = tide_player_index(sender);
    if (channel != 0 || player < 0 || (uint32_t)player >= COMMAND_PLAYERS || !command_check(data, size, NULL)
        || w->sequence[player] == UINT32_MAX || sequence != w->sequence[player] + 1u) return false;
    w->sequence[player] = sequence;
    if (data[1] == 0) return true; // A game rejection still consumes this request
    w->count[player]++;
    w->total[player] += data[1];
    w->order = w->order * 65599u + sender.id * 257u + data[1] + (uint64_t)w->ticks * 17u;
    return true;
}

static const tide_game command_game = {
    .hash = 0xC04D,
    .world_size = sizeof(command_world),
    .start = command_start,
    .tick = command_tick,
    .copy_world = command_copy,
    .hash_world = command_hash,
    .pack_world = command_pack,
    .unpack_world = command_unpack,
    .pack_delta = command_pack_delta,
    .unpack_delta = command_unpack_delta,
    .player_joined = command_joined,
    .player_left = command_left,
    .check_command = command_check,
    .command_sequence = command_sequence,
    .command = command_receive,
    .max_players = COMMAND_PLAYERS,
    .host_migration = true,
};

static tide_loopback *command_net;
static tide_server *command_server;
static tide_client *command_clients[2];
static double command_now;
static tide_transport command_wrapped;
static uint32_t command_nonce;

static void command_capture_send(void *user, tide_address to, const void *data, uint32_t size)
{
    (void)user;
    tide_reader r = {data, size, 0, false};
    (void)tide_read_u16(&r);
    (void)tide_read_u8(&r);
    if (tide_read_u8(&r) == 1u) { // HELLO carries this connection attempt's nonce
        (void)tide_read_u64(&r);
        (void)tide_read_bytes(&r, 32);
        command_nonce = tide_read_u32(&r);
    }
    command_wrapped.send(command_wrapped.self, to, data, size);
}

static uint32_t command_capture_receive(void *user, tide_address *from, void *data, uint32_t capacity)
{
    (void)user;
    return command_wrapped.receive(command_wrapped.self, from, data, capacity);
}

static void command_setup(tide_net_conditions conditions)
{
    command_now = 0;
    command_net = tide_loopback_create(75213);
    tide_loopback_set_conditions(command_net, conditions);
    command_server = tide_server_create(&(tide_server_desc){.game = &command_game,
        .transports = {tide_loopback_endpoint(command_net, 1)}}, command_now);
    for (uint32_t i = 0; i < 2u; i++) {
        tide_transport transport = tide_loopback_endpoint(command_net, i + 2u);
        if (i == 0) {
            command_wrapped = transport;
            transport = (tide_transport){.send = command_capture_send, .receive = command_capture_receive};
        }
        command_clients[i] = tide_client_create(&(tide_client_desc){.game = &command_game,
            .transport = transport, .server = tide_loopback_address(1), .lead = 2}, command_now);
    }
}

static void command_run(double seconds)
{
    const double until = command_now + seconds;
    while (command_now < until) {
        command_now += 0.01;
        tide_loopback_set_time(command_net, command_now);
        for (uint32_t i = 0; i < 2u; i++) tide_client_update(command_clients[i], command_now);
        tide_server_update(command_server, command_now);
    }
}

static void command_finish(void)
{
    for (uint32_t i = 0; i < 2u; i++) tide_client_destroy(command_clients[i]);
    tide_server_destroy(command_server);
    tide_loopback_destroy(command_net);
}

TIDE_TEST(commands_survive_loss_and_reordering_without_input)
{
    command_setup((tide_net_conditions){.latency = 0.08, .jitter = 0.06, .loss = 0.3});
    command_run(2.0);
    for (uint32_t p = 0; p < 2u; p++) {
        TIDE_REQUIRE(tide_client_status_of(command_clients[p]).state == TIDE_SESSION_CONNECTED);
        for (uint8_t value = 1; value <= 32u; value++) {
            uint8_t data[] = {0xA5, value};
            TIDE_CHECK(tide_client_send_command(command_clients[p], data, sizeof data));
            data[1] = 0; // The pending queue owns a copy
        }
        const uint8_t full[] = {0xA5, 99};
        TIDE_CHECK(!tide_client_send_command(command_clients[p], full, sizeof full));
    }
    command_run(5.0);
    const command_world *server = tide_server_world(command_server);
    for (uint32_t p = 0; p < 2u; p++) {
        const tide_client_status status = tide_client_status_of(command_clients[p]);
        const uint32_t slot = status.player.id - 1u;
        TIDE_CHECK(server->sequence[slot] == 32u);
        TIDE_CHECK(server->count[slot] == 32u);
        TIDE_CHECK(server->total[slot] == 528u);
        const command_world *verified = tide_client_verified_world(command_clients[p]);
        TIDE_REQUIRE(verified);
        TIDE_CHECK(verified->order == server->order);
        TIDE_CHECK(status.resyncs == 0);
        const uint8_t again[] = {0xA5, 1};
        TIDE_CHECK(tide_client_send_command(command_clients[p], again, sizeof again));
    }
    command_run(2.0);
    for (uint32_t p = 0; p < 2u; p++) {
        const uint32_t slot = tide_client_status_of(command_clients[p]).player.id - 1u;
        TIDE_CHECK(((const command_world *)tide_server_world(command_server))->count[slot] == 33u);
    }
    command_finish();
}

TIDE_TEST(commands_are_bounded_and_rejections_consume_the_sequence)
{
    command_setup((tide_net_conditions){0});
    const uint8_t good[] = {0xA5, 7};
    TIDE_CHECK(!tide_client_send_command(command_clients[0], good, sizeof good)); // Connecting
    command_run(0.5);
    uint8_t data[TIDE_COMMAND_BYTES + 1u];
    memset(data, 0xCC, sizeof data);
    data[0] = 0xA5;
    data[1] = 3;
    TIDE_CHECK(!tide_client_send_command(command_clients[0], data, sizeof data));
    TIDE_CHECK(!tide_client_send_command(command_clients[0], NULL, 2));
    TIDE_CHECK(!tide_client_send_command(command_clients[0], data, 0));
    data[0] = 0;
    TIDE_CHECK(!tide_client_send_command(command_clients[0], data, 2));
    data[0] = 0xA5;
    TIDE_CHECK(tide_client_send_command(command_clients[0], data, TIDE_COMMAND_BYTES));
    const uint8_t reject[] = {0xA5, 0};
    TIDE_CHECK(tide_client_send_command(command_clients[0], reject, sizeof reject));
    TIDE_CHECK(tide_client_send_command(command_clients[0], good, sizeof good));
    command_run(1.0);
    const uint32_t slot = tide_client_status_of(command_clients[0]).player.id - 1u;
    const command_world *world = tide_server_world(command_server);
    TIDE_CHECK(world->sequence[slot] == 3u);
    TIDE_CHECK(world->count[slot] == 2u);
    TIDE_CHECK(world->total[slot] == 10u);
    TIDE_CHECK(tide_client_status_of(command_clients[0]).resyncs == 0);
    command_finish();
}

TIDE_TEST(commands_pending_before_a_new_match_do_not_cross_into_it)
{
    tide_session *s = tide_session_create(&(tide_session_desc){.game = &command_game});
    tide_session_start(s, NULL, 0);
    for (int i = 0; i < 50; i++) tide_session_update(s, (double)i / 60.0);
    const uint8_t data[] = {0xA5, 4};
    TIDE_REQUIRE(tide_session_send_command(s, data, sizeof data));
    tide_session_start(s, NULL, 1.0);
    for (int i = 0; i < 50; i++) tide_session_update(s, 1.0 + (double)i / 60.0);
    const command_world *world = tide_session_server_world(s);
    TIDE_REQUIRE(world);
    TIDE_CHECK(world->count[0] == 0 && world->sequence[0] == 0);
    tide_session_leave(s);
    TIDE_CHECK(!tide_session_send_command(s, data, sizeof data));
    tide_session_destroy(s);
}

static void command_raw(uint32_t nonce, uint32_t sequence, const uint8_t *data, uint32_t bytes, bool extra)
{
    uint8_t packet[TIDE_NET_MTU];
    tide_writer w = {packet, sizeof packet, 0, false, false};
    tide_write_u16(&w, 0x5449);
    tide_write_u8(&w, 15); // PROTOCOL
    tide_write_u8(&w, 9); // MSG_COMMAND: no sender supplied by the packet
    tide_write_u32(&w, nonce);
    tide_write_varint(&w, sequence);
    tide_write_varint(&w, bytes);
    if (data) tide_write_bytes(&w, data, bytes);
    if (extra) tide_write_u8(&w, 0);
    command_wrapped.send(command_wrapped.self, tide_loopback_address(1), packet, w.size);
}

TIDE_TEST(commands_reject_wrong_attempt_and_malformed_packets_then_deduplicate)
{
    command_setup((tide_net_conditions){0});
    command_run(0.5);
    TIDE_REQUIRE(command_nonce);
    const uint8_t bad[] = {0, 55};
    const uint8_t good[] = {0xA5, 5};
    command_raw(command_nonce ^ 1u, 1, good, sizeof good, false);
    command_raw(command_nonce, 1, bad, sizeof bad, false);
    command_raw(command_nonce, 1, NULL, TIDE_COMMAND_BYTES + 1u, false);
    command_raw(command_nonce, 1, good, sizeof good, true);
    command_raw(command_nonce, TIDE_COMMAND_PENDING + 1u, good, sizeof good, false);
    command_run(0.1);
    const uint32_t slot = tide_client_status_of(command_clients[0]).player.id - 1u;
    TIDE_CHECK(((const command_world *)tide_server_world(command_server))->sequence[slot] == 0);
    command_raw(command_nonce, 2, good, sizeof good, false); // Waits for the missing first one
    command_run(0.1);
    TIDE_CHECK(((const command_world *)tide_server_world(command_server))->count[slot] == 0);
    command_raw(command_nonce, 1, good, sizeof good, false);
    command_run(0.3);
    command_raw(command_nonce, 1, good, sizeof good, false);
    command_raw(command_nonce, 2, good, sizeof good, false);
    command_run(0.3);
    const command_world *world = tide_server_world(command_server);
    TIDE_CHECK(world->sequence[slot] == 2u);
    TIDE_CHECK(world->count[slot] == 2u && world->total[slot] == 10u);
    const uint32_t other = tide_client_status_of(command_clients[1]).player.id - 1u;
    TIDE_CHECK(world->sequence[other] == 0 && world->count[other] == 0);
    TIDE_CHECK(tide_client_status_of(command_clients[0]).resyncs == 0);
    command_finish();
}

typedef struct migration_transport {
    tide_transport inner;
    bool block_commands;
    bool block_frames;
} migration_transport;

static void migration_send(void *user, tide_address to, const void *data, uint32_t size)
{
    migration_transport *t = user;
    if (t->block_commands && size >= 4u && ((const uint8_t *)data)[3] == 9u) return;
    t->inner.send(t->inner.self, to, data, size);
}

static uint32_t migration_receive(void *user, tide_address *from, void *data, uint32_t capacity)
{
    migration_transport *t = user;
    uint32_t size;
    while ((size = t->inner.receive(t->inner.self, from, data, capacity)) > 0) {
        if (!t->block_frames || size < 4u || ((const uint8_t *)data)[3] != 6u) return size;
    }
    return 0;
}

static tide_loopback *migration_net;
static tide_session *migration_sessions[3];
static migration_transport migration_transports[2];
static double migration_now;

static void migration_run(double seconds)
{
    const double until = migration_now + seconds;
    while (migration_now < until) {
        migration_now += 0.01;
        tide_loopback_set_time(migration_net, migration_now);
        for (int i = 0; i < 3; i++) tide_session_update(migration_sessions[i], migration_now);
    }
}

static void migration_setup(void)
{
    migration_now = 0;
    migration_net = tide_loopback_create(367);
    memset(migration_transports, 0, sizeof migration_transports);
    for (int i = 0; i < 3; i++) migration_sessions[i] = tide_session_create(&(tide_session_desc){.game = &command_game});
    tide_session_start(migration_sessions[0], NULL, 0);
    tide_session_open(migration_sessions[0], tide_loopback_endpoint(migration_net, 1));
    tide_session_set_room(migration_sessions[0], "ABCDEF", "key");
    for (int i = 0; i < 2; i++) {
        migration_transports[i].inner = tide_loopback_endpoint(migration_net, (uint32_t)i + 2u);
        tide_session_join(migration_sessions[i + 1], (tide_transport){.self = &migration_transports[i],
            .send = migration_send, .receive = migration_receive}, tide_loopback_address(1), 0);
    }
    migration_run(2.0); // The host's handover record has reached both players
}

static void migration_finish(void)
{
    for (int i = 0; i < 3; i++) tide_session_destroy(migration_sessions[i]);
    tide_loopback_destroy(migration_net);
}

TIDE_TEST(commands_unconfirmed_survive_host_migration_and_applied_ones_are_not_repeated)
{
    migration_setup();
    for (int i = 0; i < 3; i++) {
        TIDE_REQUIRE(tide_session_status_of(migration_sessions[i]).client.state == TIDE_SESSION_CONNECTED);
    }
    const uint32_t first = tide_session_status_of(migration_sessions[1]).client.player.id - 1u;
    const uint32_t second = tide_session_status_of(migration_sessions[2]).client.player.id - 1u;
    migration_transports[0].block_frames = true; // Its request applies, but it never hears that tick
    migration_transports[1].block_commands = true; // The next host's request never reaches the old host
    const uint8_t a[] = {0xA5, 11};
    const uint8_t b[] = {0xA5, 22};
    TIDE_REQUIRE(tide_session_send_command(migration_sessions[1], a, sizeof a));
    TIDE_REQUIRE(tide_session_send_command(migration_sessions[2], b, sizeof b));
    migration_run(0.3);
    const command_world *old = tide_session_server_world(migration_sessions[0]);
    TIDE_REQUIRE(old);
    TIDE_CHECK(old->count[first] == 1u && old->count[second] == 0);
    TIDE_CHECK(((const command_world *)tide_session_view(migration_sessions[1]).verified)->sequence[first] == 0);
    TIDE_CHECK(((const command_world *)tide_session_view(migration_sessions[2]).verified)->sequence[first] == 1u);
    tide_session_leave(migration_sessions[0]);
    migration_run(0.1);
    char code[TIDE_ROOM_CODE_LENGTH + 1];
    char key[TIDE_ROOM_KEY_LENGTH + 1];
    TIDE_REQUIRE(tide_session_migrating(migration_sessions[1], code, key));
    TIDE_REQUIRE(tide_session_migrating(migration_sessions[2], code, key));
    tide_session_take_over(migration_sessions[2], tide_loopback_endpoint(migration_net, 4), migration_now);
    tide_session_join(migration_sessions[1], tide_loopback_endpoint(migration_net, 5), tide_loopback_address(4), migration_now);
    migration_run(1.0);
    const command_world *next = tide_session_server_world(migration_sessions[2]);
    TIDE_REQUIRE(next);
    TIDE_CHECK(next->count[first] == 1u && next->total[first] == 11u);
    TIDE_CHECK(next->count[second] == 1u && next->total[second] == 22u);
    TIDE_CHECK(next->sequence[first] == 1u && next->sequence[second] == 1u);
    TIDE_CHECK(tide_session_status_of(migration_sessions[1]).client.resyncs == 0);
    TIDE_CHECK(tide_session_status_of(migration_sessions[2]).client.resyncs == 0);

    // Even while between hosts, an incompatible reload must purge the stale
    // client's serialized intent. The plain-data layout stays the same here
    // so this checks only the command queue's schema lifetime.
    TIDE_REQUIRE(tide_session_send_command(migration_sessions[1], a, sizeof a));
    tide_session_leave(migration_sessions[2]);
    migration_run(0.1);
    TIDE_REQUIRE(tide_session_migrating(migration_sessions[1], code, key));
    tide_game changed = command_game;
    changed.hash++;
    TIDE_REQUIRE(tide_session_migrate(migration_sessions[1], &changed, command_migrate, NULL));
    tide_session_take_over(migration_sessions[1], tide_loopback_endpoint(migration_net, 6), migration_now);
    migration_run(0.5);
    next = tide_session_server_world(migration_sessions[1]);
    TIDE_REQUIRE(next);
    TIDE_CHECK(next->sequence[first] == 1u && next->count[first] == 1u && next->total[first] == 11u);
    migration_finish();
}

TIDE_TEST(commands_rebase_pending_intent_when_the_next_host_verified_less)
{
    migration_setup();
    const uint32_t sender = tide_session_status_of(migration_sessions[1]).client.player.id - 1u;
    migration_transports[1].block_frames = true; // The next host keeps an older verified world
    const uint8_t first[] = {0xA5, 3};
    const uint8_t pending[] = {0xA5, 7};
    TIDE_REQUIRE(tide_session_send_command(migration_sessions[1], first, sizeof first));
    migration_run(0.3);
    TIDE_CHECK(((const command_world *)tide_session_view(migration_sessions[1]).verified)->sequence[sender] == 1u);
    TIDE_CHECK(((const command_world *)tide_session_view(migration_sessions[2]).verified)->sequence[sender] == 0);
    migration_transports[0].block_commands = true;
    TIDE_REQUIRE(tide_session_send_command(migration_sessions[1], pending, sizeof pending));
    tide_session_leave(migration_sessions[0]);
    migration_run(0.1);
    char code[TIDE_ROOM_CODE_LENGTH + 1];
    char key[TIDE_ROOM_KEY_LENGTH + 1];
    TIDE_REQUIRE(tide_session_migrating(migration_sessions[1], code, key));
    TIDE_REQUIRE(tide_session_migrating(migration_sessions[2], code, key));
    tide_session_take_over(migration_sessions[2], tide_loopback_endpoint(migration_net, 4), migration_now);
    tide_session_join(migration_sessions[1], tide_loopback_endpoint(migration_net, 5), tide_loopback_address(4), migration_now);
    migration_run(1.0);
    const command_world *world = tide_session_server_world(migration_sessions[2]);
    TIDE_REQUIRE(world);
    TIDE_CHECK(world->sequence[sender] == 1u);
    TIDE_CHECK(world->count[sender] == 1u && world->total[sender] == 7u);
    migration_finish();
}

TIDE_TEST(commands_sequences_do_not_wrap)
{
    tide_session *s = tide_session_create(&(tide_session_desc){.game = &command_game});
    command_world world = {0};
    world.sequence[0] = UINT32_MAX;
    tide_player_set players = {{0}};
    tide_players_add(&players, 0);
    tide_session_start_from(s, &world, players, 0);
    for (int i = 0; i < 50; i++) tide_session_update(s, (double)i / 60.0);
    TIDE_REQUIRE(tide_session_status_of(s).client.state == TIDE_SESSION_CONNECTED);
    const uint8_t data[] = {0xA5, 9};
    TIDE_CHECK(!tide_session_send_command(s, data, sizeof data));
    TIDE_CHECK(((const command_world *)tide_session_server_world(s))->sequence[0] == UINT32_MAX);
    tide_session_destroy(s);
}

static bool command_migrate(void *user, const void *from, void *to)
{
    (void)user;
    command_copy(to, from);
    return true;
}

TIDE_TEST(commands_hot_reload_keeps_compatible_bytes_and_discards_another_layout)
{
    tide_game compatible = command_game;
    tide_game changed = command_game;
    changed.hash++;
    tide_session *s = tide_session_create(&(tide_session_desc){.game = &command_game});
    tide_session_start(s, NULL, 0);
    for (int i = 0; i < 50; i++) tide_session_update(s, (double)i / 60.0);
    const uint8_t keep[] = {0xA5, 2};
    const uint8_t discard[] = {0xA5, 7};
    TIDE_REQUIRE(tide_session_send_command(s, keep, sizeof keep));
    tide_session_set_game(s, &compatible);
    for (int i = 50; i < 80; i++) tide_session_update(s, (double)i / 60.0);
    TIDE_CHECK(((const command_world *)tide_session_server_world(s))->total[0] == 2u);
    TIDE_REQUIRE(tide_session_send_command(s, discard, sizeof discard));
    TIDE_REQUIRE(tide_session_migrate(s, &changed, command_migrate, NULL));
    for (int i = 80; i < 110; i++) tide_session_update(s, (double)i / 60.0);
    const command_world *world = tide_session_server_world(s);
    TIDE_CHECK(world->sequence[0] == 1u && world->count[0] == 1u && world->total[0] == 2u);
    TIDE_REQUIRE(tide_session_send_command(s, keep, sizeof keep));
    for (int i = 110; i < 140; i++) tide_session_update(s, (double)i / 60.0);
    world = tide_session_server_world(s);
    TIDE_CHECK(world->sequence[0] == 2u && world->count[0] == 2u && world->total[0] == 4u);
    tide_session_destroy(s);

    // A remote server can have dispatched the old bytes before this client
    // verified them. Purging those bytes must not reuse their request ID.
    migration_setup();
    const uint32_t sender = tide_session_status_of(migration_sessions[1]).client.player.id - 1u;
    migration_transports[0].block_frames = true;
    TIDE_REQUIRE(tide_session_send_command(migration_sessions[1], keep, sizeof keep));
    migration_run(0.3);
    TIDE_CHECK(((const command_world *)tide_session_server_world(migration_sessions[0]))->sequence[sender] == 1u);
    TIDE_CHECK(((const command_world *)tide_session_view(migration_sessions[1]).verified)->sequence[sender] == 0);
    TIDE_REQUIRE(tide_session_migrate(migration_sessions[0], &changed, command_migrate, NULL));
    TIDE_REQUIRE(tide_session_migrate(migration_sessions[1], &changed, command_migrate, NULL));
    TIDE_CHECK(!tide_session_send_command(migration_sessions[1], keep, sizeof keep));
    migration_transports[0].block_frames = false;
    migration_run(0.3);
    TIDE_REQUIRE(tide_session_send_command(migration_sessions[1], keep, sizeof keep));
    migration_run(0.3);
    world = tide_session_server_world(migration_sessions[0]);
    TIDE_CHECK(world->sequence[sender] == 2u && world->count[sender] == 2u && world->total[sender] == 4u);
    migration_finish();
}
