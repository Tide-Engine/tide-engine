#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

// Random numbers in a match on a network, in made-up time: a server and
// players on a loopback network with latency. The match's seed is the
// server's to pick, from the entropy its host gives it; every player has it
// with the world. Clients predict, roll wrong when another player's input
// isn't what they guessed, and run those ticks again: they end with the
// server's generators, tick for tick.

static tide_loopback *net;
static tide_server *server;
static tide_client *clients[3];
static int who[3] = {0, 1, 2};
static double now;

// What the server's world was after each tick, to hold the clients' against
#define TICKS 2048u
static uint64_t server_hash[TICKS];
static int64_t server_table[TICKS];
static uint32_t server_ticks;
static uint32_t checked; // Times a client's verified world was held against the server's

// What a system would give a host that asks for random bits: other ones each time
static uint64_t entropy_calls;
static uint64_t entropy(void)
{
    entropy_calls++;
    return 0x9E3779B97F4A7C15ull * entropy_calls + 0x1234567ull;
}

// Each player rolls in bursts of its own, and bets what the tick says: the
// others can't guess when a burst starts or ends.
static void sample(void *user, const uint32_t tick, void *input)
{
    const int player = *(const int *)user;
    Controls *c = input;
    *c = (Controls){0};
    c->roll = ((tick + (uint32_t)player * 5u) / (7u + (uint32_t)player * 4u)) % 2u == 0u;
    c->bet = (int32_t)((tick / 3u) % 50u);
}

static void start(const tide_net_conditions conditions, uint64_t (*bits)(void))
{
    now = 0.0;
    net = tide_loopback_create(4242);
    tide_loopback_set_conditions(net, conditions);
    const tide_server_desc desc = {.game = &tide_game_api, .tick_rate = 60, .transports = {tide_loopback_endpoint(net, 1)},
                                   .entropy = bits};
    server = tide_server_create(&desc, now);
    memset(clients, 0, sizeof clients);
    memset(server_hash, 0, sizeof server_hash);
    server_ticks = 0;
    checked = 0;
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

// Each client's verified world is the server's at that tick: its hash, and the
// table's generator, which every roll of every player stepped.
static void check_verified(void)
{
    for (int i = 0; i < 3; i++) {
        if (!clients[i]) continue;
        const tide_client_status s = tide_client_status_of(clients[i]);
        const tide_world *verified = tide_client_verified_world(clients[i]);
        if (s.state != TIDE_SESSION_CONNECTED || !verified || s.verified_tick >= TICKS || !server_hash[s.verified_tick]) continue;
        TIDE_CHECK(tide_world_hash(verified) == server_hash[s.verified_tick]);
        TIDE_CHECK(verified->Table.random.state == server_table[s.verified_tick]);
        checked++;
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
        const uint32_t tick = tide_server_tick(server);
        if (tick < TICKS && tick != server_ticks) { // Updates this short run a tick at most
            const tide_world *w = tide_server_world(server);
            server_hash[tick] = tide_world_hash(w);
            server_table[tick] = w->Table.random.state;
            server_ticks = tick;
        }
        check_verified();
    }
}

static void finish(void)
{
    for (int i = 0; i < 3; i++) tide_client_destroy(clients[i]);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}

TIDE_TEST(random_net_clients_end_with_the_servers_generators)
{
    start((tide_net_conditions){.latency = 0.05, .jitter = 0.01}, entropy);
    const tide_world *match = tide_server_world(server);
    const int32_t seed = match->Time.seed;
    TIDE_CHECK(seed != 0 && match->Table.seed == seed);
    join(0);
    join(1);
    run(6.0);
    join(2); // Into a match that's been rolling for a while
    run(12.0);

    uint32_t again = 0;
    for (int i = 0; i < 3; i++) {
        const tide_client_status s = tide_client_status_of(clients[i]);
        TIDE_CHECK(s.state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(s.resyncs == 0); // Every tick it ran again came out as the server's
        TIDE_CHECK(s.verified_tick + 30u > tide_server_tick(server));
        again += s.again_ticks;
        // The seed came with the world: nobody sent it, and everybody has it
        const tide_world *theirs = tide_client_verified_world(clients[i]);
        TIDE_REQUIRE(theirs != NULL);
        TIDE_CHECK(theirs->Time.seed == seed && theirs->Table.seed == seed);
    }
    printf("    the clients ran %u predicted ticks again; their verified worlds were held against the server's %u times\n",
           (unsigned)again, (unsigned)checked);
    TIDE_CHECK(again > 100u); // They did guess wrong, and rolled wrong for it
    TIDE_CHECK(checked > 1000u);

    // The match rolled: the fighters, the table, and the crates they spawned
    match = tide_server_world(server);
    int fighters = 0;
    int crates = 0;
    for (uint32_t i = 0; i < match->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&match->entities, i);
        if (tide_has_Fighter(match, e)) {
            const Fighter f = tide_get_Fighter(match, e);
            TIDE_CHECK(f.rolls > 50 && f.total >= f.rolls && f.total <= f.rolls * 6);
            fighters++;
        }
        if (tide_has_Crate(match, e)) {
            TIDE_CHECK(tide_get_Crate(match, e).random.state != 0);
            crates++;
        }
    }
    TIDE_CHECK(fighters == 3 && match->Table.crates > 10 && crates > 0 && match->Table.pot > 0);
    finish();
}

// A player joining a match with a seed starts the match itself, with the seed
// the server tells it, to have a world to build the server's from: the 20,000
// stones' generators come out the same, so the server only sends what changed
// since the start. With another seed, it would lack every one of them.
TIDE_TEST(random_net_a_joiner_starts_the_match_with_its_seed)
{
    start((tide_net_conditions){.latency = 0.02}, entropy);
    TIDE_CHECK(tide_game_api.pure_start && tide_game_api.seeded);
    const uint32_t whole = tide_world_pack(tide_server_world(server), NULL, 0);
    join(0);
    run(4.0);
    const tide_client_status s = tide_client_status_of(clients[0]);
    TIDE_REQUIRE(s.state == TIDE_SESSION_CONNECTED);
    printf("    the world is %u bytes; the player was sent %u\n", (unsigned)whole, (unsigned)s.world_bytes);
    TIDE_CHECK(whole > 300000u);
    TIDE_CHECK(s.world_bytes < 16000u); // The stones' generators alone are 160,000 bytes no seed but theirs gives
    TIDE_CHECK(s.resyncs == 0);
    const tide_world *theirs = tide_client_verified_world(clients[0]);
    TIDE_REQUIRE(theirs != NULL);
    TIDE_CHECK(theirs->Time.seed == ((const tide_world *)tide_server_world(server))->Time.seed);
    finish();
}

// A server whose host gives it entropy picks another seed for each match; one
// that's given none starts every match with seed 0, so a run repeats.
TIDE_TEST(random_net_entropy_makes_matches_differ)
{
    int32_t seeds[3];
    uint64_t hashes[3];
    for (int i = 0; i < 3; i++) {
        start((tide_net_conditions){0}, entropy);
        const tide_world *w = tide_server_world(server);
        seeds[i] = w->Time.seed;
        hashes[i] = tide_world_hash(w);
        TIDE_CHECK(seeds[i] > 0);
        finish();
    }
    TIDE_CHECK(seeds[0] != seeds[1] && seeds[1] != seeds[2] && seeds[0] != seeds[2]);
    TIDE_CHECK(hashes[0] != hashes[1] && hashes[1] != hashes[2] && hashes[0] != hashes[2]);

    uint64_t plain[2];
    for (int i = 0; i < 2; i++) {
        start((tide_net_conditions){.latency = 0.03}, NULL);
        TIDE_CHECK(((const tide_world *)tide_server_world(server))->Time.seed == 0);
        join(0);
        join(1);
        run(3.0);
        plain[i] = tide_world_hash(tide_server_world(server));
        finish();
    }
    TIDE_CHECK(plain[0] == plain[1]);
}

// A session, which a host program uses: each match it starts gets a seed from
// the entropy its desc has, and its own player has it.
TIDE_TEST(random_net_a_session_seeds_each_match)
{
    tide_session *s = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .tick_rate = 60, .sample = sample,
                                                               .user = &who[0], .entropy = entropy});
    double t = 0.0;
    int32_t seeds[2];
    for (int match = 0; match < 2; match++) {
        tide_session_start(s, NULL, t);
        for (int frame = 0; frame < 60; frame++) {
            t += 1.0 / 60.0;
            tide_session_update(s, t);
        }
        const tide_world *w = tide_session_world(s);
        const tide_world *theirs = tide_session_server_world(s);
        TIDE_REQUIRE(w != NULL && theirs != NULL);
        seeds[match] = theirs->Time.seed;
        TIDE_CHECK(seeds[match] > 0 && w->Time.seed == seeds[match]);
    }
    TIDE_CHECK(seeds[0] != seeds[1]);
    tide_session_destroy(s);

    // With no entropy to ask, a session's matches start with seed 0
    tide_session *plain = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .tick_rate = 60, .sample = sample,
                                                                   .user = &who[0]});
    tide_session_start(plain, NULL, 0.0);
    tide_session_update(plain, 0.1);
    const tide_world *w = tide_session_server_world(plain);
    TIDE_REQUIRE(w != NULL);
    TIDE_CHECK(w->Time.seed == 0);
    tide_session_destroy(plain);
}
