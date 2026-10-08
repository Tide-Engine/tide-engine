#include "tide/mesh.h"

#include <stdio.h>
#include <string.h>

#include "tide/math.h"
#include "tide/net.h"
#include "tide/pack.h"

static float unit(const float v)
{
    if (!(v > 0.0f)) return 0.0f; // Also NaN
    return v >= 1.0f ? 1.0f : v;
}

// A color's channel as a texture has it: times the color's alpha (see
// tide_gpu_blend), so that what a GPU blends between two pixels is right,
// whatever color a clear one has.
static uint8_t channel(const float v, const float alpha)
{
    return (uint8_t)(unit(v) * alpha * 255.0f + 0.5f);
}

uint16_t tide_half_from_float(const float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    const uint32_t sign = bits >> 16 & 0x8000u;
    const uint32_t exponent = bits >> 23 & 255u;
    uint32_t mantissa = bits & 0x7FFFFFu;
    if (exponent == 255u) return mantissa ? 0u : (uint16_t)(sign | 0x7BFFu); // NaN, and either infinity
    if (exponent > 142u) return (uint16_t)(sign | 0x7BFFu);                   // 65536 and past it
    if (exponent < 102u) return (uint16_t)sign;                               // Under half of the least a half holds
    uint32_t half, rest, tie;
    if (exponent < 113u) { // The halves with no leading one: what's left of the float's, shifted down
        mantissa |= 0x800000u;
        const uint32_t shift = 126u - exponent; // 14 to 24
        half = mantissa >> shift;
        rest = mantissa & ((1u << shift) - 1u);
        tie = 1u << (shift - 1u);
    } else {
        half = (exponent - 112u) << 10 | mantissa >> 13;
        rest = mantissa & 0x1FFFu;
        tie = 0x1000u;
    }
    if (rest > tie || (rest == tie && (half & 1u))) half++; // Carries into the exponent as it should
    if (half > 0x7BFFu) half = 0x7BFFu;
    return (uint16_t)(sign | half);
}

// `bytes` bytes of a grid's memory from `offset`, where they're all within
// one page of its heap, or within the pack's region for a grid of the pack's
// (`heap` is NULL): NULL otherwise. A grid's own blocks always are. What a
// Texture kept past its grid names may be anything, so nothing is read of it
// that this didn't give.
static const uint8_t *span(const tide_heap *heap, const uint32_t offset, const uint64_t bytes)
{
    if (!heap) return tide_pack_span(offset, bytes);
    if (!offset || (uint64_t)offset + bytes > heap->used || offset >> TIDE_HEAP_PAGE_SHIFT >= heap->pages) return NULL;
    const tide_page *p = heap->page[offset >> TIDE_HEAP_PAGE_SHIFT];
    if (!p) return NULL;
    const uint64_t first = (uint64_t)p->first << TIDE_HEAP_PAGE_SHIFT;
    if (offset < first || (uint64_t)offset + bytes > first + p->size) return NULL;
    return (const uint8_t *)tide_page_data(p) + (offset - first);
}

// A chunk's place in the grid's pixels, where it has one: its first cell.
static bool chunk_place(const tide_grid_record *record, const tide_grid_shape *s, const int32_t width, const int32_t height,
                        int32_t *x0, int32_t *y0)
{
    const int64_t x = (int64_t)record->chunk[0] << s->shift[0], y = (int64_t)record->chunk[1] << s->shift[1];
    if (record->chunk[2] != 0 || x < 0 || y < 0 || x >= width || y >= height) return false;
    *x0 = (int32_t)x;
    *y0 = (int32_t)y;
    return true;
}

// The grid's cells as pixels: its chunks' colors, a byte a channel, or its
// floats, 16 bits each, and zeros where it has no chunk.
static void grid_pixels(const int32_t width, const int32_t height, const tide_grid_record *records, const uint32_t count,
                        const tide_heap *heap, const tide_grid_shape *s, const bool raw, uint8_t *pixels)
{
    const bool colors = s->cell == sizeof(tide_color);
    const size_t pixel = colors ? 4u : 2u;
    memset(pixels, 0, (size_t)width * (size_t)height * pixel);
    const int32_t across = 1 << s->shift[0], down = 1 << s->shift[1];
    for (uint32_t r = 0; r < count; r++) {
        int32_t x0, y0;
        const uint8_t *cells = span(heap, records[r].block, tide_grid_chunk_bytes(s));
        if (!cells || !chunk_place(&records[r], s, width, height, &x0, &y0)) continue;
        for (int32_t y = 0; y < down && y0 + y < height; y++) {
            uint8_t *out = pixels + ((size_t)(y0 + y) * (size_t)width + (size_t)x0) * pixel;
            const uint8_t *in = cells + (size_t)y * (size_t)across * s->cell;
            if (colors) {
                const tide_color *row = (const tide_color *)(const void *)in;
                for (int32_t x = 0; x < across && x0 + x < width; x++, out += 4) {
                    const float alpha = unit(row[x].a), worked = raw ? 1.0f : alpha;
                    out[0] = channel(row[x].r, worked);
                    out[1] = channel(row[x].g, worked);
                    out[2] = channel(row[x].b, worked);
                    out[3] = channel(1.0f, alpha);
                }
            } else {
                const float *row = (const float *)(const void *)in;
                for (int32_t x = 0; x < across && x0 + x < width; x++, out += 2) {
                    const uint16_t half = tide_half_from_float(row[x]);
                    memcpy(out, &half, sizeof half);
                }
            }
        }
    }
}

static uint32_t grid_texture(tide_draw_list *d, const tide_grid g, const tide_grid_shape *s, const bool raw)
{
    const bool colors = s->cell == sizeof(tide_color);
    // A chunk is a page at the most, which is also what keeps its size in an int
    if (!g.at || s->dims != 2u || (!colors && s->cell != sizeof(float)) || s->shift[0] + s->shift[1] > TIDE_HEAP_PAGE_SHIFT
        || s->shift[2] != 0u || tide_grid_chunk_bytes(s) > 1u << TIDE_HEAP_PAGE_SHIFT) {
        return 0;
    }
    // The confirmed world's too: only its pages' hashes are asked of it, which it keeps
    tide_heap *heap = (tide_heap *)(uintptr_t)tide_heap_read(g.at >> 30);
    const bool packed = !heap && tide_pack_holds(g.at); // The pack's (tide/pack.h): its chunks in the scratch area's slots
    if (!heap && !packed) return 0;
    const uint32_t at = g.at & 0x3FFFFFFFu;
    const uint8_t *block = span(heap, at, sizeof(tide_block) + sizeof(tide_grid_dir));
    if (!block) return 0;
    tide_grid_dir dir;
    memcpy(&dir, block + sizeof(tide_block), sizeof dir);
    if (dir.dims != 2u || dir.size[0] <= 0 || dir.size[1] <= 0 || dir.records > dir.room) return 0;
    // No GPU has a texture bigger, and bytes that aren't a grid's could say any size
    if (dir.size[0] > TIDE_GRID_TEXTURE_MOST || dir.size[1] > TIDE_GRID_TEXTURE_MOST) return 0;
    // A grid with a size on both axes has as many chunks as cover it, and no more of them than that
    const uint64_t across = ((uint64_t)dir.size[0] + (1u << s->shift[0]) - 1u) >> s->shift[0];
    const uint64_t down = ((uint64_t)dir.size[1] + (1u << s->shift[1]) - 1u) >> s->shift[1];
    if (dir.records > across * down) return 0;
    if (dir.flat && ((uint64_t)dir.chunks[0] != across || (uint64_t)dir.chunks[1] != down)) return 0;
    const tide_grid_record *records = (const tide_grid_record *)(const void *)
        span(heap, at + (uint32_t)(sizeof(tide_block) + sizeof(tide_grid_dir)), (uint64_t)dir.records * sizeof(tide_grid_record));
    if (dir.records && !records) return 0;
    const uint32_t format = colors ? TIDE_TEXELS_BYTES : TIDE_TEXELS_HALF;
    uint64_t version;
    if (packed) { // Its version is its file's, in its directory's header, as the builder put it
        version = tide_pack_grid_version(g);
    } else {
        // What its cells are: its size, and each chunk's place and hash, which its
        // heap keeps until the chunk changes
        const bool whole_pages = tide_grid_chunk_bytes(s) == 1u << TIDE_HEAP_PAGE_SHIFT;
        version = tide_hash_more(TIDE_HASH_START, dir.size, sizeof dir.size);
        for (uint32_t r = 0; r < dir.records; r++) {
            const uint8_t *chunk = span(heap, records[r].block, tide_grid_chunk_bytes(s));
            const uint64_t cells = !chunk ? 0u : whole_pages ? tide_heap_block_hash(heap, records[r].block)
                                                             : tide_hash(chunk, tide_grid_chunk_bytes(s));
            version = tide_hash_more(version, records[r].chunk, sizeof records[r].chunk);
            version = tide_hash_more(version, &cells, sizeof cells);
        }
        version = tide_hash_end(version);
    }
    if (raw) version ^= UINT64_C(0x5241575241575241); // Other bytes than the same cells as colors
    uint8_t *pixels;
    const uint32_t slot = tide_draw_texture_slot_as(d, TIDE_PIXELS_GRID, g.at, version, dir.size[0], dir.size[1], format, &pixels);
    if (pixels) grid_pixels(dir.size[0], dir.size[1], records, dir.records, packed ? NULL : heap, s, raw, pixels);
    return slot;
}

uint32_t tide_draw_grid_texture(tide_draw_list *d, const tide_grid g, const tide_grid_shape *s)
{
    return grid_texture(d, g, s, false);
}

uint32_t tide_draw_grid_texture_raw(tide_draw_list *d, const tide_grid g, const tide_grid_shape *s)
{
    return grid_texture(d, g, s, true);
}

static void mesh_lists(tide_draw_list *d, const tide_list vertices, const tide_list indices, const uint32_t slot,
                       const tide_filter filter, const tide_wrap wrap)
{
    const int32_t vertex_count = tide_list_count(vertices);
    const int32_t index_count = tide_list_count(indices);
    if (vertex_count <= 0 || index_count < 3) return;
    // A list's elements side by side, as they are unless it's a world's big one
    bool vertices_copied, indices_copied;
    tide_vertex *v = tide_list_flatten(vertices, sizeof(tide_vertex), false, &vertices_copied);
    uint32_t *i = tide_list_flatten(indices, sizeof(uint32_t), false, &indices_copied);
    tide_draw_triangles_with(d, tide_draw_vertices(d, v, (uint32_t)vertex_count), i, (uint32_t)index_count, slot, filter,
                             wrap);
    tide_list_unflatten(indices, i, sizeof(uint32_t), false, indices_copied);
    tide_list_unflatten(vertices, v, sizeof(tide_vertex), false, vertices_copied);
}

void tide_draw_mesh_lists(tide_draw_list *d, const tide_list vertices, const tide_list indices)
{
    mesh_lists(d, vertices, indices, 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
}

void tide_draw_mesh_texture(tide_draw_list *d, const tide_list vertices, const tide_list indices,
                            const tide_texture_ref texture)
{
    mesh_lists(d, vertices, indices, tide_draw_texture_source(d, texture), tide_texture_filter(texture),
               tide_texture_wrap(texture));
}

// ---------------------------------------------------------------------------
// 3D

// What's in a list, as a version: its count, and what its elements are. A
// world's list says it without reading them: the hashes its heap keeps of
// its pages until they change. Elements side by side share their page with
// other blocks, whose changes count as theirs, but they're under a page, so
// copying them again costs little. One made along the way, in the scratch
// area, is read.
static uint64_t list_version(const tide_list l, const uint32_t size)
{
    const int32_t count = tide_list_count(l);
    uint64_t h = tide_hash_more(TIDE_HASH_START, &count, sizeof count);
    if (count <= 0) return tide_hash_end(h);
    if (l.at >> 30 == TIDE_IN_SCRATCH) {
        return tide_hash_end(tide_hash_more(h, tide_list_at(l, 0, size), (size_t)count * size));
    }
    tide_heap *heap = (tide_heap *)(uintptr_t)tide_heap_read(l.at >> 30);
    if (!heap) return tide_hash_end(h);
    const uint32_t block = l.at & 0x3FFFFFFFu;
    const tide_block *b = tide_heap_block(heap, block);
    if (!(b->b & TIDE_LIST_CHUNKED)) {
        const uint64_t page = tide_heap_block_hash(heap, block);
        return tide_hash_end(tide_hash_more(h, &page, sizeof page));
    }
    // In chunks, a page each: where they are, and what's in each
    const uint32_t chunks = b->b & ~TIDE_LIST_CHUNKED;
    const uint32_t *at = (const uint32_t *)(const void *)(b + 1);
    h = tide_hash_more(h, at, (size_t)chunks * sizeof(uint32_t));
    for (uint32_t i = 0; i < chunks; i++) {
        const uint64_t page = tide_heap_block_hash(heap, at[i]);
        h = tide_hash_more(h, &page, sizeof page);
    }
    return tide_hash_end(h);
}

uint32_t tide_draw_list_data(tide_draw_list *d, const tide_list list, const uint32_t element_size, const uint32_t words,
                             tide_list_words_fn *write, int32_t *count)
{
    *count = 0;
    const int32_t held = tide_list_count(list);
    if (held <= 0 || words == 0 || element_size == 0) return 0;
    const uint32_t stride = tide_data_stride(words); // Ints from an element to the next
    const uint64_t most = (uint64_t)TIDE_LIST_DATA_MOST * 4u / stride;
    if (most == 0) return 0;
    const uint32_t elements = (uint64_t)held < most ? (uint32_t)held : (uint32_t)most;
    const uint32_t pixels = (uint32_t)(((uint64_t)elements * stride + 3u) / 4u);
    const int32_t width = pixels < (uint32_t)TIDE_LIST_DATA_ACROSS ? (int32_t)pixels : TIDE_LIST_DATA_ACROSS;
    const int32_t height = (int32_t)((pixels + (uint32_t)TIDE_LIST_DATA_ACROSS - 1u) / (uint32_t)TIDE_LIST_DATA_ACROSS);
    // What its elements are, and what they're read as: another kind of list at the same place is other numbers
    uint64_t version = list_version(list, element_size);
    const uint32_t shape[2] = {element_size, words};
    version = tide_hash_end(tide_hash_more(version, shape, sizeof shape));
    // One made along the way is what's in it, wherever it is in the scratch area
    const uint64_t id = list.at >> 30 == TIDE_IN_SCRATCH ? UINT64_C(1) << 63 | version >> 1 : list.at;
    uint8_t *out;
    const uint32_t slot = tide_draw_texture_slot_as(d, TIDE_PIXELS_LIST, id, version, width, height, TIDE_TEXELS_INTS, &out);
    if (out) {
        memset(out, 0, (size_t)width * (size_t)height * 16u);
        bool copied;
        const uint8_t *flat = tide_list_flatten(list, element_size, false, &copied);
        int32_t *to = (int32_t *)(void *)out;
        for (uint32_t i = 0; i < elements; i++) write(flat + (size_t)i * element_size, to + (size_t)i * stride);
        tide_list_unflatten(list, (void *)(uintptr_t)flat, element_size, false, copied);
    }
    *count = (int32_t)elements;
    return slot;
}

uint32_t tide_draw_map_data(tide_draw_list *d, const tide_map map, const uint32_t entry_size, const uint32_t words,
                            const uint32_t key_word, tide_list_words_fn *write, int32_t *count)
{
    count[0] = count[1] = 0;
    const int32_t held = tide_list_count(map.entries);
    if (map.live == 0 || held <= 0 || words == 0 || key_word >= words || entry_size < sizeof(uint32_t)) return 0;
    const uint32_t stride = tide_data_stride(words);
    // Entries and their slots, under four an entry, have to fit
    const uint64_t most = ((uint64_t)TIDE_LIST_DATA_MOST * 4u - 16u) / (stride + 4u);
    const uint32_t entries = (uint64_t)map.live < most ? map.live : (uint32_t)most;
    uint32_t slots = 8u;
    while (slots < entries * 2u) slots *= 2u;
    const uint32_t index_at = (entries * stride + 3u) / 4u; // The index's first pixel
    const uint32_t pixels = index_at + slots / 4u;
    const int32_t width = pixels < (uint32_t)TIDE_LIST_DATA_ACROSS ? (int32_t)pixels : TIDE_LIST_DATA_ACROSS;
    const int32_t height = (int32_t)((pixels + (uint32_t)TIDE_LIST_DATA_ACROSS - 1u) / (uint32_t)TIDE_LIST_DATA_ACROSS);
    // Its entries say all of it: the CPU's index only follows from them
    uint64_t version = list_version(map.entries, entry_size);
    const uint32_t shape[4] = {entry_size, words, key_word, 0x4D415053u}; // As a map's numbers, and no list's
    version = tide_hash_end(tide_hash_more(version, shape, sizeof shape));
    const uint64_t id = map.entries.at >> 30 == TIDE_IN_SCRATCH ? UINT64_C(1) << 63 | version >> 1 : map.entries.at;
    uint8_t *out;
    const uint32_t slot = tide_draw_texture_slot_as(d, TIDE_PIXELS_LIST, id, version, width, height, TIDE_TEXELS_INTS, &out);
    if (out) {
        memset(out, 0, (size_t)width * (size_t)height * 16u);
        bool copied;
        const uint8_t *flat = tide_list_flatten(map.entries, entry_size, false, &copied);
        int32_t *to = (int32_t *)(void *)out;
        int32_t *index = to + (size_t)index_at * 4u;
        uint32_t made = 0;
        for (int32_t i = 0; i < held && made < entries; i++) {
            const uint8_t *entry = flat + (size_t)i * entry_size;
            if (!tide_map_live(entry)) continue; // A removed key's place: the GPU's entries have none
            int32_t *at = to + (size_t)made * stride;
            write(entry, at);
            uint32_t s = (uint32_t)tide_hash_i(at[key_word]) & (slots - 1u);
            while (index[s]) s = (s + 1u) & (slots - 1u);
            index[s] = (int32_t)++made;
        }
        tide_list_unflatten(map.entries, (void *)(uintptr_t)flat, entry_size, false, copied);
    }
    count[0] = (int32_t)entries;
    count[1] = (int32_t)slots;
    return slot;
}

// A grid's cells as numbers hold the box its chunks take up, within its size:
// what's outside it is zeros wherever it's read. Along each axis, where the
// box starts and how many cells it is, or false for a grid with no chunk.
static bool data_box(const tide_grid_dir *dir, const tide_grid_record *records, const tide_grid_shape *s, int64_t lo[3],
                     int64_t extent[3])
{
    int64_t hi[3] = {0, 0, 1};
    bool any = false;
    lo[0] = lo[1] = lo[2] = 0;
    for (uint32_t r = 0; r < dir->records; r++) {
        if (s->dims == 2u && records[r].chunk[2] != 0) continue;
        for (uint32_t a = 0; a < s->dims; a++) {
            const int64_t from = (int64_t)records[r].chunk[a] * ((int64_t)1 << s->shift[a]);
            const int64_t to = from + ((int64_t)1 << s->shift[a]);
            if (!any || from < lo[a]) lo[a] = from;
            if (!any || to > hi[a]) hi[a] = to;
        }
        any = true;
    }
    for (uint32_t a = 0; a < 3u; a++) {
        if (a < s->dims && dir->size[a] > 0) { // The grid ends where its size does
            if (lo[a] < 0) lo[a] = 0;
            if (hi[a] > dir->size[a]) hi[a] = dir->size[a];
        }
        extent[a] = hi[a] - lo[a];
        any &= extent[a] > 0 && lo[a] >= INT32_MIN && lo[a] <= INT32_MAX;
    }
    return any;
}

uint32_t tide_draw_grid_data(tide_draw_list *d, const tide_grid g, const tide_grid_shape *s, const uint32_t words,
                             tide_list_words_fn *write, int32_t *info)
{
    const uint32_t dims = s->dims == 3u ? 3u : 2u;
    memset(info, 0, (dims == 3u ? TIDE_GRID3_DATA_INTS : TIDE_GRID2_DATA_INTS) * sizeof *info);
    // Each shift first: three of any size could add up to a small number
    if (!g.at || (s->dims != 2u && s->dims != 3u) || s->cell == 0 || words == 0 || s->shift[0] > TIDE_HEAP_PAGE_SHIFT
        || s->shift[1] > TIDE_HEAP_PAGE_SHIFT || s->shift[2] > TIDE_HEAP_PAGE_SHIFT
        || s->shift[0] + s->shift[1] + s->shift[2] > TIDE_HEAP_PAGE_SHIFT || (dims == 2u && s->shift[2] != 0u)
        || tide_grid_chunk_bytes(s) > 1u << TIDE_HEAP_PAGE_SHIFT) {
        return 0;
    }
    // As a grid that's sampled is read (grid_texture): nothing a handle names is trusted
    tide_heap *heap = (tide_heap *)(uintptr_t)tide_heap_read(g.at >> 30);
    const bool packed = !heap && tide_pack_holds(g.at);
    if (!heap && !packed) return 0;
    const uint32_t at = g.at & 0x3FFFFFFFu;
    const uint8_t *block = span(heap, at, sizeof(tide_block) + sizeof(tide_grid_dir));
    if (!block) return 0;
    tide_grid_dir dir;
    memcpy(&dir, block + sizeof(tide_block), sizeof dir);
    if (dir.dims != dims || dir.size[0] < 0 || dir.size[1] < 0 || dir.size[2] < 0 || dir.records > dir.room) return 0;
    const tide_grid_record *records = (const tide_grid_record *)(const void *)
        span(heap, at + (uint32_t)(sizeof(tide_block) + sizeof(tide_grid_dir)), (uint64_t)dir.records * sizeof(tide_grid_record));
    if (dir.records && !records) return 0;
    for (uint32_t a = 0; a < dims; a++) info[a] = dir.size[a]; // Its size is its own, whatever of it is sent
    int64_t lo[3], extent[3];
    if (!data_box(&dir, records, s, lo, extent)) return 0; // Nothing in it: zeros everywhere
    // The box's layers side by side and under each other, each a row of
    // pixels for a row of cells: as many across as make the texture's longer
    // side the shortest
    const uint32_t stride = tide_data_stride(words);
    const int64_t most = TIDE_GRID_TEXTURE_MOST;
    int64_t wide = 0, across = 1, width = 0, height = 0;
    bool fits = extent[0] <= most * 4 && extent[1] <= most && extent[2] <= most;
    if (fits) {
        wide = (extent[0] * (int64_t)stride + 3) / 4; // Pixels a row of cells
        fits = wide <= most;
    }
    if (fits) {
        int64_t best = INT64_MAX;
        for (int64_t t = 1; t <= extent[2] && t * wide <= most; t++) {
            const int64_t down = (extent[2] + t - 1) / t * extent[1];
            const int64_t side = t * wide > down ? t * wide : down;
            if (side < best) best = side, across = t;
        }
        width = across * wide;
        height = (extent[2] + across - 1) / across * extent[1];
        fits = height <= most && width * height <= (int64_t)TIDE_GRID_DATA_PIXELS;
    }
    if (!fits) {
        static bool said;
        if (!said) {
            fprintf(stderr, "a grid's cells span more than a texture of numbers holds (%lld by %lld by %lld cells, of %u ints "
                            "each): code the GPU runs reads them as zeros\n",
                    (long long)extent[0], (long long)extent[1], (long long)extent[2], (unsigned)stride);
        }
        said = true;
        return 0;
    }
    uint64_t version;
    if (packed) {
        version = tide_pack_grid_version(g);
    } else {
        const bool whole_pages = tide_grid_chunk_bytes(s) == 1u << TIDE_HEAP_PAGE_SHIFT;
        version = tide_hash_more(TIDE_HASH_START, dir.size, sizeof dir.size);
        for (uint32_t r = 0; r < dir.records; r++) {
            const uint8_t *chunk = span(heap, records[r].block, tide_grid_chunk_bytes(s));
            const uint64_t cells = !chunk ? 0u : whole_pages ? tide_heap_block_hash(heap, records[r].block)
                                                             : tide_hash(chunk, tide_grid_chunk_bytes(s));
            version = tide_hash_more(version, records[r].chunk, sizeof records[r].chunk);
            version = tide_hash_more(version, &cells, sizeof cells);
        }
    }
    // Its cells as these numbers, laid out so, and no list's
    const int64_t shape[7] = {s->cell, words, 0x47524944, lo[0], lo[1], lo[2], across};
    version = tide_hash_end(tide_hash_more(version, shape, sizeof shape));
    uint8_t *out;
    const uint32_t slot = tide_draw_texture_slot_as(d, TIDE_PIXELS_LIST, g.at, version, (int32_t)width, (int32_t)height,
                                                    TIDE_TEXELS_INTS, &out);
    if (out) {
        memset(out, 0, (size_t)width * (size_t)height * 16u);
        const int64_t side[3] = {(int64_t)1 << s->shift[0], (int64_t)1 << s->shift[1], (int64_t)1 << s->shift[2]};
        for (uint32_t r = 0; r < dir.records; r++) {
            const uint8_t *cells = span(packed ? NULL : heap, records[r].block, tide_grid_chunk_bytes(s));
            if (!cells || (dims == 2u && records[r].chunk[2] != 0)) continue;
            // The chunk's cells that are in the box: from[a] up to to[a] of its own, and where its first is in the box
            int64_t from[3], to[3], first[3];
            bool some = true;
            for (uint32_t a = 0; a < 3u; a++) {
                first[a] = (a < dims ? (int64_t)records[r].chunk[a] * side[a] : 0) - lo[a];
                from[a] = first[a] < 0 ? -first[a] : 0;
                to[a] = first[a] + side[a] > extent[a] ? extent[a] - first[a] : side[a];
                some &= from[a] < to[a];
            }
            if (!some) continue;
            for (int64_t z = from[2]; z < to[2]; z++) {
                const int64_t layer = first[2] + z;
                const size_t left = (size_t)(layer % across * wide), top = (size_t)(layer / across * extent[1]);
                for (int64_t y = from[1]; y < to[1]; y++) {
                    int32_t *row = (int32_t *)(void *)(out + ((top + (size_t)(first[1] + y)) * (size_t)width + left) * 16u);
                    const uint8_t *in = cells + (size_t)((z << (s->shift[0] + s->shift[1])) + (y << s->shift[0])) * s->cell;
                    for (int64_t x = from[0]; x < to[0]; x++) {
                        write(in + (size_t)x * s->cell, row + (size_t)(first[0] + x) * stride);
                    }
                }
            }
        }
    }
    for (uint32_t a = 0; a < dims; a++) {
        info[dims + a] = (int32_t)lo[a];
        info[2u * dims + a] = (int32_t)extent[a];
    }
    if (dims == 3u) info[9] = (int32_t)across;
    return slot;
}

static uint32_t mesh3_slot(tide_draw_list *d, const tide_list vertices, const tide_list indices)
{
    const int32_t vertex_count = tide_list_count(vertices);
    const int32_t index_count = tide_list_count(indices);
    if (vertex_count <= 0 || index_count < 3) return 0;
    // A mesh is its two lists at their versions. A list made along the way
    // is what's in it, wherever it is in the scratch area: a view that runs
    // for each entity makes `[0, 1, 2]` in another place each time, and its
    // instances are still one mesh's
    uint64_t version = list_version(vertices, sizeof(tide_vertex3));
    const uint64_t index_version = list_version(indices, sizeof(uint32_t));
    version = tide_hash_end(tide_hash_more(version, &index_version, sizeof index_version));
    const uint64_t scratch = (uint64_t)TIDE_IN_SCRATCH << 32; // Past every list's place
    const uint64_t id = vertices.at >> 30 == TIDE_IN_SCRATCH ? scratch : vertices.at;
    const uint64_t other = indices.at >> 30 == TIDE_IN_SCRATCH ? scratch : indices.at;
    bool fill;
    const uint32_t mesh = tide_draw_mesh_slot(d, TIDE_MESH_LISTS, id, other, version, &fill);
    if (fill) {
        bool vertices_copied, indices_copied;
        tide_vertex3 *v = tide_list_flatten(vertices, sizeof(tide_vertex3), false, &vertices_copied);
        uint32_t *i = tide_list_flatten(indices, sizeof(uint32_t), false, &indices_copied);
        tide_draw_mesh_fill(d, mesh, v, (uint32_t)vertex_count, i, (uint32_t)index_count);
        tide_list_unflatten(indices, i, sizeof(uint32_t), false, indices_copied);
        tide_list_unflatten(vertices, v, sizeof(tide_vertex3), false, vertices_copied);
    }
    return mesh;
}

void tide_draw_mesh_shaded(tide_draw_list *d, const tide_shader *shader, const tide_list vertices, const tide_list indices,
                           const void *instance, const void *uniforms, const uint32_t *textures, const uint32_t sampling)
{
    tide_draw_mesh_shaded_instances(d, shader, vertices, indices, instance, 1, uniforms, textures, sampling);
}

void tide_draw_mesh_shaded_instances(tide_draw_list *d, const tide_shader *shader, const tide_list vertices,
                                     const tide_list indices, const void *instances, const uint32_t count,
                                     const void *uniforms, const uint32_t *textures, const uint32_t sampling)
{
    if (count == 0 || !shader) return;
    const int32_t vertex_count = tide_list_count(vertices);
    const int32_t index_count = tide_list_count(indices);
    if (vertex_count <= 0 || index_count < 3) return;
    // As a 3D mesh: its two lists at their versions, and how big a vertex is,
    // so no other kind of list with the same bytes is taken for it
    const uint32_t size = shader->vertex_size;
    uint64_t version = list_version(vertices, size);
    const uint64_t index_version = list_version(indices, sizeof(uint32_t));
    version = tide_hash_more(version, &index_version, sizeof index_version);
    version = tide_hash_end(tide_hash_more(version, &size, sizeof size));
    const uint64_t scratch = (uint64_t)TIDE_IN_SCRATCH << 32;
    const uint64_t id = vertices.at >> 30 == TIDE_IN_SCRATCH ? scratch : vertices.at;
    const uint64_t other = indices.at >> 30 == TIDE_IN_SCRATCH ? scratch : indices.at;
    bool fill;
    const uint32_t mesh = tide_draw_mesh_slot(d, TIDE_MESH_BYTES, id, other, version, &fill);
    if (fill) {
        bool vertices_copied, indices_copied;
        void *v = tide_list_flatten(vertices, size, false, &vertices_copied);
        uint32_t *i = tide_list_flatten(indices, sizeof(uint32_t), false, &indices_copied);
        tide_draw_mesh_fill_bytes(d, mesh, v, size, (uint32_t)vertex_count, i, (uint32_t)index_count);
        tide_list_unflatten(indices, i, sizeof(uint32_t), false, indices_copied);
        tide_list_unflatten(vertices, v, size, false, vertices_copied);
    }
    tide_draw_shaded_instances(d, shader, mesh, instances, count, uniforms, textures, sampling);
}

void tide_draw_mesh3_lists(tide_draw_list *d, const tide_list vertices, const tide_list indices,
                           const tide_float4x4 transform)
{
    tide_draw_instance(d, mesh3_slot(d, vertices, indices), transform, 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
}

void tide_draw_mesh3_texture(tide_draw_list *d, const tide_list vertices, const tide_list indices,
                             const tide_float4x4 transform, const tide_texture_ref texture)
{
    // The texture first, as the draws always found theirs before their mesh
    const uint32_t pixels = tide_draw_texture_source(d, texture);
    tide_draw_instance(d, mesh3_slot(d, vertices, indices), transform, pixels, tide_texture_filter(texture),
                       tide_texture_wrap(texture));
}

static void mesh3_instances_lists(tide_draw_list *d, const tide_list vertices, const tide_list indices,
                                  const tide_list transforms, const uint32_t texture, const tide_filter filter,
                                  const tide_wrap wrap)
{
    const int32_t count = tide_list_count(transforms);
    if (count <= 0) return;
    const uint32_t mesh = mesh3_slot(d, vertices, indices);
    if (mesh == 0) return;
    bool copied;
    tide_float4x4 *flat = tide_list_flatten(transforms, sizeof(tide_float4x4), false, &copied);
    tide_draw_instances(d, mesh, flat, (uint32_t)count, texture, filter, wrap);
    tide_list_unflatten(transforms, flat, sizeof(tide_float4x4), false, copied);
}

void tide_draw_mesh3_instances_lists(tide_draw_list *d, const tide_list vertices, const tide_list indices,
                                    const tide_list transforms)
{
    mesh3_instances_lists(d, vertices, indices, transforms, 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
}

void tide_draw_mesh3_instances_texture(tide_draw_list *d, const tide_list vertices, const tide_list indices,
                                       const tide_list transforms, const tide_texture_ref texture)
{
    if (tide_list_count(transforms) <= 0) return;
    mesh3_instances_lists(d, vertices, indices, transforms, tide_draw_texture_source(d, texture),
                          tide_texture_filter(texture), tide_wrap_of((int32_t)tide_texture_wrap(texture)));
}
