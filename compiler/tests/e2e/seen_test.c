#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

// How late other players see what one does: from the frame a player's key is read to the frame
// another player's view shows it. A server and two players on a loopback network, in simulated
// time, frames a 60th of a second.
//
// Between the two are the network and what the engine adds: the wait for a tick to sample the
// key, how early a client's inputs are sent to be in time (the server holds each until its tick),
// the server's own frame, and the watching player's. Inputs wait a frame at the server at the least.

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
    double early;       // How long the pressing player's inputs wait at the server, in milliseconds, on the whole
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
                                                                       .sample = sample, .user = &players[i], .lead = 1,
                                                                       .lead_frame = true},
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
            early += (double)tide_client_status_of(players[0].client).early_ms;
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

// Frames, on the whole, at each tick rate and network. These are the most they may be: a change
// that makes players see each other later fails here, and one that makes it sooner lowers them.
TIDE_TEST(seen_how_late_other_players_see_you)
{
    const uint32_t rates[] = {60, 30, 20};
    const double latencies[] = {0.0, 0.03};
    const double most[3][2] = {{2.0, 5.0}, {2.5, 5.5}, {3.0, 6.1}};
    for (int r = 0; r < 3; r++) {
        for (int l = 0; l < 2; l++) {
            const seen s = measure(rates[r], latencies[l]);
            TIDE_CHECK(s.resyncs == 0);
            TIDE_CHECK(s.late == 0); // As early as they have to be: none too late
            TIDE_CHECK(s.frames <= most[r][l] + 0.01);
            // A frame at the least, and what a datagram that comes between two of the server's frames adds
            TIDE_CHECK(s.early >= 16.6 && s.early <= 21.0);
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
                                                        .lead = 1, .lead_frame = true},
                                    now);
    uint32_t late_before = 0;
    float least = 1000.0f, most = -1000.0f;
    for (int frame = 0; frame < 3600; frame++) {
        now += FRAME;
        tide_loopback_set_time(net, now);
        one.key = frame / 7 % 2 == 0;
        tide_client_update(one.client, now);
        tide_server_update(server, now);
        const tide_client_status status = tide_client_status_of(one.client);
        if (frame == 600) late_before = status.late_ticks;
        if (frame < 600) continue; // Its first ten seconds, as it finds out how uneven the network is
        if (status.early_ms < least) least = status.early_ms;
        if (status.early_ms > most) most = status.early_ms;
    }
    const tide_client_status status = tide_client_status_of(one.client);
    TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED && status.resyncs == 0);
    TIDE_CHECK(status.late_ticks - late_before <= 2u); // Up to 40 ms more for a datagram is 3 ticks: it went that early
    TIDE_CHECK(least >= 0.0f && most <= 100.0f);
    tide_client_destroy(one.client);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}

// One player in a match of 20 ticks a second, on a network 30 ms each way,
// whose frames come as `every` says: how long its inputs waited at the least
// once its clock had settled, and how many came too late.
typedef struct waited {
    float least;
    uint32_t late;
    uint32_t resyncs;
} waited;

static waited wait_with_frames(bool (*every)(int frame))
{
    tide_loopback *net = tide_loopback_create(7);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.03});
    double now = 0.0;
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &tide_game_api, .tick_rate = 20,
                                                                  .transports = {tide_loopback_endpoint(net, 1)}},
                                             now);
    player one = {0};
    one.client = tide_client_create(&(tide_client_desc){.game = &tide_game_api, .transport = tide_loopback_endpoint(net, 2),
                                                        .server = tide_loopback_address(1), .sample = sample, .user = &one,
                                                        .lead = 1, .lead_frame = true},
                                    now);
    waited result = {.least = 1000.0f};
    uint32_t late_before = 0;
    for (int frame = 0; frame < 3600; frame++) {
        now += FRAME;
        tide_loopback_set_time(net, now);
        one.key = frame / 7 % 2 == 0;
        if (frame < 60 || every(frame)) tide_client_update(one.client, now);
        tide_server_update(server, now);
        const tide_client_status status = tide_client_status_of(one.client);
        if (frame == 1200) late_before = status.late_ticks;
        if (frame >= 1200 && status.early_ms < result.least) result.least = status.early_ms;
    }
    const tide_client_status status = tide_client_status_of(one.client);
    TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED);
    result.late = status.late_ticks - late_before;
    result.resyncs = status.resyncs;
    tide_client_destroy(one.client);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
    return result;
}

// One player in a match of 60 ticks a second, on a network 30 ms each way,
// settled, and then one of the two machines stops for a while: `frames` of the
// player's, or of the server's. What the player's updates ran from then on,
// how far ahead of what's confirmed it was, and how long the server said its
// inputs waited, each `after` frames after both run again.
typedef struct stopped {
    uint32_t most;      // The most ticks one update ran, from when both ran again
    uint32_t furthest;  // The furthest it was ahead of what's confirmed, `after` frames later and on
    float longest;      // ...and the longest its inputs waited, and the least, in milliseconds
    float least;
    uint32_t late;      // Ticks counted as late, from when both ran again
    uint32_t again;     // ...and predicted ticks run again
    uint32_t resyncs;
    bool caught_up;     // Level with the server at the end
} stopped;

static stopped stop_for(const int frames, const bool server_stops, const int after)
{
    tide_loopback *net = tide_loopback_create(13);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.03});
    double now = 0.0;
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &tide_game_api, .tick_rate = 60,
                                                                  .transports = {tide_loopback_endpoint(net, 1)}},
                                             now);
    player one = {0};
    one.client = tide_client_create(&(tide_client_desc){.game = &tide_game_api, .transport = tide_loopback_endpoint(net, 2),
                                                        .server = tide_loopback_address(1), .sample = sample, .user = &one,
                                                        .lead = 1, .lead_frame = true},
                                    now);
    stopped result = {.least = 1000.0f};
    tide_client_status back = {0};
    const int stop = 600; // Ten seconds in: its clock has settled
    for (int frame = 0; frame < stop + frames + 600; frame++) {
        now += FRAME;
        tide_loopback_set_time(net, now);
        one.key = frame / 7 % 2 == 0;
        const bool stopped_now = frame >= stop && frame < stop + frames;
        const tide_client_status before = tide_client_status_of(one.client);
        if (!stopped_now || server_stops) tide_client_update(one.client, now);
        if (!stopped_now || !server_stops) tide_server_update(server, now);
        if (frame < stop + frames) {
            back = tide_client_status_of(one.client);
            continue;
        }
        const tide_client_status status = tide_client_status_of(one.client);
        if (status.run_ticks - before.run_ticks > result.most) result.most = status.run_ticks - before.run_ticks;
        if (frame < stop + frames + after) continue;
        if (status.predicted_tick - status.verified_tick > result.furthest) result.furthest = status.predicted_tick - status.verified_tick;
        if (status.early_ms > result.longest) result.longest = status.early_ms;
        if (status.early_ms < result.least) result.least = status.early_ms;
    }
    const tide_client_status status = tide_client_status_of(one.client);
    TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED);
    result.late = status.late_ticks - back.late_ticks;
    result.again = status.again_ticks - back.again_ticks;
    result.resyncs = status.resyncs;
    result.caught_up = status.verified_tick + 6u > tide_server_tick(server);
    tide_client_destroy(one.client);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
    return result;
}

// A player's machine stops for two seconds (a breakpoint, a tab that slept) and the match goes
// on. Back, it has 120 server ticks to run, 8 an update, with its clock where it should be; it
// predicts nothing until it has, and samples no input for the ticks it slept through. Those
// aren't inputs that came late, though the server says for a while that they are two seconds
// late; taking that at its word would put the player's clock two seconds ahead, past what it can sample.
TIDE_TEST(seen_a_machine_that_stopped_catches_up_and_keeps_its_clock)
{
    const stopped s = stop_for(120, false, 60);
    printf("    back after two seconds, the most a player's update ran was %u ticks; a second later and on it was %u ticks ahead"
           " at the most, its inputs waiting %.0f to %.0f ms; %u counted as late, %u run again\n",
           s.most, s.furthest, (double)s.least, (double)s.longest, s.late, s.again);
    TIDE_CHECK(s.resyncs == 0 && s.caught_up);
    TIDE_CHECK(s.most <= 16u);
    TIDE_CHECK(s.furthest <= 8u); // A round trip here is 4 ticks, and its inputs wait a frame
    TIDE_CHECK(s.least >= 0.0f && s.longest <= 40.0f);
    TIDE_CHECK(s.late <= 1u && s.again == 0);
}

// The server's machine stops for three seconds and drops the time: its ticks are three seconds
// behind the player's clock, past what the player can sample. The server only says the player's
// inputs wait about a second, so the player's clock must come back at once, as soon as the server
// reports its tick: a clock set by that is within what it samples. The inputs already sampled
// for the second to come are the server's, so inputs wait as ever once those have run.
TIDE_TEST(seen_a_clock_far_ahead_comes_back_at_once)
{
    const stopped soon = stop_for(180, true, 15);
    const stopped s = stop_for(180, true, 90);
    printf("    the server back after three seconds, a player was %u ticks ahead of what's confirmed at the most a quarter of a"
           " second later and on; from a second and a half later, its inputs waited %.0f to %.0f ms\n",
           soon.furthest, (double)s.least, (double)s.longest);
    TIDE_CHECK(s.resyncs == 0 && s.caught_up);
    TIDE_CHECK(soon.furthest <= 8u);
    TIDE_CHECK(s.furthest <= 8u);
    TIDE_CHECK(s.least >= 0.0f && s.longest <= 40.0f);
}

typedef struct recovering_player {
    bool key;
    int frame;
    int sampled_at;
    int sample_gap;
    uint32_t sampled_tick;
    bool sampled;
} recovering_player;

static void sample_recovering(void *user, const uint32_t tick, void *input)
{
    recovering_player *p = user;
    if (p->sampled) TIDE_CHECK(tick > p->sampled_tick); // Sent inputs must never be replaced or sampled again
    if (p->sampled && p->frame >= 600 && p->frame - p->sampled_at > p->sample_gap) {
        p->sample_gap = p->frame - p->sampled_at;
    }
    p->sampled = true;
    p->sampled_tick = tick;
    p->sampled_at = p->frame;
    *(Pad *)input = (Pad){.held = p->key};
}

// A half-second server pause is short enough that the client can still sample every input.
// Recover its excess lead promptly without turning that remote lag into a local input freeze.
// Keep changing the key through recovery, then measure a fresh press two seconds after resuming.
// Repeat on the same connection to catch leftover correction state and tick-phase differences.
TIDE_TEST(seen_a_short_server_stall_recovers_without_freezing_own_input)
{
    const uint32_t rates[] = {60, 30};
    const tide_net_conditions conditions[] = {{.latency = 0.03}, {.latency = 0.08, .jitter = 0.01, .loss = 0.03}};
    for (uint32_t r = 0; r < 2; r++) {
        for (uint32_t network = 0; network < 2; network++) {
            const uint32_t rate = rates[r];
            tide_loopback *net = tide_loopback_create(42);
            tide_loopback_set_conditions(net, conditions[network]);
            tide_server *server = tide_server_create(&(tide_server_desc){.game = &tide_game_api, .tick_rate = rate,
                .transports = {tide_loopback_endpoint(net, 1)}}, 0.0);
            recovering_player p = {0};
            tide_client *client = tide_client_create(&(tide_client_desc){.game = &tide_game_api,
                .transport = tide_loopback_endpoint(net, 2), .server = tide_loopback_address(1),
                .sample = sample_recovering, .user = &p, .lead = 1, .lead_frame = true}, 0.0);
            TIDE_REQUIRE(server && client);
            double now = 0.0;
            for (int episode = 0; episode < 2; episode++) {
                const int stop = 600 + episode * 600, resume = stop + 30, press = resume + 120;
                int own_pending = -1, own_most = 0, server_seen = -1;
                float early_most = 0.0f;
                uint32_t late_before = 0;
                uint32_t predicted_before = 0;
                for (int frame = episode ? 930 : 0; frame < resume + 300; frame++) {
                    now += FRAME;
                    tide_loopback_set_time(net, now);
                    p.frame = frame;
                    const bool key = frame < resume + 90 ? frame / 13 % 2 == 0 : frame >= press;
                    if (key != p.key && frame >= stop) {
                        TIDE_CHECK(own_pending < 0);
                        own_pending = frame;
                    }
                    p.key = key;
                    tide_client_update(client, now);
                    if (frame < stop || frame >= resume) tide_server_update(server, now);
                    const tide_client_status status = tide_client_status_of(client);
                    if (frame == stop - 1) late_before = status.late_ticks;
                    if (frame >= stop) {
                        TIDE_CHECK(status.predicted_tick >= predicted_before);
                        TIDE_CHECK(status.delay_ticks == 0);
                        if (own_pending >= 0) {
                            const float x = shown(tide_client_view(client));
                            if ((key && x > 0.0f) || (!key && x < 100.0f)) {
                                const int took = frame - own_pending;
                                if (took > own_most) own_most = took;
                                own_pending = -1;
                            }
                        }
                    }
                    predicted_before = status.predicted_tick;
                    const float server_x = ((const tide_world *)tide_server_world(server))->Meter.first_x;
                    if (frame == press - 1) TIDE_CHECK(server_x == 0.0f);
                    if (frame >= press) {
                        if (server_seen < 0 && server_x > 0.0f) server_seen = frame;
                        if (status.early_ms > early_most) early_most = status.early_ms;
                    }
                }
                const tide_client_status status = tide_client_status_of(client);
                printf("    %u Hz, %.0f ms/way, stall %d: fresh press took %d frames at server;"
                       " own response %d, sample gap %d frames; later input wait <=%.0f ms, %u late\n",
                       rate, conditions[network].latency * 1000.0, episode + 1, server_seen - press, own_most, p.sample_gap,
                       (double)early_most, status.late_ticks - late_before);
                TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED && status.resyncs == 0);
                TIDE_CHECK(status.verified_tick + rate / 4u > tide_server_tick(server));
                TIDE_CHECK(own_pending < 0 && own_most <= (int)(180u / rate));
                TIDE_CHECK(p.sample_gap <= (int)(120u / rate) + 1);
                TIDE_CHECK(server_seen >= press && server_seen - press <= 9 + (int)(180u / rate));
                TIDE_CHECK(early_most <= 150.0f);
                TIDE_CHECK(status.late_ticks - late_before <= (network ? 4u : 0u));
            }
            tide_client_destroy(client);
            tide_server_destroy(server);
            tide_loopback_destroy(net);
        }
    }
}

// A connection slower than the prediction window isn't a server whose clock stopped: its answers
// took that long too, so a clock set from them would still be past the window, and the existing
// clock mustn't come back. Fresh packets passing older ones still on their way must not move
// the server's last known tick back.
TIDE_TEST(seen_delayed_and_reordered_answers_do_not_reset_a_running_clock)
{
    tide_loopback *net = tide_loopback_create(14);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.03});
    double now = 0.0;
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &tide_game_api, .tick_rate = 60,
                                                                  .transports = {tide_loopback_endpoint(net, 1)}}, now);
    player one = {0};
    one.client = tide_client_create(&(tide_client_desc){.game = &tide_game_api, .transport = tide_loopback_endpoint(net, 2),
                                                        .server = tide_loopback_address(1), .sample = sample, .user = &one,
                                                        .lead = 1, .lead_frame = true}, now);
    TIDE_REQUIRE(server && one.client);
    uint32_t furthest = 0;
    uint32_t longest_answer = 0;
    uint32_t heard = 0;
    for (int frame = 0; frame < 1200; frame++) {
        if (frame == 600) tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 1.2, .jitter = 0.15});
        if (frame == 900) tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.03});
        now += FRAME;
        tide_loopback_set_time(net, now);
        const tide_client_status before = tide_client_status_of(one.client);
        tide_client_update(one.client, now);
        tide_server_update(server, now);
        const tide_client_status status = tide_client_status_of(one.client);
        TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED || frame < SETTLE);
        TIDE_CHECK(status.server_tick >= heard);
        heard = status.server_tick;
        if (frame >= 600 && frame < 900) {
            TIDE_CHECK(status.predicted_tick >= before.predicted_tick);
            const uint32_t ahead = status.predicted_tick - status.verified_tick;
            if (ahead > furthest) furthest = ahead;
            if (status.answer_ms > longest_answer) longest_answer = status.answer_ms;
        }
    }
    const tide_client_status status = tide_client_status_of(one.client);
    printf("    with delayed and reordered answers, a running client reached %u ticks ahead of what's confirmed, its longest"
           " answer took %u ms, and its clock never came back\n", furthest, longest_answer);
    TIDE_CHECK(furthest >= 60u && longest_answer >= 2400u);
    TIDE_CHECK(status.resyncs == 0 && status.verified_tick + 6u > tide_server_tick(server));
    tide_client_destroy(one.client);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}

static bool every_other(const int frame) { return frame % 2 == 0; }
static bool but_one_in_97(const int frame) { return frame % 97 != 0; }

// A machine with longer frames keeps its inputs one of its own frames early (a 30th of a second at 30 fps).
TIDE_TEST(seen_inputs_wait_a_frame_of_the_machine_that_sends_them)
{
    const waited w = wait_with_frames(every_other);
    TIDE_CHECK(w.resyncs == 0 && w.late == 0);
    TIDE_CHECK(w.least >= 33.0f && w.least <= 50.0f); // Never a tick, which is 50 ms here
}

// A frame that never came is what the wait is for: its input goes a frame later, still in time.
TIDE_TEST(seen_a_frame_that_never_came_makes_no_input_late)
{
    const waited w = wait_with_frames(but_one_in_97);
    TIDE_CHECK(w.resyncs == 0 && w.late == 0);
    TIDE_CHECK(w.least >= 0.0f && w.least <= 50.0f);
}
