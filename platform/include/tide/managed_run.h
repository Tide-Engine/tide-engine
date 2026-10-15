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
    tide_server_recovery recovery;
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
    tide_managed_checkpoint checkpoint = {.magic = UINT32_C(0x314d4454), .size = size};
    tide_server_recovery_state(tide_managed_server, &checkpoint.recovery);
    memcpy(out, &checkpoint, sizeof checkpoint);
    return write(world, out + sizeof checkpoint, size) == size ? total : 0;
}

TIDE_MANAGED_EXPORT(tide_managed_restore)
uint32_t tide_managed_restore(const uint8_t *data, const uint32_t size, const double now)
{
    if (!tide_managed_server || size < sizeof(tide_managed_checkpoint)) return 0;
    tide_managed_checkpoint checkpoint;
    memcpy(&checkpoint, data, sizeof checkpoint);
    if (checkpoint.magic != UINT32_C(0x314d4454) || checkpoint.size != size - sizeof checkpoint
        || !checkpoint.recovery.tick_rate || checkpoint.recovery.tick_rate > 1000
        || !(checkpoint.recovery.dt > 0.0f && checkpoint.recovery.dt <= 1.0f)) return 0;
    void *world = calloc(1, tide_game_api.world_size);
    if (!world) return 0;
    bool (*read)(void *, const uint8_t *, uint32_t) = tide_managed_read_save
        ? tide_managed_read_save : tide_game_api.unpack_world;
    const bool loaded = read(world, data + sizeof checkpoint, checkpoint.size);
    tide_server *restored = NULL;
    if (loaded) {
        tide_server_desc d = tide_managed_description;
        d.world = world;
        d.players = checkpoint.recovery.players;
        d.leaving = checkpoint.recovery.leaving;
        d.digests = checkpoint.recovery.digests;
        d.identities = (const uint8_t (*)[32])checkpoint.recovery.identities;
        d.closed = checkpoint.recovery.closed;
        d.tick_rate = checkpoint.recovery.tick_rate;
        d.seed = checkpoint.recovery.seed;
        d.wait_for_first = !checkpoint.recovery.started;
        d.dt = checkpoint.recovery.dt;
        tide_managed_now = now;
        restored = tide_server_create(&d, now);
    }
    if (tide_game_api.free_world) tide_game_api.free_world(world);
    free(world);
    if (!restored) return 0;
    tide_server_destroy(tide_managed_server);
    tide_managed_server = restored;
    tide_managed_first = tide_managed_count = 0;
    tide_managed_ended = false;
    return 1;
}
