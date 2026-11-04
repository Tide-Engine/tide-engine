#include "game.h"
#include "tide_test.h"

// A match whose players see only part of it (Visibility), played on the
// server's machine: the host's player has no world of its own, it shares the
// server's after each tick (mirror_tick in engine/src/session.c). So the
// match is ticked once there, the host sees all of it, as its server does,
// and its local handlers hear everything the match sends, at once verified.
// Another machine's player still sees only its part.

static tide_draw_list draw;
static tide_gui gui;
static int ticks;

static void counted_tick(void *w, const tide_jobs *jobs)
{
    ticks++;
    tide_game_api.tick(w, jobs);
}

static void counted_tick_events(void *w, const tide_jobs *jobs, tide_event_log *events)
{
    ticks++;
    tide_game_api.tick_events(w, jobs, events);
}

static void counted_tick_client_events(void *w, const tide_jobs *jobs, tide_event_log *logs, tide_event_log *all)
{
    ticks++;
    tide_game_api.tick_client_events(w, jobs, logs, all);
}

// Back and forth, past the walkers, which come into sight and go out of it
static void sample(void *user, const uint32_t tick, void *input)
{
    (void)user;
    *(Controls *)input = (Controls){.move = {(tick / 90u) % 2u ? -1.0f : 1.0f, 0.0f}};
}

static uint32_t walkers(const tide_world *w)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        count += tide_entity_alive(&w->entities, e) && tide_has_Walker(w, e);
    }
    return count;
}

typedef struct played {
    int confirmed;      // Ticks the host's player confirmed
    int ticks;          // ...and ticks run on the host's machine
    bool alike;         // Every frame, the host's world the server's
    uint32_t fewest;    // Walkers the guest saw, at the fewest and the most
    uint32_t most;
    uint32_t everywhere; // ...and the server has
    uint32_t spawned;   // Walkers the match ever had
    uint32_t tick;      // Where the match got to
    Heard heard;        // What the host's local handlers heard
} played;

static played play(const tide_game *game, const bool guest)
{
    played p = {.alike = true, .fewest = UINT32_MAX};
    static tide_local own;
    tide_local_init(&own);
    tide_loopback *net = tide_loopback_create(3);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.02, .jitter = 0.005, .loss = 0.03});
    tide_session *s = tide_session_create(&(tide_session_desc){.game = game, .tick_rate = 60, .sample = sample});
    tide_session *other = guest ? tide_session_create(&(tide_session_desc){.game = &tide_game_api, .tick_rate = 60, .sample = sample}) : NULL;
    double t = 0.0;
    tide_session_events(s); // Before its first tick, so it records from there
    tide_session_start(s, NULL, t);
    if (other) {
        TIDE_CHECK(tide_session_open(s, tide_loopback_endpoint(net, 1)));
        tide_session_join(other, tide_loopback_endpoint(net, 2), tide_loopback_address(1), t);
    }
    for (int frame = 0; frame < 60; frame++) {
        t += 1.0 / 60.0;
        tide_loopback_set_time(net, t);
        tide_session_update(s, t);
        if (other) tide_session_update(other, t);
        tide_local_events(&own, tide_session_events(s));
        const tide_view_worlds view = tide_session_view(s);
        if (!view.current) continue;
        tide_draw_reset(&draw); // Drawn from the first, so every walker it ever had was shown
        tide_frame(view.current, view.previous, view.alpha, view.verified, view.verified_previous, view.verified_alpha, &own, &draw, &gui);
    }
    const tide_session_status before = tide_session_status_of(s);
    TIDE_CHECK(before.client.state == TIDE_SESSION_CONNECTED);
    if (other) TIDE_CHECK(tide_session_status_of(other).client.state == TIDE_SESSION_CONNECTED);
    ticks = 0;
    for (int frame = 0; frame < 360; frame++) {
        // Frames a little apart, so some updates run no tick and some two
        t += frame % 3 == 0 ? 0.6 / 60.0 : 1.2 / 60.0;
        tide_loopback_set_time(net, t);
        tide_session_update(s, t);
        const tide_view_worlds view = tide_session_view(s);
        tide_local_events(&own, tide_session_events(s));
        tide_draw_reset(&draw);
        tide_frame(view.current, view.previous, view.alpha, view.verified, view.verified_previous, view.verified_alpha, &own, &draw, &gui);
        const tide_world *mine = tide_session_world(s), *server = tide_session_server_world(s);
        p.alike &= tide_world_hash(mine) == tide_world_hash(server) && walkers(mine) == walkers(server);
        p.everywhere = walkers(server);
        if (!other) continue;
        tide_session_update(other, t); // Its ticks aren't counted: it runs the game as generated
        const tide_world *seen = tide_session_world(other);
        const uint32_t n = seen ? walkers(seen) : 0u;
        if (n < p.fewest) p.fewest = n;
        if (n > p.most) p.most = n;
    }
    const tide_session_status after = tide_session_status_of(s);
    TIDE_CHECK(after.client.state == TIDE_SESSION_CONNECTED && after.client.resyncs == 0);
    if (other) TIDE_CHECK(tide_session_status_of(other).client.state == TIDE_SESSION_CONNECTED);
    p.confirmed = (int)(after.client.verified_tick - before.client.verified_tick);
    p.ticks = ticks;
    const tide_world *server = tide_session_server_world(s);
    p.tick = server->Time.tick;
    p.spawned = (uint32_t)server->Walkers.next;
    p.heard = own.Heard;
    tide_session_destroy(other);
    tide_session_destroy(s);
    tide_loopback_destroy(net);
    tide_local_free(&own);
    return p;
}

// The game, its ticks counted: it has visibility, and local handlers
static tide_game counted(void)
{
    tide_game g = tide_game_api;
    g.tick = counted_tick;
    g.tick_events = counted_tick_events;
    g.tick_client_events = counted_tick_client_events;
    return g;
}

// Played alone: each tick runs once, on the server, the host sees every
// walker, and hears every Rang and every walker shown, as it happens and as verified
TIDE_TEST(net_alone_seen_ticks_once)
{
    TIDE_REQUIRE(tide_game_api.client_state && tide_game_api.tick_client_events && tide_game_api.hears_match_events);
    const tide_game game = counted();
    const played p = play(&game, false);
    TIDE_CHECK(p.confirmed >= 350);
    TIDE_CHECK(p.ticks == p.confirmed);
    TIDE_CHECK(p.alike && p.everywhere == 8);
    TIDE_CHECK(p.heard.rang == (int)(p.tick + 29u) / 30 && p.heard.sureRang == p.heard.rang);
    TIDE_CHECK(p.heard.shown == (int)p.spawned && p.heard.sureShown == p.heard.shown);
}

// With another machine's player in: still once on the host's machine, which
// sees the whole match, while the guest sees only the walkers near it
TIDE_TEST(net_alone_seen_with_a_guest_ticks_once_on_the_host)
{
    const tide_game game = counted();
    const played p = play(&game, true);
    TIDE_CHECK(p.confirmed >= 350);
    TIDE_CHECK(p.ticks == p.confirmed);
    TIDE_CHECK(p.alike && p.everywhere == 8);
    TIDE_CHECK(p.most < p.everywhere && p.fewest < p.most);
    TIDE_CHECK(p.heard.rang == (int)(p.tick + 29u) / 30 && p.heard.sureRang == p.heard.rang);
    TIDE_CHECK(p.heard.shown == (int)p.spawned && p.heard.sureShown == p.heard.shown);
}
