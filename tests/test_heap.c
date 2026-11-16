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

// Where a heap's first block is: place 0 is no block's
#define HEAP_FIRST (UINT64_C(1) << TIDE_HEAP_PAGE_SHIFT)

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
        for (uint32_t i = 1; i < tide_heaps_of(&world.heap, 0u)->pages; i++) TIDE_REQUIRE(tide_heaps_of(&world.heap, 0u)->page[i] != NULL);
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
    TIDE_CHECK(tide_heaps_of(&snapshot.heap, 0u)->page[1] == tide_heaps_of(&world.heap, 0u)->page[1]);
    TIDE_CHECK(tide_heaps_hash(1, &snapshot.heap) == tide_heaps_hash(1, &world.heap));

    tide_text_set(&world.name, text("dog"), TIDE_IN_MATCH);
    tide_heaps_flush(&world.heap);
    TIDE_CHECK(tide_heaps_of(&snapshot.heap, 0u)->page[1] != tide_heaps_of(&world.heap, 0u)->page[1]);
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
    TIDE_CHECK(tide_handle_world(world.name.at) == TIDE_IN_MATCH && tide_handle_offset(world.name.at) == HEAP_FIRST);
    TIDE_CHECK(tide_handle_offset(world.title.at) == HEAP_FIRST); // Each heap hands its own blocks out from its start
    TIDE_CHECK(tide_heaps_of(&world.heap, 0u)->pages == 0 && tide_heaps_used(&world.heap) > 0);
    TIDE_CHECK(tide_heaps_of(&world.heap, 7u) != tide_heaps_of(&world.heap, 900u) && tide_heaps_of(&world.heap, 0u) == tide_heaps_of(&world.heap, 0u));
    TIDE_CHECK(tide_heaps_of(&world.heap, 7u)->pages != 0 && tide_heaps_of(&world.heap, 900u)->pages != 0);
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
    const uint64_t nines = tide_heap_hash(0, tide_heaps_of(&world.heap, 900u));
    for (int i = 0; i < 100; i++) tide_text_set(&world.name, text(i % 2 ? "a longer cat than before" : "cat"), IN_SEVEN);
    tide_heaps_flush(&world.heap);
    TIDE_CHECK(tide_heap_hash(0, tide_heaps_of(&world.heap, 900u)) == nines && reads(world.title, "captain"));
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
    TIDE_CHECK(tide_heaps_pages_apart(&back.heap, &back_base.heap) == 2u); // Nine's place it left and its new one
    TIDE_CHECK(tide_heaps_of(&back.heap, 7u)->page[1] == tide_heaps_of(&back_base.heap, 7u)->page[1]); // Seven's is the base's

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
    TIDE_CHECK(tide_handle_where(world.name.at) == SEVEN_RUN(3) && tide_handle_offset(world.name.at) == HEAP_FIRST);
    TIDE_CHECK(tide_handle_in(world.name.at, IN_SEVEN) && !tide_handle_in(world.name.at, IN_NINE) && !tide_handle_in(world.name.at, TIDE_IN_MATCH));
    TIDE_CHECK(seven(0)->pages == 0 && seven(3)->pages != 0 && seven(3)->run == 3u && seven(3)->base == seven(0));
    tide_text_set(&world.title, text("dog"), IN_SEVEN);
    TIDE_CHECK(tide_handle_offset(world.title.at) == HEAP_FIRST && world.title.at != world.name.at);
    TIDE_CHECK(reads(world.name, "cat") && reads(world.title, "dog"));
    const tide_handle threes = world.name.at;

    // Another run's row lets go of it: run 3's heap is as it was, and run 5's keeps the block, readable until
    // the row is done, zeroed then, and back in run 3's heap when the runs join
    was = tide_heap_run_enter(5u);
    const tide_text old = world.name;
    tide_text_set(&world.name, text("a bird"), TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u));
    TIDE_CHECK(tide_handle_where(world.name.at) == SEVEN_RUN(5) && reads(world.name, "a bird") && reads(old, "cat"));
    TIDE_CHECK(seven(3)->pending == 0 && seven(3)->taken == 32u && seven(5)->away != 0 && seven(5)->held == 0);
    tide_heap_flush(seven(5));
    TIDE_CHECK(seven(5)->away == 0 && seven(5)->held != 0 && seven(3)->taken == 32u);
    TIDE_CHECK(tide_text_view(old).bytes == 0); // Freed memory is zero
    tide_heap_run_leave(was);
    tide_heap_runs_join(seven(0));
    TIDE_CHECK(seven(5)->held == 0 && seven(3)->taken == 0 && seven(3)->pages == 0); // Its page went with its one block
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
    TIDE_CHECK(!tide_heaps_pending(&world.heap) && seven(3)->pages == 0 && seven(5)->pages == 0 && seven(0)->taken == 32u);
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
    const uint64_t ones = seven(1)->most;

    // Written and cut where it is from another run, and added to within its room
    was = tide_heap_run_enter(2u);
    const tide_handle at = l.at;
    *(int32_t *)tide_list_at_mut(l, 10, sizeof(int32_t)) = -10;
    tide_list_resize(&l, 5990, sizeof(int32_t), TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u));
    *(int32_t *)tide_list_add(&l, sizeof(int32_t), TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u)) = 5990;
    TIDE_CHECK(l.at == at && seven(2)->pages == 0);
    // More chunks: they'd be run 2's, which a block in run 1's heap can't list. The list moves there whole
    for (int32_t i = 5991; i < 12000; i++) *(int32_t *)tide_list_add(&l, sizeof(int32_t), TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u)) = i;
    TIDE_CHECK(tide_handle_where(l.at) == SEVEN_RUN(2) && seven(1)->most == ones && seven(2)->away != 0);
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
    TIDE_CHECK(seven(1)->most == ones && tide_list_count(second) == 6000);
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
    const uint64_t ones = seven(1)->most;

    // From another run: cells of its chunks written where they are, and zero where it has none makes none
    was = tide_heap_run_enter(2u);
    const tide_handle at = g.at;
    grid_put(&g, 1, 3, 15, TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u));
    grid_put(&g, 500, 500, 0, TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u));
    TIDE_CHECK(g.at == at && seven(2)->pages == 0 && grid_cell(g, 1, 3) == 15 && tide_grid_dir_of(g)->records == 3u);
    // ...through a cache too, which holds the grid's own heap
    tide_grid_cache cache = {0};
    *(int32_t *)tide_grid_cached_poke(&cache, &g, 2, 3, 0, &grid_ints, true, TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u)) = 16;
    TIDE_CHECK(g.at == at && cache.heap == seven(1) && cache.write != NULL);
    // A chunk it hasn't: run 2's rows make blocks in run 2's heap alone, and a directory names its chunks by their
    // offsets in its own, so the grid moves there whole, every cell with it. Run 1's blocks wait for the join
    grid_put(&g, 200, 200, 17, TIDE_WHERE_RUN(TIDE_IN_MATCH, 7u));
    TIDE_CHECK(tide_handle_where(g.at) == SEVEN_RUN(2) && seven(1)->most == ones && seven(2)->away != 0);
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
    TIDE_CHECK(seven(1)->most == ones && grid_cell(again, -5, -70) == 1);

    // Outside runs, a chunk it hasn't is made in its own heap, whichever run's that is
    const tide_handle twos = g.at;
    grid_put(&g, -300, 0, 18, IN_SEVEN);
    TIDE_CHECK(g.at == twos && grid_cell(g, -300, 0) == 18 && seven(0)->pages == 0);

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
    tide_page *shared = seven(0)->page[1];
    const uint32_t refs = shared->refs;
    TIDE_CHECK(refs == 2u);

    // A run writes a block of the page: the heap gets a copy, and the page keeps the heap's reference, so a
    // thread that read it from the table a moment ago still sees a page that isn't the heap's to write
    const uint32_t was = tide_heap_run_enter(4u);
    tide_block_write(world.name.at)->b = 3u;
    TIDE_CHECK(seven(0)->page[1] != shared && shared->refs == refs && seven(0)->retired_count == 1u);
    tide_block_write(world.title.at)->b = 7u; // The copy is the heap's own: written where it is
    TIDE_CHECK(seven(0)->retired_count == 1u && seven(0)->page[1]->refs == 1u);
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
    TIDE_CHECK(tide_heaps_pages_apart(&back.heap, &back_base.heap) == 2u); // Run 6's place it left and its new one
    TIDE_CHECK(tide_heaps_of(&back.heap, 900u)->page[1] == tide_heaps_of(&back_base.heap, 900u)->page[1]); // Nine's is the base's
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
    TIDE_CHECK(tide_heap_hash(0, tide_heaps_of(&kept.heap, 7u)) == tide_heap_hash(0, tide_heaps_of(&world.heap, 7u)));
    TIDE_CHECK(tide_heaps_of(&kept.heap, 900u)->pages == 0 && tide_heaps_pages(&kept.heap) == 1u);
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
// Places (tide/heap.h): what a heap hands out goes in the lowest places that
// have room, so one that held much and holds little is small again, and where
// it puts a block depends on what it holds alone, so a copy, its bytes and a
// delta of it go on as it does.

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

// `hs` read back from its bytes into `back` (empty).
static bool unpacks(const tide_heaps *hs, tide_heaps *back)
{
    uint32_t size = 0;
    uint8_t *bytes = packed_heaps(hs, &size);
    tide_reader r = {bytes, size, 0, false};
    const bool ok = tide_heaps_unpack(back, &r) && r.at == size;
    free(bytes);
    return ok;
}

// A delta of `now` from `base`, read back over `read_base` (the same heaps as `base`) into `back` (empty).
static bool delta_into(const tide_heaps *now, const tide_heaps *base, const tide_heaps *read_base, tide_heaps *back)
{
    tide_delta_writer d;
    tide_delta_begin(&d, base != NULL, NULL, 0, 42);
    tide_heaps_pack_delta(now, base, &d);
    uint32_t size = 0;
    uint8_t *delta = tide_delta_end(&d, &size);
    tide_delta_reader dr;
    uint64_t hash = 0;
    const bool ok = tide_delta_open(&dr, delta, size, base != NULL, &hash) && tide_heaps_unpack_delta(back, read_base, &dr);
    free(delta);
    return ok;
}

// A block's first bytes say which block it is.
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

static tide_heap *first_heap(tide_heaps *hs)
{
    return tide_heaps_of(hs, 0u);
}

static const uint32_t mixed_sizes[] = {24, 100, 1000, 4000, 9000, 40000, 24, 200};
#define MIXED_SIZES (sizeof mixed_sizes / sizeof mixed_sizes[0])

TIDE_TEST(heap_that_held_much_and_holds_little_is_small_again)
{
    enum { BLOCKS = 3000, KEPT = 32 };
    tide_heaps hs = {0}, fresh = {0}, empty = {0};
    static uint64_t at[BLOCKS];
    for (uint32_t i = 0; i < BLOCKS; i++) {
        at[i] = tide_heap_alloc(first_heap(&hs), mixed_sizes[i % MIXED_SIZES]);
        fill_block(&hs, at[i], 24, i);
        if (i < KEPT) {
            TIDE_REQUIRE(tide_heap_alloc(first_heap(&fresh), mixed_sizes[i % MIXED_SIZES]) == at[i]);
            fill_block(&fresh, at[i], 24, i);
        }
    }
    const uint32_t most_pages = tide_heaps_pages(&hs), most_packed = tide_heaps_packed_size(&hs);
    TIDE_CHECK(most_pages > 1000u && most_packed > 1000u * 16384u);
    // A snapshot from then keeps every block
    tide_heaps before = {0};
    tide_heaps_copy(&before, &hs);
    const uint64_t before_hash = tide_heaps_hash(1, &before);

    // The ones made last let go of, the last first: what's left is what a heap that only ever had those has
    for (uint32_t i = BLOCKS; i-- > KEPT;) tide_heap_release(first_heap(&hs), at[i]);
    TIDE_CHECK(tide_heaps_pages(&hs) == most_pages); // Released, not yet freed
    tide_heaps_flush(&hs);
    tide_heaps_settle(&hs);
    TIDE_CHECK(tide_heaps_pages(&hs) == tide_heaps_pages(&fresh) && tide_heaps_gone(&hs) == 0);
    TIDE_CHECK(tide_heaps_hash(1, &hs) == tide_heaps_hash(1, &fresh) && same_packed(&hs, &fresh));
    TIDE_CHECK(tide_heaps_packed_size(&hs) < most_packed / 20u);
    for (uint32_t i = 0; i < KEPT; i++) TIDE_CHECK(block_filled(&hs, at[i], 24, i));
    TIDE_CHECK(tide_heaps_hash(1, &before) == before_hash);
    for (uint32_t i = 0; i < BLOCKS; i += 97u) TIDE_CHECK(block_filled(&before, at[i], 24, i));

    // Taken again in another order, and let go of in any: the pages left are the few the kept blocks are in
    for (uint32_t i = KEPT; i < BLOCKS; i++) {
        at[i] = tide_heap_alloc(first_heap(&hs), mixed_sizes[(i * 7u) % MIXED_SIZES]);
        TIDE_CHECK(block_zero(&hs, at[i], 24));
    }
    for (uint32_t i = KEPT; i < BLOCKS; i++) tide_heap_release(first_heap(&hs), at[(i * 1543u) % (BLOCKS - KEPT) + KEPT]);
    tide_heaps_flush(&hs);
    TIDE_CHECK(tide_heaps_pages(&hs) == tide_heaps_pages(&fresh));
    TIDE_CHECK(tide_heaps_packed_size(&hs) < most_packed / 20u);
    for (uint32_t i = 0; i < KEPT; i++) TIDE_CHECK(block_filled(&hs, at[i], 24, i));

    // Everything let go of: an empty heap, as one that never had anything
    for (uint32_t i = 0; i < KEPT; i++) tide_heap_release(first_heap(&hs), at[i]);
    tide_heaps_flush(&hs);
    tide_heaps_settle(&hs);
    (void)first_heap(&empty);
    TIDE_CHECK(first_heap(&hs)->pages == 0 && tide_heaps_pages(&hs) == 0 && first_heap(&hs)->taken == 0);
    TIDE_CHECK(tide_heaps_hash(1, &hs) == tide_heaps_hash(1, &empty) && same_packed(&hs, &empty));
    TIDE_CHECK(tide_heaps_hash(1, &before) == before_hash && block_filled(&before, at[0], 24, 0));
    // ...which hands out from its start again
    TIDE_CHECK(tide_heap_alloc(first_heap(&hs), 24) == HEAP_FIRST);
    tide_heaps_free(&before);
    tide_heaps_free(&hs);
    tide_heaps_free(&fresh);
    tide_heaps_free(&empty);
}

TIDE_TEST(heap_read_from_bytes_gives_back_the_pages_it_came_with)
{
    enum { BLOCKS = 2000 };
    tide_heaps hs = {0}, back = {0}, from_delta = {0};
    static uint64_t at[BLOCKS];
    for (uint32_t i = 0; i < BLOCKS; i++) {
        at[i] = tide_heap_alloc(first_heap(&hs), mixed_sizes[i % MIXED_SIZES]);
        fill_block(&hs, at[i], 24, i);
    }
    TIDE_REQUIRE(unpacks(&hs, &back) && delta_into(&hs, NULL, NULL, &from_delta));
    TIDE_CHECK(tide_heaps_hash(1, &back) == tide_heaps_hash(1, &hs) && tide_heaps_hash(1, &from_delta) == tide_heaps_hash(1, &hs));
    TIDE_CHECK(tide_heaps_used(&back) == tide_heaps_used(&hs));
    tide_heaps *all[] = {&hs, &back, &from_delta};
    for (uint32_t k = 0; k < 3; k++) {
        for (uint32_t i = 10; i < BLOCKS; i++) tide_heap_release(first_heap(all[k]), at[i]);
        tide_heaps_flush(all[k]);
        tide_heaps_settle(all[k]);
    }
    TIDE_CHECK(tide_heaps_pages(&hs) < 40u);
    for (uint32_t k = 1; k < 3; k++) {
        TIDE_CHECK(tide_heaps_pages(all[k]) == tide_heaps_pages(&hs) && same_packed(all[k], &hs));
        TIDE_CHECK(tide_heaps_hash(1, all[k]) == tide_heaps_hash(1, &hs));
        for (uint32_t i = 0; i < 10; i++) TIDE_CHECK(block_filled(all[k], at[i], 24, i));
    }
    const uint64_t next = tide_heap_alloc(first_heap(&hs), 4000);
    TIDE_CHECK(tide_heap_alloc(first_heap(&back), 4000) == next && tide_heap_alloc(first_heap(&from_delta), 4000) == next);
    tide_heaps_free(&hs);
    tide_heaps_free(&back);
    tide_heaps_free(&from_delta);
}

// The size class of a block with room for `bytes`.
static uint32_t class_of(const uint32_t bytes)
{
    uint32_t c = 0;
    while (((uint64_t)16 << c) < (uint64_t)bytes + sizeof(tide_block)) c++;
    return c;
}

// The block the policy says one of `bytes` is, worked out from what's at each place: in the lowest page of its
// class with one to spare (the last of its blocks freed, else the next it hasn't made), else in the lowest free
// place (the lowest run of them for a block of several), else at the end.
static uint64_t expected_block(const tide_heap *h, const uint32_t bytes)
{
    const uint32_t c = class_of(bytes);
    const uint32_t end = h->pages ? h->pages : 1u;
    if (c >= TIDE_HEAP_SMALL) {
        const uint32_t count = 1u << (c - TIDE_HEAP_SMALL);
        for (uint32_t i = 1; i < h->pages; i++) {
            uint32_t k = 0;
            while (k < count && i + k < h->pages && h->place[i + k].kind == TIDE_PLACE_FREE) k++;
            if (k == count) return (uint64_t)i << TIDE_HEAP_PAGE_SHIFT;
        }
        return (uint64_t)end << TIDE_HEAP_PAGE_SHIFT;
    }
    for (uint32_t i = 1; i < h->pages; i++) {
        const tide_heap_place pl = h->place[i];
        if (pl.kind != 1u + c || pl.out == 1u << (TIDE_HEAP_PAGE_SHIFT - c - 4u)) continue;
        const uint32_t index = pl.free ? pl.free - 1u : pl.made;
        return (uint64_t)i << TIDE_HEAP_PAGE_SHIFT | (uint64_t)index << (c + 4u);
    }
    for (uint32_t i = 1; i < h->pages; i++) {
        if (h->place[i].kind == TIDE_PLACE_FREE) return (uint64_t)i << TIDE_HEAP_PAGE_SHIFT;
    }
    return (uint64_t)end << TIDE_HEAP_PAGE_SHIFT;
}

// What holds of any heap once its blocks are freed: no page of a class's without a block out, the last place
// not free, a page at every place in use and none at a free one, and what's handed out the bytes of the blocks
// held.
static bool heap_holds_together(const tide_heap *h, const uint64_t taken)
{
    if (h->taken != taken || h->pages == 1u || (h->pages && h->place[h->pages - 1u].kind == TIDE_PLACE_FREE)) return false;
    for (uint32_t i = 1; i < h->pages; i++) {
        const tide_heap_place pl = h->place[i];
        if (pl.kind - 1u < TIDE_HEAP_SMALL && !pl.out) return false;
        if ((pl.kind == TIDE_PLACE_FREE) != (h->page[i] == NULL)) return false;
    }
    return true;
}

TIDE_TEST(heap_hands_out_the_lowest_places_and_its_copies_go_on_the_same)
{
    enum { MOST = 1500, STEPS = 24000, TWINS = 3 };
    static const uint32_t sizes[] = {0, 24, 200, 1000, 4000, 4000, 9000, 16368, 40000, 100000};
    tide_heaps hs = {0}, twins[TWINS] = {{0}}, kept = {0};
    static uint64_t at[MOST];
    static uint32_t which[MOST], size_of[MOST];
    uint32_t held = 0, state = 777u, most_pages = 0, least_after = UINT32_MAX;
    uint64_t taken = 0, kept_hash = 0;
    bool twinned = false;
    for (uint32_t step = 0; step < STEPS; step++) {
        state = state * 1664525u + 1013904223u;
        const uint32_t roll = state >> 8;
        // Waves: it fills, empties nearly, and fills again
        const bool grow = held == 0 || (held < MOST && ((step / 4000u) & 1u ? roll % 10u < 3u : roll % 10u < 7u));
        if (grow) {
            const uint32_t bytes = sizes[(roll >> 4) % (sizeof sizes / sizeof sizes[0])];
            const uint64_t expected = expected_block(first_heap(&hs), bytes);
            at[held] = tide_heap_alloc(first_heap(&hs), bytes);
            TIDE_REQUIRE(at[held] == expected);
            for (uint32_t k = 0; twinned && k < TWINS; k++) TIDE_REQUIRE(tide_heap_alloc(first_heap(&twins[k]), bytes) == at[held]);
            TIDE_CHECK(block_zero(&hs, at[held], bytes));
            const uint32_t marked = bytes < 24u ? bytes : 24u;
            fill_block(&hs, at[held], marked, step);
            for (uint32_t k = 0; twinned && k < TWINS; k++) fill_block(&twins[k], at[held], marked, step);
            size_of[held] = (uint32_t)16 << class_of(bytes);
            taken += size_of[held];
            which[held++] = step;
        } else {
            const uint32_t i = (roll >> 4) % held;
            TIDE_CHECK(block_filled(&hs, at[i], size_of[i] > 40u ? 24u : 0u, which[i]));
            tide_heap_release(first_heap(&hs), at[i]);
            for (uint32_t k = 0; twinned && k < TWINS; k++) tide_heap_release(first_heap(&twins[k]), at[i]);
            taken -= size_of[i];
            at[i] = at[--held];
            which[i] = which[held];
            size_of[i] = size_of[held];
        }
        if (step % 5u == 4u) { // A tick's end
            tide_heaps_flush(&hs);
            tide_heaps_settle(&hs);
            for (uint32_t k = 0; twinned && k < TWINS; k++) {
                tide_heaps_flush(&twins[k]);
                tide_heaps_settle(&twins[k]);
            }
            TIDE_REQUIRE(heap_holds_together(first_heap(&hs), taken));
            if (tide_heaps_pages(&hs) > most_pages) most_pages = tide_heaps_pages(&hs);
            if (step > 6000u && (step / 4000u) & 1u && tide_heaps_pages(&hs) < least_after) least_after = tide_heaps_pages(&hs);
        }
        if (step % 1000u == 999u) {
            // Its twins again: a snapshot, its bytes read back, and a delta from the snapshot kept last read over
            // that snapshot's bytes. Each goes on as it does
            const uint64_t hash = tide_heaps_hash(1, &hs);
            for (uint32_t k = 0; twinned && k < TWINS; k++) {
                TIDE_CHECK(tide_heaps_hash(1, &twins[k]) == hash && same_packed(&twins[k], &hs));
            }
            if (kept_hash) TIDE_CHECK(tide_heaps_hash(1, &kept) == kept_hash);
            tide_heaps kept_back = {0};
            TIDE_REQUIRE(!kept_hash || unpacks(&kept, &kept_back));
            for (uint32_t k = 0; k < TWINS; k++) tide_heaps_free(&twins[k]);
            tide_heaps_copy(&twins[0], &hs);
            TIDE_REQUIRE(unpacks(&hs, &twins[1]));
            TIDE_REQUIRE(delta_into(&hs, kept_hash ? &kept : NULL, kept_hash ? &kept_back : NULL, &twins[2]));
            tide_heaps_free(&kept_back);
            for (uint32_t k = 0; k < TWINS; k++) TIDE_REQUIRE(tide_heaps_hash(1, &twins[k]) == hash);
            tide_heaps_copy(&kept, &hs);
            kept_hash = hash;
            twinned = true;
        }
    }
    // It grew, and gave back most of it when it held little
    TIDE_CHECK(most_pages > 300u && least_after < most_pages / 4u);
    for (uint32_t i = 0; i < held; i++) TIDE_CHECK(block_filled(&hs, at[i], size_of[i] > 40u ? 24u : 0u, which[i]));
    TIDE_CHECK(tide_heaps_hash(1, &kept) == kept_hash);
    for (uint32_t k = 0; k < TWINS; k++) tide_heaps_free(&twins[k]);
    tide_heaps_free(&kept);
    tide_heaps_free(&hs);
}

// The bytes of a page in a heap's packed bytes: a class's page's free blocks as their numbers, then the blocks out.
static uint32_t page_packed(const tide_heap *h, const uint32_t at)
{
    const tide_heap_place pl = h->place[at];
    if (pl.kind - 1u >= TIDE_HEAP_SMALL) return tide_heap_page_used(h, h->page[at]);
    return (uint32_t)(pl.made - pl.out) * 2u + ((uint32_t)pl.out << (pl.kind + 3u));
}

// One heap's bytes as tide_heap_pack has them, and where its places start in them.
static uint8_t *one_heap_bytes(const tide_heap *h, uint32_t *size, uint32_t *places_at)
{
    *size = tide_heap_packed_size(h);
    uint8_t *bytes = malloc(*size);
    tide_writer w = {bytes, *size, 0, false, false};
    tide_heap_pack(h, &w);
    TIDE_CHECK(!w.overflow && w.size == *size);
    uint32_t pages_bytes = 0;
    for (uint32_t i = 1; i < h->pages; i++) {
        if (h->page[i] && h->page[i]->first == i) pages_bytes += page_packed(h, i);
    }
    *places_at = *size - pages_bytes - h->pages * (uint32_t)sizeof(tide_heap_place);
    return bytes;
}

static bool one_heap_unpacks(const uint8_t *bytes, const uint32_t size)
{
    tide_heap h = {0};
    tide_reader r = {bytes, size, 0, false};
    const bool ok = tide_heap_unpack(&h, &r) && r.at == size;
    if (ok) { // Whatever it took for a heap is one: blocks come and go in it
        uint64_t got[64];
        for (uint32_t i = 0; i < 64; i++) got[i] = tide_heap_alloc(&h, (i * 97u) % 5000u);
        for (uint32_t i = 0; i < 64; i += 2) tide_heap_release(&h, got[i]);
        tide_heap_flush(&h);
        tide_heap_settle(&h);
    }
    tide_heap_free(&h);
    return ok;
}

TIDE_TEST(heap_bytes_that_arent_a_heap_are_refused)
{
    tide_heaps hs = {0};
    tide_heap *h = first_heap(&hs);
    static uint64_t at[400];
    for (uint32_t i = 0; i < 400; i++) at[i] = tide_heap_alloc(h, mixed_sizes[i % MIXED_SIZES]);
    for (uint32_t i = 0; i < 399; i += 3) tide_heap_release(h, at[i]); // Free blocks in pages that stay
    tide_heap_flush(h);
    tide_heap_release(h, at[1]); // And two waiting to be freed
    tide_heap_release(h, at[4]);
    uint32_t size = 0, places_at = 0;
    uint8_t *bytes = one_heap_bytes(h, &size, &places_at);
    TIDE_REQUIRE(one_heap_unpacks(bytes, size));
    uint8_t *spoilt = malloc(size);
    const uint32_t pages = h->pages;
    tide_heap_place *places = (tide_heap_place *)(void *)(spoilt + places_at);
    // A class's page with free blocks, and where its bytes are
    uint32_t paged = 0, bytes_at = places_at + pages * (uint32_t)sizeof(tide_heap_place), page_bytes_at = 0;
    for (uint32_t i = 1; i < pages; i++) {
        if (!h->page[i] || h->page[i]->first != i) continue;
        if (!paged && h->place[i].kind - 1u < TIDE_HEAP_SMALL && h->place[i].free) {
            paged = i;
            page_bytes_at = bytes_at;
        }
        bytes_at += page_packed(h, i);
    }
    TIDE_REQUIRE(paged && bytes_at == size);
    const tide_heap_place was = h->place[paged];
    uint32_t refused = 0, tried = 0;
#define SPOIL(change)                                                                                                  \
    do {                                                                                                               \
        memcpy(spoilt, bytes, size);                                                                                   \
        change;                                                                                                        \
        tried++;                                                                                                       \
        refused += !one_heap_unpacks(spoilt, size);                                                                    \
    } while (0)
    SPOIL(memcpy(spoilt + places_at - 4u, &(uint32_t){1u}, 4u));          // A heap of place 0 alone
    SPOIL(memcpy(spoilt + places_at - 4u, &(uint32_t){UINT32_MAX}, 4u)); // More places than bytes
    SPOIL(memcpy(spoilt + places_at - 4u, &(uint32_t){pages + 1u}, 4u)); // ...or one more
    SPOIL(places[0].kind = 1u);                                            // Place 0 a page
    SPOIL(places[paged].kind = (uint8_t)(TIDE_HEAP_SMALL + 1u));           // No kind
    SPOIL(places[paged].kind = 14u);
    SPOIL(places[paged].kind = TIDE_PLACE_MORE); // Another's place, alone
    SPOIL(places[paged].kind = 255u);
    SPOIL(places[paged].out = (uint16_t)(was.made + 1u)); // More out than made
    SPOIL(places[paged].out = 0u);                        // A page with none out
    SPOIL(places[paged].made = (uint16_t)((1u << TIDE_HEAP_PAGE_SHIFT) / (16u << (was.kind - 1u)) + 1u));
    SPOIL(places[paged].free = (uint16_t)(was.made + 1u)); // A free block past what's made
    SPOIL(places[paged].free = 0u);                        // Free blocks with no list
    SPOIL(places[paged].shift = 1u);
    SPOIL(places[pages - 1u] = (tide_heap_place){0});                         // The last place free
    const tide_heap_place past = {TIDE_PLACE_BLOCK, 12u, 0, 0, 0};
    SPOIL(places[paged] = past); // A block past the heap's end
    // Its free blocks' numbers: none, one past what's made, another first than its place's, one twice
    TIDE_REQUIRE(was.made - was.out >= 2u);
    uint16_t second = 0;
    memcpy(&second, bytes + page_bytes_at + 2u, 2u);
    SPOIL(memcpy(spoilt + page_bytes_at, &(uint16_t){0}, 2u));
    SPOIL(memcpy(spoilt + page_bytes_at, &(uint16_t){(uint16_t)(was.made + 1u)}, 2u));
    SPOIL(memcpy(spoilt + page_bytes_at, &second, 2u));
    SPOIL(memcpy(spoilt + page_bytes_at + 2u, &(uint16_t){was.free}, 2u));
    // The blocks waiting to be freed: one past the heap's places, one not where a block starts, one that's free
    SPOIL(memcpy(spoilt + places_at - 12u, &(uint64_t){(uint64_t)pages << TIDE_HEAP_PAGE_SHIFT}, 8u));
    SPOIL(memcpy(spoilt + places_at - 12u, &(uint64_t){at[4] + 16u}, 8u));
    SPOIL(memcpy(spoilt + places_at - 12u, &(uint64_t){((uint64_t)paged << TIDE_HEAP_PAGE_SHIFT) + ((uint64_t)(was.free - 1u) << (was.kind + 3u))}, 8u));
    TIDE_CHECK(refused == tried);
    // Cut anywhere
    bool cut_refused = true;
    for (uint32_t cut = 0; cut < size; cut += cut < places_at + 64u ? 1u : 61u) {
        tide_heap cut_heap = {0};
        tide_reader r = {bytes, cut, 0, false};
        cut_refused &= !tide_heap_unpack(&cut_heap, &r);
        tide_heap_free(&cut_heap);
    }
    TIDE_CHECK(cut_refused);
    // Any bit at all: refused or a heap, and never anything else (ASan and the heap's own use say)
    uint32_t state = 99u;
    for (uint32_t i = 0; i < 3000; i++) {
        memcpy(spoilt, bytes, size);
        state = state * 1664525u + 1013904223u;
        const uint32_t where = i % 2u ? places_at + (state >> 8) % (pages * (uint32_t)sizeof(tide_heap_place)) : (state >> 8) % size;
        spoilt[where] ^= (uint8_t)(1u << (state >> 4 & 7u));
        (void)one_heap_unpacks(spoilt, size);
    }
    // And a delta from nothing, spoilt the same way
    tide_delta_writer d;
    tide_delta_begin(&d, false, NULL, 0, 42);
    tide_heaps_pack_delta(&hs, NULL, &d);
    uint32_t delta_size = 0;
    uint8_t *delta = tide_delta_end(&d, &delta_size);
    for (uint32_t i = 0; i < 3000; i++) {
        uint8_t *copy = malloc(delta_size);
        memcpy(copy, delta, delta_size);
        state = state * 1664525u + 1013904223u;
        copy[(state >> 8) % delta_size] ^= (uint8_t)(1u << (state >> 4 & 7u));
        tide_delta_reader dr;
        uint64_t hash = 0;
        tide_heaps back = {0};
        if (tide_delta_open(&dr, copy, delta_size, false, &hash) && tide_heaps_unpack_delta(&back, NULL, &dr)) {
            const uint64_t got = tide_heap_alloc(first_heap(&back), 100);
            tide_heap_release(first_heap(&back), got);
            tide_heaps_flush(&back);
        }
        tide_heaps_free(&back);
        free(copy);
    }
    free(delta);
#undef SPOIL
    free(spoilt);
    free(bytes);
    tide_heaps_free(&hs);
}
