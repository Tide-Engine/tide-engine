#include "tide/shard_coordinator.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shard_map.h"
#include "tide/shard_agent.h"

// See tide/shard_coordinator.h. It keeps, outside every world: which shard
// owns each entity that moved (the directory; one that never moved is the
// shard's whose range its index is in), which messages of each shard it
// carried and acknowledged, the players of the world, and the regions.

#define RANGE_SIZE (1u << 20)    // Entity indices a shard is given at a time
#define SPLIT_EVERY 30u          // Updates between looks at whether a region should split or merge
#define PLAYER_AWAY_SECONDS 20.0 // How long a player of the world on no shard has to come back before PlayerLeft
#define SAVE_MAGIC 0x53444854u   // "THDS"
#define SAVE_VERSION 4u

typedef struct sighting {
    uint32_t player, shard, count;
    tide_shard_box box;
} sighting;

typedef struct cshard {
    bool used;
    bool region; // The root has none, nor a retiring shard
    tide_shard_box box;
    // A region merged into another's (`into`): it gives up what it owns, then
    // ends, and its id is never another's (receivers know its messages' numbers)
    bool retiring, gone;
    uint32_t into;
    uint64_t match; // What its host knows it by, for players' routes

    // What its last report said
    bool heard;        // Any report yet
    uint32_t applied;  // The last batch it applied
    uint32_t tick;
    uint32_t injected; // Messages handed it and not yet applied
    tide_shard_status status;
    uint32_t *connected; // PlayerIDs...
    uint64_t *cookies;   // ...and their cookies
    uint32_t connected_count, connected_room;
    sighting *presence;
    uint32_t presence_count, presence_room;
    tide_writer outbox; // Its messages reported and not yet carried (outbox records)
    tide_writer ghosts; // ...and its ghost messages (u32 to, u32 size, bytes)
    bool sampled;       // Where its entities are, for a split
    tide_shard_box spread;
    double (*points)[3];
    uint32_t point_count;
    uint8_t *world; // Its checkpoint, while one's gathered
    uint32_t world_size, world_tick;

    uint32_t delivered; // Its messages carried, through this number...
    uint32_t acked;     // ...and acknowledged

    // Its next instructions
    uint32_t batch; // The last it was given
    uint8_t flags;
    tide_writer injects;
    uint32_t inject_count;
    tide_writer admits;
    uint32_t admit_count;
    tide_writer held;
    uint32_t held_count;
    uint32_t sample_stride;
    bool sample_asked;
} cshard;

typedef struct roster {
    uint64_t cookie;
    bool here;
    bool away; // ...though on no shard since `away_since`: they've a while to come back
    double away_since;
    uint32_t home;
    uint64_t route; // The route the shards were last told (its hash)
} roster;

typedef struct range {
    uint32_t first, end, shard;
} range;

typedef enum checkpoint_state { CHECKPOINT_IDLE, CHECKPOINT_GATHERING, CHECKPOINT_KEEPING } checkpoint_state;

struct tide_shard_coordinator {
    tide_shard_coordinator_desc desc;
    double now;
    uint64_t updates;
    cshard *shards;
    uint32_t count, room;
    map64 directory; // Entity key to the shard it last went to
    range *ranges;
    uint32_t range_count;
    uint64_t next_index; // The next range's first
    roster *players;
    uint32_t player_count;
    // The regions as they are (each change, a version), and each region's
    // neighbours as of `neighbours_of`
    uint64_t map_version, neighbours_of;
    uint32_t *neighbour_first, *neighbours;
    // Checkpoints: when the last was made; whether the shape changed since;
    // a change waiting for one to be kept before it's told (the map, and
    // messages: u32 shard, u32 size, bytes)
    checkpoint_state checkpoint;
    tide_writer head; // Its own part of the checkpoint being gathered, as it was when it began
    double checkpointed;
    bool changed;
    bool tell_after;
    tide_writer told_after;
    uint64_t reshaped; // The update it last looked at splitting and merging
    // Its actions since the last step, and the bytes they point into
    tide_shard_action *actions;
    uint32_t action_count, action_room;
    uint8_t **action_bytes;
    uint32_t action_bytes_count, action_bytes_room;
};

// ---------------------------------------------------------------------------
// Instructions

static void inject(tide_shard_coordinator *c, const uint32_t to, const uint8_t *data, const uint32_t size)
{
    if (to >= c->count || !c->shards[to].used) return;
    cshard *sh = &c->shards[to];
    sh->injects.grows = true;
    tide_write_u32(&sh->injects, size);
    tide_write_bytes(&sh->injects, data, size);
    if (sh->injects.overflow) tide_out_of_memory();
    sh->inject_count++;
}

static void admit(tide_shard_coordinator *c, const uint32_t to, const uint32_t player, const uint64_t cookie)
{
    cshard *sh = &c->shards[to];
    sh->admits.grows = true;
    tide_write_u32(&sh->admits, tide_player_from_index((int32_t)player).id);
    tide_write_u64(&sh->admits, cookie);
    sh->admit_count++;
}

static void admit_everywhere(tide_shard_coordinator *c, const uint32_t player)
{
    for (uint32_t i = 0; i < c->count; i++) {
        if (c->shards[i].used) admit(c, i, player, c->players[player].cookie);
    }
}

static bool near(const tide_shard_coordinator *c, const cshard *a, const cshard *b)
{
    return tide_shard_box_near(&a->box, &b->box, c->desc.range, c->desc.dims);
}

// Where each shard's ghosts go: a region's to its neighbours, the root's (and
// a retiring shard's) to every region
static void write_targets(const tide_shard_coordinator *c, const uint32_t from, tide_writer *w)
{
    const cshard *a = &c->shards[from];
    const uint32_t at = w->size;
    tide_write_u32(w, 0u);
    uint32_t count = 0;
    for (uint32_t b = 0; b < c->count; b++) {
        const cshard *t = &c->shards[b];
        if (b == from || !t->used || !t->region || (a->region && !near(c, a, t))) continue;
        tide_write_u32(w, b);
        tide_write_u8(w, 1u);
        tide_write_bytes(w, t->box.lo, sizeof t->box.lo);
        tide_write_bytes(w, t->box.hi, sizeof t->box.hi);
        count++;
    }
    if (!w->overflow) tide_shard_patch_u32(w->data + at, count);
}

static void retarget(tide_shard_coordinator *c)
{
    for (uint32_t i = 0; i < c->count; i++) {
        if (c->shards[i].used) c->shards[i].flags |= TIDE_SHARD_DO_TARGETS;
    }
}

uint8_t *tide_shard_coordinator_instructions(tide_shard_coordinator *c, const uint32_t shard, uint32_t *size)
{
    *size = 0;
    if (!c || shard >= c->count || !c->shards[shard].used) return NULL;
    cshard *sh = &c->shards[shard];
    if (!sh->inject_count && !sh->admit_count && !sh->flags && !sh->held_count) return NULL;
    uint8_t flags = sh->flags;
    if (sh->held_count) flags |= TIDE_SHARD_DO_HELD;
    if (sh->sample_stride) flags |= TIDE_SHARD_DO_SAMPLE;
    tide_writer w = {.grows = true};
    tide_write_u32(&w, TIDE_SHARD_INSTRUCTIONS);
    tide_write_u32(&w, ++sh->batch);
    tide_write_u8(&w, flags);
    tide_write_u32(&w, sh->inject_count);
    tide_write_bytes(&w, sh->injects.data, sh->injects.size);
    tide_write_u32(&w, sh->admit_count);
    tide_write_bytes(&w, sh->admits.data, sh->admits.size);
    if (flags & TIDE_SHARD_DO_TARGETS) write_targets(c, shard, &w);
    if (flags & TIDE_SHARD_DO_SAMPLE) tide_write_u32(&w, sh->sample_stride);
    if (flags & TIDE_SHARD_DO_HELD) {
        tide_write_u32(&w, sh->held_count);
        tide_write_bytes(&w, sh->held.data, sh->held.size);
    }
    if (w.overflow) tide_out_of_memory();
    sh->injects.size = sh->admits.size = sh->held.size = 0;
    sh->inject_count = sh->admit_count = sh->held_count = 0;
    sh->flags = 0;
    sh->sample_stride = 0;
    *size = w.size;
    return w.data;
}

// ---------------------------------------------------------------------------
// The world's shape

static void write_regions(const tide_shard_coordinator *c, tide_writer *w)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < c->count; i++) count += c->shards[i].used && c->shards[i].region;
    tide_write_u32(w, count);
    for (uint32_t i = 0; i < c->count; i++) {
        const cshard *sh = &c->shards[i];
        if (!sh->used || !sh->region) continue;
        tide_write_u32(w, i);
        tide_write_bytes(w, sh->box.lo, sizeof sh->box.lo);
        tide_write_bytes(w, sh->box.hi, sizeof sh->box.hi);
    }
}

static range give_range(tide_shard_coordinator *c, const uint32_t shard)
{
    if (c->next_index + RANGE_SIZE > UINT32_MAX) {
        fprintf(stderr, "tide: the sharded world has given every entity index it has\n");
        abort();
    }
    const range r = {(uint32_t)c->next_index, (uint32_t)(c->next_index + RANGE_SIZE), shard};
    c->next_index += RANGE_SIZE;
    range *grown = realloc(c->ranges, (c->range_count + 1u) * sizeof *grown);
    if (!grown) tide_out_of_memory();
    c->ranges = grown;
    c->ranges[c->range_count++] = r;
    return r;
}

static void tell_range(tide_shard_coordinator *c, const range r)
{
    uint8_t data[24];
    tide_writer w = {data, sizeof data, 0, false, false};
    tide_shard_header(&w, TIDE_SHARD_RANGE, TIDE_SHARD_ORCHESTRATOR, 0u);
    tide_write_u32(&w, r.first);
    tide_write_u32(&w, r.end);
    inject(c, r.shard, data, w.size);
}

// A CONFIG message: who the shard is, the regions, and its ranges
static uint8_t *config_for(const tide_shard_coordinator *c, const uint32_t id, uint32_t *size)
{
    tide_writer w = {.grows = true};
    tide_shard_header(&w, TIDE_SHARD_CONFIG, TIDE_SHARD_ORCHESTRATOR, 0u);
    tide_write_u32(&w, id);
    tide_write_u8(&w, id == TIDE_SHARD_ROOT ? TIDE_SHARD_ROOT_ROLE : TIDE_SHARD_REGION);
    write_regions(c, &w);
    uint32_t count = 0;
    for (uint32_t i = 0; i < c->range_count; i++) count += c->ranges[i].shard == id;
    tide_write_u32(&w, count);
    for (uint32_t i = 0; i < c->range_count; i++) {
        if (c->ranges[i].shard != id) continue;
        tide_write_u32(&w, c->ranges[i].first);
        tide_write_u32(&w, c->ranges[i].end);
    }
    if (w.overflow) tide_out_of_memory();
    *size = w.size;
    return w.data;
}

static uint32_t new_shard(tide_shard_coordinator *c)
{
    if (c->count == c->room) {
        const uint32_t room = c->room ? c->room * 2u : 8u;
        cshard *grown = realloc(c->shards, room * sizeof *grown);
        if (!grown) tide_out_of_memory();
        memset(grown + c->room, 0, (room - c->room) * sizeof *grown);
        c->shards = grown;
        c->room = room;
    }
    return c->count++;
}

static void free_shard(cshard *sh)
{
    free(sh->connected);
    free(sh->cookies);
    free(sh->presence);
    free(sh->outbox.data);
    free(sh->ghosts.data);
    free(sh->points);
    free(sh->world);
    free(sh->injects.data);
    free(sh->admits.data);
    free(sh->held.data);
}

// ---------------------------------------------------------------------------
// Actions

static uint8_t *keep_bytes(tide_shard_coordinator *c, uint8_t *bytes)
{
    if (c->action_bytes_count == c->action_bytes_room) {
        c->action_bytes_room = c->action_bytes_room ? c->action_bytes_room * 2u : 16u;
        c->action_bytes = realloc(c->action_bytes, c->action_bytes_room * sizeof *c->action_bytes);
        if (!c->action_bytes) tide_out_of_memory();
    }
    c->action_bytes[c->action_bytes_count++] = bytes;
    return bytes;
}

static tide_shard_action *act(tide_shard_coordinator *c, const tide_shard_action_kind kind, const uint32_t shard)
{
    if (c->action_count == c->action_room) {
        c->action_room = c->action_room ? c->action_room * 2u : 16u;
        c->actions = realloc(c->actions, c->action_room * sizeof *c->actions);
        if (!c->actions) tide_out_of_memory();
    }
    tide_shard_action *a = &c->actions[c->action_count++];
    *a = (tide_shard_action){.kind = kind, .shard = shard};
    return a;
}

static void clear_actions(tide_shard_coordinator *c)
{
    for (uint32_t i = 0; i < c->action_bytes_count; i++) free(c->action_bytes[i]);
    c->action_bytes_count = 0;
    c->action_count = 0;
}

uint32_t tide_shard_coordinator_actions(const tide_shard_coordinator *c, const tide_shard_action **out)
{
    *out = c ? c->actions : NULL;
    return c ? c->action_count : 0u;
}

// A shard started anew from its CONFIG, at `tick`
static void start_anew(tide_shard_coordinator *c, const uint32_t id, const uint32_t tick)
{
    uint32_t size;
    uint8_t *config = keep_bytes(c, config_for(c, id, &size));
    tide_shard_action *a = act(c, TIDE_SHARD_ACTION_START, id);
    a->config = config;
    a->config_size = size;
    a->tick = tick;
    c->shards[id].used = true;
    c->shards[id].flags |= TIDE_SHARD_DO_TARGETS;
}

// The regions, told every shard: at once, or once a checkpoint of them is
// kept, when checkpoints are made, so no record depends on a shape none has
static void tell_map(tide_shard_coordinator *c)
{
    c->map_version++;
    c->changed = true;
    tide_writer w = {.grows = true};
    tide_shard_header(&w, TIDE_SHARD_MAP, TIDE_SHARD_ORCHESTRATOR, 0u);
    write_regions(c, &w);
    if (w.overflow) tide_out_of_memory();
    for (uint32_t i = 0; i < c->count; i++) inject(c, i, w.data, w.size);
    free(w.data);
    retarget(c);
}

static void reshaped(tide_shard_coordinator *c)
{
    c->changed = true;
    if (c->desc.checkpoints) c->tell_after = true;
    else tell_map(c);
}

// A message told once the change it's part of is in a kept checkpoint
static void tell_after(tide_shard_coordinator *c, const uint32_t to, const uint8_t *data, const uint32_t size)
{
    if (!c->desc.checkpoints) {
        inject(c, to, data, size);
        return;
    }
    c->told_after.grows = true;
    tide_write_u32(&c->told_after, to);
    tide_write_u32(&c->told_after, size);
    tide_write_bytes(&c->told_after, data, size);
}

// ---------------------------------------------------------------------------
// Creating, loading

static bool setup(tide_shard_coordinator *c, const tide_shard_coordinator_desc *desc, const double now)
{
    if (!desc || !desc->dims || desc->dims > 3u || !(desc->range > 0.0)) return false;
    c->desc = *desc;
    c->now = now;
    c->checkpointed = now;
    c->player_count = desc->players;
    c->players = calloc(c->player_count ? c->player_count : 1u, sizeof *c->players);
    if (!c->players) tide_out_of_memory();
    return true;
}

tide_shard_coordinator *tide_shard_coordinator_create(const tide_shard_coordinator_desc *desc, const double now)
{
    tide_shard_coordinator *c = calloc(1, sizeof *c);
    if (!c || !setup(c, desc, now)) {
        free(c);
        return NULL;
    }
    // The root, then a region for each cell of the split world
    const uint32_t root = new_shard(c);
    give_range(c, root);
    uint32_t n[3];
    for (uint32_t k = 0; k < 3u; k++) n[k] = desc->split[k] ? desc->split[k] : 1u;
    if (desc->dims == 2u) n[2] = 1u;
    for (uint32_t z = 0; z < n[2]; z++) {
        for (uint32_t y = 0; y < n[1]; y++) {
            for (uint32_t x = 0; x < n[0]; x++) {
                const uint32_t id = new_shard(c);
                cshard *sh = &c->shards[id];
                sh->region = true;
                const uint32_t cell[3] = {x, y, z};
                for (uint32_t k = 0; k < 3u; k++) {
                    const double lo = desc->world.lo[k], hi = desc->world.hi[k];
                    const double step = (hi - lo) / (double)n[k];
                    // The outer edges reach past the world, so what goes outside it stays in a region
                    sh->box.lo[k] = cell[k] == 0 ? -INFINITY : lo + step * (double)cell[k];
                    sh->box.hi[k] = cell[k] == n[k] - 1u ? INFINITY : lo + step * (double)(cell[k] + 1u);
                    if (k == 2 && desc->dims == 2u) {
                        sh->box.lo[k] = -INFINITY;
                        sh->box.hi[k] = INFINITY;
                    }
                }
                sh->used = true; // So the regions written into configs have it
                give_range(c, id);
            }
        }
    }
    for (uint32_t i = 0; i < c->count; i++) start_anew(c, i, 0u);
    c->map_version = 1;
    c->changed = true; // A first checkpoint, of the world as it starts
    return c;
}

void tide_shard_coordinator_destroy(tide_shard_coordinator *c)
{
    if (!c) return;
    for (uint32_t i = 0; i < c->count; i++) free_shard(&c->shards[i]);
    free(c->shards);
    free(c->ranges);
    free(c->players);
    free(c->neighbour_first);
    free(c->neighbours);
    free(c->told_after.data);
    free(c->head.data);
    clear_actions(c);
    free(c->actions);
    free(c->action_bytes);
    map_free(&c->directory);
    free(c);
}

// ---------------------------------------------------------------------------
// Reports

static bool read_box(tide_reader *r, tide_shard_box *box)
{
    const uint8_t *lo = tide_read_bytes(r, 24u), *hi = tide_read_bytes(r, 24u);
    if (!lo || !hi) return false;
    memcpy(box->lo, lo, 24u);
    memcpy(box->hi, hi, 24u);
    return true;
}

// Where an entity a shard owns is, from its report on going on from a checkpoint
static void know_owned(tide_shard_coordinator *c, const uint32_t shard, const tide_entity e)
{
    int32_t home = -1;
    for (uint32_t i = 0; i < c->range_count; i++) {
        if (e.index >= c->ranges[i].first && e.index < c->ranges[i].end) home = (int32_t)c->ranges[i].shard;
    }
    if (home == (int32_t)shard) map_remove(&c->directory, key_of(e));
    else map_put(&c->directory, key_of(e), shard);
}

bool tide_shard_coordinator_report(tide_shard_coordinator *c, const uint8_t *report, const uint32_t size)
{
    if (!c || !report) return false;
    tide_reader r = {report, size, 0, false};
    if (tide_read_u32(&r) != TIDE_SHARD_REPORT) return false;
    const uint32_t id = tide_read_u32(&r);
    if (r.failed || id >= c->count || !c->shards[id].used) return false;
    cshard *sh = &c->shards[id];
    const uint32_t applied = tide_read_u32(&r);
    const uint32_t tick = tide_read_u32(&r);
    const uint32_t injected = tide_read_u32(&r);
    tide_shard_status status;
    status.id = (int32_t)tide_read_u32(&r);
    status.role = tide_read_u32(&r);
    status.owned = tide_read_u32(&r);
    status.ghosts = tide_read_u32(&r);
    status.waiting = tide_read_u32(&r);
    status.outbox = tide_read_u32(&r);
    status.lent = tide_read_u32(&r);
    status.indices = tide_read_u64(&r);
    status.ranges_end = tide_read_u64(&r);
    const uint32_t connected = tide_read_u32(&r);
    if (r.failed || connected > (r.size - r.at) / 12u) return false;
    if (connected > sh->connected_room) {
        sh->connected_room = connected;
        sh->connected = realloc(sh->connected, connected * sizeof *sh->connected);
        sh->cookies = realloc(sh->cookies, connected * sizeof *sh->cookies);
        if (!sh->connected || !sh->cookies) tide_out_of_memory();
    }
    for (uint32_t i = 0; i < connected; i++) {
        sh->connected[i] = tide_read_u32(&r);
        sh->cookies[i] = tide_read_u64(&r);
    }
    sh->connected_count = connected;
    const uint32_t present = tide_read_u32(&r);
    if (r.failed || present > (r.size - r.at) / 56u) return false;
    if (present > sh->presence_room) {
        sh->presence_room = present;
        sh->presence = realloc(sh->presence, present * sizeof *sh->presence);
        if (!sh->presence) tide_out_of_memory();
    }
    for (uint32_t i = 0; i < present; i++) {
        const tide_player_id player = {tide_read_u32(&r)};
        const int32_t p = tide_player_index(player);
        sh->presence[i].player = p < 0 ? UINT32_MAX : (uint32_t)p;
        sh->presence[i].shard = id;
        sh->presence[i].count = tide_read_u32(&r);
        if (!read_box(&r, &sh->presence[i].box)) return false;
    }
    sh->presence_count = present;
    // Its messages and ghosts, kept until the step carries them
    const uint32_t messages = tide_read_u32(&r);
    if (r.failed || messages > (r.size - r.at) / 24u) return false;
    for (uint32_t i = 0; i < messages && !r.failed; i++) {
        const uint32_t from = r.at;
        for (uint32_t k = 0; k < 5u; k++) tide_read_u32(&r);
        tide_read_bytes(&r, tide_read_u32(&r));
        if (r.failed) return false;
        sh->outbox.grows = true;
        tide_write_bytes(&sh->outbox, report + from, r.at - from);
    }
    const uint32_t ghosts = tide_read_u32(&r);
    if (r.failed || ghosts > (r.size - r.at) / 8u) return false;
    for (uint32_t i = 0; i < ghosts && !r.failed; i++) {
        const uint32_t from = r.at;
        tide_read_u32(&r);
        tide_read_bytes(&r, tide_read_u32(&r));
        if (r.failed) return false;
        sh->ghosts.grows = true;
        tide_write_bytes(&sh->ghosts, report + from, r.at - from);
    }
    if (tide_read_u8(&r)) { // Where its entities are
        if (!read_box(&r, &sh->spread)) return false;
        const uint32_t n = tide_read_u32(&r);
        if (r.failed || n > (r.size - r.at) / 24u) return false;
        free(sh->points);
        sh->points = malloc((n ? n : 1u) * sizeof *sh->points);
        if (!sh->points) tide_out_of_memory();
        for (uint32_t i = 0; i < n; i++) memcpy(sh->points[i], tide_read_bytes(&r, 24u), 24u);
        sh->point_count = n;
        sh->sampled = true;
        sh->sample_asked = false;
    }
    if (tide_read_u8(&r)) { // What it owns, having gone on from a checkpoint
        const uint32_t n = tide_read_u32(&r);
        if (r.failed || n > (r.size - r.at) / 8u) return false;
        for (uint32_t i = 0; i < n; i++) know_owned(c, id, entity_of(tide_read_u64(&r)));
    }
    if (tide_read_u8(&r)) { // ...and the ghosts it holds: their shards know it holds them
        const uint32_t n = tide_read_u32(&r);
        if (r.failed || n > (r.size - r.at) / 12u) return false;
        for (uint32_t i = 0; i < n; i++) {
            const uint64_t key = tide_read_u64(&r);
            const uint32_t source = tide_read_u32(&r);
            if (source >= c->count || !c->shards[source].used || source == id) continue;
            cshard *from = &c->shards[source];
            from->held.grows = true;
            tide_write_u32(&from->held, id);
            tide_write_u64(&from->held, key);
            from->held_count++;
        }
    }
    if (tide_read_u8(&r)) { // Its checkpoint
        const uint32_t world_tick = tide_read_u32(&r);
        const uint32_t n = tide_read_u32(&r);
        const uint8_t *world = tide_read_bytes(&r, n);
        if (!world) return false;
        free(sh->world);
        sh->world = malloc(n ? n : 1u);
        if (!sh->world) tide_out_of_memory();
        memcpy(sh->world, world, n);
        sh->world_size = n;
        sh->world_tick = world_tick;
    }
    if (r.failed || r.at != r.size) return false;
    sh->heard = true;
    sh->applied = applied;
    sh->tick = tick;
    sh->injected = injected;
    sh->status = status;
    return true;
}

// ---------------------------------------------------------------------------
// Carrying messages

// Where a message for a shard goes: to it, or for one that merged into
// another and ended, to that one (an entity lent to it on the way is that one's)
static uint32_t live_shard(const tide_shard_coordinator *c, uint32_t id)
{
    for (uint32_t left = c->count; left-- && id < c->count && c->shards[id].gone;) id = c->shards[id].into;
    return id;
}

int32_t tide_shard_coordinator_owner(const tide_shard_coordinator *c, const tide_entity e)
{
    const uint64_t *moved = map_find(&c->directory, key_of(e));
    if (moved) return (int32_t)*moved;
    for (uint32_t i = 0; i < c->range_count; i++) {
        if (e.index >= c->ranges[i].first && e.index < c->ranges[i].end) return (int32_t)c->ranges[i].shard;
    }
    return -1;
}

// The handles of an ARRIVE's tree (after its header, flags and lender) go to
// the directory as the shard it went to's
static void note_tree(tide_shard_coordinator *c, const uint8_t *data, const uint32_t size, const uint32_t to)
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
        map_put(&c->directory, key_of(handle), to);
    }
}

static void carry(tide_shard_coordinator *c, const uint32_t from)
{
    cshard *sh = &c->shards[from];
    tide_reader r = {sh->outbox.data, sh->outbox.size, 0, false};
    tide_shard_outgoing m;
    while (tide_shard_next_outgoing(&r, &m)) {
        if (!m.number || m.number <= c->shards[from].delivered) continue;
        c->shards[from].delivered = m.number;
        const uint8_t kind = m.data[0];
        switch (m.route) {
        case TIDE_SHARD_TO_SHARD: {
            const uint32_t to = live_shard(c, m.target);
            if (kind == TIDE_SHARD_ARRIVE) note_tree(c, m.data, m.size, to);
            inject(c, to, m.data, m.size);
            break;
        }
        case TIDE_SHARD_TO_OWNER: {
            const int32_t owner = tide_shard_coordinator_owner(c, m.entity);
            if (owner < 0 && kind != TIDE_SHARD_ARRIVE) break;
            // An entity with no position, to where the one it goes with is
            // (or back, where that's no shard's)
            const uint32_t to = owner >= 0 ? live_shard(c, (uint32_t)owner) : from;
            if (kind == TIDE_SHARD_ARRIVE) note_tree(c, m.data, m.size, to);
            inject(c, to, m.data, m.size);
            break;
        }
        case TIDE_SHARD_TO_ALL:
            for (uint32_t i = 0; i < c->count; i++) {
                if (i != from) inject(c, i, m.data, m.size);
            }
            break;
        default: break;
        }
    }
    sh = &c->shards[from];
    sh->outbox.size = 0;
    // Its ghosts, to the shards they're for
    tide_reader g = {sh->ghosts.data, sh->ghosts.size, 0, false};
    while (g.at < g.size) {
        const uint32_t to = tide_read_u32(&g);
        const uint32_t n = tide_read_u32(&g);
        const uint8_t *message = tide_read_bytes(&g, n);
        if (!message) break;
        inject(c, live_shard(c, to), message, n);
    }
    sh->ghosts.size = 0;
}

// Whether every shard has applied all it was given and handed: nothing is on
// its way but in the outboxes, which are in the worlds
static bool drained(const tide_shard_coordinator *c)
{
    for (uint32_t i = 0; i < c->count; i++) {
        const cshard *sh = &c->shards[i];
        if (!sh->used) continue;
        if (!sh->heard || sh->applied != sh->batch || sh->injected) return false;
    }
    return true;
}

// Every sender lets go of what it sent that was applied: each shard's ticks
// were kept (its journal's) before it reported, so nothing it let go of can
// be lost with a shard
static void acknowledge(tide_shard_coordinator *c)
{
    for (uint32_t i = 0; i < c->count; i++) {
        cshard *sh = &c->shards[i];
        if (!sh->used || sh->acked == sh->delivered) continue;
        sh->acked = sh->delivered;
        uint8_t ack[16];
        tide_writer w = {ack, sizeof ack, 0, false, false};
        tide_shard_header(&w, TIDE_SHARD_ACK, TIDE_SHARD_ORCHESTRATOR, 0u);
        tide_write_u32(&w, sh->acked);
        inject(c, i, ack, w.size);
    }
}

// ---------------------------------------------------------------------------
// Splitting a region whose shard holds too much, merging quiet neighbours,
// and ending a retired shard

static int compare_double(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static bool maybe_split(tide_shard_coordinator *c, const uint32_t id)
{
    cshard *sh = &c->shards[id];
    if (sh->status.owned <= c->desc.most_entities) return false;
    if (!sh->sampled) { // Where its entities are, first
        if (!sh->sample_asked) {
            sh->sample_stride = sh->status.owned / 4096u + 1u;
            sh->sample_asked = true;
        }
        return false;
    }
    sh->sampled = false;
    // Across its longest side where its entities are, at their middle, keeping
    // each half twice the range across
    uint32_t axis = 0;
    double widest = -1.0;
    for (uint32_t k = 0; k < c->desc.dims; k++) {
        const double lo = sh->box.lo[k] > sh->spread.lo[k] ? sh->box.lo[k] : sh->spread.lo[k];
        const double hi = sh->box.hi[k] < sh->spread.hi[k] ? sh->box.hi[k] : sh->spread.hi[k];
        if (hi - lo > widest) {
            widest = hi - lo;
            axis = k;
        }
    }
    const uint32_t n = sh->point_count;
    double *values = malloc((n ? n : 1u) * sizeof *values);
    if (!values) tide_out_of_memory();
    for (uint32_t i = 0; i < n; i++) values[i] = sh->points[i][axis];
    qsort(values, n, sizeof *values, compare_double);
    const double cut = n ? values[n / 2u] : 0.0;
    free(values);
    const double floor = 2.0 * c->desc.range;
    if (!(cut - sh->box.lo[axis] >= floor && sh->box.hi[axis] - cut >= floor)) return false;
    const uint32_t added = new_shard(c);
    sh = &c->shards[id]; // The array may have moved
    cshard *half = &c->shards[added];
    half->region = true;
    half->box = sh->box;
    half->box.lo[axis] = cut;
    sh->box.hi[axis] = cut;
    half->used = true;
    give_range(c, added);
    // It starts at the world's tick, the root's, holding nothing
    start_anew(c, added, c->shards[TIDE_SHARD_ROOT].tick);
    for (uint32_t p = 0; p < c->player_count; p++) {
        if (c->players[p].here) admit(c, added, p, c->players[p].cookie);
    }
    // Every shard learns the new regions, and what's in the new half moves
    // there as its shard's next tick ends
    reshaped(c);
    return true;
}

// Whether two regions make one box, and which
static bool joins(const tide_shard_coordinator *c, const tide_shard_box *a, const tide_shard_box *b, tide_shard_box *both)
{
    uint32_t touching = 3u;
    for (uint32_t k = 0; k < c->desc.dims; k++) {
        if (a->lo[k] == b->lo[k] && a->hi[k] == b->hi[k]) continue;
        if (touching != 3u || !(a->hi[k] == b->lo[k] || b->hi[k] == a->lo[k])) return false;
        touching = k;
    }
    if (touching == 3u) return false;
    *both = *a;
    both->lo[touching] = a->lo[touching] < b->lo[touching] ? a->lo[touching] : b->lo[touching];
    both->hi[touching] = a->hi[touching] > b->hi[touching] ? a->hi[touching] : b->hi[touching];
    return true;
}

// One merge at a time, of two regions together owning a quarter of what
// splits one at most, so the merged one doesn't split again soon
static bool maybe_merge(tide_shard_coordinator *c)
{
    for (uint32_t i = 0; i < c->count; i++) {
        if (c->shards[i].retiring) return false;
    }
    for (uint32_t a = 0; a < c->count; a++) {
        if (!c->shards[a].used || !c->shards[a].region) continue;
        for (uint32_t b = a + 1u; b < c->count; b++) {
            if (!c->shards[b].used || !c->shards[b].region) continue;
            tide_shard_box both;
            if (!joins(c, &c->shards[a].box, &c->shards[b].box, &both)) continue;
            if (((uint64_t)c->shards[a].status.owned + c->shards[b].status.owned) * 4u > c->desc.most_entities) continue;
            c->shards[a].box = both;
            c->shards[b].region = false;
            c->shards[b].retiring = true;
            c->shards[b].into = a;
            uint8_t data[16];
            tide_writer w = {data, sizeof data, 0, false, false};
            tide_shard_header(&w, TIDE_SHARD_RETIRE, TIDE_SHARD_ORCHESTRATOR, 0u);
            tide_write_u32(&w, a);
            tell_after(c, b, data, w.size);
            reshaped(c);
            return true;
        }
    }
    return false;
}

// A retiring shard ends once it owns nothing, nothing it said is
// unacknowledged, no code of it waits and nothing for it is still to be applied
static void finish_retiring(tide_shard_coordinator *c)
{
    for (uint32_t i = 0; i < c->count; i++) {
        cshard *sh = &c->shards[i];
        if (!sh->used || !sh->retiring || c->tell_after) continue;
        if (sh->status.owned || sh->status.waiting || sh->status.lent || sh->status.outbox || sh->injected) continue;
        act(c, TIDE_SHARD_ACTION_STOP, i);
        free_shard(sh);
        const uint32_t into = sh->into;
        *sh = (cshard){.gone = true, .into = into};
        c->changed = true;
        retarget(c);
    }
}

// More indices before a shard runs out, splits and merges, and retired
// shards ending, at a moment nothing is on its way and no checkpoint is
static void reshape(tide_shard_coordinator *c)
{
    for (uint32_t i = 0; i < c->count; i++) {
        cshard *sh = &c->shards[i];
        if (!sh->used || sh->status.indices >= RANGE_SIZE / 2u) continue;
        tell_range(c, give_range(c, i));
        sh->status.indices += RANGE_SIZE; // Until it reports having it
        c->changed = true;
    }
    if (c->desc.most_entities && !c->tell_after) {
        const uint32_t count = c->count;
        bool split = false;
        for (uint32_t i = 0; i < count; i++) {
            if (c->shards[i].used && c->shards[i].region && (c->shards[i].sampled || c->updates - c->reshaped >= SPLIT_EVERY))
                split |= maybe_split(c, i);
        }
        if (c->updates - c->reshaped >= SPLIT_EVERY) {
            c->reshaped = c->updates;
            if (!split) (void)maybe_merge(c);
        }
    }
    finish_retiring(c);
}

// ---------------------------------------------------------------------------
// Players: a player joins the world at the root, which gives them a cookie;
// every other shard takes them as the same player with it. The root sends
// PlayerJoined and PlayerLeft as they come and go from the world as a whole.

void tide_shard_coordinator_player(tide_shard_coordinator *c, const tide_player_id player, const bool joined)
{
    uint8_t data[16];
    tide_writer w = {data, sizeof data, 0, false, false};
    tide_shard_header(&w, TIDE_SHARD_PLAYER, TIDE_SHARD_ORCHESTRATOR, 0u);
    tide_write_u8(&w, joined ? 1u : 0u);
    tide_write_u32(&w, player.id);
    inject(c, TIDE_SHARD_ROOT, data, w.size);
}

static void track_players(tide_shard_coordinator *c)
{
    for (uint32_t p = 0; p < c->player_count; p++) {
        roster *r = &c->players[p];
        const tide_player_id id = tide_player_from_index((int32_t)p);
        bool here = false;
        uint64_t cookie = 0;
        for (uint32_t i = 0; i < c->count && !here; i++) {
            const cshard *sh = &c->shards[i];
            if (!sh->used) continue;
            for (uint32_t k = 0; k < sh->connected_count; k++) {
                if (sh->connected[k] != id.id) continue;
                here = true;
                cookie = sh->cookies[k];
                break;
            }
        }
        if (here) {
            r->away = false;
            if (r->here) continue;
            r->here = true;
            r->cookie = cookie;
            admit_everywhere(c, p);
            tide_shard_coordinator_player(c, id, true);
            continue;
        }
        // On no shard: they've a while to come back, as a server gives them
        if (!r->here) continue;
        if (!r->away) {
            r->away = true;
            r->away_since = c->now;
            continue;
        }
        if (c->now - r->away_since < PLAYER_AWAY_SECONDS) continue;
        r->here = r->away = false;
        tide_shard_coordinator_player(c, id, false);
    }
}

static int by_player(const void *a, const void *b)
{
    const sighting *x = a, *y = b;
    if (x->player != y->player) return x->player < y->player ? -1 : 1;
    return x->shard < y->shard ? -1 : x->shard > y->shard ? 1 : 0;
}

static int by_number(const void *a, const void *b)
{
    const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

// Each region's neighbours: the regions near enough that what it owns (up to
// a quarter of the range past it) can be within the range of them
static void find_neighbours(tide_shard_coordinator *c)
{
    if (c->neighbour_first && c->neighbours_of == c->map_version) return;
    c->neighbours_of = c->map_version;
    free(c->neighbour_first);
    free(c->neighbours);
    c->neighbour_first = calloc((size_t)c->count + 1u, sizeof *c->neighbour_first);
    uint32_t room = 16u, count = 0;
    c->neighbours = malloc(room * sizeof *c->neighbours);
    if (!c->neighbour_first || !c->neighbours) tide_out_of_memory();
    for (uint32_t a = 0; a < c->count; a++) {
        c->neighbour_first[a] = count;
        if (!c->shards[a].used || !c->shards[a].region) continue;
        for (uint32_t b = 0; b < c->count; b++) {
            if (a == b || !c->shards[b].used || !c->shards[b].region) continue;
            if (!tide_shard_box_near(&c->shards[a].box, &c->shards[b].box, 1.25 * c->desc.range, c->desc.dims)) continue;
            if (count == room) {
                room *= 2u;
                c->neighbours = realloc(c->neighbours, room * sizeof *c->neighbours);
                if (!c->neighbours) tide_out_of_memory();
            }
            c->neighbours[count++] = b;
        }
    }
    c->neighbour_first[c->count] = count;
}

// Where each player's entities are: their home is the shard that owns the
// most of them, and their machine follows each shard owning any, and each
// region within the range of any. The shards are told when that changes,
// every shard every player's.
static void route_players(tide_shard_coordinator *c)
{
    if (!c->player_count) return;
    find_neighbours(c);
    uint32_t seen_count = 0;
    for (uint32_t i = 0; i < c->count; i++) seen_count += c->shards[i].used ? c->shards[i].presence_count : 0u;
    sighting *seen = malloc((seen_count ? seen_count : 1u) * sizeof *seen);
    if (!seen) tide_out_of_memory();
    seen_count = 0;
    for (uint32_t i = 0; i < c->count; i++) {
        const cshard *sh = &c->shards[i];
        if (!sh->used) continue;
        for (uint32_t k = 0; k < sh->presence_count; k++) {
            if (sh->presence[k].player < c->player_count) seen[seen_count++] = sh->presence[k];
        }
    }
    if (seen_count) qsort(seen, seen_count, sizeof *seen, by_player);
    uint32_t *follows = NULL;
    uint32_t follows_room = 0;
    for (uint32_t p = 0, at = 0; p < c->player_count; p++) {
        const uint32_t first = at;
        while (at < seen_count && seen[at].player == p) at++;
        roster *r = &c->players[p];
        uint32_t count = 0;
        if (r->here) {
            uint32_t home = r->home, most = 0;
            for (uint32_t k = first; k < at; k++) {
                if (seen[k].count > most) {
                    most = seen[k].count;
                    home = seen[k].shard;
                }
            }
            if (home >= c->count || !c->shards[home].used) home = TIDE_SHARD_ROOT;
            r->home = home;
            // Its home, the shards owning any of its entities, and the regions near those
            uint32_t most_follows = 1u + (at - first);
            for (uint32_t k = first; k < at; k++) {
                if (seen[k].count) most_follows += c->neighbour_first[seen[k].shard + 1u] - c->neighbour_first[seen[k].shard];
            }
            if (most_follows > follows_room) {
                follows_room = most_follows * 2u;
                follows = realloc(follows, follows_room * sizeof *follows);
                if (!follows) tide_out_of_memory();
            }
            follows[count++] = home;
            for (uint32_t k = first; k < at; k++) {
                follows[count++] = seen[k].shard;
                if (!seen[k].count) continue;
                for (uint32_t n = c->neighbour_first[seen[k].shard]; n < c->neighbour_first[seen[k].shard + 1u]; n++) {
                    const uint32_t b = c->neighbours[n];
                    if (tide_shard_box_near(&seen[k].box, &c->shards[b].box, c->desc.range, c->desc.dims)) follows[count++] = b;
                }
            }
            qsort(follows, count, sizeof *follows, by_number);
            uint32_t kept = 0;
            for (uint32_t k = 0; k < count; k++) {
                if (c->shards[follows[k]].used && (!kept || follows[kept - 1u] != follows[k])) follows[kept++] = follows[k];
            }
            count = kept;
        }
        // Told again only when it changed
        uint64_t hash = tide_hash_more(TIDE_HASH_START, &r->home, sizeof r->home);
        hash = tide_hash_more(hash, follows, count * sizeof *follows);
        hash = tide_hash_end(hash) | 1u;
        if (!r->here) hash = 0;
        if (hash == r->route) continue;
        r->route = hash;
        tide_writer w = {.grows = true};
        tide_shard_header(&w, TIDE_SHARD_ROUTE, TIDE_SHARD_ORCHESTRATOR, 0u);
        tide_write_u32(&w, tide_player_from_index((int32_t)p).id);
        tide_write_u32(&w, r->home);
        tide_write_u32(&w, count);
        for (uint32_t k = 0; k < count; k++) {
            tide_write_u32(&w, follows[k]);
            tide_write_u64(&w, c->shards[follows[k]].match);
        }
        if (w.overflow) tide_out_of_memory();
        for (uint32_t i = 0; i < c->count; i++) inject(c, i, w.data, w.size);
        free(w.data);
    }
    free(follows);
    free(seen);
}

// ---------------------------------------------------------------------------
// Checkpoints: a header, the ranges, the directory and the players, then each
// shard, the tick its journal goes on from and its world

static void save_head(const tide_shard_coordinator *c, tide_writer *w)
{
    tide_write_u32(w, SAVE_MAGIC);
    tide_write_u32(w, SAVE_VERSION);
    tide_write_u64(w, c->next_index);
    tide_write_u32(w, c->range_count);
    for (uint32_t i = 0; i < c->range_count; i++) {
        tide_write_u32(w, c->ranges[i].first);
        tide_write_u32(w, c->ranges[i].end);
        tide_write_u32(w, c->ranges[i].shard);
    }
    tide_write_u32(w, c->directory.count);
    for (uint32_t i = 0; i < c->directory.room; i++) {
        if (!c->directory.keys[i]) continue;
        tide_write_u64(w, c->directory.keys[i]);
        tide_write_u32(w, (uint32_t)c->directory.values[i]);
    }
    // Who's in the world, and their cookies: they come back as themselves
    tide_write_u32(w, c->player_count);
    for (uint32_t p = 0; p < c->player_count; p++) {
        tide_write_u8(w, c->players[p].here ? 1u : 0u);
        tide_write_u64(w, c->players[p].here ? c->players[p].cookie : 0u);
    }
    tide_write_u32(w, c->count);
}

static void save_shard(const tide_shard_coordinator *c, const uint32_t i, tide_writer *w, const uint8_t *world,
                       const uint32_t world_size, const uint32_t tick)
{
    const cshard *sh = &c->shards[i];
    tide_write_u8(w, sh->used ? 1u : 0u);
    tide_write_u8(w, sh->gone ? 1u : sh->retiring ? 2u : 0u);
    tide_write_u32(w, sh->into);
    if (!sh->used) return;
    tide_write_u8(w, sh->region ? 1u : 0u);
    tide_write_bytes(w, sh->box.lo, sizeof sh->box.lo);
    tide_write_bytes(w, sh->box.hi, sizeof sh->box.hi);
    tide_write_u32(w, tick);
    tide_write_u32(w, world_size);
    tide_write_bytes(w, world, world_size);
}

uint8_t *tide_shard_coordinator_save(const tide_shard_coordinator *c, const tide_shard_world_fn world, void *user,
                                     uint32_t *size)
{
    tide_writer w = {.grows = true};
    save_head(c, &w);
    for (uint32_t i = 0; i < c->count; i++) {
        uint32_t n = 0, tick = 0;
        uint8_t *bytes = c->shards[i].used ? world(user, i, &n, &tick) : NULL;
        save_shard(c, i, &w, bytes, n, tick);
        free(bytes);
    }
    if (w.overflow) tide_out_of_memory();
    *size = w.size;
    return w.data;
}

bool tide_shard_checkpoint_world(const uint8_t *data, const uint32_t size, const uint32_t shard, const uint8_t **world,
                                 uint32_t *world_size, uint32_t *tick)
{
    tide_reader r = {data, size, 0, false};
    if (tide_read_u32(&r) != SAVE_MAGIC || tide_read_u32(&r) != SAVE_VERSION) return false;
    tide_read_u64(&r);
    const uint32_t ranges = tide_read_u32(&r);
    tide_read_bytes(&r, ranges * 12u);
    const uint32_t moved = tide_read_u32(&r);
    tide_read_bytes(&r, moved * 12u);
    const uint32_t players = tide_read_u32(&r);
    tide_read_bytes(&r, players * 9u);
    const uint32_t count = tide_read_u32(&r);
    for (uint32_t i = 0; i < count && !r.failed; i++) {
        const bool used = tide_read_u8(&r) != 0;
        tide_read_u8(&r);
        tide_read_u32(&r);
        if (!used) continue;
        tide_read_bytes(&r, 1u + 48u);
        const uint32_t at = tide_read_u32(&r);
        const uint32_t n = tide_read_u32(&r);
        const uint8_t *bytes = tide_read_bytes(&r, n);
        if (i == shard && bytes) {
            *world = bytes;
            *world_size = n;
            *tick = at;
            return true;
        }
    }
    return false;
}

// Every running shard makes a checkpoint of its world for its next report
static void begin_checkpoint(tide_shard_coordinator *c)
{
    c->checkpoint = CHECKPOINT_GATHERING;
    c->head = (tide_writer){.data = c->head.data, .capacity = c->head.capacity, .grows = true};
    save_head(c, &c->head);
    if (c->head.overflow) tide_out_of_memory();
    for (uint32_t i = 0; i < c->count; i++) {
        cshard *sh = &c->shards[i];
        if (!sh->used) continue;
        free(sh->world);
        sh->world = NULL;
        sh->flags |= TIDE_SHARD_DO_CHECKPOINT;
    }
}

// Once every shard's world is in, the checkpoint, for the host to keep
static void gather_checkpoint(tide_shard_coordinator *c)
{
    for (uint32_t i = 0; i < c->count; i++) {
        if (c->shards[i].used && !c->shards[i].world) return;
    }
    tide_writer w = {.grows = true};
    tide_write_bytes(&w, c->head.data, c->head.size);
    for (uint32_t i = 0; i < c->count; i++) {
        cshard *sh = &c->shards[i];
        save_shard(c, i, &w, sh->world, sh->world_size, sh->world_tick);
        free(sh->world);
        sh->world = NULL;
    }
    if (w.overflow) tide_out_of_memory();
    tide_shard_action *a = act(c, TIDE_SHARD_ACTION_CHECKPOINT, 0u);
    a->data = keep_bytes(c, w.data);
    a->size = w.size;
    c->checkpoint = CHECKPOINT_KEEPING;
}

void tide_shard_coordinator_kept(tide_shard_coordinator *c)
{
    if (!c || c->checkpoint != CHECKPOINT_KEEPING) return;
    c->checkpoint = CHECKPOINT_IDLE;
    c->checkpointed = c->now;
    // The change it has, told now
    if (c->tell_after) {
        c->tell_after = false;
        tell_map(c);
        tide_reader r = {c->told_after.data, c->told_after.size, 0, false};
        while (r.at < r.size) {
            const uint32_t to = tide_read_u32(&r);
            const uint32_t n = tide_read_u32(&r);
            const uint8_t *message = tide_read_bytes(&r, n);
            if (!message) break;
            inject(c, to, message, n);
        }
        c->told_after.size = 0;
    }
    c->changed = false;
}

void tide_shard_coordinator_restarted(tide_shard_coordinator *c, const uint32_t shard)
{
    if (!c || shard >= c->count || !c->shards[shard].used) return;
    cshard *sh = &c->shards[shard];
    sh->heard = false; // Until it reports, as a new agent
    sh->injected = 0;
    sh->outbox.size = sh->ghosts.size = 0;
    sh->injects.size = sh->admits.size = sh->held.size = 0;
    sh->inject_count = sh->admit_count = sh->held_count = 0;
    // Everything unacknowledged, again from every shard: what it was handed went with it
    for (uint32_t i = 0; i < c->count; i++) {
        cshard *other = &c->shards[i];
        if (!other->used) continue;
        other->delivered = other->acked;
        other->outbox.size = 0;
        other->flags |= TIDE_SHARD_DO_FROM_START | TIDE_SHARD_DO_TARGETS; // ...and their ghosts whole
    }
    // What the orchestrator told it since, again
    for (uint32_t k = 0; k < c->range_count; k++) {
        if (c->ranges[k].shard == shard) tell_range(c, c->ranges[k]);
    }
    if (sh->retiring) {
        uint8_t retire[16];
        tide_writer w = {retire, sizeof retire, 0, false, false};
        tide_shard_header(&w, TIDE_SHARD_RETIRE, TIDE_SHARD_ORCHESTRATOR, 0u);
        tide_write_u32(&w, sh->into);
        inject(c, shard, retire, w.size);
    }
    tide_writer w = {.grows = true};
    tide_shard_header(&w, TIDE_SHARD_MAP, TIDE_SHARD_ORCHESTRATOR, 0u);
    write_regions(c, &w);
    if (w.overflow) tide_out_of_memory();
    inject(c, shard, w.data, w.size);
    free(w.data);
    for (uint32_t p = 0; p < c->player_count; p++) {
        if (c->players[p].here) admit(c, shard, p, c->players[p].cookie);
        c->players[p].route = 0; // Every route told again
    }
}

bool tide_shard_coordinator_checkpointing(const tide_shard_coordinator *c)
{
    return c && c->checkpoint != CHECKPOINT_IDLE;
}

tide_shard_coordinator *tide_shard_coordinator_load(const tide_shard_coordinator_desc *desc, const uint8_t *data,
                                                    const uint32_t size, const double now)
{
    tide_shard_coordinator *c = calloc(1, sizeof *c);
    if (!c || !setup(c, desc, now)) {
        free(c);
        return NULL;
    }
    tide_reader r = {data, size, 0, false};
    if (tide_read_u32(&r) != SAVE_MAGIC || tide_read_u32(&r) != SAVE_VERSION) goto fail;
    c->next_index = tide_read_u64(&r);
    const uint32_t ranges = tide_read_u32(&r);
    if (r.failed || ranges > (r.size - r.at) / 12u) goto fail;
    c->ranges = calloc(ranges ? ranges : 1u, sizeof *c->ranges);
    if (!c->ranges) tide_out_of_memory();
    for (uint32_t i = 0; i < ranges; i++) {
        c->ranges[i].first = tide_read_u32(&r);
        c->ranges[i].end = tide_read_u32(&r);
        c->ranges[i].shard = tide_read_u32(&r);
    }
    c->range_count = ranges;
    const uint32_t moved = tide_read_u32(&r);
    if (r.failed || moved > (r.size - r.at) / 12u) goto fail;
    for (uint32_t i = 0; i < moved; i++) {
        const uint64_t key = tide_read_u64(&r);
        const uint32_t owner = tide_read_u32(&r);
        if (key) map_put(&c->directory, key, owner);
    }
    const uint32_t players = tide_read_u32(&r);
    if (r.failed || players != c->player_count) goto fail;
    for (uint32_t p = 0; p < players; p++) {
        c->players[p].here = tide_read_u8(&r) != 0;
        c->players[p].cookie = tide_read_u64(&r);
        // Every player who was in the world has a while to come back
        c->players[p].away = c->players[p].here;
        c->players[p].away_since = now;
    }
    const uint32_t count = tide_read_u32(&r);
    if (r.failed || count > (r.size - r.at)) goto fail;
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t id = new_shard(c);
        cshard *sh = &c->shards[id];
        const bool used = tide_read_u8(&r) != 0;
        const uint8_t state = tide_read_u8(&r);
        sh->gone = state == 1u;
        sh->retiring = state == 2u;
        sh->into = tide_read_u32(&r);
        if (r.failed || state > 2u || (sh->gone && used) || (sh->retiring && !used) || ((sh->gone || sh->retiring) && sh->into >= count)) goto fail;
        if (!used) continue;
        sh->region = tide_read_u8(&r) != 0;
        if (!read_box(&r, &sh->box)) goto fail;
        const uint32_t tick = tide_read_u32(&r);
        const uint32_t bytes = tide_read_u32(&r);
        const uint8_t *world = tide_read_bytes(&r, bytes);
        if (!world) goto fail;
        uint8_t *copy = malloc(bytes ? bytes : 1u);
        if (!copy) tide_out_of_memory();
        memcpy(copy, world, bytes);
        tide_shard_action *a = act(c, TIDE_SHARD_ACTION_START, id);
        a->world = keep_bytes(c, copy);
        a->world_size = bytes;
        a->tick = tick;
        sh->used = true;
        sh->tick = tick;
        sh->status.indices = RANGE_SIZE; // Until it reports
        sh->flags |= TIDE_SHARD_DO_TARGETS;
    }
    if (r.failed || r.at != r.size) goto fail;
    // Orchestrator messages on their way when it was saved went with the
    // servers: each shard's ranges again, a retiring one's retirement, the
    // regions, and the players, admitted everywhere with their cookies
    for (uint32_t k = 0; k < c->range_count; k++) tell_range(c, c->ranges[k]);
    for (uint32_t i = 0; i < c->count; i++) {
        if (!c->shards[i].used || !c->shards[i].retiring) continue;
        uint8_t retire[16];
        tide_writer w = {retire, sizeof retire, 0, false, false};
        tide_shard_header(&w, TIDE_SHARD_RETIRE, TIDE_SHARD_ORCHESTRATOR, 0u);
        tide_write_u32(&w, c->shards[i].into);
        inject(c, i, retire, w.size);
    }
    for (uint32_t p = 0; p < c->player_count; p++) {
        if (c->players[p].here) admit_everywhere(c, p);
    }
    tell_map(c);
    c->checkpointed = now;
    return c;
fail:
    tide_shard_coordinator_destroy(c);
    return NULL;
}

// ---------------------------------------------------------------------------

void tide_shard_coordinator_step(tide_shard_coordinator *c, const double now)
{
    if (!c) return;
    clear_actions(c);
    c->now = now;
    c->updates++;
    if (c->checkpoint == CHECKPOINT_GATHERING) gather_checkpoint(c);
    if (c->checkpoint == CHECKPOINT_IDLE && drained(c)) {
        reshape(c);
        const double every = c->desc.checkpoint_seconds > 0.0 ? c->desc.checkpoint_seconds : 60.0;
        if (c->desc.checkpoints && (c->changed || now - c->checkpointed >= every)) begin_checkpoint(c);
        else acknowledge(c);
        if (!c->desc.checkpoints) c->changed = false;
    }
    track_players(c);
    for (uint32_t i = 0; i < c->count; i++) {
        if (c->shards[i].used) carry(c, i);
    }
    route_players(c);
}

// ---------------------------------------------------------------------------

uint32_t tide_shard_coordinator_count(const tide_shard_coordinator *c)
{
    return c ? c->count : 0u;
}

bool tide_shard_coordinator_running(const tide_shard_coordinator *c, const uint32_t shard)
{
    return c && shard < c->count && c->shards[shard].used;
}

bool tide_shard_coordinator_region(const tide_shard_coordinator *c, const uint32_t shard, tide_shard_box *out)
{
    if (!c || shard >= c->count || !c->shards[shard].used || !c->shards[shard].region) return false;
    *out = c->shards[shard].box;
    return true;
}

void tide_shard_coordinator_status(const tide_shard_coordinator *c, const uint32_t shard, tide_shard_status *out)
{
    memset(out, 0, sizeof *out);
    out->id = TIDE_SHARD_NONE;
    if (c && shard < c->count && c->shards[shard].used && c->shards[shard].heard) *out = c->shards[shard].status;
}

uint32_t tide_shard_coordinator_at(const tide_shard_coordinator *c, const double p[3])
{
    for (uint32_t i = 0; c && i < c->count; i++) {
        const cshard *sh = &c->shards[i];
        if (sh->used && sh->region && tide_shard_box_holds(&sh->box, p, c->desc.dims)) return i;
    }
    return TIDE_SHARD_ROOT;
}

void tide_shard_coordinator_set_match(tide_shard_coordinator *c, const uint32_t shard, const uint64_t match)
{
    if (c && shard < c->count) c->shards[shard].match = match;
}
