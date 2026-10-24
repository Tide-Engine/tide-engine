#include <math.h>

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

// The planet in four regions along x, 1,600 m wide: a player looks at the
// globe a moment, then goes to town 3 (in the region furthest along +x), then
// to town 7 (furthest along -x), their machine following their walker from
// one shard to another; once they've gone for good, the root's PlayerLeft
// destroys it where it is. A shard that has the walker before its player's
// input guesses an input of zeros, which mustn't read as a request to go
// anywhere: until the walker took only newer requests, it went back and forth
// between two shards, and neither owned it.
static tide_loopback *net;
static tide_shards *shards;
static tide_shard_client *client;
static double now;
static tide_float3 drop;
static int moves;

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
    *(Controls *)input = (Controls){.drop = drop, .moves = moves};
}

// The townsfolk the player's machine has of its home shard's world: only
// those near enough for it to see them (Seen, Visibility)
static int people_seen(void)
{
    const tide_world *w = tide_client_view(tide_shard_client_home(client)).current;
    int count = 0;
    if (!w) return -1;
    EACH(w, e) count += tide_has_Person(w, e);
    return count;
}

// The player's walker as the shards that own it have it
static int owners(Walker *found)
{
    int count = 0;
    for (uint32_t i = 0; i < tide_shards_count(shards); i++) {
        const tide_world *w = tide_shards_world(shards, i);
        if (!w) continue;
        EACH(w, e)
        {
            if (!tide_has_Walker(w, e) || tide_has_Person(w, e) || tide_has_tide_ghost(w, e)) continue;
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
        if (client) tide_shard_client_update(client, now);
        Walker w;
        TIDE_CHECK(owners(&w) <= 1);
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
    const tide_shard_client_desc pd = {
        .client = {.game = &tide_game_api, .transport = tide_loopback_endpoint(net, 100u), .server = tide_loopback_address(10u),
                   .sample = sample, .lead = 2},
        .reach = reach,
    };
    client = tide_shard_client_create(&pd, now);
    TIDE_REQUIRE(client != NULL);

    moves = 0;
    run(2.0);
    drop = times(up_at(52, 8), PLANET);
    moves = 1;
    run(3.0);
    Walker w;
    TIDE_CHECK(owners(&w) == 1);
    TIDE_CHECK(w.placed && w.position.x > 1600.0f);
    const uint32_t first = tide_shard_client_home_shard(client);
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

    drop = times(up_at(6, 138), PLANET);
    moves = 2;
    run(3.0);
    TIDE_CHECK(owners(&w) == 1);
    TIDE_CHECK(w.placed && w.position.x < -1600.0f);
    TIDE_CHECK(tide_shard_client_home_shard(client) != first);
    const tide_client_status home = tide_client_status_of(tide_shard_client_home(client));
    TIDE_CHECK(home.state == TIDE_SESSION_CONNECTED && home.resyncs == 0);
    TIDE_CHECK(people_seen() == 6); // The new town's, on the new shard

    tide_shard_client_destroy(client);
    client = NULL;
    run(25.0);
    TIDE_CHECK(owners(&w) == 0);

    tide_shards_destroy(shards);
    tide_loopback_destroy(net);
}
