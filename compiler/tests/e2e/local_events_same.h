#pragma once

// The match of local_events_same.tide and of local_events_plain.tide, which is
// that game without its events for local code and their local handlers. Such
// events cost the match nothing, so both games' worlds are the same, and so is
// what their machines send each other. Each game's test plays these scripts
// and checks what comes out against the numbers below: the world's size, its
// hash, its bytes (tide_world_pack), what a player who joins downloads (a
// whole delta), a delta from an earlier tick, and the bytes a server and two
// players send in a match of three seconds.
//
// A world's bytes start with its struct, whose storage has pointers in it, so
// they're one thing where pointers are 8 bytes and another where they're 4
// (the web). When the engine changes how worlds are kept or sent on purpose,
// the tests print what they got: both games have to print the same.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

#define SAME_HASH UINT64_C(0x5945DBD91B407964)
#define SAME_SERVER_SENT 29132u
// Protocol 15 adds a 32-byte simulation ID to each of the two HELLOs.
#define SAME_CLIENTS_SENT 11108u
#define SAME_WHOLE_SIZE 202u
#define SAME_WHOLE UINT64_C(0x5B30CD0C096F89F9)
#define SAME_DELTA_SIZE 91u
#define SAME_DELTA UINT64_C(0xFD5ADE706615CDC4)
#if UINTPTR_MAX > 0xFFFFFFFFu
#define SAME_WORLD_SIZE 296u
#define SAME_PACKED_SIZE 538u
#define SAME_PACKED UINT64_C(0x41ED2EC44708D9F2)
#else
#define SAME_WORLD_SIZE 264u
#define SAME_PACKED_SIZE 506u
#define SAME_PACKED UINT64_C(0xC786A576CF21D572)
#endif

static uint64_t same_bytes_hash(const uint8_t *bytes, const uint32_t size)
{
    uint64_t h = UINT64_C(0xCBF29CE484222325); // FNV-1a
    for (uint32_t i = 0; i < size; i++) h = (h ^ bytes[i]) * UINT64_C(0x100000001B3);
    return h;
}

// The second player pushes now and then.
static float same_push(const uint32_t tick)
{
    return tick % 40u < 6u ? 1.0f : tick % 70u == 3u ? -2.0f : 0.0f;
}

// Plays 240 ticks on `w`, each run by `tick`, and checks the world it ends
// with.
static void same_match_check(tide_world *w, tide_world *base, void (*tick)(tide_world *w))
{
    tide_world_init(w, 1.0f / 60.0f);
    tide_world_player_joined(w, tide_player_from_index(0));
    tide_world_player_joined(w, tide_player_from_index(1));
    for (uint32_t t = 0; t < 240u; t++) {
        tide_world_set_input(w, tide_player_from_index(1), (Controls){.push = same_push(t)});
        if (t == 100u) tide_world_copy(base, w);
        tick(w);
    }
    const uint64_t hash = tide_world_hash(w);
    const uint32_t packed_size = tide_world_pack(w, NULL, 0);
    uint8_t *packed = malloc(packed_size);
    tide_world_pack(w, packed, packed_size);
    uint32_t whole_size;
    uint8_t *whole = tide_world_pack_delta(w, NULL, NULL, 0, &whole_size);
    uint32_t delta_size;
    uint8_t *delta = tide_world_pack_delta(w, base, NULL, 0, &delta_size);
    const uint64_t packed_hash = same_bytes_hash(packed, packed_size);
    const uint64_t whole_hash = same_bytes_hash(whole, whole_size);
    const uint64_t delta_hash = same_bytes_hash(delta, delta_size);
    const bool same = sizeof *w == SAME_WORLD_SIZE && hash == SAME_HASH && packed_size == SAME_PACKED_SIZE
                   && packed_hash == SAME_PACKED && whole_size == SAME_WHOLE_SIZE && whole_hash == SAME_WHOLE
                   && delta_size == SAME_DELTA_SIZE && delta_hash == SAME_DELTA;
    if (!same) {
        printf("    world %uu\n    hash 0x%016llX\n    packed %uu 0x%016llX\n    whole %uu 0x%016llX\n    delta %uu 0x%016llX\n",
               (unsigned)sizeof *w, (unsigned long long)hash, (unsigned)packed_size, (unsigned long long)packed_hash,
               (unsigned)whole_size, (unsigned long long)whole_hash, (unsigned)delta_size, (unsigned long long)delta_hash);
    }
    TIDE_CHECK(same);
    TIDE_CHECK(w->Field.bounces > 4); // Balls did bounce, so the game with events did send them
    free(packed);
    free(whole);
    free(delta);
}

// A transport that counts what goes out through it.
typedef struct same_counted {
    tide_transport inner;
    uint64_t sent;
} same_counted;

static void same_send(void *self, const tide_address to, const void *data, const uint32_t size)
{
    same_counted *c = self;
    c->sent += size;
    c->inner.send(c->inner.self, to, data, size);
}

static uint32_t same_receive(void *self, tide_address *from, void *data, const uint32_t capacity)
{
    same_counted *c = self;
    return c->inner.receive(c->inner.self, from, data, capacity);
}

static void same_close(void *self)
{
    same_counted *c = self;
    if (c->inner.close) c->inner.close(c->inner.self);
}

static tide_transport same_count(same_counted *c, const tide_transport inner)
{
    *c = (same_counted){.inner = inner};
    return (tide_transport){.self = c, .send = same_send, .receive = same_receive, .close = same_close};
}

static int same_who[2] = {0, 1};

static void same_sample(void *user, const uint32_t tick, void *input)
{
    *(Controls *)input = (Controls){.push = *(const int *)user == 1 ? same_push(tick) : 0.0f};
}

// A server and two players on a loopback network with latency, for three
// seconds of made-up time: the first player's machine hears the match's
// events into `events`, if it's given a log, and `frame` runs after each of
// its updates. What the machines sent is what the same match sends without
// them.
static void same_traffic_check(tide_event_log *events, void (*frame)(const tide_client *first))
{
    static same_counted counted[3];
    double now = 0.0;
    tide_loopback *net = tide_loopback_create(77);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.04});
    const tide_server_desc desc = {.game = &tide_game_api, .tick_rate = 60,
                                   .transports = {same_count(&counted[0], tide_loopback_endpoint(net, 1))}};
    tide_server *server = tide_server_create(&desc, now);
    tide_client *clients[2];
    for (int i = 0; i < 2; i++) {
        const tide_client_desc client = {
            .game = &tide_game_api,
            .transport = same_count(&counted[i + 1], tide_loopback_endpoint(net, (uint32_t)i + 2u)),
            .server = tide_loopback_address(1),
            .sample = same_sample,
            .user = &same_who[i],
            .lead = 2,
            .events = i == 0 ? events : NULL,
        };
        clients[i] = tide_client_create(&client, now);
    }
    bool odd = false;
    while (now < 3.0) {
        now += (odd = !odd) ? 0.011 : 0.022;
        tide_loopback_set_time(net, now);
        for (int i = 0; i < 2; i++) tide_client_update(clients[i], now);
        tide_server_update(server, now);
        if (frame) frame(clients[0]);
    }
    const uint64_t server_sent = counted[0].sent;
    const uint64_t clients_sent = counted[1].sent + counted[2].sent;
    const tide_world *match = tide_server_world(server);
    TIDE_CHECK(match->Time.tick > 150 && match->Field.bounces > 6);
    TIDE_CHECK(tide_client_status_of(clients[0]).again_ticks > 0); // It did predict the pushes wrong, and ran ticks again
    for (int i = 0; i < 2; i++) TIDE_CHECK(tide_client_status_of(clients[i]).resyncs == 0);
    const bool same = server_sent == SAME_SERVER_SENT && clients_sent == SAME_CLIENTS_SENT;
    if (!same) printf("    server sent %lluu\n    clients sent %lluu\n", (unsigned long long)server_sent, (unsigned long long)clients_sent);
    TIDE_CHECK(same);
    for (int i = 0; i < 2; i++) tide_client_destroy(clients[i]);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}
