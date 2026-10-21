#include <string.h>

#include "tide/list.h"
#include "tide/text.h"
#include "tide_test.h"

// The scratch area grows as it needs (see tide/text.h): what code makes along
// the way is whole however big it is, and the same whatever else was made
// before it.

typedef struct scratch_world {
    tide_list numbers;
    tide_heap heap;
} scratch_world;

static scratch_world world;

#define BIG_COUNT 300000 // Ints: 1.2 MB, more than a slot of the area (TIDE_SCRATCH_BYTES)

static uint32_t value_at(const int32_t i)
{
    return (uint32_t)i * 2654435761u; // Every byte of it changes from one element to the next
}

static void start(void)
{
    tide_heap_free(&world.heap);
    memset(&world, 0, sizeof world);
    tide_text_use(&world.heap, NULL);
    for (int32_t i = 0; i < BIG_COUNT; i++) {
        uint32_t *slot = tide_list_add(&world.numbers, sizeof *slot, TIDE_IN_MATCH);
        *slot = value_at(i);
    }
}

static int32_t wrong(const tide_list l, const int32_t count)
{
    int32_t n = 0;
    for (int32_t i = 0; i < count; i++) {
        const uint32_t *p = tide_list_at(l, i, sizeof *p);
        n += !p || *p != value_at(i);
    }
    return n;
}

static bool is(const tide_str s, const char *expected)
{
    return s.bytes == (int32_t)strlen(expected) && memcmp(s.ptr, expected, strlen(expected)) == 0 && s.ptr[s.bytes] == '\0';
}

static bool is_pattern(const tide_str s, const int32_t count, const char *tail)
{
    if (s.bytes != count * 16 + (int32_t)strlen(tail) || s.chars != s.bytes) return false;
    bool same = true;
    for (int32_t i = 0; i < count; i++) same &= memcmp(s.ptr + (size_t)i * 16u, "0123456789abcdef", 16) == 0;
    return same && memcmp(s.ptr + (size_t)count * 16u, tail, strlen(tail)) == 0 && s.ptr[s.bytes] == '\0';
}

TIDE_TEST(scratch_big_lists_are_whole)
{
    start();
    const uint32_t mark = tide_scratch_mark();
    tide_list copy = tide_list_copy(world.numbers, sizeof(uint32_t));
    TIDE_CHECK(copy.at >> 30 == TIDE_IN_SCRATCH);
    TIDE_CHECK(tide_list_count(copy) == BIG_COUNT);
    TIDE_CHECK(wrong(copy, BIG_COUNT) == 0);
    TIDE_CHECK(tide_scratch_mark() - mark > BIG_COUNT * sizeof(uint32_t));

    *(uint32_t *)tide_list_at_mut(copy, 0, sizeof(uint32_t)) = 7u;
    TIDE_CHECK(wrong(world.numbers, BIG_COUNT) == 0);
    TIDE_CHECK(wrong(copy, BIG_COUNT) == 1);

    for (int32_t i = BIG_COUNT; i < BIG_COUNT + 100; i++) {
        uint32_t *slot = tide_list_add(&copy, sizeof *slot, TIDE_IN_SCRATCH);
        TIDE_REQUIRE(slot != NULL);
        *slot = value_at(i);
    }
    *(uint32_t *)tide_list_at_mut(copy, 0, sizeof(uint32_t)) = value_at(0);
    TIDE_CHECK(tide_list_count(copy) == BIG_COUNT + 100 && wrong(copy, BIG_COUNT + 100) == 0);

    uint32_t *raw = tide_scratch_memory(BIG_COUNT * sizeof *raw);
    TIDE_CHECK(((uintptr_t)raw & 15u) == 0);
    for (int32_t i = 0; i < BIG_COUNT; i++) raw[i] = value_at(i);
    const tide_list made = tide_list_from(raw, BIG_COUNT, sizeof *raw);
    TIDE_CHECK(made.at >> 30 == TIDE_IN_SCRATCH && tide_list_count(made) == BIG_COUNT && wrong(made, BIG_COUNT) == 0);
    TIDE_CHECK(wrong(copy, BIG_COUNT + 100) == 0);

    tide_scratch_reset(mark);
    TIDE_CHECK(tide_scratch_mark() == mark);
    tide_heap_free(&world.heap);
}

TIDE_TEST(scratch_big_text_is_whole)
{
    const uint32_t mark = tide_scratch_mark();
    const int32_t n = 100000;
    tide_str s = TIDE_STR_EMPTY;
    for (int32_t i = 0; i < n; i++) s = tide_str_add_cstr(s, "0123456789abcdef");
    TIDE_CHECK(is_pattern(s, n, ""));

    const tide_str b = tide_str_add_int(TIDE_STR_EMPTY, 12345, 0);
    s = tide_str_add_cstr(s, "end");
    TIDE_CHECK(is_pattern(s, n, "end"));
    TIDE_CHECK(is(b, "12345"));
    TIDE_CHECK(tide_str_ends_with(s, tide_str_from_cstr("fend")));
    const tide_str piece = tide_str_substring(s, (int32_t)TIDE_SCRATCH_BYTES - 6, 12);
    TIDE_CHECK(piece.ptr == s.ptr + TIDE_SCRATCH_BYTES - 6 && piece.bytes == 12 && piece.chars == 12);
    TIDE_CHECK(memcmp(piece.ptr, "abcdef012345", 12) == 0 && piece.ptr[12] == '6');
    TIDE_CHECK(strcmp(tide_str_c(piece), "abcdef012345") == 0);

    const tide_text temp = tide_text_temp(s);
    const tide_str back = tide_text_view(temp);
    TIDE_CHECK(temp.at >> 30 == TIDE_IN_SCRATCH && back.ptr != s.ptr);
    TIDE_CHECK(back.bytes == s.bytes && back.chars == s.chars && memcmp(back.ptr, s.ptr, (size_t)s.bytes) == 0);
    const tide_str upper = tide_str_to_upper(back);
    TIDE_CHECK(upper.bytes == s.bytes && !tide_str_eq(upper, s) && tide_str_eq(tide_str_to_lower(upper), s));
    tide_scratch_reset(mark);
    TIDE_CHECK(tide_scratch_mark() == mark);
}

TIDE_TEST(scratch_results_are_the_same_whatever_came_first)
{
    start();
    const uint32_t mark = tide_scratch_mark();
    const uint32_t befores[] = {0u, TIDE_SCRATCH_BYTES - 100u, TIDE_SCRATCH_BYTES - 17u, TIDE_SCRATCH_BYTES + 5u,
                                2u * TIDE_SCRATCH_BYTES + TIDE_SCRATCH_BYTES / 2u};
    for (size_t k = 0; k < sizeof befores / sizeof befores[0]; k++) {
        char *filler = tide_scratch_memory(befores[k]);
        memset(filler, 'f', befores[k]);
        const tide_list copy = tide_list_copy(world.numbers, sizeof(uint32_t));
        TIDE_CHECK(tide_list_count(copy) == BIG_COUNT && wrong(copy, BIG_COUNT) == 0);
        tide_str s = TIDE_STR_EMPTY;
        for (int32_t i = 0; i < 70000; i++) s = tide_str_add_cstr(s, "0123456789abcdef");
        TIDE_CHECK(is_pattern(s, 70000, ""));
        TIDE_CHECK(wrong(copy, BIG_COUNT) == 0);
        bool same = true;
        for (uint32_t i = 0; i < befores[k]; i++) same &= filler[i] == 'f';
        TIDE_CHECK(same);
        tide_scratch_reset(mark);
    }
    tide_heap_free(&world.heap);
}

TIDE_TEST(scratch_pieces_go_back_with_the_mark)
{
    start();
    const uint32_t mark = tide_scratch_mark();
    uint32_t first = 0;
    for (int round = 0; round < 50; round++) {
        const tide_list copy = tide_list_copy(world.numbers, sizeof(uint32_t));
        if (round == 0) first = copy.at;
        TIDE_CHECK(copy.at == first);
        TIDE_CHECK(tide_list_count(copy) == BIG_COUNT && wrong(copy, BIG_COUNT) == 0);
        tide_scratch_reset(mark);
        TIDE_CHECK(tide_scratch_mark() == mark);
    }

    const tide_list copy = tide_list_copy(world.numbers, sizeof(uint32_t));
    const uint32_t inner = tide_scratch_mark();
    char *more = tide_scratch_memory(2u * TIDE_SCRATCH_BYTES);
    memset(more, 1, 2u * TIDE_SCRATCH_BYTES);
    tide_scratch_reset(inner);
    TIDE_CHECK(tide_scratch_mark() == inner);
    TIDE_CHECK(wrong(copy, BIG_COUNT) == 0);
    const uint32_t most = 3u * TIDE_SCRATCH_BYTES - 32u;
    char *again = tide_scratch_memory(most);
    memset(again, 2, most);
    const uint32_t after = tide_scratch_mark();
    TIDE_CHECK(((uintptr_t)again & 15u) == 0 && wrong(copy, BIG_COUNT) == 0);
    char *small = tide_scratch_memory(5u);
    TIDE_CHECK(small == again + most);
    TIDE_CHECK(tide_scratch_mark() == after + 5u);
    tide_scratch_reset(mark);
    TIDE_CHECK(tide_scratch_mark() == mark);

    // The same requests land at the same offsets whether or not a bigger piece
    // was let go before them: a spare of three slots that a copy of two uses
    // takes only two, so what comes after it goes where it went the first time
    uint32_t marks[2][2];
    for (int round = 0; round < 2; round++) {
        const tide_list two = tide_list_copy(world.numbers, sizeof(uint32_t));
        marks[round][0] = tide_scratch_mark();
        char *three = tide_scratch_memory(most);
        memset(three, 3, most);
        marks[round][1] = tide_scratch_mark();
        TIDE_CHECK(wrong(two, BIG_COUNT) == 0);
        tide_scratch_reset(mark);
    }
    TIDE_CHECK(marks[0][0] == marks[1][0] && marks[0][1] == marks[1][1]);
    tide_heap_free(&world.heap);
}
