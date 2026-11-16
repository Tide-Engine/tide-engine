#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

#ifdef TIDE_IX_THREADS
#include "tide/platform.h"
#endif

// index_follow.tide: loops' indexes that take in what changed, and copies of a world that keep its indexes
// (notes/indexed-loops.md, Followed indexes and copies). The game checks itself: each indexed loop has a plain one
// beside it, and they have to find the same rows in the same order every tick. What the tests add is how the
// indexes came to be right, which the game's counts say (TIDE_INDEX_STATS): without them every test here would
// pass with each index made whole each time.

extern uint64_t tide_index_stats[5];
enum { AS_IT_WAS, FOLLOWED, WHOLE, CHUNKS, SHARED };

typedef struct counts {
    uint64_t n[5];
} counts;

static counts now(void)
{
    counts c;
    memcpy(c.n, tide_index_stats, sizeof c.n);
    return c;
}

// What was counted since `from`
static uint64_t since(const counts from, const int what)
{
    return tide_index_stats[what] - from.n[what];
}

static int finds; // Rows the loops found over a test: it has to be looking at something

// A tick, and the indexed loops against the plain ones beside them
static void tick_on(tide_world *w, const tide_jobs *jobs)
{
    if (jobs) tide_world_tick_on(w, jobs);
    else tide_world_tick(w);
    const bool same = w->Probe.found == w->Probe.plain && w->Probe.sum == w->Probe.plain_sum
        && w->Probe.kinds == w->Probe.kinds_plain && w->Probe.kind_sum == w->Probe.kind_plain_sum
        && w->Probe.listed == w->Probe.list_plain && w->Probe.list_sum == w->Probe.list_plain_sum && w->Probe.wrong == 0;
    if (!same) {
        printf("    tick %d: by place %d (plain %d), by kind %d (%d), listed %d (%d), crates wrong %d\n", (int)w->Time.tick,
               w->Probe.found, w->Probe.plain, w->Probe.kinds, w->Probe.kinds_plain, w->Probe.listed, w->Probe.list_plain,
               w->Probe.wrong);
    }
    TIDE_REQUIRE(same);
    TIDE_REQUIRE(w->Probe.seen > 0 || w->Made.count == 0);
    finds += w->Probe.found + w->Probe.kinds + w->Probe.listed;
}

static void tick(tide_world *w)
{
    tick_on(w, NULL);
}

static void order(tide_world *w, const int spawn, const int crates, const int destroy, const int shift, const int phase)
{
    w->Orders.spawn = spawn;
    w->Orders.crates = crates;
    w->Orders.destroy = destroy;
    w->Orders.shift = shift;
    w->Orders.phase = phase;
    w->Orders.below = 0;
    w->Orders.clear = 0;
}

static void quiet(tide_world *w)
{
    order(w, 0, 0, 0, 0, 0);
}

// 3,000 items in a table of three chunks and 300 with crates in another, then two quiet ticks
static void start(tide_world *w)
{
    tide_world_init(w, 1.0f / 60.0f);
    order(w, 3300, 300, 0, 0, 0);
    tick(w);
    quiet(w);
    tick(w);
    tick(w);
}

// The sites of the game's indexed loops over its items, which a copy can take: Seek's two and Near's. Browse's
// list is made each tick by whoever runs it
#define SITES 3u
#define LISTS 1u

TIDE_TEST(index_copy_runs_on_its_sources_indexes)
{
    tide_world a = {0}, b = {0}, c = {0}, d = {0};
    finds = 0;
    start(&a);
    // Nothing changes: the world's own indexes last
    counts from = now();
    tick(&a);
    TIDE_CHECK(since(from, WHOLE) == LISTS && since(from, AS_IT_WAS) >= SITES);
    // A copy takes them when it runs, and makes none
    tide_world_copy(&b, &a);
    from = now();
    TIDE_CHECK(since(from, SHARED) == 0); // Not before it runs: a world kept and never run takes nothing
    tick(&b);
    TIDE_CHECK(since(from, SHARED) == SITES);
    TIDE_CHECK(since(from, WHOLE) == LISTS);
    TIDE_CHECK(since(from, AS_IT_WAS) >= SITES);
    tick(&a);
    TIDE_CHECK(tide_world_hash(&a) == tide_world_hash(&b));
    // A copy of a copy that hasn't run takes the first's; so does the copy, after
    tide_world_copy(&c, &a);
    tide_world_copy(&d, &c);
    from = now();
    tick(&d);
    TIDE_CHECK(since(from, SHARED) == SITES && since(from, WHOLE) == LISTS);
    tick(&c);
    TIDE_CHECK(since(from, SHARED) == 2 * SITES && since(from, WHOLE) == 2 * LISTS);
    tick(&a);
    TIDE_CHECK(tide_world_hash(&a) == tide_world_hash(&c) && tide_world_hash(&a) == tide_world_hash(&d));
    // Copied back from its own copy, and a world copied over one that had indexes of other rows
    order(&d, 0, 0, 3, 0, 1);
    tick(&d);
    tick(&d);
    tide_world_copy(&a, &d);
    tide_world_copy(&d, &a);
    quiet(&a);
    quiet(&d);
    tick(&a);
    tick(&d);
    TIDE_CHECK(tide_world_hash(&a) == tide_world_hash(&d));
    tide_world_copy(&d, &b); // b is three ticks and two thirds of the items away
    quiet(&d);
    from = now();
    tick(&d);
    tick(&b);
    TIDE_CHECK(since(from, SHARED) == SITES);
    TIDE_CHECK(tide_world_hash(&b) == tide_world_hash(&d));
    TIDE_CHECK(finds > 500);
    // A source freed before its copies, with one that never ran; then the copies run on what they hold
    tide_world_copy(&c, &a);
    tide_world_copy(&b, &a);
    tick(&b);
    tide_world_free(&a);
    tick(&c);
    tick(&b);
    tick(&b);
    tide_world_free(&c);
    tick(&b);
    tide_world_free(&b);
    tide_world_free(&d);
}

// Run on with the same orders from the start by a world nobody copied: `orders` gives each tick's
typedef void orders_fn(tide_world *w, int t);

static uint64_t replayed(orders_fn *orders, const int ticks)
{
    tide_world w = {0};
    start(&w);
    for (int t = 0; t < ticks; t++) {
        orders(&w, t);
        tick(&w);
    }
    const uint64_t hash = tide_world_hash(&w);
    tide_world_free(&w);
    return hash;
}

static void source_orders(tide_world *w, const int t)
{
    if (t < 6) order(w, 20, 5, 7, 0, t % 7); // Items come and go
    else if (t < 10) quiet(w);
    else order(w, 0, 0, 0, 3, t % 3); // ...and move, once the copy is still
}

static void copy_orders(tide_world *w, const int t)
{
    if (t < 6) order(w, 0, 0, 0, 5, t % 5); // Items move where they are
    else if (t < 10) order(w, 35, 0, 11, 4, t % 4);
    else quiet(w);
}

TIDE_TEST(index_copy_and_its_source_part_ways)
{
    tide_world a = {0}, b = {0};
    finds = 0;
    start(&a);
    tide_world_copy(&b, &a);
    for (int t = 0; t < 14; t++) {
        source_orders(&a, t);
        copy_orders(&b, t);
        // Either first: neither's index is the other's once it changes
        if (t % 2) tick(&a);
        tick(&b);
        if (!(t % 2)) tick(&a);
        TIDE_CHECK(tide_world_hash(&a) != tide_world_hash(&b));
    }
    // Each is where a world of its own, never copied, gets with those orders
    TIDE_CHECK(tide_world_hash(&a) == replayed(source_orders, 14));
    TIDE_CHECK(tide_world_hash(&b) == replayed(copy_orders, 14));
    TIDE_CHECK(finds > 500);
    tide_world_free(&a);
    tide_world_free(&b);
}

static void ring_orders(tide_world *w, const int t)
{
    if (t % 9 == 4) order(w, 30, 4, 13, 6, t % 6); // As a repair landing: rows come, go and move
    else quiet(w);
}

// As a predicting client: each tick is run by a copy of the world before it, in a ring of worlds whose own
// indexes are of whatever they held last time round
TIDE_TEST(index_ring_of_copies_makes_an_index_only_when_rows_change)
{
    enum { RING = 5, TICKS = 40 };
    tide_world ring[RING] = {0};
    uint64_t at[TICKS + 1];
    finds = 0;
    start(&ring[0]);
    at[0] = tide_world_hash(&ring[0]);
    for (int t = 0; t < TICKS; t++) {
        tide_world *next = &ring[(t + 1) % RING];
        tide_world_copy(next, &ring[t % RING]);
        ring_orders(next, t);
        const counts from = now();
        tick(next);
        at[t + 1] = tide_world_hash(next);
        TIDE_CHECK(since(from, SHARED) == SITES);
        // A tick after one in which nothing changed makes no index but the list's
        if (t % 9 != 4 && t % 9 != 5) TIDE_CHECK(since(from, WHOLE) == LISTS);
    }
    TIDE_CHECK(at[TICKS] == replayed(ring_orders, TICKS));
    // Back three ticks, as after a guess that went wrong, and on again through the same slots
    for (int t = TICKS - 3; t < TICKS; t++) {
        tide_world *next = &ring[(t + 1) % RING];
        tide_world_copy(next, &ring[t % RING]);
        ring_orders(next, t);
        tick(next);
        TIDE_CHECK(tide_world_hash(next) == at[t + 1]);
    }
    TIDE_CHECK(finds > 500);
    for (int i = 0; i < RING; i++) tide_world_free(&ring[i]);
}

// A list's index is of one run of its world's ticks, which each cache counts for itself (its epoch). A copy that
// took its source's would find it "of this run" on the tick its own count reaches the source's, with the list
// changed where it is since: here the copy has run one tick fewer than its source when it's copied over
TIDE_TEST(index_of_a_list_is_never_a_copys)
{
    tide_world a = {0}, b = {0};
    start(&a);
    start(&b);
    tick(&a); // One tick more than b: a's list index is of its fourth run, b's next is its fourth
    tide_world_copy(&b, &a);
    const counts from = now();
    tick(&b);
    TIDE_CHECK(since(from, WHOLE) == LISTS);
    tick(&a);
    TIDE_CHECK(tide_world_hash(&a) == tide_world_hash(&b));
    // ...and the same through bytes, which carry no indexes
    const uint32_t size = tide_world_pack(&a, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes && tide_world_pack(&a, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&b, bytes, size));
    free(bytes);
    tick(&a);
    tick(&b);
    TIDE_CHECK(tide_world_hash(&a) == tide_world_hash(&b));
    tide_world_free(&a);
    tide_world_free(&b);
}
