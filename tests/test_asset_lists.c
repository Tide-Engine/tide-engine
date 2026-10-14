#include <stdlib.h>
#include <string.h>

#include "tide/list.h"
#include "tide/pack.h"
#include "tide/sha256.h"
#include "tide_test.h"

#define ASSET_BYTES 130123u

static uint8_t *asset_pack(size_t *size, uint8_t *values)
{
    uint32_t state = 17;
    for (uint32_t i = 0; i < ASSET_BYTES; i++) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        values[i] = (uint8_t)state;
    }
    uint8_t id[32];
    tide_sha256_of(values, ASSET_BYTES, id);
    tide_pack_builder builder = {0};
    uint32_t chars;
    const uint32_t at = tide_pack_bytes(&builder, id, values, ASSET_BYTES, &chars);
    tide_pack_name(&builder, "bytes", TIDE_PACK_BYTES, at, ASSET_BYTES, 0);
    return tide_pack_end(&builder, size);
}

static tide_list packed_bytes(void)
{
    const tide_pack_entry *entry = tide_pack_find("bytes", TIDE_PACK_BYTES);
    return (tide_list){TIDE_IN_SCRATCH << 30 | TIDE_PACK_BASE | entry->offset};
}

static uint8_t *delta(const tide_heap *heap, const tide_heap *base, const uint8_t *need,
    const uint32_t need_size, const bool assets, uint32_t *size)
{
    tide_delta_writer writer;
    tide_delta_begin(&writer, base != NULL, need, need_size, tide_heap_hash(0, heap));
    writer.assets = assets;
    tide_heap_pack_delta(heap, base, &writer);
    return tide_delta_end(&writer, size);
}

static bool undelta(tide_heap *heap, const tide_heap *base, const uint8_t *bytes, const uint32_t size)
{
    tide_delta_reader reader;
    uint64_t hash;
    return tide_delta_open(&reader, bytes, size, base != NULL, &hash)
        && tide_heap_unpack_delta(heap, base, &reader) && reader.bytes.at == size
        && tide_heap_hash(0, heap) == hash;
}

TIDE_TEST(asset_lists_share_immutable_values_detach_and_survive_pack_unload)
{
    uint8_t *values = malloc(ASSET_BYTES);
    size_t size;
    uint8_t *pack = asset_pack(&size, values);
    TIDE_REQUIRE(tide_pack_load(pack, size));
    tide_pack_use();
    tide_heap heap = {0}, snapshot = {0}, back = {0};
    tide_text_use(&heap, NULL);
    tide_list lists[100] = {{0}};
    const tide_list source = packed_bytes();
    for (uint32_t i = 0; i < 100u; i++) tide_list_set(&lists[i], source, 1u, TIDE_IN_MATCH);
    TIDE_CHECK(heap.used < 8192u);
    const uint32_t kept_at = lists[0].at;
    const uint32_t kept_used = heap.used;
    for (uint32_t i = 0; i < 100u; i++) tide_list_set(&lists[0], source, 1u, TIDE_IN_MATCH);
    TIDE_CHECK(lists[0].at == kept_at && heap.used == kept_used && !heap.pending);
    const void *shared = tide_list_at(lists[0], 0, 1u);
    for (uint32_t i = 0; i < 100u; i++) TIDE_CHECK(tide_list_at(lists[i], 0, 1u) == shared);
    TIDE_CHECK(!tide_list_at_mut(lists[0], 0, 1u));
    const uint64_t original = tide_heap_hash(0, &heap);
    tide_heap_copy(&snapshot, &heap);
    tide_list local = tide_list_copy(lists[0], 1u);
    tide_list_prepare(&local, 1u, TIDE_IN_SCRATCH);
    *(uint8_t *)tide_list_at_mut(local, 3, 1u) ^= 255u;
    TIDE_CHECK(tide_heap_hash(0, &heap) == original);
    tide_list_prepare(&lists[0], 1u, TIDE_IN_MATCH);
    *(uint8_t *)tide_list_at_mut(lists[0], 3, 1u) ^= 7u;
    TIDE_CHECK(*(const uint8_t *)tide_list_at(lists[1], 3, 1u) == values[3]);
    TIDE_CHECK(*(const uint8_t *)tide_list_read(&snapshot, lists[1], 3, 1u) == values[3]);
    tide_list_resize(&lists[2], 7, 1u, TIDE_IN_MATCH);
    tide_list_out out;
    tide_list_out_begin(&out, 1u);
    tide_list_out_room(&out, 0);
    tide_list_out_end(&out, &lists[3], TIDE_IN_MATCH);
    TIDE_CHECK(!lists[3].at);
    const uint32_t bytes_size = tide_heap_packed_size(&heap);
    uint8_t *bytes = malloc(bytes_size);
    tide_writer writer = {bytes, bytes_size, 0, false, false};
    tide_heap_pack(&heap, &writer);
    TIDE_REQUIRE(!writer.overflow && writer.size == bytes_size);
    tide_pack_unload();
    TIDE_CHECK(!memcmp(tide_list_at(lists[1], 0, 1u), values, ASSET_BYTES));
    tide_reader reader = {bytes, bytes_size, 0, false};
    TIDE_REQUIRE(tide_heap_unpack(&back, &reader));
    TIDE_CHECK(reader.at == bytes_size && tide_heap_hash(0, &back) == tide_heap_hash(0, &heap));
    TIDE_CHECK(!memcmp(tide_list_read(&back, lists[1], 0, 1u), values, ASSET_BYTES));
    tide_heap_free(&heap);
    TIDE_CHECK(!memcmp(tide_list_read(&snapshot, lists[1], 0, 1u), values, ASSET_BYTES));
    tide_heap_free(&snapshot); tide_heap_free(&back);
    tide_text_use(NULL, NULL); tide_scratch_reset(0);
    free(bytes); free(pack); free(values);
}

TIDE_TEST(asset_lists_transfer_references_sparse_patches_and_full_fallback)
{
    uint8_t *values = malloc(ASSET_BYTES);
    size_t pack_size;
    uint8_t *pack = asset_pack(&pack_size, values);
    TIDE_REQUIRE(tide_pack_load(pack, pack_size));
    tide_pack_use();
    tide_heap heap = {0}, back = {0}, snapshot = {0};
    tide_text_use(&heap, NULL);
    tide_list list = {0}, other = {0};
    tide_list_set(&list, packed_bytes(), 1u, TIDE_IN_MATCH);
    tide_list_set(&other, packed_bytes(), 1u, TIDE_IN_MATCH);
    uint32_t ref_size, full_size;
    uint8_t *reference = delta(&heap, NULL, NULL, 0, true, &ref_size);
    uint8_t *full = delta(&heap, NULL, NULL, 0, false, &full_size);
    TIDE_CHECK(ref_size < 256u && full_size > ASSET_BYTES);
    TIDE_REQUIRE(undelta(&back, NULL, reference, ref_size));
    tide_heap_free(&back);
    // Every incomplete reference packet rejects cleanly, including identity and shape records.
    for (uint32_t end = 0; end < ref_size; end++) {
        TIDE_CHECK(!undelta(&back, NULL, reference, end));
        tide_heap_free(&back);
    }
    // A sender cannot supply false leases or unknown descriptor identities.
    uint8_t *spoiled = malloc(ref_size);
    memcpy(spoiled, reference, ref_size);
    const uint32_t one = 1;
    memcpy(spoiled + 53u, &one, sizeof one);
    TIDE_CHECK(!undelta(&back, NULL, spoiled, ref_size));
    tide_heap_free(&back);
    // Also remove the omitted lease's wire offset, leaving otherwise valid
    // dictionary/page framing. The authenticated registry must still reject it.
    memcpy(spoiled, reference, 65u);
    memcpy(spoiled + 53u, &one, sizeof one);
    memcpy(spoiled + 65u, reference + 69u, ref_size - 69u);
    TIDE_CHECK(!undelta(&back, NULL, spoiled, ref_size - 4u));
    tide_heap_free(&back);
    free(spoiled);
    tide_heap_copy(&snapshot, &heap);
    ((uint8_t *)(tide_heap_write(&snapshot, list.at & 0x3FFFFFFFu) + 1))[0] ^= 1u;
    uint32_t bad_size;
    uint8_t *bad = delta(&snapshot, NULL, NULL, 0, true, &bad_size);
    TIDE_CHECK(!undelta(&back, NULL, bad, bad_size));
    tide_heap_free(&back); tide_heap_free(&snapshot); free(bad);
    free(reference); free(full);
    tide_list_prepare(&list, 1u, TIDE_IN_MATCH);
    *(uint8_t *)tide_list_at_mut(list, 50000, 1u) ^= 1u;
    reference = delta(&heap, NULL, NULL, 0, true, &ref_size);
    full = delta(&heap, NULL, NULL, 0, false, &full_size);
    TIDE_CHECK(ref_size < 2048u && full_size > ASSET_BYTES * 2u);
    TIDE_REQUIRE(undelta(&back, NULL, reference, ref_size));
    TIDE_CHECK(*(const uint8_t *)tide_list_read(&back, list, 50000, 1u) == (uint8_t)(values[50000] ^ 1u));
    tide_heap_free(&back);
    tide_heap_copy(&snapshot, &heap);
    *(uint8_t *)tide_list_at_mut(list, 50001, 1u) ^= 4u;
    uint32_t changed_size;
    uint8_t *changed = delta(&heap, &snapshot, NULL, 0, true, &changed_size);
    TIDE_REQUIRE(undelta(&back, &snapshot, changed, changed_size));
    tide_heap_free(&back); free(changed);
    tide_writer hashes = {.grows = true};
    tide_heap_hash_pages(&heap, &hashes);
    uint8_t needs[512];
    tide_needs need;
    tide_needs_begin(&need, hashes.data, hashes.size, needs, sizeof needs);
    tide_heap_need_pages(&snapshot, &need);
    const uint32_t need_size = tide_needs_end(&need);
    changed = delta(&heap, NULL, needs, need_size, true, &changed_size);
    TIDE_REQUIRE(undelta(&back, &snapshot, changed, changed_size));
    tide_heap_free(&back); free(changed); free(hashes.data);
    tide_pack_unload();
    TIDE_CHECK(!undelta(&back, NULL, reference, ref_size));
    tide_heap_free(&back);
    // An explicit shared base already owns the immutable dictionary, even
    // without a loaded pack. Reuse it rather than sending the payload again.
    changed = delta(&heap, &snapshot, NULL, 0, false, &changed_size);
    TIDE_CHECK(changed_size < 2048u);
    TIDE_REQUIRE(undelta(&back, &snapshot, changed, changed_size));
    TIDE_CHECK(*(const uint8_t *)tide_list_read(&back, other, 0, 1u) == values[0]);
    tide_heap_free(&back); free(changed);
    TIDE_REQUIRE(undelta(&back, NULL, full, full_size));
    tide_heap_free(&back);
    free(reference); free(full);
    TIDE_REQUIRE(tide_pack_load(pack, pack_size));
    tide_pack_use();
    // Dense replacement must choose ordinary full regions when they encode smaller.
    for (uint32_t i = 0; i < ASSET_BYTES; i++) *(uint8_t *)tide_list_at_mut(list, (int32_t)i, 1u) = 0;
    reference = delta(&heap, NULL, NULL, 0, true, &ref_size);
    full = delta(&heap, NULL, NULL, 0, false, &full_size);
    TIDE_REQUIRE(undelta(&back, NULL, reference, ref_size));
    TIDE_CHECK(ref_size < 1024u && ref_size <= full_size);
    tide_heap_free(&back); tide_heap_free(&snapshot); tide_heap_free(&heap);
    tide_pack_unload(); tide_text_use(NULL, NULL); tide_scratch_reset(0);
    free(reference); free(full); free(pack); free(values);
}
