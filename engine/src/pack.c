// The pack: a game's assets as the values its `asset` declarations resolve to
// (see tide/pack.h). The builder, here, lays blocks out as a heap would, so
// the code that reads text, lists, maps and grids reads them unchanged. It
// needs nothing of the runtime but the heap's layout, so tidec compiles it
// too; the loader is pack_load.c.

#include "tide/pack.h"

#include <stddef.h>
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

// The region is built where the file will have it: in one allocation, after room for the header and the entries
// (FRONT bytes, which tide_pack_end makes more of for a pack with more entries than that holds), so the file's
// bytes are made with no second copy of a region that can be gigabytes.
#define FRONT (1u << 20)

// Room for `room` bytes of region.
static void make_room(tide_pack_builder *b, const uint64_t room)
{
    const uint64_t front = b->base ? b->front : FRONT;
    if (room > SIZE_MAX - front) tide_out_of_memory(); // As memory runs out
    b->base = tide_realloc(b->base, b->base ? (size_t)(front + b->room) : 0u, (size_t)(front + room));
    b->front = front;
    b->room = room;
    b->region = b->base + front;
}

void tide_pack_reserve(tide_pack_builder *b, const uint64_t bytes)
{
    if (bytes > b->room) make_room(b, bytes);
}

// A block with room for `bytes` after its header, 16-byte aligned, zeroed:
// its offset. Its size class is the heap's for that size, for what reads it.
static uint64_t block(tide_pack_builder *b, const uint64_t bytes)
{
    const uint64_t size = (uint64_t)sizeof(tide_block) + bytes;
    uint32_t c = 0;
    while (c + 1 < TIDE_HEAP_CLASSES && ((uint64_t)16 << c) < size) c++;
    const uint64_t padded = (size + 15u) & ~15ull;
    if (!b->base || b->size + padded > b->room) {
        uint64_t room = b->room ? b->room : 1u << 16;
        while (room < b->size + padded) room *= 2u;
        make_room(b, room);
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

uint64_t tide_pack_values(tide_pack_builder *b, const uint8_t content[32], const void *data, const uint32_t element, const uint32_t count)
{
    const tide_pack_shared *had = find_shared(b, content, TIDE_PACK_BYTES);
    if (had) return had->offset;
    const uint64_t bytes = (uint64_t)element * count;
    const uint64_t at = block(b, bytes ? bytes : 1u);
    tide_block *h = header(b, at);
    h->a = count; // A list's count
    h->b = count; // ...and its room
    if (bytes) memcpy(h + 1, data, (size_t)bytes);
    add_shared(b, content, TIDE_PACK_BYTES, at, bytes, 0);
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

uint64_t tide_pack_font(tide_pack_builder *b, const uint8_t content[32], const void *data, const uint32_t size)
{
    const tide_pack_shared *had = find_shared(b, content, TIDE_PACK_FONT);
    if (had) return had->offset;
    const uint64_t at = block(b, sizeof(uint32_t) + (uint64_t)size);
    tide_block *h = header(b, at);
    uint64_t version = 0;
    memcpy(&version, content, sizeof version);
    if (!version) version = 1u;
    h->a = (uint32_t)version;
    h->b = (uint32_t)(version >> 32);
    memcpy(h + 1, &size, sizeof size);
    if (size) memcpy((uint8_t *)(h + 1) + sizeof size, data, size);
    add_shared(b, content, TIDE_PACK_FONT, at, size, 0);
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

// A file's bytes hashed a piece at a time (TIDE_PACK_DIGEST_PIECE each, the last one what's left), which
// tide_pack_builder.each does at once
typedef struct pieces {
    const uint8_t *bytes;
    uint64_t size;
    uint8_t (*digests)[32];
} pieces;

static void hash_piece(void *context, const uint32_t i)
{
    const pieces *p = context;
    const uint64_t at = (uint64_t)i * TIDE_PACK_DIGEST_PIECE;
    const uint64_t n = p->size - at < TIDE_PACK_DIGEST_PIECE ? p->size - at : TIDE_PACK_DIGEST_PIECE;
    tide_sha256_of(p->bytes + at, (size_t)n, p->digests[i]);
}

void tide_pack_digest_of(const uint8_t *bytes, const uint64_t size, const tide_pack_each each, uint8_t digest[32])
{
    const uint64_t count = (size + TIDE_PACK_DIGEST_PIECE - 1u) / TIDE_PACK_DIGEST_PIECE;
    if (count > UINT32_MAX) tide_out_of_memory(); // More than memory holds
    pieces p = {bytes, size, tide_alloc(count ? (size_t)count * 32u : 32u)};
    if (each) each((uint32_t)count, hash_piece, &p);
    else for (uint32_t i = 0; i < count; i++) hash_piece(&p, i);
    tide_sha256_of(p.digests, (size_t)count * 32u, digest);
    free(p.digests);
}

// Regions this big stay where they were built, with unused bytes between the entries and them, where a smaller
// one is moved next to its entries: a small game's pack has no bytes it doesn't need.
#define GAP_FROM (16u << 20)

uint8_t *tide_pack_end(tide_pack_builder *b, size_t *size)
{
    const uint64_t table = sizeof(tide_pack_header);
    const uint64_t least = (table + (uint64_t)b->entry_count * sizeof(tide_pack_entry) + 15u) & ~(uint64_t)15u;
    uint64_t region_at = b->front;
    if (!b->base || least > b->front || b->size < GAP_FROM) {
        region_at = least;
        if (region_at > SIZE_MAX - b->size) tide_out_of_memory();
        if (!b->base || least > b->front) { // More entries than the front has room for, or no block at all
            uint8_t *base = tide_alloc((size_t)(region_at + b->size) ? (size_t)(region_at + b->size) : 1u);
            if (b->size) memcpy(base + region_at, b->region, (size_t)b->size);
            free(b->base);
            b->base = base;
        } else if (b->size) memmove(b->base + region_at, b->region, (size_t)b->size);
    }
    *size = (size_t)(region_at + b->size);
    uint8_t *out = b->base;
    memset(out, 0, (size_t)region_at);
    tide_pack_header h = {.version = TIDE_PACK_VERSION, .entries = b->entry_count, .region_at = region_at, .region_size = b->size};
    memcpy(h.magic, TIDE_PACK_MAGIC, 8);
    memcpy(out, &h, sizeof h);
    if (b->entry_count) memcpy(out + table, b->entries, (size_t)b->entry_count * sizeof(tide_pack_entry));
    // Its identity, of the file as it is with the digest's bytes zero, so loading needn't hash it
    tide_pack_digest_of(out, *size, b->each, h.digest);
    memcpy(out + offsetof(tide_pack_header, digest), h.digest, sizeof h.digest);
    free(b->shared);
    free(b->entries);
    *b = (tide_pack_builder){0};
    return out;
}

// ---------------------------------------------------------------------------
// A pack compressed (tide/pack.h): its pieces as LZ4 blocks.
//
// A block is sequences: a token (a count of literal bytes above, a match's length less 4 below, each going on in
// bytes of 255 where it's 15), the literals, then how far back the match starts (two bytes, the low one first)
// and the rest of its length. The last sequence is literals alone, at least the block's last 5 bytes, and no match
// starts in its last 12: what lz4's own readers ask of a block.

#define LZ_LEAST 4u        // A match's fewest bytes
#define LZ_FAR 65535u      // The furthest back one starts
#define LZ_HASH_BITS 16u
#define LZ_TRIES 64u       // Earlier places looked at for each match
#define LZ_LAZY_UNDER 128u // A match this long is taken without looking for a longer one a byte on

typedef struct lz_finder {
    const uint8_t *in;
    uint32_t next;   // The first place not in the tables yet
    uint32_t *head;  // By the hash of four bytes: 1 + the last place they were
    uint16_t *chain; // By a place's low 16 bits: how far back the place before it with that hash is, or 0
} lz_finder;

static inline uint32_t lz_hash(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof v);
    return (v * 2654435761u) >> (32u - LZ_HASH_BITS);
}

// How many bytes at `a` and at `b` are the same, of at most `most`.
static inline uint32_t lz_same(const uint8_t *a, const uint8_t *b, const uint32_t most)
{
    uint32_t n = 0;
    while (n + 8u <= most) {
        uint64_t x, y;
        memcpy(&x, a + n, sizeof x);
        memcpy(&y, b + n, sizeof y);
        if (x != y) break;
        n += 8u;
    }
    while (n < most && a[n] == b[n]) n++;
    return n;
}

// The longest match for the bytes at `at`, among the places before it: its length (under LZ_LEAST for none), and
// how far back it starts. Every place before `at` goes into the tables first.
static uint32_t lz_find(lz_finder *f, const uint32_t at, const uint32_t most, uint32_t *back)
{
    for (; f->next < at; f->next++) {
        const uint32_t h = lz_hash(f->in + f->next);
        const uint32_t before = f->head[h];
        const uint32_t far = before ? f->next - (before - 1u) : 0u;
        f->chain[f->next & LZ_FAR] = far <= LZ_FAR ? (uint16_t)far : 0u;
        f->head[h] = f->next + 1u;
    }
    uint32_t best = 0;
    uint32_t place = f->head[lz_hash(f->in + at)];
    for (uint32_t tries = 0; place && tries < LZ_TRIES && best < most; tries++) {
        const uint32_t from = place - 1u;
        if (at - from > LZ_FAR) break;
        if (f->in[from + best] == f->in[at + best]) {
            const uint32_t n = lz_same(f->in + from, f->in + at, most);
            if (n > best) {
                best = n;
                *back = at - from;
            }
        }
        const uint32_t far = f->chain[from & LZ_FAR];
        if (!far || far > from) break;
        place = from - far + 1u;
    }
    return best;
}

// A count that's 15 or more in its token, in bytes of 255: false when they don't fit.
static inline bool lz_put_count(uint8_t *out, uint32_t *o, const uint32_t room, uint32_t rest)
{
    for (; rest >= 255u; rest -= 255u) {
        if (*o >= room) return false;
        out[(*o)++] = 255u;
    }
    if (*o >= room) return false;
    out[(*o)++] = (uint8_t)rest;
    return true;
}

// `size` bytes as an LZ4 block in `out`: its bytes, or 0 when they aren't fewer than `room`.
static uint32_t lz_block(const uint8_t *in, const uint32_t size, uint8_t *out, const uint32_t room, uint32_t *head, uint16_t *chain)
{
    lz_finder f = {in, 0, head, chain};
    uint32_t o = 0, anchor = 0, at = 0;
    while (size >= 13u && at + 12u <= size) {
        const uint32_t most = size - 5u - at;
        uint32_t back = 0, length = lz_find(&f, at, most, &back);
        if (length < LZ_LEAST) {
            at++;
            continue;
        }
        // A longer match a byte on is worth the literal it costs
        while (length < LZ_LAZY_UNDER && at + 13u <= size) {
            uint32_t next_back = 0;
            const uint32_t next = lz_find(&f, at + 1u, size - 5u - (at + 1u), &next_back);
            if (next <= length) break;
            at++;
            length = next;
            back = next_back;
        }
        const uint32_t literals = at - anchor, more = length - LZ_LEAST;
        if (o >= room) return 0;
        out[o++] = (uint8_t)((literals < 15u ? literals : 15u) << 4 | (more < 15u ? more : 15u));
        if (literals >= 15u && !lz_put_count(out, &o, room, literals - 15u)) return 0;
        if (literals > room - o || room - o - literals < 2u) return 0;
        memcpy(out + o, in + anchor, literals);
        o += literals;
        out[o++] = (uint8_t)back;
        out[o++] = (uint8_t)(back >> 8);
        if (more >= 15u && !lz_put_count(out, &o, room, more - 15u)) return 0;
        at += length;
        anchor = at;
    }
    const uint32_t literals = size - anchor;
    if (o >= room) return 0;
    out[o++] = (uint8_t)((literals < 15u ? literals : 15u) << 4);
    if (literals >= 15u && !lz_put_count(out, &o, room, literals - 15u)) return 0;
    if (literals > room - o) return 0;
    memcpy(out + o, in + anchor, literals);
    o += literals;
    return o < room ? o : 0u;
}

typedef struct compressing {
    const uint8_t *bytes;
    uint64_t size;
    uint8_t **pieces; // Each piece's bytes as the file has them
    uint32_t *sizes;
} compressing;

static void compress_piece(void *context, const uint32_t i)
{
    const compressing *c = context;
    const uint64_t at = (uint64_t)i * TIDE_PACK_PIECE;
    const uint32_t n = (uint32_t)(c->size - at < TIDE_PACK_PIECE ? c->size - at : TIDE_PACK_PIECE);
    uint32_t *head = tide_alloc_zeroed((size_t)1u << LZ_HASH_BITS, sizeof *head);
    uint16_t *chain = tide_alloc_zeroed(LZ_FAR + 1u, sizeof *chain);
    uint8_t *out = tide_alloc(n);
    uint32_t made = lz_block(c->bytes + at, n, out, n, head, chain);
    if (!made) { // No smaller: as it is
        memcpy(out, c->bytes + at, n);
        made = n;
    }
    free(head);
    free(chain);
    c->pieces[i] = tide_realloc(out, n, made);
    c->sizes[i] = made;
}

uint8_t *tide_pack_compress(const uint8_t *bytes, const size_t size, const tide_pack_each each, size_t *compressed)
{
    const uint64_t count = ((uint64_t)size + TIDE_PACK_PIECE - 1u) / TIDE_PACK_PIECE;
    if (count > UINT32_MAX) tide_out_of_memory(); // More than memory holds
    compressing c = {bytes, size, tide_alloc_zeroed(count ? (size_t)count : 1u, sizeof(uint8_t *)),
                     tide_alloc_zeroed(count ? (size_t)count : 1u, sizeof(uint32_t))};
    if (each) each((uint32_t)count, compress_piece, &c);
    else for (uint32_t i = 0; i < count; i++) compress_piece(&c, i);
    tide_pack_compressed_header h = {.version = TIDE_PACK_COMPRESSED_VERSION, .pieces = (uint32_t)count, .size = size};
    memcpy(h.magic, TIDE_PACK_COMPRESSED_MAGIC, 8);
    const uint64_t table = sizeof h + count * sizeof(uint32_t);
    uint64_t total = table;
    for (uint32_t i = 0; i < count; i++) total += c.sizes[i];
    uint8_t *out = tide_alloc((size_t)total);
    memcpy(out, &h, sizeof h);
    if (count) memcpy(out + sizeof h, c.sizes, (size_t)count * sizeof(uint32_t));
    uint64_t at = table;
    for (uint32_t i = 0; i < count; i++) {
        memcpy(out + at, c.pieces[i], c.sizes[i]);
        at += c.sizes[i];
        free(c.pieces[i]);
    }
    free(c.pieces);
    free(c.sizes);
    *compressed = (size_t)total;
    return out;
}
