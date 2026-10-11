#pragma once

#include "tide/heap.h"
#include "tide/list.h"

// Immutable list payloads. IDs describe element layout, count and exact bytes;
// neither pack offsets nor pointers enter simulation storage.
typedef struct tide_asset_data {
    uint32_t refs, count, element, pack_at;
    uint8_t id[32];
    uint64_t version;
    bool skinned, nonnegative;
    _Alignas(16) uint8_t bytes[];
} tide_asset_data;

typedef struct tide_asset_entry {
    tide_asset_data *data;
    uint32_t users, room;
    uint32_t *blocks; // Descriptor offsets, outside pages; grids reuse headers as cells.
} tide_asset_entry;

typedef struct tide_asset_pool {
    uint32_t refs, count, slots;
    tide_asset_entry entries[];
} tide_asset_pool;

typedef struct tide_asset_retired {
    tide_asset_pool *pool;
    struct tide_asset_retired *next;
} tide_asset_retired;

tide_asset_data *tide_asset_pack(tide_list list, uint32_t element);
tide_asset_data *tide_asset_find(const tide_heap *heap, const uint8_t id[32]);
tide_asset_data *tide_asset_list(tide_list list);
void tide_asset_use(tide_heap *heap, tide_asset_data *data, uint32_t block);
void tide_asset_drop(tide_heap *heap, const uint8_t id[32], uint32_t block);
void tide_asset_pool_retain(tide_asset_pool *pool);
void tide_asset_pool_release(tide_asset_pool *pool);
void tide_asset_settle(tide_heap *heap);
void tide_asset_retain(tide_asset_data *data);
void tide_asset_release(tide_asset_data *data);
void tide_asset_pack_clear(void);

uint32_t tide_asset_packed_size(const tide_heap *heap);
void tide_asset_pack_heap(const tide_heap *heap, tide_writer *writer, bool references, const tide_heap *base);
bool tide_asset_unpack_heap(tide_heap *heap, tide_reader *reader, const tide_heap *base);
bool tide_asset_validate_heap(const tide_heap *heap);

// Serialization hints for detached page-sized list chunks. They are outside
// match state; arbitrary edits still read/write ordinary owned pages.
typedef struct tide_asset_origin {
    tide_asset_data *data;
    uint32_t first, count; // Source bytes after a normal chunk header.
} tide_asset_origin;
typedef struct tide_asset_origins {
    uint32_t refs, room;
    tide_asset_origin pages[];
} tide_asset_origins;
void tide_asset_origins_retain(tide_asset_origins *origins);
void tide_asset_origins_release(tide_asset_origins *origins);
void tide_asset_origin_clear(tide_heap *heap, uint32_t block);
void tide_asset_origin_set(tide_heap *heap, uint32_t page, tide_asset_origin origin);
void tide_asset_origin_list(tide_heap *heap, tide_list list, tide_asset_data *data);
const tide_asset_origin *tide_asset_origin_page(const tide_heap *heap, uint32_t page);
bool tide_asset_origin_reference(const tide_asset_origin *origin);
bool tide_asset_origin_bytes(const tide_asset_origin *origin, void *bytes, uint32_t size);
