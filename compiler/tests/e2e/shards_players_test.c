#include "game.h"
#include "tide/shard_client.h"
#include "tide/shards.h"
#include "tide_test.h"

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
