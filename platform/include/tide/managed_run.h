#pragma once

// Include once, after the generated game header, in a WASM reactor. One
// instance owns one match. The trusted service owns routing and admission.
#include "tide/hosting.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef __wasm__
#error "managed_run.h is for sandboxed WASM server builds"
#endif
#define TIDE_MANAGED_EXPORT(name) __attribute__((export_name(#name)))
__attribute__((import_module("tide_host"), import_name("send")))
extern void tide_managed_send(uint32_t peer, const void *data, uint32_t size);
__attribute__((import_module("tide_host"), import_name("entropy")))
extern uint64_t tide_managed_entropy(void);
extern void tide_server_entry(tide_hosting_desc *host) __attribute__((weak));
// Optional game-owned durable payload. Both callbacks must be supplied together.
// The default is an exact-release world checkpoint, with no upgrade guarantee.
extern uint32_t tide_managed_write_save(const void *world, uint8_t *out, uint32_t capacity) __attribute__((weak));
extern bool tide_managed_read_save(void *world, const uint8_t *data, uint32_t size) __attribute__((weak));

static tide_server *tide_managed_server;
static tide_server_desc tide_managed_description;
static double tide_managed_now;
static bool tide_managed_ended;
static struct {
    uint32_t peer, size;
    uint8_t data[TIDE_NET_MTU];
} tide_managed_packets[64];
static uint32_t tide_managed_first, tide_managed_count;

static void tide_managed_transport_send(void *self, const tide_address to, const void *data, const uint32_t size)
{
    (void)self;
    if (to.kind == TIDE_ADDRESS_NETWORK && to.host && size <= TIDE_NET_MTU)
        tide_managed_send(to.host, data, size);
}
static uint32_t tide_managed_transport_receive(void *self, tide_address *from, void *data, const uint32_t capacity)
{
    (void)self;
    if (!tide_managed_count) return 0;
    const uint32_t at = tide_managed_first;
    tide_managed_first = (at + 1u) % 64u;
    tide_managed_count--;
    const uint32_t size = tide_managed_packets[at].size;
    if (size > capacity) return 0;
    *from = (tide_address){.kind = TIDE_ADDRESS_NETWORK, .host = tide_managed_packets[at].peer};
    memcpy(data, tide_managed_packets[at].data, size);
    return size;
}
static void tide_managed_transport_close(void *self) { (void)self; }
static void tide_managed_transport_end(void *self) { (void)self; tide_managed_ended = true; }
static double tide_managed_clock(void *self) { (void)self; return tide_managed_now; }

TIDE_MANAGED_EXPORT(tide_managed_abi) uint32_t tide_managed_abi(void) { return 1; }
TIDE_MANAGED_EXPORT(tide_managed_alloc) void *tide_managed_alloc(const uint32_t size) { return malloc(size); }
TIDE_MANAGED_EXPORT(tide_managed_free) void tide_managed_free(void *p) { free(p); }
TIDE_MANAGED_EXPORT(tide_managed_simulation) const char *tide_managed_simulation(void) { return TIDE_SIMULATION_ID; }

TIDE_MANAGED_EXPORT(tide_managed_init)
uint32_t tide_managed_init(const double now, const uint32_t seed, const uint8_t *parameters,
                          const uint32_t size, const uint8_t *pack, const uint32_t pack_size)
{
    if (tide_managed_server || size > TIDE_HOSTING_PARAMETER_BYTES) return 0;
#if defined(TIDE_HAS_ASSETS) && TIDE_SERVER_HAS_ASSETS
    if (!pack_size || !tide_assets_load(pack, pack_size)) return 0;
#else
    (void)pack; (void)pack_size;
#endif
    tide_hosting_desc host = {.game = &tide_game_api};
    if (tide_server_entry) tide_server_entry(&host);
    // Native process orchestration has no equivalent inside one match sandbox.
    if (host.ready || host.poll || host.authorize || host.secure_peer) {
        fputs("Managed matches use project admission policy and the trusted HTTP API; native authorize/ready/poll hooks are unsupported.\n", stderr);
        return 0;
    }
    if ((tide_managed_write_save != NULL) != (tide_managed_read_save != NULL)) {
        fputs("Define both tide_managed_write_save and tide_managed_read_save, or neither.\n", stderr); return 0;
    }
#ifdef TIDE_MAIN_IS_LOCAL
    if (!host.start) {
        fputs("Main is local; define tide_server_entry and select a match scene in its start callback.\n", stderr); return 0;
    }
#endif
    tide_server_desc *d = &tide_managed_description;
    *d = (tide_server_desc){.seed = seed};
    const tide_hosting_request request = {.parameters = parameters, .parameter_size = size};
    if (host.start && !host.start(host.user, &request, d)) return 0;
    d->game = &tide_game_api;
    d->jobs = NULL;
    d->entropy = tide_managed_entropy;
    d->transports[0] = (tide_transport){0};
    d->transports[1] = (tide_transport){.send = tide_managed_transport_send, .receive = tide_managed_transport_receive,
        .close = tide_managed_transport_close, .end = tide_managed_transport_end,
        .clock = tide_managed_clock, .arrived = tide_managed_clock};
    tide_managed_now = now;
    tide_managed_server = tide_server_create(d, now);
    d->start = NULL; // The callback's start value need not survive creation.
    return tide_managed_server != NULL;
}

TIDE_MANAGED_EXPORT(tide_managed_receive)
void tide_managed_receive(const uint32_t peer, const uint8_t *data, const uint32_t size, const double now)
{
    (void)now;
    if (!peer || !size || size > TIDE_NET_MTU || tide_managed_count == 64) return;
    const uint32_t at = (tide_managed_first + tide_managed_count++) % 64u;
    tide_managed_packets[at].peer = peer;
    tide_managed_packets[at].size = size;
    memcpy(tide_managed_packets[at].data, data, size);
}
TIDE_MANAGED_EXPORT(tide_managed_update) void tide_managed_update(const double now)
{
    tide_managed_now = now;
    if (tide_managed_server) tide_server_update(tide_managed_server, now);
}
TIDE_MANAGED_EXPORT(tide_managed_status) uint32_t tide_managed_status(void)
{
    return tide_managed_server ? tide_server_player_count(tide_managed_server)
        | (tide_managed_ended || (tide_game_api.ended && tide_game_api.ended(tide_server_world(tide_managed_server)))
            ? UINT32_C(0x40000000) : 0u) : 0u;
}

// Versioned envelope belongs to this exact WASM release. Authentication and
// player-cookie metadata remain outside the game's serialized world/payload.
typedef struct tide_managed_checkpoint {
    uint32_t magic, size;
    tide_player_set players, leaving;
    uint64_t digests[TIDE_GAME_PLAYERS];
    uint8_t identities[TIDE_GAME_PLAYERS][32];
    uint32_t tick_rate, seed;
    float dt;
    bool closed, started;
} tide_managed_checkpoint;

TIDE_MANAGED_EXPORT(tide_managed_save)
uint32_t tide_managed_save(uint8_t *out, const uint32_t capacity)
{
    if (!tide_managed_server) return 0;
    const void *world = tide_server_world(tide_managed_server);
    uint32_t (*write)(const void *, uint8_t *, uint32_t) = tide_managed_write_save
        ? tide_managed_write_save : tide_game_api.pack_world;
    const uint32_t size = write(world, NULL, 0);
    if (!size || size > UINT32_MAX - sizeof(tide_managed_checkpoint)) return 0;
    const uint32_t total = size + (uint32_t)sizeof(tide_managed_checkpoint);
    if (!out || capacity < total) return total;
    tide_managed_checkpoint *checkpoint = calloc(1, sizeof *checkpoint);
    if (!checkpoint) return 0;
    checkpoint->magic = UINT32_C(0x324d4454);
    checkpoint->size = size;
    tide_server_recovery recovery = {.digests = checkpoint->digests, .identities = checkpoint->identities};
    tide_server_recovery_state(tide_managed_server, &recovery);
    checkpoint->players = recovery.players;
    checkpoint->leaving = recovery.leaving;
    checkpoint->tick_rate = recovery.tick_rate;
    checkpoint->seed = recovery.seed;
    checkpoint->dt = recovery.dt;
    checkpoint->closed = recovery.closed;
    checkpoint->started = recovery.started;
    memcpy(out, checkpoint, sizeof *checkpoint);
    free(checkpoint);
    return write(world, out + sizeof(tide_managed_checkpoint), size) == size ? total : 0;
}

TIDE_MANAGED_EXPORT(tide_managed_restore)
uint32_t tide_managed_restore(const uint8_t *data, const uint32_t size, const double now)
{
    if (!tide_managed_server || size < sizeof(tide_managed_checkpoint)) return 0;
    tide_managed_checkpoint *checkpoint = malloc(sizeof *checkpoint);
    if (!checkpoint) return 0;
    memcpy(checkpoint, data, sizeof *checkpoint);
    if (checkpoint->magic != UINT32_C(0x324d4454) || checkpoint->size != size - sizeof *checkpoint
        || !checkpoint->tick_rate || checkpoint->tick_rate > 1000
        || !(checkpoint->dt > 0.0f && checkpoint->dt <= 1.0f)
        || checkpoint->players.words > TIDE_PLAYER_SET_BITS / 64u
        || checkpoint->leaving.words > TIDE_PLAYER_SET_BITS / 64u) { free(checkpoint); return 0; }
    void *world = calloc(1, tide_game_api.world_size);
    if (!world) { free(checkpoint); return 0; }
    bool (*read)(void *, const uint8_t *, uint32_t) = tide_managed_read_save
        ? tide_managed_read_save : tide_game_api.unpack_world;
    const bool loaded = read(world, data + sizeof *checkpoint, checkpoint->size);
    tide_server *restored = NULL;
    if (loaded) {
        tide_server_desc d = tide_managed_description;
        d.world = world;
        d.players = checkpoint->players;
        d.leaving = checkpoint->leaving;
        d.digests = checkpoint->digests;
        d.identities = (const uint8_t (*)[32])checkpoint->identities;
        d.closed = checkpoint->closed;
        d.tick_rate = checkpoint->tick_rate;
        d.seed = checkpoint->seed;
        d.wait_for_first = !checkpoint->started;
        d.dt = checkpoint->dt;
        tide_managed_now = now;
        restored = tide_server_create(&d, now);
    }
    free(checkpoint);
    if (tide_game_api.free_world) tide_game_api.free_world(world);
    free(world);
    if (!restored) return 0;
    tide_server_destroy(tide_managed_server);
    tide_managed_server = restored;
    tide_managed_first = tide_managed_count = 0;
    tide_managed_ended = false;
    return 1;
}

#ifdef TIDE_GAME_SHARDED
// ---------------------------------------------------------------------------
// Shards (docs/spec.md, Shards): an instance of a sharded game runs one shard
// of a world, a match like any (its update, receive, status, save and restore
// are the ones above), with its agent (tide/shard_agent.h); or the world's
// coordinator (tide/shard_coordinator.h). The service carries their reports
// and instructions between them. Bytes they give the host are in
// tide_managed_bytes() until the next call that gives any.
#include "tide/shard_agent.h"
#include "tide/shard_coordinator.h"

// The shard's journal (tide_server_desc.journal): each tick's record, which
// the host keeps before anything the shard reports goes on.
__attribute__((import_module("tide_host"), import_name("journal")))
extern void tide_managed_journal(uint32_t tick, const void *data, uint32_t size);

static tide_shard_agent *tide_managed_agent;
static uint32_t tide_managed_ticks_before; // Its ticks before this server's first, which its journal numbers on from
static void *tide_managed_staged;          // A world being made from a checkpoint and its records
static uint32_t tide_managed_staged_next;
static tide_shard_coordinator *tide_managed_coordinator;
static uint8_t *tide_managed_out;

TIDE_MANAGED_EXPORT(tide_managed_bytes) const uint8_t *tide_managed_bytes(void) { return tide_managed_out; }

static uint32_t tide_managed_give(uint8_t *bytes, const uint32_t size)
{
    free(tide_managed_out);
    tide_managed_out = bytes;
    return bytes ? size : 0u;
}

static void tide_managed_record(void *user, const uint32_t tick, const uint8_t *record, const uint32_t size)
{
    (void)user;
    const uint32_t on = tide_managed_ticks_before + tick;
    if (!tide_managed_ticks_before || size < 4u) {
        tide_managed_journal(on, record, size);
        return;
    }
    uint8_t *numbered = malloc(size);
    if (!numbered) tide_out_of_memory();
    memcpy(numbered, record, size);
    for (uint32_t k = 0; k < 4u; k++) numbered[k] = (uint8_t)(on >> (8u * k));
    tide_managed_journal(on, numbered, size);
    free(numbered);
}

static bool tide_managed_shard_assets(const uint8_t *pack, const uint32_t pack_size)
{
#if defined(TIDE_HAS_ASSETS) && TIDE_SERVER_HAS_ASSETS
    return pack_size && tide_assets_load(pack, pack_size);
#else
    (void)pack;
    (void)pack_size;
    return true;
#endif
}

// A shard's server from `world`, and its agent
static uint32_t tide_managed_shard_serve(const double now, const uint32_t seed, const uint32_t shard, const void *world,
                                         const bool fresh)
{
    tide_server_desc *d = &tide_managed_description;
    *d = (tide_server_desc){.seed = seed};
    d->game = &tide_game_api;
    d->world = world;
    d->closed = shard != TIDE_SHARD_ROOT; // Players join the world at the root, and a region takes those it's told of
    d->entropy = tide_managed_entropy;
    d->transports[1] = (tide_transport){.send = tide_managed_transport_send, .receive = tide_managed_transport_receive,
        .close = tide_managed_transport_close, .end = tide_managed_transport_end,
        .clock = tide_managed_clock, .arrived = tide_managed_clock};
    d->journal = tide_managed_record;
    tide_managed_now = now;
    tide_managed_server = tide_server_create(d, now);
    d->world = NULL;
    if (!tide_managed_server) return 0;
    tide_managed_agent = tide_shard_agent_create(&tide_game_api, tide_managed_server, shard, tide_managed_ticks_before, fresh);
    return tide_managed_agent != NULL;
}

// A shard started anew from its CONFIG message, at the world's tick
TIDE_MANAGED_EXPORT(tide_managed_shard_new)
uint32_t tide_managed_shard_new(const double now, const uint32_t seed, const uint32_t shard, const uint32_t tick,
                                const uint8_t *config, const uint32_t config_size, const uint8_t *pack, const uint32_t pack_size)
{
    if (tide_managed_server || tide_managed_coordinator || !tide_managed_shard_assets(pack, pack_size)) return 0;
    tide_hosting_desc host = {.game = &tide_game_api};
    if (tide_server_entry) tide_server_entry(&host);
    tide_server_desc chosen = {.seed = seed};
    const tide_hosting_request request = {0};
    if (shard == TIDE_SHARD_ROOT && host.start && !host.start(host.user, &request, &chosen)) return 0;
    void *world = calloc(1, tide_game_api.world_size);
    if (!world) return 0;
    const float dt = 1.0f / (float)(tide_game_api.tick_rate ? tide_game_api.tick_rate : 60u);
    tide_game_api.shards->start(world, dt, shard == TIDE_SHARD_ROOT ? chosen.start : NULL, seed, tick, config, config_size);
    tide_managed_ticks_before = 0;
    const uint32_t made = tide_managed_shard_serve(now, seed, shard, world, false);
    if (tide_game_api.free_world) tide_game_api.free_world(world);
    free(world);
    return made;
}

// A shard from a checkpoint: its world (tide_managed_shard_stage), then each
// journal record kept since (tide_managed_shard_replay), then it serves
// (tide_managed_shard_begin)
TIDE_MANAGED_EXPORT(tide_managed_shard_stage)
uint32_t tide_managed_shard_stage(const uint8_t *world, const uint32_t size, const uint32_t tick, const uint8_t *pack,
                                  const uint32_t pack_size)
{
    if (tide_managed_server || tide_managed_staged || !tide_managed_shard_assets(pack, pack_size)) return 0;
    tide_managed_staged = calloc(1, tide_game_api.world_size);
    if (!tide_managed_staged) return 0;
    if (!tide_game_api.unpack_world(tide_managed_staged, world, size)) {
        if (tide_game_api.free_world) tide_game_api.free_world(tide_managed_staged);
        free(tide_managed_staged);
        tide_managed_staged = NULL;
        return 0;
    }
    tide_managed_staged_next = tick;
    return 1;
}

TIDE_MANAGED_EXPORT(tide_managed_shard_replay)
uint32_t tide_managed_shard_replay(const uint8_t *record, const uint32_t size)
{
    uint32_t tick;
    if (!tide_managed_staged || !tide_replay_tick(record, size, &tick)) return 0;
    if (tick < tide_managed_staged_next) return 1; // From before the checkpoint
    if (tick != tide_managed_staged_next || !tide_replay(&tide_game_api, tide_managed_staged, record, size, NULL)) return 0;
    tide_managed_staged_next++;
    return 1;
}

TIDE_MANAGED_EXPORT(tide_managed_shard_begin)
uint32_t tide_managed_shard_begin(const double now, const uint32_t seed, const uint32_t shard)
{
    if (!tide_managed_staged) return 0;
    tide_managed_ticks_before = tide_managed_staged_next;
    const uint32_t made = tide_managed_shard_serve(now, seed, shard, tide_managed_staged, true);
    if (tide_game_api.free_world) tide_game_api.free_world(tide_managed_staged);
    free(tide_managed_staged);
    tide_managed_staged = NULL;
    return made;
}

// How busy the shard is, as its host measures it (tide_shard_agent_busy):
// the share of the time its updates take of late
TIDE_MANAGED_EXPORT(tide_managed_shard_busy) void tide_managed_shard_busy(const float busy)
{
    tide_shard_agent_busy(tide_managed_agent, busy);
}

// Its report, after an update: its size, the bytes in tide_managed_bytes()
TIDE_MANAGED_EXPORT(tide_managed_shard_report) uint32_t tide_managed_shard_report(void)
{
    uint32_t size = 0;
    return tide_managed_agent ? tide_managed_give(tide_shard_agent_report(tide_managed_agent, &size), size) : 0u;
}

TIDE_MANAGED_EXPORT(tide_managed_shard_apply)
uint32_t tide_managed_shard_apply(const uint8_t *data, const uint32_t size)
{
    return tide_managed_agent && tide_shard_agent_apply(tide_managed_agent, data, size);
}

// The coordinator, from its description: 48 bytes the world's box, 3 u32 its
// split, u32 most entities, u8 whether it makes checkpoints, f64 every how
// many seconds, f32 the busiest a shard should be
static bool tide_managed_coordinator_desc(const uint8_t *data, const uint32_t size, tide_shard_coordinator_desc *out)
{
    tide_reader r = {data, size, 0, false};
    const uint8_t *lo = tide_read_bytes(&r, 24u), *hi = tide_read_bytes(&r, 24u);
    if (!lo || !hi || !tide_game_api.shards) return false;
    *out = (tide_shard_coordinator_desc){.players = tide_game_players(&tide_game_api), .range = tide_game_api.shards->range,
                                         .dims = tide_game_api.shards->dims};
    memcpy(out->world.lo, lo, 24u);
    memcpy(out->world.hi, hi, 24u);
    for (uint32_t k = 0; k < 3u; k++) out->split[k] = tide_read_u32(&r);
    out->most_entities = tide_read_u32(&r);
    out->checkpoints = tide_read_u8(&r) != 0;
    const uint64_t seconds = tide_read_u64(&r);
    memcpy(&out->checkpoint_seconds, &seconds, sizeof seconds);
    const uint32_t busiest = tide_read_u32(&r);
    memcpy(&out->busiest, &busiest, sizeof busiest);
    return !r.failed && r.at == r.size;
}

TIDE_MANAGED_EXPORT(tide_managed_coordinator_create)
uint32_t tide_managed_coordinator_create(const double now, const uint8_t *desc, const uint32_t size)
{
    tide_shard_coordinator_desc d;
    if (tide_managed_server || tide_managed_coordinator || !tide_managed_coordinator_desc(desc, size, &d)) return 0;
    tide_managed_coordinator = tide_shard_coordinator_create(&d, now);
    return tide_managed_coordinator != NULL;
}

TIDE_MANAGED_EXPORT(tide_managed_coordinator_load)
uint32_t tide_managed_coordinator_load(const double now, const uint8_t *desc, const uint32_t desc_size, const uint8_t *data,
                                       const uint32_t size)
{
    tide_shard_coordinator_desc d;
    if (tide_managed_server || tide_managed_coordinator || !tide_managed_coordinator_desc(desc, desc_size, &d)) return 0;
    tide_managed_coordinator = tide_shard_coordinator_load(&d, data, size, now);
    return tide_managed_coordinator != NULL;
}

TIDE_MANAGED_EXPORT(tide_managed_coordinator_report)
uint32_t tide_managed_coordinator_report(const uint8_t *report, const uint32_t size)
{
    return tide_shard_coordinator_report(tide_managed_coordinator, report, size);
}

TIDE_MANAGED_EXPORT(tide_managed_coordinator_step) void tide_managed_coordinator_step(const double now)
{
    tide_shard_coordinator_step(tide_managed_coordinator, now);
}

// Its actions since the last step, as bytes: u32 count, then each u8 kind
// (tide_shard_action_kind), u32 shard, u32 tick, and u32 size and bytes of its
// config, its world and its data
TIDE_MANAGED_EXPORT(tide_managed_coordinator_actions) uint32_t tide_managed_coordinator_actions(void)
{
    const tide_shard_action *actions;
    const uint32_t count = tide_shard_coordinator_actions(tide_managed_coordinator, &actions);
    tide_writer w = {.grows = true};
    tide_write_u32(&w, count);
    for (uint32_t i = 0; i < count; i++) {
        tide_write_u8(&w, (uint8_t)actions[i].kind);
        tide_write_u32(&w, actions[i].shard);
        tide_write_u32(&w, actions[i].tick);
        tide_write_u32(&w, actions[i].config_size);
        tide_write_bytes(&w, actions[i].config, actions[i].config_size);
        tide_write_u32(&w, actions[i].world_size);
        tide_write_bytes(&w, actions[i].world, actions[i].world_size);
        tide_write_u32(&w, actions[i].size);
        tide_write_bytes(&w, actions[i].data, actions[i].size);
    }
    if (w.overflow) tide_out_of_memory();
    return tide_managed_give(w.data, w.size);
}

TIDE_MANAGED_EXPORT(tide_managed_coordinator_instructions)
uint32_t tide_managed_coordinator_instructions(const uint32_t shard)
{
    uint32_t size = 0;
    return tide_managed_give(tide_shard_coordinator_instructions(tide_managed_coordinator, shard, &size), size);
}

TIDE_MANAGED_EXPORT(tide_managed_coordinator_kept) void tide_managed_coordinator_kept(void)
{
    tide_shard_coordinator_kept(tide_managed_coordinator);
}

TIDE_MANAGED_EXPORT(tide_managed_coordinator_restarted) void tide_managed_coordinator_restarted(const uint32_t shard)
{
    tide_shard_coordinator_restarted(tide_managed_coordinator, shard);
}

// Each shard's tick in a checkpoint, from which its journal's records are
// still needed: u32 count, then each u32 shard and u32 tick
TIDE_MANAGED_EXPORT(tide_managed_checkpoint_ticks)
uint32_t tide_managed_checkpoint_ticks(const uint8_t *data, const uint32_t size, const uint32_t shards)
{
    tide_writer w = {.grows = true};
    const uint32_t at = w.size;
    tide_write_u32(&w, 0u);
    uint32_t count = 0;
    for (uint32_t shard = 0; shard < shards; shard++) {
        const uint8_t *world = NULL;
        uint32_t world_size = 0, tick = 0;
        if (!tide_shard_checkpoint_world(data, size, shard, &world, &world_size, &tick)) continue;
        tide_write_u32(&w, shard);
        tide_write_u32(&w, tick);
        count++;
    }
    if (w.overflow) tide_out_of_memory();
    tide_shard_patch_u32(w.data + at, count);
    return tide_managed_give(w.data, w.size);
}

// One shard's world in a checkpoint, to start it again elsewhere: u32 the tick
// its journal goes on from, then its world; 0 for none
TIDE_MANAGED_EXPORT(tide_managed_checkpoint_world)
uint32_t tide_managed_checkpoint_world(const uint8_t *data, const uint32_t size, const uint32_t shard)
{
    const uint8_t *world = NULL;
    uint32_t world_size = 0, tick = 0;
    if (!tide_shard_checkpoint_world(data, size, shard, &world, &world_size, &tick)) return tide_managed_give(NULL, 0);
    tide_writer w = {.grows = true};
    tide_write_u32(&w, tick);
    tide_write_bytes(&w, world, world_size);
    if (w.overflow) tide_out_of_memory();
    return tide_managed_give(w.data, w.size);
}

TIDE_MANAGED_EXPORT(tide_managed_coordinator_checkpointing) uint32_t tide_managed_coordinator_checkpointing(void)
{
    return tide_shard_coordinator_checkpointing(tide_managed_coordinator);
}

TIDE_MANAGED_EXPORT(tide_managed_coordinator_count) uint32_t tide_managed_coordinator_count(void)
{
    return tide_shard_coordinator_count(tide_managed_coordinator);
}

// The match its host knows a shard by, below 2^48, as a double
TIDE_MANAGED_EXPORT(tide_managed_coordinator_set_match)
void tide_managed_coordinator_set_match(const uint32_t shard, const double match)
{
    if (match >= 0.0 && match < 281474976710656.0)
        tide_shard_coordinator_set_match(tide_managed_coordinator, shard, (uint64_t)match);
}
#endif
