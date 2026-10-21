#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

// A server and a player on a loopback network, in made-up time, with updates
// a tenth of a second apart: the server sends 64 KB of the world in each, so
// the world's 4 MB take about six seconds, more than the four seconds of
// ticks it keeps. The ticks the player needs once it has the world are kept
// until it catches up, rather than the world being sent again and again.

static void sample(void *user, const uint32_t tick, void *input)
{
    (void)user;
    Controls *c = input;
    *c = (Controls){.value = (int32_t)(tick % 7u)};
}

TIDE_TEST(net_join_big_world_catches_up)
{
    tide_loopback *net = tide_loopback_create(7);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.02, .loss = 0.01});
    double now = 0.0;
    const tide_server_desc server_desc = {.game = &tide_game_api, .tick_rate = 60, .transports = {tide_loopback_endpoint(net, 1)}};
    tide_server *server = tide_server_create(&server_desc, now);
    TIDE_REQUIRE(server != NULL);
    const tide_client_desc client_desc = {
        .game = &tide_game_api,
        .transport = tide_loopback_endpoint(net, 2),
        .server = tide_loopback_address(1),
        .sample = sample,
        .lead = 2,
    };
    tide_client *client = tide_client_create(&client_desc, now);
    TIDE_REQUIRE(client != NULL);

    double connected = 0.0;
    while (now < 20.0) {
        now += 0.1;
        tide_loopback_set_time(net, now);
        tide_client_update(client, now);
        tide_server_update(server, now);
        if (!connected && tide_client_status_of(client).state == TIDE_SESSION_CONNECTED) connected = now;
    }

    const tide_client_status s = tide_client_status_of(client);
    printf("    the world arrived after %.1f s\n", connected);
    TIDE_CHECK(connected > 4.0);
    TIDE_CHECK(s.state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(s.resyncs == 0);
    TIDE_CHECK(s.verified_tick + 30u > tide_server_tick(server));
    const tide_world *world = tide_client_verified_world(client);
    TIDE_CHECK(world && world->Archive.total > 0);

    tide_client_destroy(client);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}

// One player's frames, a 60th of a second apart, watched from the moment it
// has a world (again from `watch`): what each ran, and where it stood as it
// first ran a tick ahead of what's confirmed. With `twice`, a frame updates
// the client twice, at the same time, as a session does (tide_session_update):
// what a frame may run is a frame's, however many updates it has.
typedef struct watched {
    tide_loopback *net;
    tide_server *server;
    tide_client *client;
    double now;
    bool twice;
    uint64_t worked;         // The client's clock as the frame before began (slow_clock)
    double slowest;          // Its longest frame, in seconds
    tide_client_status from; // Its status as the watch began
    uint32_t most;        // The most ticks one frame ran...
    uint32_t most_sent;   // ...and the most of the server's: the ticks it ran as the server sent them
    uint32_t furthest;    // The furthest it was ahead of what's confirmed
    float longest;        // The longest the server said its inputs waited, in milliseconds
    bool predicted;       // It has run a tick ahead since...
    double predicted_at;  // ...when...
    uint32_t confirmed;   // ...with this many ticks confirmed...
    uint32_t heard;       // ...and the server's tick, as it had last said, this
} watched;

static void watch(watched *w)
{
    w->from = tide_client_status_of(w->client);
    w->most = w->most_sent = w->furthest = 0;
    w->longest = 0.0f;
    w->predicted = false;
}

// A clock for a client to measure its ticks by, which only its ticks move:
// each one it runs takes `slow_cost` nanoseconds, as on a machine where the
// game's ticks are dear.
static uint64_t slow_clock;
static uint64_t slow_cost;

static uint64_t slow_now(void)
{
    return slow_clock;
}

static void slow_tick(void *world, const tide_jobs *jobs)
{
    tide_game_api.tick(world, jobs);
    slow_clock += slow_cost;
}

static void step(watched *w, const bool client, const bool server)
{
    // A frame takes a 60th of a second, or as long as the work in the one
    // before took, where that's more: time passes while a machine works
    const double worked = (double)(slow_clock - w->worked) * 1e-9;
    w->worked = slow_clock;
    const double frame = worked > 1.0 / 60 ? worked : 1.0 / 60;
    if (frame > w->slowest) w->slowest = frame;
    w->now += frame;
    tide_loopback_set_time(w->net, w->now);
    const tide_client_status before = tide_client_status_of(w->client);
    if (client) tide_client_update(w->client, w->now);
    if (client && w->twice) tide_client_update(w->client, w->now);
    if (server) tide_server_update(w->server, w->now);
    const tide_client_status s = tide_client_status_of(w->client);
    if (s.state != TIDE_SESSION_CONNECTED) return;
    const uint32_t ran = s.run_ticks - before.run_ticks;
    const uint32_t sent = ran - (s.ahead_ticks - before.ahead_ticks);
    if (ran > w->most) w->most = ran;
    if (sent > w->most_sent) w->most_sent = sent;
    if (s.predicted_tick - s.verified_tick > w->furthest) w->furthest = s.predicted_tick - s.verified_tick;
    if (s.early_ms > w->longest) w->longest = s.early_ms;
    if (!w->predicted && s.ahead_ticks > w->from.ahead_ticks) {
        w->predicted = true;
        w->predicted_at = w->now;
        w->confirmed = s.verified_tick;
        w->heard = s.server_tick;
    }
}

// The server, and a player who joins, on `net`: until the player has the world.
// With `cost`, the player has a clock, by which each of its ticks takes that
// many nanoseconds (`game` then runs its ticks through slow_tick).
static void join_at_a_cost(watched *w, tide_loopback *net, const tide_game *game, const bool twice, const uint64_t cost)
{
    slow_clock = 0;
    slow_cost = cost;
    *w = (watched){.net = net, .twice = twice};
    w->server = tide_server_create(&(tide_server_desc){.game = &tide_game_api, .tick_rate = 60,
                                     .transports = {tide_loopback_endpoint(net, 1)}}, w->now);
    TIDE_REQUIRE(w->server != NULL);
    w->client = tide_client_create(&(tide_client_desc){.game = game, .transport = tide_loopback_endpoint(net, 2),
                                     .server = tide_loopback_address(1), .sample = sample, .lead = 2,
                                     .now_ns = cost ? slow_now : NULL}, w->now);
    TIDE_REQUIRE(w->client != NULL);
    watch(w);
    while (tide_client_status_of(w->client).state != TIDE_SESSION_CONNECTED) {
        TIDE_REQUIRE(w->now < 30.0);
        step(w, true, true);
    }
}

static void join(watched *w, tide_loopback *net, const tide_game *game, const bool twice)
{
    join_at_a_cost(w, net, game, twice, 0);
}

static void leave(watched *w)
{
    tide_client_destroy(w->client);
    tide_server_destroy(w->server);
    tide_loopback_destroy(w->net);
}

// A player who joins runs no further ahead than its inputs need, however long its world took to arrive. The
// server's ticks meanwhile ran with no input of the player's, as it has no world to sample one for: those are not
// late inputs, and counting them so would set its clock that far ahead once it's in.
//
// Nor does it predict from the world that came, which is as old as it took to come: it runs the ticks the server
// sent since, so many a frame, and predicts once it has run every tick the server said it had.
TIDE_TEST(net_join_big_world_runs_no_further_ahead_than_its_inputs_need)
{
    uint32_t furthest = 0;
    float longest = 0.0f;
    uint32_t most = 0;
    uint32_t most_sent = 0;
    for (uint32_t seed = 1; seed <= 8; seed++) {
        tide_loopback *net = tide_loopback_create(seed);
        tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.04, .jitter = 0.015, .loss = 0.05});
        watched w;
        join(&w, net, &tide_game_api, seed % 2u == 0u);
        // Frames a 60th of a second apart: the world takes a second and more
        const double connected = w.now;
        const uint32_t world_took = tide_server_tick(w.server);
        while (w.now < connected + 4.0) step(&w, true, true);
        const tide_client_status s = tide_client_status_of(w.client);
        TIDE_CHECK(world_took > 60u); // Ticks the server ran for the player before it had the world
        TIDE_CHECK(s.resyncs == 0);
        TIDE_CHECK(s.early_ms >= 0.0f);
        const tide_world *world = tide_client_verified_world(w.client);
        TIDE_CHECK(world && world->Archive.total > 0);
        // It had run every tick the server said it had before it ran one ahead: the ticks the world took among them
        TIDE_CHECK(w.predicted && w.confirmed >= w.heard && w.confirmed >= world_took);
        if (w.furthest > furthest) furthest = w.furthest;
        if (w.longest > longest) longest = w.longest;
        if (w.most > most) most = w.most;
        if (w.most_sent > most_sent) most_sent = w.most_sent;
        leave(&w);
    }
    // A round trip here is about 7 ticks and the lead 2. Counting the ticks before the world as late would have it
    // run the 60 a client may, its inputs waiting most of a second.
    printf("    of 8 networks, the furthest a player ran ahead of what's confirmed, from when it had the world, was %u ticks,"
           " the longest its inputs waited %.0f ms, and the most ticks it ran in a frame %u, %u of them the server's\n",
           furthest, (double)longest, most, most_sent);
    TIDE_CHECK(furthest <= 20u);
    TIDE_CHECK(longest <= 150.0f);
    TIDE_CHECK(most_sent <= 8u);
}

// A player's first moments on a network that's only late, 50 ms each way. The world takes over a second to come,
// and the server ran 70 ticks and more meanwhile. The player runs those as the server sent them, 8 a frame at the
// most, and nothing ahead until it has; then the ticks up to its clock, 16 an update at the most. It samples no
// input for a tick its clock says can't be in time, and the ticks the server ran with no input of its own went as
// predicted: none is run again, as nobody else is in the match to guess wrong about.
static void first_moments(const bool twice)
{
    tide_loopback *net = tide_loopback_create(21);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.05});
    watched w;
    join(&w, net, &tide_game_api, twice);
    const double connected = w.now;
    const uint32_t world_took = tide_server_tick(w.server);
    while (w.now < connected + 4.0) step(&w, true, true);
    const tide_client_status s = tide_client_status_of(w.client);
    printf("    updated %s a frame: the server ran %u ticks before the player had the world; %.2f s after it had, with %u confirmed"
           " of the %u the server had said, it ran its first tick ahead; the most it ran in a frame was %u ticks, %u of them the"
           " server's; %u counted as late, %u run again\n", twice ? "twice" : "once", world_took, w.predicted_at - connected,
           w.confirmed, w.heard, w.most, w.most_sent, s.late_ticks, s.again_ticks);
    TIDE_CHECK(world_took > 60u);
    TIDE_CHECK(s.state == TIDE_SESSION_CONNECTED && s.resyncs == 0);
    TIDE_CHECK(w.predicted && w.confirmed >= w.heard && w.confirmed >= world_took);
    TIDE_CHECK(w.predicted_at - connected <= 0.5); // 70 ticks and those since at 7 a frame more than the server runs, after a round trip
    TIDE_CHECK(w.most_sent <= 8u);
    TIDE_CHECK(w.most <= 8u + (twice ? 32u : 16u)); // ...and the ticks up to its clock, in the frame it's level
    TIDE_CHECK(s.again_ticks == 0);
    TIDE_CHECK(s.late_ticks <= 2u); // The first it sampled, as its clock was first set
    TIDE_CHECK(w.furthest <= 12u && w.longest <= 100.0f);
    TIDE_CHECK(s.verified_tick + 10u > tide_server_tick(w.server));
    const tide_world *world = tide_client_verified_world(w.client);
    TIDE_CHECK(world && world->Archive.total > 0);
    leave(&w);
}

TIDE_TEST(net_join_big_world_is_caught_up_with_before_anything_is_predicted)
{
    first_moments(false);
}

// What a frame may run of the server's ticks is the frame's, not each
// update's: a session updates its client twice a frame, at the same time.
TIDE_TEST(net_join_big_world_is_caught_up_with_as_a_session_updates_it)
{
    first_moments(true);
}

typedef struct fresh_inputs {
    tide_client *client;
    bool watch;
    uint32_t sampled;
    uint32_t stale;
    uint32_t next; // The tick after the last input sampled, even while unwatched
} fresh_inputs;

// Freshness is judged when an input is sampled: a tick the server has
// already said it ran cannot receive a newly sampled input in time.
static void sample_fresh(void *user, const uint32_t tick, void *input)
{
    fresh_inputs *f = user;
    if (f->watch) {
        f->sampled++;
        if (tick < tide_client_status_of(f->client).server_tick) f->stale++;
    }
    f->next = tick + 1u;
    sample(NULL, tick, input);
}

static void join_fresh_at_cost(watched *w, fresh_inputs *f, const bool twice, const uint64_t cost, const double budget,
                               const tide_prediction prediction)
{
    static tide_game slow;
    slow = tide_game_api;
    slow.tick = slow_tick;
    slow.prediction = prediction;
    slow_clock = 0;
    slow_cost = cost;
    *f = (fresh_inputs){0};
    *w = (watched){.net = tide_loopback_create(25), .twice = twice};
    tide_loopback_set_conditions(w->net, (tide_net_conditions){.latency = 0.05});
    w->server = tide_server_create(&(tide_server_desc){.game = &tide_game_api, .tick_rate = 60,
                                     .transports = {tide_loopback_endpoint(w->net, 1)}}, w->now);
    TIDE_REQUIRE(w->server != NULL);
    const bool own = cost > 0 || prediction != tide_game_api.prediction; // A game of its own: dear ticks, or no prediction
    w->client = tide_client_create(&(tide_client_desc){.game = own ? &slow : &tide_game_api,
                                     .transport = tide_loopback_endpoint(w->net, 2), .server = tide_loopback_address(1),
                                     .sample = sample_fresh, .user = f, .lead = 2, .now_ns = cost ? slow_now : NULL,
                                     .resim_budget = budget}, w->now);
    TIDE_REQUIRE(w->client != NULL);
    f->client = w->client;
    watch(w);
    while (tide_client_status_of(w->client).state != TIDE_SESSION_CONNECTED) {
        TIDE_REQUIRE(w->now < 30.0);
        step(w, true, true);
    }
}

static void join_fresh(watched *w, fresh_inputs *f, const bool twice)
{
    join_fresh_at_cost(w, f, twice, 0, 0.0, tide_game_api.prediction);
}

// Catching up can take several frames, and a later frame can itself be
// late. The first catch-up update skipped old inputs, but that floor must
// follow the clock and the server while catch-up continues. Otherwise the
// later update samples the ticks this machine just slept through.
TIDE_TEST(net_join_big_world_keeps_catch_up_inputs_fresh_after_another_pause)
{
    for (int twice = 0; twice < 2; twice++) {
        watched w;
        fresh_inputs f;
        join_fresh(&w, &f, twice == 1);
        const tide_client_status joined = tide_client_status_of(w.client);
        TIDE_REQUIRE(joined.predicted_tick == joined.verified_tick && joined.server_tick > joined.verified_tick + 8u);
        // The server runs half a second while the client, still catching
        // up, waits for its next frame. This is a second pause, not its
        // original world download or the update that entered catch-up.
        for (int frame = 0; frame < 30; frame++) step(&w, false, true);
        f.watch = true;
        step(&w, true, true);
        const tide_client_status resumed = tide_client_status_of(w.client);
        TIDE_CHECK(resumed.predicted_tick == resumed.verified_tick && resumed.server_tick > resumed.verified_tick);
        TIDE_CHECK(f.sampled > 0 && f.stale == 0);
        printf("    updated %s a frame, after a pause during catch-up: sampled %u inputs, %u already known to be stale\n",
               twice ? "twice" : "once", f.sampled, f.stale);
        const double resumed_at = w.now;
        while (w.now < resumed_at + 3.0) step(&w, true, true);
        const tide_client_status s = tide_client_status_of(w.client);
        TIDE_CHECK(f.stale == 0);
        TIDE_CHECK(s.state == TIDE_SESSION_CONNECTED && s.resyncs == 0);
        TIDE_CHECK(s.verified_tick + 10u > tide_server_tick(w.server));
        leave(&w);
    }
}

// An input that was fresh when sampled can still arrive late if the network
// slows afterwards. Skipping knowingly old ticks must not hide this real
// lateness from the counter or the clock's early-input feedback.
TIDE_TEST(net_join_big_world_fresh_inputs_can_still_be_counted_as_late)
{
    watched w;
    fresh_inputs f;
    join_fresh(&w, &f, true);
    const double connected = w.now;
    while (w.now < connected + 4.0) step(&w, true, true);
    const uint32_t late = tide_client_status_of(w.client).late_ticks;
    f.watch = true;
    tide_loopback_set_conditions(w.net, (tide_net_conditions){.latency = 0.25});
    const double slowed = w.now;
    while (w.now < slowed + 3.0) step(&w, true, true);
    const tide_client_status s = tide_client_status_of(w.client);
    printf("    after the network slowed, sampled %u fresh inputs and counted %u of them as late\n",
           f.sampled, s.late_ticks - late);
    TIDE_CHECK(f.sampled > 0 && f.stale == 0);
    TIDE_CHECK(s.late_ticks > late);
    TIDE_CHECK(s.state == TIDE_SESSION_CONNECTED && s.resyncs == 0);
    leave(&w);
}

// With enough input delay, the world is behind the sampled inputs. A short
// pause can leave fewer than eight ticks since the last sample but more than
// eight authoritative ticks to run: bounded playback mustn't then sample
// the old inputs it hasn't reached. A game whose clients never predict
// (Prediction.Delay) has that delay by itself, about a round trip and a half:
// a client that predicts takes none while no guess of its is wrong, and this
// one's never is, as it's alone with the server.
TIDE_TEST(net_join_big_world_a_delayed_client_keeps_inputs_fresh_after_a_short_pause)
{
    for (int twice = 0; twice < 2; twice++) {
        watched w;
        fresh_inputs f;
        join_fresh_at_cost(&w, &f, twice == 1, 0, 0.0, TIDE_PREDICTION_DELAY);
        const double connected = w.now;
        while (w.now < connected + 6.0) step(&w, true, true);
        const tide_client_status before = tide_client_status_of(w.client);
        TIDE_REQUIRE(before.state == TIDE_SESSION_CONNECTED && before.resyncs == 0 && before.delay_ticks >= 6u);
        const uint32_t next = f.next;
        // A pause of a few frames more than the last sample is ahead of the server: the server gets past it by those few
        const uint32_t gap = next > before.server_tick ? next - before.server_tick : 0u;
        for (uint32_t frame = 0; frame < gap + 4u; frame++) step(&w, false, true);
        f.watch = true;
        step(&w, true, true);
        const tide_client_status resumed = tide_client_status_of(w.client);
        // This is the case a trigger based on >8 ticks past the last sample
        // misses, although the confirmed world has more than eight to run.
        TIDE_REQUIRE(resumed.server_tick > next && resumed.server_tick - next <= 8u);
        TIDE_REQUIRE(resumed.server_tick > before.verified_tick + 8u);
        TIDE_REQUIRE(resumed.verified_tick < resumed.server_tick);
        TIDE_CHECK(resumed.predicted_tick == resumed.verified_tick);
        TIDE_CHECK(f.sampled > 0 && f.stale == 0);
        printf("    updated %s a frame with %u ticks of delay: after a short pause, the server was %u ticks past the last"
               " sample and %u past the verified world; sampled %u inputs, %u already known to be stale\n",
               twice ? "twice" : "once", before.delay_ticks, resumed.server_tick - next,
               resumed.server_tick - before.verified_tick, f.sampled, f.stale);
        const double resumed_at = w.now;
        while (w.now < resumed_at + 3.0) step(&w, true, true);
        const tide_client_status s = tide_client_status_of(w.client);
        TIDE_CHECK(f.stale == 0 && s.state == TIDE_SESSION_CONNECTED && s.resyncs == 0);
        TIDE_CHECK(s.verified_tick + 12u > tide_server_tick(w.server));
        leave(&w);
    }
}

// A machine with a clock, on which the game's ticks are dear: 5 ms each, where
// a frame is 16.7. Catching up takes half a frame's time at the most, once it
// has run a tick more than came due, so its frames stay about as long as they
// were: 2 of the server's ticks a frame here, where a machine with no clock
// runs 8. It still gets level, a tick a frame.
TIDE_TEST(net_join_big_world_is_caught_up_with_in_the_time_a_frame_has_to_spare)
{
    static tide_game slow;
    slow = tide_game_api;
    slow.tick = slow_tick;
    for (int twice = 0; twice < 2; twice++) {
        tide_loopback *net = tide_loopback_create(23);
        tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.05});
        watched w;
        join_at_a_cost(&w, net, &slow, twice == 1, 5000000u);
        const double connected = w.now;
        w.slowest = 0.0;
        // Until it's level: what it runs ahead from there isn't looked at here
        while (!w.predicted && w.now < connected + 10.0) step(&w, true, true);
        printf("    updated %s a frame, with ticks of 5 ms: level %.2f s after it had the world; the most of the server's ticks it"
               " ran in a frame was %u, and its longest frame %.1f ms\n", twice ? "twice" : "once", w.predicted_at - connected,
               w.most_sent, w.slowest * 1e3);
        TIDE_CHECK(w.predicted && w.confirmed >= w.heard);
        TIDE_CHECK(w.most_sent <= 3u);
        TIDE_CHECK(w.slowest <= 0.0171); // No frame was longer for it
        TIDE_CHECK(w.predicted_at - connected <= 3.0);
        TIDE_CHECK(tide_client_status_of(w.client).resyncs == 0);
        leave(&w);
    }
}

// ...and one that can't run the match's ticks in the time they take: 25 ms
// each, where 16.7 pass. It can't keep up whatever it does, and falls behind
// until the server sends the world again. What it mustn't do is run every
// tick that came due while it ran the last ones: each frame would then be
// longer than the one before, without end. It runs 8 a frame, so its frames
// are as long as 8 ticks take, and stay so.
TIDE_TEST(net_join_big_world_on_a_machine_too_slow_for_it_keeps_its_frames_bounded)
{
    static tide_game slow;
    slow = tide_game_api;
    slow.tick = slow_tick;
    for (int twice = 0; twice < 2; twice++) {
        tide_loopback *net = tide_loopback_create(24);
        tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.05});
        watched w;
        join_at_a_cost(&w, net, &slow, twice == 1, 25000000u);
        const double connected = w.now;
        w.slowest = 0.0;
        while (w.now < connected + 8.0) step(&w, true, true);
        printf("    updated %s a frame, with ticks of 25 ms: the most of the server's ticks it ran in a frame was %u, and its"
               " longest frame %.0f ms, in 8 seconds\n", twice ? "twice" : "once", w.most_sent, w.slowest * 1e3);
        TIDE_CHECK(tide_client_status_of(w.client).state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(w.most_sent <= 8u);
        TIDE_CHECK(w.slowest <= 0.201); // 8 ticks of 25 ms
        leave(&w);
    }
}

// The same for a player whose world went wrong and was sent again: a hash
// that comes out wrong, once, on this machine alone.
static bool break_hash;

static uint64_t flaky_hash(const void *world)
{
    const uint64_t hash = tide_game_api.hash_world(world);
    if (!break_hash) return hash;
    break_hash = false;
    return hash ^ 1u;
}

TIDE_TEST(net_join_big_world_sent_again_is_caught_up_with_too)
{
    static tide_game flaky;
    flaky = tide_game_api;
    flaky.hash_world = flaky_hash;
    break_hash = false;
    tide_loopback *net = tide_loopback_create(22);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.05});
    watched w;
    join(&w, net, &flaky, true);
    while (tide_server_tick(w.server) < 300u) step(&w, true, true);
    TIDE_CHECK(tide_client_status_of(w.client).predicted_tick > tide_client_status_of(w.client).verified_tick);
    break_hash = true;
    while (tide_client_status_of(w.client).resyncs == 0) {
        TIDE_REQUIRE(w.now < 30.0);
        step(&w, true, true);
    }
    // From the frame the world came in again: nothing ahead of it until the ticks since are run
    const double again = w.now;
    const tide_client_status then = tide_client_status_of(w.client);
    TIDE_CHECK(then.predicted_tick == then.verified_tick);
    watch(&w);
    while (w.now < again + 3.0) step(&w, true, true);
    const tide_client_status s = tide_client_status_of(w.client);
    printf("    sent the world again, it ran its first tick ahead %.2f s later, with %u confirmed of the %u the server had"
           " said; the most it ran in a frame was %u ticks, %u of them the server's; %u run again\n",
           w.predicted_at - again, w.confirmed, w.heard, w.most, w.most_sent, s.again_ticks);
    TIDE_CHECK(s.state == TIDE_SESSION_CONNECTED && s.resyncs == 1);
    TIDE_CHECK(w.predicted && w.confirmed >= w.heard);
    TIDE_CHECK(w.predicted_at - again <= 0.5);
    TIDE_CHECK(w.most_sent <= 8u);
    TIDE_CHECK(s.again_ticks == 0);
    TIDE_CHECK(w.furthest <= 12u && w.longest <= 100.0f);
    TIDE_CHECK(s.verified_tick + 10u > tide_server_tick(w.server));
    leave(&w);
}
