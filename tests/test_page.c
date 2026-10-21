#include <stdlib.h>
#include <string.h>

#include "tide/jobs.h"
#include "tide/net.h"
#include "tide/page.h"
#include "tide/table.h"
#include "tide_test.h"

// tide_same_bytes says what memcmp says, at every length around the 64 and 8 bytes it reads at a time, wherever the runs start and the differing byte is.
TIDE_TEST(page_same_bytes_is_what_memcmp_says)
{
    enum { ROOM = 4096 + 64 };
    uint8_t *a = malloc(ROOM);
    uint8_t *b = malloc(ROOM);
    TIDE_REQUIRE(a && b);
    uint32_t dice = 12345u;
    for (int i = 0; i < ROOM; i++) {
        dice = dice * 1664525u + 1013904223u;
        a[i] = (uint8_t)(dice >> 24);
    }
    memcpy(b, a, ROOM);
    static const int sizes[] = {0,  1,  2,  7,  8,  9,  15,  16,  17,  31,  32,  33,   63,   64,   65,  71,
                                72, 73, 127, 128, 129, 191, 192, 193, 200, 255, 256, 1000, 4095, 4096};
    int checked = 0;
    for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        const int size = sizes[s];
        for (int at = 0; at < 9; at++) {
            for (int other = 0; other < 9; other += 4) {
                const uint8_t *x = a + at;
                uint8_t *y = b + other;
                memmove(y, x, (size_t)size);
                TIDE_CHECK(tide_same_bytes(x, y, (size_t)size));
                for (int k = 0; k < size; k++) {
                    const int in_round = k % 64;
                    if (size > 200 && in_round > 9 && in_round < 55 && k < size - 9) continue;
                    y[k] ^= 0x40u;
                    TIDE_CHECK(!tide_same_bytes(x, y, (size_t)size));
                    TIDE_CHECK(tide_same_bytes(x, y, (size_t)k));
                    y[k] ^= 0x40u;
                    checked++;
                }
                y[size] ^= 0xFFu;
                TIDE_CHECK(tide_same_bytes(x, y, (size_t)size) == (memcmp(x, y, (size_t)size) == 0));
                y[size] ^= 0xFFu;
            }
        }
    }
    TIDE_CHECK(checked > 20000);
    free(a);
    free(b);
}

// A table with megabytes in it: an entity and four numbers a row, in chunks
// of 2,048 rows, so a chunk is a page of 16 KiB and four of 8.
static const uint32_t wide_sizes[] = {sizeof(tide_entity), 4, 4, 4, 4};
static const tide_columns wide = {5, 11, wide_sizes};

static void widen(tide_table *t, const uint32_t rows)
{
    for (uint32_t i = 0; i < rows; i++) {
        const uint32_t row = tide_table_add(t, &wide);
        *(tide_entity *)tide_table_cell(t, &wide, row, 0) = (tide_entity){row, 1};
        for (uint32_t k = 1; k < 5; k++) *(uint32_t *)tide_table_cell(t, &wide, row, k) = row * 2654435761u + k;
    }
}

// A pool that takes turns: the threads it calls in each run the work to its
// end, one after another, and then the caller's does. It says what it was
// asked for.
typedef struct turns {
    uint32_t rounds;
    uint32_t others;
} turns;

static void take_turns(void *self, const uint32_t others, void (*work)(void *context, uint32_t thread), void *context)
{
    turns *t = self;
    t->rounds++;
    t->others = others;
    for (uint32_t thread = others; thread > 0; thread--) work(context, thread);
    work(context, 0);
}

static uint32_t stale_in(const tide_table *t, uint64_t *bytes)
{
    tide_stale_pages stale = {0};
    tide_table_stale(t, &wide, &stale);
    const uint32_t count = stale.count;
    if (bytes) *bytes = stale.bytes;
    (void)tide_pages_hash(&stale, NULL);
    return count;
}

TIDE_TEST(page_stale_pages_are_hashed_on_threads_before_the_hash)
{
    tide_table a = {0};
    tide_table b = {0};
    widen(&a, 60000);
    widen(&b, 60000);
    const uint64_t alone = tide_table_hash(TIDE_HASH_START, &b, &wide);

    uint64_t bytes = 0;
    const uint32_t pages = stale_in(&a, &bytes);
    TIDE_CHECK(pages == a.chunks * wide.count && bytes == 60000u * 24u);

    turns pool = {0};
    const tide_jobs jobs = {.self = &pool, .threads = 12, .run_some = take_turns};
    tide_stale_pages stale = {0};
    tide_table_stale(&a, &wide, &stale);
    TIDE_CHECK(tide_pages_hash(&stale, &jobs) == pages && stale.pages == NULL && stale.count == 0);
    TIDE_CHECK(pool.rounds == 1 && pool.others == 2);
    TIDE_CHECK(stale_in(&a, NULL) == 0);
    TIDE_CHECK(tide_table_hash(TIDE_HASH_START, &a, &wide) == alone);

    *(uint32_t *)tide_table_cell(&a, &wide, 31000, 2) = 7;
    *(uint32_t *)tide_table_cell(&b, &wide, 31000, 2) = 7;
    tide_table_stale(&a, &wide, &stale);
    TIDE_CHECK(stale.count == 1 && stale.bytes == 2048u * 4u);
    TIDE_CHECK(tide_pages_hash(&stale, &jobs) == 0 && pool.rounds == 1);
    TIDE_CHECK(stale_in(&a, NULL) == 1);
    TIDE_CHECK(tide_table_hash(TIDE_HASH_START, &a, &wide) == tide_table_hash(TIDE_HASH_START, &b, &wide));
    TIDE_CHECK(tide_table_hash(TIDE_HASH_START, &a, &wide) != alone);

    const tide_jobs two = {.self = &pool, .threads = 2, .run_some = take_turns};
    const tide_jobs every = {.self = &pool, .threads = 12};
    const tide_jobs one = {.self = &pool, .threads = 1, .run_some = take_turns};
    for (int kind = 0; kind < 3; kind++) {
        for (uint32_t row = 0; row < 60000u; row += 2048u) {
            ((tide_entity *)tide_table_cell(&a, &wide, row, 0))->generation += 1;
            ((tide_entity *)tide_table_cell(&b, &wide, row, 0))->generation += 1;
            *(uint32_t *)tide_table_cell(&a, &wide, row, 1) ^= 0x5Au;
            *(uint32_t *)tide_table_cell(&b, &wide, row, 1) ^= 0x5Au;
        }
        tide_table_stale(&a, &wide, &stale);
        TIDE_CHECK(stale.count == 2u * a.chunks && stale.bytes == 60000u * 12u);
        const uint32_t rounds = pool.rounds;
        const uint32_t hashed = tide_pages_hash(&stale, kind == 0 ? &two : kind == 1 ? &every : &one);
        TIDE_CHECK(hashed == (kind == 0 ? 2u * a.chunks : 0u));
        TIDE_CHECK(pool.rounds == rounds + (kind == 0 ? 1u : 0u));
        if (kind == 0) TIDE_CHECK(pool.others == 1);
        TIDE_CHECK(tide_table_hash(TIDE_HASH_START, &a, &wide) == tide_table_hash(TIDE_HASH_START, &b, &wide));
    }
    tide_table_free(&a, &wide);
    tide_table_free(&b, &wide);
}
