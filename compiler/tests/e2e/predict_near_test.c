#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

// predictionRange: a client's predicted ticks leave out what's farther than
// that from its player's own entities. A far walker stays in its predicted
// world as the server last confirmed it, so it ticks once, as each tick is
// confirmed; a near one is predicted as before. Every confirmed tick still
// comes out as the server's.
//
// Two walkers, one 4 units from where the players start and one 100 units
// away, with a range of 10. The mover stays put for its first five seconds,
// then runs to x = 96 in a second, beside the other walker: the two change
// places. The other player stays where it started: its own client predicts
// the first walker throughout.

#define RUN_FROM 300u
#define RUN_TO 360u

static tide_loopback *net;
static tide_server *server;
static tide_client *mover;
static tide_client *other;
static double now;

static void move_sample(void *user, const uint32_t tick, void *input)
{
    (void)user;
    Controls *c = input;
    *c = (Controls){0};
    if (tick >= RUN_FROM && tick < RUN_TO) c->move = tide_f2(1.6f, 0.0f);
}

static void still_sample(void *user, const uint32_t tick, void *input)
{
    (void)user;
    (void)tick;
    *(Controls *)input = (Controls){0};
}

static tide_client *join(const uint32_t endpoint, const tide_sample_fn sample)
{
    const tide_client_desc desc = {
        .game = &tide_game_api,
        .transport = tide_loopback_endpoint(net, endpoint),
        .server = tide_loopback_address(1),
        .sample = sample,
        .lead = 2,
    };
    return tide_client_create(&desc, now);
}

static void start(const tide_net_conditions conditions)
{
    now = 0.0;
    net = tide_loopback_create(17);
    tide_loopback_set_conditions(net, conditions);
    const tide_server_desc desc = {.game = &tide_game_api, .tick_rate = 60, .transports = {tide_loopback_endpoint(net, 1)}};
    server = tide_server_create(&desc, now);
    mover = join(2, move_sample);
    other = join(3, still_sample);
}

static void finish(void)
{
    tide_client_destroy(mover);
    tide_client_destroy(other);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}

// A walker's body in a world: false while it has none.
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

// What a client shows of a near walker and a far one, against what it knows
// of them, frame by frame while it runs ahead.
typedef struct watched {
    int frames;     // Frames with both walkers in both worlds
    int far_same;   // ...in which it showed the far walker exactly as it knew it
    int near_ahead; // ...and the near one run every tick it predicted past that
} watched;

static void watch(const tide_client *c, const int32_t near, const int32_t far, watched *out)
{
    const tide_client_status status = tide_client_status_of(c);
    if (status.state != TIDE_SESSION_CONNECTED || status.predicted_tick <= status.verified_tick) return;
    Body shown_near, known_near, shown_far, known_far;
    const void *shown = tide_client_world(c), *known = tide_client_verified_world(c);
    if (!walker(shown, near, &shown_near) || !walker(known, near, &known_near) || !walker(shown, far, &shown_far)
        || !walker(known, far, &known_far)) return;
    out->frames++;
    if (memcmp(&shown_far, &known_far, sizeof shown_far) == 0) out->far_same++;
    // Run every predicted tick: by Tide's system and by the one written in C
    const int ahead = (int)(status.predicted_tick - status.verified_tick);
    if (shown_near.ran - known_near.ran == ahead && shown_near.aged - known_near.aged == ahead) out->near_ahead++;
}

typedef struct phase {
    watched mover;  // Walker 1 near, 2 far; then, once it has run, the other way round
    watched other;  // Walker 1 near, 2 far, throughout
} phase;

// Runs everything until `until`, in frames of 11 and 22 milliseconds.
static void run(const double until, phase *seen, const bool ran)
{
    bool odd = false;
    while (now < until) {
        now += (odd = !odd) ? 0.011 : 0.022;
        tide_loopback_set_time(net, now);
        tide_client_update(mover, now);
        tide_client_update(other, now);
        tide_server_update(server, now);
        if (!seen) continue;
        watch(mover, ran ? 2 : 1, ran ? 1 : 2, &seen->mover);
        watch(other, 1, 2, &seen->other);
    }
}

static void check_all_of(const watched *w)
{
    TIDE_CHECK(w->frames > 100);
    TIDE_CHECK(w->far_same == w->frames); // Never run in a predicted tick
    TIDE_CHECK(w->near_ahead == w->frames); // Run in every one
}

static void check_confirmed_as_the_server(void)
{
    TIDE_CHECK(tide_client_status_of(mover).state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(tide_client_status_of(other).state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(tide_client_status_of(mover).resyncs == 0);
    TIDE_CHECK(tide_client_status_of(other).resyncs == 0);
    // Both walkers ran every tick the server did, as far as each client knows
    for (int32_t id = 1; id <= 2; id++) {
        Body truth, known;
        TIDE_CHECK(walker(tide_server_world(server), id, &truth));
        TIDE_CHECK(walker(tide_client_verified_world(mover), id, &known));
        TIDE_CHECK(known.ran > 0 && known.ran <= truth.ran && truth.ran - known.ran < 60);
    }
}

static void check_far_walkers_wait(const tide_net_conditions conditions)
{
    start(conditions);
    phase before = {0};
    run(5.0, &before, false);
    check_all_of(&before.mover);
    check_all_of(&before.other);
    run(7.0, NULL, false); // The mover runs over
    phase after = {0};
    run(12.0, &after, true);
    check_all_of(&after.mover);
    check_all_of(&after.other);
    check_confirmed_as_the_server();
    finish();
}

// Views draw a far walker from the confirmed worlds the frame presents,
// blended on their clock: where the drawn world has it the same at both ends
// of every tick, it would only move a tick at a time. A near one is drawn
// from the predicted worlds as ever.
TIDE_TEST(predict_near_draws_far_walkers_blended_as_confirmed)
{
    start((tide_net_conditions){.latency = 0.1});
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    tide_local_init(&local);
    int frames = 0, far_right = 0, between = 0, near_right = 0;
    bool odd = false;
    while (now < 5.0) {
        now += (odd = !odd) ? 0.011 : 0.022;
        tide_loopback_set_time(net, now);
        tide_client_update(mover, now);
        tide_client_update(other, now);
        tide_server_update(server, now);
        const tide_view_worlds v = tide_client_view(mover);
        Body confirmed, before, shown, shown_before;
        if (!v.current || !v.previous || !v.verified || !v.verified_previous || !walker(v.verified, 2, &confirmed)
            || !walker(v.verified_previous, 2, &before) || !walker(v.current, 1, &shown) || !walker(v.previous, 1, &shown_before)) continue;
        tide_draw_reset(&draw);
        tide_frame(v.current, v.previous, v.alpha, v.verified, v.verified_previous, v.verified_alpha, &local, &draw, &gui);
        tide_client_mark_presented(mover);
        frames++;
        const float expected = before.position.y + (confirmed.position.y - before.position.y) * v.verified_alpha;
        if (tide_abs_f(local.Drawn.secondY - expected) < 1e-6f) far_right++;
        if (tide_abs_f(local.Drawn.secondY - before.position.y) > 1e-6f && tide_abs_f(local.Drawn.secondY - confirmed.position.y) > 1e-6f) between++;
        const float near_expected = shown_before.position.y + (shown.position.y - shown_before.position.y) * v.alpha;
        if (tide_abs_f(local.Drawn.firstY - near_expected) < 1e-6f) near_right++;
    }
    TIDE_CHECK(frames > 100);
    TIDE_CHECK(far_right == frames);
    TIDE_CHECK(between > frames / 4); // Blended, not a tick at a step
    TIDE_CHECK(near_right == frames);
    tide_local_free(&local);
    tide_draw_free(&draw);
    finish();
}

TIDE_TEST(predict_near_leaves_far_walkers_as_confirmed)
{
    check_far_walkers_wait((tide_net_conditions){.latency = 0.1});
}

TIDE_TEST(predict_near_holds_on_a_bad_network)
{
    check_far_walkers_wait((tide_net_conditions){.latency = 0.12, .jitter = 0.06, .loss = 0.2});
}
