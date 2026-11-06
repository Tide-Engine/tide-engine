#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/delta.h"
#include "recipient_delta.h"
#include "tide_test.h"

// Regions of a delta (tide/delta.h), filtered by a stride: whichever the
// writer picks, every byte comes back as it was.

static uint64_t seed = 0x9E3779B97F4A7C15ull;

static uint32_t next_random(void)
{
    seed = seed * 6364136223846793005ull + 1442695040888963407ull;
    return (uint32_t)(seed >> 32);
}

typedef struct row {
    int32_t x;
    int32_t y;
    float z;
} row;

// `size` bytes of a kind: 0 zeros, 1 noise, 2 rows like the one before, 3
// runs of a few values, 4 noise with zeros between.
static void fill(uint8_t *out, const uint32_t size, const int kind)
{
    for (uint32_t i = 0; i < size; i++) {
        const uint32_t r = next_random();
        if (kind == 0) out[i] = 0;
        if (kind == 1) out[i] = (uint8_t)r;
        if (kind == 3) out[i] = (uint8_t)(i / 37u % 3u);
        if (kind == 4) out[i] = r % 4u ? 0 : (uint8_t)(r >> 8);
    }
    if (kind != 2) return;
    for (uint32_t i = 0; i * sizeof(row) < size; i++) {
        const row r = {(int32_t)i, 7, 1.0f};
        const uint32_t n = size - i * (uint32_t)sizeof r < sizeof r ? size - i * (uint32_t)sizeof r : (uint32_t)sizeof r;
        memcpy(out + i * sizeof r, &r, n);
    }
}

// A delta of one region and back: its size, or 0 if it didn't come back.
static uint32_t round_trip(const uint8_t *now, const uint32_t size, const uint8_t *base, const uint32_t base_size,
                           const uint32_t stride)
{
    tide_delta_writer d;
    tide_delta_begin(&d, base != NULL, NULL, 0, 42);
    tide_delta_region(&d, now, size, base, base_size, false, stride);
    tide_delta_close(&d);
    uint32_t packed = 0;
    uint8_t *data = tide_delta_end(&d, &packed);

    tide_delta_reader r;
    uint64_t hash = 0;
    uint8_t *out = calloc(1, size + 1u);
    bool ok = tide_delta_open(&r, data, packed, base != NULL, &hash) && hash == 42;
    ok = ok && tide_delta_get(&r, out, size, base, base_size) && tide_delta_closed(&r);
    ok = ok && memcmp(out, now, size) == 0;
    free(out);
    free(data);
    return ok ? packed : 0u;
}

// The recipient codec splits its flat, variable-length byte stream into 16 KiB
// regions. Keep a standalone reproduction of the loss of delta locality when
// an early list grows. The size is measured, not pinned: a better codec should
// be able to reduce it without weakening this round-trip check.
static uint32_t recipient_regions_round_trip(const uint8_t *now, uint32_t size,
                                            const uint8_t *base, uint32_t base_size)
{
    tide_delta_writer d;
    tide_delta_begin(&d, true, NULL, 0, 42);
    for (uint32_t at = 0; at < size;) {
        const uint32_t n = size - at < 16384u ? size - at : 16384u;
        const uint32_t bn = at < base_size ? (base_size - at < 16384u ? base_size - at : 16384u) : 0u;
        tide_delta_region(&d, now + at, n, bn ? base + at : NULL, bn, false, 0);
        at += n;
    }
    tide_delta_close(&d);
    uint32_t packed = 0;
    uint8_t *bytes = tide_delta_end(&d, &packed);
    uint8_t *out = calloc(size, 1);
    tide_delta_reader r;
    uint64_t hash = 0;
    bool ok = out && tide_delta_open(&r, bytes, packed, true, &hash) && hash == 42;
    for (uint32_t at = 0; ok && at < size;) {
        const uint32_t n = size - at < 16384u ? size - at : 16384u;
        const uint32_t bn = at < base_size ? (base_size - at < 16384u ? base_size - at : 16384u) : 0u;
        ok = tide_delta_get(&r, out + at, n, bn ? base + at : NULL, bn);
        at += n;
    }
    ok = ok && tide_delta_closed(&r) && memcmp(out, now, size) == 0;
    free(out);
    free(bytes);
    return ok ? packed : 0u;
}

TIDE_TEST(delta_recipient_shifted_records_round_trip)
{
    const uint32_t size = 1000u * 512u;
    uint8_t *base = malloc(size), *now = malloc(size + 1u);
    TIDE_REQUIRE(base && now);
    uint32_t x = 0x1287abcdu;
    for (uint32_t i = 0; i < size; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        base[i] = (uint8_t)x;
    }
    memcpy(now, base, size);
    now[1000] ^= 1;
    const uint32_t changed = recipient_regions_round_trip(now, size, base, size);
    memcpy(now + 1, base, size);
    now[0] = 7;
    const uint32_t shifted = recipient_regions_round_trip(now, size + 1u, base, size);
    TIDE_CHECK(changed > 0 && shifted > 0);
    uint32_t improved = 0;
    uint8_t *packed = tide_recipient_delta_pack(now, size + 1u, base, size, &improved);
    uint8_t *out = malloc(size + 1u);
    TIDE_REQUIRE(packed && out);
    TIDE_CHECK(tide_recipient_delta_unpack(out, size + 1u, base, size, packed, improved));
    TIDE_CHECK(memcmp(out, now, size + 1u) == 0);
    TIDE_CHECK(improved < 4096u && improved < shifted / 20u);
    if (getenv("TIDE_VISIBILITY_METRICS"))
        printf("recipient 1000 x 512-byte records: one changed byte %u, prefix insertion %u -> %u bytes\n", changed, shifted, improved);
    free(out);
    free(packed);
    free(now);
    free(base);
}

TIDE_TEST(delta_strides_come_back_as_they_were)
{
    static const uint32_t sizes[] = {1, 7, 8, 9, 12, 100, 1000, 4096, 4097, 16384};
    static const uint32_t strides[] = {0, 1, 4, 12, 24, 200};
    uint8_t *now = malloc(16384);
    uint8_t *base = malloc(16384);
    for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        for (int kind = 0; kind < 5; kind++) {
            for (size_t k = 0; k < sizeof strides / sizeof strides[0]; k++) {
                const uint32_t size = sizes[s];
                fill(now, size, kind);
                TIDE_CHECK(round_trip(now, size, NULL, 0, strides[k]) > 0);
                // From a base, shorter, as long and longer
                for (int b = 0; b < 3; b++) {
                    const uint32_t base_size = b == 0 ? size / 2u : b == 1 ? size : size + 5u < 16384u ? size + 5u : size;
                    memcpy(base, now, size);
                    fill(base, base_size, (kind + 1) % 5); // Unlike it
                    if (kind == 2) {
                        for (uint32_t i = 0; i < base_size; i += 64u) base[i] = now[i]; // Some alike
                    }
                    TIDE_CHECK(round_trip(now, size, base, base_size, strides[k]) > 0);
                }
            }
        }
    }
    free(now);
    free(base);
}

TIDE_TEST(delta_strides_pack_rows_like_the_one_before)
{
    // Rows whose x counts up: as they are, nothing is zeros but y's and z's
    // top bytes; XOR'd with the row before, nearly everything is
    uint8_t *now = malloc(16384);
    fill(now, 16384, 2);
    const uint32_t packed = round_trip(now, 16384, NULL, 0, sizeof(row));
    TIDE_CHECK(packed > 0 && packed < 16384u / 3u);
    fill(now, 16384, 3); // Runs of a few values: XOR'd with the byte before
    const uint32_t runs = round_trip(now, 16384, NULL, 0, 0);
    TIDE_CHECK(runs > 0 && runs < 16384u / 8u);
    free(now);
}

TIDE_TEST(delta_strides_turn_down_one_too_long)
{
    // A region whose stride is past the most a writer uses
    uint8_t bytes[32];
    tide_writer w = {bytes, sizeof bytes, 0, false, false};
    tide_write_varint(&w, 0);   // Flags
    tide_write_u64(&w, 42);     // Hash
    tide_write_varint(&w, 0);   // No regions the same before it
    tide_write_varint(&w, 200); // Its stride
    tide_write_varint(&w, 0);   // Zeros
    tide_write_varint(&w, 1);   // A literal
    tide_write_u8(&w, 9);
    tide_write_varint(&w, 0); // The end of its part
    tide_delta_reader r;
    uint64_t hash = 0;
    uint8_t out[1] = {0};
    TIDE_REQUIRE(tide_delta_open(&r, bytes, w.size, false, &hash));
    TIDE_CHECK(!tide_delta_get(&r, out, 1, NULL, 0));
}
