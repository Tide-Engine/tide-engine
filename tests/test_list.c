#include <string.h>

#include "tide/list.h"
#include "tide/text.h"
#include "tide_test.h"

typedef struct list_world {
    tide_list scores;
    tide_list other;
    tide_heap heap;
} list_world;

static list_world world;

static void start(void)
{
    tide_heap_free(&world.heap);
    memset(&world, 0, sizeof world);
    tide_text_use(&world.heap, NULL);
}

static void add(tide_list *l, const int32_t v, const uint32_t where)
{
    int32_t *slot = tide_list_add(l, sizeof v, where);
    if (slot) *slot = v;
}

static int32_t at(const tide_list l, const int32_t i)
{
    const int32_t *p = tide_list_at(l, i, sizeof(int32_t));
    return p ? *p : -999;
}

TIDE_TEST(list_world_lists_grow_in_their_heap)
{
    start();
    for (int32_t i = 0; i < 100; i++) add(&world.scores, i, TIDE_IN_MATCH);
    TIDE_CHECK(tide_list_count(world.scores) == 100);
    TIDE_CHECK(at(world.scores, 0) == 0 && at(world.scores, 99) == 99);
    TIDE_CHECK(at(world.scores, 100) == -999 && at(world.scores, -1) == -999);
    TIDE_CHECK(world.scores.at >> 30 == TIDE_IN_MATCH);

    tide_list_remove_at(&world.scores, 10, sizeof(int32_t));
    TIDE_CHECK(tide_list_count(world.scores) == 99 && at(world.scores, 10) == 11);
    tide_list_remove_at(&world.scores, 500, sizeof(int32_t));
    TIDE_CHECK(tide_list_count(world.scores) == 99);
    *(int32_t *)tide_list_insert(&world.scores, -5, sizeof(int32_t), TIDE_IN_MATCH) = 42;
    TIDE_CHECK(at(world.scores, 0) == 42 && at(world.scores, 1) == 0);

    tide_heap_flush(&world.heap);
    const uint32_t used = world.heap.used;
    tide_list_clear(&world.scores, sizeof(int32_t));
    for (int32_t i = 0; i < 100; i++) add(&world.scores, i, TIDE_IN_MATCH);
    TIDE_CHECK(world.heap.used == used);

    for (int32_t i = 0; i < 20000; i++) add(&world.scores, i, TIDE_IN_MATCH);
    TIDE_CHECK(tide_list_count(world.scores) == 20100 && at(world.scores, 20099) == 19999);
}

TIDE_TEST(list_copies_are_their_own)
{
    start();
    const uint32_t mark = tide_scratch_mark();
    add(&world.scores, 1, TIDE_IN_MATCH);
    add(&world.scores, 2, TIDE_IN_MATCH);
    tide_list copy = tide_list_copy(world.scores, sizeof(int32_t));
    add(&copy, 3, TIDE_IN_SCRATCH);
    *(int32_t *)tide_list_at_mut(copy, 0, sizeof(int32_t)) = 9;
    TIDE_CHECK(tide_list_count(world.scores) == 2 && at(world.scores, 0) == 1);
    TIDE_CHECK(tide_list_count(copy) == 3 && at(copy, 0) == 9 && copy.at >> 30 == TIDE_IN_SCRATCH);

    tide_list_set(&world.other, copy, sizeof(int32_t), TIDE_IN_MATCH);
    TIDE_CHECK(world.other.at >> 30 == TIDE_IN_MATCH);
    tide_scratch_reset(mark);
    TIDE_CHECK(tide_list_count(world.other) == 3 && at(world.other, 2) == 3);

    const int32_t three[3] = {7, 8, 9};
    world.other = tide_list_from(three, 3, sizeof(int32_t));
    tide_list_own(&world.other, sizeof(int32_t), TIDE_IN_MATCH);
    tide_scratch_reset(mark);
    TIDE_CHECK(world.other.at >> 30 == TIDE_IN_MATCH && at(world.other, 1) == 8);

    tide_list_release(&world.other, TIDE_IN_MATCH);
    TIDE_CHECK(world.other.at == 0 && world.heap.pending != 0);
    tide_heap_flush(&world.heap);
}

TIDE_TEST(list_snapshots_share_until_changed)
{
    start();
    for (int32_t i = 0; i < 10; i++) add(&world.scores, i, TIDE_IN_MATCH);
    list_world snapshot = {0};
    snapshot.scores = world.scores;
    tide_heap_copy(&snapshot.heap, &world.heap);

    *(int32_t *)tide_list_at_mut(world.scores, 3, sizeof(int32_t)) = 33;
    tide_list_remove_at(&world.scores, 0, sizeof(int32_t));
    TIDE_CHECK(at(world.scores, 2) == 33 && tide_list_count(world.scores) == 9);

    tide_text_use(&snapshot.heap, NULL);
    TIDE_CHECK(at(snapshot.scores, 3) == 3 && tide_list_count(snapshot.scores) == 10);
    tide_text_use(&world.heap, NULL);
    tide_heap_free(&snapshot.heap);
}

// The flat copy C gets of a chunked list comes from memory kept between calls; two can be out at once (a mesh's vertices and indices).
TIDE_TEST(list_flat_copies_keep_their_own_room)
{
    start();
    const uint32_t size = sizeof(int32_t);
    tide_list small = {0};
    tide_list big = {0};
    tide_list between = {0};
    for (int32_t i = 0; i < 6000; i++) add(&small, 0, TIDE_IN_MATCH);
    for (int32_t i = 0; i < 40000; i++) add(&big, 0, TIDE_IN_MATCH);
    for (int32_t i = 0; i < 20000; i++) add(&between, 0, TIDE_IN_MATCH);
    bool small_copied, big_copied, between_copied;
    int32_t *a = tide_list_flatten(small, size, false, &small_copied);
    int32_t *b = tide_list_flatten(big, size, false, &big_copied);
    TIDE_REQUIRE(small_copied && big_copied);
    tide_list_unflatten(big, b, size, false, big_copied);
    tide_list_unflatten(small, a, size, false, small_copied);
    int32_t *c = tide_list_flatten(between, size, true, &between_copied);
    TIDE_REQUIRE(between_copied);
    for (int32_t i = 0; i < 20000; i++) c[i] = i;
    tide_list_unflatten(between, c, size, true, between_copied);
    bool right = true;
    for (int32_t i = 0; i < 20000; i++) right &= at(between, i) == i;
    TIDE_CHECK(right);
}

static bool chunked(const tide_list l)
{
    return l.at && (tide_block_at(l.at)->b & TIDE_LIST_CHUNKED);
}

static bool counts_up(const tide_list l, const int32_t from, const int32_t to, const int32_t first)
{
    for (int32_t i = from; i < to; i++) {
        if (at(l, i) != first + (i - from)) return false;
    }
    return true;
}

static bool zeros(const tide_list l, const int32_t from, const int32_t to)
{
    for (int32_t i = from; i < to; i++) {
        if (at(l, i) != 0) return false;
    }
    return true;
}

static void number(const tide_list l)
{
    for (int32_t i = 0; i < tide_list_count(l); i++) *(int32_t *)tide_list_at_mut(l, i, sizeof(int32_t)) = i;
}

static uint32_t pages_apart(const tide_heap *a, const tide_heap *b)
{
    uint32_t apart = a->pages > b->pages ? a->pages - b->pages : b->pages - a->pages;
    for (uint32_t i = 0; i < a->pages && i < b->pages; i++) apart += a->page[i] != b->page[i];
    return apart;
}

TIDE_TEST(list_resize_in_a_world)
{
    start();
    const uint32_t size = sizeof(int32_t);
    const int32_t per = (int32_t)(TIDE_LIST_CHUNK / size);
    tide_list_resize(&world.scores, 10, size, TIDE_IN_MATCH);
    TIDE_CHECK(tide_list_count(world.scores) == 10 && zeros(world.scores, 0, 10) && !chunked(world.scores));
    number(world.scores);
    tide_list_resize(&world.scores, 10, size, TIDE_IN_MATCH);
    TIDE_CHECK(counts_up(world.scores, 0, 10, 0));
    tide_list_resize(&world.scores, 4, size, TIDE_IN_MATCH);
    tide_list_resize(&world.scores, 12, size, TIDE_IN_MATCH);
    TIDE_CHECK(tide_list_count(world.scores) == 12 && counts_up(world.scores, 0, 4, 0) && zeros(world.scores, 4, 12));
    tide_list_resize(&world.scores, -3, size, TIDE_IN_MATCH);
    TIDE_CHECK(tide_list_count(world.scores) == 0 && at(world.scores, 0) == -999);

    tide_list_resize(&world.scores, 3 * per + 5, size, TIDE_IN_MATCH);
    TIDE_CHECK(tide_list_count(world.scores) == 3 * per + 5 && zeros(world.scores, 0, 3 * per + 5) && chunked(world.scores));
    number(world.scores);
    tide_list_resize(&world.scores, 9 * per, size, TIDE_IN_MATCH);
    TIDE_CHECK(counts_up(world.scores, 0, 3 * per + 5, 0) && zeros(world.scores, 3 * per + 5, 9 * per));
    number(world.scores);
    tide_heap_flush(&world.heap);
    const uint32_t used = world.heap.used;

    tide_list_resize(&world.scores, 4 * per, size, TIDE_IN_MATCH);
    TIDE_CHECK(tide_list_count(world.scores) == 4 * per && counts_up(world.scores, 0, 4 * per, 0));
    TIDE_CHECK((tide_block_at(world.scores.at)->b & ~TIDE_LIST_CHUNKED) == 4u && world.heap.pending != 0);
    tide_list_resize(&world.scores, per + 7, size, TIDE_IN_MATCH);
    TIDE_CHECK((tide_block_at(world.scores.at)->b & ~TIDE_LIST_CHUNKED) == 2u);
    tide_heap_flush(&world.heap);
    tide_list_resize(&world.scores, 9 * per, size, TIDE_IN_MATCH);
    TIDE_CHECK(world.heap.used == used);
    TIDE_CHECK(counts_up(world.scores, 0, per + 7, 0) && zeros(world.scores, per + 7, 9 * per));

    number(world.scores);
    tide_list_remove_at(&world.scores, 9 * per - 1, size);
    tide_list_resize(&world.scores, 9 * per, size, TIDE_IN_MATCH);
    TIDE_CHECK(at(world.scores, 9 * per - 2) == 9 * per - 2 && at(world.scores, 9 * per - 1) == 0);
    tide_list_clear(&world.scores, size);
    tide_list_resize(&world.scores, 9 * per, size, TIDE_IN_MATCH);
    TIDE_CHECK(zeros(world.scores, 0, 9 * per) && world.heap.used == used);

    number(world.scores);
    tide_list_resize(&world.scores, 3, size, TIDE_IN_MATCH);
    TIDE_CHECK(tide_list_count(world.scores) == 3 && counts_up(world.scores, 0, 3, 0) && at(world.scores, 3) == -999);
    tide_list_resize(&world.scores, 0, size, TIDE_IN_MATCH);
    TIDE_CHECK(tide_list_count(world.scores) == 0 && world.scores.at == 0);
    tide_heap_flush(&world.heap);
    tide_list_resize(&world.scores, 6, size, TIDE_IN_MATCH);
    TIDE_CHECK(tide_list_count(world.scores) == 6 && zeros(world.scores, 0, 6) && !chunked(world.scores));
    tide_list_resize(&world.scores, per, size, TIDE_IN_MATCH);
    TIDE_CHECK(!chunked(world.scores) && zeros(world.scores, 0, per));
    tide_list_resize(&world.scores, per + 1, size, TIDE_IN_MATCH);
    TIDE_CHECK(chunked(world.scores) && zeros(world.scores, 0, per + 1));
}

TIDE_TEST(list_resize_shares_with_snapshots)
{
    start();
    const uint32_t size = sizeof(int32_t);
    const int32_t per = (int32_t)(TIDE_LIST_CHUNK / size);
    tide_list_resize(&world.scores, 8 * per, size, TIDE_IN_MATCH);
    number(world.scores);
    list_world snapshot = {0};
    snapshot.scores = world.scores;
    tide_heap_copy(&snapshot.heap, &world.heap);

    tide_list_resize(&world.scores, 8 * per, size, TIDE_IN_MATCH);
    TIDE_CHECK(pages_apart(&world.heap, &snapshot.heap) == 0);
    tide_list_resize(&world.scores, 8 * per - 10, size, TIDE_IN_MATCH);
    TIDE_CHECK(pages_apart(&world.heap, &snapshot.heap) == 2);
    tide_list_resize(&world.scores, 8 * per, size, TIDE_IN_MATCH);
    TIDE_CHECK(counts_up(world.scores, 0, 8 * per - 10, 0) && zeros(world.scores, 8 * per - 10, 8 * per));
    TIDE_CHECK(pages_apart(&world.heap, &snapshot.heap) == 2);

    tide_text_use(&snapshot.heap, NULL);
    TIDE_CHECK(tide_list_count(snapshot.scores) == 8 * per && counts_up(snapshot.scores, 0, 8 * per, 0));
    tide_text_use(&world.heap, NULL);
    tide_heap_free(&snapshot.heap);
}

TIDE_TEST(list_resize_in_the_scratch_area)
{
    start();
    const uint32_t size = sizeof(int32_t);
    const uint32_t mark = tide_scratch_mark();
    memset(tide_scratch_memory(1u << 16), 0xEE, 1u << 16); // What a list made next finds there
    tide_scratch_reset(mark);

    tide_list local = {0};
    tide_list_resize(&local, 100, size, TIDE_IN_SCRATCH);
    TIDE_CHECK(tide_list_count(local) == 100 && zeros(local, 0, 100) && local.at >> 30 == TIDE_IN_SCRATCH);
    number(local);
    tide_list_resize(&local, 40, size, TIDE_IN_SCRATCH);
    tide_list_resize(&local, 3000, size, TIDE_IN_SCRATCH);
    TIDE_CHECK(tide_list_count(local) == 3000 && counts_up(local, 0, 40, 0) && zeros(local, 40, 3000));
    tide_list_resize(&local, 0, size, TIDE_IN_SCRATCH);
    add(&local, 5, TIDE_IN_SCRATCH);
    TIDE_CHECK(tide_list_count(local) == 1 && at(local, 0) == 5);

    tide_list_resize(&world.scores, 20000, size, TIDE_IN_MATCH);
    number(world.scores);
    tide_list borrowed = world.scores;
    tide_list_resize(&borrowed, 5000, size, TIDE_IN_SCRATCH);
    TIDE_CHECK(borrowed.at >> 30 == TIDE_IN_SCRATCH && tide_list_count(borrowed) == 5000 && counts_up(borrowed, 0, 5000, 0));
    borrowed = world.scores;
    tide_list_resize(&borrowed, 25000, size, TIDE_IN_SCRATCH);
    TIDE_CHECK(borrowed.at >> 30 == TIDE_IN_SCRATCH && counts_up(borrowed, 0, 20000, 0) && zeros(borrowed, 20000, 25000));
    TIDE_CHECK(tide_list_count(world.scores) == 20000 && counts_up(world.scores, 0, 20000, 0));
    tide_scratch_reset(mark);
}

static void make(tide_list *l, const uint32_t where, const uint32_t count, const int32_t first, const uint32_t said)
{
    tide_list_out out;
    tide_list_out_begin(&out, sizeof(int32_t));
    int32_t *room = tide_list_out_room(&out, count);
    for (uint32_t i = 0; i < count; i++) room[i] = first + (int32_t)i;
    tide_list_out_count(&out, said);
    tide_list_out_end(&out, l, where);
}

TIDE_TEST(list_out_makes_the_list)
{
    start();
    const int32_t per = (int32_t)(TIDE_LIST_CHUNK / sizeof(int32_t));
    make(&world.scores, TIDE_IN_MATCH, 10, 100, 10);
    TIDE_CHECK(tide_list_count(world.scores) == 10 && counts_up(world.scores, 0, 10, 100) && !chunked(world.scores));
    TIDE_CHECK(world.scores.at >> 30 == TIDE_IN_MATCH);
    make(&world.scores, TIDE_IN_MATCH, (uint32_t)(5 * per + 3), 7, (uint32_t)(5 * per + 3));
    TIDE_CHECK(tide_list_count(world.scores) == 5 * per + 3 && counts_up(world.scores, 0, 5 * per + 3, 7) && chunked(world.scores));
    tide_heap_flush(&world.heap);
    const uint32_t used = world.heap.used;
    make(&world.scores, TIDE_IN_MATCH, (uint32_t)(2 * per), 50, (uint32_t)(2 * per));
    TIDE_CHECK(tide_list_count(world.scores) == 2 * per && counts_up(world.scores, 0, 2 * per, 50));
    TIDE_CHECK((tide_block_at(world.scores.at)->b & ~TIDE_LIST_CHUNKED) == 2u);
    make(&world.scores, TIDE_IN_MATCH, 3, 9, 3);
    TIDE_CHECK(tide_list_count(world.scores) == 3 && counts_up(world.scores, 0, 3, 9) && at(world.scores, 3) == -999);
    tide_heap_flush(&world.heap);
    make(&world.scores, TIDE_IN_MATCH, (uint32_t)(5 * per + 3), 1, (uint32_t)(5 * per + 3));
    TIDE_CHECK(counts_up(world.scores, 0, 5 * per + 3, 1) && world.heap.used == used);

    make(&world.scores, TIDE_IN_MATCH, 100, 0, 60);
    TIDE_CHECK(tide_list_count(world.scores) == 60 && counts_up(world.scores, 0, 60, 0));
    make(&world.scores, TIDE_IN_MATCH, 100, 0, 5000);
    TIDE_CHECK(tide_list_count(world.scores) == 100 && counts_up(world.scores, 0, 100, 0));
    make(&world.scores, TIDE_IN_MATCH, 100, 0, 0);
    TIDE_CHECK(tide_list_count(world.scores) == 0);
    make(&world.scores, TIDE_IN_MATCH, 0, 0, 0);
    TIDE_CHECK(tide_list_count(world.scores) == 0);

    tide_list_out out;
    tide_list_out_begin(&out, sizeof(int32_t));
    int32_t *room = tide_list_out_room(&out, 8);
    for (int32_t i = 0; i < 8; i++) room[i] = -1;
    tide_list_out_count(&out, 8);
    room = tide_list_out_room(&out, 40000);
    TIDE_REQUIRE(room != NULL);
    for (int32_t i = 0; i < 40000; i++) room[i] = i;
    tide_list_out_count(&out, 40000);
    room = tide_list_out_room(&out, 20);
    for (int32_t i = 0; i < 20; i++) room[i] = 300 + i;
    tide_list_out_count(&out, 12);
    tide_list_out_end(&out, &world.scores, TIDE_IN_MATCH);
    TIDE_CHECK(tide_list_count(world.scores) == 12 && counts_up(world.scores, 0, 12, 300));
    tide_list_out_begin(&out, sizeof(int32_t));
    room = tide_list_out_room(&out, 20);
    tide_list_out_count(&out, 20);
    room = tide_list_out_room(&out, 30);
    for (int32_t i = 0; i < 30; i++) room[i] = i;
    tide_list_out_end(&out, &world.scores, TIDE_IN_MATCH);
    TIDE_CHECK(tide_list_count(world.scores) == 0);
    tide_list_out_begin(&out, 16);
    TIDE_CHECK(tide_list_out_room(&out, 0) == NULL);
    TIDE_CHECK(tide_list_out_room(&out, 0x10000000u) == NULL);
    tide_list_out_end(&out, &world.other, TIDE_IN_MATCH);
    TIDE_CHECK(world.other.at == 0);

    const uint32_t mark = tide_scratch_mark();
    tide_list local = {0};
    make(&local, TIDE_IN_SCRATCH, 50, 5, 50);
    TIDE_CHECK(local.at >> 30 == TIDE_IN_SCRATCH && tide_list_count(local) == 50 && counts_up(local, 0, 50, 5));
    make(&local, TIDE_IN_SCRATCH, 400000, 2, 400000);
    TIDE_CHECK(tide_list_count(local) == 400000 && counts_up(local, 0, 400000, 2));
    make(&local, TIDE_IN_SCRATCH, 4, 1, 4);
    TIDE_CHECK(tide_list_count(local) == 4 && counts_up(local, 0, 4, 1));
    make(&local, TIDE_IN_SCRATCH, 4, 1, 0);
    TIDE_CHECK(tide_list_count(local) == 0);
    make(&world.scores, TIDE_IN_MATCH, 30000, 0, 30000);
    tide_list named = world.scores;
    make(&named, TIDE_IN_SCRATCH, 10, 77, 10);
    TIDE_CHECK(named.at >> 30 == TIDE_IN_SCRATCH && counts_up(named, 0, 10, 77));
    TIDE_CHECK(tide_list_count(world.scores) == 30000 && counts_up(world.scores, 0, 30000, 0));
    tide_scratch_reset(mark);
}

TIDE_TEST(list_out_writes_what_differs)
{
    start();
    const int32_t per = (int32_t)(TIDE_LIST_CHUNK / sizeof(int32_t));
    const uint32_t count = (uint32_t)(8 * per);
    make(&world.scores, TIDE_IN_MATCH, count, 0, count);
    list_world snapshot = {0};
    snapshot.scores = world.scores;
    tide_heap_copy(&snapshot.heap, &world.heap);
    const uint64_t before = tide_heap_hash(0, &world.heap);

    make(&world.scores, TIDE_IN_MATCH, count, 0, count);
    TIDE_CHECK(pages_apart(&world.heap, &snapshot.heap) == 0 && tide_heap_hash(0, &world.heap) == before);

    tide_list_out out;
    tide_list_out_begin(&out, sizeof(int32_t));
    int32_t *room = tide_list_out_room(&out, count);
    for (uint32_t i = 0; i < count; i++) room[i] = (int32_t)i;
    room[5 * per + 9] = -5;
    tide_list_out_count(&out, count);
    tide_list_out_end(&out, &world.scores, TIDE_IN_MATCH);
    TIDE_CHECK(pages_apart(&world.heap, &snapshot.heap) == 1 && at(world.scores, 5 * per + 9) == -5);
    TIDE_CHECK(counts_up(world.scores, 0, 5 * per + 9, 0) && tide_heap_hash(0, &world.heap) != before);
    TIDE_CHECK(counts_up(world.scores, 5 * per + 10, (int32_t)count, 5 * per + 10));

    list_world second = {0};
    second.scores = world.scores;
    tide_heap_copy(&second.heap, &world.heap);
    const uint32_t fewer = count - (uint32_t)per / 2u;
    tide_list_out_begin(&out, sizeof(int32_t));
    room = tide_list_out_room(&out, fewer);
    for (uint32_t i = 0; i < fewer; i++) room[i] = (int32_t)i + (i >= 7u * (uint32_t)per ? 1 : 0);
    tide_list_out_count(&out, fewer);
    tide_list_out_end(&out, &world.scores, TIDE_IN_MATCH);
    TIDE_CHECK(tide_list_count(world.scores) == (int32_t)fewer && counts_up(world.scores, 7 * per, (int32_t)fewer, 7 * per + 1));
    tide_list_resize(&world.scores, (int32_t)count, sizeof(int32_t), TIDE_IN_MATCH);
    TIDE_CHECK(zeros(world.scores, (int32_t)fewer, (int32_t)count));
    tide_text_use(&second.heap, NULL);
    TIDE_CHECK(at(second.scores, 5 * per + 9) == -5 && counts_up(second.scores, 7 * per, (int32_t)count, 7 * per));
    tide_text_use(&world.heap, NULL);
    tide_heap_free(&second.heap);

    make(&world.scores, TIDE_IN_MATCH, 5, 1000, 5);
    tide_heap_flush(&world.heap);
    tide_text_use(&snapshot.heap, NULL);
    TIDE_CHECK(tide_list_count(snapshot.scores) == (int32_t)count && counts_up(snapshot.scores, 0, (int32_t)count, 0));
    TIDE_CHECK(tide_heap_hash(0, &snapshot.heap) == before);
    tide_text_use(&world.heap, NULL);
    tide_heap_free(&snapshot.heap);
}

static const tide_page *page_of(const tide_heap *heap, const tide_list l, const int32_t i)
{
    uint32_t place;
    const uint32_t chunk = tide_list_chunk(tide_heap_block(heap, l.at & 0x3FFFFFFFu), (uint32_t)i, sizeof(int32_t), &place);
    return heap->page[chunk >> TIDE_HEAP_PAGE_SHIFT];
}

// What C's parts point at has to be good after C returned: not a function's own variables.
static int32_t given[60000];

static const int32_t *numbers(const uint32_t at, const uint32_t count, const int32_t value)
{
    for (uint32_t i = 0; i < count; i++) given[at + i] = value + (int32_t)i;
    return given + at;
}

static void patch(tide_list *l, const uint32_t where, const uint32_t count, const uint32_t first, const uint32_t said, const int32_t value)
{
    tide_list_out out;
    tide_list_out_begin(&out, sizeof(int32_t));
    tide_list_out_keep(&out, count);
    tide_list_out_part(&out, first, numbers(0, said, value), said);
    tide_list_out_end(&out, l, where);
}

TIDE_TEST(list_out_keeps_what_it_holds)
{
    start();
    const int32_t per = (int32_t)(TIDE_LIST_CHUNK / sizeof(int32_t));
    const uint32_t count = (uint32_t)(8 * per);
    make(&world.scores, TIDE_IN_MATCH, count, 0, count);
    list_world snapshot = {0};
    snapshot.scores = world.scores;
    tide_heap_copy(&snapshot.heap, &world.heap);
    const uint64_t before = tide_heap_hash(0, &world.heap);

    tide_list_out out;
    tide_list_out_begin(&out, sizeof(int32_t));
    tide_list_out_keep(&out, count);
    tide_list_out_end(&out, &world.scores, TIDE_IN_MATCH);
    TIDE_CHECK(tide_list_count(world.scores) == (int32_t)count && pages_apart(&world.heap, &snapshot.heap) == 0);
    patch(&world.scores, TIDE_IN_MATCH, count, (uint32_t)(5 * per + 9), 40, 5 * per + 9);
    patch(&world.scores, TIDE_IN_MATCH, count, (uint32_t)(2 * per - 20), (uint32_t)(2 * per), 2 * per - 20);
    TIDE_CHECK(pages_apart(&world.heap, &snapshot.heap) == 0 && tide_heap_hash(0, &world.heap) == before);

    patch(&world.scores, TIDE_IN_MATCH, count, (uint32_t)(5 * per + 9), 3, -50);
    TIDE_CHECK(pages_apart(&world.heap, &snapshot.heap) == 1 && tide_heap_hash(0, &world.heap) != before);
    TIDE_CHECK(counts_up(world.scores, 0, 5 * per + 9, 0) && counts_up(world.scores, 5 * per + 9, 5 * per + 12, -50));
    TIDE_CHECK(counts_up(world.scores, 5 * per + 12, (int32_t)count, 5 * per + 12));

    tide_list_out_begin(&out, sizeof(int32_t));
    tide_list_out_keep(&out, count);
    numbers(0, 20, 2 * per - 10);
    given[15] = -7;
    tide_list_out_part(&out, (uint32_t)(2 * per - 10), given, 20);
    tide_list_out_end(&out, &world.scores, TIDE_IN_MATCH);
    TIDE_CHECK(pages_apart(&world.heap, &snapshot.heap) == 2 && at(world.scores, 2 * per + 5) == -7);
    TIDE_CHECK(page_of(&world.heap, world.scores, 2 * per - 5) == page_of(&snapshot.heap, world.scores, 2 * per - 5));
    TIDE_CHECK(page_of(&world.heap, world.scores, 2 * per + 5) != page_of(&snapshot.heap, world.scores, 2 * per + 5));

    patch(&world.scores, TIDE_IN_MATCH, count, (uint32_t)(6 * per), (uint32_t)per, 1000);
    patch(&world.scores, TIDE_IN_MATCH, count, (uint32_t)(3 * per), 12, 2000);
    TIDE_CHECK(pages_apart(&world.heap, &snapshot.heap) == 4);
    TIDE_CHECK(counts_up(world.scores, 6 * per, 7 * per, 1000) && counts_up(world.scores, 7 * per, 8 * per, 7 * per));
    TIDE_CHECK(counts_up(world.scores, 3 * per, 3 * per + 12, 2000) && counts_up(world.scores, 3 * per + 12, 5 * per + 9, 3 * per + 12));
    TIDE_CHECK(counts_up(world.scores, 0, 2 * per + 5, 0));

    tide_text_use(&snapshot.heap, NULL);
    TIDE_CHECK(tide_list_count(snapshot.scores) == (int32_t)count && counts_up(snapshot.scores, 0, (int32_t)count, 0));
    TIDE_CHECK(tide_heap_hash(0, &snapshot.heap) == before);
    tide_text_use(&world.heap, NULL);
    tide_heap_free(&snapshot.heap);

    make(&world.scores, TIDE_IN_MATCH, count, 0, count);
    patch(&world.scores, TIDE_IN_MATCH, count + (uint32_t)per, count + 4u, 6, 77);
    TIDE_CHECK(tide_list_count(world.scores) == 9 * per && counts_up(world.scores, 0, (int32_t)count, 0));
    TIDE_CHECK(zeros(world.scores, (int32_t)count, (int32_t)count + 4) && counts_up(world.scores, (int32_t)count + 4, (int32_t)count + 10, 77));
    TIDE_CHECK(zeros(world.scores, (int32_t)count + 10, 9 * per));
    patch(&world.scores, TIDE_IN_MATCH, (uint32_t)(3 * per + 5), (uint32_t)(3 * per), 100, -200);
    TIDE_CHECK(tide_list_count(world.scores) == 3 * per + 5 && counts_up(world.scores, 0, 3 * per, 0));
    TIDE_CHECK(counts_up(world.scores, 3 * per, 3 * per + 5, -200) && at(world.scores, 3 * per + 5) == -999);
    TIDE_CHECK((tide_block_at(world.scores.at)->b & ~TIDE_LIST_CHUNKED) == 4u);
    patch(&world.scores, TIDE_IN_MATCH, (uint32_t)(3 * per + 5), (uint32_t)(4 * per), 10, 1);
    TIDE_CHECK(tide_list_count(world.scores) == 3 * per + 5 && counts_up(world.scores, 3 * per, 3 * per + 5, -200));
    tide_list_resize(&world.scores, 9 * per, sizeof(int32_t), TIDE_IN_MATCH);
    TIDE_CHECK(zeros(world.scores, 3 * per + 5, 9 * per));

    make(&world.scores, TIDE_IN_MATCH, count, 0, count);
    tide_list_out_begin(&out, sizeof(int32_t));
    tide_list_out_keep(&out, count);
    for (uint32_t k = 0; k < 500; k++) tide_list_out_part(&out, 37u * k, numbers(k, 1, -(int32_t)k), 1);
    tide_list_out_part(&out, 37u * 20u, numbers(600, 2, 9000), 2);
    tide_list_out_end(&out, &world.scores, TIDE_IN_MATCH);
    bool right = true;
    for (int32_t i = 0; i < (int32_t)count; i++) {
        const int32_t expected = i == 37 * 20 ? 9000 : i == 37 * 20 + 1 ? 9001 : i % 37 == 0 && i / 37 < 500 ? -(i / 37) : i;
        right &= at(world.scores, i) == expected;
    }
    TIDE_CHECK(right);

    make(&world.scores, TIDE_IN_MATCH, 100, 0, 100);
    tide_list_out_begin(&out, sizeof(int32_t));
    tide_list_out_part(&out, 0, numbers(0, 4, 50), 4);
    int32_t *room = tide_list_out_room(&out, 10);
    for (int32_t i = 0; i < 10; i++) room[i] = -i;
    tide_list_out_count(&out, 10);
    tide_list_out_keep(&out, 100);
    tide_list_out_part(&out, 2, numbers(10, 3, 500), 3);
    tide_list_out_end(&out, &world.scores, TIDE_IN_MATCH);
    TIDE_CHECK(tide_list_count(world.scores) == 100 && counts_up(world.scores, 0, 2, 0) && counts_up(world.scores, 2, 5, 500));
    TIDE_CHECK(counts_up(world.scores, 5, 100, 5));
    tide_list_out_begin(&out, sizeof(int32_t));
    tide_list_out_keep(&out, 100);
    tide_list_out_part(&out, 2, numbers(0, 3, 900), 3);
    room = tide_list_out_room(&out, 6);
    for (int32_t i = 0; i < 6; i++) room[i] = 40 + i;
    tide_list_out_count(&out, 6);
    tide_list_out_end(&out, &world.scores, TIDE_IN_MATCH);
    TIDE_CHECK(tide_list_count(world.scores) == 6 && counts_up(world.scores, 0, 6, 40));

    tide_list_out_begin(&out, sizeof(int32_t));
    tide_list_out_keep(&out, 6);
    tide_list_out_count(&out, 9);
    tide_list_out_part(&out, 7, numbers(0, 2, 70), 2);
    tide_list_out_end(&out, &world.scores, TIDE_IN_MATCH);
    TIDE_CHECK(tide_list_count(world.scores) == 9 && counts_up(world.scores, 0, 6, 40) && at(world.scores, 6) == 0);
    TIDE_CHECK(counts_up(world.scores, 7, 9, 70));
    patch(&world.scores, TIDE_IN_MATCH, 0x7FFFFFFFu, 0, 1, 3);
    TIDE_CHECK(tide_list_count(world.scores) == 9 && at(world.scores, 0) == 3 && at(world.scores, 1) == 41);
    patch(&world.scores, TIDE_IN_MATCH, 0, 0, 4, 1);
    TIDE_CHECK(tide_list_count(world.scores) == 0);
    patch(&world.other, TIDE_IN_MATCH, 5, 1, 2, 8);
    TIDE_CHECK(tide_list_count(world.other) == 5 && at(world.other, 0) == 0 && counts_up(world.other, 1, 3, 8) && zeros(world.other, 3, 5));

    const uint32_t mark = tide_scratch_mark();
    memset(tide_scratch_memory(1u << 16), 0xEE, 1u << 16);
    tide_scratch_reset(mark);
    tide_list local = {0};
    make(&local, TIDE_IN_SCRATCH, 50, 5, 50);
    patch(&local, TIDE_IN_SCRATCH, 3000, 60, 4, -1);
    TIDE_CHECK(local.at >> 30 == TIDE_IN_SCRATCH && tide_list_count(local) == 3000 && counts_up(local, 0, 50, 5));
    TIDE_CHECK(zeros(local, 50, 60) && counts_up(local, 60, 64, -1) && zeros(local, 64, 3000));
    patch(&local, TIDE_IN_SCRATCH, 20, 18, 9, 33);
    TIDE_CHECK(tide_list_count(local) == 20 && counts_up(local, 0, 18, 5) && counts_up(local, 18, 20, 33));
    make(&world.scores, TIDE_IN_MATCH, 30000, 0, 30000);
    for (int32_t round = 0; round < 3; round++) {
        const uint32_t to = round == 0 ? 30000u : round == 1 ? 31000u : 12000u;
        tide_list named = world.scores;
        patch(&named, TIDE_IN_SCRATCH, to, 10, 5, -9);
        TIDE_CHECK(named.at >> 30 == TIDE_IN_SCRATCH && tide_list_count(named) == (int32_t)to);
        TIDE_CHECK(counts_up(named, 0, 10, 0) && counts_up(named, 10, 15, -9) && counts_up(named, 15, to < 30000u ? (int32_t)to : 30000, 15));
        TIDE_CHECK(zeros(named, 30000, (int32_t)to));
        TIDE_CHECK(tide_list_count(world.scores) == 30000 && counts_up(world.scores, 0, 30000, 0));
    }
    tide_scratch_reset(mark);
}
