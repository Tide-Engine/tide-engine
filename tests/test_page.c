#include <stdlib.h>
#include <string.h>

#include "tide/page.h"
#include "tide_test.h"

// tide_same_bytes says what memcmp says, on the web too, where it's the
// engine's own loop: at every length around the 64 and the 8 bytes it reads at
// a time, wherever the two runs start, and wherever the one byte that differs
// is.
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
        // Wherever each run starts: neither has to be aligned
        for (int at = 0; at < 9; at++) {
            for (int other = 0; other < 9; other += 4) {
                const uint8_t *x = a + at;
                uint8_t *y = b + other;
                memmove(y, x, (size_t)size);
                TIDE_CHECK(tide_same_bytes(x, y, (size_t)size));
                // One byte apart, at each place: every one for a short run, and for a long one the
                // places around where a round of 64 or of 8 starts and ends
                for (int k = 0; k < size; k++) {
                    const int in_round = k % 64;
                    if (size > 200 && in_round > 9 && in_round < 55 && k < size - 9) continue;
                    y[k] ^= 0x40u;
                    TIDE_CHECK(!tide_same_bytes(x, y, (size_t)size));
                    TIDE_CHECK(tide_same_bytes(x, y, (size_t)k)); // Up to it they're the same
                    y[k] ^= 0x40u;
                    checked++;
                }
                // A byte past the end that differs changes nothing
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
