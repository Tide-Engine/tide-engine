#include <string.h>

#include "game.h"
#include "tide_test.h"

// Local handlers of Changed<...>: a field of the match that changed over a
// tick, heard with what it was and what it is, on a C host that ticks a world
// itself and on machines of a match on a loopback network in made-up time.

static tide_draw_list draw;
static tide_gui gui;

static bool shield_at(const uint32_t tick)
{
    return tick >= 89u && tick <= 92u;
}

static tide_entity unit(const tide_world *w, const int32_t id)
{
    for (uint32_t i = 0; w && i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Unit(w, e) && tide_get_Unit(w, e).id == id) return e;
    }
    return (tide_entity){0};
}

// What every machine hears the same: units 1 and 3's health, unit 2's level and the score's points
static void check_agreed(const Heard *heard)
{
    TIDE_CHECK(heard->ids == ((1 << 1) | (1 << 3)));
    TIDE_CHECK(heard->sureHealth == 3 && heard->sureSum == 1009 + 100 + 908 && heard->sureWrong == 0);
    TIDE_CHECK(heard->levels == 1 && heard->level == 20003);
    TIDE_CHECK(heard->points == 1 && heard->pointsLast == 10);
    TIDE_CHECK(heard->steady == 0);
    TIDE_CHECK(heard->lit == 1 && heard->aimed == 3.0);
}

// A host with one world: each change is heard by both kinds of handler at the next frame, and the world goes the
// same with a log as without.
TIDE_TEST(changed_events_plain_host)
{
    static tide_world world, quiet;
    static tide_local local;
    tide_event_log events = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_init(&quiet, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_world *both[] = {&world, &quiet};
    for (int i = 0; i < 2; i++) {
        tide_world_player_joined(both[i], tide_player_from_index(0));
        tide_world_player_joined(both[i], tide_player_from_index(1));
    }
    for (uint32_t tick = 0; tick < 140u; tick++) {
        for (int i = 0; i < 2; i++) tide_world_set_input(both[i], tide_player_from_index(1), (Controls){.shield = shield_at(tick)});
        tide_world_tick_events(&world, NULL, &events);
        tide_world_tick(&quiet);
        TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&quiet));
        if (tick % 3u != 2u) continue;
        tide_local_events(&local, &events);
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
        if (tick < 30u) TIDE_CHECK(local.Heard.health == 0);
    }
    tide_local_events(&local, &events);
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    const Heard *heard = &local.Heard;
    check_agreed(heard);
    // 10 to 9 at tick 30, 1 to 0 at 110, and 9 to 8 at 120: nothing at 40, when it went back, or at 90, shielded
    TIDE_CHECK(heard->health == 3 && heard->sum == 1009 + 100 + 908 && heard->wrong == 0);
    TIDE_CHECK(heard->ticks == 30 + 110 + 120); // Time as the tick that changed it
    TIDE_CHECK(tide_get_Health_local(&local, unit(&world, 1)).changes == 2);
    TIDE_CHECK(tide_get_Health_local(&local, unit(&world, 3)).changes == 1);
    TIDE_CHECK(tide_get_Health_local(&local, unit(&world, 4)).changes == 0);
    tide_event_log_free(&events);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_world_free(&quiet);
}

// ---------------------------------------------------------------------------
// A match on a network

static tide_loopback *net;
static tide_server *server;
static tide_client *clients[2];
static int who[2] = {0, 1};
static double now;
static tide_local local;
static tide_event_log heard_0;

static void sample(void *user, const uint32_t tick, void *input)
{
    const int player = *(const int *)user;
    Controls *c = input;
    *c = (Controls){0};
    c->shield = player == 1 && shield_at(tick);
}

static void join(const int number, tide_event_log *events)
{
    if (events) events->taken = true;
    const tide_client_desc desc = {
        .game = &tide_game_api,
        .transport = tide_loopback_endpoint(net, (uint32_t)number + 2u),
        .server = tide_loopback_address(1),
        .sample = sample,
        .user = &who[number],
        .lead = 2,
        .events = events,
    };
    clients[number] = tide_client_create(&desc, now);
}

static void run(const double until)
{
    bool odd = false;
    while (now < until) {
        now += (odd = !odd) ? 0.011 : 0.022;
        tide_loopback_set_time(net, now);
        for (int i = 0; i < 2; i++) tide_client_update(clients[i], now);
        tide_server_update(server, now);
        const tide_view_worlds view = tide_client_view(clients[0]);
        tide_local_events(&local, &heard_0);
        tide_draw_reset(&draw);
        tide_frame(view.current, view.previous, view.alpha, view.verified, view.verified_previous, view.verified_alpha, &local,
                   &draw, &gui);
        TIDE_CHECK(local.Heard.sureWrong == 0); // Never confirmed
    }
}

TIDE_TEST(changed_events_net_heard_once_through_wrong_predictions)
{
    now = 0.0;
    net = tide_loopback_create(2026);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.05});
    const tide_server_desc desc = {.game = &tide_game_api, .tick_rate = 60, .transports = {tide_loopback_endpoint(net, 1)}};
    server = tide_server_create(&desc, now);
    tide_local_init(&local);
    heard_0 = (tide_event_log){0};
    join(0, &heard_0);
    join(1, NULL);
    run(3.0);
    const tide_world *match = tide_server_world(server);
    TIDE_REQUIRE(match->Time.tick > 140);
    TIDE_CHECK(tide_client_status_of(clients[0]).again_ticks > 0);
    const Heard *heard = &local.Heard;
    check_agreed(heard);
    // The first player's machine predicted tick 90 without the shield: heard as predicted, once through every run
    // of it again, never confirmed and not taken back. Every other change heard once, as it was predicted.
    TIDE_CHECK(heard->wrong == 1);
    TIDE_CHECK(heard->health == 4 && heard->sum == 1009 + 100 + 908 + 907);
    TIDE_CHECK(tide_get_Health(match, unit(match, 1)).value == 8);
    TIDE_CHECK(tide_get_Health_local(&local, unit(match, 1)).changes == 3);
    for (int i = 0; i < 2; i++) {
        TIDE_CHECK(tide_client_status_of(clients[i]).resyncs == 0);
        tide_client_destroy(clients[i]);
    }
    tide_server_destroy(server);
    tide_loopback_destroy(net);
    tide_event_log_free(&heard_0);
    tide_local_free(&local);
}
