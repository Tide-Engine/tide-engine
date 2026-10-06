#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/net.h"
#include "tide_test.h"

// tide_hash_wide, written the plain way: each chain goes through the run by
// itself, taking every fourth word, where the engine's goes through it once
// with four at a time. The same steps as tide_hash's, which net.c keeps to
// itself, so they're here again.
static uint64_t step(uint64_t h, const uint64_t word)
{
    h ^= word * 0xBF58476D1CE4E5B9ull;
    return (h << 27 | h >> 37) * 0x94D049BB133111EBull;
}

static uint64_t word_at(const uint8_t *p, const size_t at, const size_t size)
{
    uint64_t word = 0;
    for (size_t k = 0; k < 8u && at + k < size; k++) word |= (uint64_t)p[at + k] << (8u * k);
    return word;
}

static uint64_t plain(const uint8_t *p, const size_t size)
{
    const size_t words = (size + 7u) / 8u; // The last one short, and zeros past its bytes
    uint64_t h = step(TIDE_HASH_START, (uint64_t)size);
    for (uint64_t k = 0; k < 4u; k++) {
        uint64_t chain = TIDE_HASH_START + k * 0xBF58476D1CE4E5B9ull;
        for (size_t w = (size_t)k; w < words; w += 4u) chain = step(chain, word_at(p, 8u * w, size));
        h = step(h, chain);
    }
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ull;
    h ^= h >> 33;
    return h;
}

static void fill(uint8_t *p, const size_t size, uint32_t dice)
{
    for (size_t i = 0; i < size; i++) {
        dice = dice * 1664525u + 1013904223u;
        p[i] = (uint8_t)(dice >> 24);
    }
}

// At every length around its rounds of 32 bytes and its words of 8, and at
// the lengths pages have, wherever the run starts.
TIDE_TEST(hash_wide_is_four_chains_side_by_side)
{
    enum { ROOM = 16384 + 16 };
    uint8_t *p = malloc(ROOM);
    TIDE_REQUIRE(p);
    fill(p, ROOM, 7u);
    for (size_t size = 0; size <= 300u; size++) {
        for (size_t at = 0; at < 8u; at += 3u) TIDE_REQUIRE(tide_hash_wide(p + at, size) == plain(p + at, size));
    }
    static const size_t sizes[] = {1000, 4095, 4096, 4097, 8192, 16000, 16383, 16384};
    for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        TIDE_CHECK(tide_hash_wide(p, sizes[s]) == plain(p, sizes[s]));
        TIDE_CHECK(tide_hash_wide(p + 5, sizes[s]) == plain(p + 5, sizes[s]));
    }
    free(p);
}

// A page that differs in one bit, anywhere, is another number: every byte of
// a page of 16 KiB, a bit of each. And so is a run that's the same bytes with
// one more or one fewer: zeros, where nothing else tells lengths apart.
TIDE_TEST(hash_wide_tells_pages_apart)
{
    enum { PAGE = 16384 };
    uint8_t *p = malloc(PAGE);
    TIDE_REQUIRE(p);
    fill(p, PAGE, 99u);
    const uint64_t whole = tide_hash_wide(p, PAGE);
    int same = 0;
    for (size_t i = 0; i < PAGE; i++) {
        const uint8_t bit = (uint8_t)(1u << (i % 8u));
        p[i] ^= bit;
        same += tide_hash_wide(p, PAGE) == whole;
        p[i] ^= bit;
    }
    TIDE_CHECK(same == 0);
    // The top bit of a word and the bits around the middle of one, in each chain: the ones a multiply
    // carries no further up, which the step's turn brings back down
    for (size_t word = 0; word < 16u; word++) {
        for (uint32_t b = 24; b < 64u; b++) {
            p[8u * word + b / 8u] ^= (uint8_t)(1u << (b % 8u));
            same += tide_hash_wide(p, PAGE) == whole;
            p[8u * word + b / 8u] ^= (uint8_t)(1u << (b % 8u));
        }
    }
    TIDE_CHECK(same == 0);
    TIDE_CHECK(tide_hash_wide(p, PAGE) == whole);

    memset(p, 0, 400);
    for (size_t size = 0; size < 400u; size++) {
        for (size_t other = size + 1u; other < 400u; other += 7u) same += tide_hash_wide(p, size) == tide_hash_wide(p, other);
    }
    TIDE_CHECK(same == 0);
    free(p);
}

// The same numbers on every platform: of nothing, of a byte, of a run that
// ends in the middle of a word, and of a page.
TIDE_TEST(hash_wide_is_the_same_everywhere)
{
    enum { PAGE = 16384 };
    uint8_t *p = malloc(PAGE);
    TIDE_REQUIRE(p);
    for (size_t i = 0; i < PAGE; i++) p[i] = (uint8_t)(i * 31u + (i >> 8));
    const uint64_t got[4] = {tide_hash_wide(p, 0), tide_hash_wide(p + 1, 1), tide_hash_wide(p, 45), tide_hash_wide(p, PAGE)};
    static const uint64_t expected[4] = {0x9667D443F69A9305ull, 0x54CBADDBEE23DEA2ull, 0x51FB53DCBABE7E86ull, 0x2DDED3B43D52129Full};
    for (int i = 0; i < 4; i++) {
        if (got[i] != expected[i]) printf("    hash %d is 0x%016llXull\n", i, (unsigned long long)got[i]);
        TIDE_CHECK(got[i] == expected[i]);
    }
    free(p);
}
