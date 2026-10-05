#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

// The tick that's coming (tide_client_desc.peek): a client runs the tick its
// clock hasn't reached, with this machine's input as it is, and views blend
// towards it, so what a player does shows in the frame that reads it. Without
// it, views blend between the last two ticks run: a tick ago, and a key shows
// a tick or two after it's read.
//
// Frames are a 60th of a second, in made-up time. The key is a button as the
// devices keep one (tide_button): pressed from when it goes down until a
// sample has seen it up, so a tick it was down in part of counts it down.

static bool key_down; // As it is
static bool pressed;  // ...and as the next sample has it
static int peeks;

static void set_key(const bool down)
{
    key_down = down;
    pressed |= down;
}

static void peek(void *user, const uint32_t tick, void *input)
{
    (void)user, (void)tick;
    peeks++;
    *(Pad *)input = (Pad){.held = pressed};
}

static void sample(void *user, const uint32_t tick, void *input)
{
    (void)user, (void)tick;
    *(Pad *)input = (Pad){.held = pressed};
    pressed = key_down; // tide_devices_consume
}

// A game that counts the ticks run on it.
static int ticks_run;

static void counted_tick(void *world, const tide_jobs *jobs)
{
    ticks_run++;
    tide_game_api.tick(world, jobs);
}

static tide_game counted;

// What a view shows of the meter: its worlds' values, blended as views blend.
typedef struct shown {
    float x;
    float walked;
} shown;

static shown shown_by(const tide_view_worlds v)
{
    if (!v.current) return (shown){0.0f, 0.0f}; // Not in the match yet
    const Meter *now = &((const tide_world *)v.current)->Meter;
    if (!v.previous) return (shown){now->x, now->walked};
    const Meter *before = &((const tide_world *)v.previous)->Meter;
    return (shown){before->x + (now->x - before->x) * v.alpha, before->walked + (now->walked - before->walked) * v.alpha};
}

static bool close_to(const float a, const float b)
{
    return a - b < 1e-3f && b - a < 1e-3f;
}

#define FRAME (1.0 / 60.0)
#define FRAMES 100
#define PRESS 40   // The frame the key goes down at
#define RELEASE 70 // ...and up

// A machine alone, which runs the server too: a match of `rate` ticks a
// second, with the key down from PRESS to RELEASE. What each frame's view
// showed goes into `frames`.
static void play_alone(const uint32_t rate, const bool peeking, shown frames[FRAMES], tide_session_status *status,
                       int *presses)
{
    counted = tide_game_api;
    counted.tick = counted_tick;
    key_down = pressed = false;
    peeks = 0;
    ticks_run = 0;
    tide_session *s = tide_session_create(&(tide_session_desc){.game = &counted, .tick_rate = rate, .sample = sample,
                                                                .peek = peeking ? peek : NULL});
    double now = 0.0;
    tide_session_start(s, NULL, now);
    for (int frame = 0; frame < FRAMES; frame++) {
        now += FRAME;
        set_key(frame >= PRESS && frame < RELEASE);
        tide_session_update(s, now);
        frames[frame] = shown_by(tide_session_view(s));
    }
    *status = tide_session_status_of(s);
    *presses = ((const tide_world *)tide_session_server_world(s))->Meter.presses;
    tide_session_destroy(s);
}

TIDE_TEST(coming_shows_what_a_player_does_in_the_frame_that_reads_it)
{
    const uint32_t rates[] = {120, 60, 30, 20};
    for (int r = 0; r < 4; r++) {
        const uint32_t rate = rates[r];
        const float tick = 1.0f / (float)rate;
        shown frames[FRAMES];
        tide_session_status status;
        int presses = 0;
        play_alone(rate, true, frames, &status, &presses);
        TIDE_CHECK(status.client.state == TIDE_SESSION_CONNECTED && status.client.resyncs == 0);
        TIDE_CHECK(status.client.again_ticks == 0); // Nothing went otherwise than this machine said
        TIDE_CHECK(presses == 1);
        TIDE_CHECK(peeks > 0);

        // Nothing before the key, and something in the frame that reads it.
        // A match of whole ticks counts the key down since the tick began, so
        // it's as much as that tick has gone by, at the most a tick and what
        // the frame took
        TIDE_CHECK(frames[PRESS - 1].x == 0.0f && frames[PRESS - 1].walked == 0.0f);
        TIDE_CHECK(frames[PRESS].x > 1.0f);
        TIDE_CHECK(frames[PRESS].walked > 1e-3f && frames[PRESS].walked <= tick + (float)FRAME + 1e-3f);
        // ...and a frame's walk more each frame from then on, while it's held:
        // no wait, then no catching up
        for (int frame = PRESS + 1; frame < RELEASE; frame++) {
            TIDE_CHECK(close_to(frames[frame].walked - frames[frame - 1].walked, (float)FRAME));
        }
        // All of it once the tick it was read in is over
        const int whole = PRESS + (int)(60u / rate) + 1;
        TIDE_CHECK(close_to(frames[whole].x, 100.0f));
        // Let go, it walks to the end of the tick it was held in, stops, and never goes back
        for (int frame = RELEASE; frame < FRAMES; frame++) TIDE_CHECK(frames[frame].walked >= frames[frame - 1].walked - 1e-6f);
        const int stopped = RELEASE + (int)(60u / rate) + 1;
        TIDE_CHECK(close_to(frames[FRAMES - 1].walked, frames[stopped].walked) && close_to(frames[FRAMES - 1].x, 0.0f));
        // It walked as long as the match says the key was down: whole ticks
        const float held_for = (float)(RELEASE - PRESS) * (float)FRAME;
        TIDE_CHECK(frames[FRAMES - 1].walked >= held_for - 1e-3f && frames[FRAMES - 1].walked <= held_for + tick + 1e-3f);
        // A tick is run once, as ever, only sooner: by the client, and by the
        // server. The few more are the ticks the key changed in, run again.
        const int ticks = (int)status.client.predicted_tick;
        TIDE_CHECK(ticks_run >= 2 * ticks && ticks_run <= 2 * ticks + 8);
    }
}

// Without it, as ever: the key shows a tick or two after it's read.
TIDE_TEST(coming_is_what_takes_the_wait_out)
{
    shown frames[FRAMES];
    tide_session_status status;
    int presses = 0;
    play_alone(60, false, frames, &status, &presses);
    TIDE_CHECK(presses == 1 && peeks == 0);
    TIDE_CHECK(frames[PRESS].x < 1.0f && frames[PRESS].walked < 1e-3f); // Read, and not shown
    TIDE_CHECK(close_to(frames[PRESS + 1].x, 100.0f));
    play_alone(20, false, frames, &status, &presses);
    TIDE_CHECK(frames[PRESS].x < 1.0f && frames[PRESS + 1].x < 99.0f && frames[PRESS + 2].x < 99.0f);
    TIDE_CHECK(close_to(frames[PRESS + 5].x, 100.0f));
}

// Over a network that loses and delays, with a key that changes at any frame,
// between a tick's peeks and its sample too: every tick the client ran is the
// server's, by its hash, though most were run before their time, and some
// again.
static uint32_t noise = 12345u;

static bool flip(void)
{
    noise = noise * 1664525u + 1013904223u;
    return (noise >> 24) % 5u == 0;
}

TIDE_TEST(coming_ticks_are_the_servers_over_a_network)
{
    const uint32_t rates[] = {60, 20};
    for (int r = 0; r < 2; r++) {
        tide_loopback *net = tide_loopback_create(9);
        tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.03, .jitter = 0.01, .loss = 0.05});
        double now = 0.0;
        tide_server *server = tide_server_create(&(tide_server_desc){.game = &tide_game_api, .tick_rate = rates[r],
                                                                      .transports = {tide_loopback_endpoint(net, 1)}},
                                                 now);
        key_down = pressed = false;
        peeks = 0;
        tide_client *client = tide_client_create(&(tide_client_desc){.game = &tide_game_api,
                                                                     .transport = tide_loopback_endpoint(net, 2),
                                                                     .server = tide_loopback_address(1),
                                                                     .sample = sample, .peek = peek, .lead = 2},
                                                 now);
        float walked = 0.0f;
        for (int frame = 0; frame < 1200; frame++) {
            now += FRAME;
            if (flip()) set_key(!key_down);
            else set_key(key_down);
            tide_loopback_set_time(net, now);
            tide_client_update(client, now);
            tide_server_update(server, now);
            walked = shown_by(tide_client_view(client)).walked;
        }
        const tide_client_status status = tide_client_status_of(client);
        TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(status.resyncs == 0);
        TIDE_CHECK(peeks > 0 && walked > 1.0f);
        // What views draw is a tick past the last it ran: the one that's coming
        const tide_view_worlds view = tide_client_view(client);
        TIDE_CHECK(((const tide_world *)view.current)->Time.tick == ((const tide_world *)tide_client_world(client))->Time.tick + 1);
        TIDE_CHECK(view.previous == tide_client_world(client));
        tide_client_destroy(client);
        tide_server_destroy(server);
        tide_loopback_destroy(net);
    }
}
