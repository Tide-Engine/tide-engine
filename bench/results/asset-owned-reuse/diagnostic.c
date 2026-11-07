#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tide/list.h"
#include "tide/pack.h"
#include "tide/sha256.h"
#include "tide/time.h"
#ifndef SHARED
#define SHARED 0
#endif
#ifndef REUSE_OWNED
#define REUSE_OWNED 0
#endif

// Prototype only: current pack handles, without cross-pack stable identities.
// No modified runtime is linked. This measures ownership/copy placement.
static tide_list copy(tide_list value, uint32_t size)
{
    if (SHARED && tide_pack_holds(value.at)) return value;
    return tide_list_copy(value, size);
}
static void assign(tide_list *to, tide_list value, uint32_t size)
{
    if (SHARED && tide_pack_holds(value.at)) {
        if (REUSE_OWNED && to->at && to->at >> 30 == TIDE_IN_MATCH) {
            tide_list_set(to, value, size, TIDE_IN_MATCH);
            return;
        }
        tide_list_release(to, TIDE_IN_MATCH);
        *to = value;
    } else {
        if (SHARED && tide_pack_holds(to->at)) to->at = 0;
        tide_list_set(to, value, size, TIDE_IN_MATCH);
    }
}
static void prepare_write(tide_list *value, uint32_t size)
{
    if (SHARED && tide_pack_holds(value->at)) {
        const tide_list from = *value;
        value->at = 0;
        tide_list_set(value, from, size, TIDE_IN_MATCH);
    }
}
static void *mutable_slot(tide_list_cache *c, tide_list *l, int32_t i, uint32_t size)
{
    if (!SHARED) return tide_list_cached_at_mut(c, *l, i, size);
    // Identical owned-cache hot path; detachment is only a cache-miss concern.
    if (c->writable && c->at == l->at && c->at && (uint32_t)i - c->first < c->count && (uint32_t)i < c->header->a
        && __atomic_load_n(&c->heap->moves, __ATOMIC_RELAXED) == c->moves)
        return (uint8_t *)(uintptr_t)c->elements + (size_t)((uint32_t)i - c->first) * size;
    prepare_write(l, size);
    return tide_list_cache_fill_mut(c, *l, i, size);
}
static uint64_t digest(tide_list value, uint32_t size)
{
    uint64_t hash = TIDE_HASH_START;
    for (int32_t i = 0; i < tide_list_count(value); i++)
        hash = tide_hash_more(hash, tide_list_at(value, i, size), size);
    return tide_hash_end(hash);
}
static uint64_t diagnostic_dirty_bytes(const tide_heap *h)
{
    uint64_t bytes = 0;
    for (uint32_t i = 0; i < h->pages;) {
        const tide_page *p = h->page[i];
        const uint32_t left = h->used - (p->first << TIDE_HEAP_PAGE_SHIFT);
        if (p->hashed == UINT32_MAX) bytes += left < p->size ? left : p->size;
        i += p->size > (1u << TIDE_HEAP_PAGE_SHIFT) ? p->size >> TIDE_HEAP_PAGE_SHIFT : 1u;
    }
    return bytes;
}
static uint32_t diagnostic_pending(const tide_heap *h)
{
    uint32_t count = 0;
    for (uint32_t at = h->pending; at; at = tide_heap_block(h, at)->next) count++;
    return count;
}
int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3) return 2;
    const bool edit = strcmp(argv[1], "mutable-edit") == 0;
    const bool cold = strcmp(argv[1], "first-write-once") == 0;
    const bool first = cold || strcmp(argv[1], "first-write") == 0;
    const bool mutable_copy = strcmp(argv[1], "mutable-copy") == 0;
    const bool read_after_edit = strcmp(argv[1], "written-then-read") == 0;
    if (!edit && !first && !mutable_copy && !read_after_edit && strcmp(argv[1], "asset-assign") != 0) return 2;
    const uint32_t count = edit ? 65536u : mutable_copy ? 256u : argc == 3 ? (uint32_t)strtoul(argv[2], NULL, 10) : 16384u;
    if (count < 2u || count > 1048576u) return 2;
    const uint32_t size = edit ? sizeof(float) : 1u;
    uint8_t *bytes = calloc(count, size);
    if (!bytes) return 1;
    if (edit) { for (uint32_t i = 0; i < count; i++) ((float *)bytes)[i] = (float)(i % 127u); }
    else { for (uint32_t i = 0; i < count; i++) bytes[i] = (uint8_t)(i * 17u); }
    uint8_t sha[32];
    tide_sha256_of(bytes, count * size, sha);
    tide_pack_builder builder = {0};
    uint32_t chars;
    const uint32_t at = tide_pack_bytes(&builder, sha, bytes, count * size, &chars);
    tide_pack_name(&builder, "Data.bytes", TIDE_PACK_BYTES, at, count * size, 1u);
    size_t pack_size;
    uint8_t *pack = tide_pack_end(&builder, &pack_size);
    if (!tide_pack_load(pack, pack_size)) return 1;
    tide_list asset = {tide_pack_handle(tide_pack_find("Data.bytes", TIDE_PACK_BYTES))};
    tide_heap heap = {0}, kept = {0};
    tide_text_use(&heap, NULL);
    tide_list source = {0}, values[64] = {{0}};
    if (edit) tide_list_set(&source, tide_list_from(bytes, (int32_t)count, size), size, TIDE_IN_MATCH);
    else if (mutable_copy) tide_list_set(&source, asset, size, TIDE_IN_MATCH);
    if (edit) tide_list_set(&values[0], source, size, TIDE_IN_MATCH);
    if (read_after_edit) for (uint32_t i = 0; i < 64u; i++) {
        tide_list_set(&values[i], asset, size, TIDE_IN_MATCH);
        *(uint8_t *)tide_list_at_mut(values[i], 1, size) = 42u;
    }
    double assign_ns = 0, write_ns = 0, flush_ns = 0, hash_ns = 0;
    uint64_t hash = 0, diagnostic_bytes = 0; uint32_t diagnostic_releases = 0;
    for (uint32_t round = 0; round < 150u; round++) {
        tide_heap_copy(&kept, &heap);
        const uint64_t kept_hash = tide_heap_hash(TIDE_HASH_START, &kept);
        const uint64_t mark = tide_scratch_mark();
        uint64_t a = tide_time_now_ns();
        if (!edit) for (uint32_t i = 0; i < 64u; i++) {
            const tide_list value = copy(mutable_copy ? source : asset, size);
            assign(&values[i], value, size);
        }
        uint64_t b = tide_time_now_ns(); if (round == 149u) diagnostic_releases = diagnostic_pending(&heap);
        if (first) for (uint32_t i = 0; i < 64u; i++) {
            prepare_write(&values[i], size);
            uint8_t *value = tide_list_at_mut(values[i], 1, size);
            if (!value) return 1;
            *value = (uint8_t)round;
        }
        if (edit) {
            tide_list_cache cache = {0};
            for (uint32_t i = 0; i < count; i++) {
                float *value = mutable_slot(&cache, &values[0], (int32_t)i, size);
                if (!value) return 1;
                *value += 0.125f;
            }
        }
        uint64_t c = tide_time_now_ns();
        tide_heap_flush(&heap); // Generated ticks flush before world hashing.
        uint64_t f = tide_time_now_ns(); if (round == 149u) diagnostic_bytes = diagnostic_dirty_bytes(&heap);
        hash = tide_heap_hash(TIDE_HASH_START, &heap);
        uint64_t d = tide_time_now_ns();
        if (tide_heap_hash(TIDE_HASH_START, &kept) != kept_hash) return 1;
        if (round >= 30u) { assign_ns += (double)(b-a); write_ns += (double)(c-b); flush_ns += (double)(f-c); hash_ns += (double)(d-f); }
        tide_scratch_reset(mark);
        if (cold && round != 149u) {
            tide_heap_free(&kept);
            tide_heap_free(&heap);
            memset(values, 0, sizeof values);
        }
    }
    printf("diagnostic_only=1 pending_after_assign=%u dirty_bytes_before_hash=%llu\n", diagnostic_releases, (unsigned long long)diagnostic_bytes); const uint64_t actual = digest(values[0], size);
    uint64_t expected = TIDE_HASH_START;
    if (edit) {
        for (uint32_t i = 0; i < count; i++) {
            const float value = (float)(i % 127u) + 18.75f;
            expected = tide_hash_more(expected, &value, sizeof value);
        }
    } else {
        for (uint32_t i = 0; i < count; i++) {
            const uint8_t value = first && i == 1u ? 149u : bytes[i];
            expected = tide_hash_more(expected, &value, sizeof value);
        }
    }
    if (actual != tide_hash_end(expected)) return 1;
    if (!edit) for (uint32_t i = 0; i < 64u; i++) {
        if (tide_list_count(values[i]) != (int32_t)count || digest(values[i], size) != actual) return 1;
    }
    printf("case=%s shared=%d reuse=%d count=%u assign_ms=%.6f write_ms=%.6f flush_ms=%.6f hash_ms=%.6f heap_bytes=%u hash=%llu digest=%llu\n",
        argv[1], SHARED, REUSE_OWNED, count, assign_ns / 120000000.0, write_ns / 120000000.0, flush_ns / 120000000.0, hash_ns / 120000000.0,
        tide_heap_packed_size(&heap), (unsigned long long)hash, (unsigned long long)actual);
    if (memcmp(tide_list_at(asset, 0, 1u), bytes, (size_t)count * size)) return 1;
    tide_text_use(NULL, NULL);
    tide_heap_free(&kept);
    tide_heap_free(&heap);
    tide_pack_unload();
    free(pack);
    free(bytes);
    return 0;
}
