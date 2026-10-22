#include "tide/shards.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// See tide/shards.h. The orchestrator keeps, outside every world: which shard
// owns each entity that moved (the directory; one that never moved is the
// shard's whose range its index is in), which messages of each shard it
// carried, and what it last sent each shard of its neighbours' ghosts.

#define RANGE_SIZE (1u << 20) // Entity indices a shard is given at a time
#define SPLIT_EVERY 30u       // Updates between looks at whether a region should split

// ---------------------------------------------------------------------------
// A map of 64-bit keys (never 0) to 64-bit values: linear probing, deletion by
// shifting back what follows.

typedef struct map64 {
    uint64_t *keys;
    uint64_t *values;
    uint32_t count, room; // room: a power of two, or 0
} map64;

static uint32_t slot_of(const map64 *m, const uint64_t key)
{
    uint64_t h = key * 0x9E3779B97F4A7C15ull;
    h ^= h >> 32;
    return (uint32_t)h & (m->room - 1u);
}

static uint64_t *map_find(const map64 *m, const uint64_t key)
{
    if (!m->room) return NULL;
    for (uint32_t i = slot_of(m, key);; i = (i + 1u) & (m->room - 1u)) {
        if (m->keys[i] == key) return &m->values[i];
        if (!m->keys[i]) return NULL;
    }
}

static void map_put(map64 *m, uint64_t key, uint64_t value);

static void map_grow(map64 *m)
{
    const map64 old = *m;
    m->room = old.room ? old.room * 2u : 64u;
    m->keys = calloc(m->room, sizeof *m->keys);
    m->values = calloc(m->room, sizeof *m->values);
    if (!m->keys || !m->values) tide_out_of_memory();
    m->count = 0;
    for (uint32_t i = 0; i < old.room; i++) {
        if (old.keys[i]) map_put(m, old.keys[i], old.values[i]);
    }
    free(old.keys);
    free(old.values);
}

static void map_put(map64 *m, const uint64_t key, const uint64_t value)
{
    if ((m->count + 1u) * 2u > m->room) map_grow(m);
    for (uint32_t i = slot_of(m, key);; i = (i + 1u) & (m->room - 1u)) {
        if (m->keys[i] == key) {
            m->values[i] = value;
            return;
        }
        if (!m->keys[i]) {
            m->keys[i] = key;
            m->values[i] = value;
            m->count++;
            return;
        }
    }
}

static void map_remove(map64 *m, const uint64_t key)
{
    if (!m->room) return;
    uint32_t i = slot_of(m, key);
    while (m->keys[i] != key) {
        if (!m->keys[i]) return;
        i = (i + 1u) & (m->room - 1u);
    }
    m->keys[i] = 0;
    m->count--;
    // What follows in its run moves back into the gap when it can
    for (uint32_t j = (i + 1u) & (m->room - 1u); m->keys[j]; j = (j + 1u) & (m->room - 1u)) {
        const uint32_t home = slot_of(m, m->keys[j]);
        const bool between = i <= j ? (home > i && home <= j) : (home > i || home <= j);
        if (between) continue;
        m->keys[i] = m->keys[j];
        m->values[i] = m->values[j];
        m->keys[j] = 0;
        i = j;
    }
}

static void map_free(map64 *m)
{
    free(m->keys);
    free(m->values);
    memset(m, 0, sizeof *m);
}

static uint64_t key_of(const tide_entity e)
{
    return ((uint64_t)e.generation << 32) | e.index;
}

static tide_entity entity_of(const uint64_t key)
{
    return (tide_entity){(uint32_t)key, (uint32_t)(key >> 32)};
}

// ---------------------------------------------------------------------------

typedef struct shard {
    bool used;
    bool region; // The root has none
    tide_shard_box box;
    tide_server *server;
    uint32_t delivered; // Its messages carried, through this number
    // What it last sent each shard of its ghosts: by tree root, its bytes' hash
    map64 *sent;
    uint32_t sent_room;
    uint64_t singletons; // The root's: its singletons' hash, as last sent
} shard;

typedef struct range {
    uint32_t first, end, shard;
} range;

struct tide_shards {
    tide_shards_desc desc;
    const tide_game *game;
    const tide_shard_game *api;
    uint32_t tick_rate;
    float dt;
    shard *shards;
    uint32_t count, room;
    map64 directory; // Entity key to the shard it last went to
    range *ranges;
    uint32_t range_count;
    uint64_t next_index; // The next range's first
    uint64_t updates;
    double now;
};

static map64 *sent_to(tide_shards *s, const uint32_t from, const uint32_t to)
{
    shard *f = &s->shards[from];
    if (to >= f->sent_room) {
        const uint32_t room = to + 8u;
        map64 *grown = realloc(f->sent, room * sizeof *grown);
        if (!grown) tide_out_of_memory();
        memset(grown + f->sent_room, 0, (room - f->sent_room) * sizeof *grown);
        f->sent = grown;
        f->sent_room = room;
    }
    return &f->sent[to];
}

static void write_regions(const tide_shards *s, tide_writer *w)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < s->count; i++) count += s->shards[i].used && s->shards[i].region;
    tide_write_u32(w, count);
    for (uint32_t i = 0; i < s->count; i++) {
        const shard *sh = &s->shards[i];
        if (!sh->used || !sh->region) continue;
        tide_write_u32(w, i);
        tide_write_bytes(w, sh->box.lo, sizeof sh->box.lo);
        tide_write_bytes(w, sh->box.hi, sizeof sh->box.hi);
    }
}

static range give_range(tide_shards *s, const uint32_t shard_id)
{
    if (s->next_index + RANGE_SIZE > UINT32_MAX) {
        fprintf(stderr, "tide: the sharded world has given every entity index it has\n");
        abort();
    }
    const range r = {(uint32_t)s->next_index, (uint32_t)(s->next_index + RANGE_SIZE), shard_id};
    s->next_index += RANGE_SIZE;
    range *grown = realloc(s->ranges, (s->range_count + 1u) * sizeof *grown);
    if (!grown) tide_out_of_memory();
    s->ranges = grown;
    s->ranges[s->range_count++] = r;
    return r;
}

// A CONFIG message: who the shard is, the regions, and its ranges
static uint8_t *config_for(tide_shards *s, const uint32_t id, uint32_t *size)
{
    tide_writer w = {.grows = true};
    tide_shard_header(&w, TIDE_SHARD_CONFIG, TIDE_SHARD_ORCHESTRATOR, 0u);
    tide_write_u32(&w, id);
    tide_write_u8(&w, id == TIDE_SHARD_ROOT ? TIDE_SHARD_ROOT_ROLE : TIDE_SHARD_REGION);
    write_regions(s, &w);
    uint32_t count = 0;
    for (uint32_t i = 0; i < s->range_count; i++) count += s->ranges[i].shard == id;
    tide_write_u32(&w, count);
    for (uint32_t i = 0; i < s->range_count; i++) {
        if (s->ranges[i].shard != id) continue;
        tide_write_u32(&w, s->ranges[i].first);
        tide_write_u32(&w, s->ranges[i].end);
    }
    if (w.overflow) tide_out_of_memory();
    *size = w.size;
    return w.data;
}

static void inject(tide_shards *s, const uint32_t to, const uint8_t *data, const uint32_t size)
{
    if (to >= s->count || !s->shards[to].used) return;
    if (!tide_server_inject(s->shards[to].server, data, size)) tide_out_of_memory();
}

static uint32_t new_shard(tide_shards *s)
{
    for (uint32_t i = 1; i < s->count; i++) {
        if (!s->shards[i].used && !s->shards[i].server) return i;
    }
    if (s->count == s->room) {
        const uint32_t room = s->room ? s->room * 2u : 8u;
        shard *grown = realloc(s->shards, room * sizeof *grown);
        if (!grown) tide_out_of_memory();
        memset(grown + s->room, 0, (room - s->room) * sizeof *grown);
        s->shards = grown;
        s->room = room;
    }
    return s->count++;
}

// A shard's server, from `world` (which it copies), or a world started for it
static void start_server(tide_shards *s, const uint32_t id, const void *world, const double now)
{
    void *started = NULL;
    if (!world) {
        uint32_t size;
        uint8_t *config = config_for(s, id, &size);
        started = calloc(1, s->game->world_size);
        if (!started) tide_out_of_memory();
        // A shard that starts later starts at the world's tick, the root's
        const uint32_t tick = id != TIDE_SHARD_ROOT && s->shards[0].server ? tide_server_tick(s->shards[0].server) : 0u;
        s->api->start(started, s->dt, id == TIDE_SHARD_ROOT ? s->desc.start : NULL, s->desc.seed, tick, config, size);
        free(config);
        world = started;
    }
    tide_server_desc d = {0};
    d.game = s->game;
    d.tick_rate = s->tick_rate;
    d.dt = s->dt;
    d.world = world;
    d.seed = s->desc.seed;
    d.jobs = s->desc.jobs;
    if (s->desc.transports) s->desc.transports(s->desc.user, id, d.transports);
    shard *sh = &s->shards[id];
    sh->server = tide_server_create(&d, now);
    if (!sh->server) tide_out_of_memory();
    sh->used = true;
    if (started) {
        if (s->game->free_world) s->game->free_world(started);
        free(started);
    }
}

static void tell_map(tide_shards *s)
{
    tide_writer w = {.grows = true};
    tide_shard_header(&w, TIDE_SHARD_MAP, TIDE_SHARD_ORCHESTRATOR, 0u);
    write_regions(s, &w);
    if (w.overflow) tide_out_of_memory();
    for (uint32_t i = 0; i < s->count; i++) inject(s, i, w.data, w.size);
    free(w.data);
}

static bool setup(tide_shards *s, const tide_shards_desc *desc)
{
    if (!desc || !desc->game || !desc->game->shards) return false;
    s->desc = *desc;
    s->game = desc->game;
    s->api = desc->game->shards;
    s->tick_rate = desc->tick_rate ? desc->tick_rate : desc->game->tick_rate ? desc->game->tick_rate : 60u;
    s->dt = desc->dt > 0.0f ? desc->dt : 1.0f / (float)s->tick_rate;
    return true;
}

tide_shards *tide_shards_create(const tide_shards_desc *desc, const double now)
{
    tide_shards *s = calloc(1, sizeof *s);
    if (!s || !setup(s, desc)) {
        free(s);
        return NULL;
    }
    s->now = now;
    // The root, then a region for each cell of the split world
    const uint32_t root = new_shard(s);
    give_range(s, root);
    uint32_t n[3];
    for (uint32_t k = 0; k < 3u; k++) n[k] = desc->split[k] ? desc->split[k] : 1u;
    if (s->api->dims == 2u) n[2] = 1u;
    for (uint32_t z = 0; z < n[2]; z++) {
        for (uint32_t y = 0; y < n[1]; y++) {
            for (uint32_t x = 0; x < n[0]; x++) {
                const uint32_t id = new_shard(s);
                shard *sh = &s->shards[id];
                sh->region = true;
                const uint32_t cell[3] = {x, y, z};
                for (uint32_t k = 0; k < 3u; k++) {
                    const double lo = desc->world.lo[k], hi = desc->world.hi[k];
                    const double step = (hi - lo) / (double)n[k];
                    // The outer edges reach past the world, so what goes outside it stays in a region
                    sh->box.lo[k] = cell[k] == 0 ? -INFINITY : lo + step * (double)cell[k];
                    sh->box.hi[k] = cell[k] == n[k] - 1u ? INFINITY : lo + step * (double)(cell[k] + 1u);
                    if (k == 2 && s->api->dims == 2u) {
                        sh->box.lo[k] = -INFINITY;
                        sh->box.hi[k] = INFINITY;
                    }
                }
                sh->used = true; // So the regions written into configs have it
                give_range(s, id);
            }
        }
    }
    for (uint32_t i = 0; i < s->count; i++) start_server(s, i, NULL, now);
    return s;
}

void tide_shards_destroy(tide_shards *s)
{
    if (!s) return;
    for (uint32_t i = 0; i < s->count; i++) {
        shard *sh = &s->shards[i];
        if (sh->server) tide_server_destroy(sh->server);
        for (uint32_t k = 0; k < sh->sent_room; k++) map_free(&sh->sent[k]);
        free(sh->sent);
    }
    free(s->shards);
    free(s->ranges);
    map_free(&s->directory);
    free(s);
}

uint32_t tide_shards_count(const tide_shards *s)
{
    return s->count;
}

tide_server *tide_shards_server(const tide_shards *s, const uint32_t shard)
{
    return shard < s->count && s->shards[shard].used ? s->shards[shard].server : NULL;
}

const void *tide_shards_world(const tide_shards *s, const uint32_t shard)
{
    tide_server *server = tide_shards_server(s, shard);
    return server ? tide_server_world(server) : NULL;
}

bool tide_shards_region(const tide_shards *s, const uint32_t shard, tide_shard_box *out)
{
    if (shard >= s->count || !s->shards[shard].used || !s->shards[shard].region) return false;
    *out = s->shards[shard].box;
    return true;
}

void tide_shards_status(const tide_shards *s, const uint32_t shard, tide_shard_status *out)
{
    memset(out, 0, sizeof *out);
    out->id = TIDE_SHARD_NONE;
    const void *world = tide_shards_world(s, shard);
    if (world) s->api->status(world, out);
}

int32_t tide_shards_owner(const tide_shards *s, const tide_entity e)
{
    const uint64_t *moved = map_find(&s->directory, key_of(e));
    if (moved) return (int32_t)*moved;
    for (uint32_t i = 0; i < s->range_count; i++) {
        if (e.index >= s->ranges[i].first && e.index < s->ranges[i].end) return (int32_t)s->ranges[i].shard;
    }
    return -1;
}

uint32_t tide_shards_at(const tide_shards *s, const double p[3])
{
    for (uint32_t i = 0; i < s->count; i++) {
        const shard *sh = &s->shards[i];
        if (sh->used && sh->region && tide_shard_box_holds(&sh->box, p, s->api->dims)) return i;
    }
    return TIDE_SHARD_ROOT;
}

void tide_shards_player(tide_shards *s, const tide_player_id player, const bool joined)
{
    uint8_t data[16];
    tide_writer w = {data, sizeof data, 0, false, false};
    tide_shard_header(&w, TIDE_SHARD_PLAYER, TIDE_SHARD_ORCHESTRATOR, 0u);
    tide_write_u8(&w, joined ? 1u : 0u);
    tide_write_u32(&w, player.id);
    inject(s, TIDE_SHARD_ROOT, data, w.size);
}

// ---------------------------------------------------------------------------
// Carrying messages

// The handles of an ARRIVE's tree (after its header, flags and lender)
static void note_tree(tide_shards *s, const uint8_t *data, const uint32_t size, const uint32_t to, const uint32_t from)
{
    tide_reader r = {data, size, 9u, false};
    tide_read_u8(&r);
    tide_read_u32(&r);
    const uint32_t count = tide_read_u32(&r);
    for (uint32_t i = 0; i < count && !r.failed; i++) {
        const uint32_t record = tide_read_u32(&r);
        if (r.failed || record < 8u || record > r.size - r.at) return;
        tide_reader e = {r.data + r.at, record, 0, false};
        tide_entity handle;
        handle.index = tide_read_u32(&e);
        handle.generation = tide_read_u32(&e);
        r.at += record;
        map_put(&s->directory, key_of(handle), to);
        // The shard it left keeps it as a ghost of the one it went to (the root
        // keeps none): what that one sends it of its ghosts starts from there
        if (i == 0 && s->shards[from].region) map_put(sent_to(s, to, from), key_of(handle), 1u);
    }
}

static void carry(tide_shards *s, const uint32_t from)
{
    shard *sh = &s->shards[from];
    uint32_t size = 0;
    uint8_t *bytes = s->api->outbox(tide_server_world(sh->server), &size);
    if (!bytes) return;
    uint32_t through = sh->delivered;
    tide_reader r = {bytes, size, 0, false};
    tide_shard_outgoing m;
    while (tide_shard_next_outgoing(&r, &m)) {
        if (!m.number || m.number <= sh->delivered) continue;
        through = m.number > through ? m.number : through;
        const uint8_t kind = m.data[0];
        switch (m.route) {
        case TIDE_SHARD_TO_SHARD:
            if (kind == TIDE_SHARD_ARRIVE) note_tree(s, m.data, m.size, m.target, from);
            inject(s, m.target, m.data, m.size);
            break;
        case TIDE_SHARD_TO_OWNER: {
            const int32_t owner = tide_shards_owner(s, m.entity);
            if (owner >= 0) inject(s, (uint32_t)owner, m.data, m.size);
            break;
        }
        case TIDE_SHARD_TO_ALL:
            for (uint32_t i = 0; i < s->count; i++) {
                if (i != from) inject(s, i, m.data, m.size);
            }
            break;
        default: break;
        }
    }
    free(bytes);
    if (through == sh->delivered) return;
    sh->delivered = through;
    // Delivered: it lets them go. Every shard is in this process and saved at
    // one tick, so delivered is as good as kept (tide/shard.h)
    uint8_t ack[16];
    tide_writer w = {ack, sizeof ack, 0, false, false};
    tide_shard_header(&w, TIDE_SHARD_ACK, TIDE_SHARD_ORCHESTRATOR, 0u);
    tide_write_u32(&w, through);
    inject(s, from, ack, w.size);
}

// ---------------------------------------------------------------------------
// Ghosts

typedef struct border {
    uint64_t *keys;
    uint64_t *hashes;
    uint32_t *at; // Where each tree's bytes are in `bytes`
    uint32_t *sizes;
    uint32_t count, room;
    tide_writer bytes;
} border;

static void take_border(void *context, const tide_entity e, const uint8_t *data, const uint32_t size)
{
    border *b = context;
    if (b->count == b->room) {
        b->room = b->room ? b->room * 2u : 64u;
        b->keys = realloc(b->keys, b->room * sizeof *b->keys);
        b->hashes = realloc(b->hashes, b->room * sizeof *b->hashes);
        b->at = realloc(b->at, b->room * sizeof *b->at);
        b->sizes = realloc(b->sizes, b->room * sizeof *b->sizes);
        if (!b->keys || !b->hashes || !b->at || !b->sizes) tide_out_of_memory();
    }
    b->keys[b->count] = key_of(e);
    const uint64_t hash = tide_hash(data, size);
    b->hashes[b->count] = hash ? hash : 2u; // 0 is no tree, and 1 one whose bytes aren't known
    b->at[b->count] = b->bytes.size;
    b->sizes[b->count] = size;
    tide_write_bytes(&b->bytes, data, size);
    b->count++;
}

static void free_border(border *b)
{
    free(b->keys);
    free(b->hashes);
    free(b->at);
    free(b->sizes);
    free(b->bytes.data);
}

// What `from` holds near `to`'s region, against what it last sent: what
// changed goes, and what went is dropped
static void send_ghosts(tide_shards *s, const uint32_t from, const uint32_t to)
{
    const shard *f = &s->shards[from];
    const shard *t = &s->shards[to];
    const void *world = tide_server_world(f->server);
    tide_shard_box near = t->box;
    for (uint32_t k = 0; k < 3u; k++) {
        near.lo[k] -= s->api->range;
        near.hi[k] += s->api->range;
    }
    border b = {.bytes = {.grows = true}};
    s->api->border(world, &near, take_border, &b);
    map64 *sent = sent_to(s, from, to);
    tide_writer m = {.grows = true};
    tide_shard_header(&m, TIDE_SHARD_GHOSTS, from, 0u);
    const uint32_t count_at = m.size;
    tide_write_u32(&m, 0u);
    uint32_t puts = 0;
    map64 now = {0};
    for (uint32_t i = 0; i < b.count; i++) {
        map_put(&now, b.keys[i], 1u);
        const uint64_t *had = map_find(sent, b.keys[i]);
        if (had && *had == b.hashes[i]) continue;
        tide_write_bytes(&m, b.bytes.data + b.at[i], b.sizes[i]);
        map_put(sent, b.keys[i], b.hashes[i]);
        puts++;
    }
    // What it sent before and holds there no more
    uint32_t drops = 0;
    const uint32_t drops_at = m.size;
    tide_write_u32(&m, 0u);
    for (uint32_t i = 0; i < sent->room; i++) {
        const uint64_t key = sent->keys[i];
        if (!key || key == UINT64_MAX || map_find(&now, key)) continue; // UINT64_MAX: the singletons
        const tide_entity e = entity_of(key);
        tide_write_u32(&m, e.index);
        tide_write_u32(&m, e.generation);
        drops++;
    }
    for (uint32_t i = drops_at + 4u; i < m.size; i += 8u) { // Forgotten as sent, now that they're dropped
        tide_reader e = {m.data + i, 8u, 0, false};
        tide_entity dropped;
        dropped.index = tide_read_u32(&e);
        dropped.generation = tide_read_u32(&e);
        map_remove(sent, key_of(dropped));
    }
    // The root's singletons, to every region, when they changed
    bool singletons = false;
    if (from == TIDE_SHARD_ROOT && s->api->singletons) {
        uint32_t size = 0;
        uint8_t *values = s->api->singletons(world, &size);
        const uint64_t hash = values ? tide_hash(values, size) : 0u;
        const uint64_t *had = map_find(sent, UINT64_MAX);
        if (values && (!had || *had != hash)) {
            tide_write_u8(&m, 1u);
            tide_write_bytes(&m, values, size);
            map_put(sent, UINT64_MAX, hash);
            singletons = true;
        }
        free(values);
    }
    if (puts || drops || singletons) {
        if (m.overflow) tide_out_of_memory();
        tide_shard_patch_u32(m.data + count_at, puts);
        tide_shard_patch_u32(m.data + drops_at, drops);
        if (!singletons) tide_write_u8(&m, 0u);
        if (m.overflow) tide_out_of_memory();
        inject(s, to, m.data, m.size);
    }
    free(m.data);
    map_free(&now);
    free_border(&b);
}

static bool near(const tide_shards *s, const shard *a, const shard *b)
{
    return tide_shard_box_near(&a->box, &b->box, s->api->range, s->api->dims);
}

// ---------------------------------------------------------------------------
// Splitting a region whose shard holds too much

static int compare_double(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static void maybe_split(tide_shards *s, const uint32_t id, const double now)
{
    shard *sh = &s->shards[id];
    tide_shard_status status;
    s->api->status(tide_server_world(sh->server), &status);
    if (!s->desc.most_entities || status.owned <= s->desc.most_entities) return;
    // Across its longest side where its entities are, at their middle, keeping
    // each half twice the range across
    tide_shard_box spread;
    if (!s->api->load(tide_server_world(sh->server), &spread)) return;
    uint32_t axis = 0;
    double widest = -1.0;
    for (uint32_t k = 0; k < s->api->dims; k++) {
        const double lo = sh->box.lo[k] > spread.lo[k] ? sh->box.lo[k] : spread.lo[k];
        const double hi = sh->box.hi[k] < spread.hi[k] ? sh->box.hi[k] : spread.hi[k];
        if (hi - lo > widest) {
            widest = hi - lo;
            axis = k;
        }
    }
    const uint32_t capacity = 4096u;
    double (*points)[3] = malloc(capacity * sizeof *points);
    double *values = malloc(capacity * sizeof *values);
    if (!points || !values) tide_out_of_memory();
    const uint32_t stride = status.owned / capacity + 1u;
    const uint32_t n = s->api->sample(tide_server_world(sh->server), stride, points, capacity);
    for (uint32_t i = 0; i < n; i++) values[i] = points[i][axis];
    qsort(values, n, sizeof *values, compare_double);
    const double cut = n ? values[n / 2u] : 0.0;
    free(points);
    free(values);
    const double floor = 2.0 * s->api->range;
    if (!(cut - sh->box.lo[axis] >= floor && sh->box.hi[axis] - cut >= floor)) return;
    const uint32_t added = new_shard(s);
    sh = &s->shards[id]; // The array may have moved
    shard *half = &s->shards[added];
    half->region = true;
    half->box = sh->box;
    half->box.lo[axis] = cut;
    sh->box.hi[axis] = cut;
    half->used = true;
    give_range(s, added);
    start_server(s, added, NULL, now);
    // Every shard learns the new regions, and what's in the new half moves
    // there as its shard's next tick ends
    tell_map(s);
}

// ---------------------------------------------------------------------------

void tide_shards_update(tide_shards *s, const double now)
{
    s->now = now;
    s->updates++;
    for (uint32_t i = 0; i < s->count; i++) {
        if (s->shards[i].used) tide_server_update(s->shards[i].server, now);
    }
    for (uint32_t i = 0; i < s->count; i++) {
        if (s->shards[i].used) carry(s, i);
    }
    // Ghosts: each region's to its neighbours, and the root's to every region
    for (uint32_t a = 0; a < s->count; a++) {
        if (!s->shards[a].used) continue;
        for (uint32_t b = 0; b < s->count; b++) {
            if (a == b || !s->shards[b].used || !s->shards[b].region) continue;
            if (s->shards[a].region && !near(s, &s->shards[a], &s->shards[b])) continue;
            send_ghosts(s, a, b);
        }
    }
    // More indices before a shard runs out, and splits
    for (uint32_t i = 0; i < s->count; i++) {
        if (!s->shards[i].used) continue;
        tide_shard_status status;
        s->api->status(tide_server_world(s->shards[i].server), &status);
        if (status.indices < RANGE_SIZE / 2u) {
            const range r = give_range(s, i);
            uint8_t data[24];
            tide_writer w = {data, sizeof data, 0, false, false};
            tide_shard_header(&w, TIDE_SHARD_RANGE, TIDE_SHARD_ORCHESTRATOR, 0u);
            tide_write_u32(&w, r.first);
            tide_write_u32(&w, r.end);
            inject(s, i, data, w.size);
        }
    }
    if (s->desc.most_entities && s->updates % SPLIT_EVERY == 0u) {
        const uint32_t count = s->count;
        for (uint32_t i = 0; i < count; i++) {
            if (s->shards[i].used && s->shards[i].region) maybe_split(s, i, now);
        }
    }
}

// ---------------------------------------------------------------------------
// Checkpoints: a header, the ranges and the directory, then each shard and its world

#define SAVE_MAGIC 0x53444854u // "THDS"
#define SAVE_VERSION 1u

uint8_t *tide_shards_save(const tide_shards *s, uint32_t *size)
{
    tide_writer w = {.grows = true};
    tide_write_u32(&w, SAVE_MAGIC);
    tide_write_u32(&w, SAVE_VERSION);
    tide_write_u64(&w, s->next_index);
    tide_write_u32(&w, s->range_count);
    for (uint32_t i = 0; i < s->range_count; i++) {
        tide_write_u32(&w, s->ranges[i].first);
        tide_write_u32(&w, s->ranges[i].end);
        tide_write_u32(&w, s->ranges[i].shard);
    }
    tide_write_u32(&w, s->directory.count);
    for (uint32_t i = 0; i < s->directory.room; i++) {
        if (!s->directory.keys[i]) continue;
        tide_write_u64(&w, s->directory.keys[i]);
        tide_write_u32(&w, (uint32_t)s->directory.values[i]);
    }
    tide_write_u32(&w, s->count);
    for (uint32_t i = 0; i < s->count; i++) {
        const shard *sh = &s->shards[i];
        tide_write_u8(&w, sh->used ? 1u : 0u);
        if (!sh->used) continue;
        tide_write_u8(&w, sh->region ? 1u : 0u);
        tide_write_bytes(&w, sh->box.lo, sizeof sh->box.lo);
        tide_write_bytes(&w, sh->box.hi, sizeof sh->box.hi);
        const void *world = tide_server_world(sh->server);
        const uint32_t bytes = s->game->pack_world(world, NULL, 0);
        tide_write_u32(&w, bytes);
        uint8_t *packed = malloc(bytes ? bytes : 1u);
        if (!packed) tide_out_of_memory();
        s->game->pack_world(world, packed, bytes);
        tide_write_bytes(&w, packed, bytes);
        free(packed);
    }
    if (w.overflow) tide_out_of_memory();
    *size = w.size;
    return w.data;
}

static void know_ghost(void *context, const tide_entity e, const int32_t source)
{
    void **args = context;
    tide_shards *s = args[0];
    const uint32_t to = *(const uint32_t *)args[1];
    if (source < 0 || (uint32_t)source >= s->count) return;
    map_put(sent_to(s, (uint32_t)source, to), key_of(e), 1u); // Sent, with bytes it doesn't know
}

tide_shards *tide_shards_load(const tide_shards_desc *desc, const uint8_t *data, const uint32_t size, const double now)
{
    tide_shards *s = calloc(1, sizeof *s);
    if (!s || !setup(s, desc)) {
        free(s);
        return NULL;
    }
    s->now = now;
    tide_reader r = {data, size, 0, false};
    if (tide_read_u32(&r) != SAVE_MAGIC || tide_read_u32(&r) != SAVE_VERSION) goto fail;
    s->next_index = tide_read_u64(&r);
    const uint32_t ranges = tide_read_u32(&r);
    if (r.failed || ranges > (r.size - r.at) / 12u) goto fail;
    s->ranges = calloc(ranges ? ranges : 1u, sizeof *s->ranges);
    if (!s->ranges) tide_out_of_memory();
    for (uint32_t i = 0; i < ranges; i++) {
        s->ranges[i].first = tide_read_u32(&r);
        s->ranges[i].end = tide_read_u32(&r);
        s->ranges[i].shard = tide_read_u32(&r);
    }
    s->range_count = ranges;
    const uint32_t moved = tide_read_u32(&r);
    if (r.failed || moved > (r.size - r.at) / 12u) goto fail;
    for (uint32_t i = 0; i < moved; i++) {
        const uint64_t key = tide_read_u64(&r);
        const uint32_t owner = tide_read_u32(&r);
        if (key) map_put(&s->directory, key, owner);
    }
    const uint32_t count = tide_read_u32(&r);
    if (r.failed || count > (r.size - r.at)) goto fail;
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t id = new_shard(s);
        shard *sh = &s->shards[id];
        if (!tide_read_u8(&r)) {
            sh->used = false;
            continue;
        }
        sh->region = tide_read_u8(&r) != 0;
        const uint8_t *lo = tide_read_bytes(&r, sizeof sh->box.lo);
        const uint8_t *hi = tide_read_bytes(&r, sizeof sh->box.hi);
        if (!lo || !hi) goto fail;
        memcpy(sh->box.lo, lo, sizeof sh->box.lo);
        memcpy(sh->box.hi, hi, sizeof sh->box.hi);
        // What was carried but not yet applied went with the servers: everything
        // still in an outbox goes again, and receivers drop what they had (by number)
        sh->delivered = 0;
        const uint32_t bytes = tide_read_u32(&r);
        const uint8_t *packed = tide_read_bytes(&r, bytes);
        if (!packed) goto fail;
        void *world = calloc(1, s->game->world_size);
        if (!world) tide_out_of_memory();
        if (!s->game->unpack_world(world, packed, bytes)) {
            if (s->game->free_world) s->game->free_world(world);
            free(world);
            goto fail;
        }
        start_server(s, id, world, now);
        if (s->game->free_world) s->game->free_world(world);
        free(world);
    }
    if (r.failed || r.at != r.size) goto fail;
    // What each shard was sent of its neighbours' ghosts is what it holds now
    for (uint32_t i = 0; i < s->count; i++) {
        if (!s->shards[i].used || !s->api->ghosts) continue;
        void *args[2] = {s, &i};
        s->api->ghosts(tide_server_world(s->shards[i].server), know_ghost, args);
    }
    // The regions again, in case a split's map was on its way when it was saved
    tell_map(s);
    return s;
fail:
    tide_shards_destroy(s);
    return NULL;
}
