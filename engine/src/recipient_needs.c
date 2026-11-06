#include "recipient_needs.h"
#include "recipient_delta.h"

#include "tide/net.h"

#include <stdlib.h>
#include <string.h>

static uint32_t blocks_of(uint32_t count)
{
    return count / TIDE_RECIPIENT_BLOCK_BYTES + (count % TIDE_RECIPIENT_BLOCK_BYTES != 0u);
}

static uint32_t mask_size(uint32_t blocks)
{
    return blocks / 8u + (blocks % 8u != 0u);
}

static bool valid_mask(const uint8_t *mask, uint32_t size, uint32_t blocks)
{
    if (blocks > TIDE_RECIPIENT_NEEDS_BYTES * 8u || size != mask_size(blocks) || (size && !mask)) return false;
    const uint32_t tail = blocks % 8u;
    return !tail || !(mask[size - 1u] & (uint8_t)(0xFFu << tail));
}

static uint32_t block_size(uint32_t count, uint32_t at)
{
    return count - at < TIDE_RECIPIENT_BLOCK_BYTES ? count - at : TIDE_RECIPIENT_BLOCK_BYTES;
}

uint8_t *tide_recipient_hashes(const uint8_t *target, uint32_t count, uint32_t *size)
{
    *size = 0;
    const uint32_t blocks = blocks_of(count);
    if ((count && !target) || blocks > TIDE_RECIPIENT_NEEDS_BYTES * 8u) return NULL;
    tide_writer w = {.grows = true};
    tide_write_u32(&w, count);
    tide_write_u64(&w, tide_hash(target, count));
    for (uint32_t at = 0; at < count;) {
        const uint32_t n = block_size(count, at);
        tide_write_u64(&w, tide_hash(target + at, n));
        at += n;
    }
    if (w.overflow) { free(w.data); return NULL; }
    *size = w.size;
    return w.data;
}

bool tide_recipient_needs(const uint8_t *candidate, uint32_t count,
    const uint8_t *hashes, uint32_t size, uint8_t *out, uint32_t capacity, uint32_t *needed)
{
    if ((count && !candidate) || !hashes) return false;
    tide_reader r = {hashes, size, 0, false};
    const uint32_t target_count = tide_read_u32(&r);
    tide_read_u64(&r); // The complete hash is verified when the patch arrives.
    const uint32_t blocks = blocks_of(target_count), bytes = mask_size(blocks);
    if (r.failed || blocks > TIDE_RECIPIENT_NEEDS_BYTES * 8u || bytes > capacity || (bytes && !out)
        || r.size - r.at != blocks * 8u) return false;
    if (bytes) memset(out, 0, bytes);
    uint32_t at = 0;
    for (uint32_t i = 0; i < blocks; i++) {
        const uint32_t n = block_size(target_count, at);
        const uint64_t hash = tide_read_u64(&r);
        if (at > count || n > count - at || tide_hash(candidate + at, n) != hash)
            out[i / 8u] |= (uint8_t)(1u << (i % 8u));
        at += n;
    }
    *needed = bytes;
    return !r.failed;
}

uint8_t *tide_recipient_patch(const uint8_t *target, uint32_t count,
    const uint8_t *needs, uint32_t need_size, uint32_t *size)
{
    *size = 0;
    const uint32_t blocks = blocks_of(count);
    if ((count && !target) || !valid_mask(needs, need_size, blocks)) return NULL;
    tide_writer w = {.grows = true};
    tide_write_u32(&w, count);
    tide_write_u64(&w, tide_hash(target, count));
    tide_write_bytes(&w, needs, need_size);
    uint32_t at = 0;
    for (uint32_t i = 0; i < blocks; i++) {
        const uint32_t n = block_size(count, at);
        if (needs[i / 8u] & (1u << (i % 8u))) {
            uint32_t encoded_size = 0;
            uint8_t *encoded = tide_recipient_delta_pack(target + at, n, NULL, 0, &encoded_size);
            if (!encoded) { free(w.data); return NULL; }
            tide_write_varint(&w, encoded_size);
            tide_write_bytes(&w, encoded, encoded_size);
            free(encoded);
        }
        at += n;
    }
    if (w.overflow) { free(w.data); return NULL; }
    *size = w.size;
    return w.data;
}

bool tide_recipient_patch_apply(const uint8_t *candidate, uint32_t count,
    const uint8_t *patch, uint32_t size, uint8_t **out, uint32_t *out_count)
{
    if ((count && !candidate) || !patch) return false;
    tide_reader r = {patch, size, 0, false};
    const uint32_t target_count = tide_read_u32(&r);
    const uint64_t hash = tide_read_u64(&r);
    const uint32_t blocks = blocks_of(target_count), bytes = mask_size(blocks);
    if (r.failed || blocks > TIDE_RECIPIENT_NEEDS_BYTES * 8u) return false;
    const uint8_t *needs = tide_read_bytes(&r, bytes);
    if (r.failed || !valid_mask(needs, bytes, blocks)) return false;
    uint8_t *result = malloc(target_count ? target_count : 1u);
    if (!result) return false;
    bool ok = true;
    uint32_t at = 0;
    for (uint32_t i = 0; ok && i < blocks; i++) {
        const uint32_t n = block_size(target_count, at);
        if (needs[i / 8u] & (1u << (i % 8u))) {
            const uint32_t encoded_size = tide_read_varint(&r);
            const uint8_t *encoded = tide_read_bytes(&r, encoded_size);
            ok = !r.failed && encoded && tide_recipient_delta_unpack(result + at, n, NULL, 0, encoded, encoded_size);
        } else {
            ok = at <= count && n <= count - at;
            if (ok) memcpy(result + at, candidate + at, n);
        }
        at += n;
    }
    ok = ok && !r.failed && r.at == r.size && tide_hash(result, target_count) == hash;
    if (!ok) { free(result); return false; }
    *out = result;
    *out_count = target_count;
    return true;
}
