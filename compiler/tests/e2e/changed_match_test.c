#include <stdlib.h>
#include <string.h>

#if defined(ENDLESS) && !defined(__wasi__)
#include <signal.h>
#endif

#include "game.h"
#include "tide_test.h"

// Match handlers of Changed<...>: a field that changed over a tick, taken by
// the match as the tick ends, in rounds while their own changes go on.

static tide_draw_list draw;
static tide_gui gui;

#if defined(ENDLESS) && !defined(__wasi__)
// The endless build stops the program from inside a tick (tide_endless_chain), by abort(), which ends it by a
// signal on Linux and macOS that CTest counts as a crash whatever it printed: an exit code instead, as
// chain_endless_main.c does
static void aborted(const int sig)
{
    (void)sig;
    _Exit(1);
}

__attribute__((constructor)) static void stop_by_exit(void)
{
    signal(SIGABRT, aborted);
}
#endif

static tide_entity unit(const tide_world *w, const int32_t id)
{
    for (uint32_t i = 0; w && i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Unit(w, e) && tide_get_Unit(w, e).id == id) return e;
    }
    return (tide_entity){0};
}

static void check_match(const tide_world *w)
{
    const Workshop *s = &w->Workshop;
    // 0 to 4 at tick 10 and 4 to 5 at 30; nothing at 20, set to 9 and back
    TIDE_CHECK(s->level == 5 && s->board == 5 && s->loads == 2);
    // Each load's serial heard in the round after it: 0 to 1, then 1 to 2
    TIDE_CHECK(s->serial == 2 && s->serials == 2 && s->sum == 1 + 102);
    const tide_entity one = unit(w, 1), two = unit(w, 2);
    TIDE_REQUIRE(tide_has_Unit(w, one) && tide_has_Unit(w, two));
    TIDE_CHECK(tide_get_Tally(w, one).heard == 1 && !tide_get_Unit(w, one).down);
    TIDE_CHECK(tide_get_Tally(w, two).heard == 2 && tide_get_Unit(w, two).down);
}

// The handlers change the match the same whether or not a log hears it, and local handlers hear the change once
TIDE_TEST(changed_match_plain_host)
{
    static tide_world world, quiet;
    static tide_local local;
    tide_event_log events = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_init(&quiet, 1.0f / 60.0f);
    tide_local_init(&local);
    for (uint32_t tick = 0; tick < 60u; tick++) {
        tide_world_tick_events(&world, NULL, &events);
        tide_world_tick(&quiet);
        TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&quiet));
        if (tick == 10u) TIDE_CHECK(world.Workshop.board == 4 && world.Workshop.serials == 1); // The same tick
        tide_local_events(&local, &events);
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    }
    check_match(&world);
    check_match(&quiet);
    TIDE_CHECK(local.Heard.levels == 2 && local.Heard.last == 405);
    tide_event_log_free(&events);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_world_free(&quiet);
}

// ---------------------------------------------------------------------------
// A match on a network: a client predicting and running ticks again comes to the server's match

TIDE_TEST(changed_match_net)
{
    double now = 0.0;
    tide_loopback *net = tide_loopback_create(2026);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.05});
    const tide_server_desc desc = {.game = &tide_game_api, .tick_rate = 60, .transports = {tide_loopback_endpoint(net, 1)}};
    tide_server *server = tide_server_create(&desc, now);
    const tide_client_desc join = {
        .game = &tide_game_api,
        .transport = tide_loopback_endpoint(net, 2u),
        .server = tide_loopback_address(1),
        .lead = 2,
    };
    tide_client *client = tide_client_create(&join, now);
    bool odd = false;
    while (now < 2.0) {
        now += (odd = !odd) ? 0.011 : 0.022;
        tide_loopback_set_time(net, now);
        tide_client_update(client, now);
        tide_server_update(server, now);
    }
    const tide_world *match = tide_server_world(server);
    TIDE_REQUIRE(match->Time.tick > 60);
    check_match(match);
    const tide_view_worlds view = tide_client_view(client);
    TIDE_REQUIRE(view.verified != NULL);
    check_match(view.verified);
    TIDE_CHECK(tide_client_status_of(client).resyncs == 0);
    tide_client_destroy(client);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}
