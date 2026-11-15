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
//
// Kind 5's counts pin a Provisional pick (docs/spec.md, State visibility,
// 2026-11-15) that's the owner's to decide: a guest's predicted handlers hear
// the Destroyed its own run made, its verified ones Hidden. A decision
// otherwise changes those checks deliberately; they're marked below.

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

static void frame(tide_session *s, tide_local *local, const bool server, const bool takes)
{
    // Who this machine's player is, for `with ownership`, as tide/run.h tells local code
    const tide_session_status status = tide_session_status_of(s);
    tide_local_set_session(local, status.client.state, status.client.player, 0, server, server, "");
    if (takes) tide_local_events(local, tide_session_events(s));
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

// `late`: the host takes what its handlers hear only from tick 60, after the first preparation
static heard_by play(const tide_net_conditions conditions, const bool late)
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
    if (!late) tide_session_events(s); // Before their first ticks, so they record from there
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
        const tide_world *server = tide_session_server_world(s);
        frame(s, &host, true, !late || server->Time.tick >= 60);
        frame(other, &guest, false, true);
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
    // The guest takes the server's prepared world once, for the preparation in play (notes/netcode.md, Scene
    // preparation), and kept up through it: asking for that world after ticks it had heard of left it waiting for
    // good at the preparation's tick
    TIDE_CHECK(mine.client.resyncs == 0 && theirs.client.resyncs == 1);
    TIDE_CHECK(theirs.client.verified_tick + 30u >= mine.client.verified_tick);
    // Pieces 1 and 3 are PlayerID(0)'s, 5 PlayerID(1)'s
    TIDE_CHECK(tide_player_equal(mine.client.player, tide_player_from_index(0)));
    TIDE_CHECK(tide_player_equal(theirs.client.player, tide_player_from_index(1)));
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
    printf("    mine destroyed %d, rows %d in %d, mark shown %d hidden %d/%d, prepared %d/%d shown %d\n", h->destroyedMine,
           h->destroyedRows, h->destructions, h->markShown, h->markHidden, h->sureMarkHidden, h->prepared, h->surePrepared,
           h->preparedShown);
}

static void check(const heard_by *h)
{
    print("host before", &h->host_before);
    print("guest before", &h->guest_before);
    print("host", &h->host);
    print("guest", &h->guest);
    // Every piece was spawned, and shown, on both machines, predicted and verified
    TIDE_CHECK(h->host.spawned == 111111 && h->host.sureSpawned == 111111);
    TIDE_CHECK(h->guest.spawned == 111111 && h->guest.sureSpawned == 111111);
    TIDE_CHECK(h->host.shown == 111111 && h->host.sureShown == 111111);
    TIDE_CHECK(h->guest.shown == 111111 && h->guest.sureShown == 111111);
    // Hiding 2 and 4 told the guest and not the host, which sees the whole match
    TIDE_CHECK(h->host_before.hidden == 0 && h->host_before.sureHidden == 0 && h->host_before.forgotten == 0);
    TIDE_CHECK(h->guest_before.hidden == 1010 && h->guest_before.sureHidden == 1010 && h->guest_before.forgotten == 1010);
    TIDE_CHECK(h->host_before.destroyed == 0 && h->guest_before.destroyed == 0);
    // ...and 6's Mark going was Hidden for Mark's handlers alone, on both: the host's one Hidden
    TIDE_CHECK(h->host_before.markShown == 1 && h->host_before.markHidden == 1 && h->host_before.sureMarkHidden == 1);
    TIDE_CHECK(h->guest_before.markShown == 1 && h->guest_before.markHidden == 1 && h->guest_before.sureMarkHidden == 1);
    TIDE_CHECK(h->host.markHidden == 1 && h->host.sureMarkHidden == 1 && h->guest.markHidden == 1 && h->guest.sureMarkHidden == 1);
    // The host hears 1, 2, 3 and 5 destroyed, and none of them hidden
    TIDE_CHECK(h->host.destroyed == 10111 && h->host.sureDestroyed == 10111);
    TIDE_CHECK(h->host.hidden == 0 && h->host.sureHidden == 0);
    TIDE_CHECK(h->host.forgotten == 10111);
    // ...1 and 3 its player's (`with ownership`, its owner unscrubbed), and each Destroyed's query has what's left
    // after the tick, 4 and 6
    TIDE_CHECK(h->host.destroyedMine == 101);
    TIDE_CHECK(h->host.destructions == 4 && h->host.destroyedRows == 4 * 101000);
    // The guest hears 1 destroyed, as it saw it go, and 3 hidden, as it went from its sight with its group:
    // nothing of 2's destruction, which it couldn't see
    TIDE_CHECK(h->guest.destroyed % 10000 == 1 && h->guest.sureDestroyed == 1);
    TIDE_CHECK(h->guest.hidden % 10000 == 1110 && h->guest.sureHidden % 10000 == 1110);
    TIDE_CHECK(h->guest.forgotten == 11111);
    // Provisional pick (kind 5): destroyed as its own tick ran it, its player's, and hidden as the server
    // confirmed it
    TIDE_CHECK(h->guest.destroyed / 10000 == 1 && h->guest.sureDestroyed / 10000 == 0);
    TIDE_CHECK(h->guest.hidden / 10000 == 0 && h->guest.sureHidden / 10000 == 1);
    TIDE_CHECK(h->guest.destroyedMine == 10000 && h->guest.destructions == 2);
    // Each tick the clock counted, heard once by the host from its first, and never by the guest
    TIDE_CHECK(h->host.changes == h->ticks && h->host.sureChanges == h->ticks);
    TIDE_CHECK(h->guest.changes == 0 && h->guest.sureChanges == 0);
    // What both preparations spawned: Spawned once on the host, predicted and verified, and Shown on both
    TIDE_CHECK(h->host.prepared == 11 && h->host.surePrepared == 11);
    TIDE_CHECK(h->host.preparedShown == 11 && h->guest.preparedShown == 11);
}

TIDE_TEST(visibility_host_hears_destruction_and_a_guest_what_it_saw)
{
    const heard_by h = play((tide_net_conditions){.latency = 0.02, .jitter = 0.005}, false);
    check(&h);
}

// The same over a lossy network: what's heard doesn't depend on when the guest's repairs come
TIDE_TEST(visibility_host_hears_destruction_and_a_guest_what_it_saw_over_loss)
{
    const heard_by h = play((tide_net_conditions){.latency = 0.03, .jitter = 0.01, .loss = 0.05}, false);
    check(&h);
}

// A host that takes what its handlers hear only from tick 60 hears nothing of the first preparation, which went
// before: what it sent isn't kept for it (tick_begin in engine/src/session.c), and its entity comes as Shown alone.
// The one in play comes as Spawned
TIDE_TEST(visibility_host_hears_what_a_preparation_sent_once_it_takes_it)
{
    const heard_by h = play((tide_net_conditions){.latency = 0.02, .jitter = 0.005}, true);
    print("host", &h.host);
    TIDE_CHECK(h.host.prepared == 10 && h.host.surePrepared == 10 && h.host.preparedShown == 11);
    TIDE_CHECK(h.host.destroyed == 10111 && h.host.hidden == 0);
}
