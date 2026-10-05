#include <string.h>

#include "game.h"
#include "tide_test.h"

// `verified` parameters in views: what a machine's views read of the world the
// server has confirmed, beside the one they draw, on a C host with one world,
// with two worlds that disagree, and on a client that predicts wrong for a
// while.

static tide_draw_list draw;
static tide_gui gui;

static bool shield_at(const uint32_t tick)
{
    return (tick >= 89u && tick <= 92u) || (tick >= 149u && tick <= 152u) || (tick >= 239u && tick <= 242u);
}

static tide_entity target(const tide_world *w, const int32_t id)
{
    for (uint32_t i = 0; w && i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Target(w, e) && tide_get_Target(w, e).id == id) return e;
    }
    return (tide_entity){0};
}

static int bits(const int a, const int b, const int c, const int d)
{
    return (a ? 1 << a : 0) | (b ? 1 << b : 0) | (c ? 1 << c : 0) | (d ? 1 << d : 0);
}

// Plays the match on `w` up to `ticks`, the second player raising the shield
// when `shield` says.
static void play(tide_world *w, const uint32_t ticks, const bool shield)
{
    tide_world_init(w, 1.0f / 60.0f);
    tide_world_player_joined(w, tide_player_from_index(0));
    tide_world_player_joined(w, tide_player_from_index(1));
    for (uint32_t tick = 0; tick < ticks; tick++) {
        tide_world_set_input(w, tide_player_from_index(1), (Controls){.shield = shield && shield_at(tick)});
        tide_world_tick(w);
    }
}

// One world and no server: it's the confirmed one too.
TIDE_TEST(verified_views_one_world)
{
    static tide_world world;
    static tide_local local;
    play(&world, 260u, true);
    tide_local_init(&local);
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    const Seen *seen = &local.Seen;
    TIDE_CHECK(seen->drawn == bits(1, 2, 3, 5) && seen->both == seen->drawn && seen->confirmed == seen->drawn);
    TIDE_CHECK(seen->mismatch == 0 && seen->ahead == 0 && seen->confirmedOnly == 0 && seen->scoreAhead == 0);
    TIDE_CHECK(seen->plates == 4); // Target 2's armor
    TIDE_CHECK(seen->corpses == 1 << 2 && seen->drawnCorpses == 1 << 2 && seen->fresh == bits(1, 3, 5, 0));
    TIDE_CHECK(seen->sureMost == 1);
    TIDE_CHECK(tide_get_Target_local(&local, target(&world, 3)).sure == 1); // Local fields, through a `verified` parameter
    tide_local_free(&local);
    tide_world_free(&world);
}

// Two worlds that went differently: the one drawn never had the shield. A
// view that takes an entity from both runs for those that are the same entity
// in both, by what each was born as, not by its handle alone.
TIDE_TEST(verified_views_two_worlds)
{
    static tide_world drawn, confirmed;
    static tide_local local;
    play(&drawn, 245u, false);
    play(&confirmed, 245u, true);
    tide_local_init(&local);
    const tide_entity four = target(&drawn, 4), five = target(&confirmed, 5);
    TIDE_REQUIRE(!tide_entity_is_null(four) && tide_entity_equal(four, five)); // One handle, two entities
    TIDE_REQUIRE(tide_entity_is_null(target(&drawn, 3)) && !tide_entity_is_null(target(&confirmed, 3)));
    tide_draw_reset(&draw);
    tide_frame(&drawn, NULL, 1.0f, &confirmed, &local, &draw, &gui);
    const Seen *seen = &local.Seen;
    // Drawn: 1 and 2, the target the shield would have kept from coming, and 4
    TIDE_CHECK(seen->drawn == bits(1, 2, 4, 6));
    // In both, and the same entity: not 3 (gone from the drawn one), 6 (never confirmed), nor 4 and 5
    TIDE_CHECK(seen->both == bits(1, 2, 0, 0) && seen->mismatch == 0);
    TIDE_CHECK(seen->ahead == 1); // Target 1 took a hit only the drawn world had
    TIDE_CHECK(seen->plates == 4);
    // The confirmed world's own entities, whatever the drawn one has
    TIDE_CHECK(seen->confirmed == bits(1, 2, 3, 5) && seen->confirmedOnly == 1);
    TIDE_CHECK(tide_get_Target_local(&local, target(&confirmed, 3)).sure == 1);
    // Target 1 is Dead as drawn, and isn't as confirmed
    TIDE_CHECK(seen->drawnCorpses == bits(1, 2, 0, 0) && seen->corpses == 1 << 2 && seen->fresh == 1 << 1);
    TIDE_CHECK(seen->scoreAhead == 1 && seen->sureMost == 1 && drawn.Score.points == 11);
    tide_local_free(&local);
    tide_world_free(&drawn);
    tide_world_free(&confirmed);
}

// ---------------------------------------------------------------------------
// A client that predicts

static tide_loopback *net;
static tide_server *server;
static tide_client *clients[2];
static int who[2] = {0, 1};
static double now;
static tide_local local;
static uint32_t unconfirmed_frames; // Frames target 6, which the server never had, was drawn

static void sample(void *user, const uint32_t tick, void *input)
{
    *(Controls *)input = (Controls){.shield = *(const int *)user == 1 && shield_at(tick)};
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
        if (!tide_entity_is_null(target(view.current, 6))) unconfirmed_frames++;
        tide_draw_reset(&draw);
        tide_frame(view.current, view.previous, view.alpha, view.verified, &local, &draw, &gui);
        // What's confirmed is never what only a prediction had
        TIDE_CHECK(local.Seen.sureMost <= 1 && (local.Seen.confirmed & bits(4, 6, 0, 0)) == 0);
        TIDE_CHECK((local.Seen.corpses & (1 << 1)) == 0 && local.Seen.mismatch == 0);
    }
}

TIDE_TEST(verified_views_through_wrong_predictions)
{
    now = 0.0;
    net = tide_loopback_create(2024);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.05});
    const tide_server_desc desc = {.game = &tide_game_api, .tick_rate = 60, .transports = {tide_loopback_endpoint(net, 1)}};
    server = tide_server_create(&desc, now);
    tide_local_init(&local);
    unconfirmed_frames = 0;
    for (int i = 0; i < 2; i++) {
        const tide_client_desc client = {
            .game = &tide_game_api,
            .transport = tide_loopback_endpoint(net, (uint32_t)i + 2u),
            .server = tide_loopback_address(1),
            .sample = sample,
            .user = &who[i],
            .lead = 2,
        };
        clients[i] = tide_client_create(&client, now);
    }
    const Seen *seen = &local.Seen;

    // Past tick 100. At 90 the first player's machine predicted a hit and ten
    // points the server never gave, and at 91 a destroy it never made: its
    // views saw the drawn world ahead of the confirmed one for a while, and
    // the confirmed world's view kept running for the target meanwhile
    run(2.2);
    const tide_world *match = tide_server_world(server);
    TIDE_REQUIRE(match->Time.tick > 105 && match->Time.tick < 145);
    TIDE_CHECK(tide_client_status_of(clients[0]).again_ticks > 0);
    TIDE_CHECK(seen->ahead > 0 && seen->scoreAhead > 0 && seen->confirmedOnly > 0);
    TIDE_CHECK(seen->sureMost == 1 && match->Score.points == 1);
    TIDE_CHECK(seen->both == bits(1, 2, 3, 0) && seen->confirmed == bits(1, 2, 3, 0));
    const tide_entity three = target(match, 3);
    // Its view of the confirmed world drew target 3 every frame, through the wrong destroy
    const int sure_three = tide_get_Target_local(&local, three).sure;
    TIDE_CHECK(sure_three > 0 && sure_three == tide_get_Target_local(&local, target(match, 1)).sure);

    // Past tick 151: a target it predicted, which never was, was drawn and
    // never taken from both worlds; and target 1 was Dead as drawn only
    run(3.2);
    match = tide_server_world(server);
    TIDE_REQUIRE(match->Time.tick > 160 && match->Time.tick < 200);
    TIDE_CHECK(unconfirmed_frames > 0 && (seen->drawn & (1 << 6)) != 0);
    TIDE_CHECK((seen->both & (1 << 6)) == 0 && (seen->fresh & (1 << 6)) == 0);
    TIDE_CHECK(seen->drawnCorpses == 1 << 1 && seen->corpses == 0);
    TIDE_CHECK(seen->fresh == bits(1, 2, 3, 0));

    // Past tick 241: target 2 is Dead for good, and the target the server
    // spawned is the one both worlds have, not the one predicted on its handle
    run(4.6);
    match = tide_server_world(server);
    TIDE_REQUIRE(match->Time.tick > 250 && !tide_entity_is_null(target(match, 5)));
    TIDE_CHECK(seen->corpses == 1 << 2 && seen->drawnCorpses == bits(1, 2, 0, 0));
    TIDE_CHECK((seen->drawn & bits(4, 5, 0, 0)) == bits(4, 5, 0, 0));
    TIDE_CHECK(seen->both == bits(1, 2, 3, 5) && seen->confirmed == bits(1, 2, 3, 5) && seen->mismatch == 0);
    TIDE_CHECK(seen->plates == 4);
    for (int i = 0; i < 2; i++) {
        TIDE_CHECK(tide_client_status_of(clients[i]).state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(tide_client_status_of(clients[i]).resyncs == 0);
    }
    for (int i = 0; i < 2; i++) tide_client_destroy(clients[i]);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
    tide_local_free(&local);
}
