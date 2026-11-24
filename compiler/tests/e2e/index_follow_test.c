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

extern uint64_t tide_index_stats[7];
// Loops' starts that found their index as it was, that took in what changed, and that made it whole; the table
// chunks taken in; the sites a copied world took; the rows looked at in the chunks taken in; the indexes copied
enum { AS_IT_WAS, FOLLOWED, WHOLE, CHUNKS, SHARED, ROWS, COPIED };

typedef struct counts {
    uint64_t n[7];
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
static int lit;   // ...and lamps

// A tick, and the indexed loops against the plain ones beside them
static void tick_on(tide_world *w, const tide_jobs *jobs)
{
    if (jobs) tide_world_tick_on(w, jobs);
    else tide_world_tick(w);
    const bool same = w->Probe.found == w->Probe.plain && w->Probe.sum == w->Probe.plain_sum
        && w->Probe.kinds == w->Probe.kinds_plain && w->Probe.kind_sum == w->Probe.kind_plain_sum
        && w->Probe.listed == w->Probe.list_plain && w->Probe.list_sum == w->Probe.list_plain_sum && w->Probe.wrong == 0
        && w->Probe.looked == w->Probe.looked_plain && w->Probe.lit == w->Probe.lit_plain
        && w->Probe.lit_sum == w->Probe.lit_plain_sum;
    if (!same) {
        printf("    tick %d: by place %d (plain %d), by kind %d (%d), listed %d (%d), crates wrong %d\n", (int)w->Time.tick,
               w->Probe.found, w->Probe.plain, w->Probe.kinds, w->Probe.kinds_plain, w->Probe.listed, w->Probe.list_plain,
               w->Probe.wrong);
    }
    TIDE_REQUIRE(same);
    TIDE_REQUIRE(w->Probe.seen > 0 || w->Made.count == 0);
    finds += w->Probe.found + w->Probe.kinds + w->Probe.listed;
    lit += w->Probe.lit;
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
    w->Orders.only = 0;
    w->Orders.again = 0;
    w->Orders.dim = 0;
}

static void quiet(tide_world *w)
{
    order(w, 0, 0, 0, 0, 0);
}

// 3,000 items in a table of three chunks and 500 with crates in another (and 200 lamps), then two quiet ticks
static void start(tide_world *w)
{
    tide_world_init(w, 1.0f / 60.0f);
    order(w, 3500, 500, 0, 0, 0);
    tick(w);
    quiet(w);
    tick(w);
    tick(w);
}

// The sites of the game's indexed loops over its items, which a copy can take: Seek's two and Near's (and Seek's
// third, while the test has it look for one item's place). Browse's list is made each tick by whoever runs it
#define SITES 3u
#define LISTS 1u
// ...and Glow's, of the lamps: an index of doubles, which a copy takes too, and which is made whole when a lamp changes
#define DOUBLES 1u

TIDE_TEST(index_copy_runs_on_its_sources_indexes)
{
    tide_world a = {0}, b = {0}, c = {0}, d = {0};
    finds = lit = 0;
    start(&a);
    // Nothing changes: the world's own indexes last
    counts from = now();
    tick(&a);
    TIDE_CHECK(since(from, WHOLE) == LISTS && since(from, AS_IT_WAS) == SITES + DOUBLES);
    // A copy takes them when it runs, and makes none
    tide_world_copy(&b, &a);
    from = now();
    TIDE_CHECK(since(from, SHARED) == 0); // Not before it runs: a world kept and never run takes nothing
    tick(&b);
    TIDE_CHECK(since(from, SHARED) == SITES + DOUBLES);
    TIDE_CHECK(since(from, WHOLE) == LISTS);
    TIDE_CHECK(since(from, AS_IT_WAS) == SITES + DOUBLES);
    tick(&a);
    TIDE_CHECK(tide_world_hash(&a) == tide_world_hash(&b));
    // A copy of a copy that hasn't run takes the first's; so does the copy, after
    tide_world_copy(&c, &a);
    tide_world_copy(&d, &c);
    from = now();
    tick(&d);
    TIDE_CHECK(since(from, SHARED) == SITES + DOUBLES && since(from, WHOLE) == LISTS);
    tick(&c);
    TIDE_CHECK(since(from, SHARED) == 2 * (SITES + DOUBLES) && since(from, WHOLE) == 2 * LISTS);
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
    TIDE_CHECK(since(from, SHARED) == SITES + DOUBLES);
    TIDE_CHECK(tide_world_hash(&b) == tide_world_hash(&d));
    TIDE_CHECK(finds > 500 && lit > 20);
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
    w->Orders.dim = t >= 8 && t % 2; // ...and its lamps change, some ticks
}

static void copy_orders(tide_world *w, const int t)
{
    if (t < 6) order(w, 0, 0, 0, 5, t % 5); // Items move where they are
    else if (t < 10) order(w, 35, 0, 11, 4, t % 4);
    else quiet(w);
    w->Orders.dim = t < 7 && t % 3 == 1; // The lamps' index, of doubles, is made whole by whichever side's change
}

TIDE_TEST(index_copy_and_its_source_part_ways)
{
    tide_world a = {0}, b = {0};
    finds = lit = 0;
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
    TIDE_CHECK(finds > 500 && lit > 20);
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
    finds = lit = 0;
    start(&ring[0]);
    at[0] = tide_world_hash(&ring[0]);
    for (int t = 0; t < TICKS; t++) {
        tide_world *next = &ring[(t + 1) % RING];
        tide_world_copy(next, &ring[t % RING]);
        ring_orders(next, t);
        const counts from = now();
        tick(next);
        at[t + 1] = tide_world_hash(next);
        TIDE_CHECK(since(from, SHARED) == SITES + DOUBLES);
        // No tick makes an index but the list's: where rows changed, the copy takes a copy of each index its source
        // holds and takes them in; where none did, it searches its source's
        TIDE_CHECK(since(from, WHOLE) == LISTS);
        // (A key written is there for that tick's loops; what's spawned and destroyed, for the next tick's)
        if (t % 9 == 4 || t % 9 == 5) TIDE_CHECK(since(from, COPIED) == SITES && since(from, ROWS) > 0);
        else TIDE_CHECK(since(from, COPIED) == 0 && since(from, ROWS) == 0);
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

// A tick in which nothing changed for the loops: each index as it was
static void as_it_was(tide_world *w)
{
    quiet(w);
    const counts from = now();
    tick(w);
    TIDE_CHECK(since(from, AS_IT_WAS) == SITES + DOUBLES && since(from, FOLLOWED) == 0 && since(from, WHOLE) == LISTS);
    TIDE_CHECK(since(from, ROWS) == 0 && since(from, COPIED) == 0);
}

// The tick the loops meet a change in: every site takes it in where its index is, `chunks` table chunks each, and
// none makes its index again
static counts taken_in(tide_world *w, const uint64_t chunks)
{
    const counts from = now();
    tick(w);
    TIDE_CHECK(since(from, FOLLOWED) == SITES && since(from, WHOLE) == LISTS && since(from, COPIED) == 0);
    if (since(from, CHUNKS) != chunks * SITES) printf("    %d table chunks taken in, not %d\n", (int)since(from, CHUNKS), (int)(chunks * SITES));
    TIDE_CHECK(since(from, CHUNKS) == chunks * SITES);
    return from;
}

// The items are 3,000 in a table of three chunks (1,024 rows each) and 500 with crates in a table of one. What
// a system spawns or destroys is there for the loops of the tick after; a key it writes, for the loops after it
TIDE_TEST(index_takes_in_the_rows_that_changed)
{
    tide_world w = {0};
    finds = lit = 0;
    start(&w);
    as_it_was(&w);

    // A key changed where it is: every fifth of the first 500 items, in the first chunk of their table, and of
    // the crates. Two chunks' pages are others; the rows looked at are the 200 that moved (fewer for the loop by
    // kind and x, whose columns some moves leave as they were)
    order(&w, 0, 0, 0, 5, 2);
    w.Orders.below = 1000;
    counts from = taken_in(&w, 2);
    TIDE_CHECK(since(from, ROWS) >= 2 * 200 && since(from, ROWS) <= SITES * 200);
    as_it_was(&w);

    // Rows come and go in one tick: 40 spawned, a ninth destroyed, and the holes filled from the tables' ends
    order(&w, 40, 5, 9, 0, 1);
    tick(&w);
    quiet(&w);
    from = taken_in(&w, 4);
    TIDE_CHECK(since(from, ROWS) > SITES * 400 && since(from, ROWS) < SITES * 1200); // Of 3,540 rows a site
    as_it_was(&w);

    // Half of them go: the table is a chunk shorter, and the rows moved into the holes are in other places
    order(&w, 0, 0, 2, 0, 1);
    tick(&w);
    quiet(&w);
    from = taken_in(&w, 4);
    as_it_was(&w);

    // 2,000 come: two chunks more, and nothing looked at but the new rows
    order(&w, 2000, 0, 0, 0, 0);
    tick(&w);
    quiet(&w);
    from = taken_in(&w, 3);
    TIDE_CHECK(since(from, ROWS) == SITES * 2000);
    as_it_was(&w);

    // Every item goes, and as many come again: with no rows a loop has nothing to search, and when it next has,
    // its index takes in tables that are all other rows
    w.Orders.clear = 1;
    tick(&w);
    quiet(&w);
    from = now();
    tick(&w);
    TIDE_CHECK(w.Probe.plain == 0 && since(from, FOLLOWED) == 0 && since(from, WHOLE) == LISTS);
    order(&w, 3500, 500, 0, 0, 0);
    tick(&w);
    quiet(&w);
    from = taken_in(&w, 5);
    TIDE_CHECK(since(from, ROWS) >= SITES * 3500);
    as_it_was(&w);
    TIDE_CHECK(finds > 500);
    tide_world_free(&w);
}

// A row that goes and another made in its place with the same key: the row's key columns are the same bytes
// before and after, and only its entity says an entry went and one came
TIDE_TEST(index_takes_in_a_row_made_where_one_went_with_its_key)
{
    enum { ITEM = 3400 }; // In the last chunk of its table, where a spawn fills a hole
    tide_world w = {0};
    start(&w);
    w.Orders.look = ITEM;
    tick(&w);
    TIDE_CHECK(w.Probe.looked == 1);
    w.Orders.only = ITEM;
    tick(&w); // Destroyed as this tick ends
    quiet(&w);
    tick(&w);
    TIDE_CHECK(w.Probe.looked == 0 && w.Probe.looked_plain == 0);
    w.Orders.again = ITEM;
    tick(&w); // Spawned as this one does, into the hole
    quiet(&w);
    const counts from = now();
    tick(&w);
    TIDE_CHECK(w.Probe.looked == 1 && w.Probe.looked_plain == 1);
    TIDE_CHECK(since(from, WHOLE) == LISTS && since(from, CHUNKS) == SITES + 1u); // One table chunk, for each site
    TIDE_CHECK(since(from, ROWS) == SITES + 1u);                                 // ...and one row of it
    tide_world_free(&w);
}

static void busy_orders(tide_world *w, const int t)
{
    order(w, t % 3 == 0 ? 25 : 0, 3, t % 4 == 1 ? 17 : 0, t % 2 ? 6 : 0, t % 5);
}

// A world rolled back to a copy of itself takes in the chunks that differ from what its indexes are of; and one
// copied every tick into a world that never runs, as a server keeps the world a tick began with, changes its
// indexes where they are, as if nobody had copied it
TIDE_TEST(index_follows_a_world_rolled_back_and_one_copied_every_tick)
{
    enum { TICKS = 8 };
    tide_world w = {0}, kept = {0}, other = {0};
    uint64_t at[TICKS];
    finds = lit = 0;
    start(&w);
    tide_world_copy(&kept, &w);
    for (int t = 0; t < TICKS; t++) {
        busy_orders(&w, t);
        tick(&w);
        at[t] = tide_world_hash(&w);
    }
    tide_world_copy(&w, &kept);
    counts from = now();
    for (int t = 0; t < TICKS; t++) {
        busy_orders(&w, t);
        tick(&w);
        TIDE_CHECK(tide_world_hash(&w) == at[t]);
    }
    TIDE_CHECK(since(from, WHOLE) == TICKS * LISTS && since(from, COPIED) == 0 && since(from, SHARED) == 0);
    TIDE_CHECK(since(from, FOLLOWED) >= TICKS * SITES / 2);
    // Copied each tick by a world that never runs
    from = now();
    for (int t = 0; t < TICKS; t++) {
        tide_world_copy(&other, &w);
        busy_orders(&w, t + 3);
        tick(&w);
    }
    TIDE_CHECK(since(from, WHOLE) == TICKS * LISTS && since(from, COPIED) == 0 && since(from, SHARED) == 0);
    TIDE_CHECK(since(from, FOLLOWED) >= TICKS * SITES / 2);
    // From bytes a world has no indexes: it makes them, once
    const uint32_t size = tide_world_pack(&w, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes && tide_world_pack(&w, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&other, bytes, size));
    free(bytes);
    quiet(&w);
    quiet(&other);
    from = now();
    tick(&other);
    TIDE_CHECK(since(from, WHOLE) == SITES + DOUBLES + LISTS);
    as_it_was(&other);
    tick(&w);
    tick(&w);
    TIDE_CHECK(tide_world_hash(&w) == tide_world_hash(&other));
    TIDE_CHECK(finds > 500);
    tide_world_free(&w);
    tide_world_free(&kept);
    tide_world_free(&other);
}

#ifdef TIDE_IX_THREADS
typedef struct limited_jobs {
    const tide_jobs *pool;
    uint32_t threads;
    uint32_t runs;
    void (*work)(void *context, uint32_t thread);
    void *context;
} limited_jobs;

static void limited_work(void *context, const uint32_t thread)
{
    limited_jobs *limited = context;
    if (thread < limited->threads) limited->work(limited->context, thread);
}

static void limited_run(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    limited_jobs *limited = self;
    limited->runs++;
    limited->work = work;
    limited->context = context;
    limited->pool->run(limited->pool->self, limited_work, limited);
}

// The same orders on one thread and on four, the world on four a copy of the tick before every third tick (its
// indexes its source's, taken in or copied as rows change): the same worlds, and every crate's search right
TIDE_TEST(index_follows_on_threads_as_on_one)
{
    const tide_jobs *pool = tide_platform_jobs();
    if (!pool || pool->threads < 2) {
        printf("    one core: nothing to compare\n");
        return;
    }
    const uint32_t threads = pool->threads < 4 ? pool->threads : 4;
    limited_jobs limited = {.pool = pool, .threads = threads};
    const tide_jobs jobs = {.self = &limited, .threads = threads, .run = limited_run};
    tide_world one = {0}, many[2] = {0};
    int on = 0;
    finds = lit = 0;
    start(&one);
    start(&many[0]);
    const counts from = now();
    for (int t = 0; t < 36; t++) {
        if (t % 3 == 0) {
            tide_world_copy(&many[!on], &many[on]);
            on = !on;
        }
        busy_orders(&one, t);
        busy_orders(&many[on], t);
        tick(&one);
        tick_on(&many[on], &jobs);
        if (tide_world_hash(&one) != tide_world_hash(&many[on])) printf("    %u threads differ at tick %d\n", threads, t);
        TIDE_REQUIRE(tide_world_hash(&one) == tide_world_hash(&many[on]));
    }
    TIDE_CHECK(since(from, WHOLE) == 2 * 36 * LISTS && since(from, COPIED) > 0 && since(from, FOLLOWED) > 36);
#ifndef __wasm__ // The web's ticks need more work before they take threads
    TIDE_CHECK(limited.runs > 0);
#endif
    TIDE_CHECK(finds > 500);
    tide_world_free(&one);
    tide_world_free(&many[0]);
    tide_world_free(&many[1]);
}
#endif

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
