#include "tide/mesh.h"

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

// ---------------------------------------------------------------------------
// What changed
//
// A grid's or a list's texture is megabytes, of which a frame changes a
// little: a brush's cells, a few entities' elements. Its writers go by the
// hashes a heap keeps of its pages, a chunk's each, to leave the chunks that
// didn't change alone, and compare what they make of the others with what the
// texture holds, so that the draw list knows the pixels that differ
// (tide_draw_texture_edit), and a renderer sends the GPU those.

// Where `count` runs of `unit` bytes differ between `a` and `b`: from *first
// up to *end. False where none does.
static bool differs(const uint8_t *a, const uint8_t *b, const size_t count, const size_t unit, size_t *first, size_t *end)
{
    if (memcmp(a, b, count * unit) == 0) return false;
    size_t low = 0, high = count;
    while (memcmp(a + low * unit, b + low * unit, unit) == 0) low++;
    while (memcmp(a + (high - 1u) * unit, b + (high - 1u) * unit, unit) == 0) high--;
    *first = low;
    *end = high;
    return true;
}

// Bytes `from` up to `to` of a texture's rows `top` up to `bottom` changed: the pixels they're in.
static void changed_bytes(tide_draw_list *d, const uint32_t slot, const size_t from, const size_t to, const int32_t top,
                          const int32_t bottom)
{
    const size_t pixel = tide_texels_bytes(d->textures[slot - 1u].format);
    const int32_t x = (int32_t)(from / pixel), end = (int32_t)((to + pixel - 1u) / pixel);
    tide_draw_texture_changed(d, slot, x, top, end - x, bottom - top);
}

// Ints `from` up to `to` of a texture of ints changed, counted through its rows as they're read: the rows they're in,
// or the pixels of the one row.
static void changed_ints(tide_draw_list *d, const uint32_t slot, const uint64_t from, const uint64_t to)
{
    if (to <= from) return;
    const uint64_t width = (uint64_t)d->textures[slot - 1u].width;
    const uint64_t first = from / 4u, end = (to + 3u) / 4u; // Pixels
    const int32_t top = (int32_t)(first / width), bottom = (int32_t)((end - 1u) / width);
    if (top == bottom) tide_draw_texture_changed(d, slot, (int32_t)(first % width), top, (int32_t)(end - first), 1);
    else tide_draw_texture_changed(d, slot, 0, top, (int32_t)width, bottom - top + 1);
}

// A texture's pixels become `fresh`, another image of it, row by row where they differ, which is said.
static void rows_changed(tide_draw_list *d, const uint32_t slot, uint8_t *pixels, const uint8_t *fresh)
{
    const tide_draw_texture *t = &d->textures[slot - 1u];
    const size_t pixel = tide_texels_bytes(t->format), row = (size_t)t->width * pixel;
    for (int32_t y = 0; y < t->height; y++) {
        size_t first, end;
        if (!differs(pixels + (size_t)y * row, fresh + (size_t)y * row, (size_t)t->width, pixel, &first, &end)) continue;
        memcpy(pixels + (size_t)y * row + first * pixel, fresh + (size_t)y * row + first * pixel, (end - first) * pixel);
        tide_draw_texture_changed(d, slot, (int32_t)first, y, (int32_t)(end - first), 1);
    }
}

// ---------------------------------------------------------------------------
// Grids

// A grid as a draw reads it: its directory and its chunks' records, each
// checked to be where a grid's are (span), as the handle may be one a Texture
// kept past its grid.
typedef struct grid_seen {
    tide_heap *heap; // NULL for a grid of the pack's (tide/pack.h): its chunks are in the scratch area's slots
    tide_grid_dir dir;
    const tide_grid_record *records;
    uint32_t across, down; // Chunks along each axis
} grid_seen;

static bool see_grid(const tide_grid g, const tide_grid_shape *s, grid_seen *v)
{
    // A chunk is a page at the most, which is also what keeps its size in an int
    if (!g.at || s->dims != 2u || s->cell == 0u || s->shift[0] + s->shift[1] > TIDE_HEAP_PAGE_SHIFT || s->shift[2] != 0u
        || tide_grid_chunk_bytes(s) > 1u << TIDE_HEAP_PAGE_SHIFT) {
        return false;
    }
    // The confirmed world's too: only its pages' hashes are asked of it, which it keeps
    v->heap = (tide_heap *)(uintptr_t)tide_heap_read(g.at >> 30);
    if (!v->heap && !tide_pack_holds(g.at)) return false;
    const uint32_t at = g.at & 0x3FFFFFFFu;
    const uint8_t *block = span(v->heap, at, sizeof(tide_block) + sizeof(tide_grid_dir));
    if (!block) return false;
    memcpy(&v->dir, block + sizeof(tide_block), sizeof v->dir);
    const tide_grid_dir *dir = &v->dir;
    if (dir->dims != 2u || dir->size[0] <= 0 || dir->size[1] <= 0 || dir->records > dir->room) return false;
    // A grid with a size on both axes has as many chunks as cover it, and no more of them than that
    const uint64_t across = ((uint64_t)dir->size[0] + (1u << s->shift[0]) - 1u) >> s->shift[0];
    const uint64_t down = ((uint64_t)dir->size[1] + (1u << s->shift[1]) - 1u) >> s->shift[1];
    if (dir->records > across * down) return false;
    if (dir->flat && ((uint64_t)dir->chunks[0] != across || (uint64_t)dir->chunks[1] != down)) return false;
    v->records = (const tide_grid_record *)(const void *)
        span(v->heap, at + (uint32_t)(sizeof(tide_block) + sizeof(tide_grid_dir)), (uint64_t)dir->records * sizeof(tide_grid_record));
    if (dir->records && !v->records) return false;
    v->across = (uint32_t)across;
    v->down = (uint32_t)down;
    return true;
}

// What a chunk of a heap's grid holds, without reading it: the hash its heap
// keeps of its page until the page changes. Other blocks in the page change it
// too, where a chunk is under a page: such a chunk is then copied again, and
// found the same. Never 0, which is no chunk's.
static uint64_t chunk_hash(const grid_seen *v, const tide_grid_shape *s, const tide_grid_record *record)
{
    if (!span(v->heap, record->block, tide_grid_chunk_bytes(s))) return 0;
    const uint64_t hash = tide_heap_block_hash(v->heap, record->block);
    return hash ? hash : 1u;
}

// What a heap's grid holds, as a number to go on hashing: its size, and each chunk's place and hash.
static uint64_t grid_hash(const grid_seen *v, const tide_grid_shape *s)
{
    uint64_t hash = tide_hash_more(TIDE_HASH_START, v->dir.size, sizeof v->dir.size);
    for (uint32_t r = 0; r < v->dir.records; r++) {
        const uint64_t cells = chunk_hash(v, s, &v->records[r]);
        hash = tide_hash_more(hash, v->records[r].chunk, sizeof v->records[r].chunk);
        hash = tide_hash_more(hash, &cells, sizeof cells);
    }
    return hash;
}

// How a grid's cells become a texture's bytes, a run of a chunk's row at a time.
typedef struct cells_writer {
    void (*row)(const struct cells_writer *w, const uint8_t *cells, int32_t count, uint8_t *out);
    uint32_t bytes;            // Of the texture, a cell
    bool raw;                  // Colors: with no alpha worked into them
    tide_list_words_fn *write; // Numbers: what writes a cell's...
    uint32_t cell;             // ...and the bytes a cell is in the grid
} cells_writer;

// Colors, a byte a channel
static void color_cells(const cells_writer *w, const uint8_t *cells, const int32_t count, uint8_t *out)
{
    const tide_color *row = (const tide_color *)(const void *)cells;
    for (int32_t x = 0; x < count; x++, out += 4) {
        const float alpha = unit(row[x].a), worked = w->raw ? 1.0f : alpha;
        out[0] = channel(row[x].r, worked);
        out[1] = channel(row[x].g, worked);
        out[2] = channel(row[x].b, worked);
        out[3] = channel(1.0f, alpha);
    }
}

// Floats, 16 bits each
static void float_cells(const cells_writer *w, const uint8_t *cells, const int32_t count, uint8_t *out)
{
    (void)w;
    const float *row = (const float *)(const void *)cells;
    for (int32_t x = 0; x < count; x++, out += 2) {
        const uint16_t half = tide_half_from_float(row[x]);
        memcpy(out, &half, sizeof half);
    }
}

// Each cell's numbers, into ints that are zeros past them
static void number_cells(const cells_writer *w, const uint8_t *cells, const int32_t count, uint8_t *out)
{
    for (int32_t x = 0; x < count; x++) w->write(cells + (size_t)x * w->cell, (int32_t *)(void *)(out + (size_t)x * w->bytes));
}

enum { GRID_PARTS_MOST = 1 << 20 }; // The most chunks a grid's texture is told apart by

// A grid's cells into its texture's pixels, zeros where it has no chunk: all
// of them, or where the pixels are `kept`, the chunks whose hashes aren't the
// ones the texture was last written from, each row of them where it differs.
static void grid_cells(tide_draw_list *d, const uint32_t slot, uint8_t *pixels, bool kept, const grid_seen *v,
                       const tide_grid_shape *s, const cells_writer *w)
{
    const int32_t width = v->dir.size[0], height = v->dir.size[1];
    const size_t row = (size_t)d->textures[slot - 1u].width * tide_texels_bytes(d->textures[slot - 1u].format);
    const int32_t across = 1 << s->shift[0], down = 1 << s->shift[1];
    const uint64_t places = (uint64_t)v->across * v->down;
    // Each chunk's hash by its place, as the texture was last written, and after them, as the grid is now
    uint64_t *parts = v->heap && places <= GRID_PARTS_MOST ? tide_draw_texture_parts(d, slot, (uint32_t)places * 2u) : NULL;
    uint64_t *now = parts ? parts + places : NULL;
    uint64_t *chunks = tide_draw_texture_used(d, slot); // How many it was written from
    if (now) memset(now, 0, (size_t)places * sizeof *now);
    if (kept && !parts) { // Nothing says which chunks changed, or went: all of it again
        kept = false;
        tide_draw_texture_changed(d, slot, 0, 0, d->textures[slot - 1u].width, height);
    }
    if (!kept) memset(pixels, 0, row * (size_t)height);
    const uint32_t mark = tide_scratch_mark();
    uint8_t *made = kept ? tide_scratch_memory((size_t)across * w->bytes) : NULL;
    uint64_t found = 0, had = 0;
    for (uint32_t r = 0; r < v->dir.records; r++) {
        const tide_grid_record *record = &v->records[r];
        int32_t x0, y0;
        const uint8_t *cells = span(v->heap, record->block, tide_grid_chunk_bytes(s));
        if (!cells || !chunk_place(record, s, width, height, &x0, &y0)) continue;
        if (now) {
            const uint64_t place = (uint64_t)(y0 >> s->shift[1]) * v->across + (uint64_t)(x0 >> s->shift[0]);
            found += !now[place];
            had += !now[place] && parts[place];
            now[place] = chunk_hash(v, s, record);
            if (kept && parts[place] == now[place]) continue; // As it was
        }
        const int32_t count = across < width - x0 ? across : width - x0;
        const size_t run = (size_t)count * w->bytes, at = (size_t)x0 * w->bytes;
        size_t left = (size_t)count, right = 0;
        int32_t top = down, bottom = 0;
        for (int32_t y = 0; y < down && y0 + y < height; y++) {
            uint8_t *out = pixels + (size_t)(y0 + y) * row + at;
            const uint8_t *in = cells + (size_t)y * (size_t)across * s->cell;
            if (!kept) {
                w->row(w, in, count, out);
                continue;
            }
            memset(made, 0, run);
            w->row(w, in, count, made);
            size_t first, end;
            if (!differs(out, made, (size_t)count, w->bytes, &first, &end)) continue;
            memcpy(out + first * w->bytes, made + first * w->bytes, (end - first) * w->bytes);
            if (first < left) left = first;
            if (end > right) right = end;
            if (y < top) top = y;
            bottom = y + 1;
        }
        if (right > left) changed_bytes(d, slot, at + left * w->bytes, at + right * w->bytes, y0 + top, y0 + bottom);
    }
    // A chunk that went is zeros again
    for (uint64_t place = 0; kept && had < *chunks && place < places; place++) {
        if (!parts[place] || now[place]) continue;
        const int32_t x0 = (int32_t)(place % v->across) << s->shift[0], y0 = (int32_t)(place / v->across) << s->shift[1];
        const int32_t count = across < width - x0 ? across : width - x0, rows = down < height - y0 ? down : height - y0;
        const size_t run = (size_t)count * w->bytes, at = (size_t)x0 * w->bytes;
        for (int32_t y = 0; y < rows; y++) memset(pixels + (size_t)(y0 + y) * row + at, 0, run);
        changed_bytes(d, slot, at, at + run, y0, y0 + rows);
    }
    if (parts) memcpy(parts, now, (size_t)places * sizeof *parts);
    if (chunks) *chunks = found;
    tide_scratch_reset(mark);
}

// The kinds of writer here (tide_draw_texture_edit)
#define KIND_COLORS UINT64_C(0x47434F4C)
#define KIND_FLOATS UINT64_C(0x47464C54)
#define KIND_RAW UINT64_C(0x47524157)

static uint32_t grid_texture(tide_draw_list *d, const tide_grid g, const tide_grid_shape *s, const bool raw)
{
    const bool colors = s->cell == sizeof(tide_color);
    grid_seen v;
    if ((!colors && s->cell != sizeof(float)) || !see_grid(g, s, &v)) return 0;
    // No GPU has a texture bigger, and bytes that aren't a grid's could say any size
    if (v.dir.size[0] > TIDE_GRID_TEXTURE_MOST || v.dir.size[1] > TIDE_GRID_TEXTURE_MOST) return 0;
    // A grid of the pack's has its file's version, in its directory's header, as the builder put it
    uint64_t version = v.heap ? tide_hash_end(grid_hash(&v, s)) : tide_pack_grid_version(g);
    if (raw) version ^= UINT64_C(0x5241575241575241); // Other bytes than the same cells as colors
    const cells_writer w = {.row = colors ? color_cells : float_cells, .bytes = colors ? 4u : 2u, .raw = raw};
    uint8_t *pixels;
    bool kept;
    const uint32_t slot = tide_draw_texture_edit(d, TIDE_PIXELS_GRID, g.at, version, v.dir.size[0], v.dir.size[1],
                                                 colors ? TIDE_TEXELS_BYTES : TIDE_TEXELS_HALF,
                                                 !colors ? KIND_FLOATS : raw ? KIND_RAW : KIND_COLORS, &pixels, &kept);
    if (pixels) grid_cells(d, slot, pixels, kept, &v, s, &w);
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

// The numbers of a list's first `elements`, `stride` ints from one to the
// next, into its texture's ints: all of them, or where the ints are `kept`,
// those of the chunks whose hashes aren't the ones the texture was last
// written from, from the first element that differs to the last.
static void list_numbers(tide_draw_list *d, const uint32_t slot, int32_t *to, const bool kept, const tide_list list,
                         const uint32_t size, const uint32_t elements, const uint32_t stride, tide_list_words_fn *write)
{
    const tide_draw_texture *t = &d->textures[slot - 1u];
    const uint64_t filled = (uint64_t)elements * stride;
    uint64_t *used = tide_draw_texture_used(d, slot); // The ints it had in use
    if (!kept) {
        memset(to, 0, (size_t)t->width * (size_t)t->height * 16u);
    } else if (*used > filled) { // Fewer elements than it had: zeros past them
        memset(to + filled, 0, (size_t)(*used - filled) * sizeof *to);
        changed_ints(d, slot, filled, *used);
    }
    *used = filled;
    // A world's big list is in chunks, a page each, whose hashes say which changed. Any other is all in one place
    tide_heap *heap = list.at >> 30 == TIDE_IN_SCRATCH ? NULL : (tide_heap *)(uintptr_t)tide_heap_read(list.at >> 30);
    const tide_block *b = tide_block_at(list.at);
    const bool chunked = heap && b && (b->b & TIDE_LIST_CHUNKED) && size <= TIDE_LIST_CHUNK;
    const uint32_t per = chunked ? TIDE_LIST_CHUNK / size : elements;
    const uint32_t runs = (elements + per - 1u) / per;
    const uint32_t *chunks = chunked ? (const uint32_t *)(const void *)(b + 1) : NULL;
    // A chunk's hash is kept only while each write here keeps it: a list with no chunks keeps none, so that one
    // that comes to have chunks again finds none from before
    uint64_t *parts = tide_draw_texture_parts(d, slot, chunked ? runs : 0u);
    const uint32_t mark = tide_scratch_mark();
    int32_t *made = kept ? tide_scratch_memory((size_t)stride * sizeof *made) : NULL;
    for (uint32_t r = 0; r < runs; r++) {
        const uint32_t first = r * per, count = per < elements - first ? per : elements - first;
        if (parts) {
            uint64_t hash = tide_heap_block_hash(heap, chunks[r]);
            if (!hash) hash = 1u; // 0 is a chunk it has nothing of
            const bool same = kept && parts[r] == hash;
            parts[r] = hash;
            if (same) continue;
        }
        const uint8_t *from = tide_list_at(list, (int32_t)first, size); // Side by side from here
        if (!from) break;
        int32_t *at = to + (size_t)first * stride;
        uint32_t low = 0, high = count;
        if (kept) { // The elements at its start and at its end that are as they were are left
            for (; low < count; low++) {
                memset(made, 0, (size_t)stride * sizeof *made);
                write(from + (size_t)low * size, made);
                if (memcmp(at + (size_t)low * stride, made, (size_t)stride * sizeof *made) != 0) break;
            }
            for (; high > low + 1u; high--) {
                memset(made, 0, (size_t)stride * sizeof *made);
                write(from + (size_t)(high - 1u) * size, made);
                if (memcmp(at + (size_t)(high - 1u) * stride, made, (size_t)stride * sizeof *made) != 0) break;
            }
            if (low == count) continue;
            changed_ints(d, slot, (uint64_t)(first + low) * stride, (uint64_t)(first + high) * stride);
        }
        // An element's ints past its numbers are zeros already: nothing ever writes them
        for (uint32_t i = low; i < high; i++) write(from + (size_t)i * size, at + (size_t)i * stride);
    }
    tide_scratch_reset(mark);
}

// What a texture of numbers is of, as its writer's kind and in its version:
// another layout of the same list is other numbers.
static uint64_t numbers_kind(const uint32_t what, const uint32_t size, const uint32_t words, const uint32_t extra,
                             const uint32_t layout)
{
    const uint32_t shape[5] = {what, size, words, extra, layout};
    return tide_hash(shape, sizeof shape);
}

// Whose numbers a texture's are: the list's or the grid's place and how they're laid out, so that two
// layouts of one list are two textures.
static uint64_t numbers_id(const uint32_t at, const uint32_t layout)
{
    return (uint64_t)(layout & 0x7FFFFFFFu) << 32 | at;
}

uint32_t tide_draw_list_data(tide_draw_list *d, const tide_list list, const uint32_t element_size, const uint32_t words,
                             const uint32_t layout, tide_list_words_fn *write, int32_t *count)
{
    *count = 0;
    const int32_t held = tide_list_count(list);
    if (held <= 0 || element_size == 0) return 0;
    if (words == 0) { // Nothing is read of its elements: they're only counted
        *count = held;
        return 0;
    }
    const uint32_t stride = tide_data_stride(words); // Ints from an element to the next
    const uint64_t most = (uint64_t)TIDE_LIST_DATA_MOST * 4u / stride;
    if (most == 0) return 0;
    const uint32_t elements = (uint64_t)held < most ? (uint32_t)held : (uint32_t)most;
    const uint32_t pixels = (uint32_t)(((uint64_t)elements * stride + 3u) / 4u);
    const int32_t width = pixels < (uint32_t)TIDE_LIST_DATA_ACROSS ? (int32_t)pixels : TIDE_LIST_DATA_ACROSS;
    const int32_t height = (int32_t)((pixels + (uint32_t)TIDE_LIST_DATA_ACROSS - 1u) / (uint32_t)TIDE_LIST_DATA_ACROSS);
    // What its elements are, and what they're read as: another kind of list at the same place is other numbers
    const uint64_t kind = numbers_kind(0x4C495354u, element_size, words, 0, layout);
    const uint64_t version = tide_hash_end(tide_hash_more(list_version(list, element_size), &kind, sizeof kind));
    // One made along the way is what's in it, wherever it is in the scratch area
    const uint64_t id = list.at >> 30 == TIDE_IN_SCRATCH ? UINT64_C(1) << 63 | version >> 1 : numbers_id(list.at, layout);
    uint8_t *out;
    bool kept;
    const uint32_t slot = tide_draw_texture_edit(d, TIDE_PIXELS_LIST, id, version, width, height, TIDE_TEXELS_INTS, kind,
                                                 &out, &kept);
    if (out) list_numbers(d, slot, (int32_t *)(void *)out, kept, list, element_size, elements, stride, write);
    *count = (int32_t)elements;
    return slot;
}

// A map's live entries as numbers, then the index a shader finds a key through, into `to`, which is zeros.
static void map_numbers(int32_t *to, const tide_map map, const uint32_t entry_size, const uint32_t entries,
                        const uint32_t stride, const uint32_t key_word, const uint32_t slots, tide_list_words_fn *write)
{
    const int32_t held = tide_list_count(map.entries);
    int32_t *index = to + (size_t)((entries * stride + 3u) / 4u) * 4u; // From the pixel after the entries
    bool copied;
    const uint8_t *flat = tide_list_flatten(map.entries, entry_size, false, &copied);
    uint32_t made = 0;
    for (int32_t i = 0; flat && i < held && made < entries; i++) {
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

uint32_t tide_draw_map_data(tide_draw_list *d, const tide_map map, const uint32_t entry_size, const uint32_t words,
                            const uint32_t key_word, const uint32_t layout, tide_list_words_fn *write, int32_t *count)
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
    const uint32_t pixels = (entries * stride + 3u) / 4u + slots / 4u;
    const int32_t width = pixels < (uint32_t)TIDE_LIST_DATA_ACROSS ? (int32_t)pixels : TIDE_LIST_DATA_ACROSS;
    const int32_t height = (int32_t)((pixels + (uint32_t)TIDE_LIST_DATA_ACROSS - 1u) / (uint32_t)TIDE_LIST_DATA_ACROSS);
    // Its entries say all of it: the CPU's index only follows from them. As a map's numbers, and no list's
    const uint64_t kind = numbers_kind(0x4D415053u, entry_size, words, key_word, layout);
    const uint64_t version = tide_hash_end(tide_hash_more(list_version(map.entries, entry_size), &kind, sizeof kind));
    const uint64_t id = map.entries.at >> 30 == TIDE_IN_SCRATCH ? UINT64_C(1) << 63 | version >> 1
                                                                : numbers_id(map.entries.at, layout);
    uint8_t *out;
    bool kept;
    const uint32_t slot = tide_draw_texture_edit(d, TIDE_PIXELS_LIST, id, version, width, height, TIDE_TEXELS_INTS, kind,
                                                 &out, &kept);
    const size_t bytes = (size_t)width * (size_t)height * 16u;
    if (out && !kept) {
        memset(out, 0, bytes);
        map_numbers((int32_t *)(void *)out, map, entry_size, entries, stride, key_word, slots, write);
    } else if (out) {
        // A key that's added or removed moves the entries after it and what the index says of them, so it's all
        // made again, apart, and the rows that came out otherwise are the texture's
        const uint32_t mark = tide_scratch_mark();
        uint8_t *fresh = tide_scratch_memory(bytes);
        memset(fresh, 0, bytes);
        map_numbers((int32_t *)(void *)fresh, map, entry_size, entries, stride, key_word, slots, write);
        rows_changed(d, slot, out, fresh);
        tide_scratch_reset(mark);
    }
    count[0] = (int32_t)entries;
    count[1] = (int32_t)slots;
    return slot;
}

uint32_t tide_draw_grid_data(tide_draw_list *d, const tide_grid g, const tide_grid_shape *s, const uint32_t words,
                             const uint32_t layout, tide_list_words_fn *write, int32_t *size)
{
    size[0] = size[1] = 0;
    grid_seen v; // As a grid that's sampled is read (grid_texture): nothing a handle names is trusted
    if (!see_grid(g, s, &v)) return 0;
    const uint32_t stride = tide_data_stride(words);
    const uint64_t wide = ((uint64_t)v.dir.size[0] * stride + 3u) / 4u; // Pixels a row
    if (wide > TIDE_GRID_TEXTURE_MOST || v.dir.size[1] > TIDE_GRID_TEXTURE_MOST) return 0;
    if (words == 0) { // Nothing is read of its cells: only its size is asked
        size[0] = v.dir.size[0];
        size[1] = v.dir.size[1];
        return 0;
    }
    // Its cells as these numbers, and no list's
    const uint64_t kind = numbers_kind(0x47524944u, s->cell, words, 0, layout);
    const uint64_t cells = v.heap ? grid_hash(&v, s) : tide_pack_grid_version(g);
    const uint64_t version = tide_hash_end(tide_hash_more(cells, &kind, sizeof kind));
    const cells_writer w = {.row = number_cells, .bytes = stride * 4u, .write = write, .cell = s->cell};
    uint8_t *out;
    bool kept;
    const uint32_t slot = tide_draw_texture_edit(d, TIDE_PIXELS_LIST, numbers_id(g.at, layout), version, (int32_t)wide,
                                                 v.dir.size[1], TIDE_TEXELS_INTS, kind, &out, &kept);
    if (out) grid_cells(d, slot, out, kept, &v, s, &w);
    size[0] = v.dir.size[0];
    size[1] = v.dir.size[1];
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
