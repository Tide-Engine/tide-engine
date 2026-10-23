#include "tide/shards.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/hosting.h"
#include "tide/shard_agent.h"
#include "tide/shard_coordinator.h"

// See tide/shards.h. Every shard of the world in this process: each its server
// and its agent (tide/shard_agent.h), and the world's coordinator
// (tide/shard_coordinator.h), which they report to and take instructions
// from each update, as they would across machines.

// A shard's server's journal goes to the world's (tide_shards_desc.journal), as that shard's
typedef struct journal_ref {
    tide_shards *s;
    uint32_t shard;
} journal_ref;

typedef struct here {
    tide_server *server;
    tide_shard_agent *agent;
    uint64_t match;        // What its host knows it by
    uint32_t ticks_before; // Its ticks before its server's first: the journal numbers them on from there
    journal_ref *journal;
} here;

struct tide_shards {
    tide_shards_desc desc;
    const tide_game *game;
    const tide_shard_game *api;
    uint32_t tick_rate;
    float dt;
    tide_shard_coordinator *coordinator;
    here *shards;
    uint32_t room;
    // Recovering: each shard's journal records since the checkpoint
    tide_shards_records records;
    void *records_user;
    bool failed;
};

static void shard_journal(void *user, const uint32_t tick, const uint8_t *record, const uint32_t size)
{
    const journal_ref *j = user;
    // Numbered on from its ticks before this server, when it went on from a checkpoint
    const uint32_t before = j->s->shards[j->shard].ticks_before;
    if (!before || size < 4u) {
        j->s->desc.journal(j->s->desc.user, j->shard, tick, record, size);
        return;
    }
    uint8_t *numbered = malloc(size);
    if (!numbered) tide_out_of_memory();
    memcpy(numbered, record, size);
    const uint32_t on = before + tick;
    for (uint32_t k = 0; k < 4u; k++) numbered[k] = (uint8_t)(on >> (8u * k));
    j->s->desc.journal(j->s->desc.user, j->shard, on, numbered, size);
    free(numbered);
}

static here *grow(tide_shards *s, const uint32_t id)
{
    if (id >= s->room) {
        const uint32_t room = id + 8u;
        here *grown = realloc(s->shards, room * sizeof *grown);
        if (!grown) tide_out_of_memory();
        memset(grown + s->room, 0, (room - s->room) * sizeof *grown);
        s->shards = grown;
        s->room = room;
    }
    return &s->shards[id];
}

// A shard's world, from a checkpoint's and its journal's records since, run again
static bool replay(tide_shards *s, const uint32_t shard, void *world, uint32_t *next)
{
    const uint8_t *record = NULL;
    uint32_t size = 0;
    for (uint32_t index = 0; s->records && s->records(s->records_user, shard, index, &record, &size); index++) {
        uint32_t tick;
        if (!tide_replay_tick(record, size, &tick)) return false;
        if (tick < *next) continue; // From before the checkpoint
        if (tick != *next || !tide_replay(s->game, world, record, size, s->desc.jobs)) return false;
        (*next)++;
    }
    return true;
}

static void start(tide_shards *s, const tide_shard_action *a, const double now)
{
    void *world = calloc(1, s->game->world_size);
    if (!world) tide_out_of_memory();
    uint32_t next = a->tick;
    if (a->world) {
        if (!s->game->unpack_world(world, a->world, a->world_size) || !replay(s, a->shard, world, &next)) {
            s->failed = true;
            if (s->game->free_world) s->game->free_world(world);
            free(world);
            return;
        }
    } else {
        // A shard that starts later starts at the world's tick, the root's
        s->api->start(world, s->dt, a->shard == TIDE_SHARD_ROOT ? s->desc.start : NULL, s->desc.seed, a->tick, a->config,
                      a->config_size);
    }
    tide_server_desc d = {0};
    d.game = s->game;
    d.tick_rate = s->tick_rate;
    d.dt = s->dt;
    d.world = world;
    d.seed = s->desc.seed;
    d.jobs = s->desc.jobs;
    d.closed = a->shard != TIDE_SHARD_ROOT; // Players join the world at the root, and a region takes those it's told of
    if (s->desc.transports) s->desc.transports(s->desc.user, a->shard, d.transports);
    here *h = grow(s, a->shard);
    h->ticks_before = a->world ? next : 0u;
    if (s->desc.journal) { // Its ticks go to the world's journal, as this shard's
        if (!h->journal) {
            h->journal = malloc(sizeof *h->journal);
            if (!h->journal) tide_out_of_memory();
            *h->journal = (journal_ref){s, a->shard};
        }
        d.journal_user = h->journal;
        d.journal = shard_journal;
    }
    h->server = s->desc.host_start ? s->desc.host_start(s->desc.user, a->shard, &d, &h->match) : tide_server_create(&d, now);
    if (!h->server) tide_out_of_memory();
    h->agent = tide_shard_agent_create(s->game, h->server, a->shard, h->ticks_before, a->world != NULL);
    if (!h->agent) tide_out_of_memory();
    tide_shard_coordinator_set_match(s->coordinator, a->shard, h->match);
    if (s->game->free_world) s->game->free_world(world);
    free(world);
}

static void stop(tide_shards *s, const uint32_t id)
{
    if (id >= s->room) return;
    here *h = &s->shards[id];
    if (h->server && s->desc.host_start) {
        if (s->desc.host_stop) s->desc.host_stop(s->desc.user, id, h->match);
    } else if (h->server) {
        tide_server_destroy(h->server);
    }
    tide_shard_agent_destroy(h->agent);
    free(h->journal);
    *h = (here){0};
}

// What the coordinator decided, done
static void act(tide_shards *s, const double now)
{
    const tide_shard_action *actions;
    const uint32_t count = tide_shard_coordinator_actions(s->coordinator, &actions);
    for (uint32_t i = 0; i < count; i++) {
        const tide_shard_action *a = &actions[i];
        switch (a->kind) {
        case TIDE_SHARD_ACTION_START: start(s, a, now); break;
        case TIDE_SHARD_ACTION_STOP: stop(s, a->shard); break;
        case TIDE_SHARD_ACTION_CHECKPOINT:
            if (s->desc.checkpoint) s->desc.checkpoint(s->desc.user, a->data, a->size);
            tide_shard_coordinator_kept(s->coordinator);
            break;
        }
    }
}

// Each shard reports, the coordinator decides, and each shard does as it's told
static void round_of(tide_shards *s, const double now)
{
    for (uint32_t i = 0; i < s->room; i++) {
        if (!s->shards[i].agent) continue;
        uint32_t size = 0;
        uint8_t *report = tide_shard_agent_report(s->shards[i].agent, &size);
        if (!tide_shard_coordinator_report(s->coordinator, report, size)) {
            fprintf(stderr, "tide: shard %u's report wasn't one its coordinator takes\n", (unsigned)i);
            abort();
        }
        free(report);
    }
    tide_shard_coordinator_step(s->coordinator, now);
    act(s, now);
    for (uint32_t i = 0; i < s->room; i++) {
        if (!s->shards[i].agent) continue;
        uint32_t size = 0;
        uint8_t *instructions = tide_shard_coordinator_instructions(s->coordinator, i, &size);
        if (instructions && !tide_shard_agent_apply(s->shards[i].agent, instructions, size)) {
            fprintf(stderr, "tide: shard %u's instructions weren't any its agent takes\n", (unsigned)i);
            abort();
        }
        free(instructions);
    }
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

static tide_shard_coordinator_desc coordinator_desc(const tide_shards *s)
{
    return (tide_shard_coordinator_desc){
        .players = tide_game_players(s->game),
        .range = s->api->range,
        .dims = s->api->dims,
        .world = s->desc.world,
        .split = {s->desc.split[0], s->desc.split[1], s->desc.split[2]},
        .most_entities = s->desc.most_entities,
        .checkpoints = s->desc.checkpoint != NULL,
        .checkpoint_seconds = s->desc.checkpoint_seconds,
    };
}

tide_shards *tide_shards_create(const tide_shards_desc *desc, const double now)
{
    tide_shards *s = calloc(1, sizeof *s);
    if (!s || !setup(s, desc)) {
        free(s);
        return NULL;
    }
    const tide_shard_coordinator_desc d = coordinator_desc(s);
    s->coordinator = tide_shard_coordinator_create(&d, now);
    if (!s->coordinator) {
        free(s);
        return NULL;
    }
    act(s, now); // The root and the regions start
    return s;
}

void tide_shards_destroy(tide_shards *s)
{
    if (!s) return;
    for (uint32_t i = 0; i < s->room; i++) stop(s, i);
    free(s->shards);
    tide_shard_coordinator_destroy(s->coordinator);
    free(s);
}

void tide_shards_update(tide_shards *s, const double now)
{
    for (uint32_t i = 0; i < s->room; i++) {
        if (s->shards[i].server && !s->desc.host_start) tide_server_update(s->shards[i].server, now); // A host updates its own
    }
    // The ticks are kept before anything they sent goes on
    if (s->desc.flush) s->desc.flush(s->desc.user);
    round_of(s, now);
    // A checkpoint begun is gathered at once: every shard is here
    for (uint32_t rounds = 0; rounds < 4u && tide_shard_coordinator_checkpointing(s->coordinator); rounds++) round_of(s, now);
}

uint32_t tide_shards_count(const tide_shards *s)
{
    return tide_shard_coordinator_count(s->coordinator);
}

tide_server *tide_shards_server(const tide_shards *s, const uint32_t shard)
{
    return shard < s->room ? s->shards[shard].server : NULL;
}

const void *tide_shards_world(const tide_shards *s, const uint32_t shard)
{
    tide_server *server = tide_shards_server(s, shard);
    return server ? tide_server_world(server) : NULL;
}

bool tide_shards_region(const tide_shards *s, const uint32_t shard, tide_shard_box *out)
{
    return tide_shard_coordinator_region(s->coordinator, shard, out);
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
    return tide_shard_coordinator_owner(s->coordinator, e);
}

void tide_shards_player(tide_shards *s, const tide_player_id player, const bool joined)
{
    tide_shard_coordinator_player(s->coordinator, player, joined);
}

uint32_t tide_shards_at(const tide_shards *s, const double p[3])
{
    return tide_shard_coordinator_at(s->coordinator, p);
}

// ---------------------------------------------------------------------------
// Checkpoints (tide_shard_coordinator_save's)

static uint8_t *world_of(void *user, const uint32_t shard, uint32_t *size, uint32_t *tick)
{
    const tide_shards *s = user;
    const here *h = shard < s->room ? &s->shards[shard] : NULL;
    *size = 0;
    *tick = 0;
    if (!h || !h->server) return NULL;
    const void *world = tide_server_world(h->server);
    const uint32_t n = s->game->pack_world(world, NULL, 0);
    uint8_t *packed = malloc(n ? n : 1u);
    if (!packed) tide_out_of_memory();
    s->game->pack_world(world, packed, n);
    *size = n;
    *tick = h->ticks_before + tide_server_tick(h->server); // The tick its journal's next record is of
    return packed;
}

uint8_t *tide_shards_save(const tide_shards *s, uint32_t *size)
{
    return tide_shard_coordinator_save(s->coordinator, world_of, (void *)(uintptr_t)s, size);
}

static tide_shards *load(const tide_shards_desc *desc, const uint8_t *data, const uint32_t size,
                         const tide_shards_records records, void *records_user, const double now)
{
    tide_shards *s = calloc(1, sizeof *s);
    if (!s || !setup(s, desc)) {
        free(s);
        return NULL;
    }
    const tide_shard_coordinator_desc d = coordinator_desc(s);
    s->coordinator = tide_shard_coordinator_load(&d, data, size, now);
    if (!s->coordinator) {
        free(s);
        return NULL;
    }
    s->records = records;
    s->records_user = records_user;
    act(s, now); // Each shard from its world, and with records, run on through them
    s->records = NULL;
    s->records_user = NULL;
    if (s->failed) {
        tide_shards_destroy(s);
        return NULL;
    }
    return s;
}

tide_shards *tide_shards_load(const tide_shards_desc *desc, const uint8_t *data, const uint32_t size, const double now)
{
    return load(desc, data, size, NULL, NULL, now);
}

tide_shards *tide_shards_recover(const tide_shards_desc *desc, const uint8_t *data, const uint32_t size,
                                 const tide_shards_records records, void *records_user, const double now)
{
    return load(desc, data, size, records, records_user, now);
}

bool tide_shards_restart(tide_shards *s, const uint32_t shard, const uint8_t *data, const uint32_t size,
                         const tide_shards_records records, void *records_user, const double now)
{
    const uint8_t *world = NULL;
    uint32_t world_size = 0, tick = 0;
    if (!s || shard >= s->room || !s->shards[shard].server
        || !tide_shard_checkpoint_world(data, size, shard, &world, &world_size, &tick)) return false;
    stop(s, shard);
    s->records = records;
    s->records_user = records_user;
    s->failed = false;
    const tide_shard_action a = {.kind = TIDE_SHARD_ACTION_START, .shard = shard, .tick = tick, .world = world,
                                 .world_size = world_size};
    start(s, &a, now);
    s->records = NULL;
    s->records_user = NULL;
    if (s->failed) return false;
    tide_shard_coordinator_restarted(s->coordinator, shard);
    return true;
}

// ---------------------------------------------------------------------------
// A dedicated server's matches as shards

tide_server *tide_shards_hosting_start(void *hosting, const uint32_t shard, const tide_server_desc *desc, uint64_t *match)
{
    tide_hosting *host = hosting;
    *match = tide_hosting_start(host, desc);
    if (!*match) return NULL;
    (void)tide_hosting_keep(host, *match);
    if (shard == TIDE_SHARD_ROOT) tide_hosting_front(host, *match);
    else (void)tide_hosting_open(host, *match); // Its server takes only players the root admitted
    return tide_hosting_server(host, *match);
}

void tide_shards_hosting_stop(void *hosting, const uint32_t shard, const uint64_t match)
{
    (void)shard;
    (void)tide_hosting_stop(hosting, match);
}
