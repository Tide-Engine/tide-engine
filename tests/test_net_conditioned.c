// A transport made worse (tide_net_conditioned): what tide run's --latency,
// --jitter and --loss put between a machine and a match it joins.

#include <string.h>

#include "tide/net.h"
#include "tide_test.h"

// Two ends of a loopback network with nothing in its way: the second goes
// through the transport made worse
typedef struct pair {
    tide_loopback *net;
    tide_transport near; // Endpoint 1
    tide_transport far;  // Endpoint 2, made worse
} pair;

static pair pair_make(const tide_net_conditions conditions, const uint64_t seed)
{
    pair p = {.net = tide_loopback_create(1)};
    p.near = tide_loopback_endpoint(p.net, 1);
    TIDE_CHECK(tide_net_conditioned(tide_loopback_endpoint(p.net, 2), conditions, seed, NULL, NULL, &p.far));
    return p;
}

static void pair_free(pair *p)
{
    p->far.close(p->far.self);
    p->near.close(p->near.self);
    tide_loopback_destroy(p->net);
}

static uint32_t take(const tide_transport t, char *out)
{
    tide_address from;
    const uint32_t size = t.receive(t.self, &from, out, 64);
    out[size] = '\0';
    return size;
}

TIDE_TEST(net_conditioned_delays_both_ways)
{
    pair p = pair_make((tide_net_conditions){.latency = 0.05}, 7);
    char got[65];
    p.far.send(p.far.self, tide_loopback_address(1), "out", 3);
    TIDE_CHECK(take(p.near, got) == 0); // Still on its way out
    tide_loopback_set_time(p.net, 0.049);
    TIDE_CHECK(take(p.far, got) == 0); // Using it moves what's due: nothing yet
    TIDE_CHECK(take(p.near, got) == 0);
    tide_loopback_set_time(p.net, 0.05);
    TIDE_CHECK(take(p.far, got) == 0);
    TIDE_CHECK(take(p.near, got) == 3 && strcmp(got, "out") == 0);

    tide_loopback_set_time(p.net, 0.1);
    p.near.send(p.near.self, tide_loopback_address(2), "in", 2);
    TIDE_CHECK(take(p.far, got) == 0); // It came, and waits its delay
    tide_loopback_set_time(p.net, 0.149);
    TIDE_CHECK(take(p.far, got) == 0);
    tide_loopback_set_time(p.net, 0.2);
    TIDE_CHECK(take(p.far, got) == 2 && strcmp(got, "in") == 0);
    // It arrived when its delay was over, by the inner transport's clock, so a ping counts it
    const double arrived = p.far.arrived(p.far.self);
    TIDE_CHECK(arrived > 0.1499 && arrived < 0.1501);
    TIDE_CHECK(p.far.clock(p.far.self) == 0.2);
    pair_free(&p);
}

TIDE_TEST(net_conditioned_jitter_reorders_and_repeats)
{
    uint8_t orders[2][40];
    for (int run = 0; run < 2; run++) {
        pair p = pair_make((tide_net_conditions){.latency = 0.02, .jitter = 0.1}, 99);
        for (uint8_t i = 0; i < 40; i++) p.far.send(p.far.self, tide_loopback_address(1), &i, 1);
        tide_loopback_set_time(p.net, 0.019);
        p.far.receive(p.far.self, &(tide_address){0}, (uint8_t[1]){0}, 1);
        uint8_t byte = 0;
        tide_address from;
        TIDE_CHECK(p.near.receive(p.near.self, &from, &byte, 1) == 0); // None comes before its latency
        tide_loopback_set_time(p.net, 0.121);
        p.far.receive(p.far.self, &from, &byte, 1);
        uint32_t n = 0;
        while (n < 40 && p.near.receive(p.near.self, &from, &byte, 1) == 1) orders[run][n++] = byte;
        TIDE_CHECK(n == 40); // All of them, within latency and jitter
        bool reordered = false;
        for (uint32_t i = 1; i < n; i++) reordered |= orders[run][i] < orders[run][i - 1];
        TIDE_CHECK(reordered);
        pair_free(&p);
    }
    TIDE_CHECK(memcmp(orders[0], orders[1], sizeof orders[0]) == 0); // The same seed, the same order
}

TIDE_TEST(net_conditioned_loses_its_share_each_way)
{
    pair p = pair_make((tide_net_conditions){.loss = 0.25}, 3);
    uint32_t out = 0, in = 0;
    tide_address from;
    uint8_t byte = 0;
    for (uint32_t i = 0; i < 1000; i++) {
        p.far.send(p.far.self, tide_loopback_address(1), "x", 1);
        p.near.send(p.near.self, tide_loopback_address(2), "y", 1);
        while (p.near.receive(p.near.self, &from, &byte, 1)) out++;
        while (p.far.receive(p.far.self, &from, &byte, 1)) in++;
    }
    TIDE_CHECK(out > 700 && out < 800);
    TIDE_CHECK(in > 700 && in < 800);
    pair_free(&p);

    p = pair_make((tide_net_conditions){.loss = 1.0}, 3);
    p.far.send(p.far.self, tide_loopback_address(1), "x", 1);
    p.near.send(p.near.self, tide_loopback_address(2), "y", 1);
    TIDE_CHECK(p.near.receive(p.near.self, &from, &byte, 1) == 0 && p.far.receive(p.far.self, &from, &byte, 1) == 0);
    pair_free(&p);
}

TIDE_TEST(net_conditioned_close_sends_what_was_on_its_way)
{
    pair p = pair_make((tide_net_conditions){.latency = 1.0}, 5);
    char got[65];
    p.far.send(p.far.self, tide_loopback_address(1), "bye", 3);
    p.far.close(p.far.self); // A goodbye goes, rather than never
    TIDE_CHECK(take(p.near, got) == 3 && strcmp(got, "bye") == 0);
    p.near.close(p.near.self);
    tide_loopback_destroy(p.net);
}

// A transport with no clock of its own, as a run of several players' ends are:
// the time is the clock it's given
typedef struct clockless {
    double now;
    uint32_t sent;
    bool closed;
} clockless;

static void clockless_send(void *self, const tide_address to, const void *data, const uint32_t size)
{
    (void)to;
    (void)data;
    (void)size;
    ((clockless *)self)->sent++;
}

static uint32_t clockless_receive(void *self, tide_address *from, void *data, const uint32_t capacity)
{
    (void)self;
    (void)from;
    (void)data;
    (void)capacity;
    return 0;
}

static void clockless_close(void *self)
{
    ((clockless *)self)->closed = true;
}

static double clockless_now(void *context)
{
    return ((const clockless *)context)->now;
}

TIDE_TEST(net_conditioned_takes_a_clock_where_the_transport_has_none)
{
    clockless inner = {0};
    tide_transport t;
    TIDE_CHECK(tide_net_conditioned((tide_transport){.self = &inner, .send = clockless_send, .receive = clockless_receive,
                                                     .close = clockless_close},
                                    (tide_net_conditions){.latency = 0.08}, 1, clockless_now, &inner, &t));
    TIDE_CHECK(t.end == NULL); // Nothing to end where the inner transport has nothing
    t.send(t.self, tide_loopback_address(1), "x", 1);
    inner.now = 0.07;
    t.receive(t.self, &(tide_address){0}, (uint8_t[1]){0}, 1);
    TIDE_CHECK(inner.sent == 0);
    inner.now = 0.08;
    t.receive(t.self, &(tide_address){0}, (uint8_t[1]){0}, 1);
    TIDE_CHECK(inner.sent == 1);
    TIDE_CHECK(t.clock(t.self) == 0.08);
    t.close(t.self);
    TIDE_CHECK(inner.closed);
}
