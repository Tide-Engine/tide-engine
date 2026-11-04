#include "game.h"
#include "tide_test.h"

// predictionRange, with guesses that stay right: nobody's input changes. A
// confirmed tick runs whole, as something was far; what's near comes out as
// predicted, so the predicted ticks after it stand rather than run again
// (tide_game.near_same). Their far walker is then older than the last
// confirmed one, never by more than FAR_STALE_SECONDS (a quarter of a second,
// 15 ticks at 60), from where they run again. Every confirmed tick still
// comes out as the server's.

static void sample(void *user, const uint32_t tick, void *input)
{
    (void)user;
    (void)tick;
    *(Controls *)input = (Controls){0};
}

static bool walker(const void *world, const int32_t id, Body *out)
{
    const tide_world *w = world;
    for (uint32_t row = 0; w && row < w->arch0_Body_Walker.count; row++) {
        if (TIDE_AT(w, arch0_Body_Walker, Walker, row).id != id) continue;
        *out = TIDE_AT(w, arch0_Body_Walker, Body, row);
        return true;
    }
    return false;
}

TIDE_TEST(predict_kept_while_guesses_are_right)
{
    double now = 0.0;
    tide_loopback *net = tide_loopback_create(29);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.1});
    const tide_server_desc desc = {.game = &tide_game_api, .tick_rate = 60, .transports = {tide_loopback_endpoint(net, 1)}};
    tide_server *server = tide_server_create(&desc, now);
    tide_client *clients[2];
    for (uint32_t k = 0; k < 2; k++) {
        const tide_client_desc cd = {.game = &tide_game_api, .transport = tide_loopback_endpoint(net, 2 + k),
                                     .server = tide_loopback_address(1), .sample = sample, .lead = 2};
        clients[k] = tide_client_create(&cd, now);
    }
    int frames = 0, near_ahead = 0, far_within = 0, far_older = 0;
    bool odd = false;
    while (now < 6.0) {
        now += (odd = !odd) ? 0.011 : 0.022;
        tide_loopback_set_time(net, now);
        for (int k = 0; k < 2; k++) tide_client_update(clients[k], now);
        tide_server_update(server, now);
        const tide_client_status status = tide_client_status_of(clients[0]);
        if (status.state != TIDE_SESSION_CONNECTED || status.predicted_tick <= status.verified_tick) continue;
        Body shown_near, known_near, shown_far, known_far;
        const void *shown = tide_client_world(clients[0]), *known = tide_client_verified_world(clients[0]);
        if (!walker(shown, 1, &shown_near) || !walker(known, 1, &known_near) || !walker(shown, 2, &shown_far)
            || !walker(known, 2, &known_far)) continue;
        frames++;
        near_ahead += shown_near.ran - known_near.ran == (int)(status.predicted_tick - status.verified_tick);
        const int older = known_far.ran - shown_far.ran; // How many confirmed ticks it lags the last one
        far_within += older >= 0 && older <= 15;
        far_older += older > 0;
    }
    TIDE_CHECK(frames > 200);
    TIDE_CHECK(near_ahead == frames); // What's near ran every tick it predicted
    TIDE_CHECK(far_within == frames); // What's far, as confirmed, a quarter of a second old at most
    TIDE_CHECK(far_older > frames / 4); // ...and older than the last, where the predicted ticks were kept
    for (int k = 0; k < 2; k++) {
        const tide_client_status s = tide_client_status_of(clients[k]);
        TIDE_CHECK(s.state == TIDE_SESSION_CONNECTED && s.resyncs == 0);
        TIDE_CHECK(s.near_kept > 100); // Most confirmed ticks kept the ones after them
    }
    Body truth, known;
    TIDE_CHECK(walker(tide_server_world(server), 2, &truth) && walker(tide_client_verified_world(clients[0]), 2, &known));
    TIDE_CHECK(known.ran > 0 && known.ran <= truth.ran && truth.ran - known.ran < 60);
    for (int k = 0; k < 2; k++) tide_client_destroy(clients[k]);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}
