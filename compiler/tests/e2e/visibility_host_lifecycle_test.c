#include "game.h"
#include "tide_test.h"

#include <stdio.h>

// A match that hides state, played by a host and a guest over a loopback.
// The host's player reads the server's world (mirror_tick in
// engine/src/session.c) and sees the whole match, so a piece goes from it only
// as the match destroys it: its local handlers hear Destroyed, never Hidden.
// The guest hears Destroyed for what it saw destroyed and Hidden for what was
// hidden from it, a piece hidden and destroyed in one tick included, as it
// couldn't see it at its destruction; but for one its own run of the tick
// destroyed (kind 5), which its predicted handlers hear as it ran it. Forgotten
// comes once on each machine for each piece that leaves it. The host hears the
// hidden clock's changes, the guest none.

// The ticks the pieces come, are hidden and go at, as the game has them
enum { SPAWN = 120, HIDE = 180, GO = 240 };

static tide_draw_list draw;
static tide_gui gui;

static void sample(void *user, const uint32_t tick, void *input)
{
    (void)user;
    (void)tick;
    (void)input;
}

static void frame(tide_session *s, tide_local *local)
{
    tide_local_events(local, tide_session_events(s));
    const tide_view_worlds view = tide_session_view(s);
    if (!view.current) return;
    tide_draw_reset(&draw);
    tide_frame(view.current, view.previous, view.alpha, view.verified, view.verified_previous, view.verified_alpha, local,
               &draw, &gui);
}

typedef struct heard_by {
    Heard host;
    Heard guest;
    Heard host_before; // ...just before the pieces go, after two were hidden
    Heard guest_before;
    int ticks; // The match's, each one a change of its clock
} heard_by;

static heard_by play(const tide_net_conditions conditions)
{
    heard_by h = {0};
    static tide_local host, guest;
    tide_local_init(&host);
    tide_local_init(&guest);
    tide_loopback *net = tide_loopback_create(3);
    tide_loopback_set_conditions(net, conditions);
    tide_session *s = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .tick_rate = 60, .sample = sample});
    tide_session *other = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .tick_rate = 60, .sample = sample});
    double t = 0.0;
    tide_session_events(s); // Before their first ticks, so they record from there
    tide_session_events(other);
    tide_session_start(s, NULL, t);
    TIDE_CHECK(tide_session_open(s, tide_loopback_endpoint(net, 1)));
    tide_session_join(other, tide_loopback_endpoint(net, 2), tide_loopback_address(1), t);
    bool before = false;
    for (int n = 0; n < 2000; n++) {
        // Frames a little apart, so some updates run no tick and some two
        t += n % 3 == 0 ? 0.6 / 60.0 : 1.2 / 60.0;
        tide_loopback_set_time(net, t);
        tide_session_update(s, t);
        tide_session_update(other, t);
        frame(s, &host);
        frame(other, &guest);
        const tide_world *server = tide_session_server_world(s);
        if (n == 0) {
            TIDE_CHECK(server->Time.tick < SPAWN);
        }
        // Both have heard the hiding by now, and nothing has gone yet
        if (!before && server->Time.tick >= (HIDE + GO) / 2) {
            before = true;
            h.host_before = host.Heard;
            h.guest_before = guest.Heard;
        }
        if (server->Time.tick >= GO + 90) break;
    }
    TIDE_CHECK(before);
    TIDE_CHECK(((const tide_world *)tide_session_server_world(s))->Time.tick >= GO + 90);
    const tide_session_status mine = tide_session_status_of(s), theirs = tide_session_status_of(other);
    TIDE_CHECK(mine.client.state == TIDE_SESSION_CONNECTED && theirs.client.state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(mine.client.resyncs == 0 && theirs.client.resyncs == 0);
    h.host = host.Heard;
    h.guest = guest.Heard;
    h.ticks = ((const tide_world *)tide_session_server_world(s))->Time.tick;
    tide_session_destroy(other);
    tide_session_destroy(s);
    tide_loopback_destroy(net);
    tide_local_free(&guest);
    tide_local_free(&host);
    return h;
}

static void print(const char *who, const Heard *h)
{
    printf("%s: spawned %d/%d shown %d/%d hidden %d/%d destroyed %d/%d forgotten %d changes %d/%d\n", who, h->spawned,
           h->sureSpawned, h->shown, h->sureShown, h->hidden, h->sureHidden, h->destroyed, h->sureDestroyed, h->forgotten,
           h->changes, h->sureChanges);
}

static void check(const heard_by *h)
{
    print("host before", &h->host_before);
    print("guest before", &h->guest_before);
    print("host", &h->host);
    print("guest", &h->guest);
    // Every piece was spawned, and shown, on both machines, predicted and verified
    TIDE_CHECK(h->host.spawned == 11111 && h->host.sureSpawned == 11111);
    TIDE_CHECK(h->guest.spawned == 11111 && h->guest.sureSpawned == 11111);
    TIDE_CHECK(h->host.shown == 11111 && h->host.sureShown == 11111);
    TIDE_CHECK(h->guest.shown == 11111 && h->guest.sureShown == 11111);
    // Hiding 2 and 4 told the guest and not the host, which sees the whole match
    TIDE_CHECK(h->host_before.hidden == 0 && h->host_before.sureHidden == 0 && h->host_before.forgotten == 0);
    TIDE_CHECK(h->guest_before.hidden == 1010 && h->guest_before.sureHidden == 1010 && h->guest_before.forgotten == 1010);
    TIDE_CHECK(h->host_before.destroyed == 0 && h->guest_before.destroyed == 0);
    // The host hears 1, 2, 3 and 5 destroyed, and none of them hidden
    TIDE_CHECK(h->host.destroyed == 10111 && h->host.sureDestroyed == 10111);
    TIDE_CHECK(h->host.hidden == 0 && h->host.sureHidden == 0);
    TIDE_CHECK(h->host.forgotten == 10111);
    // The guest hears 1 destroyed, as it saw it go, and 3 hidden, as it went from its sight with its group:
    // nothing of 2's destruction, which it couldn't see. 5 is destroyed as its own tick ran it, and hidden as
    // the server confirmed it
    TIDE_CHECK(h->guest.destroyed == 10001 && h->guest.sureDestroyed == 1);
    TIDE_CHECK(h->guest.hidden == 1110 && h->guest.sureHidden == 11110);
    TIDE_CHECK(h->guest.forgotten == 11111);
    // Each tick the clock counted, heard once by the host from its first, and never by the guest
    TIDE_CHECK(h->host.changes == h->ticks && h->host.sureChanges == h->ticks);
    TIDE_CHECK(h->guest.changes == 0 && h->guest.sureChanges == 0);
}

TIDE_TEST(visibility_host_hears_destruction_and_a_guest_what_it_saw)
{
    const heard_by h = play((tide_net_conditions){.latency = 0.02, .jitter = 0.005});
    check(&h);
}

// The same over a lossy network: what's heard doesn't depend on when the guest's repairs come
TIDE_TEST(visibility_host_hears_destruction_and_a_guest_what_it_saw_over_loss)
{
    const heard_by h = play((tide_net_conditions){.latency = 0.03, .jitter = 0.01, .loss = 0.05});
    check(&h);
}
