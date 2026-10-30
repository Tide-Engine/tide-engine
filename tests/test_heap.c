#include <stdlib.h>
#include <string.h>

#include "tide/text.h"
#include "tide_test.h"

typedef struct fake_world {
    tide_text name;
    tide_text title;
    tide_heap heap;
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
    tide_heap_free(&world.heap);
    memset(&world, 0, sizeof world);
    tide_text_use(&world.heap, NULL);
}

TIDE_TEST(heap_blocks_are_reused_in_order)
{
    start();
    const uint64_t a = tide_heap_alloc(&world.heap, 10);
    const uint64_t b = tide_heap_alloc(&world.heap, 10);
    TIDE_CHECK(a != 0 && b != 0 && a != b);
    tide_heap_release(&world.heap, a);
    TIDE_CHECK(tide_heap_alloc(&world.heap, 10) != a); // Not until the flush
    tide_heap_flush(&world.heap);
    const uint64_t c = tide_heap_alloc(&world.heap, 10);
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
    TIDE_CHECK(world.heap.pending != 0);
    tide_heap_flush(&world.heap);
    TIDE_CHECK(world.heap.pending == 0);

    // A value copied into the world takes its own copy of borrowed text.
    world.title = tide_text_temp(text("fish"));
    tide_text_own(&world.title, TIDE_IN_MATCH);
    tide_scratch_reset(mark);
    TIDE_CHECK(reads(world.title, "fish"));

    tide_text_release(&world.title, TIDE_IN_MATCH);
    TIDE_CHECK(world.title.at == 0);
    tide_text_set(&world.name, TIDE_STR_EMPTY, TIDE_IN_MATCH);
    TIDE_CHECK(reads(world.name, ""));
    tide_heap_flush(&world.heap);
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
    TIDE_CHECK(world.heap.pages >= 5);
    for (int i = 0; i < 2000; i++) tide_heap_alloc(&world.heap, 100);
    TIDE_CHECK(tide_text_view(world.title).ptr[size - 1u] == 'x');
    free(big);
}

TIDE_TEST(heap_starts_with_a_block_of_a_page)
{
    for (uint32_t places = 1; places <= 2; places++) {
        start();
        const uint32_t bytes = (places << TIDE_HEAP_PAGE_SHIFT) - (uint32_t)sizeof(tide_block);
        const uint64_t block = tide_heap_alloc(&world.heap, bytes);
        TIDE_REQUIRE(block != 0);
        TIDE_CHECK(world.heap.pages == places + 1u);
        for (uint32_t i = 0; i < world.heap.pages; i++) TIDE_REQUIRE(world.heap.page[i] != NULL);
        uint8_t *data = (uint8_t *)(tide_heap_write(&world.heap, block) + 1);
        data[0] = 1;
        data[bytes - 1u] = 2;
        const uint64_t small = tide_heap_alloc(&world.heap, 10);
        TIDE_CHECK(small >= block + (places << TIDE_HEAP_PAGE_SHIFT));
        TIDE_REQUIRE(world.heap.page[world.heap.pages - 1u] != NULL);

        tide_heap copy = {0};
        tide_heap_copy(&copy, &world.heap);
        TIDE_CHECK(tide_heap_hash(1, &copy) == tide_heap_hash(1, &world.heap));
        tide_heap_free(&copy);

        const uint32_t size = tide_heap_packed_size(&world.heap);
        uint8_t *packed = malloc(size);
        tide_writer w = {packed, size, 0, false, false};
        tide_heap_pack(&world.heap, &w);
        TIDE_CHECK(!w.overflow && w.size == size);
        tide_heap back = {0};
        tide_reader r = {packed, size, 0, false};
        TIDE_REQUIRE(tide_heap_unpack(&back, &r));
        TIDE_CHECK(tide_heap_hash(1, &back) == tide_heap_hash(1, &world.heap));
        const uint8_t *read = (const uint8_t *)(tide_heap_block(&back, block) + 1);
        TIDE_CHECK(read[0] == 1 && read[bytes - 1u] == 2);
        free(packed);
        tide_heap_free(&back);

        tide_delta_writer d;
        tide_delta_begin(&d, false, NULL, 0, 42);
        tide_heap_pack_delta(&world.heap, NULL, &d);
        uint32_t delta_size = 0;
        uint8_t *delta = tide_delta_end(&d, &delta_size);
        tide_delta_reader dr;
        uint64_t hash = 0;
        TIDE_REQUIRE(tide_delta_open(&dr, delta, delta_size, false, &hash));
        TIDE_REQUIRE(tide_heap_unpack_delta(&back, NULL, &dr));
        TIDE_CHECK(tide_heap_hash(1, &back) == tide_heap_hash(1, &world.heap));
        free(delta);
        tide_heap_free(&back);
    }
}

TIDE_TEST(heap_snapshots_share_until_changed)
{
    start();
    tide_text_set(&world.name, text("cat"), TIDE_IN_MATCH);
    fake_world snapshot = {0};
    snapshot.name = world.name;
    tide_heap_copy(&snapshot.heap, &world.heap);
    TIDE_CHECK(snapshot.heap.page[0] == world.heap.page[0]);
    TIDE_CHECK(tide_heap_hash(1, &snapshot.heap) == tide_heap_hash(1, &world.heap));

    tide_text_set(&world.name, text("dog"), TIDE_IN_MATCH);
    tide_heap_flush(&world.heap);
    TIDE_CHECK(snapshot.heap.page[0] != world.heap.page[0]);
    TIDE_CHECK(strcmp(tide_text_read(&snapshot.heap, snapshot.name).ptr, "cat") == 0);
    TIDE_CHECK(strcmp(tide_text_read(&world.heap, world.name).ptr, "dog") == 0);
    TIDE_CHECK(tide_heap_hash(1, &snapshot.heap) != tide_heap_hash(1, &world.heap));

    const uint32_t size = tide_heap_packed_size(&world.heap);
    uint8_t *bytes = malloc(size);
    tide_writer w = {bytes, size, 0, false, false};
    tide_heap_pack(&world.heap, &w);
    TIDE_CHECK(!w.overflow && w.size == size);
    tide_heap back = {0};
    tide_reader r = {bytes, size, 0, false};
    TIDE_REQUIRE(tide_heap_unpack(&back, &r));
    TIDE_CHECK(tide_heap_hash(1, &back) == tide_heap_hash(1, &world.heap));
    TIDE_CHECK(strcmp(tide_text_read(&back, world.name).ptr, "dog") == 0);
    free(bytes);
    tide_heap_free(&back);
    tide_heap_free(&snapshot.heap);
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
    tide_page *first = tide_page_new(4096);
    memset(tide_page_data(first), 0xAB, 4096);
    tide_page_release(first, 1);

    tide_page *from = tide_page_new(4096);
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
    const uint64_t a = tide_heap_alloc(&world.heap, 100);
    const uint64_t b = tide_heap_alloc(&world.heap, 100);
    tide_block *block = tide_heap_write(&world.heap, a);
    block->a = 7;
    memset(block + 1, 0x11, 100);
    block = tide_heap_write(&world.heap, b);
    block->a = 9;
    memset(block + 1, 0x22, 100);
    TIDE_REQUIRE(a >> TIDE_HEAP_PAGE_SHIFT == b >> TIDE_HEAP_PAGE_SHIFT);
    tide_heap snapshot = {0};
    tide_heap_copy(&snapshot, &world.heap);
    const tide_page *shared = world.heap.page[a >> TIDE_HEAP_PAGE_SHIFT];
    const uint32_t moves = world.heap.moves;

    block = tide_heap_write_over(&world.heap, a, 60);
    TIDE_CHECK(world.heap.page[a >> TIDE_HEAP_PAGE_SHIFT] != shared && world.heap.moves != moves);
    TIDE_CHECK(block == tide_heap_block(&world.heap, a) && block->a == 7);
    memset(block + 1, 0x33, 60);
    TIDE_CHECK(all((const uint8_t *)(block + 1) + 60, 40, 0x11));
    const tide_block *other = tide_heap_block(&world.heap, b);
    TIDE_CHECK(other->a == 9 && all((const uint8_t *)(other + 1), 100, 0x22));
    TIDE_CHECK(all((const uint8_t *)(tide_heap_block(&snapshot, a) + 1), 100, 0x11));

    tide_heap plain = {0};
    tide_heap_copy(&plain, &snapshot);
    block = tide_heap_write(&plain, a);
    memset(block + 1, 0x33, 60);
    TIDE_CHECK(tide_heap_hash(1, &plain) == tide_heap_hash(1, &world.heap));
    TIDE_CHECK(tide_heap_hash(1, &plain) != tide_heap_hash(1, &snapshot));

    const tide_page *own = world.heap.page[a >> TIDE_HEAP_PAGE_SHIFT];
    TIDE_CHECK(tide_heap_write_over(&world.heap, a, 100) == block || world.heap.page[a >> TIDE_HEAP_PAGE_SHIFT] == own);
    TIDE_CHECK(world.heap.page[a >> TIDE_HEAP_PAGE_SHIFT] == own);
    tide_heap_free(&plain);
    tide_heap_free(&snapshot);
}
