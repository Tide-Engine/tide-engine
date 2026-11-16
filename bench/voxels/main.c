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
// voxels.tide), and the run fails on what it finds wrong. With `out` and
// `back`, each flies straight out, comes back the way it went and stays: a
// world that held many chunks and holds few.

#include <stdio.h>
#include <string.h>

#include <stdlib.h>

#include "bench.h"
#include "tide/io.h"
#include "tide/storage.h"
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
// --fill: the ticks a hop's whole box may take to be there, at the median, or the run fails
static uint32_t fill_most;
static bool fill_failed;

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
    {"watch", "1: counts the chunks that come to each bot's machine and times the box of a dimension it hops into, "
              "outside every timing; 0 for a profiler, which would see the counting", 1, 1, 0, 0},
    {"look", "bots of each hundred that turn a little every tick, as a player's mouse does: an input that changes "
             "every tick, which goes to every player", 0, 0, 0, 0},
    {"make", "what a dimension makes of chunks a tick at most: a chunk of air or water counts 1, one with ground in it "
             "10", 480, 480, 0, 1},
    {"spread", "cells between the bots' starts, on a square grid: 0 has them start together, 256 is past a default "
               "view's reach, so no two share a chunk", 0, 0, 0, 0},
    {"park", "1: the bots start a dimension each (the nth in dimension n of --dimensions, around)", 0, 0, 0, 0},
    {"out", "1: each bot flies straight out a way of its own for the whole run (eight ways), never turning", 0, 0, 0, 0},
    {"fill", "the run fails where the whole box of a dimension a bot hopped into took more ticks than this to be on "
             "its machine, at the median, or where one never was; 0 doesn't check", 0, 0, 0, 0},
    {"back", "with --out: seconds after which each bot flies back the way it came, for as long, and then stays there: "
             "a world that held many chunks and holds few; 0 never", 0, 0, 0, 0},
    {"ask", "what a dimension spawns of chunks a tick at most: a box of more is asked for over the ticks it takes, "
            "nearest first", 1024, 1024, 0, 1},
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
    w->Worlds.make = (int32_t)f[16].value;
    w->Worlds.spread = (int32_t)f[17].value;
    w->Worlds.ask = (int32_t)f[21].value;
    w->Worlds.park = (int32_t)f[18].value;
    audit_every = f[12].value;
    fill_most = f[20].value;
    forget_chunks = f[13].value != 0;
}

static void sample(const bench_flag *f, const uint32_t bot, const uint32_t tick, void *input)
{
    Controls *c = input;
    c->fly = true;
    const uint32_t tower = f[8].value * 60u;
    const uint32_t hop = tower ? 0u : f[3].value * 60u;
    c->hops = hop ? (int32_t)((tick + mix(bot) % hop) / hop) : 0;
    // A mouse: a turn of its own each tick, a few thousandths of a radian as a hand makes them
    if (mix(bot * 31337u + 5u) % 100u < f[15].value) {
        const uint32_t turn = mix(bot * 2246822519u ^ tick);
        c->look = (tide_float2){(float)((int32_t)(turn & 63u) - 32) * 0.003f, (float)((int32_t)((turn >> 6) & 31u) - 16) * 0.003f};
    }
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
    if (f[19].value) c->move = (tide_float3){ways[bot & 7u][0], 0, ways[bot & 7u][1]}; // --out
    if (f[19].value && f[21].value) { // --back: out for that long, back as long, then still, digging and building nothing
        const uint32_t back = f[21].value * 60u;
        if (tick >= back) c->move = tick < 2u * back ? (tide_float3){-ways[bot & 7u][0], 0, -ways[bot & 7u][1]} : (tide_float3){0, 0, 0};
        if (tick >= 2u * back) return;
    }
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

// What a world's memory is made of (bench_desc.parts): the pages its entity table, its tables and its heaps
// have, as their sizes together, whoever else shares them
static void parts(const void *world)
{
    const tide_world *w = world;
    const tide_table *tables[] = {&w->arch0_Far_Visibility, &w->arch1_Dimension_Visibility, &w->arch2_Owner_Avatar,
                                  &w->arch3_Chunk_Visibility, &w->arch4_Main};
    uint64_t rows = 0, holes = 0, chunks = 0;
    for (uint32_t i = 0; i < sizeof tables / sizeof tables[0]; i++) {
        rows += tables[i]->count;
        holes += tables[i]->holes;
        chunks += tables[i]->chunks;
    }
    printf("    its entity table %u slots in %u pages (%.2f MB); tables %llu rows, %llu of them holes, in %llu chunks; "
           "heaps %u pages in memory (%.2f MB) and %u given back, %.2f MB handed out",
           w->entities.next_unused, w->entities.pages, (double)w->entities.pages * 32768.0 / 1e6, (unsigned long long)rows,
           (unsigned long long)holes, (unsigned long long)chunks, tide_heaps_pages(&w->heap),
           (double)tide_heaps_pages(&w->heap) * 16384.0 / 1e6, tide_heaps_gone(&w->heap),
           (double)tide_heaps_used(&w->heap) / 1e6);
    printf("; packed: entities %.2f MB, heaps %.2f MB\n", (double)tide_entities_packed_size(&w->entities) / 1e6,
           (double)tide_heaps_packed_size(&w->heap) / 1e6);
}

static void describe_watched(void);

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
    // The loads the store served, wherever it keeps them, and those it answered from what it knows is there, with
    // no read (tide/storage.h, The files a provider has); the reads and what they found are this store's in memory
    uint64_t loads, unread;
    tide_storage_loads(&loads, &unread);
    printf("  the store: %u chunks and far regions saved in %llu bytes (%llu saves); %llu loads, %llu of them answered "
           "with no read; %llu reads of this store, %llu of them found\n",
           store_count, (unsigned long long)store_bytes, (unsigned long long)store_writes, (unsigned long long)loads,
           (unsigned long long)unread, (unsigned long long)store_reads, (unsigned long long)store_found);
    if (forget_chunks) printf("  chunks' saves forgotten (--forget): %llu\n", (unsigned long long)store_forgot);
    describe_watched();
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

// What a bot's machine has, frame by frame (bench_desc.watch): the chunks that come to it, and how long the box
// of a dimension it hops into takes to be there. A chunk came where the frame before didn't have its place.
typedef struct place {
    uint64_t key;   // Its dimension and place, plus 1: 0 is an empty slot
    uint32_t frame; // The last frame it was here
} place;

typedef struct watcher {
    place *places;
    uint32_t room, count, frame;
    uint64_t frames, came;
    int32_t hops;    // As its input last asked
    int32_t from;    // The dimension it left, while the one it hopped into fills; -1 otherwise
    uint32_t since;  // Frames since that hop
    bool most;       // Nine tenths of the box are there
    int32_t home;    // The dimension most of its chunks were in, last frame
    // The same hop on the server (watch_server): it has asked for the box, has nine tenths of it made, all of it
    bool asked, made_most, made_all;
} watcher;

#define WATCHED 256u
#define FILLS 4096u
static watcher watchers[WATCHED];
static float fills_most[FILLS], fills_all[FILLS];
static uint32_t fills_most_count, fills_all_count, hops_seen, hops_unfilled;
// ...and on the server, in frames from the same moment: the box asked for, nine tenths of it made, all of it
static float asks[FILLS], makes_most[FILLS], makes_all[FILLS];
static uint32_t asks_count, makes_most_count, makes_all_count;

static place *place_slot(const watcher *w, const uint64_t key)
{
    for (uint32_t i = (uint32_t)((key * 0x9e3779b97f4a7c15ull) >> 40) & (w->room - 1u);; i = (i + 1u) & (w->room - 1u)) {
        if (!w->places[i].key || w->places[i].key == key) return &w->places[i];
    }
}

static int32_t hops_at(const bench_flag *f, const uint32_t bot, const uint32_t tick)
{
    const uint32_t hop = f[8].value ? 0u : f[3].value * 60u;
    return hop ? (int32_t)((tick + mix(bot) % hop) / hop) : 0;
}

// The server's side of each hop a bot's machine is waiting on: the chunks of the dimension it hopped into that are
// its player's (bot n is player n: they join in order), asked for and made. It runs before the bots' `watch` of
// the frame, so a hop that one saw is a frame older here.
static void watch_server(const bench_flag *f, const void *world)
{
    if (!f[14].value) return;
    const tide_world *w = world;
    uint32_t waiting = 0;
    for (uint32_t i = 0; i < WATCHED && i < 32u; i++) waiting |= watchers[i].frames && watchers[i].from >= 0 && !watchers[i].made_all ? 1u << i : 0u;
    if (!waiting) return;
    const int32_t dimensions = w->Worlds.count;
    static uint32_t asked[32], made[32];
    memset(asked, 0, sizeof asked);
    memset(made, 0, sizeof made);
    EACH(w, e)
    {
        if (!tide_has_Chunk(w, e) || !tide_has_Seen(w, e)) continue;
        const uint32_t audience = (uint32_t)tide_get_Seen(w, e).audience.players & waiting;
        if (!audience) continue;
        const Chunk chunk = tide_get_Chunk(w, e);
        for (uint32_t i = 0; i < 32u; i++) {
            if (!(audience >> i & 1u) || chunk.dimension != (watchers[i].from + 1) % dimensions) continue;
            asked[i]++;
            made[i] += chunk.stage == Stage_Made;
        }
    }
    const uint32_t near = f[0].value, tall = f[1].value;
    const uint32_t box = (2u * near + 1u) * (2u * near + 1u) * (2u * tall + 1u);
    for (uint32_t i = 0; i < 32u; i++) {
        watcher *me = &watchers[i];
        if (!(waiting >> i & 1u)) continue;
        const float since = (float)(me->since + 1u);
        if (!me->asked && asked[i] >= box) {
            me->asked = true;
            if (asks_count < FILLS) asks[asks_count++] = since;
        }
        if (!me->made_most && made[i] * 10u >= box * 9u) {
            me->made_most = true;
            if (makes_most_count < FILLS) makes_most[makes_most_count++] = since;
        }
        if (made[i] >= box) {
            me->made_all = true;
            if (makes_all_count < FILLS) makes_all[makes_all_count++] = since;
        }
    }
}

static void watch(const bench_flag *f, const uint32_t bot, const void *world)
{
    if (bot >= WATCHED || !f[14].value) return;
    const tide_world *w = world;
    watcher *me = &watchers[bot];
    const uint32_t frame = ++me->frame;
    const int32_t dimensions = w->Worlds.count;
    uint32_t in[16] = {0}, other = 0, came = 0;
    EACH(w, e)
    {
        if (!tide_has_Chunk(w, e)) continue;
        const Chunk chunk = tide_get_Chunk(w, e);
        if (chunk.dimension >= 0 && chunk.dimension < 16) in[chunk.dimension]++;
        other += me->from >= 0 && chunk.dimension == (me->from + 1) % dimensions;
        if ((me->count + 1u) * 2u > me->room) {
            const uint32_t had = me->room;
            place *was = me->places;
            me->room = had ? had * 2u : 4096u;
            me->places = calloc(me->room, sizeof *me->places);
            if (!me->places) exit(1);
            for (uint32_t i = 0; i < had; i++) {
                if (was[i].key) *place_slot(me, was[i].key) = was[i];
            }
            free(was);
        }
        const uint64_t key = (((uint64_t)(uint32_t)chunk.dimension << 56) ^ ((uint64_t)((uint32_t)chunk.at.x & 0xfffffu) << 32)
                              ^ ((uint64_t)((uint32_t)chunk.at.y & 0xfffu) << 20) ^ (uint64_t)((uint32_t)chunk.at.z & 0xfffffu))
                             + 1u;
        place *p = place_slot(me, key);
        if (!p->key) {
            p->key = key;
            me->count++;
        }
        came += p->frame + 1u != frame;
        p->frame = frame;
    }
    me->frames++;
    me->came += came;

    // A hop: the tick this machine's input first asks for another dimension, to the frame it has the box there
    const uint32_t near = f[0].value, tall = f[1].value;
    const uint32_t box = (2u * near + 1u) * (2u * near + 1u) * (2u * tall + 1u);
    const int32_t hops = hops_at(f, bot, tick_of(world));
    if (frame == 1u) {
        me->hops = hops;
        me->from = -1;
    }
    if (hops != me->hops) {
        if (me->from >= 0) hops_unfilled++;
        me->hops = hops;
        me->from = me->home;
        me->since = 0;
        me->most = false;
        me->asked = me->made_most = me->made_all = false;
        hops_seen++;
        other = 0;
    } else if (me->from >= 0) {
        me->since++;
        if (!me->most && other * 10u >= box * 9u) {
            me->most = true;
            if (fills_most_count < FILLS) fills_most[fills_most_count++] = (float)me->since;
        }
        if (other >= box) {
            if (fills_all_count < FILLS) fills_all[fills_all_count++] = (float)me->since;
            me->from = -1;
        }
    }
    int32_t home = 0;
    for (int32_t i = 1; i < 16; i++) home = in[i] > in[home] ? i : home;
    me->home = home;
}

static int compare_fills(const void *a, const void *b)
{
    const float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

// After the server's own lines: what came to the bots' machines
static void describe_watched(void)
{
    uint64_t frames = 0, came = 0;
    for (uint32_t i = 0; i < WATCHED; i++) {
        frames += watchers[i].frames;
        came += watchers[i].came;
        hops_unfilled += watchers[i].frames && watchers[i].from >= 0;
    }
    if (!frames) return;
    const double seconds = (double)frames / 60.0;
    const double cells = 16.0 * 16.0 * 16.0; // A Block is a byte
    printf("  a bot's machine: %.1f chunks came to it a second, %.1f KB a second as their cells alone (%g bytes each)\n",
           (double)came / seconds, (double)came * cells / seconds / 1e3, cells);
    if (!hops_seen) return;
    qsort(fills_most, fills_most_count, sizeof *fills_most, compare_fills);
    qsort(fills_all, fills_all_count, sizeof *fills_all, compare_fills);
    printf("  after a hop (%u of them): nine tenths of the box there in ", hops_seen);
    if (fills_most_count) printf("%.0f ticks median, %.0f at most", fills_most[fills_most_count / 2u], fills_most[fills_most_count - 1u]);
    else printf("-");
    printf("; all of it in ");
    if (fills_all_count) printf("%.0f median, %.0f at most", fills_all[fills_all_count / 2u], fills_all[fills_all_count - 1u]);
    else printf("-");
    printf("; %u never had it all before the next hop or the end\n", hops_unfilled);
    // (The last hop of a run may be cut short by its end: one that never filled is allowed for that)
    if (fill_most && (!fills_all_count || fills_all[fills_all_count / 2u] > (float)fill_most || hops_unfilled > 1u)) {
        fprintf(stderr, "voxels: a hop's whole box took more than %u ticks to be there at the median (--fill), or never was\n",
                fill_most);
        fill_failed = true;
    }
    if (!asks_count) return;
    qsort(asks, asks_count, sizeof *asks, compare_fills);
    qsort(makes_most, makes_most_count, sizeof *makes_most, compare_fills);
    qsort(makes_all, makes_all_count, sizeof *makes_all, compare_fills);
    printf("  ...of that, on the server: the box asked for %.0f ticks after the bot's machine hopped (median; %.0f at most)",
           asks[asks_count / 2u], asks[asks_count - 1u]);
    if (makes_most_count) printf(", nine tenths of it made after %.0f (%.0f)", makes_most[makes_most_count / 2u], makes_most[makes_most_count - 1u]);
    if (makes_all_count) printf(", all of it after %.0f (%.0f)", makes_all[makes_all_count / 2u], makes_all[makes_all_count - 1u]);
    printf("\n");
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
        .parts = parts,
        .tick_of = tick_of,
        .watch = watch,
        .watch_server = watch_server,
        .io = &store_io,
    };
    const int code = bench_main(&desc, argc, argv);
    return code ? code : audit_failed || fill_failed ? 1 : 0;
}
