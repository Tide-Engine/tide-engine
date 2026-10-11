// A service and a network of the game's own, for own.tide: lobbies kept in
// this program's memory, which the game's overrides of the package's slots
// ask, and a network (tide/network.h) whose datagrams go from one player's
// queue to another's. Everything a store's package would bring, with no
// store: the engine and the lobby package know nothing of either.
//
// One program is one player to a game's C. The test runs several players in
// one program, so it says whose turn it is before it runs anything of a
// player's (OwnEnter).

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/network.h"

#define OWN_PLAYERS 4
#define OWN_LOBBIES 8
#define OWN_KEYS 8
#define OWN_INBOX 128u

// Why the last call gave nothing: the numbers own.tide's Why reads.
enum { OWN_OK, OWN_NOT_FOUND, OWN_FULL, OWN_KICKED, OWN_NOT_MEMBER, OWN_NOT_OWNER, OWN_CLOSED };

typedef struct own_pair {
    char key[33];
    char value[65];
} own_pair;

typedef struct own_member {
    int player;
    char name[33];
    own_pair data[OWN_KEYS];
    int data_count;
} own_member;

typedef struct own_lobby {
    int id; // 0: none
    char name[65];
    int owner;
    int max;
    bool listed;
    bool open;
    char match[65];
    int ended; // The host that ended its match for everyone (tide_transport.end), or 0
    int version;
    own_pair data[OWN_KEYS];
    int data_count;
    own_member members[OWN_PLAYERS]; // In the order they joined
    int count;
    bool kicked[OWN_PLAYERS + 1];
} own_lobby;

static own_lobby lobbies[OWN_LOBBIES];
// The test's: a player nothing reaches, as on a network that lost it, and
// the version of its lobby it last heard of
static bool deaf[OWN_PLAYERS + 1];
static int stale[OWN_PLAYERS + 1];
static int next_id = 101;
static int me = 1; // Whose turn it is, from 1
static int why;
static char said[8192];
static size_t said_size;

void OwnEnter(const int32_t player)
{
    me = player;
}

// Everything as the program started.
void OwnReset(void);

int32_t OwnWhy(void)
{
    return why;
}

const char *OwnWho(void)
{
    static char id[8];
    snprintf(id, sizeof id, "p%d", me);
    return id;
}

static own_lobby *lobby_of(const char *id)
{
    const int number = atoi(id);
    for (int i = 0; i < OWN_LOBBIES; i++) {
        if (number && lobbies[i].id == number) return &lobbies[i];
    }
    return NULL;
}

static own_member *member_of(own_lobby *l, const int player)
{
    for (int i = 0; i < l->count; i++) {
        if (l->members[i].player == player) return &l->members[i];
    }
    return NULL;
}

// The lobby `player` is in, or NULL.
static own_lobby *lobby_with(const int player)
{
    for (int i = 0; i < OWN_LOBBIES; i++) {
        if (lobbies[i].id && member_of(&lobbies[i], player)) return &lobbies[i];
    }
    return NULL;
}

static void set_pair(own_pair *pairs, int *count, const char *key, const char *value)
{
    int at = 0;
    while (at < *count && strcmp(pairs[at].key, key) != 0) at++;
    if (!value) {
        if (at < *count) pairs[at] = pairs[--*count];
        return;
    }
    if (at == OWN_KEYS) return;
    if (at == *count) ++*count;
    snprintf(pairs[at].key, sizeof pairs[at].key, "%s", key);
    snprintf(pairs[at].value, sizeof pairs[at].value, "%s", value);
}

static const char *pair_of(const own_pair *pairs, const int count, const char *key)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(pairs[i].key, key) == 0) return pairs[i].value;
    }
    return "";
}

// JSON, into `said`.
static void put(const char *text)
{
    const size_t n = strlen(text);
    if (said_size + n >= sizeof said) return;
    memcpy(said + said_size, text, n + 1);
    said_size += n;
}

static void put_text(const char *text)
{
    put("\"");
    for (const char *c = text; *c; c++) {
        char one[8];
        if (*c == '"' || *c == '\\') snprintf(one, sizeof one, "\\%c", *c);
        else if ((unsigned char)*c < 0x20) snprintf(one, sizeof one, "\\u%04x", (unsigned)*c);
        else snprintf(one, sizeof one, "%c", *c);
        put(one);
    }
    put("\"");
}

static void put_number(const int value)
{
    char text[16];
    snprintf(text, sizeof text, "%d", value);
    put(text);
}

static void put_player(const int player)
{
    char text[8];
    snprintf(text, sizeof text, "p%d", player);
    put_text(text);
}

static void put_pairs(const own_pair *pairs, const int count)
{
    put("{");
    for (int i = 0; i < count; i++) {
        if (i) put(",");
        put_text(pairs[i].key);
        put(":");
        put_text(pairs[i].value);
    }
    put("}");
}

// A lobby as the relay's lobbies say one, which the package's LobbyInfo reads.
static const char *lobby_said(const own_lobby *l)
{
    said_size = 0;
    said[0] = '\0';
    put("{\"id\":\"");
    put_number(l->id);
    put("\",\"game\":\"own\",\"name\":");
    put_text(l->name);
    put(",\"owner\":");
    put_player(l->owner);
    put(",\"max\":");
    put_number(l->max);
    put(l->listed ? ",\"public\":true" : ",\"public\":false");
    put(l->open ? ",\"open\":true" : ",\"open\":false");
    put(",\"data\":");
    put_pairs(l->data, l->data_count);
    put(",\"match\":");
    put_text(l->match);
    put(",\"version\":");
    put_number(l->version);
    put(",\"you\":");
    put_player(me);
    put(",\"members\":[");
    for (int i = 0; i < l->count; i++) {
        if (i) put(",");
        put("{\"id\":");
        put_player(l->members[i].player);
        put(",\"name\":");
        put_text(l->members[i].name);
        put(",\"data\":");
        put_pairs(l->members[i].data, l->members[i].data_count);
        put("}");
    }
    put("]}");
    return said;
}

static const char *nothing(const int reason)
{
    why = reason;
    return "";
}

// The lobby as its member sees it, or "" with why (OwnWhy).
const char *OwnLobby(const char *id)
{
    own_lobby *l = lobby_of(id);
    if (!l) return nothing(OWN_NOT_FOUND);
    if (l->kicked[me]) return nothing(OWN_KICKED);
    if (!member_of(l, me)) return nothing(OWN_NOT_MEMBER);
    why = OWN_OK;
    return lobby_said(l);
}

// How many times it changed, or -1 for a lobby this player isn't in.
int32_t OwnVersion(const char *id)
{
    own_lobby *l = lobby_of(id);
    if (deaf[me]) return stale[me];
    return l && member_of(l, me) ? l->version : -1;
}

static const char *enter(own_lobby *l, const char *name)
{
    if (l->kicked[me]) return nothing(OWN_KICKED);
    if (!member_of(l, me)) {
        if (!l->open) return nothing(OWN_CLOSED);
        if (l->count >= l->max || l->count >= OWN_PLAYERS) return nothing(OWN_FULL);
        own_member *m = &l->members[l->count++];
        *m = (own_member){.player = me};
        snprintf(m->name, sizeof m->name, "%s", name);
        l->version++;
    }
    why = OWN_OK;
    return lobby_said(l);
}

const char *OwnCreate(const char *name, const int32_t max, const bool listed, const char *mode, const char *member)
{
    for (int i = 0; i < OWN_LOBBIES; i++) {
        own_lobby *l = &lobbies[i];
        if (l->id) continue;
        *l = (own_lobby){.id = next_id++, .owner = me, .max = max > 0 ? max : OWN_PLAYERS, .listed = listed, .open = true};
        snprintf(l->name, sizeof l->name, "%s", name);
        if (*mode) set_pair(l->data, &l->data_count, "mode", mode);
        return enter(l, member);
    }
    return nothing(OWN_FULL);
}

const char *OwnJoin(const char *id, const char *member)
{
    own_lobby *l = lobby_of(id);
    return l ? enter(l, member) : nothing(OWN_NOT_FOUND);
}

// Find or create, in one call: the fullest listed lobby of this mode with
// room, or a new one.
const char *OwnQuick(const char *mode, const char *name, const int32_t max, const char *member)
{
    own_lobby *best = NULL;
    for (int i = 0; i < OWN_LOBBIES; i++) {
        own_lobby *l = &lobbies[i];
        if (!l->id || !l->listed || !l->open || l->kicked[me] || l->count >= l->max) continue;
        if (strcmp(pair_of(l->data, l->data_count, "mode"), mode) != 0) continue;
        if (!best || l->count > best->count) best = l;
    }
    return best ? enter(best, member) : OwnCreate(name, max, true, mode, member);
}

// The listed lobbies of a mode ("" for any) that have room.
const char *OwnList(const char *mode)
{
    said_size = 0;
    said[0] = '\0';
    put("{\"lobbies\":[");
    bool first = true;
    for (int i = 0; i < OWN_LOBBIES; i++) {
        const own_lobby *l = &lobbies[i];
        if (!l->id || !l->listed || l->count >= l->max) continue;
        if (*mode && strcmp(pair_of(l->data, l->data_count, "mode"), mode) != 0) continue;
        if (!first) put(",");
        first = false;
        put("{\"id\":\"");
        put_number(l->id);
        put("\",\"name\":");
        put_text(l->name);
        put(",\"players\":");
        put_number(l->count);
        put(",\"max\":");
        put_number(l->max);
        put(",\"data\":");
        put_pairs(l->data, l->data_count);
        put("}");
    }
    put("]}");
    return said;
}

// Out of a lobby. Its owner then is whoever has been there longest: the
// authority for who hosts its match next (own_migrated).
static void leave(own_lobby *l, const int player)
{
    own_member *m = member_of(l, player);
    if (!m) return;
    const int at = (int)(m - l->members);
    for (int i = at; i + 1 < l->count; i++) l->members[i] = l->members[i + 1];
    l->count--;
    l->version++;
    if (!l->count) {
        *l = (own_lobby){0};
        return;
    }
    if (l->owner == player) l->owner = l->members[0].player;
}

void OwnLeave(const char *id)
{
    own_lobby *l = lobby_of(id);
    if (l) leave(l, me);
}

// A change its owner asks: 0 its name, 1 its match, 2 its owner, 3 a member
// sent out, 4 whether it's open ("1" or "0"). False with why.
bool OwnSet(const char *id, const int32_t what, const char *value)
{
    own_lobby *l = lobby_of(id);
    why = !l ? OWN_NOT_FOUND : !member_of(l, me) ? OWN_NOT_MEMBER : l->owner != me ? OWN_NOT_OWNER : OWN_OK;
    if (why != OWN_OK) return false;
    const int player = value[0] == 'p' ? atoi(value + 1) : 0;
    if ((what == 2 || what == 3) && !member_of(l, player)) {
        why = OWN_NOT_FOUND;
        return false;
    }
    if (what == 0) snprintf(l->name, sizeof l->name, "%s", value);
    if (what == 1) snprintf(l->match, sizeof l->match, "%s", value);
    if (what == 2) l->owner = player;
    if (what == 3) {
        l->kicked[player] = true;
        leave(l, player);
    }
    if (what == 4) l->open = value[0] == '1';
    l->version++;
    return true;
}

// A key of the lobby's data (its owner's to set), or of this member's own,
// set, or taken away with `remove`.
bool OwnSetData(const char *id, const bool own, const char *key, const char *value, const bool remove)
{
    own_lobby *l = lobby_of(id);
    own_member *m = l ? member_of(l, me) : NULL;
    why = !l ? OWN_NOT_FOUND : !m ? OWN_NOT_MEMBER : !own && l->owner != me ? OWN_NOT_OWNER : OWN_OK;
    if (why != OWN_OK) return false;
    if (own) set_pair(m->data, &m->data_count, key, remove ? NULL : value);
    else set_pair(l->data, &l->data_count, key, remove ? NULL : value);
    l->version++;
    return true;
}

bool OwnSetName(const char *id, const char *name)
{
    own_lobby *l = lobby_of(id);
    own_member *m = l ? member_of(l, me) : NULL;
    why = !l ? OWN_NOT_FOUND : !m ? OWN_NOT_MEMBER : OWN_OK;
    if (why != OWN_OK) return false;
    if (strcmp(m->name, name) == 0) return true;
    snprintf(m->name, sizeof m->name, "%s", name);
    l->version++;
    return true;
}

// ---------------------------------------------------------------------------
// The network: a player's address is its number, the text a match is joined
// by is its host's ID ("p2"), and a datagram goes into the queue of the
// player it's for. Host migration goes by the lobby: where a match's players
// meet again is the lobby they're in, and its owner is who hosts next.

typedef struct own_datagram {
    uint32_t size;
    int from;
    uint8_t data[TIDE_NET_MTU];
} own_datagram;

typedef struct own_end {
    int player;
    int host;     // Whom it joined; 0 where it hosts
    bool live;
    int generation; // Which of this player's transports it is: an older one's calls do nothing
    own_datagram inbox[OWN_INBOX];
    uint32_t first, count;
} own_end;

typedef struct own_handle {
    int player;
    int generation;
} own_handle;

static own_end ends[OWN_PLAYERS + 1];
static int lost_host[OWN_PLAYERS + 1];   // The host whose match this player was in when it went
static int meeting[OWN_PLAYERS + 1];     // The lobby it went to (own_migrate)
static int updates;                      // Times the engine ran the network's callbacks

// A player nothing reaches from now on, neither the match's datagrams nor
// what becomes of its lobby, or one that's reached again.
void OwnDeafen(const int32_t player, const bool on)
{
    const own_lobby *l = lobby_with(player);
    stale[player] = l ? l->version : -1;
    deaf[player] = on;
}

int32_t OwnUpdates(void)
{
    return updates;
}

static own_end *end_of(void *self)
{
    const own_handle *h = self;
    own_end *end = &ends[h->player];
    return end->live && end->generation == h->generation ? end : NULL;
}

static void own_send(void *self, const tide_address to, const void *data, const uint32_t size)
{
    const own_end *end = end_of(self);
    if (!end || to.kind != TIDE_ADDRESS_NETWORK || to.host < 1 || to.host > OWN_PLAYERS || size > TIDE_NET_MTU) return;
    own_end *there = &ends[to.host];
    if (!there->live || there->count == OWN_INBOX || deaf[to.host]) return; // Nobody there, or too much at once: lost
    own_datagram *d = &there->inbox[(there->first + there->count++) % OWN_INBOX];
    d->size = size;
    d->from = end->player;
    memcpy(d->data, data, size);
}

static uint32_t own_receive(void *self, tide_address *from, void *data, const uint32_t capacity)
{
    own_end *end = end_of(self);
    while (end && end->count) {
        const own_datagram *d = &end->inbox[end->first];
        end->first = (end->first + 1u) % OWN_INBOX;
        end->count--;
        // A player hears its host alone, and nothing longer than there's room for
        if ((end->host && d->from != end->host) || d->size > capacity) continue;
        memcpy(data, d->data, d->size);
        *from = (tide_address){TIDE_ADDRESS_NETWORK, (uint32_t)d->from, 0};
        return d->size;
    }
    return 0;
}

static void own_close(void *self)
{
    own_end *end = end_of(self);
    if (end) {
        end->live = false;
        end->count = 0;
    }
    free(self);
}

// The match ended for everyone: its lobby says so, where its players would
// come to take it over.
static void own_end_match(void *self)
{
    const own_end *end = end_of(self);
    own_lobby *l = end ? lobby_with(end->player) : NULL;
    if (l && l->owner == end->player) {
        l->ended = end->player;
        l->version++;
    }
}

static bool open_end(const int host, tide_transport *out)
{
    own_handle *h = malloc(sizeof *h);
    if (!h) return false;
    own_end *end = &ends[me];
    end->player = me;
    end->host = host;
    end->live = true;
    end->first = end->count = 0;
    *h = (own_handle){me, ++end->generation};
    *out = (tide_transport){.self = h, .send = own_send, .receive = own_receive, .close = own_close, .end = own_end_match};
    return true;
}

static bool own_host(void *self, tide_transport *out)
{
    (void)self;
    return open_end(0, out);
}

// "p1" to "p9": a player's ID. Only the first four are anyone.
static int host_named(const char *address)
{
    return address[0] == 'p' && address[1] >= '1' && address[1] <= '9' && !address[2] ? address[1] - '0' : 0;
}

static bool own_join(void *self, const char *address, tide_transport *out, tide_address *server)
{
    (void)self;
    const int host = host_named(address);
    if (!host || host == me || !open_end(host, out)) return false;
    lost_host[me] = host;
    *server = (tide_address){TIDE_ADDRESS_NETWORK, (uint32_t)host, 0};
    return true;
}

static void own_address(void *self, char *out, const size_t size)
{
    (void)self;
    const own_end *end = &ends[me];
    if (!end->live) snprintf(out, size, "%s", "");
    else snprintf(out, size, "p%d", end->host ? end->host : me);
}

// A match joined at an ID that's nobody's can't be.
static bool own_failed(void *self)
{
    (void)self;
    return ends[me].live && ends[me].host > OWN_PLAYERS;
}

static void own_update(void *self)
{
    (void)self;
    updates++;
}

// Where this match's players meet again: the lobby its host is in.
static void own_place(void *self, char *out, const size_t size)
{
    (void)self;
    const own_lobby *l = lobby_with(me);
    if (l) snprintf(out, size, "%d", l->id);
    else snprintf(out, size, "%s", "");
}

static void own_migrate(void *self, const char *place)
{
    (void)self;
    meeting[me] = atoi(place);
}

// Who hosts the match now is the lobby's to say: its owner. While that's
// still the host that went, nothing is known.
static int own_migrated(void *self, tide_transport *out, tide_address *server)
{
    (void)self;
    char place[16];
    snprintf(place, sizeof place, "%d", meeting[me]);
    own_lobby *l = lobby_of(place);
    if (!l || !member_of(l, me)) return TIDE_NETWORK_LOST;
    if (l->ended == lost_host[me]) return TIDE_NETWORK_ENDED;
    if (l->owner == lost_host[me]) return TIDE_NETWORK_MOVING;
    if (l->owner == me) return open_end(0, out) ? TIDE_NETWORK_HOSTS : TIDE_NETWORK_LOST;
    lost_host[me] = l->owner;
    *server = (tide_address){TIDE_ADDRESS_NETWORK, (uint32_t)l->owner, 0};
    return open_end(l->owner, out) ? TIDE_NETWORK_JOINS : TIDE_NETWORK_LOST;
}

// What own.tide's transport slot calls before it opens or joins a match.
bool UseOwnNetwork(void)
{
    tide_network_set(&(tide_network){.host = own_host, .join = own_join, .address = own_address, .failed = own_failed,
                                     .update = own_update, .place = own_place, .migrate = own_migrate,
                                     .migrated = own_migrated});
    return true;
}

void OwnReset(void)
{
    memset(lobbies, 0, sizeof lobbies);
    memset(ends, 0, sizeof ends);
    memset(lost_host, 0, sizeof lost_host);
    memset(meeting, 0, sizeof meeting);
    memset(deaf, 0, sizeof deaf);
    next_id = 101;
    me = 1;
    updates = 0;
    tide_network_set(NULL);
}
