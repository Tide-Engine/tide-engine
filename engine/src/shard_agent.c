#include "tide/shard_agent.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "shard_map.h"
#include "tide/net.h"

// See tide/shard_agent.h.

typedef struct target {
    uint32_t shard;
    bool region;
    tide_shard_box box;
} target;

struct tide_shard_agent {
    const tide_game *game;
    const tide_shard_game *api;
    tide_server *server;
    uint32_t shard;
    uint32_t ticks_before;
    uint32_t players;
    uint32_t applied;  // The last batch of instructions applied
    uint32_t reported; // Its world's messages reported, through this number
    // Where its ghosts go, and what it last sent each: by tree root, its bytes' hash
    // (UINT64_MAX: the root's singletons)
    target *targets;
    uint32_t target_count;
    map64 *sent; // By shard
    uint32_t sent_room;
    uint32_t sample_stride; // Asked to report where its entities are, every this many: 0 not
    bool fresh;             // Its first report says what it owns and holds
    uint8_t *checkpoint;    // Its world, packed, for the next report
    uint32_t checkpoint_size;
    uint32_t checkpoint_tick; // ...and the tick its journal goes on from
    float busy;               // How busy its server is, as its host last said
};

static map64 *sent_to(tide_shard_agent *a, const uint32_t to)
{
    if (to >= a->sent_room) {
        const uint32_t room = to + 8u;
        map64 *grown = realloc(a->sent, room * sizeof *grown);
        if (!grown) tide_out_of_memory();
        memset(grown + a->sent_room, 0, (room - a->sent_room) * sizeof *grown);
        a->sent = grown;
        a->sent_room = room;
    }
    return &a->sent[to];
}

tide_shard_agent *tide_shard_agent_create(const tide_game *game, tide_server *server, const uint32_t shard,
                                          const uint32_t ticks_before, const bool fresh)
{
    if (!game || !game->shards || !server) return NULL;
    tide_shard_agent *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->game = game;
    a->api = game->shards;
    a->server = server;
    a->shard = shard;
    a->ticks_before = ticks_before;
    a->players = tide_game_players(game);
    a->fresh = fresh;
    return a;
}

void tide_shard_agent_destroy(tide_shard_agent *a)
{
    if (!a) return;
    for (uint32_t i = 0; i < a->sent_room; i++) map_free(&a->sent[i]);
    free(a->sent);
    free(a->targets);
    free(a->checkpoint);
    free(a);
}

uint32_t tide_shard_agent_tick(const tide_shard_agent *a)
{
    return a->ticks_before + tide_server_tick(a->server);
}

bool tide_shard_agent_checkpointing(const tide_shard_agent *a)
{
    return a->checkpoint != NULL;
}

// ---------------------------------------------------------------------------
// Instructions

// An entity coming here from a region keeps a ghost of it there: what this
// shard sends that one of its ghosts starts from there
static void note_arrival(tide_shard_agent *a, const uint8_t *data, const uint32_t size)
{
    tide_reader r = {data, size, 0, false};
    if (tide_read_u8(&r) != TIDE_SHARD_ARRIVE) return;
    const uint32_t from = tide_read_u32(&r);
    tide_read_u32(&r);
    tide_read_u8(&r);
    tide_read_u32(&r);
    tide_read_u32(&r); // The tree's count
    tide_read_u32(&r); // Its root's record's size
    tide_entity root;
    root.index = tide_read_u32(&r);
    root.generation = tide_read_u32(&r);
    if (r.failed || from == a->shard || from == TIDE_SHARD_ORCHESTRATOR) return;
    map_put(sent_to(a, from), key_of(root), 1u); // Sent, with bytes it doesn't know
}

bool tide_shard_agent_apply(tide_shard_agent *a, const uint8_t *data, const uint32_t size)
{
    tide_reader r = {data, size, 0, false};
    if (tide_read_u32(&r) != TIDE_SHARD_INSTRUCTIONS) return false;
    const uint32_t batch = tide_read_u32(&r);
    const uint8_t flags = tide_read_u8(&r);
    // Read whole first: bytes that aren't instructions change nothing
    const uint32_t injects = tide_read_u32(&r);
    if (r.failed || injects > (r.size - r.at) / 4u) return false;
    const uint32_t injects_at = r.at;
    for (uint32_t i = 0; i < injects && !r.failed; i++) tide_read_bytes(&r, tide_read_u32(&r));
    const uint32_t admits = tide_read_u32(&r);
    if (r.failed || admits > (r.size - r.at) / 12u) return false;
    const uint32_t admits_at = r.at;
    for (uint32_t i = 0; i < admits; i++) {
        tide_read_u32(&r);
        tide_read_u64(&r);
    }
    uint32_t targets = 0, targets_at = 0;
    if (flags & TIDE_SHARD_DO_TARGETS) {
        targets = tide_read_u32(&r);
        if (r.failed || targets > (r.size - r.at) / 53u) return false;
        targets_at = r.at;
        for (uint32_t i = 0; i < targets; i++) tide_read_bytes(&r, 53u);
    }
    const uint32_t stride = flags & TIDE_SHARD_DO_SAMPLE ? tide_read_u32(&r) : 0u;
    uint32_t held = 0, held_at = 0;
    if (flags & TIDE_SHARD_DO_HELD) {
        held = tide_read_u32(&r);
        if (r.failed || held > (r.size - r.at) / 12u) return false;
        held_at = r.at;
        for (uint32_t i = 0; i < held; i++) {
            tide_read_u32(&r);
            tide_read_u64(&r);
        }
    }
    if (r.failed || r.at != r.size) return false;

    a->applied = batch;
    if (flags & TIDE_SHARD_DO_FROM_START) a->reported = 0;
    // Its checkpoint is of the world as these instructions find it: everything
    // it was handed before applied, and nothing of these yet
    if (flags & TIDE_SHARD_DO_CHECKPOINT) {
        const void *world = tide_server_world(a->server);
        const uint32_t n = a->game->pack_world(world, NULL, 0);
        free(a->checkpoint);
        a->checkpoint = malloc(n ? n : 1u);
        if (!a->checkpoint) tide_out_of_memory();
        a->game->pack_world(world, a->checkpoint, n);
        a->checkpoint_size = n;
        a->checkpoint_tick = tide_shard_agent_tick(a);
    }
    r.at = injects_at;
    for (uint32_t i = 0; i < injects; i++) {
        const uint32_t n = tide_read_u32(&r);
        const uint8_t *message = tide_read_bytes(&r, n);
        note_arrival(a, message, n);
        if (!tide_server_inject(a->server, message, n)) tide_out_of_memory();
    }
    r.at = admits_at;
    for (uint32_t i = 0; i < admits; i++) {
        const tide_player_id player = {tide_read_u32(&r)};
        const uint64_t cookie = tide_read_u64(&r);
        (void)tide_server_admit(a->server, player, cookie);
    }
    if (flags & TIDE_SHARD_DO_TARGETS) {
        target *kept = calloc(targets ? targets : 1u, sizeof *kept);
        if (!kept) tide_out_of_memory();
        r.at = targets_at;
        for (uint32_t i = 0; i < targets; i++) {
            kept[i].shard = tide_read_u32(&r);
            kept[i].region = tide_read_u8(&r) != 0;
            memcpy(kept[i].box.lo, tide_read_bytes(&r, 24u), 24u);
            memcpy(kept[i].box.hi, tide_read_bytes(&r, 24u), 24u);
        }
        // What it sent a shard that's no target any more is forgotten: that one
        // dropped them. What it sent the others goes again whole, as their
        // copies may be older than it knows (after a shard started again), and
        // what they hold that's gone from near them is still dropped.
        for (uint32_t s = 0; s < a->sent_room; s++) {
            bool still = false;
            for (uint32_t i = 0; i < targets; i++) still |= kept[i].shard == s;
            if (!still) {
                map_free(&a->sent[s]);
                continue;
            }
            for (uint32_t k = 0; k < a->sent[s].room; k++) {
                if (a->sent[s].keys[k]) a->sent[s].values[k] = 1u; // Sent, with bytes it doesn't know
            }
        }
        free(a->targets);
        a->targets = kept;
        a->target_count = targets;
    }
    if (flags & TIDE_SHARD_DO_SAMPLE) a->sample_stride = stride ? stride : 1u;
    r.at = held_at;
    for (uint32_t i = 0; i < held; i++) {
        const uint32_t holder = tide_read_u32(&r);
        const uint64_t key = tide_read_u64(&r);
        if (holder != a->shard) map_put(sent_to(a, holder), key, 1u);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Ghosts: what it owns near a target's region, against what it last sent

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
    b->hashes[b->count] = hash > 1u ? hash : 2u; // 0 is no tree, and 1 one whose bytes aren't known
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

// The GHOSTS message for `t`, written into `out` (u32 its shard, u32 size,
// the message), when anything changed: whether it was
static bool ghosts_for(tide_shard_agent *a, const target *t, tide_writer *out)
{
    const void *world = tide_server_world(a->server);
    tide_shard_box near = t->box;
    for (uint32_t k = 0; k < 3u; k++) {
        near.lo[k] -= a->api->range;
        near.hi[k] += a->api->range;
    }
    border b = {.bytes = {.grows = true}};
    a->api->border(world, &near, take_border, &b);
    map64 *sent = sent_to(a, t->shard);
    tide_writer m = {.grows = true};
    tide_shard_header(&m, TIDE_SHARD_GHOSTS, a->shard, 0u);
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
    if (a->shard == TIDE_SHARD_ROOT && a->api->singletons) {
        uint32_t size = 0;
        uint8_t *values = a->api->singletons(world, &size);
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
    const bool any = puts || drops || singletons;
    if (any) {
        if (m.overflow) tide_out_of_memory();
        tide_shard_patch_u32(m.data + count_at, puts);
        tide_shard_patch_u32(m.data + drops_at, drops);
        if (!singletons) tide_write_u8(&m, 0u);
        if (m.overflow) tide_out_of_memory();
        tide_write_u32(out, t->shard);
        tide_write_u32(out, m.size);
        tide_write_bytes(out, m.data, m.size);
    }
    free(m.data);
    map_free(&now);
    free_border(&b);
    return any;
}

// ---------------------------------------------------------------------------
// The report

typedef struct listed {
    tide_writer *out;
    uint32_t count;
} listed;

static void list_owned(void *context, const tide_entity e)
{
    listed *l = context;
    tide_write_u64(l->out, key_of(e));
    l->count++;
}

static void list_ghost(void *context, const tide_entity e, const int32_t source)
{
    listed *l = context;
    tide_write_u64(l->out, key_of(e));
    tide_write_u32(l->out, (uint32_t)source);
    l->count++;
}

void tide_shard_agent_rebuilt(tide_shard_agent *a, const tide_game *game)
{
    if (!a || !game || !game->shards) return;
    a->game = game;
    a->api = game->shards;
    // What it sent each target, it sends again whole: its bytes may be laid out
    // anew, and the ghosts on their way were dropped
    for (uint32_t s = 0; s < a->sent_room; s++) {
        for (uint32_t k = 0; k < a->sent[s].room; k++) {
            if (a->sent[s].keys[k]) a->sent[s].values[k] = 1u; // Sent, with bytes it doesn't know
        }
    }
}

void tide_shard_agent_busy(tide_shard_agent *a, const float busy)
{
    if (a) a->busy = isfinite(busy) && busy > 0.0f ? busy : 0.0f;
}

uint8_t *tide_shard_agent_report(tide_shard_agent *a, uint32_t *size)
{
    const void *world = tide_server_world(a->server);
    tide_writer w = {.grows = true};
    tide_write_u32(&w, TIDE_SHARD_REPORT);
    tide_write_u32(&w, a->shard);
    tide_write_u32(&w, a->applied);
    tide_write_u32(&w, tide_shard_agent_tick(a));
    tide_write_u32(&w, tide_server_injected(a->server));
    uint32_t busy;
    memcpy(&busy, &a->busy, sizeof busy);
    tide_write_u32(&w, busy);
    tide_shard_status status;
    a->api->status(world, &status);
    tide_write_u32(&w, (uint32_t)status.id);
    tide_write_u32(&w, status.role);
    tide_write_u32(&w, status.owned);
    tide_write_u32(&w, status.ghosts);
    tide_write_u32(&w, status.waiting);
    tide_write_u32(&w, status.outbox);
    tide_write_u32(&w, status.lent);
    tide_write_u64(&w, status.indices);
    tide_write_u64(&w, status.ranges_end);
    // The players connected here
    uint32_t at = w.size, count = 0;
    tide_write_u32(&w, 0u);
    for (uint32_t p = 0; p < a->players; p++) {
        const tide_player_id id = tide_player_from_index((int32_t)p);
        if (!tide_server_connected(a->server, id)) continue;
        tide_write_u32(&w, id.id);
        tide_write_u64(&w, tide_server_cookie(a->server, id));
        count++;
    }
    if (!w.overflow) tide_shard_patch_u32(w.data + at, count);
    // Where each player's entities are
    at = w.size;
    count = 0;
    tide_write_u32(&w, 0u);
    if (a->api->presence && a->players) {
        tide_shard_presence *found = malloc(a->players * sizeof *found);
        if (!found) tide_out_of_memory();
        const uint32_t n = a->api->presence(world, found, a->players);
        for (uint32_t k = 0; k < n && k < a->players; k++) {
            tide_write_u32(&w, found[k].player.id);
            tide_write_u32(&w, found[k].count);
            tide_write_bytes(&w, found[k].box.lo, 24u);
            tide_write_bytes(&w, found[k].box.hi, 24u);
            count++;
        }
        free(found);
    }
    if (!w.overflow) tide_shard_patch_u32(w.data + at, count);
    // What its world sent since the last report
    at = w.size;
    count = 0;
    tide_write_u32(&w, 0u);
    uint32_t outbox_size = 0;
    uint8_t *outbox = a->api->outbox(world, &outbox_size);
    tide_reader r = {outbox, outbox_size, 0, false};
    tide_shard_outgoing m;
    uint32_t through = a->reported;
    while (outbox && tide_shard_next_outgoing(&r, &m)) {
        if (!m.number || m.number <= a->reported) continue;
        through = m.number > through ? m.number : through;
        tide_write_u32(&w, m.route);
        tide_write_u32(&w, m.target);
        tide_write_u32(&w, m.entity.index);
        tide_write_u32(&w, m.entity.generation);
        tide_write_u32(&w, m.number);
        tide_write_u32(&w, m.size);
        tide_write_bytes(&w, m.data, m.size);
        count++;
    }
    free(outbox);
    a->reported = through;
    if (!w.overflow) tide_shard_patch_u32(w.data + at, count);
    // Its ghosts for its neighbours
    at = w.size;
    count = 0;
    tide_write_u32(&w, 0u);
    for (uint32_t i = 0; i < a->target_count; i++) count += ghosts_for(a, &a->targets[i], &w) ? 1u : 0u;
    if (!w.overflow) tide_shard_patch_u32(w.data + at, count);
    // Where its entities are, when asked
    tide_write_u8(&w, a->sample_stride ? 1u : 0u);
    if (a->sample_stride) {
        tide_shard_box spread = {{0}, {0}};
        a->api->load(world, &spread);
        tide_write_bytes(&w, spread.lo, 24u);
        tide_write_bytes(&w, spread.hi, 24u);
        const uint32_t capacity = 4096u;
        double (*points)[3] = malloc(capacity * sizeof *points);
        if (!points) tide_out_of_memory();
        const uint32_t n = a->api->sample(world, a->sample_stride, points, capacity);
        tide_write_u32(&w, n);
        for (uint32_t i = 0; i < n; i++) tide_write_bytes(&w, points[i], sizeof points[i]);
        free(points);
        a->sample_stride = 0;
    }
    // What it owns and holds, once it went on from a checkpoint
    tide_write_u8(&w, a->fresh && a->api->owned ? 1u : 0u);
    if (a->fresh && a->api->owned) {
        at = w.size;
        tide_write_u32(&w, 0u);
        listed l = {&w, 0u};
        a->api->owned(world, list_owned, &l);
        if (!w.overflow) tide_shard_patch_u32(w.data + at, l.count);
    }
    tide_write_u8(&w, a->fresh && a->api->ghosts ? 1u : 0u);
    if (a->fresh && a->api->ghosts) {
        at = w.size;
        tide_write_u32(&w, 0u);
        listed l = {&w, 0u};
        a->api->ghosts(world, list_ghost, &l);
        if (!w.overflow) tide_shard_patch_u32(w.data + at, l.count);
    }
    a->fresh = false;
    tide_write_u8(&w, a->checkpoint ? 1u : 0u);
    if (a->checkpoint) {
        tide_write_u32(&w, a->checkpoint_tick);
        tide_write_u32(&w, a->checkpoint_size);
        tide_write_bytes(&w, a->checkpoint, a->checkpoint_size);
        free(a->checkpoint);
        a->checkpoint = NULL;
    }
    if (w.overflow) tide_out_of_memory();
    *size = w.size;
    return w.data;
}
