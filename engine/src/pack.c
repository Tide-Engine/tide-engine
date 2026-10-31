// The pack: a game's assets as the values its `asset` declarations resolve to
// (see tide/pack.h). The builder, here, lays blocks out as a heap would, so
// the code that reads text, lists, maps and grids reads them unchanged. It
// needs nothing of the runtime but the heap's layout, so tidec compiles it
// too; the loader is pack_load.c.

#include "tide/pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/page.h"
#include "tide/sha256.h"

#define OFFSET(at) tide_handle_offset(at)

// How many UTF-8 characters `n` bytes are: those that don't continue one.
static uint32_t utf8_chars(const void *data, const uint32_t n)
{
    const uint8_t *s = data;
    uint32_t count = 0;
    for (uint32_t i = 0; i < n; i++) count += (s[i] & 0xC0u) != 0x80u;
    return count;
}

// A tagged offset into the region.
static tide_handle tagged(const uint64_t offset)
{
    return tide_handle_of(TIDE_IN_SCRATCH, TIDE_PACK_BASE + offset);
}

// ---------------------------------------------------------------------------
// Building

// A block with room for `bytes` after its header, 16-byte aligned, zeroed:
// its offset. Its size class is the heap's for that size, for what reads it.
static uint64_t block(tide_pack_builder *b, const uint64_t bytes)
{
    const uint64_t size = (uint64_t)sizeof(tide_block) + bytes;
    uint32_t c = 0;
    while (c + 1 < TIDE_HEAP_CLASSES && ((uint64_t)16 << c) < size) c++;
    const uint64_t padded = (size + 15u) & ~15ull;
    if (b->size + padded > b->room) {
        uint64_t room = b->room ? b->room : 1u << 16;
        while (room < b->size + padded) room *= 2u;
        if (room > SIZE_MAX) tide_out_of_memory(); // As memory runs out
        b->region = tide_realloc(b->region, (size_t)b->room, (size_t)room);
        b->room = room;
    }
    const uint64_t at = b->size;
    memset(b->region + at, 0, (size_t)padded);
    ((tide_block *)(void *)(b->region + at))->link = tide_block_link(c, 0);
    b->size += padded;
    return at;
}

static tide_block *header(tide_pack_builder *b, const uint64_t offset)
{
    return (tide_block *)(void *)(b->region + offset);
}

static const tide_pack_shared *find_shared(const tide_pack_builder *b, const uint8_t content[32], const uint32_t kind)
{
    for (uint32_t i = 0; i < b->shared_count; i++) {
        if (b->shared[i].kind == kind && memcmp(b->shared[i].content, content, 32) == 0) return &b->shared[i];
    }
    return NULL;
}

static void add_shared(tide_pack_builder *b, const uint8_t content[32], const uint32_t kind, const uint64_t offset,
                       const uint64_t size, const uint32_t extra)
{
    if (b->shared_count == b->shared_room) {
        const uint32_t room = b->shared_room ? b->shared_room * 2u : 16u;
        b->shared = tide_realloc(b->shared, b->shared_room * sizeof *b->shared, room * sizeof *b->shared);
        b->shared_room = room;
    }
    tide_pack_shared *s = &b->shared[b->shared_count++];
    memcpy(s->content, content, 32);
    s->kind = kind;
    s->offset = offset;
    s->size = size;
    s->extra = extra;
}

uint64_t tide_pack_bytes(tide_pack_builder *b, const uint8_t content[32], const void *data, const uint32_t size, uint32_t *chars)
{
    const tide_pack_shared *had = find_shared(b, content, TIDE_PACK_BYTES);
    if (had) {
        *chars = had->extra;
        return had->offset;
    }
    const uint64_t at = block(b, size + 1u); // The NUL after the bytes, so text reads as C text too
    tide_block *h = header(b, at);
    h->a = size; // A list's count
    h->b = size; // ...and its room
    memcpy(h + 1, data, size);
    *chars = utf8_chars(data, size);
    add_shared(b, content, TIDE_PACK_BYTES, at, size, *chars);
    return at;
}

uint64_t tide_pack_string(tide_pack_builder *b, const uint8_t content[32], const void *data, const uint32_t size, uint32_t *chars)
{
    const tide_pack_shared *had = find_shared(b, content, TIDE_PACK_TEXT);
    if (had) {
        *chars = had->extra;
        return had->offset;
    }
    // A byte a character: a list's block of the bytes says of them what a text's would, so they're one
    const uint32_t count = utf8_chars(data, size);
    if (count == size) return tide_pack_bytes(b, content, data, size, chars);
    // Otherwise a block of the text's own: a text's header says its characters (tide_text_view), which a
    // list's has no word for
    const uint64_t at = block(b, size + 1u);
    tide_block *h = header(b, at);
    h->a = size;
    h->b = count;
    memcpy(h + 1, data, size);
    *chars = count;
    add_shared(b, content, TIDE_PACK_TEXT, at, size, count);
    return at;
}

// Whether any of the chunk's cells at (cx, cy) in chunks of the picture is a color other than clear black.
static bool chunk_has_color(const int32_t width, const int32_t height, const tide_color *pixels, const int32_t cx, const int32_t cy,
                            const int32_t across, const int32_t down)
{
    for (int32_t y = cy * down; y < (cy + 1) * down && y < height; y++) {
        const tide_color *row = pixels + (size_t)y * (size_t)width;
        for (int32_t x = cx * across; x < (cx + 1) * across && x < width; x++) {
            if (row[x].r != 0.0f || row[x].g != 0.0f || row[x].b != 0.0f || row[x].a != 0.0f) return true;
        }
    }
    return false;
}

uint64_t tide_pack_grid(tide_pack_builder *b, const uint8_t content[32], const int32_t width, const int32_t height,
                        const tide_color *pixels, const tide_grid_shape *s)
{
    const tide_pack_shared *had = find_shared(b, content, TIDE_PACK_GRID);
    if (had) return had->offset;
    const int32_t across = 1 << s->shift[0], down = 1 << s->shift[1];
    const int32_t chunks_x = (width + across - 1) / across, chunks_y = (height + down - 1) / down;
    // The chunks that have a color in them, as a grid written cell by cell would have
    uint32_t records = 0;
    for (int32_t cy = 0; cy < chunks_y; cy++) {
        for (int32_t cx = 0; cx < chunks_x; cx++) records += chunk_has_color(width, height, pixels, cx, cy, across, down);
    }
    // The directory, as grid.c's new_dir and make_room would leave it: by
    // index with a size on every axis and as many chunks as that lists, else hashed
    const uint64_t total = (uint64_t)chunks_x * (uint64_t)chunks_y;
    const bool flat = total <= (1u << 16);
    uint32_t slots = flat ? (uint32_t)total : 16u;
    const uint32_t room = records ? records : 8u;
    if (!flat) {
        while (slots < room * 2u) slots *= 2u;
    }
    const uint32_t dir_bytes = (uint32_t)(sizeof(tide_grid_dir) + room * sizeof(tide_grid_record) + slots * sizeof(uint32_t));
    const uint64_t at = block(b, dir_bytes);
    tide_block *h = header(b, at);
    // The version the draw list tells the grid by, in the header's words
    uint64_t version = 0;
    memcpy(&version, content, sizeof version);
    h->a = (uint32_t)version;
    h->b = (uint32_t)(version >> 32);
    tide_grid_dir dir = {{width, height, 0}, 2u, records, room, slots, flat, {chunks_x, chunks_y, 1}, 0};
    memcpy(h + 1, &dir, sizeof dir);
    const uint32_t chunk_bytes = tide_grid_chunk_bytes(s);
    uint32_t r = 0;
    for (int32_t cy = 0; cy < chunks_y; cy++) {
        for (int32_t cx = 0; cx < chunks_x; cx++) {
            if (!chunk_has_color(width, height, pixels, cx, cy, across, down)) continue;
            // A chunk's block is all cells, header included (see grid.c)
            const uint64_t chunk = block(b, chunk_bytes - (uint32_t)sizeof(tide_block));
            tide_color *cells = (tide_color *)(void *)(b->region + chunk);
            memset(cells, 0, chunk_bytes);
            for (int32_t y = 0; y < down && cy * down + y < height; y++) {
                const int32_t n = width - cx * across < across ? width - cx * across : across;
                memcpy(cells + (size_t)y * (size_t)across, pixels + ((size_t)(cy * down + y) * (size_t)width + (size_t)cx * across),
                       (size_t)n * sizeof(tide_color));
            }
            tide_grid_dir *d = (tide_grid_dir *)(void *)(header(b, at) + 1); // The region may have moved
            // Where the chunk is as a scratch offset, which is how a grid with no heap reads its chunks (tide_grid_chunk_at)
            tide_grid_records(d)[r] = (tide_grid_record){{cx, cy, 0}, 0u, TIDE_PACK_BASE + chunk};
            uint32_t *table = tide_grid_table(d);
            if (flat) {
                table[(uint32_t)cy * (uint32_t)chunks_x + (uint32_t)cx] = r + 1u;
            } else {
                uint32_t slot = tide_grid_hash(cx, cy, 0) & (slots - 1u);
                while (table[slot]) slot = (slot + 1u) & (slots - 1u);
                table[slot] = r + 1u;
            }
            r++;
        }
    }
    add_shared(b, content, TIDE_PACK_GRID, at, dir_bytes, 0);
    return at;
}

uint64_t tide_pack_texture(tide_pack_builder *b, const uint8_t content[32], const int32_t width, const int32_t height,
                           const uint8_t *rgba)
{
    return tide_pack_texture_blocks(b, content, width, height, rgba, (tide_texture_blocks){0});
}

uint64_t tide_pack_texture_blocks(tide_pack_builder *b, const uint8_t content[32], const int32_t width, const int32_t height,
                                  const uint8_t *rgba, const tide_texture_blocks blocks)
{
    const tide_pack_shared *had = find_shared(b, content, TIDE_PACK_TEXTURE);
    if (had) return had->offset;
    uint32_t levels, compressed_bytes;
    const bool compressed = blocks.pixels && tide_texture_blocks_size(width, height, blocks.format, &levels, &compressed_bytes)
                         && levels == blocks.levels && compressed_bytes == blocks.bytes;
    // No RGBA given beside whole blocks: the image is its blocks alone, which
    // a device without them decodes (tide_texture_blocks_decode)
    const bool rgba_kept = rgba || !compressed;
    const uint64_t bytes = rgba_kept ? (uint64_t)(width > 0 ? width : 0) * (uint64_t)(height > 0 ? height : 0) * 4u : 0u;
    const uint64_t payload = bytes + 2u * sizeof(int32_t) + (compressed ? 16u + blocks.bytes : 0u);
    const uint64_t at = block(b, payload);
    tide_block *h = header(b, at);
    uint64_t version = 0;
    memcpy(&version, content, sizeof version);
    if (!version) version = 1u; // 0 is no image's
    h->a = (uint32_t)version;
    h->b = (uint32_t)(version >> 32);
    const int32_t size[2] = {width, height};
    memcpy(h + 1, size, sizeof size);
    if (bytes && rgba) memcpy((uint8_t *)(h + 1) + sizeof size, rgba, (size_t)bytes);
    if (compressed) {
        const uint32_t metadata[4] = {TIDE_TEXTURE_BLOCK_MAGIC, blocks.format, blocks.levels, blocks.bytes};
        uint8_t *trailer = (uint8_t *)(h + 1) + sizeof size + (size_t)bytes;
        memcpy(trailer, metadata, sizeof metadata);
        memcpy(trailer + sizeof metadata, blocks.pixels, blocks.bytes);
        h->link = tide_block_link(tide_block_class(h), payload);
    }
    add_shared(b, content, TIDE_PACK_TEXTURE, at, bytes, 0);
    return at;
}

uint64_t tide_pack_clip(tide_pack_builder *b, const uint8_t content[32], const void *data, const uint32_t size)
{
    const tide_pack_shared *had = find_shared(b, content, TIDE_PACK_CLIP);
    if (had) return had->offset;
    const uint64_t at = block(b, sizeof(uint32_t) + (uint64_t)size);
    tide_block *h = header(b, at);
    uint64_t version = 0;
    memcpy(&version, content, sizeof version);
    if (!version) version = 1u; // 0 is no sound's
    h->a = (uint32_t)version;
    h->b = (uint32_t)(version >> 32);
    memcpy(h + 1, &size, sizeof size);
    if (size) memcpy((uint8_t *)(h + 1) + sizeof size, data, size);
    add_shared(b, content, TIDE_PACK_CLIP, at, size, 0);
    return at;
}

// A list's block of `count` elements of `element` bytes each, with what's in it said after them (tide/pack.h).
static uint64_t versioned_list(tide_pack_builder *b, const void *elements, const uint32_t element, const uint32_t count,
                               uint64_t version, const uint64_t levels)
{
    const uint64_t bytes = ((uint64_t)element * count + 7u) & ~7ull;
    const uint64_t at = block(b, bytes + 24u); // Then its version, an element's bytes and its levels, 8 bytes each
    tide_block *h = header(b, at);
    h->a = count;
    h->b = count;
    h->link = tide_block_link(tide_block_class(h), sizeof(tide_block) + bytes);
    if (count) memcpy(h + 1, elements, (size_t)element * count);
    if (!version) version = 1u; // 0 is no list's
    // ...and where its levels of detail are, for a mesh's indices that have them: 1 + their block's offset
    const uint64_t said[2] = {element, levels};
    memcpy((uint8_t *)(h + 1) + bytes, &version, sizeof version);
    memcpy((uint8_t *)(h + 1) + bytes + sizeof version, said, sizeof said);
    return at;
}

// A mesh's levels of detail as a block: the ball, how many levels, how many indices, each level's error, first
// index and count, the asset tolerance multiplier, then the indices (LEVELS_HEAD bytes before them, in pack_load.c too).
static uint64_t levels_block(tide_pack_builder *b, const tide_mesh_levels *levels)
{
    uint32_t words[28] = {0};
    memcpy(&words[0], levels->center, sizeof levels->center);
    memcpy(&words[3], &levels->radius, sizeof levels->radius);
    words[4] = levels->count;
    words[5] = levels->index_count;
    // Zero in existing/default metadata means 1; keep default packs byte-identical.
    if (levels->multiplier != 1.0f) memcpy(&words[27], &levels->multiplier, sizeof(float));
    for (uint32_t i = 0; i < levels->count; i++) {
        memcpy(&words[6u + i * 3u], &levels->error[i], sizeof(float));
        words[7u + i * 3u] = levels->first[i];
        words[8u + i * 3u] = levels->indices[i];
    }
    const uint64_t bytes = sizeof words + (uint64_t)levels->index_count * sizeof(uint32_t);
    const uint64_t at = block(b, bytes);
    uint8_t *to = (uint8_t *)(header(b, at) + 1);
    memcpy(to, words, sizeof words);
    memcpy(to + sizeof words, levels->index, (size_t)levels->index_count * sizeof(uint32_t));
    return at;
}

void tide_pack_mesh_levels(tide_pack_builder *b, const uint8_t content[32], const void *vertices, const uint32_t vertex,
                           const uint32_t count, const uint32_t *indices, const uint32_t index_count,
                           const tide_mesh_levels *levels, uint64_t offsets[2])
{
    const tide_pack_shared *had = find_shared(b, content, TIDE_PACK_MESH);
    if (had) {
        offsets[0] = had->offset;
        offsets[1] = had->size;
        return;
    }
    uint64_t versions[2];
    memcpy(versions, content, sizeof versions);
    // Where they are, plus 1: a block can be the region's first, at 0, which is what a mesh with none says
    const uint64_t simpler = levels && levels->count && levels->count <= TIDE_MESH_LEVELS && levels->index ? levels_block(b, levels) + 1u : 0u;
    offsets[0] = versioned_list(b, vertices, vertex, count, versions[0], 0);
    offsets[1] = versioned_list(b, indices, (uint32_t)sizeof(uint32_t), index_count, versions[1], simpler);
    add_shared(b, content, TIDE_PACK_MESH, offsets[0], offsets[1], vertex);
}

void tide_pack_mesh(tide_pack_builder *b, const uint8_t content[32], const void *vertices, const uint32_t vertex, const uint32_t count,
                    const uint32_t *indices, const uint32_t index_count, uint64_t offsets[2])
{
    tide_pack_mesh_levels(b, content, vertices, vertex, count, indices, index_count, NULL, offsets);
}

uint64_t tide_pack_mesh_list(tide_pack_builder *b, const uint64_t *offsets, const uint32_t count)
{
    const uint64_t at = block(b, (uint64_t)count * sizeof(tide_mesh_lists));
    tide_block *h = header(b, at);
    h->a = count;
    h->b = count;
    tide_mesh_lists *meshes = (tide_mesh_lists *)(void *)(h + 1);
    for (uint32_t i = 0; i < count; i++) meshes[i] = (tide_mesh_lists){{tagged(offsets[2u * i])}, {tagged(offsets[2u * i + 1u])}};
    return at;
}

uint64_t tide_pack_skinned_list(tide_pack_builder *b, const uint64_t *offsets, const uint32_t count)
{
    const uint64_t at = block(b, (uint64_t)count * sizeof(tide_skinned_lists));
    tide_block *h = header(b, at);
    h->a = count;
    h->b = count;
    tide_skinned_lists *models = (tide_skinned_lists *)(void *)(h + 1);
    for (uint32_t i = 0; i < count; i++) {
        models[i] = (tide_skinned_lists){{tagged(offsets[3u * i])}, {tagged(offsets[3u * i + 1u])}, {tagged(offsets[3u * i + 2u])}};
    }
    return at;
}

uint64_t tide_pack_list(tide_pack_builder *b, const uint64_t *offsets, const uint32_t count)
{
    const uint64_t at = block(b, (uint64_t)count * sizeof(tide_handle));
    tide_block *h = header(b, at);
    h->a = count;
    h->b = count;
    tide_handle *handles = (tide_handle *)(void *)(h + 1);
    for (uint32_t i = 0; i < count; i++) handles[i] = tagged(offsets[i]);
    return at;
}

// An entry of a map from text, as generated code lays it out: order, the key, the value.
typedef struct text_entry {
    int64_t live;
    tide_handle key;
    tide_handle value;
} text_entry;

// A map's index for `count` entries whose keys are the text blocks at `keys`: its block's offset.
static uint64_t text_index(tide_pack_builder *b, const uint64_t *keys, const uint32_t count)
{
    // A power of two of slots, under half full, as map.c keeps it
    uint32_t slots = 8u;
    while (count * 2u > slots) slots *= 2u;
    const uint64_t ix = block(b, (uint64_t)slots * sizeof(uint32_t));
    tide_block *ih = header(b, ix);
    ih->a = slots;
    ih->b = slots;
    uint32_t *table = (uint32_t *)(void *)(ih + 1);
    for (uint32_t i = 0; i < count; i++) {
        const tide_block *key = header(b, keys[i]);
        uint32_t slot = tide_map_hash_bytes(key + 1, key->a) & (slots - 1u);
        while (table[slot]) slot = (slot + 1u) & (slots - 1u);
        table[slot] = i + 1u;
    }
    return ix;
}

uint64_t tide_pack_map(tide_pack_builder *b, const uint64_t *keys, const uint64_t *values, const uint32_t count, uint64_t *index)
{
    const uint64_t at = block(b, (uint64_t)count * sizeof(text_entry));
    tide_block *h = header(b, at);
    h->a = count;
    h->b = count;
    text_entry *entries = (text_entry *)(void *)(h + 1);
    for (uint32_t i = 0; i < count; i++) entries[i] = (text_entry){(int64_t)i + 1, tagged(keys[i]), tagged(values[i])};
    *index = text_index(b, keys, count);
    return at;
}

// An entry of a map from text to meshes, as generated code lays it out: order, the key, the mesh.
typedef struct mesh_entry {
    int64_t live;
    tide_handle key;
    tide_mesh_lists value;
} mesh_entry;

uint64_t tide_pack_mesh_map(tide_pack_builder *b, const uint64_t *keys, const uint64_t *offsets, const uint32_t count, uint64_t *index)
{
    const uint64_t at = block(b, (uint64_t)count * sizeof(mesh_entry));
    tide_block *h = header(b, at);
    h->a = count;
    h->b = count;
    mesh_entry *entries = (mesh_entry *)(void *)(h + 1);
    for (uint32_t i = 0; i < count; i++) {
        entries[i].live = (int64_t)i + 1;
        entries[i].key = tagged(keys[i]);
        entries[i].value = (tide_mesh_lists){{tagged(offsets[2u * i])}, {tagged(offsets[2u * i + 1u])}};
    }
    *index = text_index(b, keys, count);
    return at;
}

typedef struct skinned_entry {
    int64_t live;
    tide_handle key;
    tide_skinned_lists value;
} skinned_entry;

_Static_assert(sizeof(skinned_entry) == 40u && offsetof(skinned_entry, key) == 8u && offsetof(skinned_entry, value) == 16u,
               "packed skinned map entries must match the generated map layout");

uint64_t tide_pack_skinned_map(tide_pack_builder *b, const uint64_t *keys, const uint64_t *offsets, const uint32_t count,
                              uint64_t *index)
{
    const uint64_t at = block(b, (uint64_t)count * sizeof(skinned_entry));
    tide_block *h = header(b, at);
    h->a = count;
    h->b = count;
    skinned_entry *entries = (skinned_entry *)(void *)(h + 1);
    for (uint32_t i = 0; i < count; i++) {
        entries[i] = (skinned_entry){(int64_t)i + 1, tagged(keys[i]), {{tagged(offsets[3u * i])}, {tagged(offsets[3u * i + 1u])},
                                                        {tagged(offsets[3u * i + 2u])}}};
    }
    *index = text_index(b, keys, count);
    return at;
}

void tide_pack_name(tide_pack_builder *b, const char *qualified, const uint32_t kind, const uint64_t offset, const uint64_t size,
                    const uint32_t extra)
{
    if (b->entry_count == b->entry_room) {
        const uint32_t room = b->entry_room ? b->entry_room * 2u : 16u;
        b->entries = tide_realloc(b->entries, b->entry_room * sizeof *b->entries, room * sizeof *b->entries);
        b->entry_room = room;
    }
    tide_pack_entry *e = &b->entries[b->entry_count++];
    tide_sha256_of(qualified, strlen(qualified), e->name);
    e->kind = kind;
    e->offset = offset;
    e->size = size;
    e->extra = extra;
}

uint8_t *tide_pack_end(tide_pack_builder *b, size_t *size)
{
    const uint64_t table = sizeof(tide_pack_header);
    const uint64_t region_at = (table + (uint64_t)b->entry_count * sizeof(tide_pack_entry) + 15u) & ~(uint64_t)15u;
    if (region_at + b->size > SIZE_MAX) tide_out_of_memory();
    *size = (size_t)(region_at + b->size);
    uint8_t *out = tide_alloc(*size ? *size : 1u);
    memset(out, 0, *size);
    tide_pack_header h = {{0}, TIDE_PACK_VERSION, b->entry_count, region_at, b->size, {0, 0}};
    memcpy(h.magic, TIDE_PACK_MAGIC, 8);
    memcpy(out, &h, sizeof h);
    // A pack of nothing has no entries and no region to copy from
    if (b->entry_count) memcpy(out + table, b->entries, (size_t)b->entry_count * sizeof(tide_pack_entry));
    if (b->size) memcpy(out + region_at, b->region, (size_t)b->size);
    free(b->region);
    free(b->shared);
    free(b->entries);
    *b = (tide_pack_builder){0};
    return out;
}
