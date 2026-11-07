#include "recipient_needs.h"
#include "tide/net.h"
#include "tide_test.h"

#include <stdlib.h>
#include <string.h>

static void needs_round_trip(uint32_t count, uint32_t candidate_count, bool change)
{
    uint8_t *target = malloc(count ? count : 1u), *candidate = malloc(candidate_count ? candidate_count : 1u);
    TIDE_REQUIRE(target && candidate);
    uint32_t x = 0x74a1b39du;
    for (uint32_t i = 0; i < count; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        target[i] = (uint8_t)x;
    }
    memset(candidate, 0xA5, candidate_count);
    memcpy(candidate, target, count < candidate_count ? count : candidate_count);
    if (change && candidate_count) candidate[candidate_count / 2u] ^= 1u;
    uint32_t hash_size = 0, needed = UINT32_MAX, patch_size = 0;
    uint8_t *hashes = tide_recipient_hashes(target, count, &hash_size);
    TIDE_REQUIRE(hashes);
    uint8_t needs[TIDE_RECIPIENT_NEEDS_BYTES] = {0};
    TIDE_REQUIRE(tide_recipient_needs(candidate, candidate_count, hashes, hash_size, needs, sizeof needs, &needed));
    uint8_t *patch = tide_recipient_patch(target, count, needs, needed, &patch_size);
    TIDE_REQUIRE(patch);
    uint8_t *out = NULL;
    uint32_t out_count = UINT32_MAX;
    TIDE_REQUIRE(tide_recipient_patch_apply(candidate, candidate_count, patch, patch_size, &out, &out_count));
    TIDE_CHECK(out_count == count && memcmp(out, target, count) == 0);
    free(out);
    out = NULL;
    for (uint32_t cut = 0; cut < patch_size; cut++) {
        uint8_t *shortened = malloc(cut ? cut : 1u);
        TIDE_REQUIRE(shortened);
        memcpy(shortened, patch, cut);
        TIDE_CHECK(!tide_recipient_patch_apply(candidate, candidate_count, shortened, cut, &out, &out_count));
        TIDE_CHECK(out == NULL);
        free(shortened);
    }
    uint8_t *trailing = malloc((size_t)patch_size + 1u);
    TIDE_REQUIRE(trailing);
    memcpy(trailing, patch, patch_size);
    trailing[patch_size] = 0;
    TIDE_CHECK(!tide_recipient_patch_apply(candidate, candidate_count, trailing, patch_size + 1u, &out, &out_count));
    patch[4] ^= 1u;
    TIDE_CHECK(!tide_recipient_patch_apply(candidate, candidate_count, patch, patch_size, &out, &out_count));
    free(trailing);
    free(patch);
    free(hashes);
    free(candidate);
    free(target);
}

TIDE_TEST(recipient_needs_round_trips_missing_matching_and_partial_blocks)
{
    needs_round_trip(0, 0, false);
    needs_round_trip(31, 31, false);
    needs_round_trip(31, 31, true);
    needs_round_trip(31, 0, false);
    needs_round_trip(31, 57, false);
    needs_round_trip(TIDE_RECIPIENT_BLOCK_BYTES + 9u, TIDE_RECIPIENT_BLOCK_BYTES + 9u, false);
    needs_round_trip(TIDE_RECIPIENT_BLOCK_BYTES + 9u, TIDE_RECIPIENT_BLOCK_BYTES + 4u, false);
}

TIDE_TEST(recipient_needs_rejects_malformed_masks_hashes_and_false_have)
{
    const uint8_t target[32] = {1};
    uint32_t hash_size = 0, needed = 9, patch_size = 0;
    uint8_t *hashes = tide_recipient_hashes(target, sizeof target, &hash_size);
    TIDE_REQUIRE(hashes);
    uint8_t needs[2] = {0};
    for (uint32_t cut = 0; cut < hash_size; cut++) {
        TIDE_CHECK(!tide_recipient_needs(target, sizeof target, hashes, cut, needs, sizeof needs, &needed));
    }
    uint8_t trailing_hashes[21];
    TIDE_REQUIRE(hash_size == sizeof trailing_hashes - 1u);
    memcpy(trailing_hashes, hashes, hash_size);
    trailing_hashes[hash_size] = 0;
    TIDE_CHECK(!tide_recipient_needs(target, sizeof target, trailing_hashes, sizeof trailing_hashes,
                                    needs, sizeof needs, &needed));
    TIDE_CHECK(!tide_recipient_needs(target, sizeof target, hashes, hash_size, needs, 0, &needed));
    TIDE_CHECK(!tide_recipient_patch(target, sizeof target, needs, 0, &patch_size));
    TIDE_CHECK(!tide_recipient_patch(target, sizeof target, needs, 2, &patch_size));
    needs[0] = 2; // Nonexistent second block.
    TIDE_CHECK(!tide_recipient_patch(target, sizeof target, needs, 1, &patch_size));
    needs[0] = 0;
    uint8_t *patch = tide_recipient_patch(target, sizeof target, needs, 1, &patch_size);
    TIDE_REQUIRE(patch);
    uint8_t wrong[32] = {0}, *out = NULL;
    uint32_t out_count = 123;
    TIDE_CHECK(!tide_recipient_patch_apply(wrong, sizeof wrong, patch, patch_size, &out, &out_count));
    TIDE_CHECK(!tide_recipient_patch_apply(target, sizeof target - 1u, patch, patch_size, &out, &out_count));
    patch[12] = 2; // A malformed mask from the network, not just the pack API.
    TIDE_CHECK(!tide_recipient_patch_apply(target, sizeof target, patch, patch_size, &out, &out_count));
    TIDE_CHECK(out == NULL && out_count == 123);
    free(patch);
    free(hashes);

    // A missing block cannot use an encoded-length varint to read past input.
    tide_writer forged = {.grows = true};
    tide_write_u32(&forged, sizeof target);
    tide_write_u64(&forged, tide_hash(target, sizeof target));
    tide_write_u8(&forged, 1);
    tide_write_varint(&forged, UINT32_MAX);
    TIDE_CHECK(!tide_recipient_patch_apply(NULL, 0, forged.data, forged.size, &out, &out_count));
    TIDE_CHECK(out == NULL && out_count == 123);
    free(forged.data);

    uint8_t hostile[12] = {255, 255, 255, 255};
    TIDE_CHECK(!tide_recipient_needs(NULL, 0, hostile, sizeof hostile, needs, sizeof needs, &needed));
    TIDE_CHECK(!tide_recipient_patch_apply(NULL, 0, hostile, sizeof hostile, &out, &out_count));
    uint8_t byte = 0;
    TIDE_CHECK(!tide_recipient_hashes(&byte, UINT32_MAX, &hash_size));
}

TIDE_TEST(recipient_needs_mask_limit_is_a_fallback_not_a_world_allocation)
{
    // The last supported block and first unsupported block are checked from
    // headers alone; a hostile size never requires a correspondingly large input.
    const uint32_t count = TIDE_RECIPIENT_NEEDS_BYTES * 8u * TIDE_RECIPIENT_BLOCK_BYTES;
    tide_writer w = {.grows = true};
    tide_write_u32(&w, count);
    tide_write_u64(&w, 0);
    for (uint32_t i = 0; i < TIDE_RECIPIENT_NEEDS_BYTES * 8u; i++) tide_write_u64(&w, 0);
    uint8_t mask[TIDE_RECIPIENT_NEEDS_BYTES] = {0};
    uint32_t needed = 0;
    TIDE_CHECK(tide_recipient_needs(NULL, 0, w.data, w.size, mask, sizeof mask, &needed));
    TIDE_CHECK(needed == sizeof mask);
    for (uint32_t i = 0; i < sizeof mask; i++) TIDE_CHECK(mask[i] == 255u);
    w.data[0] = 1; // count + 1 requires one more block than negotiation allows.
    TIDE_CHECK(!tide_recipient_needs(NULL, 0, w.data, w.size, mask, sizeof mask, &needed));
    free(w.data);
}

TIDE_TEST(recipient_needs_sparse_large_endpoint_only_patches_one_block)
{
    const uint32_t count = 7u * 1024u * 1024u + 9u;
    uint8_t *target = calloc(count, 1), *candidate = calloc(count, 1);
    TIDE_REQUIRE(target && candidate);
    target[6961415] = 3;
    target[6962355] = 7;
    uint32_t hash_size = 0, needed = 0, patch_size = 0;
    uint8_t *hashes = tide_recipient_hashes(target, count, &hash_size);
    TIDE_REQUIRE(hashes);
    uint8_t needs[TIDE_RECIPIENT_NEEDS_BYTES] = {0};
    TIDE_REQUIRE(tide_recipient_needs(candidate, count, hashes, hash_size, needs, sizeof needs, &needed));
    uint32_t missing = 0;
    for (uint32_t i = 0; i < needed; i++) {
        for (uint32_t bit = 0; bit < 8; bit++) missing += (needs[i] >> bit) & 1u;
    }
    TIDE_CHECK(missing == 1u);
    uint8_t *patch = tide_recipient_patch(target, count, needs, needed, &patch_size);
    TIDE_REQUIRE(patch);
    TIDE_CHECK(hash_size + patch_size < 8192u);
    uint8_t *out = NULL;
    uint32_t out_count = 0;
    TIDE_REQUIRE(tide_recipient_patch_apply(candidate, count, patch, patch_size, &out, &out_count));
    TIDE_CHECK(out_count == count && memcmp(out, target, count) == 0);
    free(out);
    free(patch);
    free(hashes);
    free(candidate);
    free(target);
}
