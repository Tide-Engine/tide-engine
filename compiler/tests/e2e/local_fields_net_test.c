#include <string.h>

#include "game.h"
#include "tide_test.h"

// Local fields on machines of a match, on one loopback network in made-up
// time: a server and three players, each machine with local state of its own.
// The second player's shield decides whether a target is destroyed at tick 90,
// whether another comes at tick 150, and which one comes at tick 240, and the
// first player's machine hears of it a round trip late: it predicts all three
// wrong, then the server puts it right. An entity's local fields go once it's
// in neither the world views draw nor the last one the server confirmed, so
// the wrong destroy takes nothing and the wrong spawn leaves nothing; and the
// target that came where it predicted another, on the same handle, gets none
// of what was kept for the one that never was.

static tide_loopback *net;
static tide_server *server;
static tide_client *clients[3];
static int who[3] = {0, 1, 2};
static double now;

// The machines whose views run: the first player's, and the third's, who joins late
static tide_local local;
static tide_local late;
static tide_draw_list draw;
static tide_gui gui;

// What the first player's machine saw of its predictions
static uint32_t wrong_destroy_frames; // Target 1 gone from the world it draws, and still in the verified one
static uint32_t wrong_spawn_frames;   // Target 3 in the world it draws, and never in the verified one
static uint32_t wrong_swap_frames;    // Target 4 in the world it draws, where the server had target 5
static tide_entity predicted_spawn;
static tide_entity predicted_swap;
static uint32_t predicted_born;

// The second player raises a shield around ticks 90, 150 and 240; the others never do.
static void sample(void *user, const uint32_t tick, void *input)
{
    const int player = *(const int *)user;
    Controls *c = input;
    *c = (Controls){0};
    c->shield = player == 1 && ((tick >= 89u && tick <= 92u) || (tick >= 149u && tick <= 152u) || (tick >= 239u && tick <= 242u));
}

static tide_client *join(const int number)
{
    const tide_client_desc desc = {
        .game = &tide_game_api,
        .transport = tide_loopback_endpoint(net, (uint32_t)number + 2u),
        .server = tide_loopback_address(1),
        .sample = sample,
        .user = &who[number],
        .lead = 2,
    };
    clients[number] = tide_client_create(&desc, now);
    return clients[number];
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

// A frame on a machine: its views, on what its client has of the match.
static void frame(tide_local *l, const tide_client *c)
{
    const tide_view_worlds view = tide_client_view(c);
    tide_draw_reset(&draw);
    tide_frame(view.current, view.previous, view.alpha, view.verified, l, &draw, &gui);
    tide_text_use(NULL, &l->heap); // For the lists the checks read
}

// The first player's frame, with what it saw of its wrong predictions: its
// local fields are there through both.
static void first_frame(void)
{
    const tide_view_worlds view = tide_client_view(clients[0]);
    const tide_world *drawn = view.current;
    const tide_world *confirmed = view.verified;
    const tide_entity one = target(confirmed, 1);
    const tide_entity three = target(drawn, 3);
    const tide_entity four = target(drawn, 4);
    const bool destroyed_wrong = drawn && !tide_entity_is_null(one) && !tide_has_Target(drawn, one);
    const bool spawned_wrong = !tide_entity_is_null(three) && tide_entity_is_null(target(confirmed, 3));
    frame(&local, clients[0]);
    if (!tide_entity_is_null(four)) {
        wrong_swap_frames++;
        predicted_swap = four;
        predicted_born = tide_entity_born(&drawn->entities, four);
        TIDE_CHECK(tide_get_Target_local(&local, four).was == 4); // Baked as what it drew
    }
    if (destroyed_wrong) {
        wrong_destroy_frames++;
        TIDE_CHECK(tide_has_Target_local(&local, one)); // Kept: the server hasn't said it's gone
    }
    if (spawned_wrong) {
        wrong_spawn_frames++;
        predicted_spawn = three;
        TIDE_CHECK(tide_has_Target_local(&local, three)); // The view baked what it drew
    }
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
        if (clients[2]) frame(&late, clients[2]);
    }
}

TIDE_TEST(local_fields_net_outlast_wrong_predictions)
{
    now = 0.0;
    net = tide_loopback_create(2024);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.05});
    const tide_server_desc desc = {.game = &tide_game_api, .tick_rate = 60, .transports = {tide_loopback_endpoint(net, 1)}};
    server = tide_server_create(&desc, now);
    memset(clients, 0, sizeof clients);
    tide_local_init(&local);
    tide_local_init(&late);
    wrong_destroy_frames = wrong_spawn_frames = wrong_swap_frames = 0;
    predicted_spawn = predicted_swap = (tide_entity){0};
    predicted_born = 0;
    join(0);
    join(1);

    // Past tick 90: the first player's machine predicted target 1 destroyed,
    // and the server, which had the shield, kept it
    run(2.2);
    const tide_world *match = tide_server_world(server);
    const tide_entity one = target(match, 1);
    const tide_entity two = target(match, 2);
    TIDE_REQUIRE(match->Time.tick > 100 && match->Time.tick < 145);
    TIDE_REQUIRE(!tide_entity_is_null(one) && !tide_entity_is_null(two));
    TIDE_CHECK(wrong_destroy_frames > 0); // It did predict it wrong, for a while
    TIDE_CHECK(tide_has_Target(tide_client_world(clients[0]), one)); // ...and has it back
    TIDE_CHECK(tide_get_Target_local(&local, one).bakes == 1); // With what it kept for it: not baked again
    TIDE_CHECK(local.Seen.bakes == 2 && local.Seen.forgotten == 0);

    // Past tick 150: it predicted a target the server never had, and what it
    // baked for it is gone with it
    run(2.75);
    match = tide_server_world(server);
    TIDE_REQUIRE(match->Time.tick > 158 && match->Time.tick < 205);
    TIDE_CHECK(match->Plan.arrived == 0);
    TIDE_CHECK(wrong_spawn_frames > 0);
    TIDE_REQUIRE(!tide_entity_is_null(predicted_spawn));
    TIDE_CHECK(!tide_has_Target_local(&local, predicted_spawn));
    TIDE_CHECK(local.Seen.bakes == 3 && local.Seen.forgotten == 1 && local.Seen.forgot == 1 << 3);
    TIDE_CHECK(tide_has_Target_local(&local, one) && tide_has_Target_local(&local, two));

    // A machine that joins now starts from the defaults, for the entities it finds
    join(2);
    run(3.2);
    TIDE_CHECK(tide_client_status_of(clients[2]).state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(late.Seen.bakes == 2 && late.Seen.forgotten == 0);
    TIDE_CHECK(tide_get_Target_local(&late, two).bakes == 1);
    TIDE_CHECK(tide_get_Target_local(&late, two).seen > 0);
    TIDE_CHECK(tide_get_Target_local(&late, two).seen < tide_get_Target_local(&local, two).seen);

    // Past tick 210, target 1 goes for good: once the server says so too, so
    // do its local fields, on both machines
    run(3.75);
    match = tide_server_world(server);
    TIDE_REQUIRE(match->Time.tick > 218 && match->Time.tick < 235 && tide_entity_is_null(target(match, 1)));
    TIDE_CHECK(!tide_has_Target_local(&local, one) && tide_has_Target_local(&local, two));
    TIDE_CHECK(local.Seen.forgotten == 2 && local.Seen.forgot == ((1 << 3) | (1 << 1)));
    TIDE_CHECK(local.Seen.bakes == 3);
    TIDE_CHECK(!tide_has_Target_local(&late, one) && late.Seen.forgotten == 1 && late.Seen.forgot == 1 << 1);

    // Past tick 240: it predicted target 4, baked it, and the server had target
    // 5 there, on the same handle. What it baked for 4 went with 4, and 5 is
    // baked as itself: a handle a replay gave to another entity is another
    // entity's, by what each was born as
    run(4.6);
    match = tide_server_world(server);
    const tide_entity five = target(match, 5);
    TIDE_REQUIRE(match->Time.tick > 250 && !tide_entity_is_null(five) && tide_entity_is_null(target(match, 4)));
    TIDE_CHECK(wrong_swap_frames > 0);
    TIDE_REQUIRE(tide_entity_equal(five, predicted_swap)); // One handle, two entities
    const tide_world *mine = tide_client_world(clients[0]);
    TIDE_CHECK(predicted_born != 0u && tide_entity_born(&mine->entities, five) != 0u);
    TIDE_CHECK(tide_entity_born(&mine->entities, five) != predicted_born);
    TIDE_CHECK(tide_get_Target_local(&local, five).was == 5 && tide_get_Target_local(&local, five).bakes == 1);
    TIDE_CHECK(local.Seen.bakes == 5 && local.Seen.forgotten == 3);
    TIDE_CHECK(local.Seen.forgot == ((1 << 3) | (1 << 1) | (1 << 4)));
    // The machine that joined late got its world from the server, and has
    // spawned since as any other: it predicted the same, and put it right the same
    TIDE_CHECK(tide_get_Target_local(&late, five).was == 5 && tide_get_Target_local(&late, five).bakes == 1);
    TIDE_CHECK(late.Seen.bakes == 4 && late.Seen.forgotten == 2 && late.Seen.forgot == ((1 << 1) | (1 << 4)));
    TIDE_CHECK(tide_get_Target_local(&late, two).bakes == 1); // Known by its handle: it came with the world
    for (int i = 0; i < 3; i++) {
        TIDE_CHECK(tide_client_status_of(clients[i]).state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(tide_client_status_of(clients[i]).resyncs == 0); // Every tick's hash matched the server's
    }

    // Out of the match, a machine keeps nothing for it
    tide_draw_reset(&draw);
    tide_frame(NULL, NULL, 1.0f, NULL, &local, &draw, &gui);
    TIDE_CHECK(local.tide_fields_Target.rows.count == 0 && local.Seen.forgotten == 5);
    for (int i = 0; i < 3; i++) tide_client_destroy(clients[i]);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
    tide_local_free(&local);
    tide_local_free(&late);
}
