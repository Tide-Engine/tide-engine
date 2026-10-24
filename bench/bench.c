#include "bench.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tide/net.h"
#include "tide/platform.h"
#include "tide/shard_client.h"
#include "tide/shards.h"
#include "tide/time.h"

#define FRAME (1.0 / 60.0) // Made-up seconds between updates
#define JOIN_TIMEOUT 30.0  // Seconds a bot gets to join before the run fails
// A client has caught up when the ticks it has from the server are this close
// behind the server's (in ticks: half a second at 60 a second), and falls
// behind, failing the run, when they're further than twice that.
#define CAUGHT_UP 30u

static void out_of_memory(void)
{
    fprintf(stderr, "bench: out of memory\n");
    exit(1);
}

// Wall-clock times, in milliseconds, to take percentiles of.
typedef struct samples {
    float *ms;
    uint32_t count;
    uint32_t capacity;
} samples;

// ---------------------------------------------------------------------------
// The network the machines share: tide_loopback's, datagram for datagram (the
// same seed loses and delays the same ones, and each endpoint reads what has
// arrived in the same order), but with as many endpoints as a run makes, each
// with a queue of its own, so reading one doesn't go through everyone's: with
// hundreds of bots, the loopback's one queue was most of a client's update.

typedef struct packet {
    double at;
    uint64_t order;
    uint32_t from;
    uint32_t size;
    uint8_t *data;
} packet;

typedef struct wire wire;

typedef struct port {
    wire *net;
    uint32_t number; // Its address's host, from 1
    bool open;
    double arrived;
    packet *heap; // What's on its way to it, soonest first
    uint32_t count;
    uint32_t capacity;
} port;

struct wire {
    double now;
    tide_net_conditions conditions;
    uint64_t random;
    uint64_t sent;
    port **ports;
    uint32_t count;
    uint32_t capacity;
};

static double chance(wire *net)
{
    uint64_t z = (net->random += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (double)(z >> 11) * (1.0 / 9007199254740992.0);
}

static bool earlier(const packet *a, const packet *b)
{
    return a->at < b->at || (a->at == b->at && a->order < b->order);
}

static void push(port *p, const packet k)
{
    if (p->count == p->capacity) {
        p->capacity = p->capacity ? p->capacity * 2u : 16u;
        p->heap = realloc(p->heap, p->capacity * sizeof *p->heap);
        if (!p->heap) out_of_memory();
    }
    uint32_t i = p->count++;
    p->heap[i] = k;
    while (i > 0) {
        const uint32_t up = (i - 1u) / 2u;
        if (!earlier(&p->heap[i], &p->heap[up])) break;
        const packet t = p->heap[i];
        p->heap[i] = p->heap[up];
        p->heap[up] = t;
        i = up;
    }
}

static packet pop(port *p)
{
    const packet top = p->heap[0];
    p->heap[0] = p->heap[--p->count];
    uint32_t i = 0;
    for (;;) {
        const uint32_t l = 2u * i + 1u, r = l + 1u;
        uint32_t least = i;
        if (l < p->count && earlier(&p->heap[l], &p->heap[least])) least = l;
        if (r < p->count && earlier(&p->heap[r], &p->heap[least])) least = r;
        if (least == i) break;
        const packet t = p->heap[i];
        p->heap[i] = p->heap[least];
        p->heap[least] = t;
        i = least;
    }
    return top;
}

static void wire_send(void *self, const tide_address to, const void *data, const uint32_t size)
{
    const port *from = self;
    wire *net = from->net;
    if (to.kind != TIDE_ADDRESS_LOOPBACK || to.host < 1 || to.host > net->count) return;
    port *dest = net->ports[to.host - 1u];
    if (!dest->open) return; // Nobody there
    if (chance(net) < net->conditions.loss) return;
    uint8_t *copy = malloc(size ? size : 1u);
    if (!copy) out_of_memory();
    memcpy(copy, data, size);
    const double delay = net->conditions.latency + net->conditions.jitter * chance(net);
    push(dest, (packet){net->now + delay, net->sent++, from->number, size, copy});
}

static uint32_t wire_receive(void *self, tide_address *from, void *data, const uint32_t capacity)
{
    port *p = self;
    while (p->count && p->heap[0].at <= p->net->now) {
        const packet k = pop(p);
        if (k.size > capacity || k.size == 0) {
            free(k.data);
            continue;
        }
        memcpy(data, k.data, k.size);
        free(k.data);
        *from = tide_loopback_address(k.from);
        p->arrived = k.at;
        return k.size;
    }
    return 0;
}

static void wire_close(void *self)
{
    port *p = self;
    p->open = false;
    for (uint32_t i = 0; i < p->count; i++) free(p->heap[i].data);
    p->count = 0;
}

static wire *wire_create(const uint64_t seed, const tide_net_conditions conditions)
{
    wire *net = calloc(1, sizeof *net);
    if (!net) out_of_memory();
    net->random = seed;
    net->conditions = conditions;
    return net;
}

static void wire_destroy(wire *net)
{
    for (uint32_t i = 0; i < net->count; i++) {
        wire_close(net->ports[i]);
        free(net->ports[i]->heap);
        free(net->ports[i]);
    }
    free(net->ports);
    free(net);
}

// A new endpoint, at {TIDE_ADDRESS_LOOPBACK, *number}: numbered from 1, in the order they're made.
static tide_transport wire_endpoint(wire *net, uint32_t *number)
{
    if (net->count == net->capacity) {
        net->capacity = net->capacity ? net->capacity * 2u : 64u;
        net->ports = realloc(net->ports, net->capacity * sizeof *net->ports);
        if (!net->ports) out_of_memory();
    }
    port *p = calloc(1, sizeof *p);
    if (!p) out_of_memory();
    *p = (port){.net = net, .number = net->count + 1u, .open = true};
    net->ports[net->count++] = p;
    *number = p->number;
    return (tide_transport){p, wire_send, wire_receive, wire_close, NULL, NULL, NULL};
}

// ---------------------------------------------------------------------------
// Machines: the server (with --split, each shard's) and each bot's

typedef struct machine machine;

// A transport that counts what its machine sends.
typedef struct counted {
    tide_transport inner;
    machine *owner;
} counted;

struct machine {
    tide_client *client;          // A bot's, in a world one server runs...
    tide_shard_client *follower;  // ...or in a split one
    tide_server *server;          // A server's (a shard's), while it runs
    uint32_t bot;
    uint32_t port;    // A server's address
    double busy;      // A shard's last update, over a frame
    double created;   // Made-up seconds when it started joining (a server: started)
    double ended;     // ...and when a shard was merged away: 0 while it runs
    double connected; // ...when it had the world; 0 until then
    double caught_up; // ...and when it caught up with the match: it's in
    uint32_t max_lag; // Ticks it was behind the server at most, once in
    bool failed;
    uint32_t first_tick; // Its predicted tick once connected
    // A bot's machine's home's counts, which start again with each home (with
    // shards each counts its own ticks), and what they came to before
    const tide_client *home_seen;
    uint32_t seen_predicted, seen_late, seen_resyncs;
    uint64_t seen_world;
    uint64_t advanced, late, world_bytes; // Ticks it played, of them those its input was late for, and worlds' bytes
    uint32_t resyncs;
    double catching; // When it came back in at the root, having been in, to catch up again (0: it didn't)
    uint32_t rejoins;
    samples steady;      // Its updates once connected, while nobody joins...
    samples joining;     // ...and while somebody does
    // What its updates spent in the game's functions
    double tick_ms, copy_ms, hash_ms, pack_ms, unpack_ms;
    uint64_t ticks, copies, hashes, packs, unpacks;
    uint64_t bytes;
    counted **wires; // Its transports, counting
    uint32_t wire_count;
};

// The game, with its functions timed for the machine updating now. Machines
// update one at a time, so one at a time is `current`.
static const tide_game *real;
static tide_game timed;
static machine *current;

static double since(const uint64_t start)
{
    return (double)(tide_time_now_ns() - start) / 1e6;
}

static void timed_tick(void *world, const tide_jobs *jobs)
{
    const uint64_t start = tide_time_now_ns();
    real->tick(world, jobs);
    current->tick_ms += since(start);
    current->ticks++;
}

static void timed_tick_events(void *world, const tide_jobs *jobs, tide_event_log *events)
{
    const uint64_t start = tide_time_now_ns();
    real->tick_events(world, jobs, events);
    current->tick_ms += since(start);
    current->ticks++;
}

// A game with visibility's server ticks with this, noting what each player hears
static void timed_tick_client_events(void *world, const tide_jobs *jobs, tide_event_log *per_player)
{
    const uint64_t start = tide_time_now_ns();
    real->tick_client_events(world, jobs, per_player);
    current->tick_ms += since(start);
    current->ticks++;
}

static uint64_t timed_hash_on(const void *world, const tide_jobs *jobs)
{
    const uint64_t start = tide_time_now_ns();
    const uint64_t hash = real->hash_world_on(world, jobs);
    current->hash_ms += since(start);
    current->hashes++;
    return hash;
}

// ...and sends each player what they may have of the world, which their machine hashes and loads
static uint8_t *timed_client_state(const void *world, const tide_player_id recipient, uint32_t *size)
{
    const uint64_t start = tide_time_now_ns();
    uint8_t *data = real->client_state(world, recipient, size);
    current->pack_ms += since(start);
    current->packs++;
    return data;
}

static bool timed_load_client_state(void *world, const uint8_t *data, const uint32_t size)
{
    const uint64_t start = tide_time_now_ns();
    const bool ok = real->load_client_state(world, data, size);
    current->unpack_ms += since(start);
    current->unpacks++;
    return ok;
}

// --diverge: what the server says the traced bot's world is, by tick, and
// the ticks the bot's machine went otherwise
#define SAID 512u
static const bench_desc *diverging;
static machine *traced;          // The bot, once it's in
static tide_player_id traced_player;
static struct {
    uint32_t tick;
    uint64_t hash;
    uint8_t *bytes;
    uint32_t size;
} said[SAID];
static uint32_t diverged, diverge_reports;

static void compare(const void *world, const uint64_t hash)
{
    const uint32_t tick = diverging->tick_of(world);
    const uint32_t at = tick % SAID;
    if (current->server) { // What the server says
        free(said[at].bytes);
        said[at].tick = tick;
        said[at].hash = hash;
        said[at].bytes = real->client_state(world, traced_player, &said[at].size);
        return;
    }
    if (current != traced || said[at].tick != tick || !said[at].bytes || said[at].hash == hash) return;
    diverged++;
    if (diverge_reports >= 12u) return;
    diverge_reports++;
    void *server = calloc(1, real->world_size);
    if (!server) out_of_memory();
    if (real->load_client_state(server, said[at].bytes, said[at].size)) {
        printf("  bot %u's world at tick %u isn't the server's:\n", traced->bot, tick);
        diverging->differ(server, world, 8u);
    }
    if (real->free_world) real->free_world(server);
    free(server);
}

static uint64_t timed_client_hash(const void *world, const tide_player_id recipient)
{
    const uint64_t start = tide_time_now_ns();
    const uint64_t hash = real->client_hash(world, recipient);
    current->hash_ms += since(start);
    current->hashes++;
    if (diverging && traced && tide_player_equal(recipient, traced_player)) compare(world, hash);
    return hash;
}

static void timed_copy(void *to, const void *from)
{
    const uint64_t start = tide_time_now_ns();
    real->copy_world(to, from);
    current->copy_ms += since(start);
    current->copies++;
}

static uint64_t timed_hash(const void *world)
{
    const uint64_t start = tide_time_now_ns();
    const uint64_t hash = real->hash_world(world);
    current->hash_ms += since(start);
    current->hashes++;
    return hash;
}

static uint32_t timed_pack(const void *world, uint8_t *out, const uint32_t capacity)
{
    const uint64_t start = tide_time_now_ns();
    const uint32_t size = real->pack_world(world, out, capacity);
    current->pack_ms += since(start);
    if (out) current->packs++; // Not when it only asks the size
    return size;
}

static bool timed_unpack(void *world, const uint8_t *data, const uint32_t size)
{
    const uint64_t start = tide_time_now_ns();
    const bool ok = real->unpack_world(world, data, size);
    current->unpack_ms += since(start);
    current->unpacks++;
    return ok;
}

// Sessions send worlds as deltas (tide/delta.h): these are what pack them.
static uint8_t *timed_pack_delta(const void *world, const void *base, const uint8_t *need, const uint32_t need_size,
                                 bool assets, uint32_t *size)
{
    const uint64_t start = tide_time_now_ns();
    uint8_t *data = real->pack_delta(world, base, need, need_size, assets, size);
    current->pack_ms += since(start);
    current->packs++;
    return data;
}

static bool timed_unpack_delta(void *world, const void *base, const uint8_t *data, const uint32_t size)
{
    const uint64_t start = tide_time_now_ns();
    const bool ok = real->unpack_delta(world, base, data, size);
    current->unpack_ms += since(start);
    current->unpacks++;
    return ok;
}

static void counted_send(void *self, const tide_address to, const void *data, const uint32_t size)
{
    counted *c = self;
    c->owner->bytes += size;
    c->inner.send(c->inner.self, to, data, size);
}

static uint32_t counted_receive(void *self, tide_address *from, void *data, const uint32_t capacity)
{
    const counted *c = self;
    return c->inner.receive(c->inner.self, from, data, capacity);
}

static void counted_close(void *self)
{
    const counted *c = self;
    if (c->inner.close) c->inner.close(c->inner.self);
}

static void counted_end(void *self)
{
    const counted *c = self;
    if (c->inner.end) c->inner.end(c->inner.self);
}

static tide_transport count(machine *m, const tide_transport inner)
{
    counted *c = malloc(sizeof *c);
    counted **wires = realloc(m->wires, (m->wire_count + 1u) * sizeof *wires);
    if (!c || !wires) out_of_memory();
    *c = (counted){inner, m};
    m->wires = wires;
    m->wires[m->wire_count++] = c;
    return (tide_transport){.self = c, .send = counted_send, .receive = counted_receive, .close = counted_close,
                            .end = inner.end ? counted_end : NULL};
}

static void free_machine(machine *m)
{
    for (uint32_t i = 0; i < m->wire_count; i++) free(m->wires[i]);
    free(m->wires);
    free(m->steady.ms);
    free(m->joining.ms);
}

// The platform's pool, with only its first `threads` threads taking part.
typedef struct limited {
    tide_jobs jobs;
    const tide_jobs *pool;
} limited;

typedef struct limited_round {
    void (*work)(void *context, uint32_t thread);
    void *context;
    uint32_t threads;
} limited_round;

static void limited_work(void *context, const uint32_t thread)
{
    const limited_round *r = context;
    if (thread < r->threads) r->work(r->context, thread);
}

static void limited_run(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    const limited *l = self;
    limited_round r = {work, context, l->jobs.threads};
    l->pool->run(l->pool->self, limited_work, &r);
}

static limited the_limited;

// Threads to tick on: NULL for this one alone.
static const tide_jobs *jobs_for(const uint32_t threads)
{
    const tide_jobs *pool = tide_platform_jobs();
    if (!pool || threads == 1) return NULL;
    if (threads == 0 || threads >= pool->threads) return pool;
    the_limited = (limited){{&the_limited, threads, limited_run, NULL, NULL, NULL}, pool};
    return &the_limited.jobs;
}

static uint32_t thread_count(const tide_jobs *jobs)
{
    return jobs ? jobs->threads : 1u;
}

static void add_sample(samples *s, const double ms)
{
    if (s->count == s->capacity) {
        s->capacity = s->capacity ? s->capacity * 2u : 1024u;
        s->ms = realloc(s->ms, s->capacity * sizeof *s->ms);
        if (!s->ms) out_of_memory();
    }
    s->ms[s->count++] = (float)ms;
}

static void add_samples(samples *to, const samples *from)
{
    for (uint32_t i = 0; i < from->count; i++) add_sample(to, from->ms[i]);
}

static int compare_floats(const void *a, const void *b)
{
    const float x = *(const float *)a;
    const float y = *(const float *)b;
    return (x > y) - (x < y);
}

// ---------------------------------------------------------------------------
// The servers: one, or each shard's, made as the orchestrator starts them

static wire *the_net;
static double the_now;
static machine **hosts; // By shard: the server's is 0
static uint32_t host_room;

static machine *host_of(const uint32_t shard)
{
    if (shard >= host_room) {
        const uint32_t room = shard + 8u;
        hosts = realloc(hosts, room * sizeof *hosts);
        if (!hosts) out_of_memory();
        for (uint32_t i = host_room; i < room; i++) hosts[i] = NULL;
        host_room = room;
    }
    if (!hosts[shard]) {
        hosts[shard] = calloc(1, sizeof *hosts[shard]);
        if (!hosts[shard]) out_of_memory();
    }
    return hosts[shard];
}

static tide_server *host_start(void *user, const uint32_t shard, const tide_server_desc *desc, uint64_t *match)
{
    (void)user;
    machine *h = host_of(shard);
    tide_server_desc d = *desc;
    d.transports[0] = count(h, wire_endpoint(the_net, &h->port));
    h->created = the_now;
    h->ended = 0.0;
    machine *was = current;
    current = h;
    h->server = tide_server_create(&d, the_now);
    current = was;
    *match = shard + 1u;
    return h->server;
}

static void host_stop(void *user, const uint32_t shard, const uint64_t match)
{
    (void)user;
    (void)match;
    if (shard >= host_room || !hosts[shard] || !hosts[shard]->server) return;
    machine *h = hosts[shard];
    machine *was = current;
    current = h;
    tide_server_destroy(h->server);
    current = was;
    h->server = NULL;
    h->ended = the_now;
}

static float host_busy(void *user, const uint32_t shard)
{
    (void)user;
    return shard < host_room && hosts[shard] ? (float)hosts[shard]->busy : 0.0f;
}

// A bot's client of another shard, which its machine follows: `user` is the bot's machine.
static bool reach(void *user, const uint32_t shard, const uint64_t match, tide_transport *transport, tide_address *server)
{
    (void)match;
    if (shard >= host_room || !hosts[shard] || !hosts[shard]->server) return false;
    uint32_t number;
    *transport = count(user, wire_endpoint(the_net, &number));
    *server = tide_loopback_address(hosts[shard]->port);
    return true;
}

// A bot's client of the shard it plays in, or of the one server
static tide_client *home_of(const machine *m)
{
    return m->follower ? tide_shard_client_home(m->follower) : m->client;
}

// ---------------------------------------------------------------------------
// What a run found

// Of a set of samples, sorting them.
typedef struct timing {
    double median, p99, max;
    uint32_t count;
} timing;

static timing timing_of(samples *s)
{
    if (!s->count) return (timing){0};
    qsort(s->ms, s->count, sizeof *s->ms, compare_floats);
    return (timing){s->ms[s->count / 2u], s->ms[(uint32_t)((uint64_t)s->count * 99u / 100u)], s->ms[s->count - 1u], s->count};
}

// The server, a shard, or a client on average: its work per second of play.
typedef struct side {
    timing update, update_joining; // ms, while nobody joins and while one does
    double simulate_ms, hash_ms, snapshot_ms, pack_ms, unpack_ms;
    double ticks, hashes, snapshots, packs, unpacks; // Calls
    double sent;                                     // Bytes
} side;

// A shard, as the run ends
typedef struct shard_row {
    uint32_t id;
    bool gone;     // Merged away before the end
    bool region;   // Not the root: it has a box
    double lo, hi; // Along x
    tide_shard_status status;
    uint32_t held;
    side work;
} shard_row;

typedef struct results {
    double played; // Made-up seconds
    side server, client;
    double replays;     // Ticks clients ran for each one played
    timing world;       // Seconds until a bot had the world...
    timing caught_up;   // ...and until it caught up with the match
    uint32_t max_lag;   // Ticks a client was behind the server at most, once in
    double repairs;     // Bytes of worlds a second each bot's machine got, once in: its first, and repairs
    double pack_ms;     // Packing the world, each time
    uint32_t sent_size; // The world at the end, as sent...
    uint32_t size;      // ...and in memory
    uint32_t tick;
    uint64_t hash;
    uint32_t resyncs;
    bool joined; // Every bot joined
    bool ok;
    // What the bots' machines held at the end (bench_desc.held), on average and at most
    double held_mean;
    uint32_t held_most;
    // Their inputs that reached the server too late for their ticks, as a share of the ticks they played
    double late;
    double ping_ms; // Their network round trips, on average
    // With shards
    bool sharded;
    shard_row *shards;
    uint32_t shard_count;
    timing orchestra;     // Its updates: carrying messages and ghosts between shards, splitting and merging
    double ghost_bytes;   // A second, between shards
    double followed;      // Shards a bot's machine followed at the end, on average
    uint32_t home_moves;  // Times bots' homes moved to another shard, in all
    uint32_t rejoins;     // ...and of those, times one came back in at the root, catching up as on joining
    uint32_t joins;       // Clients of shards they made, in all
} results;

static side side_of(const machine *m, const double seconds)
{
    const double s = seconds > 0.0 ? seconds : 1.0;
    return (side){
        .simulate_ms = m->tick_ms / s,
        .hash_ms = m->hash_ms / s,
        .snapshot_ms = m->copy_ms / s,
        .pack_ms = m->pack_ms / s,
        .unpack_ms = m->unpack_ms / s,
        .ticks = (double)m->ticks / s,
        .hashes = (double)m->hashes / s,
        .snapshots = (double)m->copies / s,
        .packs = (double)m->packs / s,
        .unpacks = (double)m->unpacks / s,
        .sent = (double)m->bytes / s,
    };
}

static void add_side(side *to, const side *from)
{
    to->simulate_ms += from->simulate_ms;
    to->hash_ms += from->hash_ms;
    to->snapshot_ms += from->snapshot_ms;
    to->pack_ms += from->pack_ms;
    to->unpack_ms += from->unpack_ms;
    to->ticks += from->ticks;
    to->hashes += from->hashes;
    to->snapshots += from->snapshots;
    to->packs += from->packs;
    to->unpacks += from->unpacks;
    to->sent += from->sent;
}

static void collect(results *r, machine *bots_machines, const uint32_t bots, const bench_desc *d, const tide_shards *shards,
                    machine *orchestra)
{
    // The servers: each shard's work over the time it ran, and together
    samples all_steady = {0}, all_joining = {0};
    uint64_t packs = 0;
    double pack_ms = 0.0;
    r->sharded = shards != NULL;
    for (uint32_t i = 0; i < host_room; i++) {
        machine *h = hosts[i];
        if (!h) continue;
        const double ran = (h->ended > 0.0 ? h->ended : r->played) - h->created;
        side s = side_of(h, ran);
        add_side(&r->server, &s);
        add_samples(&all_steady, &h->steady);
        add_samples(&all_joining, &h->joining);
        packs += h->packs;
        pack_ms += h->pack_ms;
        if (!shards) continue;
        shard_row row = {.id = i, .gone = h->server == NULL, .work = s};
        row.work.update = timing_of(&h->steady);
        row.work.update_joining = timing_of(&h->joining);
        tide_shard_box box;
        if (h->server) {
            row.region = tide_shards_region(shards, i, &box);
            if (row.region) {
                row.lo = box.lo[0];
                row.hi = box.hi[0];
            }
            tide_shards_status(shards, i, &row.status);
            if (d->held) row.held = d->held(tide_server_world(h->server));
        }
        shard_row *rows = realloc(r->shards, (r->shard_count + 1u) * sizeof *rows);
        if (!rows) out_of_memory();
        r->shards = rows;
        r->shards[r->shard_count++] = row;
    }
    r->server.update = timing_of(&all_steady);
    r->server.update_joining = timing_of(&all_joining);
    r->pack_ms = packs ? pack_ms / (double)packs : 0.0;
    free(all_steady.ms);
    free(all_joining.ms);
    if (shards) {
        r->orchestra = timing_of(&orchestra->steady);
        r->ghost_bytes = (double)tide_shards_ghost_bytes(shards) / (r->played > 0.0 ? r->played : 1.0);
    }

    // Clients add up, over the time each was in the match
    machine sum = {0};
    double seconds = 0.0;
    uint64_t advanced = 0; // Ticks clients moved on from when they joined
    uint64_t late = 0, held = 0, followed = 0, worlds_bytes = 0;
    double ping = 0.0;
    uint32_t pinged = 0, holders = 0;
    samples steady = {0}, joining = {0}, worlds = {0}, joins = {0};
    for (uint32_t i = 0; i < bots; i++) {
        const machine *m = &bots_machines[i];
        sum.tick_ms += m->tick_ms;
        sum.hash_ms += m->hash_ms;
        sum.copy_ms += m->copy_ms;
        sum.unpack_ms += m->unpack_ms;
        sum.ticks += m->ticks;
        sum.hashes += m->hashes;
        sum.copies += m->copies;
        sum.unpacks += m->unpacks;
        sum.bytes += m->bytes;
        seconds += r->played - m->created;
        const tide_client *home = home_of(m);
        const tide_client_status s = tide_client_status_of(home);
        if (m->connected > 0.0) {
            advanced += m->advanced;
            add_sample(&worlds, m->connected - m->created);
            late += m->late;
            worlds_bytes += m->world_bytes;
            ping += s.ping_ms;
            pinged++;
        }
        if (m->caught_up > 0.0 && m->catching == 0.0) add_sample(&joins, m->caught_up - m->created);
        if (m->max_lag > r->max_lag) r->max_lag = m->max_lag;
        r->resyncs += m->resyncs;
        r->rejoins += m->rejoins;
        add_samples(&steady, &m->steady);
        add_samples(&joining, &m->joining);
        if (d->held && s.state == TIDE_SESSION_CONNECTED) {
            const void *world = tide_client_view(home).current;
            if (world) {
                const uint32_t n = d->held(world);
                held += n;
                holders++;
                if (n > r->held_most) r->held_most = n;
            }
        }
        if (m->follower) {
            for (uint32_t k = 0; k < tide_shard_client_room(m->follower); k++) followed += tide_shard_client_session(m->follower, k) != NULL;
            r->home_moves += tide_shard_client_moves(m->follower);
            r->joins += tide_shard_client_joins(m->follower);
        }
    }
    r->client = side_of(&sum, seconds);
    r->client.update = timing_of(&steady);
    r->client.update_joining = timing_of(&joining);
    r->replays = advanced ? (double)sum.ticks / (double)advanced : 0.0;
    r->late = advanced ? (double)late / (double)advanced : 0.0;
    r->repairs = seconds > 0.0 ? (double)worlds_bytes / seconds : 0.0;
    r->ping_ms = pinged ? ping / pinged : 0.0;
    r->held_mean = holders ? (double)held / holders : 0.0;
    r->followed = bots ? (double)followed / bots : 0.0;
    r->world = timing_of(&worlds);
    r->caught_up = timing_of(&joins);
    free(steady.ms);
    free(joining.ms);
    free(worlds.ms);
    free(joins.ms);

    // The world at the end, in memory and as it's sent whole (a delta from
    // nothing): with shards, every shard's, and their hashes together
    r->hash = 0;
    for (uint32_t i = 0; i < host_room; i++) {
        if (!hosts[i] || !hosts[i]->server) continue;
        const void *world = tide_server_world(hosts[i]->server);
        const uint32_t size = real->pack_world(world, NULL, 0);
        uint32_t sent = size;
        uint8_t *whole = real->pack_delta ? real->pack_delta(world, NULL, NULL, 0, false, &sent) : NULL;
        r->size += size;
        r->sent_size += whole ? sent : size;
        free(whole);
        const uint64_t hash = real->hash_world(world);
        r->hash = shards ? (r->hash ^ hash) * 0x100000001B3ull + i : hash;
    }
    r->tick = hosts[0] && hosts[0]->server ? tide_server_tick(hosts[0]->server) : 0u;
}

static void print_timing(const char *label, const timing *t)
{
    if (!t->count) printf("  %-26s %8s\n", label, "-");
    else printf("  %-26s %8.2f %8.2f %8.2f %8u\n", label, t->median, t->p99, t->max, t->count);
}

// "12.3 (45)": ms per second of play, and calls.
static void print_work(const double ms, const double calls)
{
    char cell[32];
    if (calls > 0.0) snprintf(cell, sizeof cell, calls >= 100.0 ? "%.2f (%.0f)" : "%.2f (%.3g)", ms, calls);
    else snprintf(cell, sizeof cell, "-");
    printf(" %16s", cell);
}

static void print_bytes(const char *label, const double bytes)
{
    if (bytes >= 1e6) printf("%s%.2f MB", label, bytes / 1e6);
    else printf("%s%.1f KB", label, bytes / 1e3);
}

static void print_side(const char *label, const side *s)
{
    printf("  %-26s", label);
    print_work(s->simulate_ms, s->ticks);
    print_work(s->hash_ms, s->hashes);
    print_work(s->snapshot_ms, s->snapshots);
    print_work(s->pack_ms, s->packs);
    print_work(s->unpack_ms, s->unpacks);
    printf("\n");
}

static void print_shards(const results *r)
{
    printf("\n  %-6s %-17s %7s %7s %7s %8s %8s %8s %10s %10s\n", "shard", "region (x)", "owned", "ghosts", "held", "median",
           "p99", "max", "busy (ms/s)", "sends/s");
    for (uint32_t i = 0; i < r->shard_count; i++) {
        const shard_row *s = &r->shards[i];
        char region[32];
        if (s->gone) snprintf(region, sizeof region, "merged away");
        else if (!s->region) snprintf(region, sizeof region, "root");
        else snprintf(region, sizeof region, "%.0f to %.0f", s->lo, s->hi);
        const double busy = s->work.simulate_ms + s->work.hash_ms + s->work.snapshot_ms + s->work.pack_ms + s->work.unpack_ms;
        printf("  %-6u %-17s %7u %7u %7u %8.2f %8.2f %8.2f %10.1f", s->id, region, s->status.owned, s->status.ghosts, s->held,
               s->work.update.median, s->work.update.p99, s->work.update.max, busy);
        print_bytes("   ", s->work.sent);
        printf("\n");
    }
    print_timing("between shards (ms)", &r->orchestra);
    print_bytes("  ghosts between shards: ", r->ghost_bytes);
    printf(" a second\n");
}

static void print_results(const results *r, const uint32_t bots, const bool held)
{
    printf("\n  %-26s %8s %8s %8s %8s\n", "update (ms)", "median", "p99", "max", "updates");
    print_timing(r->sharded ? "shards, each" : "server", &r->server.update);
    print_timing(r->sharded ? "shards, while one joins" : "server, while one joins", &r->server.update_joining);
    print_timing("clients", &r->client.update);
    print_timing("clients, while one joins", &r->client.update_joining);

    printf("\n  %-26s %16s %16s %16s %16s %16s\n", "ms per second (calls)", "simulate", "hash", "snapshot", "pack", "unpack");
    print_side(r->sharded ? "shards, together" : "server", &r->server);
    print_side("a client (mean)", &r->client);
    if (r->replays > 0.0) {
        printf("  clients ran %.1f ticks for each one played: the rest ran again, after a guess went wrong\n", r->replays);
    }
    if (r->sharded) print_shards(r);

    printf("\n");
    print_bytes(r->sharded ? "  sent per second: shards " : "  sent per second: server ", r->server.sent);
    if (bots) {
        print_bytes(" (", r->server.sent / bots);
        printf(" for each bot)");
    }
    print_bytes(", a client ", r->client.sent);
    printf("\n");
    if (held) {
        printf("  a bot's machine held %.0f on average, %u at most", r->held_mean, r->held_most);
        if (r->sharded) printf(", following %.2f shards", r->followed);
        printf("\n");
    }
    if (r->sharded) {
        printf("  bots' machines moved home to another shard %u times (%u of them coming back in at the root), and joined shards %u times\n",
               r->home_moves, r->rejoins, r->joins);
    }
    printf("  bots' inputs: %.2f%% reached the server too late for their tick; round trip %.0f ms on average\n", r->late * 100.0,
           r->ping_ms);
    print_bytes("  worlds a bot's machine got (its first, and repairs): ", r->repairs);
    printf(" a second; its verified world was %u ticks behind the server's at most\n", r->max_lag);
    if (r->world.count) {
        printf("  joining: the world took %.2f s median, %.2f s at most", r->world.median, r->world.max);
        if (r->caught_up.count) printf("; catching up, %.2f s median, %.2f s at most", r->caught_up.median, r->caught_up.max);
        printf("\n");
    }
    if (r->pack_ms > 0.0) {
        printf("  the server packed the world %.0f times, %.2f ms each", r->server.packs * r->played, r->pack_ms);
        if (r->caught_up.count) printf("; clients were %u ticks behind it at most, once in", r->max_lag);
        printf("\n");
    }
    print_bytes("  the world: ", (double)r->sent_size);
    print_bytes(" as sent, ", (double)r->size);
    printf(" in memory\n");
}

static void print_verdict(const results *r, const uint64_t expect)
{
    printf("\n  tick %u, hash 0x%016llx", r->tick, (unsigned long long)r->hash);
    if (expect) printf(r->hash == expect ? " as expected" : ", but 0x%016llx was expected", (unsigned long long)expect);
    printf("\n");
    if (r->resyncs) printf("  FAILED: clients' worlds went wrong %u times, and the server sent them again\n", r->resyncs);
    if (!r->joined) printf("  FAILED: not every bot joined\n");
    else if (!r->ok) printf("  FAILED\n");
}

// Saved results go here, one file per benchmark (see bench/compare.mjs).
#ifndef TIDE_BENCH_RESULTS
#define TIDE_BENCH_RESULTS "results"
#endif

static void json_text(FILE *f, const char *text)
{
    fputc('"', f);
    for (const unsigned char *c = (const unsigned char *)text; *c; c++) {
        if (*c == '"' || *c == '\\') fprintf(f, "\\%c", *c);
        else if (*c < 0x20) fprintf(f, "\\u%04x", *c);
        else fputc(*c, f);
    }
    fputc('"', f);
}

static void json_timing(FILE *f, const char *key, const timing *t)
{
    fprintf(f, ",\"%s\":{\"median\":%.6g,\"p99\":%.6g,\"max\":%.6g,\"count\":%u}", key, t->median, t->p99, t->max, t->count);
}

static void json_side(FILE *f, const char *key, const side *s)
{
    if (key) fprintf(f, ",\"%s\":", key);
    fprintf(f, "{\"simulate_ms\":%.6g,\"hash_ms\":%.6g,\"snapshot_ms\":%.6g,\"pack_ms\":%.6g,\"unpack_ms\":%.6g", s->simulate_ms,
            s->hash_ms, s->snapshot_ms, s->pack_ms, s->unpack_ms);
    fprintf(f, ",\"ticks\":%.6g,\"hashes\":%.6g,\"snapshots\":%.6g,\"packs\":%.6g,\"unpacks\":%.6g,\"sent_bytes\":%.6g", s->ticks,
            s->hashes, s->snapshots, s->packs, s->unpacks, s->sent);
    json_timing(f, "update_ms", &s->update);
    json_timing(f, "update_ms_joining", &s->update_joining);
    fputc('}', f);
}

// One line of JSON: what ran, where, and what it found. Work and bytes are
// per second of play.
static void write_record(FILE *f, const bench_desc *d, const bench_options *o, const uint32_t threads, const char *label,
                         const results *r)
{
    char date[32] = "";
    const time_t now = time(NULL);
    const struct tm *utc = gmtime(&now);
    if (utc) strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%SZ", utc);
#if defined(__wasm__)
    const char *os = "web";
#elif defined(__ANDROID__)
    const char *os = "android";
#elif defined(_WIN32)
    const char *os = "windows";
#elif defined(__APPLE__)
    const char *os = "macos";
#else
    const char *os = "linux";
#endif
#if defined(__wasm__)
    const char *arch = "wasm32";
#elif defined(__aarch64__) || defined(_M_ARM64)
    const char *arch = "arm64";
#elif defined(__arm__) || defined(_M_ARM)
    const char *arch = "arm";
#elif defined(__x86_64__) || defined(_M_X64)
    const char *arch = "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    const char *arch = "x86";
#else
    const char *arch = "unknown";
#endif
#if defined(__OPTIMIZE__)
    const bool optimized = true;
#else
    const bool optimized = false;
#endif
    fprintf(f, "{\"benchmark\":");
    json_text(f, d->name);
    fprintf(f, ",\"label\":");
    json_text(f, label);
    fprintf(f, ",\"date\":\"%s\",\"os\":\"%s\",\"arch\":\"%s\",\"optimized\":%s,\"threads\":%u,\"compiler\":", date, os, arch, optimized ? "true" : "false",
            threads);
    json_text(f, __clang_version__);
    const char *machine = getenv("TIDE_BENCH_MACHINE"), *revision = getenv("TIDE_BENCH_REVISION");
    fprintf(f, ",\"machine\":");
    json_text(f, machine ? machine : "unknown");
    fprintf(f, ",\"revision\":");
    json_text(f, revision ? revision : "unknown");
    fprintf(f, ",\"options\":{\"players\":%u,\"join_every\":%.6g,\"seconds\":%.6g,\"latency_ms\":%.6g,\"jitter_ms\":%.6g,"
               "\"loss_percent\":%.6g",
            o->players, o->join_every, o->seconds, o->latency * 1e3, o->jitter * 1e3, o->loss * 1e2);
    // Only split runs say how, so a run on one server compares with those saved before there were any
    if (o->split) fprintf(f, ",\"split\":%u,\"most\":%u,\"busiest_percent\":%.6g", o->split, o->most, o->busiest * 1e2);
    if (o->lag) fprintf(f, ",\"lag\":%u", o->lag);
    for (uint32_t i = 0; i < d->flag_count; i++) fprintf(f, ",\"%s\":%u", d->flags[i].name, d->flags[i].value);
    fprintf(f, "},\"ok\":%s,\"joined\":%s,\"resyncs\":%u,\"tick\":%u,\"hash\":\"0x%016llx\",\"played_s\":%.6g",
            r->ok ? "true" : "false", r->joined ? "true" : "false", r->resyncs, r->tick, (unsigned long long)r->hash,
            r->played);
    json_side(f, "server", &r->server);
    json_side(f, "client", &r->client);
    fprintf(f, ",\"replays\":%.6g,\"pack_ms_each\":%.6g,\"max_lag_ticks\":%u", r->replays, r->pack_ms, r->max_lag);
    json_timing(f, "world_s", &r->world);
    json_timing(f, "caught_up_s", &r->caught_up);
    fprintf(f, ",\"late_share\":%.6g,\"ping_ms\":%.6g,\"client_world_bytes\":%.6g", r->late, r->ping_ms, r->repairs);
    if (d->held) fprintf(f, ",\"client_held\":{\"mean\":%.6g,\"most\":%u}", r->held_mean, r->held_most);
    if (r->sharded) {
        fprintf(f, ",\"ghost_bytes\":%.6g,\"followed\":%.6g,\"home_moves\":%u,\"rejoins\":%u,\"shard_joins\":%u", r->ghost_bytes,
                r->followed, r->home_moves, r->rejoins, r->joins);
        json_timing(f, "between_shards_ms", &r->orchestra);
        fprintf(f, ",\"shards\":[");
        for (uint32_t i = 0; i < r->shard_count; i++) {
            const shard_row *s = &r->shards[i];
            fprintf(f, "%s{\"id\":%u,\"gone\":%s,\"owned\":%u,\"ghosts\":%u,\"held\":%u", i ? "," : "", s->id,
                    s->gone ? "true" : "false", s->status.owned, s->status.ghosts, s->held);
            if (s->region) fprintf(f, ",\"x\":[%.6g,%.6g]", s->lo, s->hi);
            fprintf(f, ",\"work\":");
            json_side(f, NULL, &s->work);
            fputc('}', f);
        }
        fputc(']', f);
    }
    fprintf(f, ",\"world_sent_bytes\":%u,\"world_bytes\":%u}\n", r->sent_size, r->size);
}

// Adds the run to TIDE_BENCH_RESULTS/<name>.jsonl, or prints its record where
// that can't be written (as on the web, which only sees its own folder).
static void save_results(const bench_desc *d, const bench_options *o, const uint32_t threads, const char *label,
                         const results *r)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.jsonl", TIDE_BENCH_RESULTS, d->name);
    FILE *f = fopen(path, "ab");
    if (!f) {
        printf("\n  couldn't add to %s; here's the record instead:\n", path);
        write_record(stdout, d, o, threads, label, r);
        return;
    }
    write_record(f, d, o, threads, label, r);
    fclose(f);
    printf("\n  saved as '%s' in %s\n", label, path);
}

static void usage(const bench_desc *d)
{
    printf("usage: %s [--quick] [options]\n\n", d->name);
    printf("  --quick             a short, small run, as tests make\n");
    printf("  --players <n>       bots that join, up to %u, the game's maxPlayers (%u)\n", tide_game_players(d->game),
           d->full.players);
    printf("  --join-every <s>    seconds between joins (%g)\n", d->full.join_every);
    printf("  --seconds <s>       how long to run once everyone's in (%g)\n", d->full.seconds);
    printf("  --threads <n>       threads to tick on, 0 for every core (%u)\n", d->full.threads);
    printf("  --latency <ms>      network delay each way (%g)\n", d->full.latency * 1e3);
    printf("  --jitter <ms>       up to this much more, at random (%g)\n", d->full.jitter * 1e3);
    printf("  --loss <percent>    datagrams lost (%g)\n", d->full.loss * 1e2);
    if (d->game->shards) {
        printf("  --split <n>         regions along x the world starts split into, each its own shard; 0 for one server (%u)\n",
               d->full.split);
        printf("  --most <n>          entities a region's shard owns before it splits; 0 never (%u)\n", d->full.most);
        printf("  --busiest <percent> of a frame a region's shard's updates take, steadily, before it splits; 0 never (%g)\n",
               d->full.busiest * 1e2);
    }
    printf("  --lag <ticks>       how far behind the server a bot's verified world may fall before the run fails (%u)\n",
           2u * CAUGHT_UP);
    if (d->differ && d->game->client_state) {
        printf("  --diverge <bot>     says what differs, each tick, between that bot's world and what the server says it is\n");
    }
    printf("  --expect <hash>     the server's world's hash at the end; fails on another\n");
    printf("  --save <label>      adds the results to bench/results/%s.jsonl (see bench/compare.mjs)\n", d->name);
    for (uint32_t i = 0; i < d->flag_count; i++) {
        printf("  --%-17s %s (%u)\n", d->flags[i].name, d->flags[i].help, d->flags[i].full);
    }
}

static bool read_number(const char *text, uint32_t *out)
{
    if (*text < '0' || *text > '9') return false;
    for (const char *c = text; *c; c++) if (*c < '0' || *c > '9') return false;
    errno = 0;
    char *end;
    const unsigned long long number = strtoull(text, &end, 10);
    if (errno == ERANGE || *end || number > UINT32_MAX) return false;
    *out = (uint32_t)number;
    return true;
}

// The options from the command line, over a full run's or --quick's.
static bool parse(const bench_desc *d, const int argc, char **argv, bench_options *o, const char **save)
{
    bool quick = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quick") == 0) quick = true;
    }
    *o = quick ? d->quick : d->full;
    for (uint32_t i = 0; i < d->flag_count; i++) d->flags[i].value = quick ? d->flags[i].quick : d->flags[i].full;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--quick") == 0) continue;
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) return false;
        if (i + 1 >= argc || strncmp(arg, "--", 2) != 0) {
            fprintf(stderr, "%s: '%s' isn't an option, or it needs a value\n", d->name, arg);
            return false;
        }
        const char *value = argv[++i];
        char *end = NULL;
        errno = 0;
        const double number = strtod(value, &end);
        const bool numeric = end != value && *end == '\0' && errno != ERANGE && isfinite(number) && number >= 0.0;
        uint32_t integer = 0;
        const bool whole = read_number(value, &integer);
        bool known = true;
        if (strcmp(arg, "--save") == 0) {
            *save = value;
        } else if (strcmp(arg, "--expect") == 0) {
            errno = 0;
            o->expect = strtoull(value, &end, 0);
            if (*value < '0' || *value > '9' || end == value || *end != '\0' || errno == ERANGE) known = false;
        } else if (!numeric) {
            known = false;
        } else if (strcmp(arg, "--players") == 0) {
            known = whole;
            if (known) o->players = integer;
        } else if (strcmp(arg, "--join-every") == 0) {
            o->join_every = number;
        } else if (strcmp(arg, "--seconds") == 0) {
            o->seconds = number;
        } else if (strcmp(arg, "--threads") == 0) {
            known = whole;
            if (known) o->threads = integer;
        } else if (strcmp(arg, "--latency") == 0) {
            o->latency = number / 1e3;
        } else if (strcmp(arg, "--jitter") == 0) {
            o->jitter = number / 1e3;
        } else if (strcmp(arg, "--loss") == 0) {
            o->loss = number / 1e2;
        } else if (strcmp(arg, "--split") == 0 && d->game->shards) {
            known = whole;
            if (known) o->split = integer;
        } else if (strcmp(arg, "--most") == 0 && d->game->shards) {
            known = whole;
            if (known) o->most = integer;
        } else if (strcmp(arg, "--busiest") == 0 && d->game->shards) {
            o->busiest = number / 1e2;
        } else if (strcmp(arg, "--lag") == 0) {
            known = whole && integer > 0;
            if (known) o->lag = integer;
        } else if (strcmp(arg, "--diverge") == 0 && d->differ && d->game->client_state) {
            known = whole && integer < INT32_MAX;
            if (known) o->diverge = integer + 1u;
        } else {
            known = false;
            for (uint32_t k = 0; k < d->flag_count; k++) {
                if (strcmp(arg + 2, d->flags[k].name) == 0) {
                    known = whole && integer <= INT32_MAX && integer >= d->flags[k].minimum;
                    if (known) d->flags[k].value = integer;
                }
            }
        }
        if (!known) {
            fprintf(stderr, "%s: '%s %s' isn't a valid option and value\n", d->name, arg, value);
            return false;
        }
    }
    if (o->players < 1 || o->players > tide_game_players(d->game)) {
        fprintf(stderr, "%s: --players goes from 1 to %u, the game's maxPlayers\n", d->name, tide_game_players(d->game));
        return false;
    }
    if (o->loss > 1.0) {
        fprintf(stderr, "%s: --loss goes from 0 to 100 percent\n", d->name);
        return false;
    }
    if (!o->split && (o->most || o->busiest > 0.0)) {
        fprintf(stderr, "%s: --most and --busiest split a world that's split to start with (--split)\n", d->name);
        return false;
    }
    if (!o->split && !d->make_world) {
        fprintf(stderr, "%s: runs only split (--split)\n", d->name);
        return false;
    }
    return true;
}

// A bot's input: `user` is its machine.
static const bench_desc *the_desc;

static void sample(void *user, const uint32_t tick, void *input)
{
    const machine *m = user;
    memset(input, 0, real->input_size);
    the_desc->sample(the_desc->flags, m->bot, tick, input);
}

int bench_main(const bench_desc *d, const int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(d);
            return 0;
        }
    }
    bench_options o;
    const char *save = NULL;
    if (!parse(d, argc, argv, &o, &save)) {
        usage(d);
        return 2;
    }
    the_desc = d;
    real = d->game;
    timed = *d->game;
    timed.tick = timed_tick;
    timed.copy_world = timed_copy;
    timed.hash_world = timed_hash;
    timed.pack_world = timed_pack;
    timed.unpack_world = timed_unpack;
    if (real->pack_delta) timed.pack_delta = timed_pack_delta;
    if (real->unpack_delta) timed.unpack_delta = timed_unpack_delta;
    if (real->tick_events) timed.tick_events = timed_tick_events;
    if (real->tick_client_events) timed.tick_client_events = timed_tick_client_events;
    if (real->hash_world_on) timed.hash_world_on = timed_hash_on;
    if (real->client_state) timed.client_state = timed_client_state;
    if (real->load_client_state) timed.load_client_state = timed_load_client_state;
    if (real->client_hash) timed.client_hash = timed_client_hash;

    const tide_jobs *jobs = jobs_for(o.threads);
    diverging = o.diverge ? d : NULL;

#if defined(__OPTIMIZE__)
    const char *build = "optimized";
#else
    const char *build = "unoptimized (the times mean little)";
#endif
#if defined(__wasm__)
    const char *platform = "web";
#else
    const char *platform = "native";
#endif
    printf("%s: %u bots, joining %g s apart, then %g s; %g ms each way (+%g), %g%% lost; %u thread%s; %s %s build\n", d->name,
           o.players, o.join_every, o.seconds, o.latency * 1e3, o.jitter * 1e3, o.loss * 1e2, thread_count(jobs),
           thread_count(jobs) == 1 ? "" : "s", build, platform);
    if (o.split) {
        printf("  split into %u region%s along x", o.split, o.split == 1 ? "" : "s");
        if (o.most) printf(", a region splitting past %u entities", o.most);
        if (o.busiest > 0.0) printf(", or when busy %g%% of a frame", o.busiest * 1e2);
        printf("\n");
    }
    if (d->flag_count) {
        printf("  ");
        for (uint32_t i = 0; i < d->flag_count; i++) printf("%s%s %u", i ? ", " : "", d->flags[i].name, d->flags[i].value);
        printf("\n");
    }
    fflush(stdout);

    the_net = wire_create(0x7469646562656e63ull, (tide_net_conditions){.latency = o.latency, .jitter = o.jitter, .loss = o.loss});
    the_now = 0.0;
    machine *bots_machines = calloc(o.players, sizeof *bots_machines);
    machine orchestra = {0}; // What the orchestrator does between the shards' updates
    if (!bots_machines) out_of_memory();

    // The server, endpoint 1: on its own, or the root, which the orchestrator starts
    tide_shards *shards = NULL;
    if (o.split) {
        const tide_shards_desc sd = {
            .game = &timed,
            .dt = (float)FRAME,
            .world = d->world,
            .split = {o.split, 1, 1},
            .most_entities = o.most,
            .busiest = (float)o.busiest,
            .busy = host_busy,
            .jobs = jobs,
            .host_start = host_start,
            .host_stop = host_stop,
        };
        current = &orchestra;
        shards = tide_shards_create(&sd, 0.0);
        if (!shards || !hosts || !hosts[0] || !hosts[0]->server) {
            fprintf(stderr, "%s: the shards didn't start\n", d->name);
            return 1;
        }
    } else {
        machine *h = host_of(0);
        current = h;
        void *first = calloc(1, real->world_size);
        if (!first) out_of_memory();
        d->make_world(d->flags, first, (float)FRAME);
        const tide_server_desc server_desc = {
            .game = &timed,
            .dt = (float)FRAME,
            .transports = {count(h, wire_endpoint(the_net, &h->port))},
            .jobs = jobs,
            .world = first,
        };
        h->server = tide_server_create(&server_desc, 0.0);
        if (real->free_world) real->free_world(first); // The server has its own copy
        free(first);
    }

    double now = 0.0;
    uint32_t bots = 0;
    double all_in = -1.0; // When the last one caught up
    bool failed = false;
    const uint32_t most_lag = o.lag ? o.lag : 2u * CAUGHT_UP;
    while (true) {
        if (bots < o.players && now >= (double)bots * o.join_every) {
            machine *m = &bots_machines[bots];
            m->bot = bots;
            m->created = now;
            current = m;
            uint32_t number;
            const tide_client_desc client_desc = {
                .game = &timed,
                .transport = count(m, wire_endpoint(the_net, &number)),
                .server = tide_loopback_address(hosts[0]->port),
                .sample = real->input_size ? sample : NULL,
                .user = m,
                .lead = 2,
                .jobs = jobs,
            };
            if (shards) {
                const tide_shard_client_desc follower = {.client = client_desc, .reach = reach, .user = m};
                m->follower = tide_shard_client_create(&follower, now);
            } else {
                m->client = tide_client_create(&client_desc, now);
            }
            bots++;
        }

        now += FRAME;
        the_now = now;
        the_net->now = now;
        bool joining = false; // Somebody is, as this frame starts
        for (uint32_t i = 0; i < bots; i++) joining |= bots_machines[i].caught_up == 0.0 && !bots_machines[i].failed;

        for (uint32_t i = 0; i < bots; i++) {
            machine *m = &bots_machines[i];
            if (m->failed) continue;
            current = m;
            const uint64_t t = tide_time_now_ns();
            if (m->follower) tide_shard_client_update(m->follower, now);
            else tide_client_update(m->client, now);
            if (m->connected > 0.0) add_sample(joining ? &m->joining : &m->steady, since(t));
        }
        for (uint32_t i = 0; i < host_room; i++) {
            machine *h = hosts[i];
            if (!h || !h->server) continue;
            current = h;
            const uint64_t t = tide_time_now_ns();
            tide_server_update(h->server, now);
            const double ms = since(t);
            add_sample(joining ? &h->joining : &h->steady, ms);
            h->busy = ms / (FRAME * 1e3);
        }
        if (shards) {
            current = &orchestra;
            const uint64_t t = tide_time_now_ns();
            tide_shards_update(shards, now);
            add_sample(&orchestra.steady, since(t));
        }

        bool everyone = bots == o.players;
        for (uint32_t i = 0; i < bots; i++) {
            machine *m = &bots_machines[i];
            if (m->failed) continue;
            const tide_client *home = home_of(m);
            const tide_client_status s = tide_client_status_of(home);
            const bool in = s.state == TIDE_SESSION_CONNECTED;
            // Its home's server's ticks: each shard counts its own
            const uint32_t shard = m->follower ? tide_shard_client_home_shard(m->follower) : 0u;
            const tide_server *home_server = shard < host_room && hosts[shard] ? hosts[shard]->server : NULL;
            const uint32_t server_tick = home_server ? tide_server_tick(home_server) : s.server_tick;
            if (in && m->connected == 0.0) {
                m->connected = now;
                m->first_tick = s.predicted_tick;
                if (o.diverge == m->bot + 1u) {
                    traced = m;
                    traced_player = s.player;
                }
            }
            const uint32_t lag = in && server_tick > s.verified_tick ? server_tick - s.verified_tick : 0u;
            if (in && home != m->home_seen) {
                // A new home: it moved to a shard it followed, or came back in at the root, which it catches up with as on joining
                if (m->home_seen && m->caught_up > 0.0 && lag > CAUGHT_UP) {
                    m->caught_up = 0.0;
                    m->catching = now;
                    m->rejoins++;
                }
                const bool first = !m->home_seen;
                m->home_seen = home;
                m->seen_predicted = s.predicted_tick;
                m->seen_late = first ? 0u : s.late_ticks;
                m->seen_world = first ? 0u : s.world_bytes;
                m->seen_resyncs = first ? 0u : s.resyncs;
            }
            if (in) {
                if (s.predicted_tick > m->seen_predicted) m->advanced += s.predicted_tick - m->seen_predicted;
                if (s.late_ticks > m->seen_late) m->late += s.late_ticks - m->seen_late;
                if (s.world_bytes > m->seen_world) m->world_bytes += s.world_bytes - m->seen_world;
                if (s.resyncs > m->seen_resyncs) m->resyncs += s.resyncs - m->seen_resyncs;
                m->seen_predicted = s.predicted_tick;
                m->seen_late = s.late_ticks;
                m->seen_world = s.world_bytes;
                m->seen_resyncs = s.resyncs;
            }
            if (in && m->caught_up == 0.0 && lag <= CAUGHT_UP) m->caught_up = now;
            if (m->caught_up > 0.0 && lag > m->max_lag) m->max_lag = lag;
            if (s.state == TIDE_SESSION_OFFLINE) {
                printf("  bot %u went offline (reason %d) at %.2f s; stopping\n", m->bot, (int)s.reason, now);
                m->failed = failed = true;
            } else if (m->caught_up == 0.0 && now - (m->catching > 0.0 ? m->catching : m->created) > JOIN_TIMEOUT) {
                if (m->connected > 0.0) {
                    printf("  bot %u got the world after %.2f s, but in %g s it never caught up with the match; stopping\n",
                           m->bot, m->connected - m->created, JOIN_TIMEOUT);
                } else {
                    printf("  bot %u didn't get the world in %g s; stopping\n", m->bot, JOIN_TIMEOUT);
                }
                m->failed = failed = true;
            } else if (m->max_lag > most_lag) {
                printf("  bot %u fell %u ticks behind the server at %.2f s; stopping\n", m->bot, m->max_lag, now);
                // What its machine follows, with shards: each client's state, its world's ticks and the server's
                for (uint32_t k = 0; m->follower && k < tide_shard_client_room(m->follower); k++) {
                    const tide_client *f = tide_shard_client_session(m->follower, k);
                    if (!f) continue;
                    const tide_client_status fs = tide_client_status_of(f);
                    const tide_server *server = k < host_room && hosts[k] ? hosts[k]->server : NULL;
                    printf("    shard %u%s: state %d, verified %u, predicted %u, its server at %u%s, repairs %llu bytes\n", k,
                           k == tide_shard_client_home_shard(m->follower) ? " (home)" : "", (int)fs.state, fs.verified_tick,
                           fs.predicted_tick, server ? tide_server_tick(server) : 0u, server ? "" : " (ended)",
                           (unsigned long long)fs.world_bytes);
                }
                m->failed = failed = true;
            }
            everyone &= m->caught_up > 0.0;
        }
        if (failed) break;
        if (everyone && all_in < 0.0) all_in = now;
        if (all_in >= 0.0 && now - all_in >= o.seconds) break;
    }

    if (diverging) {
        printf("\n  bot %u's world went otherwise than the server's at %u ticks\n", o.diverge - 1u, diverged);
        for (uint32_t i = 0; i < SAID; i++) free(said[i].bytes);
    }
    results r = {.played = now, .joined = !failed};
    collect(&r, bots_machines, bots, d, shards, &orchestra);
    r.ok = r.joined && r.resyncs == 0 && (!o.expect || r.hash == o.expect);
    print_results(&r, bots, d->held != NULL);
    if (d->describe) {
        for (uint32_t i = 0; i < host_room; i++) {
            if (!hosts[i] || !hosts[i]->server) continue;
            if (shards) printf("  shard %u:\n", i);
            d->describe(tide_server_world(hosts[i]->server));
        }
    }
    print_verdict(&r, o.expect);
    if (save) save_results(d, &o, thread_count(jobs), save, &r);

    for (uint32_t i = 0; i < bots; i++) {
        machine *m = &bots_machines[i];
        current = m;
        if (m->follower) tide_shard_client_destroy(m->follower);
        else tide_client_destroy(m->client);
        free_machine(m);
    }
    if (shards) {
        current = &orchestra;
        tide_shards_destroy(shards);
    } else if (hosts[0]->server) {
        current = hosts[0];
        tide_server_destroy(hosts[0]->server);
    }
    for (uint32_t i = 0; i < host_room; i++) {
        if (!hosts[i]) continue;
        free_machine(hosts[i]);
        free(hosts[i]);
    }
    free(hosts);
    hosts = NULL;
    host_room = 0;
    free_machine(&orchestra);
    free(r.shards);
    free(bots_machines);
    wire_destroy(the_net);
    return r.ok ? 0 : 1;
}
