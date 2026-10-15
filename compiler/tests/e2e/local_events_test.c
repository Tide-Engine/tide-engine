#include <string.h>

#include "game.h"
#include "tide_test.h"

// Local handlers of the match's events (`local event(Hit hit) ...`), on a C
// host that ticks a world itself, on machines of a match on a loopback network
// in made-up time, and on one machine through a session.

static tide_draw_list draw;
static tide_gui gui;

// The second player's shield, around ticks 90, 150 and 240
static bool shield_at(const uint32_t tick)
{
    return (tick >= 89u && tick <= 92u) || (tick >= 149u && tick <= 152u) || (tick >= 239u && tick <= 242u);
}

// The target with `id` in a world, or the null entity.
static tide_entity target(const tide_world *w, const int32_t id)
{
    for (uint32_t i = 0; w && i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Target(w, e) && tide_get_Target(w, e).id == id) return e;
    }
    return (tide_entity){0};
}

// Every Rang from tick 85 to 95, each once and in order
static void check_rang(const Heard *heard)
{
    TIDE_CHECK(heard->rang == 11 && heard->rangTicks == 0x7FF);
    TIDE_CHECK(heard->rangAgain == 0 && heard->rangLate == 0);
}

// A host with one world and no server: what it ticks is all there is, so a
// tick's events are heard by both kinds of handler, at the next frame. The
// world goes the same with a log as without. What the world started with was
// available before any tick: the host records Shown itself (tide_game.hear_arrived).
TIDE_TEST(local_events_plain_host)
{
    static tide_world world, quiet, before;
    static tide_local local;
    tide_event_log events = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_init(&quiet, 1.0f / 60.0f);
    tide_local_init(&local);
    TIDE_REQUIRE(tide_game_api.hear_arrived != NULL);
    tide_game_api.hear_arrived(&world, NULL, &events);
    // The scene and its three targets, Shown once for each handler, then their query rows
    TIDE_CHECK(events.count == 5);
    TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&quiet));
    tide_local_events(&local, &events);
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Heard.spawned == 0xE && local.Heard.sureSpawned == 0xE && local.Heard.entities == 4);
    for (int32_t id = 1; id <= 3; id++) TIDE_CHECK(tide_get_Target_local(&local, target(&world, id)).born == id);
    // Its handlers' queries go through the world as it started: all three targets
    TIDE_CHECK(local.Heard.company == 0xE && local.Heard.sureCompany == 0xE);
    // A world it had says which of them are new to it: none of its own
    tide_game_api.hear_arrived(&world, &world, &events);
    TIDE_CHECK(events.count == 0);
    tide_world *both[] = {&world, &quiet};
    for (int i = 0; i < 2; i++) {
        tide_world_player_joined(both[i], tide_player_from_index(0));
        tide_world_player_joined(both[i], tide_player_from_index(1));
    }
    for (uint32_t tick = 0; tick < 260u; tick++) {
        for (int i = 0; i < 2; i++) tide_world_set_input(both[i], tide_player_from_index(1), (Controls){.shield = shield_at(tick)});
        tide_world_tick_events(&world, NULL, &events);
        tide_world_tick(&quiet); // Nobody listens: the same world
        TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&quiet));
        if (tick == 199u) tide_world_copy(&before, &world); // With target 2, and without target 5
        if (tick % 3u != 2u) continue; // A frame every three ticks: it hears them all, in order
        tide_local_events(&local, &events);
        TIDE_CHECK(events.count == 0);
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
        if (tick < 30u) TIDE_CHECK(local.Heard.struck == 0);
    }
    tide_local_events(&local, &events);
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    const Heard *heard = &local.Heard;
    // The shield was up at 90, 150 and 240: kinds 1 and 4, no 2 or 3
    TIDE_CHECK(heard->struck == 2 && heard->kinds == ((1 << 1) | (1 << 4)));
    TIDE_CHECK(heard->sureStruck == 2 && heard->sure == heard->kinds);
    TIDE_CHECK(heard->health == 3); // As the last hit left it: the match's handler ran first
    TIDE_CHECK(heard->notes == 2);  // A local handler sends local events on
    check_rang(heard);
    TIDE_CHECK(heard->sureRang == 11);
    TIDE_CHECK(heard->glanced == 1 && heard->sureGlanced == 1);
    TIDE_CHECK(heard->glancedAt.x == 1.5f && heard->glancedAt.y == -1.0f && heard->glancedTick == 150);
    // The three first targets came as the world started, before any tick; then the players, and target 5
    TIDE_CHECK(heard->spawned == (0xE | 1 << 5) && heard->sureSpawned == (0xE | 1 << 5));
    TIDE_CHECK(heard->entities == 7 && heard->shielded == 1);
    TIDE_CHECK(heard->joined == 2);
    // Destroyed is heard with the entity's components as they were
    TIDE_CHECK(heard->destroyed == 1 << 2 && heard->sureDestroyed == 1 << 2 && heard->destroyedHealth == 5);
    // Local fields a handler wrote are the entity's, for views to read
    TIDE_CHECK(tide_get_Target_local(&local, target(&world, 1)).struck == 2 && heard->looked == 2);
    TIDE_CHECK(tide_get_Target_local(&local, target(&world, 5)).born == 5);
    // Against the world as it was at tick 200: target 5 is all it has since, as it is now
    tide_game_api.hear_arrived(&world, &before, &events);
    TIDE_REQUIRE(events.count == 2);
    const tide_heard *arrived = (const tide_heard *)events.bytes;
    TIDE_CHECK(tide_entity_equal(arrived->target, target(&world, 5)) && arrived->tick == (uint32_t)world.Time.tick);
    TIDE_CHECK(arrived->as == 0); // Straight from the game: for both kinds of handler
    // ...and the rows its handlers' queries read, which are that world's too: targets 1, 3 and 5
    const tide_heard *rows = (const tide_heard *)(events.bytes + arrived->size);
    TIDE_CHECK(rows->event == TIDE_HEARD_ROWS && rows->tick == arrived->tick && rows->same == 3);
    tide_event_log_free(&events);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_world_free(&quiet);
    tide_world_free(&before);
}

// ---------------------------------------------------------------------------
// A match on a network

static tide_loopback *net;
static tide_server *server;
static tide_client *clients[3];
static int who[3] = {0, 1, 2};
static double now;
static tide_local local;       // The first player's machine's
static tide_event_log heard_0; // ...and what its client hands it of the match
static tide_local late;        // The third's, who joins late
static tide_event_log heard_2;

// When the first player's machine heard things: the tick it had predicted up
// to, and the tick the server had confirmed up to
static uint32_t glanced_predicted, glanced_sure;
static uint32_t wrong_hit_predicted, wrong_hit_health;
static uint32_t spawn_sure_at;
static tide_entity predicted_spawn;
static uint32_t wrong_destroy_frames;

static void sample(void *user, const uint32_t tick, void *input)
{
    const int player = *(const int *)user;
    Controls *c = input;
    *c = (Controls){0};
    c->shield = player == 1 && shield_at(tick);
}

static tide_client *join(const int number, tide_event_log *events)
{
    if (events) events->taken = true; // Its host takes them, each frame
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
    return clients[number];
}

// A frame on a machine: what its client heard of the match, then its views.
static void frame(tide_local *l, const tide_client *c, tide_event_log *events)
{
    const tide_view_worlds view = tide_client_view(c);
    tide_local_events(l, events);
    tide_draw_reset(&draw);
    tide_frame(view.current, view.previous, view.alpha, view.verified, view.verified_previous, view.verified_alpha, l, &draw, &gui);
}

static void first_frame(void)
{
    const tide_client_status status = tide_client_status_of(clients[0]);
    const tide_view_worlds view = tide_client_view(clients[0]);
    const tide_world *drawn = view.current;
    const tide_world *confirmed = view.verified;
    const Heard before = local.Heard;
    const tide_entity three = target(confirmed, 3);
    const tide_entity four = target(drawn, 4);
    if (drawn && !tide_entity_is_null(three) && !tide_has_Target(drawn, three)) wrong_destroy_frames++;
    frame(&local, clients[0], &heard_0);
    const Heard *heard = &local.Heard;
    if (heard->glanced > before.glanced) glanced_predicted = status.predicted_tick;
    if (heard->sureGlanced > before.sureGlanced) glanced_sure = status.verified_tick;
    if ((heard->kinds & ~before.kinds) & (1 << 2)) {
        wrong_hit_predicted = status.predicted_tick;
        wrong_hit_health = (uint32_t)heard->health;
    }
    if ((heard->sureSpawned & ~before.sureSpawned) & (1 << 5)) spawn_sure_at = status.verified_tick;
    if (!tide_entity_is_null(four)) {
        predicted_spawn = four;
        TIDE_CHECK(tide_get_Target_local(&local, four).born == 4); // Its handler heard it spawn, as predicted
    }
    // Nothing is certain before the server said so
    TIDE_CHECK((heard->sure & ((1 << 2) | (1 << 3))) == 0);
    TIDE_CHECK((heard->sureSpawned & (1 << 4)) == 0 && (heard->sureDestroyed & (1 << 3)) == 0);
    TIDE_CHECK(heard->sureStruck <= heard->struck && heard->sureRang <= heard->rang);
}

// Runs everything until `until`, in frames of 11 and 22 milliseconds.
static void run(const double until)
{
    bool odd = false;
    while (now < until) {
        now += (odd = !odd) ? 0.011 : 0.022;
        tide_loopback_set_time(net, now);
        for (int i = 0; i < 3; i++) {
            if (clients[i]) tide_client_update(clients[i], now);
        }
        tide_server_update(server, now);
        first_frame();
        if (clients[2]) frame(&late, clients[2], &heard_2);
    }
}

TIDE_TEST(local_events_net_heard_once_through_wrong_predictions)
{
    now = 0.0;
    net = tide_loopback_create(2024);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.05});
    const tide_server_desc desc = {.game = &tide_game_api, .tick_rate = 60, .transports = {tide_loopback_endpoint(net, 1)}};
    server = tide_server_create(&desc, now);
    memset(clients, 0, sizeof clients);
    tide_local_init(&local);
    tide_local_init(&late);
    heard_0 = (tide_event_log){0};
    heard_2 = (tide_event_log){0};
    glanced_predicted = glanced_sure = wrong_hit_predicted = wrong_hit_health = spawn_sure_at = wrong_destroy_frames = 0;
    predicted_spawn = (tide_entity){0};
    join(0, &heard_0);
    join(1, NULL); // Nobody listens there: its ticks record nothing
    const Heard *heard = &local.Heard;

    // Past tick 30: a hit every machine predicted right is heard once, and
    // once more when the server confirmed it
    run(1.2);
    const tide_world *match = tide_server_world(server);
    TIDE_REQUIRE(match->Time.tick > 40 && match->Time.tick < 80);
    TIDE_CHECK(heard->struck == 1 && heard->kinds == 1 << 1 && heard->sureStruck == 1 && heard->sure == 1 << 1);
    TIDE_CHECK(heard->health == 4 && heard->notes == 1);
    TIDE_CHECK(tide_get_Target_local(&local, target(match, 1)).struck == 1 && heard->looked == 1);
    // The targets were in the world it was sent, spawned before it had one: heard as it came, by both
    // kinds of handler, with the scene, and then each player's entity as its tick spawned it
    TIDE_CHECK(heard->spawned == 0xE && heard->sureSpawned == 0xE && heard->entities == 6);
    for (int32_t id = 1; id <= 3; id++) TIDE_CHECK(tide_get_Target_local(&local, target(match, id)).born == id);

    // Past tick 96. The shield went up at 89, which the first player's machine
    // heard of a round trip late: it ran the ticks around it again, and each
    // sent its Rang again, heard once. It predicted a hit at 90 the server
    // never had: heard as it predicted it, never confirmed, and not taken
    // back. And a destroy at 91 the server never made: heard, with the entity
    // back since.
    run(2.2);
    match = tide_server_world(server);
    TIDE_REQUIRE(match->Time.tick > 100 && match->Time.tick < 145);
    TIDE_CHECK(tide_client_status_of(clients[0]).again_ticks > 0);
    check_rang(heard);
    TIDE_CHECK(heard->sureRang == 11);
    TIDE_CHECK(heard->struck == 2 && heard->kinds == ((1 << 1) | (1 << 2)));
    TIDE_CHECK(heard->sureStruck == 1 && heard->sure == 1 << 1);
    TIDE_CHECK(wrong_hit_predicted >= 90u && wrong_hit_health == 3u); // As that tick left it, in the world that predicted it
    TIDE_CHECK(match->Time.tick > 100 && tide_get_Target(match, target(match, 1)).health == 4);
    TIDE_CHECK(wrong_destroy_frames > 0);
    TIDE_CHECK(heard->destroyed == 1 << 3 && heard->sureDestroyed == 0);
    TIDE_CHECK(!tide_entity_is_null(target(tide_client_world(clients[0]), 3)));
    TIDE_CHECK(heard->entities == 7); // The corrected predicted removal restores availability once.

    // Past tick 151. The shield was up at 150, so only the tick as the server
    // ran it sent Glanced: heard late, once, with what its turn left. The hit
    // at 151 is another event than the one predicted: both are heard, and only
    // the server's is confirmed.
    join(2, &heard_2);
    run(3.2);
    match = tide_server_world(server);
    TIDE_REQUIRE(match->Time.tick > 160 && match->Time.tick < 200);
    TIDE_CHECK(heard->glanced == 1 && heard->sureGlanced == 1 && heard->glancedTick == 150);
    TIDE_CHECK(heard->glancedAt.x == 1.5f && heard->glancedAt.y == -1.0f);
    TIDE_CHECK(glanced_predicted > 152u); // It had run past the tick by then
    TIDE_CHECK(glanced_sure > 150u);      // ...and the server had confirmed it by the time its verified handler ran
    TIDE_CHECK(heard->struck == 4 && heard->kinds == ((1 << 1) | (1 << 2) | (1 << 3) | (1 << 4)));
    TIDE_CHECK(heard->sureStruck == 2 && heard->sure == ((1 << 1) | (1 << 4)));
    TIDE_CHECK(heard->notes == 4);
    TIDE_CHECK(tide_get_Target_local(&local, target(match, 1)).struck == 4);
    // The machine that joined since hears what's sent from then on, and nothing from before
    TIDE_CHECK(tide_client_status_of(clients[2]).state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(late.Heard.rang == 0 && late.Heard.destroyed == 0 && (late.Heard.kinds & ((1 << 1) | (1 << 2))) == 0);
    TIDE_CHECK(late.Heard.sure == 1 << 4 && late.Heard.sureGlanced == 1);
    // ...but for the entities of the world it was sent, which are new to it: the scene, the targets
    // as they were by then, the two players' entities, and then its own as its tick spawned it
    TIDE_CHECK(late.Heard.spawned == 0xE && late.Heard.sureSpawned == 0xE && late.Heard.entities == 7);
    TIDE_CHECK(tide_get_Target_local(&late, target(match, 1)).born == 1);
    // ...whose handlers' queries go through that world, for both kinds of handler
    TIDE_CHECK(late.Heard.company == 0xE && late.Heard.sureCompany == 0xE && heard->company == 0xE && heard->sureCompany == 0xE);
    // A machine hears every join from its own on: the world it was sent is from before it
    TIDE_CHECK(heard->joined == 3 && heard->entities == 8 && late.Heard.joined == 1);

    // Past tick 241. Target 2 went at 200, for good. At 241 the first player's
    // machine predicted target 4, and the server had target 5 there, on the
    // same handle: another entity, so its spawn is another event, and what the
    // handler kept for 4 isn't 5's.
    run(4.6);
    match = tide_server_world(server);
    const tide_entity five = target(match, 5);
    TIDE_REQUIRE(match->Time.tick > 250 && !tide_entity_is_null(five) && tide_entity_is_null(target(match, 4)));
    TIDE_CHECK(heard->destroyed == ((1 << 3) | (1 << 2)) && heard->sureDestroyed == 1 << 2 && heard->destroyedHealth == 5);
    TIDE_REQUIRE(tide_entity_equal(five, predicted_spawn)); // One handle, two entities
    TIDE_CHECK((heard->spawned & ((1 << 4) | (1 << 5))) == ((1 << 4) | (1 << 5)));
    TIDE_CHECK(heard->sureSpawned == (0xE | 1 << 5) && spawn_sure_at > 241u);
    TIDE_CHECK(tide_get_Target_local(&local, five).born == 5);
    TIDE_CHECK(heard->shielded == 1); // The singleton as the spawn's turn left it, in the tick the server ran
    // The machine that joined late predicted the same, and put it right the same
    TIDE_CHECK(late.Heard.sureSpawned == (0xE | 1 << 5) && tide_get_Target_local(&late, five).born == 5);
    TIDE_CHECK(late.Heard.destroyed == 1 << 2 && late.Heard.sureDestroyed == 1 << 2);
    check_rang(heard); // Nothing rang again
    TIDE_CHECK(heard->sureRang == 11 && heard->struck == 4 && heard->sureStruck == 2);
    for (int i = 0; i < 3; i++) {
        TIDE_CHECK(tide_client_status_of(clients[i]).state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(tide_client_status_of(clients[i]).resyncs == 0); // Every tick's hash matched the server's
    }
    for (int i = 0; i < 3; i++) tide_client_destroy(clients[i]);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
    tide_event_log_free(&heard_0);
    tide_event_log_free(&heard_2);
    tide_local_free(&local);
    tide_local_free(&late);
}

// A machine that stopped for longer than the server keeps ticks is sent the
// world again. The ticks it missed say nothing, but what was spawned in them
// and is still there is new to it: heard as the world comes, once, by both
// kinds of handler, and nothing it had is heard again.
TIDE_TEST(local_events_a_world_sent_again_says_what_is_new)
{
    now = 0.0;
    net = tide_loopback_create(77);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.03});
    const tide_server_desc desc = {.game = &tide_game_api, .tick_rate = 60, .transports = {tide_loopback_endpoint(net, 1)}};
    server = tide_server_create(&desc, now);
    memset(clients, 0, sizeof clients);
    tide_local_init(&local);
    tide_local_init(&late);
    heard_0 = (tide_event_log){0};
    heard_2 = (tide_event_log){0};
    wrong_destroy_frames = 0;
    join(0, &heard_0);
    join(1, NULL);
    run(3.0);
    const Heard *heard = &local.Heard;
    const tide_world *match = tide_server_world(server);
    TIDE_REQUIRE(match->Time.tick > 160 && match->Time.tick < 199);
    TIDE_CHECK(heard->spawned == 0xE && heard->sureSpawned == 0xE && heard->entities == 7);
    const Heard before = *heard;

    // Its machine stops: the server goes past target 2's end, at 200, and target 5's spawn, at 241
    tide_client *stopped = clients[0];
    clients[0] = NULL;
    while (now < 7.5) {
        now += 1.0 / 60.0;
        tide_loopback_set_time(net, now);
        tide_client_update(clients[1], now);
        tide_server_update(server, now);
    }
    clients[0] = stopped;
    run(10.0);
    match = tide_server_world(server);
    const tide_entity five = target(match, 5);
    TIDE_REQUIRE(tide_client_status_of(clients[0]).state == TIDE_SESSION_CONNECTED && !tide_entity_is_null(five));
    TIDE_CHECK(tide_client_status_of(clients[0]).resyncs == 1); // The world, once
    // Target 5 is all that's new to it: heard once, with what its handler wrote for it
    TIDE_CHECK(heard->spawned == (0xE | 1 << 5) && heard->sureSpawned == (0xE | 1 << 5));
    TIDE_CHECK(heard->entities == before.entities + 1);
    TIDE_CHECK(tide_get_Target_local(&local, five).born == 5);
    // Nothing is made up for what went meanwhile, or was sent
    TIDE_CHECK(heard->destroyed == before.destroyed && heard->sureDestroyed == before.sureDestroyed);
    TIDE_CHECK(heard->joined == before.joined);
    for (int i = 0; i < 2; i++) tide_client_destroy(clients[i]);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
    tide_event_log_free(&heard_0);
    tide_event_log_free(&heard_2);
    tide_local_free(&local);
    tide_local_free(&late);
}

// ---------------------------------------------------------------------------
// One machine: a session runs the server and its own player

static void sample_own(void *user, const uint32_t tick, void *input)
{
    (void)user;
    *(Controls *)input = (Controls){.shield = shield_at(tick)};
}

TIDE_TEST(local_events_single_machine)
{
    static tide_local own;
    tide_local_init(&own);
    tide_session *s = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .tick_rate = 60, .sample = sample_own});
    double at = 0.0;
    tide_session_events(s); // Before its first tick, so it records from there
    tide_session_start(s, NULL, at);
    bool odd = false;
    while (at < 4.6) {
        at += (odd = !odd) ? 0.011 : 0.022;
        tide_session_update(s, at);
        const tide_view_worlds view = tide_session_view(s);
        tide_local_events(&own, tide_session_events(s));
        tide_draw_reset(&draw);
        tide_frame(view.current, view.previous, view.alpha, view.verified, view.verified_previous, view.verified_alpha, &own, &draw, &gui);
        TIDE_CHECK(own.Heard.sureStruck <= own.Heard.struck);
    }
    const tide_world *match = tide_session_server_world(s);
    TIDE_REQUIRE(match && match->Time.tick > 250);
    const Heard *heard = &own.Heard;
    // Its own shield is its own input: nothing was predicted wrong, and each
    // event was heard once, and once more as verified
    TIDE_CHECK(heard->struck == 2 && heard->kinds == ((1 << 1) | (1 << 4)) && heard->sureStruck == 2 && heard->sure == heard->kinds);
    check_rang(heard);
    TIDE_CHECK(heard->sureRang == 11 && heard->glanced == 1 && heard->sureGlanced == 1);
    // What its match started with is new to it too: its own player's world is one it was sent
    TIDE_CHECK(heard->spawned == (0xE | 1 << 5) && heard->sureSpawned == (0xE | 1 << 5));
    TIDE_CHECK(heard->entities == 6); // The scene, the first targets, its player, target 5
    TIDE_CHECK(heard->destroyed == 1 << 2 && heard->sureDestroyed == 1 << 2);
    TIDE_CHECK(heard->notes == 2 && heard->looked == 2);
    TIDE_CHECK(tide_session_status_of(s).client.resyncs == 0);
    tide_session_destroy(s);
    tide_local_free(&own);
}
