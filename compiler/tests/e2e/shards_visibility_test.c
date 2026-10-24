#include "game.h"
#include "tide/shards.h"
#include "tide_test.h"

#include <string.h>

// e2e/shards_visibility.tide on the engine's orchestrator: the movers the
// root's group keeps from player 1 are hidden from them on every shard that
// has them, owned or as a ghost, and shown to player 0; and once code on the
// right region opens the group, every shard shows them to both, but not
// their Secret, which is player 0's alone on whichever shard has them. Each player
// also has a client of the right region, as a player there would, whose world
// has what that shard lets them see.

static tide_loopback *net;
static tide_shards *shards;
static tide_client *roots[2], *rights[2];
static double now;

static void transports(void *user, const uint32_t shard, tide_transport out[2])
{
    (void)user;
    out[0] = tide_loopback_endpoint(net, 10u + shard);
}

static tide_client *client_of(const uint32_t shard, const uint32_t endpoint, const uint64_t cookie)
{
    const tide_client_desc d = {.game = &tide_game_api, .transport = tide_loopback_endpoint(net, endpoint),
                                .server = tide_loopback_address(10u + shard), .lead = 2, .cookie = cookie};
    return tide_client_create(&d, now);
}

static void step(const double seconds)
{
    const double until = now + seconds;
    while (now < until) {
        now += 1.0 / 120.0;
        tide_loopback_set_time(net, now);
        tide_shards_update(shards, now);
        for (int p = 0; p < 2; p++) {
            if (roots[p]) tide_client_update(roots[p], now);
            if (rights[p]) tide_client_update(rights[p], now);
            // Once the root gave this player a cookie, they're let into the right region with it
            const tide_client_status root = roots[p] ? tide_client_status_of(roots[p]) : (tide_client_status){0};
            if (!rights[p] && root.state == TIDE_SESSION_CONNECTED && root.cookie) rights[p] = client_of(2, 200u + (uint32_t)p, root.cookie);
        }
        // Player 1 joins after player 0, so they're players 0 and 1
        if (!roots[1] && roots[0] && tide_client_status_of(roots[0]).state == TIDE_SESSION_CONNECTED) roots[1] = client_of(0, 101, 0);
    }
}

// How many of the hidden movers (ids 0 to 3) the shards have, owned or as
// ghosts, and how many of those `player` may see there
static void hidden_movers(const int player, int *held, int *seen, int *owned)
{
    *held = *seen = *owned = 0;
    for (uint32_t i = 0; i < tide_shards_count(shards); i++) {
        const tide_world *w = tide_shards_world(shards, i);
        for (uint32_t index = 0; w && index < w->entities.next_unused; index++) {
            if (!w->entities.page[index >> TIDE_ENTITY_PAGE_SHIFT]) {
                index |= (1u << TIDE_ENTITY_PAGE_SHIFT) - 1u;
                continue;
            }
            const tide_entity e = tide_entity_in_slot(&w->entities, index);
            if (tide_entity_is_null(e) || !tide_has_Mover(w, e) || tide_get_Mover(w, e).id > 3) continue;
            (*held)++;
            if (!tide_has_tide_ghost(w, e)) (*owned)++;
            if (tide_world_visible(w, e, tide_player_from_index(player))) (*seen)++;
        }
    }
}

// The hidden movers a client's world has, and of them those with their Secret
static int in_client_with(const tide_client *c, const bool secret)
{
    const tide_world *w = c ? tide_client_verified_world(c) : NULL;
    int count = 0;
    for (uint32_t index = 0; w && index < w->entities.next_unused; index++) {
        if (!w->entities.page[index >> TIDE_ENTITY_PAGE_SHIFT]) {
            index |= (1u << TIDE_ENTITY_PAGE_SHIFT) - 1u;
            continue;
        }
        const tide_entity e = tide_entity_in_slot(&w->entities, index);
        if (!tide_entity_is_null(e) && tide_has_Mover(w, e) && tide_get_Mover(w, e).id <= 3 && (!secret || tide_has_Secret(w, e))) count++;
    }
    return count;
}

static int in_client(const tide_client *c)
{
    return in_client_with(c, false);
}

TIDE_TEST(shards_keep_a_group_hidden_on_every_shard_and_open_it_from_a_region)
{
    const uint64_t dropped = tide_shard_dropped();
    now = 0;
    net = tide_loopback_create(7);
    const tide_shards_desc d = {.game = &tide_game_api, .tick_rate = 60, .world = {{-40.0, -10.0, 0.0}, {40.0, 10.0, 0.0}},
                                .split = {2, 1, 1}, .transports = transports};
    shards = tide_shards_create(&d, now);
    TIDE_REQUIRE(shards != NULL);
    roots[0] = client_of(0, 100, 0);
    // The movers go from the root to the left region, and on toward the
    // border: hidden from player 1 wherever they are, on the shards and in
    // the right region's world as player 1's client there has it
    int held, seen, owned, most_right = 0;
    for (int i = 0; i < 300; i++) { // 2.5 seconds: x from about -14 to -4
        step(1.0 / 120.0);
        hidden_movers(1, &held, &seen, &owned);
        TIDE_CHECK(seen == 0);
        TIDE_CHECK(in_client(rights[1]) == 0);
        if (in_client(rights[0]) > most_right) most_right = in_client(rights[0]);
    }
    TIDE_REQUIRE(rights[0] != NULL && rights[1] != NULL);
    TIDE_CHECK(tide_player_index(tide_client_status_of(rights[1]).player) == 1);
    TIDE_CHECK(most_right == 4); // Player 0's client of the right region had them all, as ghosts
    hidden_movers(0, &held, &seen, &owned);
    TIDE_CHECK(owned == 4);
    TIDE_CHECK(held > owned); // Ghosts of them too, on the right region near the border
    TIDE_CHECK(seen == held); // ...all shown to player 0, as every shard knows the group
    // Until mover 0 is past x = 10, on the right, which opens the group:
    // that waits for the root's directory, then every shard shows them all
    for (int i = 0; i < 720; i++) {
        step(1.0 / 120.0);
        hidden_movers(1, &held, &seen, &owned);
        if (owned == 4 && seen == held && seen >= 4 && in_client(rights[1]) > 0) break;
    }
    hidden_movers(1, &held, &seen, &owned);
    TIDE_CHECK(owned == 4);
    TIDE_CHECK(seen == held && seen >= 4);
    TIDE_CHECK(in_client(rights[1]) > 0); // Player 1's client of the right region sees what it has of them now
    // ...but not their Secret, though they came from other shards, which had its restriction
    TIDE_CHECK(in_client_with(rights[1], true) == 0);
    TIDE_CHECK(in_client_with(rights[0], true) == in_client(rights[0]) && in_client(rights[0]) > 0);
    TIDE_CHECK(tide_shard_dropped() == dropped); // The group's change waited, and was made, not dropped
    for (int p = 0; p < 2; p++) {
        const tide_client_status status = tide_client_status_of(rights[p]);
        TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED);
        tide_client_destroy(roots[p]);
        tide_client_destroy(rights[p]);
        roots[p] = rights[p] = NULL;
    }
    tide_shards_destroy(shards);
    shards = NULL;
    tide_loopback_destroy(net);
}
