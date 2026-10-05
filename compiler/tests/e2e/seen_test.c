#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

// How late other players see what one does: from the frame a player's key is
// read to the frame another player's view shows it. A server and two players
// on a loopback network, in made-up time, frames a 60th of a second.
//
// What's between the two is the network, and what the engine adds: the wait
// for a tick to sample the key, how early a client's inputs are sent to be
// sure to be in time (the server holds each until its tick), the server's own
// frame, and the watching player's.

typedef struct player {
    tide_client *client;
    bool key;
} player;

static void sample(void *user, const uint32_t tick, void *input)
{
    (void)tick;
    *(Pad *)input = (Pad){.held = ((const player *)user)->key};
}

// What a view shows of the first player's key: its worlds' values, blended as views blend.
static float shown(const tide_view_worlds v)
{
    if (!v.current) return 0.0f;
    const float now = ((const tide_world *)v.current)->Meter.first_x;
    if (!v.previous) return now;
    const float before = ((const tide_world *)v.previous)->Meter.first_x;
    return before + (now - before) * v.alpha;
}

#define FRAME (1.0 / 60.0)
#define SETTLE 300 // Frames for both to be in the match, and their clocks to settle
#define PRESSES 40 // Key presses measured, each at another moment of a tick

typedef struct seen {
    double frames;      // From the frame the key is read to the frame the other player's view shows any of it, on the whole
    int most;           // ...and at the most
    double early;       // How early the pressing player's inputs reach the server, in ticks, on the whole
    uint32_t resyncs;
    uint32_t late;      // Ticks the server ran without the pressing player's input, once its clock had settled
} seen;

// A match of `rate` ticks a second on a network `latency` seconds each way.
static seen measure(const uint32_t rate, const double latency)
{
    tide_loopback *net = tide_loopback_create(5);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = latency});
    double now = 0.0;
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &tide_game_api, .tick_rate = rate,
                                                                  .transports = {tide_loopback_endpoint(net, 1)}},
                                             now);
    player players[2] = {{0}, {0}};
    int frame = 0;
    seen result = {0};
    int pressed_at = -1;
    int presses = 0;
    double early = 0.0;
    int early_count = 0;
    uint32_t late_before = 0;
    // The watching player updates before the pressing one, then the server:
    // each reads what the others sent the frame before, as machines apart do
    for (; presses < PRESSES && frame < 20000; frame++) {
        now += FRAME;
        tide_loopback_set_time(net, now);
        if (frame == 0 || frame == 60) { // The one who presses joins first
            const int i = frame == 0 ? 0 : 1;
            players[i].client = tide_client_create(&(tide_client_desc){.game = &tide_game_api,
                                                                       .transport = tide_loopback_endpoint(net, (uint32_t)i + 2u),
                                                                       .server = tide_loopback_address(1),
                                                                       .sample = sample, .user = &players[i], .lead = 1},
                                                   now);
        }
        // A press every 23 frames once settled, held for 10: at another moment of a tick each time
        if (frame >= SETTLE && (frame - SETTLE) % 23 == 0) {
            players[0].key = true;
            pressed_at = frame;
        }
        if (frame >= SETTLE && (frame - SETTLE) % 23 == 10) players[0].key = false;
        if (players[1].client) tide_client_update(players[1].client, now);
        tide_client_update(players[0].client, now);
        tide_server_update(server, now);
        if (frame == SETTLE) late_before = tide_client_status_of(players[0].client).late_ticks;
        if (frame >= SETTLE) {
            early += (double)tide_client_status_of(players[0].client).early_ticks;
            early_count++;
        }
        if (pressed_at >= 0 && players[1].client && shown(tide_client_view(players[1].client)) > 0.0f) {
            const int took = frame - pressed_at;
            result.frames += took;
            if (took > result.most) result.most = took;
            presses++;
            pressed_at = -1;
        }
    }
    result.frames /= presses ? presses : 1;
    result.early = early / (early_count ? early_count : 1);
    result.resyncs = tide_client_status_of(players[0].client).resyncs + tide_client_status_of(players[1].client).resyncs;
    result.late = tide_client_status_of(players[0].client).late_ticks - late_before;
    TIDE_CHECK(presses == PRESSES);
    tide_client_destroy(players[0].client);
    tide_client_destroy(players[1].client);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
    return result;
}

// Frames, on the whole, at each tick rate and network. With inputs kept 2 to 6
// ticks early they were 3 and 6 at 60 ticks a second, 6.5 and 9.5 at 30, 8 and
// 12 at 20. These are what they may be at the most, now that inputs are as
// early as they have to be: a change that makes players see each other later
// fails here, and one that makes it sooner lowers them.
TIDE_TEST(seen_how_late_other_players_see_you)
{
    const uint32_t rates[] = {60, 30, 20};
    const double latencies[] = {0.0, 0.03};
    const double most[3][2] = {{2.0, 5.0}, {4.5, 7.5}, {5.1, 9.1}};
    for (int r = 0; r < 3; r++) {
        for (int l = 0; l < 2; l++) {
            const seen s = measure(rates[r], latencies[l]);
            TIDE_CHECK(s.resyncs == 0);
            TIDE_CHECK(s.late == 0); // As early as they have to be: none too late
            TIDE_CHECK(s.frames <= most[r][l] + 0.01);
            TIDE_CHECK(s.early >= 1.0 && s.early <= 1.6); // A tick at the least, and little more
        }
    }
}

// A network that delays some datagrams more than others: the inputs go as
// early as its worst moments need, so none is late, and no earlier.
TIDE_TEST(seen_inputs_are_as_early_as_an_uneven_network_needs)
{
    tide_loopback *net = tide_loopback_create(11);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.02, .jitter = 0.04});
    double now = 0.0;
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &tide_game_api, .tick_rate = 60,
                                                                  .transports = {tide_loopback_endpoint(net, 1)}},
                                             now);
    player one = {0};
    one.client = tide_client_create(&(tide_client_desc){.game = &tide_game_api, .transport = tide_loopback_endpoint(net, 2),
                                                        .server = tide_loopback_address(1), .sample = sample, .user = &one,
                                                        .lead = 1},
                                    now);
    uint32_t late_before = 0;
    int32_t least = 1000, most = -1000;
    for (int frame = 0; frame < 3600; frame++) {
        now += FRAME;
        tide_loopback_set_time(net, now);
        one.key = frame / 7 % 2 == 0;
        tide_client_update(one.client, now);
        tide_server_update(server, now);
        const tide_client_status status = tide_client_status_of(one.client);
        if (frame == 600) late_before = status.late_ticks;
        if (frame < 600) continue; // Its first ten seconds, as it finds out how uneven the network is
        if (status.early_ticks < least) least = status.early_ticks;
        if (status.early_ticks > most) most = status.early_ticks;
    }
    const tide_client_status status = tide_client_status_of(one.client);
    TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED && status.resyncs == 0);
    TIDE_CHECK(status.late_ticks - late_before <= 2u); // Up to 40 ms more for a datagram is 3 ticks: it went that early
    TIDE_CHECK(least >= 0 && most <= 6);
    tide_client_destroy(one.client);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}
