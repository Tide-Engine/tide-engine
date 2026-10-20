#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/net.h"
#include "tide_test.h"

// tide_hash_wide, written the plain way: a word at a time, from bytes read one
// by one, where the engine's goes through two or four words at once in vectors.
// Its keys, starting values and end are the ones net.c keeps to itself, so
// they're here again.
static const uint64_t key[24] = {
    0xF1D471BF300D2ACAull, 0xB98D6EEDD4DBCB43ull, 0xE41EE98106E4C3FCull, 0x9C8A5940DDCAF100ull,
    0x11B96D3559621C0Full, 0x2F6492F64C4825C7ull, 0x4F68DFF234BD3F70ull, 0x5117AE9AB1A42248ull,
    0xAD0C1209D1D3DB34ull, 0xAC457600ABAB1F16ull, 0x4B606D258A0FA763ull, 0x043784BAA9E95F59ull,
    0x0AE390D1EF5642B1ull, 0x9D695D2FAB53C13Bull, 0x532E68DCA3E90347ull, 0xF8E6E506A1500E40ull,
    0x47CDFC62D58F7BF2ull, 0x296BDD3EE2449306ull, 0x1F8513F3B10D4892ull, 0xDB64DF13CE0E619Full,
    0xABEF04B0B493CF32ull, 0x5ECC859D5BDACECEull, 0x8D192B91AAD61BCDull, 0xC15E687D2404F4AAull,
};

static const uint64_t start[8] = {
    0xAD7D5C3025A25336ull, 0x2F00DA7797F2515Aull, 0x1934E91F08D7208Cull, 0xF312F6ED745EF31Aull,
    0x0662C9D801DEFBCEull, 0xA776B25C6399975Aull, 0x7821036870D8D6C0ull, 0x0373291AABC48B14ull,
};

static uint64_t step(uint64_t h, const uint64_t word)
{
    h ^= word * 0xBF58476D1CE4E5B9ull;
    return (h << 27 | h >> 37) * 0x94D049BB133111EBull;
}

// The run's word at `at`, with zeros past its end
static uint64_t word_at(const uint8_t *p, const size_t at, const size_t size)
{
    uint64_t word = 0;
    for (size_t k = 0; k < 8u && at + k < size; k++) word |= (uint64_t)p[at + k] << (8u * k);
    return word;
}

static uint64_t plain(const uint8_t *p, const size_t size)
{
    uint64_t acc[8];
    memcpy(acc, start, sizeof acc);
    const size_t stripes = (size + 63u) / 64u; // The last one short, and zeros past its bytes
    for (size_t n = 0; n < stripes; n++) {
        for (uint32_t j = 0; j < 8u; j++) {
            const uint64_t d = word_at(p, 64u * n + 8u * j, size);
            const uint64_t dk = d ^ key[n % 16u + j];
            acc[j ^ 1u] += d;
            acc[j] += (dk & 0xFFFFFFFFu) * (dk >> 32);
        }
        if (n % 16u == 15u) {
            for (uint32_t j = 0; j < 8u; j++) acc[j] = ((acc[j] ^ acc[j] >> 47) ^ key[16u + j]) * 0x9E3779B1u;
        }
    }
    uint64_t h = step(0x9E3779B97F4A7C15ull, (uint64_t)size);
    for (uint32_t j = 0; j < 8u; j++) h = step(h, acc[j]);
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ull;
    h ^= h >> 33;
    return h;
}

static int compare_hashes(const void *a, const void *b)
{
    const uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static void fill(uint8_t *p, const size_t size, uint32_t dice)
{
    for (size_t i = 0; i < size; i++) {
        dice = dice * 1664525u + 1013904223u;
        p[i] = (uint8_t)(dice >> 24);
    }
}

// At every length around its stripes of 64 bytes and blocks of 1 KiB, and at
// the lengths pages have, wherever the run starts, of bytes and of zeros: both
// ways the engine has (the machine's widest vectors, where it has AVX2, and
// those every CPU of the platform has).
TIDE_TEST(hash_wide_is_stripes_of_products)
{
    enum { ROOM = 16384 + 64 };
    uint8_t *p = malloc(ROOM), *z = calloc(1, ROOM);
    TIDE_REQUIRE(p && z);
    fill(p, ROOM, 7u);
    for (size_t size = 0; size <= 300u; size++) {
        for (size_t at = 0; at < 8u; at += 3u) {
            const uint64_t want = plain(p + at, size);
            TIDE_REQUIRE(tide_hash_wide(p + at, size) == want);
            TIDE_REQUIRE(tide_hash_wide_baseline(p + at, size) == want);
        }
        TIDE_REQUIRE(tide_hash_wide(z, size) == plain(z, size));
    }
    static const size_t sizes[] = {960, 1023, 1024, 1025, 1087, 1088, 2047, 2048, 2049, 4095, 4096, 4097, 8192, 15360, 16000, 16383, 16384};
    for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        for (size_t at = 0; at < 64u; at += 21u) {
            const uint64_t want = plain(p + at, sizes[s]);
            TIDE_CHECK(tide_hash_wide(p + at, sizes[s]) == want);
            TIDE_CHECK(tide_hash_wide_baseline(p + at, sizes[s]) == want);
        }
        TIDE_CHECK(tide_hash_wide(z, sizes[s]) == plain(z, sizes[s]));
        TIDE_CHECK(tide_hash_wide_baseline(z, sizes[s]) == plain(z, sizes[s]));
    }
    free(p);
    free(z);
}

// A page that differs in one bit, anywhere, is another number: every byte of
// a page of 16 KiB, a bit of each. So is one that differs in the same bit of
// two stripes, of zeros, as worlds mostly are: the top bit of a word, whose
// two sums cancel, and the bits either side of its halves. And so is a run
// that's the same bytes with one more or one fewer: zeros, where nothing else
// tells lengths apart.
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
    TIDE_CHECK(tide_hash_wide(p, PAGE) == whole);

    // 2 KiB of zeros: two blocks, so pairs of stripes in one block and across a scramble
    enum { RUN = 2048, STRIPES = RUN / 64 };
    memset(p, 0, RUN);
    static const uint32_t places[] = {0, 31, 32, 33, 62, 63};
    enum { PLACES = sizeof places / sizeof places[0], RUNS = 1 + 8 * PLACES * STRIPES * (STRIPES - 1) / 2 };
    uint64_t *h = malloc(sizeof *h * RUNS);
    TIDE_REQUIRE(h);
    size_t count = 0;
    h[count++] = tide_hash_wide(p, RUN);
    for (uint32_t word = 0; word < 8u; word++) {
        for (uint32_t place = 0; place < PLACES; place++) {
            const size_t bit = 64u * word + places[place];
            for (size_t s = 0; s < STRIPES; s++) {
                for (size_t t = s + 1u; t < STRIPES; t++) {
                    const size_t x = 512u * s + bit, y = 512u * t + bit;
                    p[x / 8u] ^= (uint8_t)(1u << (x % 8u));
                    p[y / 8u] ^= (uint8_t)(1u << (y % 8u));
                    h[count++] = tide_hash_wide(p, RUN);
                    p[x / 8u] ^= (uint8_t)(1u << (x % 8u));
                    p[y / 8u] ^= (uint8_t)(1u << (y % 8u));
                }
            }
        }
    }
    TIDE_REQUIRE(count == RUNS);
    qsort(h, count, sizeof *h, compare_hashes); // Sorted, equal neighbours are equal hashes
    for (size_t i = 1; i < count; i++) same += h[i] == h[i - 1u];
    TIDE_CHECK(same == 0);
    free(h);

    enum { LONGEST = 2100 };
    memset(p, 0, LONGEST);
    uint64_t lengths[LONGEST + 1];
    for (size_t size = 0; size <= LONGEST; size++) lengths[size] = tide_hash_wide(p, size);
    qsort(lengths, LONGEST + 1, sizeof *lengths, compare_hashes);
    for (size_t i = 1; i <= LONGEST; i++) same += lengths[i] == lengths[i - 1u];
    TIDE_CHECK(same == 0);
    free(p);
}

// The same numbers on every platform: of nothing, of a byte, of a run that
// ends in the middle of a stripe, and of a page.
TIDE_TEST(hash_wide_is_the_same_everywhere)
{
    enum { PAGE = 16384 };
    uint8_t *p = malloc(PAGE);
    TIDE_REQUIRE(p);
    for (size_t i = 0; i < PAGE; i++) p[i] = (uint8_t)(i * 31u + (i >> 8));
    const uint64_t got[4] = {tide_hash_wide(p, 0), tide_hash_wide(p + 1, 1), tide_hash_wide(p, 45), tide_hash_wide(p, PAGE)};
    static const uint64_t expected[4] = {0x07FC10953F5EA411ull, 0xCE911F489DBA695Eull, 0x27F0D06EC69445ACull, 0x0E65FC98EFF6165Eull};
    for (int i = 0; i < 4; i++) {
        if (got[i] != expected[i]) printf("    hash %d is 0x%016llXull\n", i, (unsigned long long)got[i]);
        TIDE_CHECK(got[i] == expected[i]);
    }
    free(p);
}
