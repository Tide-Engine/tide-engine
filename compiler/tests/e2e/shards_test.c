#include "game.h"
#include "tide/shards.h"
#include "tide_test.h"

#include <math.h>
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
static int most_dying; // The most couriers seen waiting to go at once

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
    int64_t purses; // Couriers' not paid yet
    int dying;      // Couriers whose destroy waits for their bank
    int tickers, counted; // Movers with a task counting ticks, and what they counted
    int bag_owned[20]; // By the id of the mover that spawned it: how many shards own its bag
    int bags, carried; // Bags owned, and those owned where their mover is
} totals;

static void gather(const tide_world *w, totals *t)
{
    for (uint32_t p = 0; p < w->entities.pages; p++) {
        if (!w->entities.page[p]) continue;
        for (uint32_t k = 0; k < (1u << TIDE_ENTITY_PAGE_SHIFT); k++) {
            const uint32_t index = (p << TIDE_ENTITY_PAGE_SHIFT) + k;
            if (index >= w->entities.next_unused) break;
            const tide_entity e = tide_entity_in_slot(&w->entities, index);
            if (!tide_entity_is_null(e) && tide_has_Bag(w, e) && !tide_has_tide_ghost(w, e)) {
                const Bag bag = tide_get_Bag(w, e);
                if (bag.id >= 0 && bag.id < 20) t->bag_owned[bag.id]++;
                t->bags++;
                if (tide_entity_alive(&w->entities, bag.holder) && !tide_has_tide_ghost(w, bag.holder)) t->carried++;
            }
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
            if (tide_has_Courier(w, e)) t->purses += tide_get_Courier(w, e).purse;
            if (tide_has_tide_dying(w, e)) t->dying++;
            if (tide_has_Ticker(w, e)) {
                t->tickers++;
                t->counted += tide_get_Ticker(w, e).counted;
            }
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
        for (int id = 0; id < 20; id++) TIDE_CHECK(t.bag_owned[id] <= 1);
        TIDE_CHECK(t.coins + t.wallets + t.purses <= 2340);         // Nothing made from nowhere
        if (t.dying > most_dying) most_dying = t.dying;
    }
}

static void begin_with(const tide_shards_desc *d)
{
    now = 0;
    most_dying = 0;
    memset(clients, 0, sizeof clients);
    net = tide_loopback_create(7);
    shards = tide_shards_create(d, now);
}

static void begin(const uint32_t most)
{
    const tide_shards_desc d = desc_of(most);
    begin_with(&d);
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
    for (int tries = 0; (t.distinct != 22 || t.carried != 5) && tries < 600; tries++) {
        step();
        t = count();
    }
    TIDE_CHECK(t.distinct == 22);
    TIDE_CHECK(t.bags == 5 && t.carried == 5); // Every bag, with no position, where its mover is
    for (int id = 0; id < 20; id++) TIDE_CHECK(t.owned[id] == 1);
    for (int id = 50; id < 56; id++) TIDE_CHECK(t.owned[id] == 0); // Every courier went...
    TIDE_CHECK(t.purses == 0 && t.dying == 0);                     // ...having paid
    TIDE_CHECK(t.owned[100] == 1 && t.owned[101] == 1);
    TIDE_CHECK(t.wallets == 0);    // Every coin paid...
    TIDE_CHECK(t.received == 200); // ...arrived once
    TIDE_CHECK(t.coins == 2340);   // ...and the banks' transfers made and lost nothing, the tasks' 20 coins among them
    TIDE_CHECK(t.left == 0);       // Both banks' transfers, done
    TIDE_CHECK(t.tickers == 4 && t.counted == 4 * 600); // Every tick of every task, wherever its mover was
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
    TIDE_CHECK(most_dying > 0); // A courier's destroy waited for the bank across the border
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

TIDE_TEST(shards_merge_quiet_regions)
{
    const uint64_t dropped = tide_shard_dropped();
    begin(1000); // Splits past 1,000; two regions owning a quarter of that together merge
    TIDE_REQUIRE(shards != NULL);
    run(1.0, false);
    run(20.0, true);
    // The two regions are one, the other's shard ended, and nothing was lost on the way
    uint32_t regions = 0;
    tide_shard_box box;
    for (uint32_t i = 0; i < tide_shards_count(shards); i++) regions += tide_shards_region(shards, i, &box) ? 1u : 0u;
    TIDE_CHECK(regions == 1);
    TIDE_CHECK(tide_shards_server(shards, 2) == NULL);
    TIDE_CHECK(tide_shards_region(shards, 1, &box) && box.lo[0] == -INFINITY && box.hi[0] == INFINITY);
    check_done();
    TIDE_CHECK(tide_shard_dropped() == dropped);
    check_clients(2);
    finish();
}

// How busy each shard is, as a host would measure it
static float busy_of[64];

static float busy(void *user, const uint32_t shard)
{
    (void)user;
    return shard < 64u ? busy_of[shard] : 0.0f;
}

TIDE_TEST(shards_split_a_busy_region_and_merge_quiet_ones)
{
    const uint64_t dropped = tide_shard_dropped();
    memset(busy_of, 0, sizeof busy_of);
    tide_shards_desc d = desc_of(0); // No limit by entities: by load alone
    d.busiest = 0.5f;
    d.busy = busy;
    busy_of[1] = busy_of[2] = 0.3f; // Under the most, too busy to merge
    begin_with(&d);
    TIDE_REQUIRE(shards != NULL);
    run(1.0, false);
    // The left region's shard is busy for a while: it splits, and only it
    busy_of[1] = 0.9f;
    run(4.0, true);
    const uint32_t split = tide_shards_count(shards);
    TIDE_CHECK(split > 3);
    tide_shard_box left, right;
    TIDE_CHECK(tide_shards_region(shards, 1, &left) && left.hi[0] < 0.0);
    TIDE_CHECK(tide_shards_region(shards, 2, &right) && right.lo[0] == 0.0 && right.hi[0] == INFINITY);
    for (uint32_t i = 3; i < split; i++) {
        tide_shard_box box;
        TIDE_CHECK(tide_shards_region(shards, i, &box) && box.hi[0] <= 0.0); // Cut from the left
    }
    // Then everything's quiet: the regions merge back, one at a time, into one
    memset(busy_of, 0, sizeof busy_of);
    run(20.0, true);
    uint32_t regions = 0;
    tide_shard_box box;
    for (uint32_t i = 0; i < tide_shards_count(shards); i++) regions += tide_shards_region(shards, i, &box) ? 1u : 0u;
    TIDE_CHECK(regions == 1);
    TIDE_CHECK(tide_shards_server(shards, 2) == NULL);
    check_done(); // A merge's trees too big for a frame went by download: a game with no commands takes those too
    TIDE_CHECK(tide_shard_dropped() == dropped);
    check_clients(2);
    finish();
}

// A journal in memory: each shard's records since the last checkpoint, which
// a checkpoint clears
typedef struct kept {
    uint8_t *checkpoint;
    uint32_t checkpoint_size;
    uint32_t checkpoints;
    uint8_t **records[16];
    uint32_t *sizes[16];
    uint32_t counts[16], rooms[16];
} kept;

static kept journal;

static void forget_records(void)
{
    for (uint32_t i = 0; i < 16u; i++) {
        for (uint32_t k = 0; k < journal.counts[i]; k++) free(journal.records[i][k]);
        journal.counts[i] = 0;
    }
}

static void keep_record(void *user, const uint32_t shard, const uint32_t tick, const uint8_t *record, const uint32_t size)
{
    (void)user;
    (void)tick;
    if (shard >= 16u) return;
    if (journal.counts[shard] == journal.rooms[shard]) {
        journal.rooms[shard] = journal.rooms[shard] ? journal.rooms[shard] * 2u : 256u;
        journal.records[shard] = realloc(journal.records[shard], journal.rooms[shard] * sizeof *journal.records[shard]);
        journal.sizes[shard] = realloc(journal.sizes[shard], journal.rooms[shard] * sizeof *journal.sizes[shard]);
    }
    uint8_t *copy = malloc(size);
    memcpy(copy, record, size);
    journal.records[shard][journal.counts[shard]] = copy;
    journal.sizes[shard][journal.counts[shard]++] = size;
}

static void keep_checkpoint(void *user, const uint8_t *data, const uint32_t size)
{
    (void)user;
    free(journal.checkpoint);
    journal.checkpoint = malloc(size);
    memcpy(journal.checkpoint, data, size);
    journal.checkpoint_size = size;
    journal.checkpoints++;
    forget_records(); // The ones before it aren't needed
}

static bool give_record(void *user, const uint32_t shard, const uint32_t index, const uint8_t **record, uint32_t *size)
{
    (void)user;
    if (shard >= 16u || index >= journal.counts[shard]) return false;
    *record = journal.records[shard][index];
    *size = journal.sizes[shard][index];
    return true;
}

static void crash_and_recover(const uint32_t most)
{
    memset(&journal, 0, sizeof journal);
    now = 0;
    most_dying = 0;
    memset(clients, 0, sizeof clients);
    net = tide_loopback_create(7);
    tide_shards_desc d = desc_of(most);
    d.journal = keep_record;
    d.checkpoint = keep_checkpoint;
    d.checkpoint_seconds = 3.0;
    shards = tide_shards_create(&d, now);
    TIDE_REQUIRE(shards != NULL);
    run(1.0, false);
    run(7.5, true); // Checkpoints at about 3 and 6 seconds, then records since
    TIDE_REQUIRE(journal.checkpoints >= 2);
    // As each shard is when it stops for good, no checkpoint since
    const uint32_t count = tide_shards_count(shards);
    if (most) TIDE_CHECK(count > 3u); // It split before it stopped
    uint32_t ticks[8];
    uint64_t hashes[8];
    for (uint32_t i = 0; i < count && i < 8u; i++) {
        if (!tide_shards_world(shards, i)) continue;
        ticks[i] = (uint32_t)((const tide_world *)tide_shards_world(shards, i))->Time.tick;
        hashes[i] = tide_game_api.hash_world(tide_shards_world(shards, i));
    }
    for (uint32_t i = 0; i < CLIENTS; i++) {
        tide_client_destroy(clients[i]);
        clients[i] = NULL;
    }
    tide_shards_destroy(shards);
    // Started again from the last checkpoint and the records since: every shard as it was
    shards = tide_shards_recover(&d, journal.checkpoint, journal.checkpoint_size, give_record, NULL, now);
    TIDE_REQUIRE(shards != NULL);
    TIDE_CHECK(tide_shards_count(shards) == count);
    for (uint32_t i = 0; i < count && i < 8u; i++) {
        if (!tide_shards_world(shards, i)) continue;
        TIDE_CHECK((uint32_t)((const tide_world *)tide_shards_world(shards, i))->Time.tick == ticks[i]);
        TIDE_CHECK(tide_game_api.hash_world(tide_shards_world(shards, i)) == hashes[i]);
    }
    // ...and the world goes on, nothing lost or made twice
    run(20.0, true);
    check_done();
    check_clients(most ? 1u : 3u);
    finish();
    forget_records();
    for (uint32_t i = 0; i < 16u; i++) {
        free(journal.records[i]);
        free(journal.sizes[i]);
    }
    free(journal.checkpoint);
}

TIDE_TEST(shards_recover_from_a_crash_where_they_were)
{
    crash_and_recover(0);
}

// The same with regions splitting along the way: the world's shape is in the
// checkpoint that follows each change
TIDE_TEST(shards_recover_from_a_crash_after_splits)
{
    crash_and_recover(8);
    TIDE_CHECK(journal.checkpoints >= 3);
}

// One shard's machine failing: that shard starts again from the last
// checkpoint and its records, the others going on, and nothing is lost
TIDE_TEST(shards_restart_one_shard_where_it_was)
{
    memset(&journal, 0, sizeof journal);
    now = 0;
    most_dying = 0;
    memset(clients, 0, sizeof clients);
    net = tide_loopback_create(7);
    tide_shards_desc d = desc_of(0);
    d.journal = keep_record;
    d.checkpoint = keep_checkpoint;
    d.checkpoint_seconds = 3.0;
    shards = tide_shards_create(&d, now);
    TIDE_REQUIRE(shards != NULL);
    run(1.0, false);
    run(7.5, true);
    TIDE_REQUIRE(journal.checkpoints >= 2);
    const tide_world *before = tide_shards_world(shards, 1);
    const uint32_t tick = (uint32_t)before->Time.tick;
    const uint64_t hash = tide_game_api.hash_world(before);
    TIDE_REQUIRE(tide_shards_restart(shards, 1, journal.checkpoint, journal.checkpoint_size, give_record, NULL, now));
    const tide_world *after = tide_shards_world(shards, 1);
    TIDE_CHECK((uint32_t)after->Time.tick == tick);              // Where it was...
    TIDE_CHECK(tide_game_api.hash_world(after) == hash);           // ...exactly
    run(20.0, true);
    check_done();
    finish();
    forget_records();
    for (uint32_t i = 0; i < 16u; i++) {
        free(journal.records[i]);
        free(journal.sizes[i]);
    }
    free(journal.checkpoint);
}
