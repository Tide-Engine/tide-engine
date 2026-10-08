// One implementation for tidec's expected hashes and generated raw-pack checks.
// All walks have a fixed asset-kind depth; no loaded-pack state is touched.
#include "tide/pack.h"
#include "tide/page.h"
#include "tide/sha256.h"
#include "tide/skinned_data.h"
#include <stdlib.h>
#include <string.h>

#ifndef TIDE_ASSET_FINGERPRINT_DESC_DEFINED
#define TIDE_ASSET_FINGERPRINT_DESC_DEFINED
typedef struct tide_asset_fingerprint_desc {
    uint32_t kind, leaf_kind, vertex_size;
    tide_grid_shape grid_shape;
} tide_asset_fingerprint_desc;
#endif

#ifndef TIDE_ASSET_FINGERPRINT_REQUEST_DEFINED
#define TIDE_ASSET_FINGERPRINT_REQUEST_DEFINED
typedef struct tide_asset_fingerprint_request {
    const char *qualified;
    tide_asset_fingerprint_desc desc;
} tide_asset_fingerprint_request;
#endif

typedef struct tide_asset_fp_region {
    const uint8_t *bytes;
    uint32_t size;
} tide_asset_fp_region;

typedef struct tide_asset_fp_map_row {
    int64_t order;
    uint32_t key, value;
} tide_asset_fp_map_row;
typedef struct tide_asset_fp_mesh_row {
    int64_t order;
    uint32_t key;
    tide_mesh_lists value;
} tide_asset_fp_mesh_row;
typedef struct tide_asset_fp_skinned_row {
    int64_t order;
    uint32_t key;
    tide_skinned_lists value;
} tide_asset_fp_skinned_row;
_Static_assert(sizeof(tide_asset_fp_map_row) == 16u && sizeof(tide_asset_fp_mesh_row) == 24u
               && sizeof(tide_asset_fp_skinned_row) == 24u && offsetof(tide_asset_fp_map_row, value) == 12u
               && offsetof(tide_asset_fp_mesh_row, value) == 12u && offsetof(tide_asset_fp_skinned_row, value) == 12u,
               "asset fingerprint rows must follow the pack's map ABI");

static const uint8_t *tide_asset_fp_span(const tide_asset_fp_region *r, const uint64_t at, const uint64_t size)
{
    return at <= r->size && size <= r->size - at ? r->bytes + (size_t)at : NULL;
}

static void tide_asset_fp_word(tide_sha256 *hash, const uint32_t value)
{
    const uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    tide_sha256_add(hash, bytes, sizeof bytes);
}

static uint32_t tide_asset_fp_u32(const uint8_t *bytes)
{
    uint32_t word;
    memcpy(&word, bytes, sizeof word);
    return word;
}

static bool tide_asset_fp_handle(const tide_asset_fp_region *r, const uint32_t handle, uint32_t *offset)
{
    const uint32_t at = handle & 0x3fffffffu;
    if (handle >> 30 != TIDE_IN_SCRATCH || at < TIDE_PACK_BASE || at - TIDE_PACK_BASE >= r->size
        || ((at - TIDE_PACK_BASE) & 15u)) return false;
    *offset = at - TIDE_PACK_BASE;
    return true;
}

static bool tide_asset_fp_block(const tide_asset_fp_region *r, const uint32_t at, tide_block *block)
{
    const uint8_t *bytes = (at & 15u) ? NULL : tide_asset_fp_span(r, at, sizeof *block);
    if (!bytes) return false;
    memcpy(block, bytes, sizeof *block);
    return true;
}

static bool tide_asset_fp_flat(const tide_asset_fp_region *r, const uint32_t at, const uint32_t stride,
                               uint32_t *count, const uint8_t **data)
{
    tide_block b;
    if (!stride || !tide_asset_fp_block(r, at, &b) || b.a != b.b || (b.b & TIDE_LIST_CHUNKED)) return false;
    *data = tide_asset_fp_span(r, (uint64_t)at + sizeof b, (uint64_t)b.a * stride);
    if (!*data) return false;
    *count = b.a;
    return true;
}

static bool tide_asset_fp_text(const tide_asset_fp_region *r, const uint32_t at, tide_sha256 *hash,
                               uint32_t *length, uint32_t *chars)
{
    tide_block b;
    if (!tide_asset_fp_block(r, at, &b) || b.a > INT32_MAX || b.b > b.a) return false;
    const uint8_t *data = tide_asset_fp_span(r, (uint64_t)at + sizeof b, (uint64_t)b.a + 1u);
    if (!data || data[b.a]) return false;
    tide_asset_fp_word(hash, b.a);
    tide_asset_fp_word(hash, b.b);
    tide_sha256_add(hash, data, b.a);
    if (length) *length = b.a;
    if (chars) *chars = b.b;
    return true;
}

static bool tide_asset_fp_grid(const tide_asset_fp_region *r, const uint32_t at,
                               const tide_grid_shape *shape, tide_sha256 *hash)
{
    tide_block b;
    if (shape->cell != sizeof(tide_color) || shape->dims != 2u || shape->shift[0] > 30u
        || shape->shift[1] > 30u || shape->shift[2] || shape->shift[0] + shape->shift[1] > 30u
        || !tide_asset_fp_block(r, at, &b)) return false;
    const uint64_t chunk_bytes = (uint64_t)shape->cell << (shape->shift[0] + shape->shift[1]);
    const uint8_t *head = tide_asset_fp_span(r, (uint64_t)at + sizeof b, sizeof(tide_grid_dir));
    if (!head) return false;
    tide_grid_dir d;
    memcpy(&d, head, sizeof d);
    if (d.size[0] <= 0 || d.size[1] <= 0 || d.size[2] || d.dims != 2u || d.records > d.room || d.flat > 1u) return false;
    const uint32_t across = 1u << shape->shift[0], down = 1u << shape->shift[1];
    const uint32_t chunks_x = (uint32_t)(((uint64_t)d.size[0] + across - 1u) / across);
    const uint32_t chunks_y = (uint32_t)(((uint64_t)d.size[1] + down - 1u) / down);
    if (d.chunks[0] != (int32_t)chunks_x || d.chunks[1] != (int32_t)chunks_y || d.chunks[2] != 1
        || !d.slots || (d.flat && (uint64_t)chunks_x * chunks_y != d.slots)
        || (!d.flat && ((d.slots & (d.slots - 1u)) || (uint64_t)d.records * 2u > d.slots))) return false;
    const uint64_t records_at = (uint64_t)at + sizeof b + sizeof d;
    const uint8_t *records = tide_asset_fp_span(r, records_at, (uint64_t)d.room * sizeof(tide_grid_record));
    const uint8_t *table = tide_asset_fp_span(r, records_at + (uint64_t)d.room * sizeof(tide_grid_record),
                                             (uint64_t)d.slots * sizeof(uint32_t));
    if (!records || !table) return false;
    tide_asset_fp_word(hash, shape->cell);
    tide_asset_fp_word(hash, shape->shift[0]);
    tide_asset_fp_word(hash, shape->shift[1]);
    tide_asset_fp_word(hash, (uint32_t)d.size[0]);
    tide_asset_fp_word(hash, (uint32_t)d.size[1]);
    tide_asset_fp_word(hash, d.records);
    tide_asset_fp_word(hash, d.flat);
    tide_asset_fp_word(hash, d.slots);
    uint32_t occupied = 0;
    for (uint32_t i = 0; i < d.slots; i++) {
        const uint32_t record = tide_asset_fp_u32(table + (size_t)i * sizeof(uint32_t));
        if (record > d.records) return false;
        occupied += record != 0u;
    }
    if (occupied != d.records) return false;
    tide_sha256_add(hash, table, (size_t)d.slots * sizeof(uint32_t));
    for (uint32_t i = 0; i < d.records; i++) {
        tide_grid_record record;
        memcpy(&record, records + (size_t)i * sizeof record, sizeof record);
        if (record.chunk[0] < 0 || record.chunk[1] < 0 || record.chunk[2]
            || (uint32_t)record.chunk[0] >= chunks_x || (uint32_t)record.chunk[1] >= chunks_y
            || record.block < TIDE_PACK_BASE || ((record.block - TIDE_PACK_BASE) & 15u)) return false;
        const uint8_t *cells = tide_asset_fp_span(r, record.block - TIDE_PACK_BASE, chunk_bytes);
        if (!cells) return false;
        if (d.flat && tide_asset_fp_u32(table + ((size_t)record.chunk[1] * chunks_x + (uint32_t)record.chunk[0]) * sizeof(uint32_t))
                          != i + 1u) return false;
        tide_asset_fp_word(hash, (uint32_t)record.chunk[0]);
        tide_asset_fp_word(hash, (uint32_t)record.chunk[1]);
        // Authenticate the builder's complete canonical chunk payload. Its
        // unused edge cells are zero; this does not normalize custom grids.
        tide_sha256_add(hash, cells, (size_t)chunk_bytes);
    }
    return true;
}

static bool tide_asset_fp_leaf_kind(const uint32_t kind)
{
    return kind == TIDE_PACK_TEXT || kind == TIDE_PACK_BYTES || kind == TIDE_PACK_GRID
        || kind == TIDE_PACK_TEXTURE || kind == TIDE_PACK_CLIP;
}

static bool tide_asset_fp_leaf(const tide_asset_fp_region *r, const uint32_t at, const uint32_t kind,
                               const tide_asset_fingerprint_desc *desc, tide_sha256 *hash)
{
    tide_asset_fp_word(hash, kind);
    if (kind == TIDE_PACK_TEXT) return tide_asset_fp_text(r, at, hash, NULL, NULL);
    if (kind == TIDE_PACK_GRID) return tide_asset_fp_grid(r, at, &desc->grid_shape, hash);
    if (kind == TIDE_PACK_BYTES) {
        uint32_t count;
        const uint8_t *data;
        if (!tide_asset_fp_flat(r, at, 1u, &count, &data)) return false;
        tide_asset_fp_word(hash, count);
        tide_sha256_add(hash, data, count);
        return true;
    }
    tide_block b;
    if (!tide_asset_fp_block(r, at, &b)) return false;
    if (kind == TIDE_PACK_TEXTURE) {
        const uint8_t *size = tide_asset_fp_span(r, (uint64_t)at + sizeof b, 2u * sizeof(int32_t));
        if (!size) return false;
        int32_t width, height;
        memcpy(&width, size, sizeof width);
        memcpy(&height, size + sizeof width, sizeof height);
        if (width <= 0 || height <= 0) return false;
        const uint64_t bytes = (uint64_t)width * (uint32_t)height * 4u;
        const uint8_t *pixels = tide_asset_fp_span(r, (uint64_t)at + sizeof b + 2u * sizeof(int32_t), bytes);
        if (!pixels) return false;
        tide_asset_fp_word(hash, (uint32_t)width);
        tide_asset_fp_word(hash, (uint32_t)height);
        tide_sha256_add(hash, pixels, (size_t)bytes);
        return true;
    }
    if (kind == TIDE_PACK_CLIP) {
        const uint8_t *size = tide_asset_fp_span(r, (uint64_t)at + sizeof b, sizeof(uint32_t));
        if (!size) return false;
        const uint32_t bytes = tide_asset_fp_u32(size);
        const uint8_t *data = tide_asset_fp_span(r, (uint64_t)at + sizeof b + sizeof(uint32_t), bytes);
        if (bytes > INT32_MAX || !data) return false;
        tide_asset_fp_word(hash, bytes);
        tide_sha256_add(hash, data, bytes);
        return true;
    }
    return false;
}

static bool tide_asset_fp_versioned(const tide_asset_fp_region *r, const uint32_t at, const uint32_t stride,
                                    uint32_t *count, const uint8_t **data)
{
    tide_block b;
    if (!tide_asset_fp_flat(r, at, stride, count, data) || !tide_asset_fp_block(r, at, &b)
        || (uint64_t)sizeof b + (uint64_t)*count * stride > b.next) return false;
    const uint8_t *trailer = tide_asset_fp_span(r, (uint64_t)at + b.next, 16u);
    return trailer && tide_asset_fp_u32(trailer + sizeof(uint64_t)) == stride;
}

static bool tide_asset_fp_mesh(const tide_asset_fp_region *r, const uint32_t vertices, const uint32_t indices,
                               const tide_asset_fingerprint_desc *desc, tide_sha256 *hash,
                               uint32_t *vertex_count, uint32_t *index_count)
{
    const uint8_t *v, *ix;
    if (!desc->vertex_size || !tide_asset_fp_versioned(r, vertices, desc->vertex_size, vertex_count, &v)
        || !tide_asset_fp_versioned(r, indices, sizeof(uint32_t), index_count, &ix)) return false;
    for (uint32_t i = 0; i < *index_count; i++) {
        if (tide_asset_fp_u32(ix + (size_t)i * sizeof(uint32_t)) >= *vertex_count) return false;
    }
    tide_asset_fp_word(hash, desc->vertex_size);
    tide_asset_fp_word(hash, *vertex_count);
    tide_sha256_add(hash, v, (size_t)*vertex_count * desc->vertex_size);
    tide_asset_fp_word(hash, *index_count);
    tide_sha256_add(hash, ix, (size_t)*index_count * sizeof(uint32_t));
    return true;
}

static bool tide_asset_fp_model(const tide_asset_fp_region *r, const uint8_t *row, const bool skinned,
                                const tide_asset_fingerprint_desc *desc, tide_sha256 *hash)
{
    uint32_t data = 0, vertices, indices;
    if (skinned && !tide_asset_fp_handle(r, tide_asset_fp_u32(row), &data)) return false;
    if (!tide_asset_fp_handle(r, tide_asset_fp_u32(row + (skinned ? 4u : 0u)), &vertices)
        || !tide_asset_fp_handle(r, tide_asset_fp_u32(row + (skinned ? 8u : 4u)), &indices)) return false;
    uint32_t vertex_count, index_count;
    if (!tide_asset_fp_mesh(r, vertices, indices, desc, hash, &vertex_count, &index_count)) return false;
    if (skinned) {
        uint32_t count;
        const uint8_t *bytes;
        tide_skinned_data_view view;
        if (!tide_asset_fp_flat(r, data, 1u, &count, &bytes) || !tide_skinned_data_open(bytes, count, &view)
            || view.vertices != vertex_count || view.indices != index_count) return false;
        tide_asset_fp_word(hash, count);
        tide_sha256_add(hash, bytes, count);
    }
    return true;
}

static bool tide_asset_fp_map(const tide_asset_fp_region *r, const tide_pack_entry *e,
                              const tide_asset_fingerprint_desc *desc, tide_sha256 *hash)
{
    const bool mesh = e->kind == TIDE_PACK_MESHES, skinned = e->kind == TIDE_PACK_SKINNED_MAP;
    const uint32_t stride = skinned ? (uint32_t)sizeof(tide_asset_fp_skinned_row)
                                   : mesh ? (uint32_t)sizeof(tide_asset_fp_mesh_row) : (uint32_t)sizeof(tide_asset_fp_map_row);
    const uint32_t leaf = e->kind == TIDE_PACK_TEXTURES ? TIDE_PACK_TEXTURE : e->kind == TIDE_PACK_CLIPS ? TIDE_PACK_CLIP : desc->leaf_kind;
    if (!mesh && !skinned && !tide_asset_fp_leaf_kind(leaf)) return false;
    uint32_t count, slots;
    const uint8_t *rows, *table;
    if (!tide_asset_fp_flat(r, e->offset, stride, &count, &rows)
        || (!mesh && !skinned && e->extra != count)
        || !tide_asset_fp_flat(r, e->size, sizeof(uint32_t), &slots, &table)
        || slots < 8u || (slots & (slots - 1u)) || (uint64_t)count * 2u > slots) return false;
    tide_asset_fp_word(hash, count);
    tide_asset_fp_word(hash, slots);
    uint32_t occupied = 0;
    for (uint32_t i = 0; i < slots; i++) {
        const uint32_t row = tide_asset_fp_u32(table + (size_t)i * sizeof(uint32_t));
        if (row > count) return false;
        occupied += row != 0u;
    }
    if (occupied != count) return false;
    tide_sha256_add(hash, table, (size_t)slots * sizeof(uint32_t));
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *row = rows + (size_t)i * stride;
        int64_t order;
        uint32_t key;
        memcpy(&order, row, sizeof order);
        if (order != (int64_t)i + 1 || !tide_asset_fp_handle(r, tide_asset_fp_u32(row + 8u), &key)
            || !tide_asset_fp_text(r, key, hash, NULL, NULL)) return false;
        tide_asset_fp_word(hash, (uint32_t)order);
        if (mesh || skinned) {
            if (!tide_asset_fp_model(r, row + 12u, skinned, desc, hash)) return false;
        } else {
            uint32_t value;
            if (!tide_asset_fp_handle(r, tide_asset_fp_u32(row + 12u), &value)
                || !tide_asset_fp_leaf(r, value, leaf, desc, hash)) return false;
        }
    }
    return true;
}

static bool tide_asset_fp_entry(const tide_asset_fp_region *r, const tide_pack_entry *entry,
                                const tide_asset_fingerprint_desc *desc, uint8_t out[32])
{
    const tide_pack_entry e = *entry;
    const tide_asset_fp_region region = *r;
    tide_sha256 hash;
    tide_sha256_init(&hash);
    tide_sha256_add(&hash, "Tide asset content 1", sizeof("Tide asset content 1") - 1u);
    tide_asset_fp_word(&hash, e.kind);
    bool ok = false;
    if (tide_asset_fp_leaf_kind(e.kind)) {
        if (e.kind == TIDE_PACK_TEXT) {
            uint32_t length, chars;
            ok = tide_asset_fp_text(&region, e.offset, &hash, &length, &chars) && length == e.size && chars == e.extra;
        } else if (e.kind == TIDE_PACK_BYTES) {
            tide_block b;
            ok = tide_asset_fp_block(&region, e.offset, &b) && b.a == e.size
                && tide_asset_fp_leaf(&region, e.offset, e.kind, desc, &hash);
        } else ok = tide_asset_fp_leaf(&region, e.offset, e.kind, desc, &hash);
    } else if (e.kind == TIDE_PACK_MAP || e.kind == TIDE_PACK_TEXTURES || e.kind == TIDE_PACK_CLIPS
               || e.kind == TIDE_PACK_MESHES || e.kind == TIDE_PACK_SKINNED_MAP) {
        if (e.kind == TIDE_PACK_MESHES || e.kind == TIDE_PACK_SKINNED_MAP) tide_asset_fp_word(&hash, e.extra);
        ok = tide_asset_fp_map(&region, &e, desc, &hash);
    } else if (e.kind == TIDE_PACK_MESH) {
        uint32_t vertices, indices;
        tide_asset_fp_word(&hash, e.extra);
        ok = tide_asset_fp_mesh(&region, e.offset, e.size, desc, &hash, &vertices, &indices);
    } else if (e.kind == TIDE_PACK_LIST || e.kind == TIDE_PACK_CUBEMAP || e.kind == TIDE_PACK_MESH_LIST
               || e.kind == TIDE_PACK_SKINNED || e.kind == TIDE_PACK_SKINNEDS) {
        const bool skinned = e.kind == TIDE_PACK_SKINNED || e.kind == TIDE_PACK_SKINNEDS;
        const bool mesh = e.kind == TIDE_PACK_MESH_LIST;
        const uint32_t stride = skinned ? (uint32_t)sizeof(tide_skinned_lists)
                                       : mesh ? (uint32_t)sizeof(tide_mesh_lists) : (uint32_t)sizeof(uint32_t);
        const uint32_t leaf = e.kind == TIDE_PACK_CUBEMAP ? TIDE_PACK_TEXTURE : desc->leaf_kind;
        uint32_t count;
        const uint8_t *rows;
        if (tide_asset_fp_flat(&region, e.offset, stride, &count, &rows) && count == e.size
            && (e.kind != TIDE_PACK_SKINNED || count == 1u) && (e.kind != TIDE_PACK_CUBEMAP || count == TIDE_CUBE_FACES)
            && ((mesh || skinned) || (tide_asset_fp_leaf_kind(leaf) && (e.kind != TIDE_PACK_LIST || e.extra == leaf)))) {
            tide_asset_fp_word(&hash, count);
            if (mesh || skinned) tide_asset_fp_word(&hash, e.extra);
            ok = true;
            for (uint32_t i = 0; ok && i < count; i++) {
                const uint8_t *row = rows + (size_t)i * stride;
                if (mesh || skinned) ok = tide_asset_fp_model(&region, row, skinned, desc, &hash);
                else {
                    uint32_t at;
                    ok = tide_asset_fp_handle(&region, tide_asset_fp_u32(row), &at)
                        && tide_asset_fp_leaf(&region, at, leaf, desc, &hash);
                }
            }
        }
    }
    if (ok) tide_sha256_end(&hash, out);
    return ok;
}

typedef struct tide_asset_fp_selected {
    uint8_t name[32];
    uint32_t entry; // 0 missing, index + 1 found, UINT32_MAX duplicated
} tide_asset_fp_selected;

// Select all requested names in one directory pass. The index holds request
// places, never pointers into a loaded pack, and survives only for this call.
static bool tide_asset_fingerprints(const uint8_t *bytes, const size_t size,
                                    const tide_asset_fingerprint_request *requests, const uint32_t count,
                                    uint8_t (*out)[32], const uint8_t (*expected)[32], uint32_t *failed)
{
    if (failed) *failed = UINT32_MAX;
    if (!bytes || size < sizeof(tide_pack_header)
        || (count && (!requests || ((out == NULL) == (expected == NULL))))) return false;
    tide_pack_header h;
    memcpy(&h, bytes, sizeof h);
    const uint64_t table_end = (uint64_t)sizeof h + (uint64_t)h.entries * sizeof(tide_pack_entry);
    if (memcmp(h.magic, TIDE_PACK_MAGIC, sizeof h.magic) || h.version != TIDE_PACK_VERSION
        || table_end > h.region_at || (h.region_at & 15u) || h.region_size > TIDE_PACK_BASE
        || (uint64_t)h.region_at + h.region_size != size || count > h.entries) return false;
    if (!count) return true;

    uint64_t slots = 8u;
    while (slots < (uint64_t)count * 2u) slots *= 2u;
    const uint64_t scratch_size = (uint64_t)count * sizeof(tide_asset_fp_selected) + slots * sizeof(uint32_t);
    if (slots > UINT32_MAX || scratch_size > SIZE_MAX || (uint64_t)count * 32u > SIZE_MAX
        || (uint64_t)count * sizeof *requests > SIZE_MAX) return false;
    tide_asset_fp_selected *selected = malloc((size_t)scratch_size);
    if (!selected) return false;
    tide_memory_sync(); // Before clearing memory a WebAssembly worker just grew
    memset(selected, 0, (size_t)scratch_size);
    uint32_t *index = (uint32_t *)(void *)(selected + count);
    const uint32_t mask = (uint32_t)slots - 1u;
    bool ok = true;
    for (uint32_t i = 0; ok && i < count; i++) {
        if (!requests[i].qualified) {
            if (failed) *failed = i;
            ok = false;
            break;
        }
        tide_sha256_of(requests[i].qualified, strlen(requests[i].qualified), selected[i].name);
        uint32_t place = tide_asset_fp_u32(selected[i].name) & mask;
        while (index[place]) {
            if (!memcmp(selected[index[place] - 1u].name, selected[i].name, sizeof selected[i].name)) {
                if (failed) *failed = i;
                ok = false; // The caller must select a set of unique names.
                break;
            }
            place = (place + 1u) & mask;
        }
        if (ok) index[place] = i + 1u;
    }
    for (uint32_t i = 0; ok && i < h.entries; i++) {
        const uint8_t *name = bytes + sizeof h + (size_t)i * sizeof(tide_pack_entry);
        uint32_t place = tide_asset_fp_u32(name) & mask;
        while (index[place]) {
            tide_asset_fp_selected *s = &selected[index[place] - 1u];
            if (!memcmp(name, s->name, sizeof s->name)) {
                s->entry = s->entry ? UINT32_MAX : i + 1u;
                break;
            }
            place = (place + 1u) & mask;
        }
    }
    const tide_asset_fp_region region = {bytes + h.region_at, h.region_size};
    for (uint32_t i = 0; ok && i < count; i++) {
        tide_pack_entry e;
        uint8_t digest[32];
        if (!selected[i].entry || selected[i].entry == UINT32_MAX) ok = false;
        else {
            memcpy(&e, bytes + sizeof h + (size_t)(selected[i].entry - 1u) * sizeof e, sizeof e);
            ok = e.kind == requests[i].desc.kind && tide_asset_fp_entry(&region, &e, &requests[i].desc, digest)
                && (!expected || !memcmp(digest, expected[i], sizeof digest));
        }
        if (!ok) {
            if (failed) *failed = i;
        } else if (out) memcpy(out[i], digest, sizeof digest);
    }
    free(selected);
    return ok;
}
