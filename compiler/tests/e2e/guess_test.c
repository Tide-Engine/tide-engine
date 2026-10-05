#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

// A machine guesses another player's input by their last one for a while
// (tide_client_desc.guess_seconds), and past that as if they'd let go: every
// field back to its default, but for the ones the game marks [Absolute].
//
// Two players, each a quarter of a second from the server, so each runs half a
// second and more ahead of the last tick it had confirmed. One runs for three
// seconds, a unit a tick, with fire and a key held, then stops; the other
// watches. What the watcher shows of the runner is its predicted world, and
// what's known of them is its verified one, so the two say how far a guess
// carries what the runner was doing, and how far the runner's body moves in
// one update when a guess turns out wrong.

#define RUN_FROM 300u // The runner's ticks
#define RUN_TO 480u
#define WALK_FROM 600u // ...and the watcher's own, later
#define WALK_TO 700u
#define GUESS 9 // 0.15 seconds at 60 ticks a second

static tide_loopback *net;
static tide_server *server;
static tide_client *runner;
static tide_client *watcher;
static double now;

static void run_sample(void *user, const uint32_t tick, void *input)
{
    (void)user;
    Controls *c = input;
    *c = (Controls){0};
    c->aim = tide_f2(0.0f, 1.0f); // Where it points, whatever it does
    if (tick < RUN_FROM || tick >= RUN_TO) return;
    c->move = tide_f2(1.0f, 0.0f);
    c->fire = true;
    c->tide_dev.keyboard.space.pressed = true;
}

static void watch_sample(void *user, const uint32_t tick, void *input)
{
    (void)user;
    Controls *c = input;
    *c = (Controls){0};
    if (tick >= WALK_FROM && tick < WALK_TO) c->move = tide_f2(0.0f, 1.0f);
}

static tide_client *join(const uint32_t endpoint, const tide_sample_fn sample, const double guess_seconds)
{
    const tide_client_desc desc = {
        .game = &tide_game_api,
        .transport = tide_loopback_endpoint(net, endpoint),
        .server = tide_loopback_address(1),
        .sample = sample,
        .lead = 2,
        .guess_seconds = guess_seconds,
    };
    return tide_client_create(&desc, now);
}

static void start(const tide_net_conditions conditions, const double guess_seconds)
{
    now = 0.0;
    net = tide_loopback_create(41);
    tide_loopback_set_conditions(net, conditions);
    const tide_server_desc desc = {.game = &tide_game_api, .tick_rate = 60, .transports = {tide_loopback_endpoint(net, 1)}};
    server = tide_server_create(&desc, now);
    runner = join(2, run_sample, guess_seconds);
    watcher = NULL;
}

static void finish(void)
{
    tide_client_destroy(runner);
    tide_client_destroy(watcher);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}

// Which player a client is: whichever of the two the server heard from first.
static int32_t player_of(const tide_client *c)
{
    return tide_player_index(tide_client_status_of(c).player);
}

// A player's body in a world: false while it has none.
static bool body_of(const void *world, const int32_t player, Body *out)
{
    const tide_world *w = world;
    for (uint32_t row = 0; w && row < w->arch0_Owner_Body.count; row++) {
        if (tide_player_index(TIDE_AT(w, arch0_Owner_Body, Owner, row).player) != player) continue;
        *out = TIDE_AT(w, arch0_Owner_Body, Body, row);
        return true;
    }
    return false;
}

// What a client shows of a player's body, and what it knows of it.
typedef struct seen {
    bool both;
    Body shown;
    Body known;
} seen;

static seen see(const tide_client *c, const tide_client *whose)
{
    seen s = {0};
    const int32_t player = player_of(whose);
    s.both = body_of(tide_client_world(c), player, &s.shown) && body_of(tide_client_verified_world(c), player, &s.known);
    return s;
}

// What the watcher saw of the runner over a run, update by update.
typedef struct watched {
    int carried_most;  // The most ticks of running it showed past what it knew
    int carried_least; // ...and the least (under 0: it showed the runner behind what it knew)
    int at_guess;      // Updates it showed exactly GUESS ticks of running past what it knew
    int step_most;     // The most the runner's body moved in an update, past the ticks the update confirmed
    int ahead_most;    // The most ticks it ran ahead of the last one confirmed
    bool edges;        // It showed a press, a release or a lift it didn't know of
    bool turned;       // It showed the runner pointing anywhere but where it points
    int pushes_most;   // The most ticks of the held key it showed past what it knew
} watched;

static watched seen_so_far;
static float last_x;
static uint32_t last_verified;
static bool last_known;

static void watch(void)
{
    if (!watcher) return;
    const tide_client_status status = tide_client_status_of(watcher);
    const seen s = see(watcher, runner);
    if (status.state != TIDE_SESSION_CONNECTED || !s.both) return;
    watched *w = &seen_so_far;
    const int carried = (int)(s.shown.position.x - s.known.position.x);
    if (carried > w->carried_most) w->carried_most = carried;
    if (carried < w->carried_least) w->carried_least = carried;
    if (carried == GUESS) w->at_guess++;
    const int ahead = (int)(status.predicted_tick - status.verified_tick);
    if (ahead > w->ahead_most) w->ahead_most = ahead;
    const int pushes = s.shown.pushes - s.known.pushes;
    if (pushes > w->pushes_most) w->pushes_most = pushes;
    w->edges |= s.shown.presses != s.known.presses || s.shown.releases != s.known.releases || s.shown.lifts != s.known.lifts;
    w->turned |= s.shown.facing.x != s.known.facing.x || s.shown.facing.y != s.known.facing.y;
    if (last_known) {
        const int moved = (int)(s.shown.position.x - last_x);
        const int confirmed = (int)(status.verified_tick - last_verified);
        const int step = (moved < 0 ? -moved : moved) - confirmed;
        if (step > w->step_most) w->step_most = step;
    }
    last_x = s.shown.position.x;
    last_verified = status.verified_tick;
    last_known = true;
}

// Runs everything until `until`, in frames of 11 and 22 milliseconds.
static void run(const double until)
{
    bool odd = false;
    while (now < until) {
        now += (odd = !odd) ? 0.011 : 0.022;
        tide_loopback_set_time(net, now);
        tide_client_update(runner, now);
        if (watcher) tide_client_update(watcher, now);
        tide_server_update(server, now);
        watch();
    }
}

static void watch_from_here(void)
{
    seen_so_far = (watched){0};
    last_known = false;
}

static const tide_net_conditions far = {.latency = 0.25};

TIDE_TEST(guess_carries_what_a_player_did_only_so_far)
{
    start(far, 0.0);
    watcher = join(3, watch_sample, 0.0);
    watch_from_here();
    run(3.0);
    TIDE_CHECK(tide_client_status_of(runner).state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(tide_client_status_of(watcher).state == TIDE_SESSION_CONNECTED);

    // Nobody does anything: a guess with nothing to let go of is the last
    // input, so no tick runs again. Where the runner points isn't something
    // it's doing, and stays.
    const uint32_t again = tide_client_status_of(watcher).again_ticks;
    run(4.5);
    TIDE_CHECK(tide_client_status_of(watcher).again_ticks == again);
    seen s = see(watcher, runner);
    TIDE_CHECK(s.both && s.shown.facing.y == 1.0f && s.known.facing.y == 1.0f);

    // The runner runs, and stops. The watcher is half a second and more ahead
    // of what it knows, and shows GUESS ticks of running past that, never more:
    // so that's the most the runner's body moves in an update, as the watcher
    // learns it started and as it learns it stopped.
    run(9.5);
    const watched w = seen_so_far;
    TIDE_CHECK(w.ahead_most >= 30);
    TIDE_CHECK(w.carried_most == GUESS);
    TIDE_CHECK(w.carried_least == 0);
    TIDE_CHECK(w.at_guess > 100); // All the while it ran
    TIDE_CHECK(w.step_most <= GUESS);
    TIDE_CHECK(w.step_most >= GUESS - 1); // ...which it did move, twice
    TIDE_CHECK(w.pushes_most == GUESS); // What match code reads of the devices goes the same way
    // Letting go presses and lifts nothing: the runner's fire and key go up
    // when the server says they did, once
    TIDE_CHECK(!w.edges);
    TIDE_CHECK(!w.turned); // [Absolute]: kept, however long the guess
    TIDE_CHECK(tide_client_status_of(watcher).again_ticks > again + 1000u); // The price: the ticks past the guess, again

    s = see(watcher, runner);
    TIDE_CHECK(s.both && s.shown.position.x == (float)(RUN_TO - RUN_FROM) && s.known.position.x == s.shown.position.x);
    TIDE_CHECK(s.shown.presses == 1 && s.shown.releases == 1 && s.shown.lifts == 1);
    TIDE_CHECK(s.shown.pushes == (int)(RUN_TO - RUN_FROM));

    // Its own input is no guess: the watcher's own body goes as far as it runs
    // ahead, and the runner's machine shows it GUESS ticks past what it knows
    int own_most = 0;
    int theirs_most = 0;
    bool odd = false;
    while (now < 16.0) {
        now += (odd = !odd) ? 0.011 : 0.022;
        tide_loopback_set_time(net, now);
        tide_client_update(runner, now);
        tide_client_update(watcher, now);
        tide_server_update(server, now);
        const seen own = see(watcher, watcher);
        const seen theirs = see(runner, watcher);
        const int own_carried = (int)(own.shown.position.y - own.known.position.y);
        const int their_carried = (int)(theirs.shown.position.y - theirs.known.position.y);
        if (own.both && own_carried > own_most) own_most = own_carried;
        if (theirs.both && their_carried > theirs_most) theirs_most = their_carried;
    }
    TIDE_CHECK(own_most >= 30);
    TIDE_CHECK(theirs_most == GUESS);

    // Every tick's hash matched the server's: a tick run with the others let
    // go is never taken for the tick the server ran
    TIDE_CHECK(tide_client_status_of(runner).resyncs == 0);
    TIDE_CHECK(tide_client_status_of(watcher).resyncs == 0);
    Body truth;
    TIDE_CHECK(body_of(tide_server_world(server), player_of(runner), &truth) && truth.position.x == (float)(RUN_TO - RUN_FROM));
    TIDE_CHECK(body_of(tide_server_world(server), player_of(watcher), &truth) && truth.position.y == (float)(WALK_TO - WALK_FROM));
    finish();
}

// Told to guess for as far as it predicts, a client carries what a player did
// all the way: what it did before there was a limit, and the body moves by as
// much when the guess was wrong.
TIDE_TEST(guess_with_no_limit_carries_it_all_the_way)
{
    start(far, 2.0);
    watcher = join(3, watch_sample, 2.0);
    watch_from_here();
    run(12.0);
    const watched w = seen_so_far;
    TIDE_CHECK(w.ahead_most >= 30);
    TIDE_CHECK(w.carried_most >= 30);
    TIDE_CHECK(w.step_most >= 30);
    TIDE_CHECK(!w.turned);
    TIDE_CHECK(tide_client_status_of(watcher).resyncs == 0);
    finish();
}

// ...and told not to guess at all, it shows the others where it knows them to be.
TIDE_TEST(guess_of_no_ticks_shows_what_is_known)
{
    start(far, 1e-6);
    watcher = join(3, watch_sample, 1e-6);
    watch_from_here();
    run(12.0);
    const watched w = seen_so_far;
    TIDE_CHECK(w.ahead_most >= 30);
    TIDE_CHECK(w.carried_most == 0 && w.carried_least == 0);
    TIDE_CHECK(w.pushes_most == 0);
    TIDE_CHECK(!w.edges && !w.turned);
    TIDE_CHECK(tide_client_status_of(runner).resyncs == 0);
    TIDE_CHECK(tide_client_status_of(watcher).resyncs == 0);
    const seen s = see(watcher, runner);
    TIDE_CHECK(s.both && s.known.position.x == (float)(RUN_TO - RUN_FROM) && s.known.lifts == 1);
    finish();
}

// A network that loses, delays and reorders datagrams: frames come in bursts
// and the ticks confirmed in one update are many, some of them run as if the
// others had let go. The guess still carries no further, and no client's
// world ever differs from the server's. Some of the runner's inputs come late
// here, so how far it ran is the server's to say.
TIDE_TEST(guess_holds_on_a_bad_network)
{
    start((tide_net_conditions){.latency = 0.12, .jitter = 0.06, .loss = 0.2}, 0.0);
    watcher = join(3, watch_sample, 0.0);
    watch_from_here();
    run(20.0);
    const watched w = seen_so_far;
    TIDE_CHECK(w.ahead_most > GUESS + 5);
    TIDE_CHECK(w.carried_most == GUESS && w.carried_least == 0);
    TIDE_CHECK(w.pushes_most == GUESS);
    TIDE_CHECK(!w.edges && !w.turned);
    TIDE_CHECK(tide_client_status_of(runner).resyncs == 0);
    TIDE_CHECK(tide_client_status_of(watcher).resyncs == 0);
    const seen s = see(watcher, runner);
    Body truth = {0};
    TIDE_CHECK(body_of(tide_server_world(server), player_of(runner), &truth) && truth.position.x > 100.0f && truth.lifts >= 1);
    TIDE_CHECK(s.both && s.known.position.x == truth.position.x && s.known.lifts == truth.lifts);
    TIDE_CHECK(s.shown.position.x == truth.position.x && s.shown.pushes == truth.pushes);
    finish();
}
