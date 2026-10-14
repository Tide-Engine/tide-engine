#include <string.h>

#include "game.h"
#include "tide_test.h"

// Queries in views and local handlers (`query (Body body, with Fighter)
// fighters`): what a machine's local code goes through of the match, as it
// sees it. A view's rows are blended between two ticks like its own
// parameters, or come from the world the server confirmed; a local handler's
// are as the tick that sent its event left them. On a C host with one world,
// with two worlds that disagree, and on a client that predicts wrong for a
// while.

static tide_draw_list draw;
static tide_gui gui;

// The second player's shield: up for a few ticks, three times
static bool shield_at(const uint32_t tick)
{
    return (tick >= 5u && tick <= 8u) || (tick >= 89u && tick <= 95u) || (tick >= 105u && tick <= 112u);
}

static tide_entity body(const tide_world *w, const int32_t id)
{
    for (uint32_t i = 0; w && i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Body(w, e) && tide_get_Body(w, e).id == id) return e;
    }
    return (tide_entity){0};
}

static int bits(const int a, const int b, const int c, const int d, const int e)
{
    return (a ? 1 << a : 0) | (b ? 1 << b : 0) | (c ? 1 << c : 0) | (d ? 1 << d : 0) | (e ? 1 << e : 0);
}

static void start(tide_world *w)
{
    tide_world_init(w, 1.0f / 60.0f);
    tide_world_player_joined(w, tide_player_from_index(0));
    tide_world_player_joined(w, tide_player_from_index(1));
}

// Plays the match on `w` up to `ticks`, the second player raising the shield
// when `shield` says.
static void play(tide_world *w, const uint32_t ticks, const bool shield)
{
    start(w);
    for (uint32_t tick = 0; tick < ticks; tick++) {
        tide_world_set_input(w, tide_player_from_index(1), (Controls){.shield = shield && shield_at(tick)});
        tide_world_tick(w);
    }
}

static void frame(const tide_world *w, const tide_world *previous, const float alpha, const tide_world *verified, tide_local *l)
{
    tide_draw_reset(&draw);
    tide_frame(w, previous, alpha, verified, NULL, 1.0f, l, &draw, &gui);
}

// Between two ticks: a query's rows are blended as the view's own parameters
// are, an entity that came this tick is as it is, and one that went is gone.
TIDE_TEST(view_queries_blend_between_ticks)
{
    static tide_world before, now;
    static tide_local local;
    play(&before, 10u, false);
    play(&now, 11u, false);
    tide_local_init(&local);
    // Body 4 went in the last tick, and 5 came
    TIDE_REQUIRE(!tide_entity_is_null(body(&before, 4)) && tide_entity_is_null(body(&now, 4)));
    TIDE_REQUIRE(tide_entity_is_null(body(&before, 5)) && !tide_entity_is_null(body(&now, 5)));
    frame(&now, &before, 0.5f, NULL, &local);
    const Seen *seen = &local.Seen;
    TIDE_CHECK(seen->runs == 1 && seen->ids == bits(1, 2, 3, 5, 0));
    // Halfway between ticks 10 and 11: 31, 41 and 51, and the one that just came, at 50
    TIDE_CHECK(seen->sumX == 173.0f);
    TIDE_CHECK(seen->sumTurn == 33.0f); // [Snap]: as the last tick has it
    TIDE_CHECK(seen->fighters == bits(1, 2, 5, 0, 0) && seen->others == 1 << 3 && seen->standing == 3);
    // Four bodies, three fighters each; the three that are fighters found themselves, as the view sees them
    TIDE_CHECK(seen->pairs == 12 && seen->same == 3 && seen->mismatch == 0);
    TIDE_CHECK(seen->named == 1 && seen->marks == 7); // Text and a list of the match's, read through a row
    TIDE_CHECK(seen->sureNamed == 1 && seen->sureMarks == 7 && seen->sureWords == 1);
    // One world: it's the confirmed one too
    TIDE_CHECK(seen->confirmed == bits(1, 2, 3, 5, 0) && seen->sureHealth == 20 && seen->both == seen->confirmed);
    TIDE_CHECK(seen->mismatched == 0 && seen->behind == 0 && seen->undead == seen->confirmed);
    TIDE_CHECK(seen->corpses == 0 && seen->sureCorpses == 0);
    TIDE_CHECK(seen->sparks == 0 && seen->tagged == 0); // The frame's spawns come at its end

    // As the last tick has it, with nothing to blend from
    frame(&now, NULL, 1.0f, NULL, &local);
    TIDE_CHECK(seen->runs == 2 && seen->sumX == 32.0f + 42.0f + 52.0f + 50.0f);
    TIDE_CHECK(seen->pairs == 12 && seen->same == 3 && seen->mismatch == 0);
    // This machine's sparks, 1, 2 and 3: each row of the view that changes them reads what the rows before it wrote
    TIDE_CHECK(seen->sparks == 3 && seen->tagged == 3 && seen->sparkSelf == 3);
    TIDE_CHECK(seen->sparkSum == (1 + 2 + 3) + (11 + 2 + 3) + (11 + 12 + 3));

    // Outside a match, a view that queries the match doesn't run; one that queries this machine's entities does
    frame(NULL, NULL, 1.0f, NULL, &local);
    TIDE_CHECK(seen->runs == 2 && seen->sparks == 3 && seen->sparkSum == 3 * (11 + 12 + 13) + 10 + 20);
    // 31, 32 and 33, then past 50: a row's entity is the spark's own, which goes, and a local handler queries what's left
    frame(&now, NULL, 1.0f, NULL, &local);
    TIDE_CHECK(seen->snuffed == 0 && local.Seen.sparks == 3);
    frame(&now, NULL, 1.0f, NULL, &local);
    TIDE_CHECK(seen->snuffed == 0);
    frame(&now, NULL, 1.0f, NULL, &local);
    TIDE_CHECK(seen->snuffed == 51 + 52 + 53 && seen->snuffedLeft == 1);
    frame(&now, NULL, 1.0f, NULL, &local);
    TIDE_CHECK(seen->sparks == 0 && seen->tagged == 0 && seen->runs == 6);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_local_free(&local);
    tide_world_free(&before);
    tide_world_free(&now);
}

// Two worlds that went differently: the one drawn never had the shield.
// `verified` in a query reads the confirmed one, and a query of both takes
// the entities that are the same entity in both, not the same handle.
TIDE_TEST(view_queries_two_worlds)
{
    static tide_world drawn, confirmed;
    static tide_local local;
    play(&drawn, 100u, false);
    play(&confirmed, 100u, true);
    tide_local_init(&local);
    const tide_entity seven = body(&confirmed, 7);
    TIDE_REQUIRE(!tide_entity_is_null(seven) && tide_entity_is_null(body(&confirmed, 6)) && tide_entity_is_null(body(&confirmed, 8)));
    // One handle, two entities
    TIDE_REQUIRE(tide_entity_equal(body(&drawn, 6), seven) || tide_entity_equal(body(&drawn, 8), seven));
    TIDE_REQUIRE(tide_entity_is_null(body(&drawn, 3)) && !tide_entity_is_null(body(&confirmed, 3)));
    frame(&drawn, NULL, 1.0f, &confirmed, &local);
    const Seen *seen = &local.Seen;
    TIDE_CHECK(seen->ids == bits(1, 2, 5, 6, 8));
    TIDE_CHECK(seen->fighters == bits(1, 2, 5, 0, 0) && seen->standing == 2); // Body 2 is Dead, as drawn
    // The confirmed world's own entities, whatever the drawn one has
    TIDE_CHECK(seen->confirmed == bits(1, 2, 3, 5, 7) && seen->sureHealth == 25);
    // In both, and the same entity: not 3 (gone from the drawn one), nor 6, 7 and 8
    TIDE_CHECK(seen->both == bits(1, 2, 5, 0, 0) && seen->mismatched == 0);
    TIDE_CHECK(seen->behind == 1); // Body 1 took a hit only the drawn world had
    // ...and a mark: each world's list is read from its own heap
    TIDE_CHECK(seen->named == 1 && seen->marks == 12 && seen->sureNamed == 1 && seen->sureMarks == 7 && seen->sureWords == 1);
    TIDE_CHECK(seen->undead == bits(1, 2, 5, 0, 0) && seen->corpses == 1 << 2 && seen->sureCorpses == 0);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_local_free(&local);
    tide_world_free(&drawn);
    tide_world_free(&confirmed);
}

// A host with one world and no server: a local handler's query goes through
// the match as the tick that sent its event left it, whatever the world is by
// the frame that runs it. The world goes the same with a log as without.
TIDE_TEST(view_queries_handlers_hear_their_tick)
{
    static tide_world world, quiet;
    static tide_local local;
    tide_event_log events = {0};
    start(&world);
    start(&quiet);
    tide_local_init(&local);
    frame(&world, NULL, 1.0f, NULL, &local); // Three sparks, from here on
    const Seen *seen = &local.Seen;
    for (uint32_t tick = 0; tick < 40u; tick++) {
        const Controls controls = {.shield = shield_at(tick)};
        tide_world_set_input(&world, tide_player_from_index(1), controls);
        tide_world_set_input(&quiet, tide_player_from_index(1), controls);
        tide_world_tick_events(&world, NULL, &events);
        tide_world_tick(&quiet); // Nobody listens: the same world
        TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&quiet));
        if (tick == 10u) { // Body 4 went in this tick, and 5 came: the fighters it left are 1, 2 and 5
            tide_local_events(&local, &events);
            frame(&world, NULL, 1.0f, NULL, &local);
            TIDE_CHECK(seen->gone == 1 && seen->goneIds == ((1 << 20) | bits(1, 2, 5, 0, 0)));
            TIDE_CHECK(seen->goneSparks == 3); // This machine's entities, as they are
            TIDE_CHECK(seen->rangs == 0);
        }
    }
    // Rang was sent in tick 12, and it's heard at tick 40: the fighters are where that tick left them
    tide_local_events(&local, &events);
    TIDE_CHECK(events.count == 0);
    frame(&world, NULL, 1.0f, NULL, &local);
    TIDE_CHECK(seen->rangs == 1 && seen->rangIds == bits(1, 2, 5, 0, 0));
    TIDE_CHECK(seen->rangX == 36.0f * 1000.0f + (36.0f + 46.0f + 54.0f));
    // The shield was up for four ticks before body 5 came: fighters 1 and 2 rose by 4
    TIDE_CHECK(seen->sureRangs == 1 && seen->sureRangY == 4.0f * 1.0f + 4.0f * 2.0f);
    TIDE_CHECK(seen->gone == 1);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_event_log_free(&events);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_world_free(&quiet);
}

// ---------------------------------------------------------------------------
// A client that predicts

static tide_loopback *net;
static tide_server *server;
static tide_client *clients[2];
static int who[2] = {0, 1};
static double now;
static tide_local local;
static tide_event_log heard;
static int behind_frames;   // Frames a row's drawn health was under the confirmed one
static int ahead_frames;    // Frames the drawn world had a body the server never did
static int rangs_before;    // What was heard before tick 100
static int sure_before;

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
        if (!tide_entity_is_null(body(view.current, 6)) || !tide_entity_is_null(body(view.current, 8))) ahead_frames++;
        tide_local_events(&local, &heard);
        tide_draw_reset(&draw);
        tide_frame(view.current, view.previous, view.alpha, view.verified, view.verified_previous, view.verified_alpha, &local, &draw, &gui);
        const Seen *seen = &local.Seen;
        if (seen->behind) behind_frames++;
        if (tide_client_status_of(clients[0]).predicted_tick < 100u) {
            rangs_before = seen->rangs;
            sure_before = seen->sureRangs;
        }
        // What's confirmed is never what only a prediction had, and a row of both worlds is one entity
        TIDE_CHECK((seen->confirmed & bits(6, 8, 0, 0, 0)) == 0 && seen->sureCorpses == 0 && seen->mismatched == 0);
        TIDE_CHECK((seen->both & bits(6, 7, 8, 0, 0) & ~seen->confirmed) == 0 && seen->mismatch == 0);
        TIDE_CHECK(seen->sureRangs <= seen->rangs);
        TIDE_CHECK(seen->sureMarks == (view.current ? 7 : 0)); // Never the mark only a prediction gave
    }
}

TIDE_TEST(view_queries_through_wrong_predictions)
{
    now = 0.0;
    net = tide_loopback_create(2024);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.05});
    const tide_server_desc desc = {.game = &tide_game_api, .tick_rate = 60, .transports = {tide_loopback_endpoint(net, 1)}};
    server = tide_server_create(&desc, now);
    tide_local_init(&local);
    behind_frames = ahead_frames = rangs_before = sure_before = 0;
    heard.taken = true; // Its host takes them, each frame
    for (int i = 0; i < 2; i++) {
        const tide_client_desc client = {
            .game = &tide_game_api,
            .transport = tide_loopback_endpoint(net, (uint32_t)i + 2u),
            .server = tide_loopback_address(1),
            .sample = sample,
            .user = &who[i],
            .lead = 2,
            .events = i == 0 ? &heard : NULL,
        };
        clients[i] = tide_client_create(&client, now);
    }
    const Seen *seen = &local.Seen;

    // Past tick 112. Around tick 90 the first player's machine predicted a hit
    // and two bodies the server never had: its queries of the drawn world saw
    // them, and those of the confirmed world never did
    run(2.8);
    const tide_world *match = tide_server_world(server);
    TIDE_REQUIRE(match->Time.tick > 125 && match->Time.tick < 200);
    TIDE_CHECK(tide_client_status_of(clients[0]).again_ticks > 0);
    TIDE_CHECK(behind_frames > 0 && ahead_frames > 0);
    TIDE_CHECK(seen->confirmed == bits(1, 3, 5, 7, 0) && seen->both == seen->confirmed && seen->ids == seen->confirmed);
    TIDE_CHECK(seen->behind == 0 && seen->corpses == 0 && seen->sureHealth == 20);
    TIDE_CHECK(seen->marks == 7 && seen->sureMarks == 7 && seen->sureWords == 1); // The mark it predicted never was
    // Rang, sent in tick 110, was heard once however often that tick ran, with
    // the fighters where it left them: their x is the same whatever the shield
    TIDE_CHECK(seen->rangs == rangs_before + 1 && seen->rangIds == bits(1, 2, 5, 0, 0));
    TIDE_CHECK(seen->rangX == 232.0f * 1000.0f + (232.0f + 242.0f + 250.0f));
    // ...and once confirmed, with them where the server's tick left them: they
    // rose for every tick the shield was up, which this machine first predicted without
    TIDE_CHECK(seen->sureRangs == sure_before + 1);
    TIDE_CHECK(match->Witness.y > 0.0f && seen->sureRangY == match->Witness.y);
    TIDE_CHECK(seen->rangY < seen->sureRangY); // The tick as first predicted hadn't the shield's last ticks
    // Body 2 went in tick 111: the last to go, and it left fighters 1 and 5
    TIDE_CHECK(seen->goneIds == ((1 << 18) | bits(1, 5, 0, 0, 0)));
    for (int i = 0; i < 2; i++) {
        TIDE_CHECK(tide_client_status_of(clients[i]).state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(tide_client_status_of(clients[i]).resyncs == 0);
    }
    for (int i = 0; i < 2; i++) tide_client_destroy(clients[i]);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
    tide_event_log_free(&heard);
    tide_local_free(&local);
}
