#include "tide/shard_client.h"

#include <stdlib.h>
#include <string.h>

// See tide/shard_client.h. Its clients are by shard: the first, the root's,
// as it was made, and each other as its route asks for it, with the cookie
// the root gave this machine's player, so every shard takes it as that player.

#define FOLLOWS_MOST 64u // Shards a route names that it reads

struct tide_shard_client {
    tide_shard_client_desc desc;
    tide_client **clients; // By shard
    uint32_t room;
    uint32_t home;
    uint32_t moves;
    uint64_t cookie;
    tide_player_id player;
};

static void make_room(tide_shard_client *c, const uint32_t shard)
{
    if (shard < c->room) return;
    uint32_t room = c->room ? c->room : 8u;
    while (room <= shard) room *= 2u;
    tide_client **grown = realloc(c->clients, room * sizeof *grown);
    if (!grown) tide_out_of_memory();
    memset(grown + c->room, 0, (room - c->room) * sizeof *grown);
    c->clients = grown;
    c->room = room;
}

tide_shard_client *tide_shard_client_create(const tide_shard_client_desc *desc, const double now)
{
    if (!desc || !desc->client.game || !desc->client.game->shards) return NULL;
    tide_shard_client *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->desc = *desc;
    make_room(c, TIDE_SHARD_ROOT);
    c->clients[TIDE_SHARD_ROOT] = tide_client_create(&desc->client, now);
    if (!c->clients[TIDE_SHARD_ROOT]) {
        free(c->clients);
        free(c);
        return NULL;
    }
    c->home = TIDE_SHARD_ROOT;
    return c;
}

void tide_shard_client_destroy(tide_shard_client *c)
{
    if (!c) return;
    for (uint32_t i = 0; i < c->room; i++) tide_client_destroy(c->clients[i]);
    free(c->clients);
    free(c);
}

static bool connected(const tide_client *client)
{
    return client && tide_client_status_of(client).state == TIDE_SESSION_CONNECTED;
}

// A client of `shard`, as the same player
static void follow(tide_shard_client *c, const tide_shard_link link, const double now)
{
    make_room(c, link.shard);
    if (c->clients[link.shard] || !c->desc.reach) return;
    tide_client_desc d = c->desc.client;
    if (!c->desc.reach(c->desc.user, link.shard, link.match, &d.transport, &d.server)) return;
    d.cookie = c->cookie;
    d.events = NULL; // Only its home's hears for local handlers (the host's)
    c->clients[link.shard] = tide_client_create(&d, now);
}

void tide_shard_client_update(tide_shard_client *c, const double now)
{
    for (uint32_t i = 0; i < c->room; i++) {
        if (c->clients[i]) tide_client_update(c->clients[i], now);
    }
    tide_client *home = c->clients[c->home];
    if (!connected(home)) return;
    const tide_client_status status = tide_client_status_of(home);
    if (!c->cookie) {
        c->cookie = status.cookie;
        c->player = status.player;
    }
    const void *world = tide_client_verified_world(home);
    if (!world) return;
    tide_shard_link links[FOLLOWS_MOST];
    uint32_t next_home = c->home;
    const uint32_t count = c->desc.client.game->shards->route(world, c->player, &next_home, links, FOLLOWS_MOST);
    if (!count) return; // No route for it yet
    const uint32_t n = count < FOLLOWS_MOST ? count : FOLLOWS_MOST;
    for (uint32_t i = 0; i < n; i++) follow(c, links[i], now);
    // Its home moves once it plays in the next one: then views draw that one
    if (next_home != c->home && next_home < c->room && connected(c->clients[next_home])) {
        c->home = next_home;
        c->moves++;
    }
    // What its route no longer names goes, but its home
    for (uint32_t i = 0; i < c->room; i++) {
        if (!c->clients[i] || i == c->home) continue;
        bool named = false;
        for (uint32_t k = 0; k < n && !named; k++) named = links[k].shard == i;
        if (named) continue;
        tide_client_destroy(c->clients[i]);
        c->clients[i] = NULL;
    }
}

tide_client *tide_shard_client_home(const tide_shard_client *c)
{
    return c->clients[c->home];
}

uint32_t tide_shard_client_home_shard(const tide_shard_client *c)
{
    return c->home;
}

tide_client *tide_shard_client_session(const tide_shard_client *c, const uint32_t shard)
{
    return shard < c->room ? c->clients[shard] : NULL;
}

uint32_t tide_shard_client_moves(const tide_shard_client *c)
{
    return c->moves;
}
