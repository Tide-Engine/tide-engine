#include <stdlib.h>
#include <string.h>

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
