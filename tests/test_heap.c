#include <stdlib.h>
#include <string.h>

#include "tide/grid.h"
#include "tide/list.h"
#include "tide/text.h"
#include "tide_test.h"

typedef struct fake_world {
    tide_text name;
    tide_text title;
    tide_heaps heap;
} fake_world;

static fake_world world;

static bool reads(const tide_text t, const char *expected)
{
    const tide_str s = tide_text_view(t);
    return s.bytes == (int32_t)strlen(expected) && memcmp(s.ptr, expected, strlen(expected)) == 0;
}

static tide_str text(const char *s)
{
    return tide_str_from_cstr(s);
}

static void start(void)
{
    tide_heaps_free(&world.heap);
    memset(&world, 0, sizeof world);
    tide_text_use(&world.heap, NULL);
}

TIDE_TEST(heap_blocks_are_reused_in_order)
{
    start();
    const uint64_t a = tide_heap_alloc(tide_heaps_of(&world.heap, 0u), 10);
    const uint64_t b = tide_heap_alloc(tide_heaps_of(&world.heap, 0u), 10);
    TIDE_CHECK(a != 0 && b != 0 && a != b);
    tide_heap_release(tide_heaps_of(&world.heap, 0u), a);
    TIDE_CHECK(tide_heap_alloc(tide_heaps_of(&world.heap, 0u), 10) != a); // Not until the flush
    tide_heaps_flush(&world.heap);
    const uint64_t c = tide_heap_alloc(tide_heaps_of(&world.heap, 0u), 10);
    TIDE_CHECK(c == a);
}

TIDE_TEST(heap_text_fields_own_their_text)
{
    start();
    const uint64_t mark = tide_scratch_mark();
    tide_text_set(&world.name, text("cat"), TIDE_IN_MATCH);
    TIDE_CHECK(reads(world.name, "cat"));
    TIDE_CHECK(strcmp(tide_text_read(&world.heap, world.name).ptr, "cat") == 0);

    // A copy made before a change keeps the old text until the code running is done.
    const tide_text copy = world.name;
    tide_text_set(&world.name, text("dog"), TIDE_IN_MATCH);
    TIDE_CHECK(reads(world.name, "dog"));
    TIDE_CHECK(reads(copy, "cat"));

    // Outside the world, text borrows: a copy in the scratch area.
    tide_text local = {0};
    tide_text_set(&local, text("bird"), TIDE_IN_SCRATCH);
    TIDE_CHECK(reads(local, "bird"));
    TIDE_CHECK(tide_heaps_of(&world.heap, 0u)->pending != 0);
    tide_heaps_flush(&world.heap);
    TIDE_CHECK(tide_heaps_of(&world.heap, 0u)->pending == 0);

    // A value copied into the world takes its own copy of borrowed text.
    world.title = tide_text_temp(text("fish"));
    tide_text_own(&world.title, TIDE_IN_MATCH);
    tide_scratch_reset(mark);
    TIDE_CHECK(reads(world.title, "fish"));

    tide_text_release(&world.title, TIDE_IN_MATCH);
    TIDE_CHECK(world.title.at == 0);
    tide_text_set(&world.name, TIDE_STR_EMPTY, TIDE_IN_MATCH);
    TIDE_CHECK(reads(world.name, ""));
    tide_heaps_flush(&world.heap);
}

TIDE_TEST(heap_grows_past_its_pages)
{
    start();
    tide_text_set(&world.name, text("kept"), TIDE_IN_MATCH);
    const uint32_t size = 3u << TIDE_HEAP_PAGE_SHIFT;
    char *big = malloc(size);
    memset(big, 'x', size);
    const tide_str huge = {big, (int32_t)size, (int32_t)size};
    tide_text_set(&world.title, huge, TIDE_IN_MATCH);
    TIDE_CHECK(tide_text_view(world.title).bytes == (int32_t)size);
    TIDE_CHECK(reads(world.name, "kept"));
    TIDE_CHECK(tide_heaps_of(&world.heap, 0u)->pages >= 5);
    for (int i = 0; i < 2000; i++) tide_heap_alloc(tide_heaps_of(&world.heap, 0u), 100);
    TIDE_CHECK(tide_text_view(world.title).ptr[size - 1u] == 'x');
    free(big);
}

TIDE_TEST(heap_starts_with_a_block_of_a_page)
{
    for (uint32_t places = 1; places <= 2; places++) {
        start();
        const uint32_t bytes = (places << TIDE_HEAP_PAGE_SHIFT) - (uint32_t)sizeof(tide_block);
        const uint64_t block = tide_heap_alloc(tide_heaps_of(&world.heap, 0u), bytes);
        TIDE_REQUIRE(block != 0);
        TIDE_CHECK(tide_heaps_of(&world.heap, 0u)->pages == places + 1u);
        for (uint32_t i = 0; i < tide_heaps_of(&world.heap, 0u)->pages; i++) TIDE_REQUIRE(tide_heaps_of(&world.heap, 0u)->page[i] != NULL);
        uint8_t *data = (uint8_t *)(tide_heap_write(tide_heaps_of(&world.heap, 0u), block) + 1);
        data[0] = 1;
        data[bytes - 1u] = 2;
        const uint64_t small = tide_heap_alloc(tide_heaps_of(&world.heap, 0u), 10);
        TIDE_CHECK(small >= block + (places << TIDE_HEAP_PAGE_SHIFT));
        TIDE_REQUIRE(tide_heaps_of(&world.heap, 0u)->page[tide_heaps_of(&world.heap, 0u)->pages - 1u] != NULL);

        tide_heaps copy = {0};
        tide_heaps_copy(&copy, &world.heap);
        TIDE_CHECK(tide_heaps_hash(1, &copy) == tide_heaps_hash(1, &world.heap));
        tide_heaps_free(&copy);

        const uint32_t size = tide_heaps_packed_size(&world.heap);
        uint8_t *packed = malloc(size);
        tide_writer w = {packed, size, 0, false, false};
        tide_heaps_pack(&world.heap, &w);
        TIDE_CHECK(!w.overflow && w.size == size);
        tide_heaps back = {0};
        tide_reader r = {packed, size, 0, false};
        TIDE_REQUIRE(tide_heaps_unpack(&back, &r));
        TIDE_CHECK(tide_heaps_hash(1, &back) == tide_heaps_hash(1, &world.heap));
        const uint8_t *read = (const uint8_t *)(tide_heap_block(tide_heaps_of(&back, 0u), block) + 1);
        TIDE_CHECK(read[0] == 1 && read[bytes - 1u] == 2);
        free(packed);
        tide_heaps_free(&back);

        tide_delta_writer d;
        tide_delta_begin(&d, false, NULL, 0, 42);
        tide_heaps_pack_delta(&world.heap, NULL, &d);
        uint32_t delta_size = 0;
        uint8_t *delta = tide_delta_end(&d, &delta_size);
        tide_delta_reader dr;
        uint64_t hash = 0;
        TIDE_REQUIRE(tide_delta_open(&dr, delta, delta_size, false, &hash));
        TIDE_REQUIRE(tide_heaps_unpack_delta(&back, NULL, &dr));
        TIDE_CHECK(tide_heaps_hash(1, &back) == tide_heaps_hash(1, &world.heap));
        free(delta);
        tide_heaps_free(&back);
    }
}

TIDE_TEST(heap_snapshots_share_until_changed)
{
    start();
    tide_text_set(&world.name, text("cat"), TIDE_IN_MATCH);
    fake_world snapshot = {0};
    snapshot.name = world.name;
    tide_heaps_copy(&snapshot.heap, &world.heap);
    TIDE_CHECK(tide_heaps_of(&snapshot.heap, 0u)->page[0] == tide_heaps_of(&world.heap, 0u)->page[0]);
    TIDE_CHECK(tide_heaps_hash(1, &snapshot.heap) == tide_heaps_hash(1, &world.heap));

    tide_text_set(&world.name, text("dog"), TIDE_IN_MATCH);
    tide_heaps_flush(&world.heap);
    TIDE_CHECK(tide_heaps_of(&snapshot.heap, 0u)->page[0] != tide_heaps_of(&world.heap, 0u)->page[0]);
    TIDE_CHECK(strcmp(tide_text_read(&snapshot.heap, snapshot.name).ptr, "cat") == 0);
    TIDE_CHECK(strcmp(tide_text_read(&world.heap, world.name).ptr, "dog") == 0);
    TIDE_CHECK(tide_heaps_hash(1, &snapshot.heap) != tide_heaps_hash(1, &world.heap));

    const uint32_t size = tide_heaps_packed_size(&world.heap);
    uint8_t *bytes = malloc(size);
    tide_writer w = {bytes, size, 0, false, false};
    tide_heaps_pack(&world.heap, &w);
    TIDE_CHECK(!w.overflow && w.size == size);
    tide_heaps back = {0};
    tide_reader r = {bytes, size, 0, false};
    TIDE_REQUIRE(tide_heaps_unpack(&back, &r));
    TIDE_CHECK(tide_heaps_hash(1, &back) == tide_heaps_hash(1, &world.heap));
    TIDE_CHECK(strcmp(tide_text_read(&back, world.name).ptr, "dog") == 0);
    free(bytes);
    tide_heaps_free(&back);
    tide_heaps_free(&snapshot.heap);
}

static bool all(const uint8_t *bytes, const uint32_t count, const uint8_t value)
{
    for (uint32_t i = 0; i < count; i++) {
        if (bytes[i] != value) return false;
    }
    return true;
}

TIDE_TEST(pages_let_go_are_handed_out_again)
{
    tide_pages_free();
    // A new page of a size kept spare is one let go of, zeroed
    tide_page *spare = tide_page_new(4096);
    memset(tide_page_data(spare), 0xCD, 4096);
    tide_page_release(spare, 1);
    tide_page *fresh = tide_page_new(4096);
    TIDE_CHECK(fresh == spare && fresh->refs == 1 && fresh->size == 4096 && fresh->hashed == UINT32_MAX);
    TIDE_CHECK(all(tide_page_data(fresh), 4096, 0));

    tide_page *first = fresh;
    tide_page *from = tide_page_new(4096);
    memset(tide_page_data(first), 0xAB, 4096);
    tide_page_release(first, 1);
    memset(tide_page_data(from), 0x11, 4096);
    tide_page *copy = tide_page_copy(from, 1, 100);
    TIDE_CHECK(copy == first);
    TIDE_CHECK(copy->refs == 1 && copy->size == 4096 && copy->hashed == UINT32_MAX);
    const uint8_t *bytes = tide_page_data(copy);
    TIDE_CHECK(all(bytes, 100, 0x11) && all(bytes + 100, 4096 - 100, 0));

    tide_page_release(copy, 1);
    from->refs = 2;
    tide_page *around = tide_page_own_copy_around(from, 1, 3000, 16, 1000);
    TIDE_CHECK(around == first && from->refs == 1);
    bytes = tide_page_data(around);
    TIDE_CHECK(all(bytes, 16, 0x11) && all(bytes + 100, 916, 0) && all(bytes + 1016, 3000 - 1016, 0x11));
    TIDE_CHECK(all(bytes + 3000, 4096 - 3000, 0));
    tide_page_release(around, 1);
    from->refs = 2;
    around = tide_page_own_copy_around(from, 1, 3000, 2900, 5000);
    bytes = tide_page_data(around);
    TIDE_CHECK(around == first && all(bytes, 2900, 0x11));

    tide_page *odd = tide_page_take(3000);
    odd->size = 3000;
    TIDE_CHECK(odd != first);
    tide_page_give(odd);
    tide_page_release(around, 1);
    tide_page_release(from, 1);
    tide_pages_free();
}

TIDE_TEST(heap_blocks_written_over_keep_the_rest)
{
    start();
    const uint64_t a = tide_heap_alloc(tide_heaps_of(&world.heap, 0u), 100);
    const uint64_t b = tide_heap_alloc(tide_heaps_of(&world.heap, 0u), 100);
    tide_block *block = tide_heap_write(tide_heaps_of(&world.heap, 0u), a);
    block->a = 7;
    memset(block + 1, 0x11, 100);
    block = tide_heap_write(tide_heaps_of(&world.heap, 0u), b);
    block->a = 9;
    memset(block + 1, 0x22, 100);
    TIDE_REQUIRE(a >> TIDE_HEAP_PAGE_SHIFT == b >> TIDE_HEAP_PAGE_SHIFT);
    tide_heaps snapshot = {0};
    tide_heaps_copy(&snapshot, &world.heap);
    const tide_page *shared = tide_heaps_of(&world.heap, 0u)->page[a >> TIDE_HEAP_PAGE_SHIFT];
    const uint32_t moves = tide_heaps_of(&world.heap, 0u)->moves;

    block = tide_heap_write_over(tide_heaps_of(&world.heap, 0u), a, 60);
    TIDE_CHECK(tide_heaps_of(&world.heap, 0u)->page[a >> TIDE_HEAP_PAGE_SHIFT] != shared && tide_heaps_of(&world.heap, 0u)->moves != moves);
    TIDE_CHECK(block == tide_heap_block(tide_heaps_of(&world.heap, 0u), a) && block->a == 7);
    memset(block + 1, 0x33, 60);
    TIDE_CHECK(all((const uint8_t *)(block + 1) + 60, 40, 0x11));
    const tide_block *other = tide_heap_block(tide_heaps_of(&world.heap, 0u), b);
    TIDE_CHECK(other->a == 9 && all((const uint8_t *)(other + 1), 100, 0x22));
    TIDE_CHECK(all((const uint8_t *)(tide_heap_block(tide_heaps_of(&snapshot, 0u), a) + 1), 100, 0x11));

    tide_heaps plain = {0};
    tide_heaps_copy(&plain, &snapshot);
    block = tide_heap_write(tide_heaps_of(&plain, 0u), a);
    memset(block + 1, 0x33, 60);
    TIDE_CHECK(tide_heaps_hash(1, &plain) == tide_heaps_hash(1, &world.heap));
    TIDE_CHECK(tide_heaps_hash(1, &plain) != tide_heaps_hash(1, &snapshot));

    const tide_page *own = tide_heaps_of(&world.heap, 0u)->page[a >> TIDE_HEAP_PAGE_SHIFT];
    TIDE_CHECK(tide_heap_write_over(tide_heaps_of(&world.heap, 0u), a, 100) == block || tide_heaps_of(&world.heap, 0u)->page[a >> TIDE_HEAP_PAGE_SHIFT] == own);
    TIDE_CHECK(tide_heaps_of(&world.heap, 0u)->page[a >> TIDE_HEAP_PAGE_SHIFT] == own);
    tide_heaps_free(&plain);
    tide_heaps_free(&snapshot);
}

// ---------------------------------------------------------------------------
// A world's heaps (tide/heap.h): one for each owner, by the id in a `where`

// Three heaps: the world's own, and ids 7 and 900
static uint16_t three_slots[TIDE_HEAP_IDS];
static const uint16_t three_ids[] = {0, 7, 900};
#define IN_SEVEN TIDE_WHERE(TIDE_IN_MATCH, 7u)
#define IN_NINE TIDE_WHERE(TIDE_IN_MATCH, 900u)

static void start_three(void)
{
    tide_heaps_free(&world.heap);
    memset(&world, 0, sizeof world);
    memset(three_slots, 0, sizeof three_slots);
    three_slots[7] = 1;
    three_slots[900] = 2;
    tide_heaps_shape(three_slots, three_ids, 3);
    tide_text_use(&world.heap, NULL);
}

static void end_three(fake_world *others[], const int count)
{
    for (int i = 0; i < count; i++) tide_heaps_free(&others[i]->heap);
    tide_heaps_free(&world.heap);
    tide_heaps_shape(NULL, NULL, 1); // One heap again, for the tests after
    memset(&world, 0, sizeof world);
}

TIDE_TEST(heaps_keep_each_owners_blocks_apart)
{
    start_three();
    tide_text_set(&world.name, text("cat"), IN_SEVEN);
    tide_text_set(&world.title, text("captain"), IN_NINE);
    TIDE_CHECK(reads(world.name, "cat") && reads(world.title, "captain"));
    // A handle says which heap its block is in, below its offset there
    TIDE_CHECK(tide_handle_where(world.name.at) == IN_SEVEN && tide_handle_where(world.title.at) == IN_NINE);
    TIDE_CHECK(tide_handle_world(world.name.at) == TIDE_IN_MATCH && tide_handle_offset(world.name.at) == 16u);
    TIDE_CHECK(tide_handle_offset(world.title.at) == 16u); // Each heap hands its own blocks out from its start
    TIDE_CHECK(tide_heaps_of(&world.heap, 0u)->used == 0 && tide_heaps_used(&world.heap) > 0);
    TIDE_CHECK(tide_heaps_of(&world.heap, 7u) != tide_heaps_of(&world.heap, 900u) && tide_heaps_of(&world.heap, 0u) == tide_heaps_of(&world.heap, 0u));
    TIDE_CHECK(tide_heaps_of(&world.heap, 7u)->used != 0 && tide_heaps_of(&world.heap, 900u)->used != 0);
    // An id the game hasn't is the world's own heap
    TIDE_CHECK(tide_heaps_of(&world.heap, 8u) == tide_heaps_of(&world.heap, 0u));

    // A field's own text is the one in its `where`: another owner's is borrowed, so it's copied, and left where it was
    const tide_handle sevens = world.name.at;
    tide_text copy = world.name;
    tide_text_own(&copy, IN_NINE);
    TIDE_CHECK(tide_handle_where(copy.at) == IN_NINE && reads(copy, "cat") && world.name.at == sevens);
    tide_text_release(&copy, IN_SEVEN); // Not seven's: nothing happens
    TIDE_CHECK(copy.at != 0);
    tide_text_release(&copy, IN_NINE);
    TIDE_CHECK(copy.at == 0 && tide_heaps_pending(&world.heap));
    tide_heaps_flush(&world.heap);
    TIDE_CHECK(!tide_heaps_pending(&world.heap));

    // One heap's blocks come and go without the other's moving
    const uint64_t nines = tide_heaps_of(&world.heap, 900u)->used;
    for (int i = 0; i < 100; i++) tide_text_set(&world.name, text(i % 2 ? "a longer cat than before" : "cat"), IN_SEVEN);
    tide_heaps_flush(&world.heap);
    TIDE_CHECK(tide_heaps_of(&world.heap, 900u)->used == nines && reads(world.title, "captain"));
    end_three(NULL, 0);
}

TIDE_TEST(heaps_copy_hash_and_pack_every_owners)
{
    start_three();
    static fake_world snapshot, back;
    memset(&snapshot, 0, sizeof snapshot);
    memset(&back, 0, sizeof back);
    const uint64_t empty = tide_heaps_hash(0, &world.heap);
    tide_text_set(&world.name, text("cat"), IN_SEVEN);
    const uint64_t one = tide_heaps_hash(0, &world.heap);
    tide_text_set(&world.title, text("captain"), IN_NINE);
    const uint64_t two = tide_heaps_hash(0, &world.heap);
    TIDE_CHECK(empty != one && one != two);

    // A snapshot shares every heap's pages, until one changes
    tide_heaps_copy(&snapshot.heap, &world.heap);
    snapshot.name = world.name;
    snapshot.title = world.title;
    TIDE_CHECK(tide_heaps_hash(0, &snapshot.heap) == two && tide_heaps_pages_apart(&world.heap, &snapshot.heap) == 0);
    TIDE_CHECK(tide_heaps_pages(&world.heap) == 2u);
    tide_text_set(&world.title, text("admiral"), IN_NINE);
    tide_heaps_flush(&world.heap);
    TIDE_CHECK(tide_heaps_pages_apart(&world.heap, &snapshot.heap) == 1u); // Nine's page alone
    TIDE_CHECK(tide_heaps_hash(0, &snapshot.heap) == two && tide_heaps_hash(0, &world.heap) != two);
    TIDE_CHECK(strcmp(tide_text_read(&snapshot.heap, snapshot.title).ptr, "captain") == 0);
    TIDE_CHECK(strcmp(tide_text_read(&world.heap, world.title).ptr, "admiral") == 0);

    // As bytes: the heaps that handed anything out, with their ids
    const uint32_t size = tide_heaps_packed_size(&world.heap);
    uint8_t *bytes = malloc(size);
    tide_writer w = {bytes, size, 0, false, false};
    tide_heaps_pack(&world.heap, &w);
    TIDE_REQUIRE(!w.overflow && w.size == size);
    tide_reader r = {bytes, size, 0, false};
    TIDE_REQUIRE(tide_heaps_unpack(&back.heap, &r) && r.at == size);
    TIDE_CHECK(tide_heaps_hash(0, &back.heap) == tide_heaps_hash(0, &world.heap));
    TIDE_CHECK(strcmp(tide_text_read(&back.heap, world.title).ptr, "admiral") == 0);
    TIDE_CHECK(strcmp(tide_text_read(&back.heap, world.name).ptr, "cat") == 0);

    // A game without one of the owners leaves its heap out: nothing of it is left to say where its blocks are
    tide_heaps_free(&back.heap);
    three_slots[900] = 0;
    static const uint16_t two_ids[] = {0, 7};
    tide_heaps_shape(three_slots, two_ids, 2);
    r = (tide_reader){bytes, size, 0, false};
    TIDE_REQUIRE(tide_heaps_unpack(&back.heap, &r) && r.at == size);
    TIDE_CHECK(strcmp(tide_text_read(&back.heap, world.name).ptr, "cat") == 0 && tide_heaps_pages(&back.heap) == 1u);
    tide_heaps_free(&back.heap);
    three_slots[900] = 2;
    tide_heaps_shape(three_slots, three_ids, 3);
    // ...and bytes that aren't a world's heaps are refused: a heap twice, or id 0 among the others
    memcpy(bytes + tide_heap_packed_size(tide_heaps_of(&world.heap, 0u)) + sizeof(uint32_t), &(uint32_t){0}, sizeof(uint32_t));
    r = (tide_reader){bytes, size, 0, false};
    TIDE_CHECK(!tide_heaps_unpack(&back.heap, &r));
    free(bytes);
    fake_world *others[] = {&snapshot, &back};
    end_three(others, 2);
}

TIDE_TEST(heaps_deltas_carry_only_the_heaps_that_changed)
{
    start_three();
    static fake_world base, back_base, back;
    memset(&base, 0, sizeof base);
    memset(&back_base, 0, sizeof back_base);
    memset(&back, 0, sizeof back);
    tide_text_set(&world.name, text("cat"), IN_SEVEN);
    tide_text_set(&world.title, text("captain"), IN_NINE);
    tide_heaps_copy(&base.heap, &world.heap);
    tide_heaps_copy(&back_base.heap, &world.heap);

    // Nothing changed: the delta says so in a few bytes, and the heaps are the base's
    tide_delta_writer d;
    tide_delta_begin(&d, true, NULL, 0, 42);
    tide_heaps_pack_delta(&world.heap, &base.heap, &d);
    uint32_t same_size = 0;
    uint8_t *same = tide_delta_end(&d, &same_size);
    tide_delta_reader in;
    uint64_t hash = 0;
    TIDE_REQUIRE(tide_delta_open(&in, same, same_size, true, &hash) && hash == 42);
    TIDE_REQUIRE(tide_heaps_unpack_delta(&back.heap, &back_base.heap, &in));
    TIDE_CHECK(tide_heaps_hash(0, &back.heap) == tide_heaps_hash(0, &world.heap));
    TIDE_CHECK(tide_heaps_pages_apart(&back.heap, &back_base.heap) == 0);
    tide_heaps_free(&back.heap);

    // One owner's changed: only its heap is in the delta
    tide_text_set(&world.title, text("admiral of the fleet, with a longer title than a captain's"), IN_NINE);
    tide_heaps_flush(&world.heap);
    tide_delta_begin(&d, true, NULL, 0, 42);
    tide_heaps_pack_delta(&world.heap, &base.heap, &d);
    uint32_t size = 0;
    uint8_t *bytes = tide_delta_end(&d, &size);
    TIDE_CHECK(size > same_size);
    TIDE_REQUIRE(tide_delta_open(&in, bytes, size, true, &hash));
    TIDE_REQUIRE(tide_heaps_unpack_delta(&back.heap, &back_base.heap, &in));
    TIDE_CHECK(tide_heaps_hash(0, &back.heap) == tide_heaps_hash(0, &world.heap));
    TIDE_CHECK(strcmp(tide_text_read(&back.heap, world.title).ptr, "admiral of the fleet, with a longer title than a captain's") == 0);
    TIDE_CHECK(strcmp(tide_text_read(&back.heap, world.name).ptr, "cat") == 0);
    TIDE_CHECK(tide_heaps_pages_apart(&back.heap, &back_base.heap) == 1u); // Seven's page is the base's own

    // With no base, every heap that handed anything out
    tide_heaps_free(&back.heap);
    tide_delta_begin(&d, false, NULL, 0, 42);
    tide_heaps_pack_delta(&world.heap, NULL, &d);
    uint32_t whole_size = 0;
    uint8_t *whole = tide_delta_end(&d, &whole_size);
    TIDE_REQUIRE(tide_delta_open(&in, whole, whole_size, false, &hash));
    TIDE_REQUIRE(tide_heaps_unpack_delta(&back.heap, NULL, &in));
    TIDE_CHECK(tide_heaps_hash(0, &back.heap) == tide_heaps_hash(0, &world.heap));
    free(whole);
    free(bytes);
    free(same);
    fake_world *others[] = {&base, &back_base, &back};
    end_three(others, 3);
}

// ---------------------------------------------------------------------------
// A heap in runs (tide/heap.h): a system's entities in runs, each run's blocks
// in a heap of its own within its owner's

#define SEVEN_RUN(run) (IN_SEVEN | (uint32_t)(run) << TIDE_WHERE_RUN_SHIFT)

static tide_heap *seven(const uint32_t run)
{
    return tide_heaps_of(&world.heap, 7u | run << TIDE_HEAP_ID_BITS);
}

TIDE_TEST(heap_runs_hand_out_their_own_blocks)
{
    start_three();
    // What a run's code makes in an owner's memory is in that run's heap, from its start, and says so
    uint32_t was = tide_heap_run_enter(3u);
    TIDE_CHECK(TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u) == SEVEN_RUN(3));
    tide_text_set(&world.name, text("cat"), TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u));
    tide_heap_run_leave(was);
    TIDE_CHECK(TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u) == IN_SEVEN); // Outside runs: the owner's first
    TIDE_CHECK(tide_handle_where(world.name.at) == SEVEN_RUN(3) && tide_handle_offset(world.name.at) == 16u);
    TIDE_CHECK(tide_handle_in(world.name.at, IN_SEVEN) && !tide_handle_in(world.name.at, IN_NINE) && !tide_handle_in(world.name.at, TIDE_IN_MATCH));
    TIDE_CHECK(seven(0)->used == 0 && seven(3)->used != 0 && seven(3)->run == 3u && seven(3)->base == seven(0));
    tide_text_set(&world.title, text("dog"), IN_SEVEN);
    TIDE_CHECK(tide_handle_offset(world.title.at) == 16u && world.title.at != world.name.at);
    TIDE_CHECK(reads(world.name, "cat") && reads(world.title, "dog"));
    const tide_handle threes = world.name.at;

    // Another run's row lets go of it: run 3's heap is as it was, and run 5's keeps the block, readable until
    // the row is done, zeroed then, and back in run 3's heap when the runs join
    was = tide_heap_run_enter(5u);
    const tide_text old = world.name;
    tide_text_set(&world.name, text("a bird"), TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u));
    TIDE_CHECK(tide_handle_where(world.name.at) == SEVEN_RUN(5) && reads(world.name, "a bird") && reads(old, "cat"));
    TIDE_CHECK(seven(3)->pending == 0 && seven(3)->free[1] == 0 && seven(5)->away != 0 && seven(5)->held == 0);
    tide_heap_flush(seven(5));
    TIDE_CHECK(seven(5)->away == 0 && seven(5)->held != 0 && seven(3)->free[1] == 0);
    TIDE_CHECK(tide_text_view(old).bytes == 0); // Freed memory is zero
    tide_heap_run_leave(was);
    tide_heap_runs_join(seven(0));
    TIDE_CHECK(seven(5)->held == 0 && seven(3)->free[1] == 16u);
    was = tide_heap_run_enter(3u);
    tide_text again = {0};
    tide_text_set(&again, text("cow"), TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u));
    tide_heap_run_leave(was);
    TIDE_CHECK(again.at == threes && reads(again, "cow"));

    // Outside runs, a block goes back to the heap of the run it was made in, at the owner's flush
    tide_text_release(&again, IN_SEVEN);
    tide_text_release(&world.name, IN_SEVEN);
    TIDE_CHECK(again.at == 0 && world.name.at == 0 && tide_heaps_pending(&world.heap) && seven(0)->pending == 0);
    tide_heaps_flush(&world.heap);
    TIDE_CHECK(!tide_heaps_pending(&world.heap) && seven(3)->free[1] == 16u && seven(5)->free[1] == 16u && seven(0)->free[1] == 0);
    TIDE_CHECK(reads(world.title, "dog"));
    end_three(NULL, 0);
}

TIDE_TEST(heap_runs_move_a_list_in_chunks_whole)
{
    start_three();
    tide_list l = {0};
    uint32_t was = tide_heap_run_enter(1u);
    for (int32_t i = 0; i < 6000; i++) *(int32_t *)tide_list_add(&l, sizeof(int32_t), TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u)) = i;
    tide_heap_flush(seven(1));
    tide_heap_run_leave(was);
    tide_heap_runs_join(seven(0));
    TIDE_CHECK(tide_handle_where(l.at) == SEVEN_RUN(1) && (tide_block_at(l.at)->b & TIDE_LIST_CHUNKED));
    const uint64_t ones = seven(1)->used;

    // Written and cut where it is from another run, and added to within its room
    was = tide_heap_run_enter(2u);
    const tide_handle at = l.at;
    *(int32_t *)tide_list_at_mut(l, 10, sizeof(int32_t)) = -10;
    tide_list_resize(&l, 5990, sizeof(int32_t), TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u));
    *(int32_t *)tide_list_add(&l, sizeof(int32_t), TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u)) = 5990;
    TIDE_CHECK(l.at == at && seven(2)->used == 0);
    // More chunks: they'd be run 2's, which a block in run 1's heap can't list. The list moves there whole
    for (int32_t i = 5991; i < 12000; i++) *(int32_t *)tide_list_add(&l, sizeof(int32_t), TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u)) = i;
    TIDE_CHECK(tide_handle_where(l.at) == SEVEN_RUN(2) && seven(1)->used == ones && seven(2)->away != 0);
    tide_heap_flush(seven(2));
    tide_heap_run_leave(was);
    tide_heap_runs_join(seven(0));
    bool same = tide_list_count(l) == 12000;
    for (int32_t i = 0; same && i < 12000; i++) same = *(const int32_t *)tide_list_at(l, i, sizeof(int32_t)) == (i == 10 ? -10 : i);
    TIDE_CHECK(same);

    // Run 1's blocks are its own again: the same list made there takes no more of its heap
    tide_list second = {0};
    was = tide_heap_run_enter(1u);
    for (int32_t i = 0; i < 6000; i++) *(int32_t *)tide_list_add(&second, sizeof(int32_t), TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u)) = i;
    tide_heap_flush(seven(1));
    tide_heap_run_leave(was);
    tide_heap_runs_join(seven(0));
    TIDE_CHECK(seven(1)->used == ones && tide_list_count(second) == 6000);
    // Outside runs, one that needs more chunks moves to the owner's first heap
    for (int32_t i = 6000; i < 12000; i++) *(int32_t *)tide_list_add(&second, sizeof(int32_t), IN_SEVEN) = i;
    tide_heaps_flush(&world.heap);
    TIDE_CHECK(tide_handle_where(second.at) == IN_SEVEN && *(const int32_t *)tide_list_at(second, 11999, sizeof(int32_t)) == 11999);
    tide_list_release(&second, IN_SEVEN);
    tide_list_release(&l, IN_SEVEN);
    tide_heaps_flush(&world.heap);
    TIDE_CHECK(!tide_heaps_pending(&world.heap));
    end_three(NULL, 0);
}

// Grid2<int>: chunks of 64 by 64, a page each
static const tide_grid_shape grid_ints = {4u, 2u, {6u, 6u, 0u}};

static int32_t grid_cell(const tide_grid g, const int32_t x, const int32_t y)
{
    const int32_t *c = tide_grid_peek(g, x, y, 0, &grid_ints);
    return c ? *c : 0;
}

static void grid_put(tide_grid *g, const int32_t x, const int32_t y, const int32_t v, const uint32_t where)
{
    int32_t *c = tide_grid_poke(g, x, y, 0, &grid_ints, v != 0, where);
    if (c) *c = v;
}

TIDE_TEST(heap_runs_move_a_grid_whole)
{
    start_three();
    tide_grid g = {0};
    uint32_t was = tide_heap_run_enter(1u);
    grid_put(&g, 1, 2, 12, TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u));
    grid_put(&g, 100, 3, 13, TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u));
    grid_put(&g, -5, -70, 14, TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u));
    tide_heap_flush(seven(1));
    tide_heap_run_leave(was);
    tide_heap_runs_join(seven(0));
    TIDE_CHECK(tide_handle_where(g.at) == SEVEN_RUN(1) && tide_grid_dir_of(g)->records == 3u);
    const uint64_t ones = seven(1)->used;

    // From another run: cells of its chunks written where they are, and zero where it has none makes none
    was = tide_heap_run_enter(2u);
    const tide_handle at = g.at;
    grid_put(&g, 1, 3, 15, TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u));
    grid_put(&g, 500, 500, 0, TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u));
    TIDE_CHECK(g.at == at && seven(2)->used == 0 && grid_cell(g, 1, 3) == 15 && tide_grid_dir_of(g)->records == 3u);
    // ...through a cache too, which holds the grid's own heap
    tide_grid_cache cache = {0};
    *(int32_t *)tide_grid_cached_poke(&cache, &g, 2, 3, 0, &grid_ints, true, TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u)) = 16;
    TIDE_CHECK(g.at == at && cache.heap == seven(1) && cache.write != NULL);
    // A chunk it hasn't: run 2's rows make blocks in run 2's heap alone, and a directory names its chunks by their
    // offsets in its own, so the grid moves there whole, every cell with it. Run 1's blocks wait for the join
    grid_put(&g, 200, 200, 17, TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u));
    TIDE_CHECK(tide_handle_where(g.at) == SEVEN_RUN(2) && seven(1)->used == ones && seven(2)->away != 0);
    TIDE_CHECK(tide_grid_dir_of(g)->records == 4u && grid_cell(g, 1, 2) == 12 && grid_cell(g, 100, 3) == 13);
    TIDE_CHECK(grid_cell(g, -5, -70) == 14 && grid_cell(g, 1, 3) == 15 && grid_cell(g, 2, 3) == 16 && grid_cell(g, 200, 200) == 17);
    TIDE_CHECK(!tide_grid_cache_holds(&cache, g.at, 0, 0, 0)); // The grid it held is gone
    tide_heap_flush(seven(2));
    tide_heap_run_leave(was);
    tide_heap_runs_join(seven(0));
    TIDE_CHECK(!seven(2)->away && !seven(2)->held && !seven(1)->pending);

    // Run 1's blocks are its own again: the same grid made there takes no more of its heap
    tide_grid again = {0};
    was = tide_heap_run_enter(1u);
    grid_put(&again, 1, 2, 1, TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u));
    grid_put(&again, 100, 3, 1, TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u));
    grid_put(&again, -5, -70, 1, TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u));
    tide_heap_flush(seven(1));
    tide_heap_run_leave(was);
    tide_heap_runs_join(seven(0));
    TIDE_CHECK(seven(1)->used == ones && grid_cell(again, -5, -70) == 1);

    // Outside runs, a chunk it hasn't is made in its own heap, whichever run's that is
    const tide_handle twos = g.at;
    grid_put(&g, -300, 0, 18, IN_SEVEN);
    TIDE_CHECK(g.at == twos && grid_cell(g, -300, 0) == 18 && seven(0)->used == 0);

    // A parallel loop from run 3: its steps read the grid where it is, and its end makes the chunks they wrote in
    // run 3's heap, the grid moved there first
    tide_grid sized = {0};
    was = tide_heap_run_enter(1u);
    tide_grid_set(&sized, tide_grid_new(2u, 128, 64, 0), &grid_ints, TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u));
    grid_put(&sized, 3, 4, 34, TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u));
    tide_heap_flush(seven(1));
    tide_heap_run_leave(was);
    tide_heap_runs_join(seven(0));
    TIDE_CHECK(tide_handle_where(sized.at) == SEVEN_RUN(1) && tide_grid_dir_of(sized)->records == 1u);
    was = tide_heap_run_enter(3u);
    tide_par_loop loop;
    const int32_t block[3] = {1, 1, 1}, offset[3] = {0, 0, 0};
    tide_par_begin(&loop, &sized, &grid_ints, TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u), block, offset);
    TIDE_CHECK(loop.tasks == 2u);
    tide_par_cache step = {{0, 0, 0}, NULL};
    *(int32_t *)tide_par_cell(&loop, &step, 70, 5, 0) = 75;
    *(int32_t *)tide_par_cell(&loop, &step, 3, 5, 0) = 35;
    tide_par_end(&loop);
    TIDE_CHECK(tide_handle_where(sized.at) == SEVEN_RUN(3) && tide_grid_dir_of(sized)->records == 2u);
    TIDE_CHECK(grid_cell(sized, 3, 4) == 34 && grid_cell(sized, 3, 5) == 35 && grid_cell(sized, 70, 5) == 75);
    tide_heap_flush(seven(3));
    tide_heap_run_leave(was);
    tide_heap_runs_join(seven(0));

    // Let go of outside runs: each through its own heap, and the owner's flush finds them
    tide_grid_release(&g, &grid_ints, IN_SEVEN);
    tide_grid_release(&again, &grid_ints, IN_SEVEN);
    tide_grid_release(&sized, &grid_ints, IN_SEVEN);
    TIDE_CHECK(!g.at && !again.at && !sized.at && tide_heaps_pending(&world.heap));
    tide_heaps_flush(&world.heap);
    TIDE_CHECK(!tide_heaps_pending(&world.heap));
    end_three(NULL, 0);
}

TIDE_TEST(heap_runs_pages_keep_their_references_until_the_join)
{
    start_three();
    static fake_world snapshot;
    tide_text_set(&world.name, text("cat"), IN_SEVEN);
    tide_text_set(&world.title, text("captain"), IN_SEVEN);
    tide_heaps_copy(&snapshot.heap, &world.heap);
    const uint64_t before = tide_heaps_hash(0, &snapshot.heap);
    tide_page *shared = seven(0)->page[0];
    const uint32_t refs = shared->refs;
    TIDE_CHECK(refs == 2u);

    // A run writes a block of the page: the heap gets a copy, and the page keeps the heap's reference, so a
    // thread that read it from the table a moment ago still sees a page that isn't the heap's to write
    const uint32_t was = tide_heap_run_enter(4u);
    tide_block_write(world.name.at)->b = 3u;
    TIDE_CHECK(seven(0)->page[0] != shared && shared->refs == refs && seven(0)->retired_count == 1u);
    tide_block_write(world.title.at)->b = 7u; // The copy is the heap's own: written where it is
    TIDE_CHECK(seven(0)->retired_count == 1u && seven(0)->page[0]->refs == 1u);
    tide_heap_run_leave(was);
    tide_heap_runs_join(seven(0));
    TIDE_CHECK(shared->refs == 1u && seven(0)->retired_count == 0u);
    TIDE_CHECK(tide_heaps_hash(0, &snapshot.heap) == before && reads(world.name, "cat"));
    TIDE_CHECK(tide_heaps_pages_apart(&world.heap, &snapshot.heap) == 1u);
    fake_world *others[] = {&snapshot};
    end_three(others, 1);
}

TIDE_TEST(heap_runs_copy_hash_and_pack_with_their_owner)
{
    start_three();
    static fake_world other, snapshot, back, base, back_base;
    uint32_t was = tide_heap_run_enter(6u);
    tide_text_set(&world.name, text("cat"), TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u));
    tide_heap_run_leave(was);
    tide_text_set(&world.title, text("captain"), IN_NINE);
    // The same text in the owner's first heap is another world: a handle says the run
    tide_text_use(&other.heap, NULL);
    tide_text_set(&other.name, text("cat"), IN_SEVEN);
    tide_text_set(&other.title, text("captain"), IN_NINE);
    tide_text_use(&world.heap, NULL);
    const uint64_t hash = tide_heaps_hash(0, &world.heap);
    TIDE_CHECK(hash != tide_heaps_hash(0, &other.heap) && tide_heaps_used(&world.heap) == tide_heaps_used(&other.heap));
    TIDE_CHECK(tide_heaps_pages(&world.heap) == 2u);

    tide_heaps_copy(&snapshot.heap, &world.heap);
    TIDE_CHECK(tide_heaps_hash(0, &snapshot.heap) == hash && tide_heaps_pages_apart(&world.heap, &snapshot.heap) == 0);
    TIDE_CHECK(strcmp(tide_text_read(&snapshot.heap, world.name).ptr, "cat") == 0);

    const uint32_t packed = tide_heaps_packed_size(&world.heap);
    uint8_t *bytes = malloc(packed);
    tide_writer w = {bytes, packed, 0, false, false};
    tide_heaps_pack(&world.heap, &w);
    TIDE_REQUIRE(!w.overflow && w.size == packed);
    tide_reader r = {bytes, packed, 0, false};
    TIDE_REQUIRE(tide_heaps_unpack(&back.heap, &r) && r.at == packed);
    TIDE_CHECK(tide_heaps_hash(0, &back.heap) == hash && strcmp(tide_text_read(&back.heap, world.name).ptr, "cat") == 0);
    r = (tide_reader){bytes, packed, 0, false};
    TIDE_REQUIRE(tide_heaps_unpack(&back_base.heap, &r));
    free(bytes);
    tide_heaps_free(&back.heap);

    // A delta has the run's heap that changed, and the rest are the base's
    tide_heaps_copy(&base.heap, &world.heap);
    was = tide_heap_run_enter(6u);
    tide_text_set(&world.name, text("a cat with a longer name"), TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u));
    tide_heap_flush(seven(6));
    tide_heap_run_leave(was);
    tide_heap_runs_join(seven(0));
    tide_delta_writer d;
    tide_delta_begin(&d, true, NULL, 0, 42);
    tide_heaps_pack_delta(&world.heap, &base.heap, &d);
    uint32_t size = 0;
    uint8_t *delta = tide_delta_end(&d, &size);
    tide_delta_reader in;
    uint64_t said = 0;
    TIDE_REQUIRE(tide_delta_open(&in, delta, size, true, &said) && said == 42);
    TIDE_REQUIRE(tide_heaps_unpack_delta(&back.heap, &back_base.heap, &in));
    TIDE_CHECK(tide_heaps_hash(0, &back.heap) == tide_heaps_hash(0, &world.heap));
    TIDE_CHECK(strcmp(tide_text_read(&back.heap, world.name).ptr, "a cat with a longer name") == 0);
    TIDE_CHECK(strcmp(tide_text_read(&back.heap, world.title).ptr, "captain") == 0);
    TIDE_CHECK(tide_heaps_pages_apart(&back.heap, &back_base.heap) == 1u); // Nine's page is the base's own
    free(delta);

    // A snapshot with no runs taken over one that has them leaves none
    tide_heaps_copy(&snapshot.heap, &other.heap);
    TIDE_CHECK(tide_heaps_hash(0, &snapshot.heap) == tide_heaps_hash(0, &other.heap));
    fake_world *others[] = {&other, &snapshot, &back, &base, &back_base};
    end_three(others, 5);
}

// One owner's heap kept as it is (tide_heaps_keep), for reading what it held while the world's goes on: its
// blocks read through the fourth `where` (TIDE_IN_VERIFIED), the other owners' not kept at all.
TIDE_TEST(heaps_keep_one_owners_heap)
{
    start_three();
    static fake_world kept;
    memset(&kept, 0, sizeof kept);
    tide_text_set(&world.name, text("cat"), IN_SEVEN);
    tide_text_set(&world.title, text("captain"), IN_NINE);
    tide_heaps_flush(&world.heap);
    tide_heaps_keep(&kept.heap, &world.heap, 7u);
    TIDE_CHECK(tide_heaps_of(&kept.heap, 7u)->used == tide_heaps_of(&world.heap, 7u)->used);
    TIDE_CHECK(tide_heaps_of(&kept.heap, 900u)->used == 0 && tide_heaps_pages(&kept.heap) == 1u);
    // What a value read then names is the kept heap's block, whatever the world's becomes
    const tide_text was = {tide_confirmed(world.name.at)};
    tide_text_use_verified(&kept.heap);
    tide_text_set(&world.name, text("a longer cat than before"), IN_SEVEN);
    tide_text_set(&world.title, text("admiral"), IN_NINE);
    tide_heaps_flush(&world.heap);
    TIDE_CHECK(reads(was, "cat") && reads(world.name, "a longer cat than before"));
    TIDE_CHECK(strcmp(tide_text_read(&kept.heap, (tide_text){tide_handle_of(IN_SEVEN, tide_handle_offset(was.at))}).ptr, "cat") == 0);
    // Made the world's own, it's a copy
    tide_text copy = was;
    tide_text_own(&copy, IN_NINE);
    TIDE_CHECK(tide_handle_where(copy.at) == IN_NINE && reads(copy, "cat"));
    tide_text_release(&copy, IN_NINE);
    // Kept again, it's the heap as it is now; freed, the world's pages are its own
    tide_heaps_keep(&kept.heap, &world.heap, 7u);
    const tide_text now = {tide_confirmed(world.name.at)};
    TIDE_CHECK(reads(now, "a longer cat than before"));
    tide_text_use_verified(NULL);
    tide_heaps_free(&kept.heap);
    tide_text_set(&world.name, text("dog"), IN_SEVEN);
    TIDE_CHECK(reads(world.name, "dog"));
    // A world with no heaps yet keeps nothing
    static fake_world empty;
    memset(&empty, 0, sizeof empty);
    tide_heaps_keep(&kept.heap, &empty.heap, 7u);
    TIDE_CHECK(kept.heap.all == NULL);
    end_three(NULL, 0);
}

// ---------------------------------------------------------------------------
// Pages given back (tide/heap.h). Each test does the same to two heaps, one
// settled as a world's is after a tick's changes and a twin that never is: the
// first gives pages back, and is otherwise the twin, byte for byte.

static uint8_t *packed_heaps(const tide_heaps *hs, uint32_t *size)
{
    *size = tide_heaps_packed_size(hs);
    uint8_t *bytes = malloc(*size);
    tide_writer w = {bytes, *size, 0, false, false};
    tide_heaps_pack(hs, &w);
    TIDE_CHECK(!w.overflow && w.size == *size);
    return bytes;
}

static bool same_packed(const tide_heaps *a, const tide_heaps *b)
{
    uint32_t a_size, b_size;
    uint8_t *a_bytes = packed_heaps(a, &a_size), *b_bytes = packed_heaps(b, &b_size);
    const bool same = a_size == b_size && memcmp(a_bytes, b_bytes, a_size) == 0;
    free(a_bytes);
    free(b_bytes);
    return same;
}

// A block's bytes say which block it is.
static void fill_block(tide_heaps *hs, const uint64_t block, const uint32_t bytes, const uint32_t which)
{
    uint8_t *data = (uint8_t *)(tide_heap_write(tide_heaps_of(hs, 0u), block) + 1);
    for (uint32_t i = 0; i < bytes; i++) data[i] = (uint8_t)(which * 31u + i * 7u + 1u);
}

static bool block_filled(const tide_heaps *hs, const uint64_t block, const uint32_t bytes, const uint32_t which)
{
    const uint8_t *data = (const uint8_t *)(tide_heap_block(tide_heaps_of((tide_heaps *)(uintptr_t)hs, 0u), block) + 1);
    for (uint32_t i = 0; i < bytes; i++) {
        if (data[i] != (uint8_t)(which * 31u + i * 7u + 1u)) return false;
    }
    return true;
}

static bool block_zero(const tide_heaps *hs, const uint64_t block, const uint32_t bytes)
{
    const uint8_t *data = (const uint8_t *)(tide_heap_block(tide_heaps_of((tide_heaps *)(uintptr_t)hs, 0u), block) + 1);
    for (uint32_t i = 0; i < bytes; i++) {
        if (data[i]) return false;
    }
    return true;
}

TIDE_TEST(heap_gives_back_the_pages_whose_blocks_are_all_free)
{
    enum { BLOCKS = 600, KEPT = 100, BYTES = 4000 };
    tide_heaps world_heaps = {0}, twin = {0};
    static uint64_t at[BLOCKS];
    for (uint32_t i = 0; i < BLOCKS; i++) {
        at[i] = tide_heap_alloc(tide_heaps_of(&world_heaps, 0u), BYTES);
        TIDE_REQUIRE(at[i] && tide_heap_alloc(tide_heaps_of(&twin, 0u), BYTES) == at[i]);
        fill_block(&world_heaps, at[i], BYTES, i);
        fill_block(&twin, at[i], BYTES, i);
    }
    tide_heaps_settle(&world_heaps);
    TIDE_CHECK(tide_heaps_gone(&world_heaps) == 0 && tide_heaps_pages(&world_heaps) == tide_heaps_pages(&twin));
    const uint32_t most = tide_heaps_pages(&world_heaps);
    TIDE_CHECK(most >= BLOCKS / 4u);

    // A snapshot from before anything went: it keeps every block, whatever the world gives back
    tide_heaps before = {0};
    tide_heaps_copy(&before, &world_heaps);
    const uint64_t before_hash = tide_heaps_hash(1, &before);

    for (uint32_t i = KEPT; i < BLOCKS; i++) {
        tide_heap_release(tide_heaps_of(&world_heaps, 0u), at[i]);
        tide_heap_release(tide_heaps_of(&twin, 0u), at[i]);
    }
    // Released and not yet freed: nothing goes
    tide_heaps_settle(&world_heaps);
    TIDE_CHECK(tide_heaps_gone(&world_heaps) == 0);
    tide_heaps_flush(&world_heaps);
    tide_heaps_flush(&twin);
    TIDE_CHECK(tide_heaps_gone(&world_heaps) == 0); // Only a settle gives back
    tide_heaps_settle(&world_heaps);
    tide_heaps_settle(&world_heaps); // Again: nothing more to do
    const uint32_t gone = tide_heaps_gone(&world_heaps);
    TIDE_CHECK(gone >= (BLOCKS - KEPT) / 4u - 2u && tide_heaps_pages(&world_heaps) + gone == most);
    TIDE_CHECK(tide_heaps_pages(&world_heaps) <= KEPT / 4u + 3u);
    TIDE_CHECK(tide_heaps_pages(&twin) == most && tide_heaps_gone(&twin) == 0);
    // The same heap: its hash, what it's handed out, its bytes
    const uint64_t freed_hash = tide_heaps_hash(1, &twin);
    TIDE_CHECK(tide_heaps_hash(1, &world_heaps) == freed_hash && freed_hash != before_hash);
    TIDE_CHECK(tide_heaps_used(&world_heaps) == tide_heaps_used(&twin));
    TIDE_CHECK(same_packed(&world_heaps, &twin));
    for (uint32_t i = 0; i < KEPT; i++) TIDE_CHECK(block_filled(&world_heaps, at[i], BYTES, i));
    // The snapshot from before has every block still
    TIDE_CHECK(tide_heaps_hash(1, &before) == before_hash && tide_heaps_gone(&before) == 0);
    for (uint32_t i = 0; i < BLOCKS; i += 7u) TIDE_CHECK(block_filled(&before, at[i], BYTES, i));

    // A snapshot of the world with pages gone is the same heap too, and stays so when the world takes them again
    tide_heaps after = {0};
    tide_heaps_copy(&after, &world_heaps);
    TIDE_CHECK(tide_heaps_gone(&after) == gone && tide_heaps_hash(1, &after) == freed_hash);
    TIDE_CHECK(tide_heaps_pages_apart(&world_heaps, &after) == 0);
    tide_heaps twin_after = {0};
    tide_heaps_copy(&twin_after, &twin);

    // The blocks handed out next are the ones the twin hands out, zeroed, their pages back
    static uint64_t again[BLOCKS];
    for (uint32_t i = KEPT; i < BLOCKS; i++) {
        again[i] = tide_heap_alloc(tide_heaps_of(&world_heaps, 0u), BYTES);
        TIDE_REQUIRE(again[i] && tide_heap_alloc(tide_heaps_of(&twin, 0u), BYTES) == again[i]);
        TIDE_CHECK(block_zero(&world_heaps, again[i], BYTES));
        fill_block(&world_heaps, again[i], BYTES, i + 1000u);
        fill_block(&twin, again[i], BYTES, i + 1000u);
    }
    TIDE_CHECK(tide_heaps_gone(&world_heaps) == 0 && tide_heaps_pages(&world_heaps) == most);
    TIDE_CHECK(tide_heaps_hash(1, &world_heaps) == tide_heaps_hash(1, &twin) && same_packed(&world_heaps, &twin));
    for (uint32_t i = 0; i < KEPT; i++) TIDE_CHECK(block_filled(&world_heaps, at[i], BYTES, i));
    for (uint32_t i = KEPT; i < BLOCKS; i++) TIDE_CHECK(block_filled(&world_heaps, again[i], BYTES, i + 1000u));
    TIDE_CHECK(tide_heaps_hash(1, &after) == freed_hash && tide_heaps_gone(&after) == gone && same_packed(&after, &twin_after));
    TIDE_CHECK(tide_heaps_hash(1, &before) == before_hash);

    // Everything let go of: all but the first page and the last go
    for (uint32_t i = 0; i < KEPT; i++) tide_heap_release(tide_heaps_of(&world_heaps, 0u), at[i]);
    for (uint32_t i = KEPT; i < BLOCKS; i++) tide_heap_release(tide_heaps_of(&world_heaps, 0u), again[i]);
    tide_heaps_flush(&world_heaps);
    tide_heaps_settle(&world_heaps);
    TIDE_CHECK(tide_heaps_pages(&world_heaps) <= 2u);
    // The world freed first, the snapshots keep what they shared with it
    tide_heaps_free(&world_heaps);
    TIDE_CHECK(tide_heaps_hash(1, &after) == freed_hash && same_packed(&after, &twin_after));
    for (uint32_t i = 0; i < KEPT; i++) TIDE_CHECK(block_filled(&after, at[i], BYTES, i));
    TIDE_CHECK(tide_heaps_hash(1, &before) == before_hash);
    tide_heaps_free(&after);
    tide_heaps_free(&before);
    tide_heaps_free(&twin_after);
    tide_heaps_free(&twin);
}

TIDE_TEST(heap_gives_back_a_big_blocks_page_and_keeps_pages_of_small_blocks)
{
    enum { BIG = 24, SMALL = 4096 };
    tide_heaps world_heaps = {0}, twin = {0};
    static uint64_t big[BIG], small[SMALL];
    // Blocks of four places each, then small ones, a link each when free: too many to a page for it to go
    for (uint32_t i = 0; i < BIG; i++) {
        big[i] = tide_heap_alloc(tide_heaps_of(&world_heaps, 0u), 60000);
        TIDE_REQUIRE(big[i] && tide_heap_alloc(tide_heaps_of(&twin, 0u), 60000) == big[i]);
        fill_block(&world_heaps, big[i], 60000, i);
        fill_block(&twin, big[i], 60000, i);
    }
    for (uint32_t i = 0; i < SMALL; i++) {
        small[i] = tide_heap_alloc(tide_heaps_of(&world_heaps, 0u), 16);
        TIDE_REQUIRE(small[i] && tide_heap_alloc(tide_heaps_of(&twin, 0u), 16) == small[i]);
    }
    const uint32_t most = tide_heaps_pages(&world_heaps);
    for (uint32_t i = 2; i < BIG; i++) {
        tide_heap_release(tide_heaps_of(&world_heaps, 0u), big[i]);
        tide_heap_release(tide_heaps_of(&twin, 0u), big[i]);
    }
    for (uint32_t i = 0; i < SMALL; i++) {
        tide_heap_release(tide_heaps_of(&world_heaps, 0u), small[i]);
        tide_heap_release(tide_heaps_of(&twin, 0u), small[i]);
    }
    tide_heaps_flush(&world_heaps);
    tide_heaps_flush(&twin);
    tide_heaps_settle(&world_heaps);
    TIDE_CHECK(tide_heaps_gone(&world_heaps) == (BIG - 2u) * 4u);
    TIDE_CHECK(tide_heaps_pages(&world_heaps) == most - (BIG - 2u) * 4u);
    TIDE_CHECK(tide_heaps_hash(1, &world_heaps) == tide_heaps_hash(1, &twin) && same_packed(&world_heaps, &twin));
    tide_heaps_settle(&world_heaps); // The small blocks' pages aren't looked at again
    TIDE_CHECK(tide_heaps_gone(&world_heaps) == (BIG - 2u) * 4u);
    // Blocks of each kind again: the twin's, and the small ones' pages count again once they're free
    for (uint32_t round = 0; round < 3; round++) {
        for (uint32_t i = 2; i < BIG; i++) {
            big[i] = tide_heap_alloc(tide_heaps_of(&world_heaps, 0u), 60000);
            TIDE_REQUIRE(big[i] && tide_heap_alloc(tide_heaps_of(&twin, 0u), 60000) == big[i]);
            TIDE_CHECK(block_zero(&world_heaps, big[i], 60000));
            fill_block(&world_heaps, big[i], 60000, i + round);
            fill_block(&twin, big[i], 60000, i + round);
        }
        for (uint32_t i = 0; i < SMALL; i++) {
            small[i] = tide_heap_alloc(tide_heaps_of(&world_heaps, 0u), 16);
            TIDE_REQUIRE(small[i] && tide_heap_alloc(tide_heaps_of(&twin, 0u), 16) == small[i]);
        }
        TIDE_CHECK(tide_heaps_gone(&world_heaps) == 0 && tide_heaps_pages(&world_heaps) == most);
        TIDE_CHECK(block_filled(&world_heaps, big[0], 60000, 0) && block_filled(&world_heaps, big[1], 60000, 1));
        for (uint32_t i = 2; i < BIG; i++) {
            tide_heap_release(tide_heaps_of(&world_heaps, 0u), big[i]);
            tide_heap_release(tide_heaps_of(&twin, 0u), big[i]);
        }
        for (uint32_t i = 0; i < SMALL; i++) {
            tide_heap_release(tide_heaps_of(&world_heaps, 0u), small[i]);
            tide_heap_release(tide_heaps_of(&twin, 0u), small[i]);
        }
        tide_heaps_flush(&world_heaps);
        tide_heaps_flush(&twin);
        tide_heaps_settle(&world_heaps);
        TIDE_CHECK(tide_heaps_gone(&world_heaps) == (BIG - 2u) * 4u);
        TIDE_CHECK(tide_heaps_hash(1, &world_heaps) == tide_heaps_hash(1, &twin) && same_packed(&world_heaps, &twin));
    }
    tide_heaps_free(&world_heaps);
    tide_heaps_free(&twin);
}

// A delta of `now` from `base`, read back over `read_base`, which is the same heap as `base` kept another way.
static bool delta_reads(const tide_heaps *now, const tide_heaps *base, const tide_heaps *read_base)
{
    tide_delta_writer d;
    tide_delta_begin(&d, true, NULL, 0, 42);
    tide_heaps_pack_delta(now, base, &d);
    uint32_t size = 0;
    uint8_t *delta = tide_delta_end(&d, &size);
    tide_delta_reader dr;
    uint64_t hash = 0;
    tide_heaps back = {0};
    bool ok = tide_delta_open(&dr, delta, size, true, &hash) && tide_heaps_unpack_delta(&back, read_base, &dr);
    ok = ok && tide_heaps_hash(1, &back) == tide_heaps_hash(1, now) && same_packed(&back, now);
    free(delta);
    tide_heaps_free(&back);
    return ok;
}

TIDE_TEST(heap_with_pages_given_back_packs_and_makes_deltas_as_its_twin)
{
    enum { BLOCKS = 400, BYTES = 4000 };
    tide_heaps world_heaps = {0}, twin = {0};
    static uint64_t at[BLOCKS];
    for (uint32_t i = 0; i < BLOCKS; i++) {
        at[i] = tide_heap_alloc(tide_heaps_of(&world_heaps, 0u), BYTES);
        TIDE_REQUIRE(at[i] && tide_heap_alloc(tide_heaps_of(&twin, 0u), BYTES) == at[i]);
        fill_block(&world_heaps, at[i], BYTES, i);
        fill_block(&twin, at[i], BYTES, i);
    }
    for (uint32_t i = 40; i < BLOCKS; i++) {
        tide_heap_release(tide_heaps_of(&world_heaps, 0u), at[i]);
        tide_heap_release(tide_heaps_of(&twin, 0u), at[i]);
    }
    tide_heaps_flush(&world_heaps);
    tide_heaps_flush(&twin);
    tide_heaps_settle(&world_heaps);
    TIDE_REQUIRE(tide_heaps_gone(&world_heaps) >= 80u);

    // Whole: bytes of the twin's, which read back into a heap with every page, none of which it gives back (it
    // can't tell which of their blocks are free)
    uint32_t size = 0;
    uint8_t *bytes = packed_heaps(&world_heaps, &size);
    tide_heaps back = {0};
    tide_reader r = {bytes, size, 0, false};
    TIDE_REQUIRE(tide_heaps_unpack(&back, &r) && r.at == size);
    TIDE_CHECK(tide_heaps_hash(1, &back) == tide_heaps_hash(1, &twin) && tide_heaps_gone(&back) == 0);
    tide_heaps_settle(&back);
    TIDE_CHECK(tide_heaps_gone(&back) == 0 && tide_heaps_pages(&back) == tide_heaps_pages(&twin));
    // ...and hands out the twin's blocks, and gives back the pages it makes from then on
    const uint64_t next = tide_heap_alloc(tide_heaps_of(&back, 0u), BYTES);
    tide_heaps twin_next = {0};
    tide_heaps_copy(&twin_next, &twin);
    TIDE_CHECK(next == tide_heap_alloc(tide_heaps_of(&twin_next, 0u), BYTES));
    tide_heaps_free(&twin_next);
    free(bytes);
    tide_heaps_free(&back);

    // The pages' hashes one by one, as a machine says what it has
    uint8_t ours[16384], theirs[16384];
    tide_writer wa = {ours, sizeof ours, 0, false, false}, wb = {theirs, sizeof theirs, 0, false, false};
    tide_heaps_hash_pages(&world_heaps, &wa);
    tide_heaps_hash_pages(&twin, &wb);
    TIDE_CHECK(!wa.overflow && wa.size == wb.size && memcmp(ours, theirs, wa.size) == 0);

    // Deltas: from a base with the pages gone and from the twin's with them there, read over either
    tide_heaps base = {0}, twin_base = {0};
    tide_heaps_copy(&base, &world_heaps);
    tide_heaps_copy(&twin_base, &twin);
    TIDE_CHECK(delta_reads(&world_heaps, &base, &base) && delta_reads(&world_heaps, &base, &twin_base));
    for (uint32_t i = 40; i < 120; i++) {
        at[i] = tide_heap_alloc(tide_heaps_of(&world_heaps, 0u), BYTES);
        TIDE_REQUIRE(at[i] && tide_heap_alloc(tide_heaps_of(&twin, 0u), BYTES) == at[i]);
        fill_block(&world_heaps, at[i], BYTES, i + 500u);
        fill_block(&twin, at[i], BYTES, i + 500u);
    }
    for (uint32_t i = 0; i < 20; i++) {
        tide_heap_release(tide_heaps_of(&world_heaps, 0u), at[i]);
        tide_heap_release(tide_heaps_of(&twin, 0u), at[i]);
    }
    tide_heaps_flush(&world_heaps);
    tide_heaps_flush(&twin);
    tide_heaps_settle(&world_heaps);
    TIDE_CHECK(tide_heaps_gone(&world_heaps) > 0 && tide_heaps_gone(&world_heaps) != tide_heaps_gone(&base));
    TIDE_CHECK(tide_heaps_hash(1, &world_heaps) == tide_heaps_hash(1, &twin));
    TIDE_CHECK(delta_reads(&world_heaps, &base, &base));
    TIDE_CHECK(delta_reads(&world_heaps, &base, &twin_base));
    TIDE_CHECK(delta_reads(&world_heaps, &twin_base, &base));
    TIDE_CHECK(delta_reads(&twin, &twin_base, &base));
    TIDE_CHECK(delta_reads(&twin, &base, &twin_base));
    // And back the other way: the base from what the world became
    TIDE_CHECK(delta_reads(&base, &world_heaps, &twin) && delta_reads(&twin_base, &world_heaps, &world_heaps));
    tide_heaps_free(&base);
    tide_heaps_free(&twin_base);
    tide_heaps_free(&world_heaps);
    tide_heaps_free(&twin);
}

TIDE_TEST(heap_giving_back_is_the_twin_through_any_blocks_coming_and_going)
{
    enum { MOST = 1500, STEPS = 30000, SNAPSHOTS = 4 };
    static const uint32_t sizes[] = {24, 200, 1000, 4000, 4000, 4000, 9000, 40000};
    tide_heaps world_heaps = {0}, twin = {0};
    static uint64_t at[MOST];
    static uint32_t bytes_of[MOST];
    uint32_t held = 0;
    tide_heaps snapshots[SNAPSHOTS] = {{0}};
    uint64_t snapshot_hashes[SNAPSHOTS] = {0};
    uint32_t state = 12345u, gave = 0, back = 0, most_gone = 0;
    for (uint32_t step = 0; step < STEPS; step++) {
        state = state * 1664525u + 1013904223u;
        const uint32_t roll = state >> 8;
        // Growing, then shrinking, in long waves: a world that fills and empties
        const bool grow = held == 0 || (held < MOST && ((step / 3000u) & 1u ? roll % 10u < 3u : roll % 10u < 7u));
        if (grow) {
            const uint32_t bytes = sizes[(roll >> 4) % (sizeof sizes / sizeof sizes[0])];
            const uint32_t gone_was = tide_heaps_gone(&world_heaps);
            at[held] = tide_heap_alloc(tide_heaps_of(&world_heaps, 0u), bytes);
            TIDE_REQUIRE(at[held] && tide_heap_alloc(tide_heaps_of(&twin, 0u), bytes) == at[held]);
            back += tide_heaps_gone(&world_heaps) < gone_was;
            const uint32_t marked = bytes < 64u ? bytes : 64u; // Its start says which it is; the rest stays zero
            TIDE_CHECK(block_zero(&world_heaps, at[held], bytes));
            fill_block(&world_heaps, at[held], marked, step);
            fill_block(&twin, at[held], marked, step);
            bytes_of[held++] = step;
        } else {
            const uint32_t which = (roll >> 4) % held;
            TIDE_CHECK(block_filled(&world_heaps, at[which], 24, bytes_of[which]));
            tide_heap_release(tide_heaps_of(&world_heaps, 0u), at[which]);
            tide_heap_release(tide_heaps_of(&twin, 0u), at[which]);
            at[which] = at[--held];
            bytes_of[which] = bytes_of[held];
        }
        if (step % 5u == 4u) { // A tick's end
            tide_heaps_flush(&world_heaps);
            tide_heaps_flush(&twin);
            const uint32_t gone_was = tide_heaps_gone(&world_heaps);
            tide_heaps_settle(&world_heaps);
            gave += tide_heaps_gone(&world_heaps) > gone_was;
            if (tide_heaps_gone(&world_heaps) > most_gone) most_gone = tide_heaps_gone(&world_heaps);
        }
        if (step % 250u == 249u) {
            TIDE_REQUIRE(tide_heaps_hash(1, &world_heaps) == tide_heaps_hash(1, &twin));
            TIDE_CHECK(tide_heaps_used(&world_heaps) == tide_heaps_used(&twin));
            // A snapshot, kept a while: it's what it was when the world has given back and taken pages since
            const uint32_t slot = (step / 250u) % SNAPSHOTS;
            if (snapshot_hashes[slot]) TIDE_CHECK(tide_heaps_hash(1, &snapshots[slot]) == snapshot_hashes[slot]);
            tide_heaps_copy(&snapshots[slot], &world_heaps);
            snapshot_hashes[slot] = tide_heaps_hash(1, &world_heaps);
        }
        if (step % 2500u == 2499u) TIDE_CHECK(same_packed(&world_heaps, &twin) && delta_reads(&world_heaps, &snapshots[0], &snapshots[0]));
    }
    TIDE_CHECK(gave > 5u && back > 5u && most_gone > 20u);
    for (uint32_t i = 0; i < held; i++) TIDE_CHECK(block_filled(&world_heaps, at[i], 24, bytes_of[i]));
    for (uint32_t i = 0; i < SNAPSHOTS; i++) {
        TIDE_CHECK(tide_heaps_hash(1, &snapshots[i]) == snapshot_hashes[i]);
        tide_heaps_free(&snapshots[i]);
    }
    tide_heaps_free(&world_heaps);
    tide_heaps_free(&twin);
}
