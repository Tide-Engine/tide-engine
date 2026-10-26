#include "tide/hosting.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define HOST_MAGIC UINT32_C(0x48534454)
#define HOST_VERSION 2u
#define HOST_REQUEST 1u
#define HOST_REPLY 2u
#define HOST_DATA 3u
#define HOST_HEADER 24u
#define HOST_PAYLOAD (TIDE_NET_MTU - HOST_HEADER)
#define HOST_QUEUE 64u
#define HOST_PARTS 8u
#define HOST_BUDGET 256u

typedef struct host_packet {
    struct host_packet *next;
    tide_address from;
    double arrived;
    uint32_t size;
    uint8_t data[TIDE_NET_MTU];
} host_packet;

typedef struct host_queue {
    host_packet *first, *last;
    uint32_t count;
} host_queue;

typedef struct host_fragment {
    uint32_t sequence;
    uint16_t total;
    uint8_t mask;
    uint8_t data[TIDE_NET_MTU];
} host_fragment;

typedef struct host_match {
    tide_hosting *host;
    uint64_t id;
    tide_server *server;
    host_queue queue;
    double empty_since, arrived;
    bool kept; // A shard: it runs with no one in it (tide_hosting_keep)
    bool open; // ...and one whose server takes only players it admitted, which anyone may ask to join (tide_hosting_open)
} host_match;

typedef struct host_peer {
    uint64_t id, nonce, requested_match, match;
    tide_address address;
    tide_hosting_result result;
    tide_hosting_grant grant;
    double touched;
    uint16_t credential_size, parameter_size;
    uint8_t credential[TIDE_HOSTING_CREDENTIAL_BYTES];
    uint8_t parameters[TIDE_HOSTING_PARAMETER_BYTES];
    host_fragment parts[HOST_PARTS];
    uint32_t sequence;
} host_peer;

struct tide_hosting {
    tide_hosting_desc desc;
    host_match *matches;
    host_peer *peers;
    uint64_t next_match, next_peer;
    uint32_t count;
    double now;
    uint64_t front; // What a request for match 0 joins (tide_hosting_front), or 0
};

// The transport admissions to one server share (tide_hosting_client_beside):
// whichever updates first reads it, and each datagram goes to the admission
// it's for. The last admission to go closes it.
typedef struct host_link {
    tide_transport transport;
    tide_address server;
    tide_hosting_client **members;
    uint32_t count, room;
} host_link;

struct tide_hosting_client {
    host_link *link;
    tide_hosting_client_desc desc;
    tide_hosting_result result;
    uint64_t match, route;
    double began, now, sent, arrived;
    uint32_t sequence;
    uint8_t request[TIDE_NET_MTU];
    uint32_t request_size;
    host_fragment parts[HOST_PARTS];
    host_queue queue;
    bool detached;
};

static bool same_address(const tide_address a, const tide_address b)
{
    return a.kind == b.kind && a.host == b.host && a.port == b.port;
}

static void clear_queue(host_queue *q)
{
    while (q->first) {
        host_packet *p = q->first;
        q->first = p->next;
        free(p);
    }
    *q = (host_queue){0};
}

static void enqueue(host_queue *q, const tide_address from, const double arrived,
                    const uint8_t *data, const uint32_t size)
{
    if (q->count >= HOST_QUEUE) return;
    host_packet *p = malloc(sizeof *p);
    if (!p) return;
    *p = (host_packet){.from = from, .arrived = arrived, .size = size};
    memcpy(p->data, data, size);
    if (q->last) q->last->next = p;
    else q->first = p;
    q->last = p;
    q->count++;
}

static uint32_t dequeue(host_queue *q, tide_address *from, double *arrived, void *data, const uint32_t capacity)
{
    while (q->first) {
        host_packet *p = q->first;
        q->first = p->next;
        if (!q->first) q->last = NULL;
        q->count--;
        const uint32_t n = p->size <= capacity ? p->size : 0;
        if (n) {
            *from = p->from;
            *arrived = p->arrived;
            memcpy(data, p->data, n);
        }
        free(p);
        if (n) return n;
    }
    return 0;
}

static tide_writer header(uint8_t data[TIDE_NET_MTU], const uint8_t kind, const uint64_t route)
{
    tide_writer w = {.data = data, .capacity = TIDE_NET_MTU};
    tide_write_u32(&w, HOST_MAGIC);
    tide_write_u8(&w, HOST_VERSION);
    tide_write_u8(&w, kind);
    tide_write_u16(&w, 0);
    tide_write_u64(&w, route);
    return w;
}

static bool read_header(tide_reader *r, uint8_t *kind, uint64_t *route)
{
    if (tide_read_u32(r) != HOST_MAGIC || tide_read_u8(r) != HOST_VERSION) return false;
    *kind = tide_read_u8(r);
    if (tide_read_u16(r) != 0) return false;
    *route = tide_read_u64(r);
    return !r->failed && *route != 0;
}

// Sessions keep their existing MTU. Only maximum-size datagrams split, into
// two independently bounded pieces; loss/reordering remains the session's.
static void send_data(const tide_transport *t, const tide_address to, const uint64_t route,
                      uint32_t *sequence, const void *data, const uint32_t size)
{
    if (!size || size > TIDE_NET_MTU) return;
    const uint32_t number = ++*sequence;
    for (uint32_t at = 0; at < size; at += HOST_PAYLOAD) {
        uint8_t bytes[TIDE_NET_MTU];
        tide_writer w = header(bytes, HOST_DATA, route);
        const uint32_t n = size - at < HOST_PAYLOAD ? size - at : HOST_PAYLOAD;
        tide_write_u32(&w, number);
        tide_write_u16(&w, (uint16_t)size);
        tide_write_u16(&w, (uint16_t)at);
        tide_write_bytes(&w, (const uint8_t *)data + at, n);
        t->send(t->self, to, bytes, w.size);
    }
}

static void take_data(host_fragment parts[HOST_PARTS], host_queue *q, const tide_address from,
                      const double arrived, tide_reader *r)
{
    const uint32_t sequence = tide_read_u32(r);
    const uint16_t total = tide_read_u16(r), at = tide_read_u16(r);
    if (r->failed || !total || total > TIDE_NET_MTU || at >= total || at % HOST_PAYLOAD) return;
    const uint32_t n = total - at < HOST_PAYLOAD ? total - at : HOST_PAYLOAD;
    const uint8_t *data = tide_read_bytes(r, n);
    if (!data || r->at != r->size) return;
    if (total <= HOST_PAYLOAD) {
        enqueue(q, from, arrived, data, total);
        return;
    }
    host_fragment *p = &parts[sequence % HOST_PARTS];
    if (p->sequence != sequence || p->total != total) {
        p->sequence = sequence;
        p->total = total;
        p->mask = 0;
    }
    const uint8_t bit = (uint8_t)(1u << (at / HOST_PAYLOAD));
    if (p->mask & bit) return;
    memcpy(p->data + at, data, n);
    p->mask |= bit;
    if (p->mask == 3u) enqueue(q, from, arrived, p->data, total);
}

static host_match *find_match(const tide_hosting *h, const uint64_t id)
{
    if (!id) return NULL;
    const uint32_t i = (uint32_t)(id % h->desc.max_matches);
    return h->matches[i].id == id ? &h->matches[i] : NULL;
}

static host_peer *find_peer(const tide_hosting *h, const uint64_t id)
{
    if (!id) return NULL;
    const uint32_t i = (uint32_t)(id % h->desc.max_connections);
    return h->peers[i].id == id ? &h->peers[i] : NULL;
}

static tide_address peer_address(const tide_hosting *h, const host_peer *p)
{
    (void)h;
    return (tide_address){.kind = TIDE_ADDRESS_NETWORK, .host = (uint32_t)p->id, .port = (uint16_t)(p->id >> 32)};
}

static void match_send(void *self, const tide_address to, const void *data, const uint32_t size)
{
    host_match *m = self;
    tide_hosting *h = m->host;
    if (to.kind != TIDE_ADDRESS_NETWORK) return;
    host_peer *p = find_peer(h, (uint64_t)to.host | (uint64_t)to.port << 32);
    if (!p || p->result != TIDE_HOSTING_ACCEPTED || p->match != m->id) return;
    send_data(&h->desc.transport, p->address, p->id, &p->sequence, data, size);
}

static uint32_t match_receive(void *self, tide_address *from, void *data, const uint32_t capacity)
{
    host_match *m = self;
    return dequeue(&m->queue, from, &m->arrived, data, capacity);
}

static void match_close(void *self)
{
    host_match *m = self;
    clear_queue(&m->queue);
}

static double match_clock(void *self)
{
    const host_match *m = self;
    const tide_transport *t = &m->host->desc.transport;
    return t->clock ? t->clock(t->self) : m->host->now;
}

static double match_arrived(void *self)
{
    return ((const host_match *)self)->arrived;
}

static bool match_identity(void *self, const uint32_t transport, const tide_address address, uint8_t identity[32])
{
    const host_match *m = self;
    if (transport || address.kind != TIDE_ADDRESS_NETWORK) return false;
    const host_peer *p = find_peer(m->host, (uint64_t)address.host | (uint64_t)address.port << 32);
    if (!p || p->result != TIDE_HOSTING_ACCEPTED || p->match != m->id) return false;
    memcpy(identity, p->grant.identity, 32);
    return true;
}

static void reply(tide_hosting *h, const host_peer *p)
{
    uint8_t bytes[TIDE_NET_MTU];
    tide_writer w = header(bytes, HOST_REPLY, p->nonce);
    tide_write_u8(&w, (uint8_t)p->result);
    tide_write_u64(&w, p->match);
    tide_write_u64(&w, p->id);
    h->desc.transport.send(h->desc.transport.self, p->address, bytes, w.size);
}

static tide_hosting_request request_of(const host_peer *p)
{
    return (tide_hosting_request){.request = p->id, .match = p->requested_match, .peer = p->address,
        .credential = p->credential, .credential_size = p->credential_size,
        .parameters = p->parameters, .parameter_size = p->parameter_size};
}

uint64_t tide_hosting_start(tide_hosting *h, const tide_server_desc *desc)
{
    if (!h || !desc || h->count >= h->desc.max_matches) return 0;
    host_match *m = NULL;
    for (uint32_t i = 0; i < h->desc.max_matches; i++) {
        if (!h->matches[i].id) { m = &h->matches[i]; break; }
    }
    if (!m || h->next_match > UINT64_C(0x0000ffffffffffff) - 2u * h->desc.max_matches) return 0;
    h->next_match += h->desc.max_matches;
    const uint64_t id = h->next_match + (uint64_t)(m - h->matches);
    *m = (host_match){.host = h, .id = id, .empty_since = h->now};
    tide_server_desc d = *desc;
    d.game = h->desc.game;
    d.jobs = h->desc.jobs;
    d.entropy = h->desc.entropy;
    d.local_first = false;
    d.identity_user = m;
    d.identity = match_identity;
    d.transports[0] = (tide_transport){.self = m, .send = match_send, .receive = match_receive,
        .close = match_close, .clock = match_clock, .arrived = match_arrived};
    d.transports[1] = (tide_transport){0};
    m->server = tide_server_create(&d, h->now);
    if (!m->server) { *m = (host_match){0}; return 0; }
    h->count++;
    return id;
}

bool tide_hosting_stop(tide_hosting *h, const uint64_t match)
{
    host_match *m = h ? find_match(h, match) : NULL;
    if (!m) return false;
    tide_server_destroy(m->server);
    for (uint32_t i = 0; i < h->desc.max_connections; i++) {
        host_peer *p = &h->peers[i];
        if (p->match != match) continue;
        p->result = TIDE_HOSTING_NOT_FOUND;
        p->match = 0;
        p->touched = h->now;
        reply(h, p);
    }
    *m = (host_match){0};
    h->count--;
    return true;
}

bool tide_hosting_resolve(tide_hosting *h, const uint64_t request, const tide_hosting_result result,
                          const tide_hosting_grant *grant)
{
    host_peer *p = h ? find_peer(h, request) : NULL;
    if (!p || p->result != TIDE_HOSTING_PENDING || result == TIDE_HOSTING_PENDING
        || (unsigned)result > TIDE_HOSTING_WRONG_VERSION) return false;
    p->result = result;
    if (result == TIDE_HOSTING_ACCEPTED) {
        if (grant) p->grant = *grant;
        if (p->requested_match) {
            host_match *m = find_match(h, p->requested_match);
            if (!m) p->result = TIDE_HOSTING_NOT_FOUND;
            else p->match = m->id;
        } else if (h->count >= h->desc.max_matches) p->result = TIDE_HOSTING_FULL;
        else {
            tide_server_desc d = {.wait_for_first = true};
            const tide_hosting_request r = request_of(p);
            if (h->desc.start && !h->desc.start(h->desc.user, &r, &d)) p->result = TIDE_HOSTING_DENIED;
            else {
                p->match = tide_hosting_start(h, &d);
                if (!p->match) p->result = TIDE_HOSTING_FULL;
            }
        }
    }
    // Credentials are no longer needed after admission, including rejection.
    memset(p->credential, 0, sizeof p->credential);
    p->credential_size = 0;
    reply(h, p);
    return true;
}

static void on_request(tide_hosting *h, const tide_address from, const uint64_t nonce, tide_reader *r)
{
    const uint64_t asked = tide_read_u64(r);
    const uint8_t *simulation = tide_read_bytes(r, 32);
    const uint16_t cn = tide_read_u16(r), pn = tide_read_u16(r);
    if (cn > TIDE_HOSTING_CREDENTIAL_BYTES || pn > TIDE_HOSTING_PARAMETER_BYTES || (asked && pn)) return;
    // A sharded world's players join it at its front, its root shard, by asking for a match
    const uint64_t match = !asked && h->front ? h->front : asked;
    if (match && pn) return;
    const uint8_t *credential = tide_read_bytes(r, cn), *parameters = tide_read_bytes(r, pn);
    if (r->failed || r->at != r->size) return;
    host_peer *p = NULL;
    for (uint32_t i = 0; i < h->desc.max_connections; i++) {
        host_peer *candidate = &h->peers[i];
        if (candidate->id && candidate->nonce == nonce && same_address(candidate->address, from)) {
            reply(h, candidate); // Never execute a repeated create twice.
            return;
        }
        if (!candidate->id && !p) p = candidate;
    }
    if (!p || h->next_peer > UINT64_C(0x0000ffffffffffff) - 2u * h->desc.max_connections) return;
    h->next_peer += h->desc.max_connections;
    *p = (host_peer){.id = h->next_peer + (uint64_t)(p - h->peers), .nonce = nonce,
        .requested_match = match, .address = from, .touched = h->now,
        .credential_size = cn, .parameter_size = pn};
    if (cn) memcpy(p->credential, credential, cn);
    if (pn) memcpy(p->parameters, parameters, pn);
    tide_hosting_grant grant = {0};
    tide_hosting_result result = TIDE_HOSTING_DENIED;
    const host_match *wanted = find_match(h, match);
    const bool open = wanted && wanted->open;
    if (memcmp(simulation, h->desc.game->simulation, 32) != 0) {
        result = TIDE_HOSTING_WRONG_VERSION;
    } else if (cn && (!h->desc.secure_peer || !h->desc.secure_peer(h->desc.user, from))) {
        result = TIDE_HOSTING_DENIED;
    } else if (!cn && open) {
        result = TIDE_HOSTING_ACCEPTED; // Its server decides who plays
    } else if (h->desc.authorize) {
        const tide_hosting_request request = request_of(p);
        result = h->desc.authorize(h->desc.user, &request, &grant);
    } else if (!cn && (match ? h->desc.allow_anonymous_join : h->desc.allow_anonymous_create)) {
        result = TIDE_HOSTING_ACCEPTED;
    }
    if (result != TIDE_HOSTING_PENDING) {
        if (!tide_hosting_resolve(h, p->id, result, &grant))
            (void)tide_hosting_resolve(h, p->id, TIDE_HOSTING_DENIED, NULL);
    } else reply(h, p);
}

tide_hosting *tide_hosting_create(const tide_hosting_desc *desc, const double now)
{
    if (!desc || !desc->game || !desc->transport.send || !desc->transport.receive || !desc->transport.close
        || !desc->entropy || !isfinite(now) || !isfinite(desc->idle_seconds) || desc->idle_seconds < 0
        || desc->max_matches > 4096u || desc->max_connections > TIDE_PLAYERS_MOST) return NULL;
    tide_hosting *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    h->desc = *desc;
    if (!h->desc.max_matches) h->desc.max_matches = 64;
    if (!h->desc.max_connections) h->desc.max_connections = 256;
    if (!h->desc.idle_seconds) h->desc.idle_seconds = 60;
    h->matches = calloc(h->desc.max_matches, sizeof *h->matches);
    h->peers = calloc(h->desc.max_connections, sizeof *h->peers);
    if (!h->matches || !h->peers) { free(h->matches); free(h->peers); free(h); return NULL; }
    // Leave ample monotonic space and encode the slot in each ID's remainder.
    h->next_match = (desc->entropy() & UINT64_C(0x0000ffffffffffff)) / h->desc.max_matches * h->desc.max_matches;
    h->next_peer = (desc->entropy() & UINT64_C(0x000000ffffffffff)) / h->desc.max_connections * h->desc.max_connections;
    h->now = now;
    return h;
}

void tide_hosting_destroy(tide_hosting *h)
{
    if (!h) return;
    for (uint32_t i = 0; i < h->desc.max_matches; i++) {
        if (h->matches[i].id) tide_server_destroy(h->matches[i].server);
    }
    h->desc.transport.close(h->desc.transport.self);
    memset(h->peers, 0, h->desc.max_connections * sizeof *h->peers);
    free(h->peers);
    free(h->matches);
    free(h);
}

void tide_hosting_update(tide_hosting *h, const double now)
{
    if (!h || !isfinite(now) || now < h->now) return;
    h->now = now;
    const tide_transport *t = &h->desc.transport;
    for (uint32_t budget = 0; budget < HOST_BUDGET; budget++) {
        uint8_t bytes[TIDE_NET_MTU], kind;
        tide_address from;
        const uint32_t n = t->receive(t->self, &from, bytes, sizeof bytes);
        if (!n) break;
        tide_reader r = {.data = bytes, .size = n};
        uint64_t route;
        if (!read_header(&r, &kind, &route)) continue;
        if (kind == HOST_REQUEST) on_request(h, from, route, &r);
        else if (kind == HOST_DATA) {
            host_peer *p = find_peer(h, route);
            if (!p || p->result != TIDE_HOSTING_ACCEPTED || !same_address(p->address, from)) continue;
            host_match *m = find_match(h, p->match);
            if (!m) continue;
            p->touched = now;
            take_data(p->parts, &m->queue, peer_address(h, p), t->arrived ? t->arrived(t->self) : now, &r);
        }
    }
    for (uint32_t i = 0; i < h->desc.max_matches; i++) {
        host_match *m = &h->matches[i];
        if (!m->id) continue;
        tide_server_update(m->server, now);
        if (tide_server_player_count(m->server)) m->empty_since = now;
        if ((!m->kept && now - m->empty_since >= h->desc.idle_seconds)
            || (h->desc.game->ended && h->desc.game->ended(tide_server_world(m->server))))
            (void)tide_hosting_stop(h, m->id);
    }
    for (uint32_t i = 0; i < h->desc.max_connections; i++) {
        host_peer *p = &h->peers[i];
        if (!p->id) continue;
        if (p->result == TIDE_HOSTING_PENDING && now - p->touched >= 10.0)
            (void)tide_hosting_resolve(h, p->id, TIDE_HOSTING_TIMED_OUT, NULL);
        // Retain completed requests long enough for lost replies/reconnects.
        if (now - p->touched >= 30.0) *p = (host_peer){0};
    }
}

uint32_t tide_hosting_match_count(const tide_hosting *h) { return h ? h->count : 0; }

tide_server *tide_hosting_server(tide_hosting *h, const uint64_t match)
{
    host_match *m = h ? find_match(h, match) : NULL;
    return m ? m->server : NULL;
}

bool tide_hosting_keep(tide_hosting *h, const uint64_t match)
{
    host_match *m = h ? find_match(h, match) : NULL;
    if (m) m->kept = true;
    return m != NULL;
}

bool tide_hosting_open(tide_hosting *h, const uint64_t match)
{
    host_match *m = h ? find_match(h, match) : NULL;
    if (m) m->open = true;
    return m != NULL;
}

void tide_hosting_front(tide_hosting *h, const uint64_t match)
{
    if (h) h->front = match;
}

const void *tide_hosting_world(const tide_hosting *h, const uint64_t match)
{
    const host_match *m = h ? find_match(h, match) : NULL;
    return m ? tide_server_world(m->server) : NULL;
}

bool tide_hosting_player_identity(const tide_hosting *h, const uint64_t match, const tide_player_id player,
                                   uint8_t identity[32])
{
    const host_match *m = h ? find_match(h, match) : NULL;
    return tide_server_player_identity(m ? m->server : NULL, player, identity);
}

static void client_send(void *self, tide_address to, const void *data, uint32_t size);

static bool link_add(host_link *l, tide_hosting_client *c)
{
    if (l->count == l->room) {
        const uint32_t room = l->room ? l->room * 2u : 4u;
        tide_hosting_client **grown = realloc(l->members, room * sizeof *grown);
        if (!grown) return false;
        l->members = grown;
        l->room = room;
    }
    l->members[l->count++] = c;
    c->link = l;
    return true;
}

static tide_hosting_client *client_new(const tide_hosting_client_desc *desc, const double now)
{
    tide_hosting_client *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->desc = *desc;
    c->desc.credential = NULL;
    c->desc.parameters = NULL;
    c->began = c->now = now;
    c->sent = now - 1.0;
    tide_writer w = header(c->request, HOST_REQUEST, desc->nonce);
    tide_write_u64(&w, desc->match);
    tide_write_bytes(&w, desc->simulation, 32);
    tide_write_u16(&w, (uint16_t)desc->credential_size);
    tide_write_u16(&w, (uint16_t)desc->parameter_size);
    tide_write_bytes(&w, desc->credential, desc->credential_size);
    tide_write_bytes(&w, desc->parameters, desc->parameter_size);
    c->request_size = w.size;
    return c;
}

tide_hosting_client *tide_hosting_client_create(const tide_hosting_client_desc *desc, const double now)
{
    if (!desc || !desc->nonce || !desc->transport.send || !desc->transport.receive || !desc->transport.close
        || !isfinite(now) || desc->credential_size > TIDE_HOSTING_CREDENTIAL_BYTES
        || desc->parameter_size > TIDE_HOSTING_PARAMETER_BYTES || (desc->match && desc->parameter_size)
        || (desc->credential_size && (!desc->credential || !desc->secure))
        || (desc->parameter_size && !desc->parameters)) return NULL;
    tide_hosting_client *c = client_new(desc, now);
    host_link *l = c ? calloc(1, sizeof *l) : NULL;
    if (l) *l = (host_link){.transport = desc->transport, .server = desc->server};
    if (!l || !link_add(l, c)) {
        free(l);
        free(c);
        return NULL;
    }
    return c;
}

tide_hosting_client *tide_hosting_client_beside(tide_hosting_client *first, const uint64_t match, const uint64_t nonce,
                                               const double now)
{
    if (!first || !first->link || !match || !nonce || !isfinite(now)) return NULL;
    tide_hosting_client_desc d = first->desc;
    d.match = match;
    d.nonce = nonce;
    d.credential_size = d.parameter_size = 0;
    tide_hosting_client *c = client_new(&d, now);
    if (c && !link_add(first->link, c)) {
        free(c);
        return NULL;
    }
    return c;
}

tide_hosting_client *tide_hosting_client_of(const tide_transport transport)
{
    return transport.send == client_send ? transport.self : NULL;
}

void tide_hosting_client_destroy(tide_hosting_client *c)
{
    if (!c) return;
    host_link *l = c->link;
    for (uint32_t i = 0; i < l->count; i++) {
        if (l->members[i] != c) continue;
        l->members[i] = l->members[--l->count];
        break;
    }
    if (!l->count) { // The last admission over it: the transport goes
        l->transport.close(l->transport.self);
        free(l->members);
        free(l);
    }
    clear_queue(&c->queue);
    memset(c, 0, sizeof *c);
    free(c);
}

// A datagram for this admission: its reply, or its match's data
static void client_take(tide_hosting_client *c, const uint8_t kind, tide_reader *r, const double arrived)
{
    if (kind == HOST_REPLY) {
        const uint8_t result = tide_read_u8(r);
        const uint64_t match = tide_read_u64(r), id = tide_read_u64(r);
        if (r->failed || r->at != r->size || result > TIDE_HOSTING_WRONG_VERSION || !id
            || (result == TIDE_HOSTING_ACCEPTED && (!match || (c->desc.match && c->desc.match != match)))) return;
        // A delayed PENDING must not undo an accepted/refused decision.
        if (c->result > TIDE_HOSTING_ACCEPTED || (c->result == TIDE_HOSTING_ACCEPTED
            && (result == TIDE_HOSTING_PENDING || id != c->route))) return;
        c->result = (tide_hosting_result)result;
        c->match = match;
        c->route = id;
        if (result != TIDE_HOSTING_PENDING) memset(c->request, 0, sizeof c->request);
    } else if (kind == HOST_DATA) {
        take_data(c->parts, &c->queue, tide_hosting_client_address(c), arrived, r);
    }
}

void tide_hosting_client_update(tide_hosting_client *c, const double now)
{
    if (!c || !isfinite(now) || now < c->now) return;
    c->now = now;
    host_link *l = c->link;
    const tide_transport *t = &l->transport;
    for (uint32_t budget = 0; budget < HOST_BUDGET; budget++) {
        uint8_t bytes[TIDE_NET_MTU], kind;
        tide_address from;
        const uint32_t n = t->receive(t->self, &from, bytes, sizeof bytes);
        if (!n) break;
        if (!same_address(from, l->server)) continue;
        tide_reader r = {.data = bytes, .size = n};
        uint64_t route;
        if (!read_header(&r, &kind, &route)) continue;
        const double arrived = t->arrived ? t->arrived(t->self) : now;
        for (uint32_t i = 0; i < l->count; i++) {
            tide_hosting_client *m = l->members[i];
            if (kind == HOST_REPLY ? route == m->desc.nonce : kind == HOST_DATA && m->result == TIDE_HOSTING_ACCEPTED && route == m->route) {
                client_take(m, kind, &r, arrived);
                break;
            }
        }
    }
    if (c->result != TIDE_HOSTING_PENDING) return;
    if (now - c->began >= 10.0) { c->result = TIDE_HOSTING_TIMED_OUT; memset(c->request, 0, sizeof c->request); }
    else if (now - c->sent >= 0.25) {
        t->send(t->self, l->server, c->request, c->request_size);
        c->sent = now;
    }
}

tide_hosting_result tide_hosting_client_result(const tide_hosting_client *c)
{
    return c ? c->result : TIDE_HOSTING_INVALID;
}

uint64_t tide_hosting_client_match(const tide_hosting_client *c) { return c ? c->match : 0; }

tide_address tide_hosting_client_address(const tide_hosting_client *c)
{
    const uint64_t id = c ? c->match : 0;
    return (tide_address){.kind = TIDE_ADDRESS_NETWORK, .host = (uint32_t)id, .port = (uint16_t)(id >> 32)};
}

static void client_send(void *self, const tide_address to, const void *data, const uint32_t size)
{
    tide_hosting_client *c = self;
    if (!c->detached && c->result == TIDE_HOSTING_ACCEPTED && same_address(to, tide_hosting_client_address(c)))
        send_data(&c->link->transport, c->link->server, c->route, &c->sequence, data, size);
}

static uint32_t client_receive(void *self, tide_address *from, void *data, const uint32_t capacity)
{
    tide_hosting_client *c = self;
    return c->detached ? 0 : dequeue(&c->queue, from, &c->arrived, data, capacity);
}

static void client_close(void *self)
{
    tide_hosting_client *c = self;
    c->detached = true;
    clear_queue(&c->queue);
}

static double client_clock(void *self)
{
    const tide_hosting_client *c = self;
    const tide_transport *t = &c->link->transport;
    return t->clock ? t->clock(t->self) : c->now;
}

static double client_arrived(void *self) { return ((const tide_hosting_client *)self)->arrived; }

tide_transport tide_hosting_client_transport(tide_hosting_client *c)
{
    if (!c || c->detached) return (tide_transport){0};
    return (tide_transport){.self = c, .send = client_send, .receive = client_receive, .close = client_close,
        .clock = client_clock, .arrived = client_arrived};
}
