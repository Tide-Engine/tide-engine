#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

// A match played alone where the player sees only part of it (Visibility):
// the server runs the whole world, and this machine's client, rather than
// running its part of each tick again, takes the server's (twin_take in
// engine/src/session.c), unless its local handlers hear events of the
// match's own, which only its own run of a tick sends them.

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

static void counted_tick_client_events(void *w, const tide_jobs *jobs, tide_event_log *logs)
{
    ticks++;
    tide_game_api.tick_client_events(w, jobs, logs);
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

// What the player's world says it has, and what the server's says the player sees
static bool seen_alike(const tide_game *g, const void *own, const void *server)
{
    uint32_t own_size = 0, server_size = 0;
    uint8_t *mine = g->client_state(own, tide_player_from_index(0), &own_size);
    uint8_t *theirs = g->client_state(server, tide_player_from_index(0), &server_size);
    const bool alike = mine && theirs && own_size == server_size && memcmp(mine, theirs, own_size) == 0;
    free(mine);
    free(theirs);
    return alike;
}

typedef struct played {
    int confirmed;      // Ticks the player confirmed
    int ticks;          // ...and ticks run, the server's and the player's
    bool alike;         // Every frame, the player's world as the server said it sees it
    uint32_t fewest;    // Walkers the player saw, at the fewest and the most
    uint32_t most;
    uint32_t everywhere; // ...and the server has
    uint32_t tick;      // Where the match got to
    Heard heard;
} played;

static played play(const tide_game *game)
{
    played p = {.alike = true, .fewest = UINT32_MAX};
    static tide_local own;
    tide_local_init(&own);
    tide_session *s = tide_session_create(&(tide_session_desc){.game = game, .tick_rate = 60, .sample = sample});
    double t = 0.0;
    tide_session_events(s); // Before its first tick, so it records from there
    tide_session_start(s, NULL, t);
    for (int frame = 0; frame < 30; frame++) {
        t += 1.0 / 60.0;
        tide_session_update(s, t);
        tide_local_events(&own, tide_session_events(s));
    }
    const tide_session_status before = tide_session_status_of(s);
    TIDE_CHECK(before.client.state == TIDE_SESSION_CONNECTED);
    ticks = 0;
    for (int frame = 0; frame < 360; frame++) {
        // Frames a little apart, so some updates run no tick and some two
        t += frame % 3 == 0 ? 0.6 / 60.0 : 1.2 / 60.0;
        tide_session_update(s, t);
        const tide_view_worlds view = tide_session_view(s);
        tide_local_events(&own, tide_session_events(s));
        tide_draw_reset(&draw);
        tide_frame(view.current, view.previous, view.alpha, view.verified, view.verified_previous, view.verified_alpha, &own, &draw, &gui);
        const tide_world *mine = tide_session_world(s), *server = tide_session_server_world(s);
        p.alike &= seen_alike(game, mine, server);
        const uint32_t seen = walkers(mine);
        if (seen < p.fewest) p.fewest = seen;
        if (seen > p.most) p.most = seen;
        p.everywhere = walkers(server);
    }
    const tide_session_status after = tide_session_status_of(s);
    TIDE_CHECK(after.client.state == TIDE_SESSION_CONNECTED && after.client.resyncs == 0);
    p.confirmed = (int)(after.client.verified_tick - before.client.verified_tick);
    p.ticks = ticks;
    p.tick = ((const tide_world *)tide_session_server_world(s))->Time.tick;
    p.heard = own.Heard;
    tide_session_destroy(s);
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

// Its local handlers hear only what the server journals for it (Spawned and
// Destroyed) and what it finds in its worlds (Shown, Hidden), as a game's
// would that hears none of its own events: each tick runs once, on the
// server, and the player's world is what the server says it sees, frame
// after frame, some walkers in it and others not
TIDE_TEST(net_alone_seen_ticks_once)
{
    TIDE_REQUIRE(tide_game_api.client_state && tide_game_api.tick_client_events && tide_game_api.tick_events);
    tide_game game = counted();
    game.hears_match_events = false;
    const played p = play(&game);
    TIDE_CHECK(p.confirmed >= 350);
    TIDE_CHECK(p.ticks >= p.confirmed && p.ticks < p.confirmed + 10);
    TIDE_CHECK(p.alike);
    TIDE_CHECK(p.everywhere == 8 && p.most < p.everywhere && p.fewest < p.most);
    // Walkers that came into sight, heard once, and once as verified
    TIDE_CHECK(p.heard.shown > 8 && p.heard.sureShown == p.heard.shown);
    TIDE_CHECK(p.heard.rang == 0); // Rang is the player's to hear from its own run of a tick, which it didn't run
}

// Its local handlers hear Rang, which the match sends: the player runs its
// part of each tick, which is where it hears it from
TIDE_TEST(net_alone_seen_hearing_its_events_runs_its_ticks)
{
    TIDE_REQUIRE(tide_game_api.client_state && tide_game_api.tick_client_events && tide_game_api.hears_match_events);
    const tide_game game = counted();
    const played p = play(&game);
    TIDE_CHECK(p.confirmed >= 350);
    TIDE_CHECK(p.ticks >= 2 * p.confirmed);
    TIDE_CHECK(p.most < p.everywhere && p.fewest < p.most);
    TIDE_CHECK(p.heard.rang == (int)(p.tick + 29u) / 30);
}
