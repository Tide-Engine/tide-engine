#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recipient_delta.h"
#include "tide/net.h"
#include "tide/time.h"
#include "tide_test.h"

static void recipient_round_trip(const uint8_t *now, uint32_t count, const uint8_t *base, uint32_t base_count)
{
    uint32_t size = 0;
    uint8_t *bytes = tide_recipient_delta_pack(now, count, base, base_count, &size);
    uint8_t *out = malloc(count ? count : 1u);
    TIDE_REQUIRE(bytes && out);
    memset(out, 0xA5, count);
    TIDE_CHECK(tide_recipient_delta_unpack(out, count, base, base_count, bytes, size));
    TIDE_CHECK(!count || memcmp(out, now, count) == 0);
    // An exact-sized allocation at every cut makes ASan observe overreads.
    for (uint32_t cut = 0; cut < size; cut++) {
        uint8_t *shortened = malloc(cut ? cut : 1u);
        TIDE_REQUIRE(shortened);
        memcpy(shortened, bytes, cut);
        TIDE_CHECK(!tide_recipient_delta_unpack(out, count, base, base_count, shortened, cut));
        free(shortened);
    }
    bytes[9] ^= 1; // Output hash, not token shape.
    TIDE_CHECK(!tide_recipient_delta_unpack(out, count, base, base_count, bytes, size));
    bytes[9] ^= 1;
    uint8_t *trailing = malloc((size_t)size + 1u);
    TIDE_REQUIRE(trailing);
    memcpy(trailing, bytes, size);
    trailing[size] = 0;
    TIDE_CHECK(!tide_recipient_delta_unpack(out, count, base, base_count, trailing, size + 1u));
    TIDE_CHECK(!tide_recipient_delta_unpack(out, count + 1u, base, base_count, bytes, size));
    if (base_count) {
        uint8_t *wrong = malloc(base_count);
        TIDE_REQUIRE(wrong);
        memcpy(wrong, base, base_count);
        wrong[base_count / 2u] ^= 1;
        TIDE_CHECK(!tide_recipient_delta_unpack(out, count, wrong, base_count, bytes, size));
        free(wrong);
    }
    free(trailing);
    free(out);
    free(bytes);
}

TIDE_TEST(recipient_delta_modes_round_trip_and_reject_corruption)
{
    uint8_t base[1024], now[1025];
    uint32_t x = 0x72b13a9du;
    for (uint32_t i = 0; i < sizeof base; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        base[i] = (uint8_t)x;
    }
    recipient_round_trip(base, 0, NULL, 0);
    recipient_round_trip(base, sizeof base, NULL, 0); // Incompressible literal fallback.
    recipient_round_trip(base, sizeof base, base, sizeof base); // Tiny XOR delta.
    now[0] = 17;
    memcpy(now + 1, base, sizeof base);
    recipient_round_trip(now, sizeof now, base, sizeof base); // Shifted baseline spans.
    recipient_round_trip(base + 1, sizeof base - 1u, base, sizeof base); // Deletion.
    memset(now, 7, sizeof now);
    recipient_round_trip(now, sizeof now, NULL, 0); // Distance-one overlapping output copy.
    for (uint32_t n = 1; n <= 32; n++) recipient_round_trip(base, n, base, n / 2u);
}

static void recipient_bad_span(uint32_t literal, uint32_t length, uint64_t source)
{
    const uint8_t base[32] = {0};
    uint8_t out[32];
    tide_writer w = {.grows = true};
    tide_write_u8(&w, 1); // Copy spans.
    tide_write_u64(&w, tide_hash(base, sizeof base));
    tide_write_u64(&w, tide_hash(base, sizeof base));
    tide_write_varint(&w, sizeof out);
    tide_write_varint(&w, literal);
    if (literal <= sizeof base) tide_write_bytes(&w, base, literal);
    tide_write_varint(&w, length);
    tide_write_varint64(&w, source);
    TIDE_CHECK(!tide_recipient_delta_unpack(out, sizeof out, base, sizeof base, w.data, w.size));
    free(w.data);
}

TIDE_TEST(recipient_delta_rejects_invalid_copy_spans)
{
    recipient_bad_span(UINT32_MAX, 8, 0); // Literal past output.
    recipient_bad_span(0, 0, 0); // No progress.
    recipient_bad_span(0, UINT32_MAX, 0); // Copy past output.
    recipient_bad_span(0, 8, UINT64_MAX - 1u); // Base offset arithmetic.
    recipient_bad_span(0, 8, 1); // Self copy with no preceding bytes.
    recipient_bad_span(1, 8, 3); // Self source at output end.
    recipient_bad_span(1, 8, 63); // Self source beyond output.
    recipient_bad_span(0, 8, 62); // Base copy runs past base end.
}

TIDE_TEST(recipient_delta_fixed_changing_records_remain_compact)
{
    const uint32_t count = 1000u * 512u;
    uint8_t *base = malloc(count), *now = malloc(count + 1u), *out = malloc(count + 1u);
    TIDE_REQUIRE(base && now && out);
    uint32_t x = 0x1287abcdu;
    for (uint32_t i = 0; i < count; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        base[i] = (uint8_t)x;
    }
    memcpy(now, base, count);
    for (uint32_t i = 0; i < 1000; i++) for (uint32_t j = 0; j < 12; j++) now[i * 512u + j] ^= (uint8_t)(j + 1u);
    uint32_t size = 0;
    const uint64_t began = tide_time_now_ns();
    for (uint32_t n = 0; n < 16; n++) {
        uint8_t *bytes = tide_recipient_delta_pack(now, count, base, count, &size);
        TIDE_REQUIRE(bytes);
        TIDE_CHECK(tide_recipient_delta_unpack(out, count, base, count, bytes, size));
        TIDE_CHECK(memcmp(out, now, count) == 0);
        free(bytes);
    }
    const double ms = (double)(tide_time_now_ns() - began) / 1000000.0 / 16.0;
    TIDE_CHECK(size < 24000u);
    if (getenv("TIDE_VISIBILITY_METRICS")) printf("recipient fixed records: %u bytes, %.3f ms pack+unpack\n", size, ms);
    memmove(now + 1, now, count);
    now[0] = 7;
    const uint64_t shifted_began = tide_time_now_ns();
    for (uint32_t n = 0; n < 16; n++) {
        uint8_t *bytes = tide_recipient_delta_pack(now, count + 1u, base, count, &size);
        TIDE_REQUIRE(bytes);
        TIDE_CHECK(tide_recipient_delta_unpack(out, count + 1u, base, count, bytes, size));
        TIDE_CHECK(memcmp(out, now, count + 1u) == 0);
        free(bytes);
    }
    const double shifted_ms = (double)(tide_time_now_ns() - shifted_began) / 1000000.0 / 16.0;
    TIDE_CHECK(size < count / 4u); // Shifts must not resend the mostly unchanged records.
    if (getenv("TIDE_VISIBILITY_METRICS")) printf("recipient shifted changing records: %u bytes, %.3f ms pack+unpack\n", size, shifted_ms);
    free(out);
    free(now);
    free(base);
}

TIDE_TEST(recipient_delta_large_baseline_keeps_early_matches)
{
    const uint32_t count = 7u * 1024u * 1024u;
    uint8_t *base = malloc(count), *now = malloc(count + 1u), *out = malloc(count + 1u);
    TIDE_REQUIRE(base && now && out);
    uint32_t x = 0x125a3bc9u;
    for (uint32_t i = 0; i < count; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        base[i] = (uint8_t)x;
    }
    memcpy(now + 1, base, count);
    now[0] = 17;
    // Dynamic records throughout a much larger permitted baseline. A fixed
    // dense direct-map index overwrites early anchors with late-region keys.
    for (uint32_t i = 0; i < 1000; i++) {
        const uint32_t at = 1u + i * (count / 1000u);
        for (uint32_t j = 0; j < 12; j++) now[at + j] ^= (uint8_t)(j + 1u);
    }
    uint32_t size = 0;
    uint8_t *bytes = tide_recipient_delta_pack(now, count + 1u, base, count, &size);
    TIDE_REQUIRE(bytes);
    TIDE_CHECK(size < count / 20u);
    TIDE_CHECK(tide_recipient_delta_unpack(out, count + 1u, base, count, bytes, size));
    TIDE_CHECK(memcmp(out, now, count + 1u) == 0);
    if (getenv("TIDE_VISIBILITY_METRICS")) printf("recipient 7 MiB shifted changing baseline: %u bytes\n", size);
    free(bytes);
    free(out);
    free(now);
    free(base);
}
