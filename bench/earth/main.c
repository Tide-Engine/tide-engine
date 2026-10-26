// Earth's bots, and its run (see bench/bench.h): the home page's planet
// (examples/earth), built here to take more players than the page does.
//
// Each bot lands in a town, a different one for each of the first `towns`
// bots and round again, walks about it in a new direction every three
// seconds (standing still now and then, waving, hopping), and `drivers` of
// each hundred turn into a car as they land and drive about instead. Every
// `hop` seconds each goes to another town, as a player clicking the globe
// does, which with --split takes it to another shard.

#include <stdio.h>
#include <string.h>

#include "bench.h"
#include "game.h"

#define TOWNS 10u

static bench_flag flags[] = {
    {"towns", "towns the bots spread over: 1 puts everyone in one", 10, 10, 0, 1},
    {"drivers", "bots of each hundred that drive", 30, 30, 0, 0},
    {"hop", "seconds between a bot's trips to another town; 0 never", 40, 4, 0, 0},
};

// Where each town's middle is, from examples/earth/tools/towns.mjs's latitudes
// and longitudes, as numbers: a bot's input mustn't come from the platform's
// sines and cosines, which differ, or no two platforms would play one match.
static const tide_float3 towns[TOWNS] = {
    {519.55f, 2229.43f, -1938.99f},  {-1377.64f, 623.74f, -2590.96f}, {1243.56f, -1408.41f, -2338.79f},
    {1829.01f, 2364.03f, 257.05f},   {2542.59f, 104.70f, 1588.79f},   {1581.85f, -1846.98f, 1756.82f},
    {-82.50f, 1846.98f, 2362.59f},   {-2217.22f, 313.59f, 1996.40f},  {-1372.58f, -2007.39f, 1756.82f},
    {-1178.63f, 2740.64f, 315.81f},
};

static uint32_t mix(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static void make_world(const bench_flag *f, void *world, const float dt)
{
    (void)f;
    tide_world_init(world, dt);
}

static void sample(const bench_flag *f, const uint32_t bot, const uint32_t tick, void *input)
{
    Controls *c = input;
    const uint32_t spread = f[0].value < TOWNS ? f[0].value : TOWNS;
    const bool driver = mix(bot * 7919u + 1u) % 100u < f[1].value;
    const uint32_t hop = f[2].value * 60u;
    // Trips: the first as the bot joins, then one every `hop`, each bot at its own time
    const uint32_t trip = hop ? (tick + mix(bot) % hop) / hop : 0u;
    const uint32_t since = hop ? (tick + mix(bot) % hop) % hop : tick;
    c->moves = (int32_t)trip + 1;
    c->drop = towns[(bot + trip * 3u) % spread];
    // A driver turns into a car a second and a half after landing (landing makes a walker of it again)
    c->car = driver && since == 90u;

    const uint32_t leg = (tick + bot * 53u) / 180u;
    const uint32_t h = mix(bot * 2654435761u ^ leg);
    if (h % 5u == 0u) return; // Standing still
    // Somewhere in a cube, which the game flattens along the ground and makes at most 1 long
    c->move = (tide_float3){(float)(h & 255u) / 127.5f - 1.0f, (float)((h >> 8) & 255u) / 127.5f - 1.0f,
                            (float)((h >> 16) & 15u) / 7.5f - 1.0f};
    c->wave = !driver && (h >> 20) % 7u == 0u;
    c->jump = (h >> 24) % 11u == 0u && tick % 60u == 0u;
}

#define EACH(w, e)                                                                                                    \
    for (uint32_t index_ = 0; index_ < (w)->entities.next_unused; index_++)                                           \
        for (tide_entity e = tide_entity_in_slot(&(w)->entities, index_); !tide_entity_is_null(e); e = (tide_entity){0})

// Everything a world has, its copies of other shards' included
static uint32_t held(const void *world)
{
    const tide_world *w = world;
    uint32_t count = 0;
    EACH(w, e) count++;
    return count;
}

static void describe(const void *world)
{
    const tide_world *w = world;
    uint32_t players = 0, placed = 0, driving = 0, people = 0, cars = 0, ghosts = 0;
    EACH(w, e)
    {
        if (tide_has_tide_ghost(w, e)) {
            ghosts++;
            continue;
        }
        cars += tide_has_Car(w, e);
        if (!tide_has_Walker(w, e)) continue;
        if (tide_has_Person(w, e)) {
            people++;
            continue;
        }
        const Walker walker = tide_get_Walker(w, e);
        players++;
        placed += walker.placed;
        driving += walker.driving;
    }
    printf("  players' walkers %u (%u on the ground, %u driving), townsfolk %u, cars %u; copies of others' %u\n", players, placed,
           driving, people, cars, ghosts);
}

static uint32_t tick_of(const void *world)
{
    return (uint32_t)((const tide_world *)world)->Time.tick;
}

// What differs between two worlds, entity by entity: which components each
// has, and the fields of those both have that differ
static void differ(const void *server_world, const void *client_world, const uint32_t most)
{
    const tide_world *s = server_world, *c = client_world;
    const uint32_t slots = s->entities.next_unused > c->entities.next_unused ? s->entities.next_unused : c->entities.next_unused;
    uint32_t said = 0;
    for (uint32_t i = 0; i < slots && said < most; i++) {
        const tide_entity a = i < s->entities.next_unused ? tide_entity_in_slot(&s->entities, i) : (tide_entity){0};
        const tide_entity b = i < c->entities.next_unused ? tide_entity_in_slot(&c->entities, i) : (tide_entity){0};
        if (tide_entity_is_null(a) && tide_entity_is_null(b)) continue;
        if (tide_entity_is_null(a) || tide_entity_is_null(b) || a.generation != b.generation) {
            printf("    entity %u: the server's %s, the machine's %s\n", i, tide_entity_is_null(a) ? "has none" : "is there",
                   tide_entity_is_null(b) ? "has none" : "is there");
            said++;
            continue;
        }
#define PRESENCE(T)                                                                                                    \
    if (tide_has_##T(s, a) != tide_has_##T(c, b)) {                                                                   \
        printf("    entity %u: %s " #T " on the server, %s on the machine\n", i, tide_has_##T(s, a) ? "a" : "no",      \
               tide_has_##T(c, b) ? "one" : "none");                                                                  \
        said++;                                                                                                       \
    }
        PRESENCE(Walker) PRESENCE(Person) PRESENCE(Car) PRESENCE(Beacon) PRESENCE(Owner) PRESENCE(Seen)
#undef PRESENCE
#define FIELD(T, f, fmt, ...)                                                                                          \
    if (memcmp(&x.f, &y.f, sizeof x.f) != 0) {                                                                        \
        printf("    entity %u " #T "." #f ": server " fmt ", machine " fmt "\n", i, __VA_ARGS__);                     \
        said++;                                                                                                       \
    }
#define V3(v) (double)(v).x, (double)(v).y, (double)(v).z
        if (tide_has_Walker(s, a) && tide_has_Walker(c, b)) {
            const Walker x = tide_get_Walker(s, a), y = tide_get_Walker(c, b);
            FIELD(Walker, position, "(%.3f %.3f %.3f)", V3(x.position), V3(y.position))
            FIELD(Walker, forward, "(%.3f %.3f %.3f)", V3(x.forward), V3(y.forward))
            FIELD(Walker, velocity, "(%.3f %.3f %.3f)", V3(x.velocity), V3(y.velocity))
            FIELD(Walker, height, "%.3f", (double)x.height, (double)y.height)
            FIELD(Walker, speed, "%.3f", (double)x.speed, (double)y.speed)
            FIELD(Walker, waving, "%d", x.waving, y.waving)
            FIELD(Walker, moves, "%d", x.moves, y.moves)
            FIELD(Walker, fives, "%d", x.fives, y.fives)
            FIELD(Walker, fived, "%d", x.fived, y.fived)
            FIELD(Walker, placed, "%d", x.placed, y.placed)
            FIELD(Walker, driving, "%d", x.driving, y.driving)
        }
        if (tide_has_Person(s, a) && tide_has_Person(c, b)) {
            const Person x = tide_get_Person(s, a), y = tide_get_Person(c, b);
            FIELD(Person, goal, "(%.3f %.3f %.3f)", V3(x.goal), V3(y.goal))
            FIELD(Person, rest, "%.3f", (double)x.rest, (double)y.rest)
            FIELD(Person, stuck, "%.3f", (double)x.stuck, (double)y.stuck)
        }
        if (tide_has_Car(s, a) && tide_has_Car(c, b)) {
            const Car x = tide_get_Car(s, a), y = tide_get_Car(c, b);
            FIELD(Car, position, "(%.3f %.3f %.3f)", V3(x.position), V3(y.position))
            FIELD(Car, along, "%.3f", (double)x.along, (double)y.along)
            FIELD(Car, way, "%.3f", (double)x.way, (double)y.way)
            FIELD(Car, speed, "%.3f", (double)x.speed, (double)y.speed)
        }
        if (tide_has_Beacon(s, a) && tide_has_Beacon(c, b)) {
            const Beacon x = tide_get_Beacon(s, a), y = tide_get_Beacon(c, b);
            FIELD(Beacon, at, "(%.3f %.3f %.3f)", V3(x.at), V3(y.at))
        }
#undef V3
#undef FIELD
    }
    // The singletons the match keeps
    if (s->Roster.online != c->Roster.online || s->Roster.next != c->Roster.next) {
        printf("    Roster: server online %d next %d, machine online %d next %d\n", s->Roster.online, s->Roster.next,
               c->Roster.online, c->Roster.next);
        said++;
    }
    if (!said) printf("    (nothing this compares: something else)\n");
}

int main(int argc, char **argv)
{
    const bench_desc desc = {
        .name = "earth",
        .game = &tide_game_api,
        .full = {.players = 64, .join_every = 0.1, .seconds = 30.0, .latency = 0.05, .jitter = 0.01, .loss = 0.01},
        .quick = {.players = 6, .join_every = 0.25, .seconds = 3.0, .latency = 0.05, .jitter = 0.01, .loss = 0.01},
        .flags = flags,
        .flag_count = sizeof flags / sizeof flags[0],
        .make_world = make_world,
        .sample = sample,
        .describe = describe,
        .held = held,
        .tick_of = tick_of,
        .differ = differ,
        // As the page splits it (page.html's --shards 4 --shard-width 1600)
        .world = {{-3200.0, -3200.0, -3200.0}, {3200.0, 3200.0, 3200.0}},
    };
    return bench_main(&desc, argc, argv);
}
