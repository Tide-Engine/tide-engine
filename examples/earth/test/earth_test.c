#include <math.h>

#include "game.h"
#include "tide/shard_client.h"
#include "tide/shards.h"
#include "tide_test.h"

#define EARTH 6371000.0

// The first live entity of a world that a test asks for
typedef bool (*wanted)(const tide_world *w, tide_entity e);

static tide_entity first(const tide_world *w, const wanted want)
{
    for (uint32_t index = 0; index < w->entities.next_unused; index++) {
        if (!w->entities.page[index >> TIDE_ENTITY_PAGE_SHIFT]) {
            index |= (1u << TIDE_ENTITY_PAGE_SHIFT) - 1u;
            continue;
        }
        const tide_entity e = tide_entity_in_slot(&w->entities, index);
        if (!tide_entity_is_null(e) && want(w, e)) return e;
    }
    return (tide_entity){0};
}

static bool is_lantern(const tide_world *w, const tide_entity e)
{
    return tide_has_Lantern(w, e) && tide_get_Lantern(w, e).color < 0;
}

static bool is_bot(const tide_world *w, const tide_entity e)
{
    return tide_has_Bot(w, e);
}

static double length(const tide_double3 v)
{
    return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

// One world, as a match that isn't split: the world it starts with, and a bot
// put right beside a lantern, which takes the bot's color and tells the bot
TIDE_TEST(earth_a_plane_lights_a_lantern_it_passes)
{
    static tide_world world;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    int lanterns = 0, bots = 0;
    for (uint32_t index = 0; index < world.entities.next_unused; index++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, index);
        if (tide_entity_is_null(e)) continue;
        if (tide_has_Lantern(&world, e)) {
            lanterns++;
            const double up = length(tide_get_Lantern(&world, e).position) - EARTH;
            TIDE_CHECK(up >= 999.0 && up <= 6001.0);
        }
        bots += tide_has_Bot(&world, e);
    }
    TIDE_CHECK(lanterns == 40 * 30 + 800);
    TIDE_CHECK(bots == 12);

    const tide_entity lantern = first(&world, is_lantern);
    const tide_entity bot = first(&world, is_bot);
    TIDE_REQUIRE(!tide_entity_is_null(lantern) && !tide_entity_is_null(bot));
    Plane plane = tide_get_Plane(&world, bot);
    const Lantern was = tide_get_Lantern(&world, lantern);
    const double scale = (EARTH + 3000.0) / length(was.position);
    plane.position = (tide_double3){was.position.x * scale, was.position.y * scale, was.position.z * scale};
    plane.altitude = 3000.0f;
    plane.lit = 0;
    tide_set_Plane(&world, bot, plane);
    tide_world_tick(&world);
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_Lantern(&world, lantern).color == plane.color);
    TIDE_CHECK(tide_get_Plane(&world, bot).lit >= 1);
    // Flying on, it stays low and above the ground
    for (int i = 0; i < 600; i++) tide_world_tick(&world);
    const Plane later = tide_get_Plane(&world, bot);
    TIDE_CHECK(later.altitude >= 2000.0f && later.altitude <= 2500000.0f);
    TIDE_CHECK(fabs(length(later.position) - EARTH - later.altitude) < 1.0);
    tide_world_free(&world);
}

// The world in four regions along x, and a player who drops their plane just
// west of the border at x = 0: it flies east across it, their machine
// following it to the next shard, and once they've gone for good the root's
// PlayerLeft destroys it, on whichever shard has it
static tide_loopback *net;
static tide_shards *shards;
static tide_shard_client *player;
static double now;

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
    *(Controls *)input = (Controls){.drop = {-0.01f, 0.0f, 1.0f}, .drops = 1};
}

static bool is_flying_player(const tide_world *w, const tide_entity e)
{
    return tide_has_Plane(w, e) && !tide_has_Bot(w, e) && !tide_has_tide_ghost(w, e);
}

// The player's plane as the shards that own it have it
static int owners(Plane *plane)
{
    int count = 0;
    for (uint32_t i = 0; i < tide_shards_count(shards); i++) {
        const tide_world *w = tide_shards_world(shards, i);
        if (!w) continue;
        const tide_entity e = first(w, is_flying_player);
        if (tide_entity_is_null(e)) continue;
        *plane = tide_get_Plane(w, e);
        count++;
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
        if (player) tide_shard_client_update(player, now);
        Plane plane;
        TIDE_CHECK(owners(&plane) <= 1);
    }
}

TIDE_TEST(earth_a_player_flies_across_a_border_and_leaves)
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
    player = tide_shard_client_create(&pd, now);
    TIDE_REQUIRE(player != NULL);

    // Dropped 64 km west of the border, it's the west region's, heading east
    run(2.0);
    Plane plane;
    TIDE_CHECK(owners(&plane) == 1);
    TIDE_CHECK(plane.flying && plane.drops == 1);
    TIDE_CHECK(plane.position.x < 0.0);
    const uint32_t west = tide_shard_client_home_shard(player);
    // At 40 km up it flies some 24 km a second: across the border in seconds
    run(6.0);
    TIDE_CHECK(owners(&plane) == 1);
    TIDE_CHECK(plane.position.x > 60000.0);
    TIDE_CHECK(tide_shard_client_home_shard(player) != west);
    const tide_client_status home = tide_client_status_of(tide_shard_client_home(player));
    TIDE_CHECK(home.state == TIDE_SESSION_CONNECTED && home.resyncs == 0);
    // Every lantern and bot is some shard's, once
    int lanterns = 0, bots = 0;
    for (uint32_t i = 0; i < tide_shards_count(shards); i++) {
        const tide_world *w = tide_shards_world(shards, i);
        if (!w) continue;
        for (uint32_t index = 0; index < w->entities.next_unused; index++) {
            if (!w->entities.page[index >> TIDE_ENTITY_PAGE_SHIFT]) {
                index |= (1u << TIDE_ENTITY_PAGE_SHIFT) - 1u;
                continue;
            }
            const tide_entity e = tide_entity_in_slot(&w->entities, index);
            if (tide_entity_is_null(e) || tide_has_tide_ghost(w, e)) continue;
            lanterns += tide_has_Lantern(w, e);
            bots += tide_has_Bot(w, e);
        }
    }
    TIDE_CHECK(lanterns == 40 * 30 + 800);
    TIDE_CHECK(bots == 12);

    // Gone, and away past the 20 seconds a player has to come back: the plane goes with them
    tide_shard_client_destroy(player);
    player = NULL;
    run(25.0);
    TIDE_CHECK(owners(&plane) == 0);

    tide_shards_destroy(shards);
    tide_loopback_destroy(net);
}
