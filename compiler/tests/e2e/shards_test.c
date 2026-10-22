#include "game.h"
#include "tide/shards.h"
#include "tide_test.h"

#include <stdlib.h>
#include <string.h>

// e2e/shards.tide on the engine's orchestrator: the root and regions split at
// x = 0, a client watching each shard over a loopback network. Nothing is
// ever owned by two shards at once, no coin is made or lost, what waits on a
// border gets done, and every client keeps its shard's world tick for tick.

#define CLIENTS 8u

static tide_loopback *net;
static tide_shards *shards;
static tide_client *clients[CLIENTS];
static double now;

static void transports(void *user, const uint32_t shard, tide_transport out[2])
{
    (void)user;
    out[0] = tide_loopback_endpoint(net, 10u + shard);
}

static tide_shards_desc desc_of(const uint32_t most)
{
    return (tide_shards_desc){
        .game = &tide_game_api,
        .tick_rate = 60,
        .world = {{-40.0, -10.0, 0.0}, {40.0, 10.0, 0.0}},
        .split = {2, 1, 1},
        .most_entities = most,
        .transports = transports,
    };
}

// A client watching each shard, as one player of the world: it joins at the
// root, and each region takes it with the cookie the root gave it
static void watch(const uint32_t shard)
{
    if (shard >= CLIENTS || clients[shard]) return;
    uint64_t cookie = 0;
    if (shard != TIDE_SHARD_ROOT) {
        if (!clients[TIDE_SHARD_ROOT]) return;
        const tide_client_status root = tide_client_status_of(clients[TIDE_SHARD_ROOT]);
        if (root.state != TIDE_SESSION_CONNECTED || !root.cookie) return;
        cookie = root.cookie;
    }
    const tide_client_desc d = {.game = &tide_game_api, .transport = tide_loopback_endpoint(net, 100u + shard),
                                .server = tide_loopback_address(10u + shard), .lead = 2, .cookie = cookie};
    clients[shard] = tide_client_create(&d, now);
}

typedef struct totals {
    int owned[128]; // By mover id (the banks are 100 and 101): how many shards own it
    int distinct;   // Ids some shard owns
    int64_t coins, wallets, left, received; // Of what shards own
} totals;

static void gather(const tide_world *w, totals *t)
{
    for (uint32_t p = 0; p < w->entities.pages; p++) {
        if (!w->entities.page[p]) continue;
        for (uint32_t k = 0; k < (1u << TIDE_ENTITY_PAGE_SHIFT); k++) {
            const uint32_t index = (p << TIDE_ENTITY_PAGE_SHIFT) + k;
            if (index >= w->entities.next_unused) break;
            const tide_entity e = tide_entity_in_slot(&w->entities, index);
            if (tide_entity_is_null(e) || !tide_has_Mover(w, e) || tide_has_tide_ghost(w, e)) continue;
            const Mover m = tide_get_Mover(w, e);
            if (m.id < 0 || m.id >= 128) continue;
            if (!t->owned[m.id]++) t->distinct++;
            t->wallets += m.wallet;
            if (tide_has_Bank(w, e)) {
                const Bank b = tide_get_Bank(w, e);
                t->coins += b.coins;
                t->received += b.received;
            }
            if (tide_has_Transfer(w, e)) t->left += tide_get_Transfer(w, e).left;
        }
    }
}

// What shards own. Something on its way between shards is owned by none for
// a tick or two, so what they own together never adds up to more than there
// is, and to all of it whenever nothing is on its way.
static totals count(void)
{
    totals t = {0};
    for (uint32_t i = 0; i < tide_shards_count(shards); i++) {
        const tide_world *w = tide_shards_world(shards, i);
        if (w) gather(w, &t);
    }
    return t;
}

static void step(void)
{
    now += 1.0 / 120.0;
    tide_loopback_set_time(net, now);
    tide_shards_update(shards, now);
    for (uint32_t i = 0; i < tide_shards_count(shards); i++) watch(i);
    for (uint32_t i = 0; i < CLIENTS; i++) {
        if (clients[i]) tide_client_update(clients[i], now);
    }
}

static void run(const double seconds, const bool check)
{
    const double until = now + seconds;
    while (now < until) {
        step();
        if (!check) continue;
        const totals t = count();
        for (int id = 0; id < 128; id++) TIDE_CHECK(t.owned[id] <= 1); // Never two shards' at once
        TIDE_CHECK(t.coins + t.wallets <= 2200);                    // Nothing made from nowhere
    }
}

static void begin(const uint32_t most)
{
    now = 0;
    memset(clients, 0, sizeof clients);
    net = tide_loopback_create(7);
    const tide_shards_desc d = desc_of(most);
    shards = tide_shards_create(&d, now);
}

static void finish(void)
{
    for (uint32_t i = 0; i < CLIENTS; i++) {
        tide_client_destroy(clients[i]);
        clients[i] = NULL;
    }
    tide_shards_destroy(shards);
    shards = NULL;
    tide_loopback_destroy(net);
}

// Once everything that waited got done: at a moment nothing is on its way
// between shards, every mover and both banks are owned once, and every coin is
// where it was paid to
static void check_done(void)
{
    totals t = count();
    for (int tries = 0; t.distinct != 22 && tries < 600; tries++) {
        step();
        t = count();
    }
    TIDE_CHECK(t.distinct == 22);
    for (int id = 0; id < 20; id++) TIDE_CHECK(t.owned[id] == 1);
    TIDE_CHECK(t.owned[100] == 1 && t.owned[101] == 1);
    TIDE_CHECK(t.wallets == 0);    // Every coin paid...
    TIDE_CHECK(t.received == 200); // ...arrived once
    TIDE_CHECK(t.coins == 2200);   // ...and the banks' transfers made and lost nothing
    TIDE_CHECK(t.left == 0);       // Both banks' transfers, done
}

static void check_clients(const uint32_t count_of)
{
    for (uint32_t i = 0; i < count_of && i < CLIENTS; i++) {
        const tide_client_status status = tide_client_status_of(clients[i]);
        TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(status.resyncs == 0); // Every client's world was its shard's, tick for tick
    }
}

TIDE_TEST(shards_move_entities_and_wait_on_borders_without_losing_anything)
{
    const uint64_t dropped = tide_shard_dropped();
    begin(0);
    TIDE_REQUIRE(shards != NULL);
    TIDE_CHECK(tide_shards_count(shards) == 3);
    run(1.0, false); // Main's entities go from the root to the regions
    run(20.0, true);
    check_done();
    // Both regions held ghosts of each other's movers near the border
    tide_shard_status a, b;
    tide_shards_status(shards, 1, &a);
    tide_shards_status(shards, 2, &b);
    TIDE_CHECK(a.owned > 0 && b.owned > 0);
    TIDE_CHECK(a.ghosts > 0 && b.ghosts > 0);
    TIDE_CHECK(a.waiting == 0 && b.waiting == 0);
    TIDE_CHECK(tide_shard_dropped() == dropped); // Nothing across a border was dropped
    check_clients(3);
    finish();
}

TIDE_TEST(shards_restore_a_checkpoint_and_go_on)
{
    begin(0);
    TIDE_REQUIRE(shards != NULL);
    run(4.0, true);
    uint32_t size = 0;
    uint8_t *saved = tide_shards_save(shards, &size);
    TIDE_REQUIRE(saved != NULL && size > 0);
    const totals before = count();
    for (uint32_t i = 0; i < CLIENTS; i++) {
        tide_client_destroy(clients[i]);
        clients[i] = NULL;
    }
    tide_shards_destroy(shards);
    const tide_shards_desc d = desc_of(0);
    shards = tide_shards_load(&d, saved, size, now);
    free(saved);
    TIDE_REQUIRE(shards != NULL);
    const totals after = count();
    TIDE_CHECK(after.distinct == before.distinct);
    TIDE_CHECK(after.coins == before.coins && after.wallets == before.wallets);
    run(20.0, true);
    check_done();
    check_clients(3);
    finish();
}

TIDE_TEST(shards_split_a_crowded_region)
{
    begin(8);
    TIDE_REQUIRE(shards != NULL);
    run(1.0, false);
    run(20.0, true);
    TIDE_CHECK(tide_shards_count(shards) > 3); // A region with more than 8 split
    check_done();
    check_clients(tide_shards_count(shards));
    finish();
}
