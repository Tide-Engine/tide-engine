#include "recipient_delta.h"

#include "tide/delta.h"

#include <stdlib.h>
#include <string.h>

#define REGION 16384u
#define SLOTS 65536u
#define MATCH 8u
enum { XOR_REGIONS, COPY_SPANS, LITERALS };

// One candidate per hash, no chains or unbounded searches. Collisions can only
// reduce compression: every match is compared byte for byte before being used.
static uint32_t slot_of(const uint8_t *p)
{
    uint32_t a, b;
    memcpy(&a, p, 4);
    memcpy(&b, p + 4, 4);
    return ((a * 0x9e3779b1u) ^ (b * 0x85ebca77u)) >> 16;
}

static uint32_t match_size(const uint8_t *now, uint32_t at, uint32_t count,
                           const uint8_t *source, uint32_t from, uint32_t source_count)
{
    uint32_t n = 0;
    const uint32_t most = count - at < source_count - from ? count - at : source_count - from;
    while (n < most && now[at + n] == source[from + n]) n++;
    return n;
}

static void literals(tide_writer *w, const uint8_t *now, uint32_t from, uint32_t to)
{
    tide_write_varint(w, to - from);
    tide_write_bytes(w, now + from, to - from);
}

static uint8_t *copy_spans(const uint8_t *now, uint32_t count,
                           const uint8_t *base, uint32_t base_count, uint32_t *size)
{
    uint32_t *indexes = tide_alloc_zeroed(SLOTS * 2u, sizeof *indexes);
    uint32_t *old = indexes, *current = indexes + SLOTS;
    // Sparse baseline anchors avoid drowning the fixed index in nearby keys.
    // The current scan visits every byte, so shifts retain the opportunity to
    // find an anchor at any alignment. Hash collisions only lose compression.
    uint32_t step = ((base_count / SLOTS) + 31u) & ~31u;
    if (step < 32u) step = 32u;
    for (uint32_t i = 0; base_count >= MATCH && i <= base_count - MATCH; i += step) {
        old[slot_of(base + i)] = i + 1u;
        if (base_count - MATCH - i < step) break;
    }
    tide_writer w = {.grows = true};
    uint32_t at = 0, literal = 0;
    while (count - at >= MATCH) {
        const uint32_t slot = slot_of(now + at);
        uint32_t length = 0, source = 0;
        bool self = false;
        if (old[slot]) {
            source = old[slot] - 1u;
            length = match_size(now, at, count, base, source, base_count);
        }
        if (current[slot]) {
            const uint32_t from = current[slot] - 1u;
            const uint32_t n = match_size(now, at, count, now, from, count);
            if (n > length) { length = n; source = from; self = true; }
        }
        if (length < MATCH) {
            current[slot] = at + 1u;
            at++;
            continue;
        }
        literals(&w, now, literal, at);
        tide_write_varint(&w, length);
        tide_write_varint64(&w, (uint64_t)source * 2u + (self ? 1u : 0u));
        const uint32_t end = at + length;
        // A matched span needs no further search. Sparse indexing retains
        // future self-copy opportunities without hashing every copied byte.
        for (uint32_t i = at; i < end;) {
            if (count - i >= MATCH) current[slot_of(now + i)] = i + 1u;
            if (end - i < MATCH) break;
            i += MATCH;
        }
        at = end;
        literal = at;
    }
    if (literal < count) literals(&w, now, literal, count);
    free(indexes);
    *size = w.size;
    return w.data;
}

uint8_t *tide_recipient_delta_pack(const uint8_t *now, uint32_t count,
    const uint8_t *base, uint32_t base_count, uint32_t *size)
{
    tide_delta_writer d;
    const uint64_t hash = tide_hash(now, count);
    tide_delta_begin(&d, base != NULL, NULL, 0, hash);
    for (uint32_t at = 0; at < count;) {
        const uint32_t n = count - at < REGION ? count - at : REGION;
        const uint32_t bn = at < base_count ? (base_count - at < REGION ? base_count - at : REGION) : 0u;
        tide_delta_region(&d, now + at, n, bn ? base + at : NULL, bn, false, 0);
        at += n;
    }
    tide_delta_close(&d);
    uint32_t xor_size = 0, copy_size = 0;
    uint8_t *xor_bytes = tide_delta_end(&d, &xor_size);
    // The common sparse/unchanged path is already tiny. Do not build an LZ
    // index and scan the baseline just to save a few bytes on such a repair.
    uint8_t *copy = NULL;
    if (xor_size > 64u && xor_size > count / 32u) copy = copy_spans(now, count, base, base_count, &copy_size);
    else copy_size = UINT32_MAX;
    const uint32_t mode = xor_size < copy_size && xor_size < count ? XOR_REGIONS
        : copy_size < count ? COPY_SPANS : LITERALS;
    tide_writer w = {.grows = true};
    tide_write_u8(&w, (uint8_t)mode);
    tide_write_u64(&w, tide_hash(base, base_count));
    tide_write_u64(&w, hash);
    tide_write_varint(&w, count);
    tide_write_bytes(&w, mode == XOR_REGIONS ? xor_bytes : mode == COPY_SPANS ? copy : now,
                     mode == XOR_REGIONS ? xor_size : mode == COPY_SPANS ? copy_size : count);
    free(xor_bytes);
    free(copy);
    *size = w.size;
    return w.data;
}

static bool read_spans(tide_reader *r, uint8_t *out, uint32_t count, const uint8_t *base, uint32_t base_count)
{
    uint32_t at = 0;
    while (at < count) {
        const uint32_t literal = tide_read_varint(r);
        if (r->failed || literal > count - at) return false;
        const uint8_t *bytes = tide_read_bytes(r, literal);
        if (!bytes) return false;
        memcpy(out + at, bytes, literal);
        at += literal;
        if (at == count) break;
        const uint32_t n = tide_read_varint(r);
        const uint64_t token = tide_read_varint64(r);
        const uint64_t from = token >> 1;
        if (r->failed || n < MATCH || n > count - at) return false;
        if (token & 1u) {
            if (from >= at) return false;
            // Forward copy deliberately supports overlap, bounded by output.
            for (uint32_t i = 0; i < n; i++) out[at + i] = out[(uint32_t)from + i];
        } else {
            if (from > base_count || n > base_count - from) return false;
            memcpy(out + at, base + (uint32_t)from, n);
        }
        at += n;
    }
    return !r->failed && r->at == r->size;
}

bool tide_recipient_delta_unpack(uint8_t *out, uint32_t count,
    const uint8_t *base, uint32_t base_count, const uint8_t *data, uint32_t size)
{
    tide_reader r = {data, size, 0, false};
    const uint8_t mode = tide_read_u8(&r);
    const uint64_t base_hash = tide_read_u64(&r);
    const uint64_t hash = tide_read_u64(&r);
    const uint32_t declared = tide_read_varint(&r);
    if (r.failed || declared != count || base_hash != tide_hash(base, base_count)) return false;
    bool ok = false;
    if (mode == COPY_SPANS) ok = read_spans(&r, out, count, base, base_count);
    else if (mode == LITERALS) {
        const uint8_t *bytes = tide_read_bytes(&r, count);
        ok = bytes && r.at == r.size;
        if (ok) memcpy(out, bytes, count);
    } else if (mode == XOR_REGIONS) {
        memset(out, 0, count);
        tide_delta_reader d;
        uint64_t inner_hash = 0;
        ok = tide_delta_open(&d, data + r.at, size - r.at, base != NULL, &inner_hash) && inner_hash == hash;
        for (uint32_t at = 0; ok && at < count;) {
            const uint32_t n = count - at < REGION ? count - at : REGION;
            const uint32_t bn = at < base_count ? (base_count - at < REGION ? base_count - at : REGION) : 0u;
            ok = tide_delta_get(&d, out + at, n, bn ? base + at : NULL, bn);
            at += n;
        }
        ok = ok && tide_delta_closed(&d) && d.bytes.at == d.bytes.size;
    }
    return ok && tide_hash(out, count) == hash;
}
