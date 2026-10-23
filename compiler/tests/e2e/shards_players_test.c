#include "game.h"
#include "tide/hosting.h"
#include "tide/shard_client.h"
#include "tide/shard_coordinator.h"
#include "tide/shards.h"
#include "tide_test.h"

#include <stdlib.h>
#include <string.h>

// e2e/shards_players.tide: a player's machine (tide/shard_client.h) joins the
// world at the root and follows its avatar from shard to shard as it walks
// right across the border at x = 0, playing in whichever owns it.

static tide_loopback *net;
static tide_shards *shards;
static tide_shard_client *player;
static double now;
static double walk_until;

static void transports(void *user, const uint32_t shard, tide_transport out[2])
{
    (void)user;
    out[0] = tide_loopback_endpoint(net, 10u + shard);
}

static bool reach(void *user, const uint32_t shard, const uint64_t match, tide_transport *transport, tide_address *server)
{
    (void)user;
    (void)match;
    *transport = tide_loopback_endpoint(net, 100u + shard);
    *server = tide_loopback_address(10u + shard);
    return true;
}

static void sample(void *user, const uint32_t tick, void *input)
{
    (void)user;
    (void)tick;
    *(Controls *)input = (Controls){.move = {now < walk_until ? 1.0f : 0.0f, 0.0f}};
}

// The avatar as the shards that own it have it: how many own it, and where
static int owners(tide_entity *found, Avatar *avatar)
{
    int count = 0;
    for (uint32_t i = 0; i < tide_shards_count(shards); i++) {
        const tide_world *w = tide_shards_world(shards, i);
        if (!w) continue;
        for (uint32_t index = 0; index < w->entities.next_unused; index++) {
            if (!w->entities.page[index >> TIDE_ENTITY_PAGE_SHIFT]) {
                index |= (1u << TIDE_ENTITY_PAGE_SHIFT) - 1u;
                continue;
            }
            const tide_entity e = tide_entity_in_slot(&w->entities, index);
            if (tide_entity_is_null(e) || !tide_has_Avatar(w, e) || tide_has_tide_ghost(w, e)) continue;
            *found = e;
            *avatar = tide_get_Avatar(w, e);
            count++;
        }
    }
    return count;
}

static void run(const double seconds)
{
    const double until = now + seconds;
    while (now < until) {
        now += 1.0 / 120.0;
        tide_loopback_set_time(net, now);
        tide_shards_update(shards, now);
        tide_shard_client_update(player, now);
        tide_entity e;
        Avatar a;
        TIDE_CHECK(owners(&e, &a) <= 1); // Never two shards' at once
    }
}

TIDE_TEST(shards_a_player_follows_their_avatar_across_borders)
{
    now = 0;
    walk_until = 0;
    net = tide_loopback_create(3);
    const tide_shards_desc d = {.game = &tide_game_api, .tick_rate = 60, .world = {{-40, -10, 0}, {40, 10, 0}},
                                .split = {2, 1, 1}, .transports = transports};
    shards = tide_shards_create(&d, now);
    TIDE_REQUIRE(shards != NULL);
    const tide_shard_client_desc pd = {
        .client = {.game = &tide_game_api, .transport = tide_loopback_endpoint(net, 100u), .server = tide_loopback_address(10u),
                   .sample = sample, .lead = 2},
        .reach = reach,
    };
    player = tide_shard_client_create(&pd, now);
    TIDE_REQUIRE(player != NULL);

    // It joins at the root, which spawns its avatar; the avatar goes to the
    // left region, and its machine's home with it
    run(2.0);
    tide_entity e;
    Avatar a;
    TIDE_CHECK(owners(&e, &a) == 1);
    TIDE_CHECK(tide_shard_client_home_shard(player) == 1);
    const tide_player_id id = tide_client_status_of(tide_shard_client_home(player)).player;
    TIDE_CHECK(id.id != 0);

    // Walking right for four seconds: across the border at x = 0, into the right region
    walk_until = now + 4.0;
    run(6.0);
    TIDE_CHECK(owners(&e, &a) == 1);
    TIDE_CHECK(tide_shard_client_home_shard(player) == 2);
    TIDE_CHECK(tide_shard_client_moves(player) >= 2);
    // Its input moved it the whole way, whichever shard had it
    TIDE_CHECK(a.position.x > 18.0f && a.position.x < 21.0f);
    TIDE_CHECK(a.moved >= 230 && a.moved <= 250);
    // The same player on every shard it was in, and every world as its shard's
    for (uint32_t i = 0; i < 3; i++) {
        const tide_client *c = tide_shard_client_session(player, i);
        if (!c) continue;
        const tide_client_status status = tide_client_status_of(c);
        TIDE_CHECK(status.player.id == id.id);
        TIDE_CHECK(status.resyncs == 0);
    }
    const tide_client_status home = tide_client_status_of(tide_shard_client_home(player));
    TIDE_CHECK(home.state == TIDE_SESSION_CONNECTED);

    tide_shard_client_destroy(player);
    tide_shards_destroy(shards);
    tide_loopback_destroy(net);
}

// A machine whose player stays on the left watches an area on the right
// (Session.Watch): it follows the right region's shard too, and its views draw
// every marker once, the one near the border from the shard that owns it, not
// from the copy the left one holds. Watching no more, it lets that shard go.
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;
static tide_event_log heard; // What the shards it follows sent, for its local handlers

static Sight frame(void)
{
    tide_view_worlds more[TIDE_SESSION_VIEWS];
    uint32_t ids[TIDE_SESSION_VIEWS];
    const uint32_t n = tide_shard_client_views(player, more, ids, TIDE_SESSION_VIEWS);
    tide_frame_shards(more, n);
    tide_local_events(&local, &heard);
    const tide_view_worlds home = tide_client_view(tide_shard_client_home(player));
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(640.0f, 480.0f), NULL);
    tide_frame(home.current, home.previous, home.alpha, home.verified, home.verified_previous, home.verified_alpha, &local,
               &draw, &gui);
    tide_gui_end(&gui, &draw);
    tide_shard_client_mark_presented(player);
    // What its views asked of the session: the area to watch, as a host hands it over
    tide_session_request request;
    tide_start start;
    while (tide_local_take_request(&local, &request, &start)) {
        if (request.kind == TIDE_REQUEST_WATCH) tide_shard_client_watch(player, &request.area);
        if (request.kind == TIDE_REQUEST_UNWATCH) tide_shard_client_watch(player, NULL);
    }
    return local.Sight;
}

// Steps, each with a frame, and the most markers any frame drew; a frame
// that didn't draw the avatar once fails
static int watch_run(const double seconds)
{
    int most = 0;
    const double until = now + seconds;
    while (now < until) {
        run(1.0 / 120.0);
        const Sight sight = frame();
        if (sight.drawn > most) most = sight.drawn;
        if (sight.queried > most) most = sight.queried;
        TIDE_CHECK(sight.avatars == 1 || now < 1.0); // Once it's spawned
    }
    return most;
}

TIDE_TEST(shards_a_machine_watches_an_area_far_from_its_player)
{
    now = 0;
    walk_until = 0;
    net = tide_loopback_create(3);
    const tide_shards_desc d = {.game = &tide_game_api, .tick_rate = 60, .world = {{-40, -10, 0}, {40, 10, 0}},
                                .split = {2, 1, 1}, .transports = transports};
    shards = tide_shards_create(&d, now);
    TIDE_REQUIRE(shards != NULL);
    heard = (tide_event_log){.taken = true};
    const tide_shard_client_desc pd = {
        .client = {.game = &tide_game_api, .transport = tide_loopback_endpoint(net, 100u), .server = tide_loopback_address(10u),
                   .sample = sample, .lead = 2, .events = &heard},
        .reach = reach,
    };
    player = tide_shard_client_create(&pd, now);
    TIDE_REQUIRE(player != NULL);
    tide_local_init(&local);

    // Its avatar on the left, it sees the copy the left region holds of the marker near the border
    TIDE_CHECK(watch_run(2.0) <= 4);
    TIDE_CHECK(tide_shard_client_home_shard(player) == 1);
    Sight sight = frame();
    TIDE_CHECK(sight.drawn == 1 && sight.queried == 1 && sight.named == 0);
    TIDE_CHECK(sight.x > 2.5f && sight.x < 3.5f);
    TIDE_CHECK(tide_shard_client_session(player, 2) == NULL);
    TIDE_CHECK(sight.blinks == 0); // The markers are the right's, and it hears none of the right's code
    TIDE_CHECK(sight.shown == 1 && sight.hidden == 0); // The copy of the near one

    // Watching the right: every marker once, the near one from the right's own world
    local.Sight.watching = true;
    TIDE_CHECK(watch_run(2.0) <= 4);
    TIDE_CHECK(tide_shard_client_session(player, 2) != NULL);
    TIDE_CHECK(tide_shard_client_home_shard(player) == 1); // It plays where its avatar is, as before
    sight = frame();
    TIDE_CHECK(sight.drawn == 4 && sight.queried == 4);
    TIDE_CHECK(sight.named == 3); // The far ones' names, read from the right's world
    TIDE_CHECK(sight.found == 3); // ...and through World, which finds them there
    TIDE_CHECK(sight.x > 92.5f && sight.x < 93.5f);
    const tide_client_status right = tide_client_status_of(tide_shard_client_session(player, 2));
    TIDE_CHECK(right.state == TIDE_SESSION_CONNECTED && right.resyncs == 0);
    TIDE_CHECK(sight.blinks >= 8); // The right's markers' blinks, from the right's own ticks
    TIDE_CHECK(sight.shown == 4 && sight.hidden == 0); // The far ones came to be seen; the near one already was

    // Watching no more: the right's shard goes, and the copy is what it sees again
    local.Sight.watching = false;
    TIDE_CHECK(watch_run(1.0) <= 4);
    TIDE_CHECK(tide_shard_client_session(player, 2) == NULL);
    sight = frame();
    TIDE_CHECK(sight.drawn == 1 && sight.queried == 1);
    TIDE_CHECK(sight.shown == 4 && sight.hidden == 3); // The far ones went; the near one's copy stayed

    // Walking across the border, its avatar is drawn once every frame: by the
    // shard that owns it, or while it's on its way, by the first with a copy.
    // It joins the right's shard once: while the avatar's on its way from one
    // to the other, which neither owns, its route keeps both.
    const uint32_t joins = tide_shard_client_joins(player);
    walk_until = now + 4.0;
    watch_run(5.0);
    TIDE_CHECK(tide_shard_client_home_shard(player) == 2);
    TIDE_CHECK(tide_shard_client_joins(player) == joins + 1u);
    sight = frame();
    TIDE_CHECK(sight.shown == 7 && sight.hidden == 3); // The far ones again, each once, as the right came near

    tide_local_free(&local);
    tide_event_log_free(&heard);
    tide_shard_client_destroy(player);
    tide_shards_destroy(shards);
    tide_loopback_destroy(net);
}

// A sharded world on a dedicated server (tide/hosting.h): each shard one of
// its matches, the root its front, which a request for match 0 joins. A
// session let in at the front follows its avatar to the shards it walks into,
// each a match it's let into over the front's own connection.
static uint64_t entropy_state;

static uint64_t entropy(void)
{
    entropy_state += 0x9E3779B97F4A7C15ull;
    return entropy_state;
}

static const tide_world *home_avatar(const tide_session *s, Avatar *found)
{
    const tide_world *w = tide_session_world(s);
    int owned = 0;
    for (uint32_t index = 0; w && index < w->entities.next_unused; index++) {
        if (!w->entities.page[index >> TIDE_ENTITY_PAGE_SHIFT]) {
            index |= (1u << TIDE_ENTITY_PAGE_SHIFT) - 1u;
            continue;
        }
        const tide_entity e = tide_entity_in_slot(&w->entities, index);
        if (tide_entity_is_null(e) || !tide_has_Avatar(w, e) || tide_has_tide_ghost(w, e)) continue;
        *found = tide_get_Avatar(w, e);
        owned++;
    }
    return owned == 1 ? w : NULL;
}

TIDE_TEST(shards_a_player_joins_a_dedicated_sharded_world)
{
    now = 0;
    walk_until = 0;
    net = tide_loopback_create(5);
    tide_hosting *host = tide_hosting_create(&(tide_hosting_desc){.game = &tide_game_api, .transport = tide_loopback_endpoint(net, 1u),
                                                                  .entropy = entropy, .allow_anonymous_join = true}, now);
    TIDE_REQUIRE(host != NULL);
    shards = tide_shards_create(&(tide_shards_desc){.game = &tide_game_api, .tick_rate = 60, .world = {{-40, -10, 0}, {40, 10, 0}},
                                                    .split = {2, 1, 1}, .host_start = tide_shards_hosting_start,
                                                    .host_stop = tide_shards_hosting_stop, .user = host}, now);
    TIDE_REQUIRE(shards != NULL);
    TIDE_CHECK(tide_hosting_match_count(host) == 3);
    tide_hosting_client_desc front = {.transport = tide_loopback_endpoint(net, 2u), .server = tide_loopback_address(1u),
                                      .match = 0, .nonce = 77};
    memcpy(front.simulation, tide_game_api.simulation, 32);
    tide_hosting_client *admission = tide_hosting_client_create(&front, now);
    tide_session *s = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .tick_rate = 60, .sample = sample});
    TIDE_REQUIRE(admission != NULL && s != NULL);
    bool joined = false;
    for (int step = 0; step < 1200; step++) {
        now += 1.0 / 120.0;
        if (step == 240) walk_until = now + 4.0;
        tide_loopback_set_time(net, now);
        tide_hosting_update(host, now);
        tide_shards_update(shards, now);
        tide_hosting_client_update(admission, now);
        if (!joined && tide_hosting_client_result(admission) == TIDE_HOSTING_ACCEPTED) {
            tide_session_join(s, tide_hosting_client_transport(admission), tide_hosting_client_address(admission), now);
            joined = true;
        }
        tide_session_update(s, now);
    }
    TIDE_CHECK(joined);
    TIDE_CHECK(tide_server_player_count(tide_shards_server(shards, 2)) == 1); // Playing in the right region's match
    Avatar a = {0};
    TIDE_CHECK(home_avatar(s, &a) != NULL); // It plays in the shard that owns its avatar...
    TIDE_CHECK(a.position.x > 18.0f && a.position.x < 21.0f); // ...across the border, its input there the whole way
    const tide_session_status status = tide_session_status_of(s);
    TIDE_CHECK(status.client.state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(status.client.resyncs == 0);

    // Watching the left from the right: the left's match, as the map has it, let in beside the others
    TIDE_CHECK(tide_server_player_count(tide_shards_server(shards, 1)) == 0);
    tide_session_watch(s, &(tide_shard_box){{-35, -5, 0}, {-25, 5, 0}});
    for (int step = 0; step < 240; step++) {
        now += 1.0 / 120.0;
        tide_loopback_set_time(net, now);
        tide_hosting_update(host, now);
        tide_shards_update(shards, now);
        tide_hosting_client_update(admission, now);
        tide_session_update(s, now);
    }
    TIDE_CHECK(tide_server_player_count(tide_shards_server(shards, 1)) == 1);
    tide_view_worlds views[4];
    uint32_t ids[4];
    TIDE_CHECK(tide_session_views(s, views, ids, 4) == 1);
    TIDE_CHECK(ids[0] == 1 && views[0].current != NULL);
    TIDE_CHECK(home_avatar(s, &a) != NULL); // Still at home where its avatar is
    tide_session_destroy(s);
    tide_hosting_client_destroy(admission);
    tide_shards_destroy(shards);
    tide_hosting_destroy(host);
    tide_loopback_destroy(net);
}

// The standard host's way: a session that starts a sharded game's match runs
// every shard on this machine, its player at home wherever its avatar is
TIDE_TEST(shards_a_session_plays_a_sharded_match_on_this_machine)
{
    now = 0;
    walk_until = 0;
    tide_session *s = tide_session_create(&(tide_session_desc){
        .game = &tide_game_api, .tick_rate = 60, .sample = sample,
        .shard_world = {{-40, -10, 0}, {40, 10, 0}}, .shard_split = {2, 1, 1}});
    TIDE_REQUIRE(s != NULL);
    tide_session_start(s, NULL, now);
    const tide_world *w = NULL;
    for (int i = 0; i < 240; i++) {
        now += 1.0 / 120.0;
        tide_session_update(s, now);
    }
    TIDE_CHECK(tide_session_status_of(s).client.state == TIDE_SESSION_CONNECTED);
    walk_until = now + 4.0;
    for (int i = 0; i < 720; i++) {
        now += 1.0 / 120.0;
        tide_session_update(s, now);
    }
    // The world it plays in is the one that owns its avatar, at the walk's end
    w = tide_session_world(s);
    TIDE_REQUIRE(w != NULL);
    int owned = 0;
    for (uint32_t index = 0; index < w->entities.next_unused; index++) {
        if (!w->entities.page[index >> TIDE_ENTITY_PAGE_SHIFT]) {
            index |= (1u << TIDE_ENTITY_PAGE_SHIFT) - 1u;
            continue;
        }
        const tide_entity e = tide_entity_in_slot(&w->entities, index);
        if (tide_entity_is_null(e) || !tide_has_Avatar(w, e) || tide_has_tide_ghost(w, e)) continue;
        owned++;
        TIDE_CHECK(tide_get_Avatar(w, e).position.x > 18.0f);
    }
    TIDE_CHECK(owned == 1);
    TIDE_CHECK(tide_session_status_of(s).client.resyncs == 0);
    tide_session_destroy(s);
}

// Hot reloading a sharded match on this machine: every shard and every shard
// its player follows changes build at once, at a moment nothing the old build
// wrote is on its way between them (tide_session_can_reload). Each build here
// is a copy of the game's table; the one swapped out is zeroed, so anything
// still using it would fail. The second has another layout, carried over by
// copying each world, and the walk goes on across the border as it would have.
static int migrations;

static bool copy_over(void *user, const void *from, void *to)
{
    (void)user;
    migrations++;
    tide_world_copy(to, from);
    return true;
}

static void update_until_reloadable(tide_session *s)
{
    for (int i = 0; i < 600 && !tide_session_can_reload(s); i++) {
        now += 1.0 / 120.0;
        tide_session_update(s, now);
    }
    TIDE_REQUIRE(tide_session_can_reload(s));
}

static tide_game builds[3];

TIDE_TEST(shards_a_sharded_match_reloads_every_shard_at_once)
{
    now = 0;
    walk_until = 0;
    for (int i = 0; i < 3; i++) builds[i] = tide_game_api;
    builds[2].hash ^= 1u; // Another layout
    tide_session *s = tide_session_create(&(tide_session_desc){
        .game = &builds[0], .tick_rate = 60, .sample = sample,
        .shard_world = {{-40, -10, 0}, {40, 10, 0}}, .shard_split = {2, 1, 1}});
    TIDE_REQUIRE(s != NULL);
    tide_session_start(s, NULL, now);
    for (int i = 0; i < 240; i++) {
        now += 1.0 / 120.0;
        tide_session_update(s, now);
    }
    TIDE_REQUIRE(tide_session_status_of(s).client.state == TIDE_SESSION_CONNECTED);
    walk_until = now + 4.0;
    for (int i = 0; i < 120; i++) {
        now += 1.0 / 120.0;
        tide_session_update(s, now);
    }
    // New code, the same layout
    update_until_reloadable(s);
    tide_session_set_game(s, &builds[1]);
    memset(&builds[0], 0, sizeof builds[0]);
    for (int i = 0; i < 120; i++) {
        now += 1.0 / 120.0;
        tide_session_update(s, now);
    }
    // Another layout, as the walk crosses the border
    update_until_reloadable(s);
    migrations = 0;
    TIDE_REQUIRE(tide_session_migrate(s, &builds[2], copy_over, NULL));
    TIDE_CHECK(migrations == 3); // The root's world and both regions'
    memset(&builds[1], 0, sizeof builds[1]);
    for (int i = 0; i < 720; i++) {
        now += 1.0 / 120.0;
        tide_session_update(s, now);
    }
    const tide_world *w = tide_session_world(s);
    TIDE_REQUIRE(w != NULL);
    int owned = 0;
    for (uint32_t index = 0; index < w->entities.next_unused; index++) {
        if (!w->entities.page[index >> TIDE_ENTITY_PAGE_SHIFT]) {
            index |= (1u << TIDE_ENTITY_PAGE_SHIFT) - 1u;
            continue;
        }
        const tide_entity e = tide_entity_in_slot(&w->entities, index);
        if (tide_entity_is_null(e) || !tide_has_Avatar(w, e) || tide_has_tide_ghost(w, e)) continue;
        owned++;
        TIDE_CHECK(tide_get_Avatar(w, e).position.x > 18.0f); // It walked all the way, through both reloads
    }
    TIDE_CHECK(owned == 1);
    const tide_session_status status = tide_session_status_of(s);
    TIDE_CHECK(status.client.state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(status.client.resyncs == 0); // Each world it follows took its server's, carried over
    tide_session_destroy(s);
}

// A web build's hot reload: the old program hands the next its sharded match
// whole (tide_session_save_shards), every shard's world carried over in the
// checkpoint (tide_shard_checkpoint_rewrite, here by copying), and the new
// one goes on from it with this machine's player back as themselves: one
// avatar, walking on across the border.
static uint8_t *copy_world(void *user, const uint32_t shard, const uint8_t *world, const uint32_t size, uint32_t *out_size)
{
    (void)shard;
    (*(int *)user)++;
    uint8_t *copy = malloc(size ? size : 1u);
    if (copy) memcpy(copy, world, size);
    *out_size = size;
    return copy;
}

static int avatars(const tide_world *w, Avatar *found)
{
    int count = 0;
    for (uint32_t index = 0; w && index < w->entities.next_unused; index++) {
        if (!w->entities.page[index >> TIDE_ENTITY_PAGE_SHIFT]) {
            index |= (1u << TIDE_ENTITY_PAGE_SHIFT) - 1u;
            continue;
        }
        const tide_entity e = tide_entity_in_slot(&w->entities, index);
        if (tide_entity_is_null(e) || !tide_has_Avatar(w, e) || tide_has_tide_ghost(w, e)) continue;
        *found = tide_get_Avatar(w, e);
        count++;
    }
    return count;
}

TIDE_TEST(shards_a_sharded_match_goes_on_in_another_program)
{
    now = 0;
    walk_until = 0;
    const tide_session_desc desc = {.game = &tide_game_api, .tick_rate = 60, .sample = sample,
                                    .shard_world = {{-40, -10, 0}, {40, 10, 0}}, .shard_split = {2, 1, 1}};
    tide_session *s = tide_session_create(&desc);
    TIDE_REQUIRE(s != NULL);
    tide_session_start(s, NULL, now);
    for (int i = 0; i < 240; i++) {
        now += 1.0 / 120.0;
        tide_session_update(s, now);
    }
    walk_until = now + 4.0;
    for (int i = 0; i < 120; i++) {
        now += 1.0 / 120.0;
        tide_session_update(s, now);
    }
    update_until_reloadable(s);
    uint32_t size = 0;
    uint64_t cookie = 0;
    uint8_t *saved = tide_session_save_shards(s, &size, &cookie);
    TIDE_REQUIRE(saved != NULL && cookie != 0);
    const tide_player_id player = tide_session_status_of(s).client.player;
    tide_session_destroy(s);

    int worlds = 0;
    uint32_t carried_size = 0;
    uint8_t *carried = tide_shard_checkpoint_rewrite(saved, size, copy_world, &worlds, &carried_size);
    free(saved);
    TIDE_REQUIRE(carried != NULL);
    TIDE_CHECK(worlds == 3);
    s = tide_session_create(&desc);
    TIDE_REQUIRE(s != NULL);
    tide_session_start_shards_from(s, carried, carried_size, cookie, now);
    free(carried);
    for (int i = 0; i < 600; i++) {
        now += 1.0 / 120.0;
        tide_session_update(s, now);
    }
    const tide_session_status status = tide_session_status_of(s);
    TIDE_CHECK(status.client.state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(status.client.player.id == player.id); // The same player, joined no second time
    Avatar a = {0};
    int every = 0; // Across every shard: PlayerJoined spawned no second avatar
    for (uint32_t i = 0; i < tide_shards_count(tide_session_shards(s)); i++) {
        Avatar b;
        every += avatars(tide_shards_world(tide_session_shards(s), i), &b);
    }
    TIDE_CHECK(every == 1);
    TIDE_CHECK(avatars(tide_session_world(s), &a) == 1);
    TIDE_CHECK(a.position.x > 18.0f);
    tide_session_destroy(s);
}
