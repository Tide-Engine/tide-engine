#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

// handler_lanes_net.tide on a server and two clients a tenth of a second away, over a network that loses and
// delays datagrams. Each client predicts both players' strikes, guessing the other's, and runs its ticks again
// when a guess was wrong or late, beside the worlds of its last ticks. Its handlers put back only the lanes they
// changed: were one to lose a write or keep a page it shouldn't, a tick's hash wouldn't be the server's and the
// server would send the world again (resyncs), and what's left at the end wouldn't be the server's.

enum { POSTS = 40 };

static tide_loopback *net;
static tide_server *server;
static tide_client *clients[2];
static double now;
static uint32_t stop_at; // The tick the players let go at

// A strike every few ticks at a post that goes by the tick, another rhythm for each player, so a guess by a
// player's last input is wrong most of the times it matters
static void sample(void *user, const uint32_t tick, void *input)
{
    const uint32_t who = (uint32_t)(size_t)user;
    Controls *c = input;
    *c = (Controls){0};
    if (tick >= stop_at) return;
    c->move = tide_f2(who ? -1.0f : 1.0f, 0.0f);
    if (tick % (who ? 5u : 3u) == 0) c->strike = 1 + (int32_t)((tick * (who ? 7u : 11u)) % 97u);
}

static void run(const double until)
{
    bool odd = false;
    while (now < until) {
        now += (odd = !odd) ? 0.011 : 0.022;
        tide_loopback_set_time(net, now);
        for (int i = 0; i < 2; i++) tide_client_update(clients[i], now);
        tide_server_update(server, now);
    }
}

// Whether two worlds have the same posts and bodies, field by field
static bool same(const tide_world *a, const tide_world *b)
{
    if (!a || !b || a->arch0_Post.count != POSTS || b->arch0_Post.count != POSTS) return false;
    if (a->arch1_Owner_Body.count != 2 || b->arch1_Owner_Body.count != 2) return false;
    for (uint32_t row = 0; row < POSTS; row++) {
        const Post x = TIDE_AT(a, arch0_Post, Post, row), y = TIDE_AT(b, arch0_Post, Post, row);
        if (memcmp(&x, &y, sizeof x) != 0) return false;
    }
    for (uint32_t row = 0; row < 2; row++) {
        const Body x = TIDE_AT(a, arch1_Owner_Body, Body, row), y = TIDE_AT(b, arch1_Owner_Body, Body, row);
        if (memcmp(&x, &y, sizeof x) != 0) return false;
    }
    return true;
}

TIDE_TEST(handler_lanes_predicted_and_run_again_stay_the_servers)
{
    now = 0.0;
    stop_at = 600;
    net = tide_loopback_create(77);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.1, .jitter = 0.02, .loss = 0.03});
    const tide_server_desc host = {.game = &tide_game_api, .tick_rate = 60, .transports = {tide_loopback_endpoint(net, 1)}};
    server = tide_server_create(&host, now);
    for (uint32_t i = 0; i < 2; i++) {
        const tide_client_desc desc = {
            .game = &tide_game_api,
            .transport = tide_loopback_endpoint(net, 2 + i),
            .server = tide_loopback_address(1),
            .sample = sample,
            .user = (void *)(size_t)i,
            .lead = 2,
        };
        clients[i] = tide_client_create(&desc, now);
    }
    run(9.0);
    for (int i = 0; i < 2; i++) {
        const tide_client_status status = tide_client_status_of(clients[i]);
        TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(status.again_ticks > 300u); // It guessed wrong, and ran ticks again
        TIDE_CHECK(status.ahead_ticks > status.verified_tick); // ...having run ahead all along
        TIDE_CHECK(status.resyncs == 0);
    }
    // Everyone let go at tick 600: run on until every machine has confirmed the ticks past it
    run(14.0);
    const tide_world *world = tide_server_world(server);
    TIDE_REQUIRE(world && (uint32_t)world->Time.tick > stop_at + 60u);
    int struck = 0, twice = 0, strikes = 0, felt = 0;
    for (uint32_t row = 0; row < world->arch0_Post.count; row++) {
        const Post post = TIDE_AT(world, arch0_Post, Post, row);
        struck += post.struck;
        twice += post.twice;
        TIDE_CHECK(post.still == post.id * 7 && (post.struck == 0) == (post.by == 0));
    }
    for (uint32_t row = 0; row < world->arch1_Owner_Body.count; row++) {
        const Body body = TIDE_AT(world, arch1_Owner_Body, Body, row);
        strikes += body.strikes;
        felt += body.felt;
        TIDE_CHECK(body.quiet == 3);
    }
    // Every strike reached its post and its player (300 were sampled; a late input is a strike not made)
    TIDE_CHECK(struck > 200 && struck <= 320 && struck == strikes && felt == strikes && twice > 20 && twice < struck);
    for (int i = 0; i < 2; i++) {
        const tide_client_status status = tide_client_status_of(clients[i]);
        TIDE_CHECK(status.resyncs == 0);
        TIDE_CHECK(same(tide_client_verified_world(clients[i]), world));
        TIDE_CHECK(same(tide_client_world(clients[i]), world)); // What it shows: nothing left to guess
    }
    for (int i = 0; i < 2; i++) tide_client_destroy(clients[i]);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}
