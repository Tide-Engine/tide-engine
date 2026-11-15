// Voxels' bots, and its run (see bench/bench.h).
//
// Each bot flies off in a direction of its own, turning every few seconds, so
// the box of chunks it has keeps moving: chunks are made ahead of it and left
// behind it. It digs and builds some of the time, and every `hop` seconds
// goes to the next dimension.

#include <stdio.h>
#include <string.h>

#include <stdlib.h>

#include "bench.h"
#include "tide/io.h"
#include "voxels.h"

// The server's store, kept in memory: each chunk a player changed, saved as
// its last player left, by its path. A table that grows, its slots found by
// the path's hash.
typedef struct kept {
    char *path;
    uint8_t *data;
    uint32_t size;
} kept;

static kept *store;
static uint32_t store_room, store_count;
static uint64_t store_bytes, store_writes, store_reads, store_found;

static uint32_t path_hash(const char *path)
{
    uint32_t h = 2166136261u;
    for (; *path; path++) h = (h ^ (uint8_t)*path) * 16777619u;
    return h;
}

static kept *store_slot(const char *path)
{
    for (uint32_t i = path_hash(path) & (store_room - 1u);; i = (i + 1u) & (store_room - 1u)) {
        if (!store[i].path || strcmp(store[i].path, path) == 0) return &store[i];
    }
}

static bool store_exists(void *user, const char *path, const bool directory)
{
    (void)user;
    return directory || (store_room && store_slot(path)->path != NULL); // Folders are only names here
}

static tide_io_error store_create(void *user, const char *path, const bool directory)
{
    (void)user;
    (void)path;
    (void)directory;
    return TIDE_IO_NONE;
}

static tide_io_error store_read(void *user, const char *path, uint8_t **data, uint32_t *size)
{
    (void)user;
    store_reads++;
    const kept *k = store_room ? store_slot(path) : NULL;
    if (!k || !k->path) return TIDE_IO_NOT_FOUND;
    store_found++;
    *data = malloc(k->size ? k->size : 1u);
    if (!*data) return TIDE_IO_FAILED;
    memcpy(*data, k->data, k->size);
    *size = k->size;
    return TIDE_IO_NONE;
}

static tide_io_error store_write(void *user, const char *path, const uint8_t *data, const uint32_t size, const bool append)
{
    (void)user;
    (void)append;
    if ((store_count + 1u) * 2u > store_room) {
        const uint32_t room = store_room ? store_room * 2u : 1024u, had = store_room;
        kept *was = store;
        store = calloc(room, sizeof *store);
        if (!store) return TIDE_IO_FAILED;
        store_room = room;
        for (uint32_t i = 0; i < had; i++) {
            if (was[i].path) *store_slot(was[i].path) = was[i];
        }
        free(was);
    }
    kept *k = store_slot(path);
    if (!k->path) {
        k->path = malloc(strlen(path) + 1u);
        if (!k->path) return TIDE_IO_FAILED;
        strcpy(k->path, path);
        store_count++;
    } else store_bytes -= k->size;
    free(k->data);
    k->data = malloc(size ? size : 1u);
    if (!k->data) return TIDE_IO_FAILED;
    memcpy(k->data, data, size);
    k->size = size;
    store_bytes += size;
    store_writes++;
    return TIDE_IO_NONE;
}

static const tide_io store_io = {.exists = store_exists, .create = store_create, .read = store_read, .write = store_write};

static bench_flag flags[] = {
    {"near", "chunks a player has around them, sideways", 6, 2, 0, 1},
    {"tall", "chunks a player has above and below them", 2, 1, 0, 0},
    {"dimensions", "dimensions the world has", 2, 2, 0, 1},
    {"hop", "seconds between a bot's changes of dimension; 0 never", 20, 2, 0, 0},
    {"still", "bots of each hundred that stay where they are", 0, 0, 0, 0},
    {"keep", "ticks between saves of a chunk players changed, while it's held", 600, 90, 0, 1},
    {"linger", "ticks a chunk nobody has stays before it goes", 120, 30, 0, 0},
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
    tide_world *w = world;
    tide_world_init(w, dt);
    w->Worlds.near = (int32_t)f[0].value;
    w->Worlds.tall = (int32_t)f[1].value;
    w->Worlds.count = (int32_t)f[2].value;
    w->Worlds.keep = (int32_t)f[5].value;
    w->Worlds.linger = (int32_t)f[6].value;
}

static void sample(const bench_flag *f, const uint32_t bot, const uint32_t tick, void *input)
{
    Controls *c = input;
    c->fly = true;
    const uint32_t hop = f[3].value * 60u;
    c->hops = hop ? (int32_t)((tick + mix(bot) % hop) / hop) : 0;
    if (mix(bot * 7919u + 1u) % 100u < f[4].value) return;
    // A direction along the ground for five seconds, from a table: no platform's sines
    static const float ways[8][2] = {{1, 0}, {0.7f, 0.7f}, {0, 1}, {-0.7f, 0.7f}, {-1, 0}, {-0.7f, -0.7f}, {0, -1}, {0.7f, -0.7f}};
    const uint32_t leg = (tick + bot * 53u) / 300u;
    const uint32_t h = mix(bot * 2654435761u ^ leg);
    c->move = (tide_float3){ways[h & 7u][0], (float)((h >> 3) % 3u) * 0.1f - 0.1f, ways[h & 7u][1]};
    c->dig = (tick / 30u + bot) % 4u == 0u;
    c->build = (tick / 45u + bot) % 6u == 0u;
}

#define EACH(w, e)                                                                                                    \
    for (uint32_t index_ = 0; index_ < (w)->entities.next_unused; index_++)                                           \
        for (tide_entity e = tide_entity_in_slot(&(w)->entities, index_); !tide_entity_is_null(e); e = (tide_entity){0})

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
    uint32_t chunks = 0, edited = 0, avatars = 0;
    uint64_t edits = 0;
    EACH(w, e)
    {
        avatars += tide_has_Avatar(w, e);
        if (!tide_has_Chunk(w, e)) continue;
        chunks++;
        const int32_t changed = tide_get_Chunk(w, e).edits;
        edited += changed > 0;
        edits += (uint64_t)changed;
    }
    printf("  chunks %u (%u changed by players, %llu changes), avatars %u\n", chunks, edited, (unsigned long long)edits, avatars);
    printf("  the store: %u chunks saved in %llu bytes (%llu saves); %llu loads, %llu of them found\n", store_count,
           (unsigned long long)store_bytes, (unsigned long long)store_writes, (unsigned long long)store_reads,
           (unsigned long long)store_found);
}

static uint32_t tick_of(const void *world)
{
    return (uint32_t)((const tide_world *)world)->Time.tick;
}

int main(int argc, char **argv)
{
    const bench_desc desc = {
        .name = "voxels",
        .game = &tide_game_api,
        .full = {.players = 8, .join_every = 0.5, .seconds = 30.0, .latency = 0.05, .jitter = 0.01, .loss = 0.01},
        .quick = {.players = 3, .join_every = 0.25, .seconds = 8.0, .latency = 0.05, .jitter = 0.01, .loss = 0.01},
        .flags = flags,
        .flag_count = sizeof flags / sizeof flags[0],
        .make_world = make_world,
        .sample = sample,
        .describe = describe,
        .held = held,
        .tick_of = tick_of,
        .io = &store_io,
    };
    return bench_main(&desc, argc, argv);
}
