#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/shard_client.h"
#include "tide/shards.h"
#include "tide_test.h"

#define PLANET 3000.0f

// Every live entity of a world, in index order
#define EACH(w, e)                                                                                                    \
    for (uint32_t index_ = 0; index_ < (w)->entities.next_unused; index_++)                                           \
        for (tide_entity e = tide_entity_in_slot(&(w)->entities, index_); !tide_entity_is_null(e); e = (tide_entity){0})

static tide_world world;

static tide_player_id player(const int n)
{
    return tide_player_from_index(n);
}

// Straight up at a latitude and longitude, as city.tide has a town's middle
static tide_float3 up_at(const float lat, const float lon)
{
    const float a = lat * 3.14159265f / 180.0f, o = lon * 3.14159265f / 180.0f;
    return (tide_float3){cosf(a) * cosf(o), sinf(a), cosf(a) * sinf(o)};
}

static float length3(const tide_float3 v)
{
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static tide_float3 minus(const tide_float3 a, const tide_float3 b)
{
    return (tide_float3){a.x - b.x, a.y - b.y, a.z - b.z};
}

static tide_float3 times(const tide_float3 a, const float k)
{
    return (tide_float3){a.x * k, a.y * k, a.z * k};
}

// A player's walker: the one their PlayerJoined spawned, which no Person has
static tide_entity walker_of(const tide_world *w, const tide_player_id who)
{
    EACH(w, e)
    {
        if (tide_has_Walker(w, e) && !tide_has_Person(w, e) && !tide_has_tide_ghost(w, e) && tide_has_Owner(w, e)
            && tide_get_Owner(w, e).player.id == who.id)
            return e;
    }
    return (tide_entity){0};
}

static void ticks(const int n)
{
    for (int i = 0; i < n; i++) tide_world_tick(&world);
}

// One world, as a match that isn't split: the townsfolk and the traffic; a
// player who picks a town lands by its fountain, walks, can't go through the
// fountain, turns into a car and drives, stops a car by standing in front of
// it, and high-fives another who waves
TIDE_TEST(earth_walkers_walk_drive_and_high_five_and_traffic_stops_for_them)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_player_joined(&world, player(0));
    tide_world_player_joined(&world, player(1));
    ticks(1);
    int people = 0, cars = 0;
    EACH(&world, e)
    {
        people += tide_has_Person(&world, e);
        cars += tide_has_Car(&world, e);
    }
    TIDE_CHECK(people == 10 * 6);
    TIDE_CHECK(cars == 11 * 2);
    TIDE_CHECK(world.Roster.online == 2);
    const tide_entity me = walker_of(&world, player(0)), you = walker_of(&world, player(1));
    TIDE_REQUIRE(!tide_entity_is_null(me) && !tide_entity_is_null(you));
    TIDE_CHECK(!tide_get_Walker(&world, me).placed);

    // Both to town 3: by its fountain, facing it
    const tide_float3 town = up_at(52, 8);
    const tide_float3 square = times(town, PLANET);
    tide_world_set_input(&world, player(0), (Controls){.drop = square, .moves = 1});
    tide_world_set_input(&world, player(1), (Controls){.drop = square, .moves = 1});
    ticks(2);
    Walker a = tide_get_Walker(&world, me);
    TIDE_CHECK(a.placed);
    TIDE_CHECK(length3(minus(a.position, square)) < 18.0f);
    TIDE_CHECK(length3(minus(a.position, square)) >= 4.0f + 0.45f - 0.05f); // Out of the fountain
    TIDE_CHECK(fabsf(length3(a.position) - PLANET) < 0.01f);

    // Walking ahead for a second: about 5 m, less the start, and still on the ground
    const tide_float3 was = a.position;
    tide_world_set_input(&world, player(0), (Controls){.drop = square, .moves = 1, .move = times(a.forward, -1)});
    ticks(60);
    a = tide_get_Walker(&world, me);
    const float walked = length3(minus(a.position, was));
    TIDE_CHECK(walked > 3.5f && walked < 5.5f);
    TIDE_CHECK(fabsf(length3(a.position) - PLANET) < 0.01f);

    // Into a car, and off: faster than walking
    tide_world_set_input(&world, player(0), (Controls){.drop = square, .moves = 1, .car = true});
    ticks(1);
    TIDE_CHECK(tide_get_Walker(&world, me).driving);
    // Out in the country, between two towns no road joins, heading east
    a = tide_get_Walker(&world, me);
    const tide_float3 between = up_at(60, 90);
    a.position = times(between, PLANET);
    a.forward = (tide_float3){-between.z / sqrtf(between.x * between.x + between.z * between.z), 0,
                              between.x / sqrtf(between.x * between.x + between.z * between.z)};
    tide_set_Walker(&world, me, a);
    tide_world_set_input(&world, player(0), (Controls){.drop = square, .moves = 1, .move = a.forward});
    ticks(180);
    a = tide_get_Walker(&world, me);
    TIDE_CHECK(a.speed > 15.0f);
    TIDE_CHECK(fabsf(length3(a.position) - PLANET) < 0.01f);
    // ...and out again
    tide_world_set_input(&world, player(0), (Controls){.drop = square, .moves = 1});
    ticks(1);
    tide_world_set_input(&world, player(0), (Controls){.drop = square, .moves = 1, .car = true});
    ticks(1);
    tide_world_set_input(&world, player(0), (Controls){.drop = square, .moves = 1});
    ticks(1);
    TIDE_CHECK(!tide_get_Walker(&world, me).driving);

    // In front of a townsperson's car on its road, standing still: it stops short
    tide_entity car = {0};
    EACH(&world, e)
    {
        if (tide_has_Car(&world, e) && tide_entity_is_null(car)) car = e;
    }
    TIDE_REQUIRE(!tide_entity_is_null(car));
    ticks(30); // Up to speed
    const Car c = tide_get_Car(&world, car);
    a = tide_get_Walker(&world, me);
    a.position = (tide_float3){c.position.x + c.forward.x * 7, c.position.y + c.forward.y * 7, c.position.z + c.forward.z * 7};
    a.velocity = (tide_float3){0, 0, 0};
    tide_set_Walker(&world, me, a);
    ticks(120);
    const Car stopped = tide_get_Car(&world, car);
    TIDE_CHECK(stopped.speed < 0.5f);
    TIDE_CHECK(length3(minus(tide_get_Walker(&world, me).position, stopped.position)) > 1.5f);
    // Out of the way, it goes on
    a = tide_get_Walker(&world, me);
    a.position = times(square, 1.0f);
    tide_set_Walker(&world, me, a);
    ticks(120);
    TIDE_CHECK(tide_get_Car(&world, car).speed > 5.0f);

    // Side by side by the fountain, both waving: a high five each
    a = tide_get_Walker(&world, me);
    Walker b = tide_get_Walker(&world, you);
    b.position = minus(a.position, times(a.forward, -1.2f));
    tide_set_Walker(&world, you, b);
    tide_world_set_input(&world, player(0), (Controls){.drop = square, .moves = 1, .wave = true});
    tide_world_set_input(&world, player(1), (Controls){.drop = square, .moves = 1, .wave = true});
    ticks(3);
    TIDE_CHECK(tide_get_Walker(&world, me).fives == 1);
    TIDE_CHECK(tide_get_Walker(&world, you).fives == 1);
    ticks(30); // Not again while it lasts
    TIDE_CHECK(tide_get_Walker(&world, me).fives == 1);

    tide_world_player_left(&world, player(1));
    ticks(2);
    TIDE_CHECK(world.Roster.online == 1);
    TIDE_CHECK(tide_entity_is_null(walker_of(&world, player(1))));
    tide_world_free(&world);
}

// What a player's machine is sent of the world, loaded, hashes as the server
// says it does, all players' at once or one at a time: for a player near
// another (who has their Walker) and one far off (who has their Beacon alone)
TIDE_TEST(earth_a_machine_hashes_its_world_as_the_server_does)
{
    tide_world_init(&world, 1.0f / 60.0f);
    for (int p = 0; p < 3; p++) tide_world_player_joined(&world, player(p));
    ticks(1);
    const tide_float3 town = times(up_at(52, 8), PLANET), far = times(up_at(6, 138), PLANET);
    tide_world_set_input(&world, player(0), (Controls){.drop = town, .moves = 1});
    tide_world_set_input(&world, player(1), (Controls){.drop = town, .moves = 1});
    tide_world_set_input(&world, player(2), (Controls){.drop = far, .moves = 1});
    ticks(61); // Landed, and looked at who's near
    const tide_player_id all[3] = {player(0), player(1), player(2)};
    uint64_t together[3];
    tide_game_api.client_hashes(&world, all, 3, together);
    for (int p = 0; p < 3; p++) {
        TIDE_CHECK(together[p] == tide_game_api.client_hash(&world, all[p]));
        uint32_t size = 0;
        uint8_t *bytes = tide_game_api.client_state(&world, all[p], &size);
        tide_world *machine = calloc(1, sizeof *machine);
        TIDE_REQUIRE(bytes && machine && tide_game_api.load_client_state(machine, bytes, size));
        TIDE_CHECK(tide_game_api.client_hash(machine, all[p]) == together[p]);
        // Players 0 and 1 have each other's Walker; player 2 has neither's
        TIDE_CHECK(tide_has_Walker(machine, walker_of(&world, player(p == 0 ? 1 : 0))) == (p != 2));
        tide_world_free(machine);
        free(machine);
        free(bytes);
    }
    TIDE_CHECK(together[0] != together[1] && together[1] != together[2]);
    // Whose inputs each may have, all at once, is what each one's filter says
    enum { SLOTS = TIDE_GAME_PLAYERS + 1 };
    static uint8_t allowed[3 * SLOTS];
    static Controls filtered[SLOTS];
    Controls none;
    tide_game_api.client_input_masks(&world, all, 3, allowed, &none);
    for (int p = 0; p < 3; p++) {
        for (uint32_t slot = 0; slot < SLOTS; slot++) tide_game_api.world_input(&world, slot, &filtered[slot]);
        tide_game_api.filter_client_inputs(&world, all[p], filtered);
        for (uint32_t slot = 0; slot < SLOTS; slot++) {
            Controls raw;
            tide_game_api.world_input(&world, slot, &raw);
            const Controls *expect = allowed[p * SLOTS + slot] ? &raw : &none;
            TIDE_CHECK(memcmp(&filtered[slot], expect, sizeof raw) == 0);
        }
    }
    TIDE_CHECK(allowed[0 * SLOTS + 1] && !allowed[2 * SLOTS + 0]); // Players 0 and 1 have each other's; 2 has neither's
    tide_world_free(&world);
}

// The planet in four regions along x, 1,600 m wide: a player looks at the
// globe a moment, then goes to town 3 (in the region furthest along +x), then
// to town 7 (furthest along -x), their machine following their walker from
// one shard to another; once they've gone for good, the root's PlayerLeft
// destroys it where it is. A shard that has the walker before its player's
// input guesses an input of zeros, which mustn't read as a request to go
// anywhere: until the walker took only newer requests, it went back and forth
// between two shards, and neither owned it. A second player, in town 4 on the
// same shard but 700 m off, is only a Beacon on the first one's machine until
// they come to town 3 too.
typedef struct visitor {
    tide_shard_client *client;
    uint32_t endpoints; // Its clients' loopback endpoints: this and the shard's number
    tide_float3 drop;
    int moves;
} visitor;

static tide_loopback *net;
static tide_shards *shards;
static visitor first = {.endpoints = 100}, second = {.endpoints = 200};
static double now;

static void transports(void *user, const uint32_t shard, tide_transport out[2])
{
    (void)user;
    out[0] = tide_loopback_endpoint(net, 10u + shard);
}

static bool reach(void *user, const uint32_t shard, const uint64_t match, tide_transport *transport, tide_address *server)
{
    const visitor *v = user;
    (void)match;
    *transport = tide_loopback_endpoint(net, v->endpoints + shard);
    *server = tide_loopback_address(10u + shard);
    return true;
}

static void sample(void *user, const uint32_t tick, void *input)
{
    const visitor *v = user;
    (void)tick;
    *(Controls *)input = (Controls){.drop = v->drop, .moves = v->moves};
}

static void join(visitor *v)
{
    const tide_shard_client_desc pd = {
        .client = {.game = &tide_game_api, .transport = tide_loopback_endpoint(net, v->endpoints), .server = tide_loopback_address(10u),
                   .sample = sample, .user = v, .lead = 2},
        .reach = reach,
        .user = v,
    };
    v->client = tide_shard_client_create(&pd, now);
}

static const tide_world *home_world(const visitor *v)
{
    return tide_client_view(tide_shard_client_home(v->client)).current;
}

// The townsfolk the player's machine has of its home shard's world: only
// those near enough for it to see them (Seen, Visibility)
static int people_seen(void)
{
    const tide_world *w = home_world(&first);
    int count = 0;
    if (!w) return -1;
    EACH(w, e) count += tide_has_Person(w, e);
    return count;
}

// The players the first player's machine has, as Beacons, and of them those it has the Walker of
static void players_seen(int *beacons, int *walkers)
{
    const tide_world *w = home_world(&first);
    *beacons = *walkers = 0;
    if (!w) return;
    EACH(w, e)
    {
        if (!tide_has_Beacon(w, e) || tide_has_tide_ghost(w, e)) continue;
        *beacons += 1;
        *walkers += tide_has_Walker(w, e);
    }
}

// A player's walker, by its tag (the order they came in), as the shards that own it have it
static int owners(const int tag, Walker *found)
{
    int count = 0;
    for (uint32_t i = 0; i < tide_shards_count(shards); i++) {
        const tide_world *w = tide_shards_world(shards, i);
        if (!w) continue;
        EACH(w, e)
        {
            if (!tide_has_Walker(w, e) || tide_has_Person(w, e) || tide_has_tide_ghost(w, e)) continue;
            if (tide_get_Walker(w, e).tag != tag) continue;
            *found = tide_get_Walker(w, e);
            count++;
        }
    }
    return count;
}

static void run(const double seconds)
{
    const double until = now + seconds;
    while (now < until) {
        now += 1.0 / 60.0;
        tide_loopback_set_time(net, now);
        tide_shards_update(shards, now);
        if (first.client) tide_shard_client_update(first.client, now);
        if (second.client) tide_shard_client_update(second.client, now);
        Walker w;
        TIDE_CHECK(owners(0, &w) <= 1);
        TIDE_CHECK(owners(1, &w) <= 1);
    }
}

TIDE_TEST(earth_a_player_visits_towns_on_two_shards_and_leaves)
{
    now = 0;
    net = tide_loopback_create(3);
    const double half = 2.0 * 1600.0;
    const tide_shards_desc d = {.game = &tide_game_api, .tick_rate = 60, .world = {{-half, -half, -half}, {half, half, half}},
                                .split = {4, 1, 1}, .transports = transports};
    shards = tide_shards_create(&d, now);
    TIDE_REQUIRE(shards != NULL);
    first.moves = second.moves = 0;
    join(&first);
    TIDE_REQUIRE(first.client != NULL);
    run(2.0);
    join(&second);
    TIDE_REQUIRE(second.client != NULL);
    run(2.0);
    first.drop = times(up_at(52, 8), PLANET);
    first.moves = 1;
    second.drop = times(up_at(2, 32), PLANET);
    second.moves = 1;
    run(3.0);
    Walker w, w2;
    TIDE_CHECK(owners(0, &w) == 1);
    TIDE_CHECK(w.placed && w.position.x > 1600.0f);
    TIDE_CHECK(owners(1, &w2) == 1);
    TIDE_CHECK(w2.placed && w2.position.x > 1600.0f && length3(minus(w.position, w2.position)) > 600.0f);
    const uint32_t home = tide_shard_client_home_shard(first.client);
    TIDE_CHECK(tide_shard_client_home_shard(second.client) == home);
    // Every townsperson and car is some shard's, once
    int people = 0, cars = 0;
    for (uint32_t i = 0; i < tide_shards_count(shards); i++) {
        const tide_world *s = tide_shards_world(shards, i);
        if (!s) continue;
        EACH(s, e)
        {
            if (tide_has_tide_ghost(s, e)) continue;
            people += tide_has_Person(s, e);
            cars += tide_has_Car(s, e);
        }
    }
    TIDE_CHECK(people == 10 * 6);
    TIDE_CHECK(cars == 11 * 2);
    // ...but the player's machine has only the townsfolk of the town it's in: the rest are past its horizon
    TIDE_CHECK(people_seen() == 6);
    // ...and of the other player, past its horizon, where they are and not their Walker
    int beacons, walkers;
    players_seen(&beacons, &walkers);
    TIDE_CHECK(beacons == 2 && walkers == 1);

    // The other player comes to town 3: now they're near, their Walker too
    second.drop = first.drop;
    second.moves = 2;
    run(3.0);
    players_seen(&beacons, &walkers);
    TIDE_CHECK(beacons == 2 && walkers == 2);

    first.drop = times(up_at(6, 138), PLANET);
    first.moves = 2;
    run(3.0);
    TIDE_CHECK(owners(0, &w) == 1);
    TIDE_CHECK(w.placed && w.position.x < -1600.0f);
    TIDE_CHECK(tide_shard_client_home_shard(first.client) != home);
    const tide_client_status status = tide_client_status_of(tide_shard_client_home(first.client));
    TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED && status.resyncs == 0);
    TIDE_CHECK(people_seen() == 6); // The new town's, on the new shard

    tide_shard_client_destroy(first.client);
    first.client = NULL;
    tide_shard_client_destroy(second.client);
    second.client = NULL;
    run(25.0);
    TIDE_CHECK(owners(0, &w) == 0);
    TIDE_CHECK(owners(1, &w) == 0);

    tide_shards_destroy(shards);
    tide_loopback_destroy(net);
}
