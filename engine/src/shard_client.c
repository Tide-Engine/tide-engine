#include "tide/shard_client.h"

#include <stdlib.h>
#include <string.h>

// See tide/shard_client.h. Its clients are by shard: the first, the root's,
// as it was made, and each other as its route asks for it, with the cookie
// the root gave this machine's player, so every shard takes it as that player.
// What local handlers hear (tide_client_desc.events) is its home's: each
// client records into a log of its own, and the home's goes on to the host's.
// An area it watches adds the shards whose regions are near it, which it
// finds in the map every shard's world has (tide_shard_game.regions).

#define FOLLOWS_MOST 64u // Shards a route names that it reads

struct tide_shard_client {
    tide_shard_client_desc desc;
    tide_event_log *heard;  // The host's, or NULL
    tide_client **clients;  // By shard
    tide_event_log **logs;  // ...and what each heard, when the host hears
    uint32_t room;
    uint32_t home;
    uint32_t moves;
    uint32_t joins;
    uint64_t cookie;
    tide_player_id player;
    bool watching;
    tide_shard_box watch;
    tide_shard_region *regions; // The map, as the home's world last said
    uint32_t region_room;
};

static void make_room(tide_shard_client *c, const uint32_t shard)
{
    if (shard < c->room) return;
    uint32_t room = c->room ? c->room : 8u;
    while (room <= shard) room *= 2u;
    tide_client **clients = realloc(c->clients, room * sizeof *clients);
    if (!clients) tide_out_of_memory();
    c->clients = clients;
    tide_event_log **logs = realloc(c->logs, room * sizeof *logs);
    if (!logs) tide_out_of_memory();
    c->logs = logs;
    memset(c->clients + c->room, 0, (room - c->room) * sizeof *c->clients);
    memset(c->logs + c->room, 0, (room - c->room) * sizeof *c->logs);
    c->room = room;
}

// A client of `shard`, made as `d` says, hearing into a log of its own
static bool open_client(tide_shard_client *c, const uint32_t shard, tide_client_desc d, const double now)
{
    if (c->heard && !c->logs[shard]) {
        c->logs[shard] = calloc(1, sizeof *c->logs[shard]);
        if (!c->logs[shard]) tide_out_of_memory();
    }
    d.events = c->heard ? c->logs[shard] : NULL;
    c->clients[shard] = tide_client_create(&d, now);
    c->joins += c->clients[shard] != NULL;
    return c->clients[shard] != NULL;
}

tide_shard_client *tide_shard_client_create(const tide_shard_client_desc *desc, const double now)
{
    if (!desc || !desc->client.game || !desc->client.game->shards) return NULL;
    tide_shard_client *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->desc = *desc;
    c->heard = desc->client.events;
    make_room(c, TIDE_SHARD_ROOT);
    if (!open_client(c, TIDE_SHARD_ROOT, desc->client, now)) {
        tide_shard_client_destroy(c);
        return NULL;
    }
    c->home = TIDE_SHARD_ROOT;
    return c;
}

void tide_shard_client_destroy(tide_shard_client *c)
{
    if (!c) return;
    for (uint32_t i = 0; i < c->room; i++) {
        tide_client_destroy(c->clients[i]);
        if (c->logs[i]) tide_event_log_free(c->logs[i]);
        free(c->logs[i]);
    }
    free(c->clients);
    free(c->logs);
    free(c->regions);
    free(c);
}

static bool connected(const tide_client *client)
{
    return client && tide_client_status_of(client).state == TIDE_SESSION_CONNECTED;
}

// A client of a shard its route names, as the same player
static void follow(tide_shard_client *c, const tide_shard_link link, const double now)
{
    make_room(c, link.shard);
    if (c->clients[link.shard] || !c->desc.reach) return;
    tide_client_desc d = c->desc.client;
    if (!c->desc.reach(c->desc.user, link.shard, link.match, &d.transport, &d.server)) return;
    d.cookie = c->cookie;
    (void)open_client(c, link.shard, d, now);
}

// What the home heard goes on to the host; what the others heard isn't heard
static void pass_heard(tide_shard_client *c)
{
    if (!c->heard) return;
    for (uint32_t i = 0; i < c->room; i++) {
        if (!c->logs[i]) continue;
        if (i == c->home) tide_event_log_take(c->heard, c->logs[i]);
        else tide_event_log_clear(c->logs[i]);
    }
}

// The shards whose regions are within the game's reach of the area it
// watches, added to `links` (`n` of them) where they aren't yet: how many
// there are then
static uint32_t add_watched(tide_shard_client *c, const void *world, tide_shard_link *links, uint32_t n)
{
    const tide_shard_game *api = c->desc.client.game->shards;
    const uint32_t count = api->regions(world, c->regions, c->region_room);
    if (count > c->region_room) {
        free(c->regions);
        c->region_room = count * 2u;
        c->regions = malloc(c->region_room * sizeof *c->regions);
        if (!c->regions) tide_out_of_memory();
        (void)api->regions(world, c->regions, c->region_room);
    }
    for (uint32_t i = 0; i < count && n < FOLLOWS_MOST; i++) {
        const tide_shard_region *r = &c->regions[i];
        if (!tide_shard_box_near(&c->watch, &r->box, api->range, api->dims)) continue;
        bool named = false;
        for (uint32_t k = 0; k < n && !named; k++) named = links[k].shard == r->shard;
        if (!named) links[n++] = (tide_shard_link){r->shard, r->match};
    }
    return n;
}

void tide_shard_client_update(tide_shard_client *c, const double now)
{
    for (uint32_t i = 0; i < c->room; i++) {
        if (c->clients[i]) tide_client_update(c->clients[i], now);
    }
    tide_client *home = c->clients[c->home];
    if (!connected(home)) {
        pass_heard(c);
        return;
    }
    const tide_client_status status = tide_client_status_of(home);
    if (!c->cookie) {
        c->cookie = status.cookie;
        c->player = status.player;
    }
    const void *world = tide_client_verified_world(home);
    tide_shard_link links[FOLLOWS_MOST];
    uint32_t next_home = c->home;
    const uint32_t count = world ? c->desc.client.game->shards->route(world, c->player, &next_home, links, FOLLOWS_MOST) : 0u;
    uint32_t n = count < FOLLOWS_MOST ? count : FOLLOWS_MOST;
    if (c->watching && world) n = add_watched(c, world, links, n);
    for (uint32_t i = 0; i < n; i++) follow(c, links[i], now);
    pass_heard(c); // What the home heard up to now is the old home's
    if (!count) return; // No route for it yet: what it has, it keeps
    // Its home moves once it plays in the next one: then local code sees that one
    if (next_home != c->home && next_home < c->room && connected(c->clients[next_home])) {
        c->home = next_home;
        c->moves++;
    }
    // What neither its route nor the area it watches names goes, but its home
    for (uint32_t i = 0; i < c->room; i++) {
        if (!c->clients[i] || i == c->home) continue;
        bool named = false;
        for (uint32_t k = 0; k < n && !named; k++) named = links[k].shard == i;
        if (named) continue;
        tide_client_destroy(c->clients[i]);
        c->clients[i] = NULL;
        if (c->logs[i]) tide_event_log_clear(c->logs[i]);
    }
}

void tide_shard_client_rebuilt(tide_shard_client *c, const tide_game *game)
{
    c->desc.client.game = game;
}

uint32_t tide_shard_client_room(const tide_shard_client *c)
{
    return c->room;
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

uint32_t tide_shard_client_joins(const tide_shard_client *c)
{
    return c->joins;
}

void tide_shard_client_watch(tide_shard_client *c, const tide_shard_box *area)
{
    c->watching = area != NULL;
    if (area) c->watch = *area;
}

uint32_t tide_shard_client_views(const tide_shard_client *c, tide_view_worlds *out, uint32_t *shards, const uint32_t capacity)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < c->room; i++) {
        if (i == c->home || !connected(c->clients[i])) continue;
        const tide_view_worlds view = tide_client_view(c->clients[i]);
        if (!view.current) continue;
        if (n < capacity) {
            out[n] = view;
            shards[n] = i;
        }
        n++;
    }
    return n;
}

void tide_shard_client_mark_presented(tide_shard_client *c)
{
    for (uint32_t i = 0; i < c->room; i++) {
        if (i != c->home && connected(c->clients[i])) tide_client_mark_presented(c->clients[i]);
    }
}
