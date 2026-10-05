// Sessions over real UDP sockets on this machine: a server and two players,
// with a game written by hand in C (tide/session.h only needs its functions).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/platform.h"
#include "tide/session.h"
#include "tide/time.h"
#include "tide_test.h"

typedef struct world {
    uint32_t tick;
    uint32_t joined;
    int32_t inputs[TIDE_PLAYERS_DEFAULT + 1];
    int32_t sum; // Every input, added up every tick
} world;

static void start(void *w, const float dt, const void *s, const uint32_t seed)
{
    (void)dt;
    (void)s;
    (void)seed;
    memset(w, 0, sizeof(world));
}

static void copy(void *to, const void *from)
{
    memcpy(to, from, sizeof(world));
}

static uint64_t hash(const void *w)
{
    return tide_hash(w, sizeof(world));
}

static void tick(void *w, const tide_jobs *jobs)
{
    (void)jobs;
    world *x = w;
    for (uint32_t i = 0; i <= TIDE_PLAYERS_DEFAULT; i++) x->sum += x->inputs[i];
    x->tick++;
}

static void joined(void *w, const tide_player_id player)
{
    (void)player;
    ((world *)w)->joined++;
}

static void left(void *w, const tide_player_id player)
{
    (void)w;
    (void)player;
}

static void set_input(void *w, const tide_player_id player, const void *input)
{
    const int32_t index = tide_player_index(player);
    if (index >= 0) memcpy(&((world *)w)->inputs[index], input, 4);
}

static void set_server_input(void *w, const void *input)
{
    memcpy(&((world *)w)->inputs[TIDE_PLAYERS_DEFAULT], input, 4);
}

static uint32_t write_input(const void *input, uint8_t *out, const uint32_t capacity)
{
    if (capacity < 4) return 0;
    memcpy(out, input, 4);
    return 4;
}

static bool read_input(const uint8_t *data, const uint32_t size, void *input)
{
    if (size < 4) return false;
    memcpy(input, data, 4);
    return true;
}

// The world as bytes, to send it: plain data, all of it, whatever the
// receiver has.
static uint8_t *pack(const void *w, const void *base, const uint8_t *need, const uint32_t need_size, uint32_t *size)
{
    (void)base;
    (void)need;
    (void)need_size;
    uint8_t *out = malloc(sizeof(world));
    if (!out) abort();
    memcpy(out, w, sizeof(world));
    *size = sizeof(world);
    return out;
}

static bool unpack(void *w, const void *base, const uint8_t *data, const uint32_t size)
{
    (void)base;
    if (size != sizeof(world)) return false;
    memcpy(w, data, size);
    return true;
}

static const tide_game game = {
    .hash = 42,
    .world_size = sizeof(world),
    .input_size = 4,
    .max_input_bytes = 4,
    .start = start,
    .tick = tick,
    .copy_world = copy,
    .hash_world = hash,
    .pack_delta = pack,
    .unpack_delta = unpack,
    .player_joined = joined,
    .player_left = left,
    .set_input = set_input,
    .set_server_input = set_server_input,
    .write_input = write_input,
    .read_input = read_input,
};

static void sample(void *user, const uint32_t t, void *input)
{
    const int32_t value = (int32_t)(t / 10u) * *(const int32_t *)user;
    memcpy(input, &value, 4);
}

typedef struct receiver {
    tide_transport transport;
    bool paused;
} receiver;

static void send(void *self, const tide_address to, const void *data, const uint32_t size)
{
    const receiver *r = self;
    r->transport.send(r->transport.self, to, data, size);
}

static uint32_t receive(void *self, tide_address *from, void *data, const uint32_t capacity)
{
    const receiver *r = self;
    return r->paused ? 0u : r->transport.receive(r->transport.self, from, data, capacity);
}

static void close_receiver(void *self)
{
    const receiver *r = self;
    r->transport.close(r->transport.self);
}

static bool drain(tide_server *server, tide_client *const clients[2], const double now, const uint32_t through)
{
    const uint64_t began = tide_time_now_ns();
    for (;;) {
        tide_server_update(server, now);
        bool complete = true;
        for (int i = 0; i < 2; i++) {
            tide_client_update(clients[i], now);
            const tide_client_status s = tide_client_status_of(clients[i]);
            complete &= s.state == TIDE_SESSION_CONNECTED && s.verified_tick >= through;
        }
        if (complete) return true;
        if (tide_time_now_ns() - began >= 5000000000ull) return false;
        tide_yield();
    }
}

static void players_play_together(const bool delayed)
{
    // A free port: the first that opens
    tide_transport network;
    uint16_t port = 47000;
    while (!tide_platform_udp_open_local(port, &network) && port < 47100) port++;
    TIDE_REQUIRE(port < 47100);
    double now = 0.0;
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &game, .tick_rate = 60, .transports = {network}}, now);
    TIDE_REQUIRE(server != NULL);

    tide_address address;
    char text[32];
    snprintf(text, sizeof text, "localhost:%u", (unsigned)port);
    TIDE_REQUIRE(tide_platform_resolve(text, 1, &address));
    TIDE_CHECK(address.host == 0x7F000001u && address.port == port);

    int32_t factors[2] = {1, 3};
    tide_client *clients[2];
    receiver receivers[2] = {0};
    for (int i = 0; i < 2; i++) {
        TIDE_REQUIRE(tide_platform_udp_open_local(0, &receivers[i].transport));
        const tide_transport t = {.self = &receivers[i], .send = send, .receive = receive, .close = close_receiver};
        clients[i] = tide_client_create(&(tide_client_desc){.game = &game, .transport = t, .server = address,
                                                            .sample = sample, .user = &factors[i], .lead = 2},
                                        now);
        TIDE_REQUIRE(clients[i] != NULL);
    }
    for (int frame = 0; frame < 240; frame++) {
        now += 1.0 / 60.0;
        for (int i = 0; i < 2; i++) {
            receivers[i].paused = delayed && frame >= 224;
            tide_client_update(clients[i], now);
        }
        tide_server_update(server, now);
        // Don't let simulated seconds outrun delivery by the real sockets:
        // finish this tick before advancing the clock, except while testing
        // the deliberately withheld final receives.
        if (!receivers[0].paused) {
            const uint32_t through = tide_server_tick(server);
            const bool ready = drain(server, clients, now, through);
            TIDE_CHECK(ready);
            TIDE_CHECK(tide_server_tick(server) == through);
            if (!ready) break;
        }
    }
    const uint32_t through = tide_server_tick(server);
    for (int i = 0; i < 2; i++) {
        if (delayed) TIDE_CHECK(tide_client_status_of(clients[i]).verified_tick + 10u <= through);
        receivers[i].paused = false;
    }
    // The simulation's four seconds can run before the kernel delivers its
    // datagrams. Finish receiving those ticks without advancing the match.
    TIDE_CHECK(drain(server, clients, now, through));
    TIDE_CHECK(tide_server_tick(server) == through);
    const world *w = tide_server_world(server);
    TIDE_CHECK(w->joined == 2);
    TIDE_CHECK(w->sum != 0);
    for (int i = 0; i < 2; i++) {
        const tide_client_status s = tide_client_status_of(clients[i]);
        TIDE_CHECK(s.state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(s.resyncs == 0);
        TIDE_CHECK(s.verified_tick + 10u > tide_server_tick(server));
        TIDE_CHECK(s.verified_tick == through);
        tide_client_destroy(clients[i]);
    }
    tide_server_destroy(server);
}

TIDE_TEST(udp_players_play_together)
{
    players_play_together(false);
}

// A ping is the network's round trip: the sockets say when each datagram
// arrived, so the wait for the frame that reads it is no part of it. Frames
// here are 60 ms apart, in real time, on one machine: an answer takes a frame
// to be read, and the network next to nothing. The least of the pings is
// what's checked, as a machine busy with other things can note one late.
TIDE_TEST(udp_a_ping_counts_no_wait_for_a_frame)
{
    tide_transport network;
    uint16_t port = 47200;
    while (!tide_platform_udp_open_local(port, &network) && port < 47300) port++;
    TIDE_REQUIRE(port < 47300);
    TIDE_REQUIRE(network.clock != NULL && network.arrived != NULL);
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &game, .tick_rate = 60, .transports = {network}}, 0.0);
    TIDE_REQUIRE(server != NULL);
    tide_transport joining;
    TIDE_REQUIRE(tide_platform_udp_open_local(0, &joining));
    int32_t factor = 1;
    tide_client *client = tide_client_create(&(tide_client_desc){.game = &game, .transport = joining,
                                                                 .server = {TIDE_ADDRESS_IPV4, 0x7F000001u, port},
                                                                 .sample = sample, .user = &factor, .lead = 2},
                                             0.0);
    TIDE_REQUIRE(client != NULL);

    const uint64_t began = tide_time_now_ns();
    const uint64_t frame_ns = 60000000ull;
    uint32_t least = UINT32_MAX;
    uint32_t answer = 0;
    int measured = 0;
    for (uint64_t frame = 1; frame <= 200 && measured < 20; frame++) {
        while (tide_time_now_ns() - began < frame * frame_ns) tide_yield();
        const double now = (double)(tide_time_now_ns() - began) * 1e-9;
        tide_server_update(server, now);
        tide_client_update(client, now);
        const tide_client_status s = tide_client_status_of(client);
        if (s.state != TIDE_SESSION_CONNECTED || frame < 8) continue;
        if (s.ping_ms < least) least = s.ping_ms;
        answer = s.answer_ms;
        measured++;
    }
    TIDE_CHECK(measured == 20);
    TIDE_CHECK(answer >= 50u); // A frame, to read it
    TIDE_CHECK(least < 30u);   // ...of which the network took next to none
    tide_client_destroy(client);
    tide_server_destroy(server);
}

TIDE_TEST(udp_players_play_together_after_delayed_receive)
{
    players_play_together(true);
}

TIDE_TEST(udp_addresses)
{
    tide_address a;
    TIDE_CHECK(tide_platform_resolve("192.168.1.5", 7777, &a) && a.host == 0xC0A80105u && a.port == 7777);
    TIDE_CHECK(tide_platform_resolve("10.0.0.1:1234", 7777, &a) && a.host == 0x0A000001u && a.port == 1234);
    TIDE_CHECK(!tide_platform_resolve("10.0.0.1:99999", 7777, &a));
    TIDE_CHECK(!tide_platform_resolve("", 7777, &a));
}
