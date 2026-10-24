#include <math.h>

#include "game.h"
#include "tide/shard_client.h"
#include "tide/shards.h"
#include "tide_test.h"

// Every live entity of a world, in index order
#define EACH(w, e)                                                                                                    \
    for (uint32_t index_ = 0; index_ < (w)->entities.next_unused; index_++)                                           \
        for (tide_entity e = tide_entity_in_slot(&(w)->entities, index_); !tide_entity_is_null(e); e = (tide_entity){0})

static tide_world world;

static tide_player_id player(const int n)
{
    return tide_player_from_index(n);
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

static float length2(const tide_float2 v)
{
    return sqrtf(v.x * v.x + v.y * v.y);
}

// One world, as a match that isn't split: its townsfolk and cars; a player
// who picks a town lands in its square, walks, can't go through the fountain,
// stops a car by standing in front of it, and high-fives another who waves
TIDE_TEST(earth_walkers_walk_bump_and_high_five_and_cars_stop_for_them)
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
    TIDE_CHECK(people == 12 * 8);
    TIDE_CHECK(cars == 12 * 6);
    TIDE_CHECK(world.Roster.online == 2);
    const tide_entity me = walker_of(&world, player(0)), you = walker_of(&world, player(1));
    TIDE_REQUIRE(!tide_entity_is_null(me) && !tide_entity_is_null(you));
    TIDE_CHECK(tide_get_Walker(&world, me).city == -1);

    // Both to town 9: each in its square
    tide_world_set_input(&world, player(0), (Controls){.city = 9, .moves = 1});
    tide_world_set_input(&world, player(1), (Controls){.city = 9, .moves = 1});
    ticks(2);
    Walker a = tide_get_Walker(&world, me);
    TIDE_CHECK(a.city == 9);
    TIDE_CHECK(length2(a.at) < 34.0f);
    const double up = sqrt(a.position.x * a.position.x + a.position.y * a.position.y + a.position.z * a.position.z);
    TIDE_CHECK(fabs(up - 6371000.0) < 0.5);

    // Walking east for a second: about 5 m, less the start
    const tide_float2 was = a.at;
    tide_world_set_input(&world, player(0), (Controls){.city = 9, .moves = 1, .move = {1, 0}});
    ticks(60);
    a = tide_get_Walker(&world, me);
    TIDE_CHECK(a.at.x - was.x > 3.5f && a.at.x - was.x < 5.5f);

    // Put in the fountain, it's pushed out of it; past the city's edge, back inside it
    a.at = (tide_float2){1, 0};
    tide_set_Walker(&world, me, a);
    tide_world_set_input(&world, player(0), (Controls){.city = 9, .moves = 1});
    ticks(1);
    a = tide_get_Walker(&world, me);
    TIDE_CHECK(length2(a.at) >= 5.0f + 0.45f - 0.01f);
    a.at = (tide_float2){0, 900};
    tide_set_Walker(&world, me, a);
    ticks(1);
    TIDE_CHECK(length2(tide_get_Walker(&world, me).at) <= 400.0f);

    // In front of a car on the ring road, standing still: the car stops short of it
    tide_entity car = {0};
    EACH(&world, e)
    {
        if (tide_has_Car(&world, e) && tide_get_Car(&world, e).city == 9 && tide_entity_is_null(car)) car = e;
    }
    TIDE_REQUIRE(!tide_entity_is_null(car));
    const Car c = tide_get_Car(&world, car);
    const float ahead = c.turn + c.way * 7.0f / c.lane;
    a.at = (tide_float2){sinf(ahead) * c.lane, cosf(ahead) * c.lane};
    a.velocity = (tide_float2){0, 0};
    tide_set_Walker(&world, me, a);
    ticks(120);
    const Car stopped = tide_get_Car(&world, car);
    TIDE_CHECK(stopped.speed < 0.5f);
    TIDE_CHECK(fabsf(stopped.turn - c.turn) * c.lane < 7.0f);
    // Put where the car is, the walker is pushed out of it
    a = tide_get_Walker(&world, me);
    a.at = (tide_float2){sinf(stopped.turn) * stopped.lane, cosf(stopped.turn) * stopped.lane};
    tide_set_Walker(&world, me, a);
    ticks(1);
    a = tide_get_Walker(&world, me);
    const tide_float2 middle = {sinf(stopped.turn) * stopped.lane, cosf(stopped.turn) * stopped.lane};
    TIDE_CHECK(length2((tide_float2){a.at.x - middle.x, a.at.y - middle.y}) > 1.0f);
    // Out of the way, it goes on
    a = tide_get_Walker(&world, me);
    a.at = (tide_float2){0, 20};
    tide_set_Walker(&world, me, a);
    ticks(120);
    TIDE_CHECK(tide_get_Car(&world, car).speed > 5.0f);

    // Side by side, both waving: a high five each
    Walker b2 = tide_get_Walker(&world, you);
    a = tide_get_Walker(&world, me);
    a.at = (tide_float2){12, 12};
    b2.at = (tide_float2){13.2f, 12};
    tide_set_Walker(&world, me, a);
    tide_set_Walker(&world, you, b2);
    tide_world_set_input(&world, player(0), (Controls){.city = 9, .moves = 1, .wave = true});
    tide_world_set_input(&world, player(1), (Controls){.city = 9, .moves = 1, .wave = true});
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

// The world in four regions along x, 3,300 km wide: a player looks at the
// globe a moment, then visits town 3 (in the region furthest along +x), then
// town 10 (furthest along -x), their machine following their walker from one
// shard to another; once they've gone for good, the root's PlayerLeft
// destroys it where it is. A shard that has the walker before its player's
// input guesses an input of zeros, which mustn't read as a request to go to
// town 0: until the walker took only newer requests, it went back and forth
// between two shards, and neither owned it.
static tide_loopback *net;
static tide_shards *shards;
static tide_shard_client *client;
static double now;
static int city = -1, moves = 0;

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
    *(Controls *)input = (Controls){.city = city, .moves = moves};
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
    const double half = 2.0 * 3300000.0;
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

    city = -1;
    moves = 0;
    run(2.0);
    city = 3;
    moves = 1;
    run(3.0);
    Walker w;
    TIDE_CHECK(owners(&w) == 1);
    TIDE_CHECK(w.city == 3 && w.position.x > 3300000.0);
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
    TIDE_CHECK(people == 12 * 8);
    TIDE_CHECK(cars == 12 * 6);

    city = 10;
    moves = 2;
    run(3.0);
    TIDE_CHECK(owners(&w) == 1);
    TIDE_CHECK(w.city == 10 && w.position.x < -3300000.0);
    TIDE_CHECK(tide_shard_client_home_shard(client) != first);
    const tide_client_status home = tide_client_status_of(tide_shard_client_home(client));
    TIDE_CHECK(home.state == TIDE_SESSION_CONNECTED && home.resyncs == 0);

    tide_shard_client_destroy(client);
    client = NULL;
    run(25.0);
    TIDE_CHECK(owners(&w) == 0);

    tide_shards_destroy(shards);
    tide_loopback_destroy(net);
}
