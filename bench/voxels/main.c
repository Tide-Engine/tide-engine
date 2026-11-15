// Voxels' bots, and its run (see bench/bench.h).
//
// Each bot flies off in a direction of its own, turning every few seconds, so
// the box of chunks it has keeps moving: chunks are made ahead of it and left
// behind it. It digs and builds some of the time (`builders` of each hundred
// bots do), and every `hop` seconds goes to the next dimension. With `tower`,
// each flies straight out a way of its own for that many seconds and then
// climbs, building a tower, all in the first dimension: what each builds is far
// from the others, who see it in their far regions. With `audit`, the server
// checks its far regions against its chunks every that many ticks (Audit in
// voxels.tide), and the run fails on what it finds wrong.

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
static uint64_t store_bytes, store_writes, store_reads, store_found, store_far_found, store_forgot;
// --forget: chunks' saves aren't kept (far regions' are), as a server that stopped before it saved its chunks
// would have lost them
static bool forget_chunks;
// --audit: the far regions' checks ran (Streamer.audits) and found nothing wrong, or the run fails
static uint32_t audit_every;
static bool audit_failed;

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
    store_far_found += strncmp(path, "saved/far/", 10) == 0;
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
    if (forget_chunks && strncmp(path, "saved/chunks/", 13) == 0) {
        store_forgot++;
        return TIDE_IO_NONE;
    }
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

// A folder's files or folders, one level, as a provider gives them (Storage.List walks them): what's kept under
// `path`, a file each, and the first folder of each path that goes deeper, once. Folders are only names here, so
// one nothing is in isn't there.
static tide_io_error store_list(void *user, const char *path, const bool directories, const bool recursive,
                                const tide_io_entry_fn entry, void *entry_user)
{
    (void)user;
    (void)recursive;
    const size_t n = strlen(path);
    char **given = NULL;
    uint32_t given_count = 0, given_room = 0;
    bool any = false;
    for (uint32_t i = 0; i < store_room; i++) {
        const char *p = store[i].path;
        if (!p || strncmp(p, path, n) != 0 || p[n] != '/') continue;
        any = true;
        const char *slash = strchr(p + n + 1, '/');
        if (!directories) {
            if (!slash) entry(entry_user, p);
            continue;
        }
        if (!slash) continue;
        const size_t size = (size_t)(slash - p);
        bool seen = false;
        for (uint32_t k = 0; k < given_count && !seen; k++) seen = strlen(given[k]) == size && memcmp(given[k], p, size) == 0;
        if (seen) continue;
        if (given_count == given_room) {
            given_room = given_room ? given_room * 2u : 16u;
            char **grown = realloc(given, given_room * sizeof *given);
            if (!grown) break;
            given = grown;
        }
        char *folder = malloc(size + 1u);
        if (!folder) break;
        memcpy(folder, p, size);
        folder[size] = '\0';
        given[given_count++] = folder;
        entry(entry_user, folder);
    }
    for (uint32_t k = 0; k < given_count; k++) free(given[k]);
    free(given);
    return any ? TIDE_IO_NONE : TIDE_IO_NOT_FOUND;
}

static const tide_io store_io = {.exists = store_exists, .create = store_create, .read = store_read, .write = store_write,
                                 .list = store_list};

static bench_flag flags[] = {
    {"near", "chunks a player has around them, sideways", 6, 2, 0, 1},
    {"tall", "chunks a player has above and below them", 2, 1, 0, 0},
    {"dimensions", "dimensions the world has", 2, 2, 0, 1},
    {"hop", "seconds between a bot's changes of dimension; 0 never", 20, 2, 0, 0},
    {"still", "bots of each hundred that stay where they are", 0, 0, 0, 0},
    {"keep", "ticks between saves of a chunk players changed, while it's held", 600, 90, 0, 1},
    {"linger", "ticks a chunk or a far region nobody has stays before it goes", 120, 5, 0, 0},
    {"builders", "bots of each hundred that dig and build", 100, 100, 0, 0},
    {"tower", "seconds a bot flies straight out, each its own way, before it climbs building a tower (in its first "
              "dimension: no hops); 0 never", 0, 0, 0, 0},
    {"far", "far regions (8 by 8 chunks) a player has around theirs, sideways", 6, 1, 0, 1},
    {"pace", "far columns a dimension's changes are sent in each time, at most", 64, 64, 0, 1},
    {"gap", "ticks between sendings of a dimension's far changes; 0 every tick", 30, 30, 0, 0},
    {"audit", "ticks between checks that the far regions say what the chunks the server has do, and are the right "
              "players'; the run fails on what they find; 0 never", 0, 60, 0, 0},
    {"forget", "1: the store keeps no chunk's save, and keeps the far regions', as a server that stopped before it "
               "saved its chunks would have", 0, 0, 0, 0},
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
    w->Worlds.far = (int32_t)f[9].value;
    w->Worlds.pace = (int32_t)f[10].value;
    w->Worlds.gap = (int32_t)f[11].value;
    w->Worlds.audit = (int32_t)f[12].value;
    audit_every = f[12].value;
    forget_chunks = f[13].value != 0;
}

static void sample(const bench_flag *f, const uint32_t bot, const uint32_t tick, void *input)
{
    Controls *c = input;
    c->fly = true;
    const uint32_t tower = f[8].value * 60u;
    const uint32_t hop = tower ? 0u : f[3].value * 60u;
    c->hops = hop ? (int32_t)((tick + mix(bot) % hop) / hop) : 0;
    if (mix(bot * 7919u + 1u) % 100u < f[4].value) return;
    const bool builds = mix(bot * 104729u + 3u) % 100u < f[7].value;
    // A direction along the ground for five seconds, from a table: no platform's sines
    static const float ways[8][2] = {{1, 0}, {0.7f, 0.7f}, {0, 1}, {-0.7f, 0.7f}, {-1, 0}, {-0.7f, -0.7f}, {0, -1}, {0.7f, -0.7f}};
    if (tower) {
        // Out along a way of its own (eight bots, eight ways), then straight up, a plank above it each cell it rises:
        // a tower whose top changes twelve times a second (it flies 12 cells a second), far from the others'
        if (tick < tower) c->move = (tide_float3){ways[bot & 7u][0], 0, ways[bot & 7u][1]};
        else {
            c->move = (tide_float3){0, 1, 0};
            c->build = builds;
        }
        return;
    }
    const uint32_t leg = (tick + bot * 53u) / 300u;
    const uint32_t h = mix(bot * 2654435761u ^ leg);
    c->move = (tide_float3){ways[h & 7u][0], (float)((h >> 3) % 3u) * 0.1f - 0.1f, ways[h & 7u][1]};
    c->dig = builds && (tick / 30u + bot) % 4u == 0u;
    c->build = builds && (tick / 45u + bot) % 6u == 0u;
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
    uint32_t chunks = 0, edited = 0, avatars = 0, regions = 0, sent = 0, far = 0;
    uint64_t edits = 0, sendings = 0, audits = 0, wrong = 0;
    EACH(w, e)
    {
        avatars += tide_has_Avatar(w, e);
        if (tide_has_Streamer(w, e)) {
            const Streamer streamer = tide_get_Streamer(w, e);
            audits += (uint64_t)streamer.audits;
            wrong += (uint64_t)streamer.wrong;
        }
        if (tide_has_Far(w, e)) {
            regions++;
            const int32_t version = tide_get_Far(w, e).version;
            sent += version > 0;
            sendings += (uint64_t)version;
        }
        if (!tide_has_Chunk(w, e)) continue;
        chunks++;
        const int32_t changed = tide_get_Chunk(w, e).edits;
        edited += changed > 0;
        edits += (uint64_t)changed;
    }
    for (uint32_t i = 0; i < store_room; i++) far += store[i].path && strncmp(store[i].path, "saved/far/", 10) == 0;
    printf("  chunks %u (%u changed by players, %llu changes), avatars %u\n", chunks, edited, (unsigned long long)edits, avatars);
    printf("  far regions %u (%u with columns players changed, sent %llu times); %u kept in the store, %llu loaded back\n",
           regions, sent, (unsigned long long)sendings, far, (unsigned long long)store_far_found);
    printf("  the store: %u chunks and far regions saved in %llu bytes (%llu saves); %llu loads, %llu of them found\n",
           store_count, (unsigned long long)store_bytes, (unsigned long long)store_writes, (unsigned long long)store_reads,
           (unsigned long long)store_found);
    if (forget_chunks) printf("  chunks' saves forgotten (--forget): %llu\n", (unsigned long long)store_forgot);
    if (!audit_every) return;
    printf("  far checks every %u ticks (--audit): %llu, %llu wrong\n", audit_every, (unsigned long long)audits,
           (unsigned long long)wrong);
    if (!audits || wrong) {
        fprintf(stderr, "voxels: the far regions' checks %s\n", audits ? "found what's wrong above" : "never ran");
        audit_failed = true;
    }
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
    const int code = bench_main(&desc, argc, argv);
    return code ? code : audit_failed ? 1 : 0;
}
